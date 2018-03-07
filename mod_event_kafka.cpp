/*
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2014, Anthony Minessale II <anthm@freeswitch.org>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Anthony Minessale II <anthm@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Based on mod_skel by
 * Anthony Minessale II <anthm@freeswitch.org>
 *
 * Contributor(s):
 * 
 * Kinshuk Bairagi <me@kinshuk.in>
 * Anthony Minessale II <anthm@freeswitch.org>
 * Neal Horman <neal at wanlink dot com>
 *
 *
 * mod_event_kafka.c -- Sends FreeSWITCH events to an Kafka broker
 *
 */

#include <iostream>
#include <memory>
#include <exception>
#include <stdexcept>
#include <string>
#include <sstream>
#include <switch.h>

#include "mod_event_kafka.hpp"
 
namespace mod_event_kafka {

    static switch_xml_config_item_t instructions[] = {
        SWITCH_CONFIG_ITEM("bootstrap-servers", SWITCH_CONFIG_STRING, CONFIG_RELOADABLE, &globals.brokers,
                            "localhost:9092", NULL, "bootstrap-servers", "Kafka Bootstrap Brokers"),
        SWITCH_CONFIG_ITEM("topic-prefix", SWITCH_CONFIG_STRING, CONFIG_RELOADABLE, &globals.topic_prefix,
                            "fs", NULL, "topic-prefix", "Kafka Topic Prefix"),
        SWITCH_CONFIG_ITEM_END()
    };

    static switch_status_t load_config(switch_bool_t reload)
    {
        memset(&globals, 0, sizeof(globals));
        if (switch_xml_config_parse_module_settings("event_kafka.conf", reload, instructions) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Could not open event_kafka.conf\n");
            return SWITCH_STATUS_FALSE;
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "event_kafka.conf reloaded :: brokers : %s, prefix %s \n", globals.brokers, globals.topic_prefix);
        }
        return SWITCH_STATUS_SUCCESS;
    }

    class KafkaDeliveryReportCallback : public RdKafka::DeliveryReportCb {
        public:
        void dr_cb (RdKafka::Message &message) {
            if (message.err())
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Message delivery failed %s \n",message.errstr().c_str());
            else
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Message delivered (%zd bytes, partition %d) \n",message.len(), message.partition());
        }
    };

    class KafkaEventPublisher {
       
        public:
        KafkaEventPublisher(){

            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "KafkaEventPublisher Initialising...");

            load_config(SWITCH_FALSE);
            
            RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
            RdKafka::Conf *tconf = RdKafka::Conf::create(RdKafka::Conf::CONF_TOPIC);

            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, globals.brokers);

            if (conf->set("metadata.broker.list",  globals.brokers, errstr) != RdKafka::Conf::CONF_OK) {
               switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr.c_str());
            }

            if (conf->set("queue.buffering.max.messages", "5", errstr) != RdKafka::Conf::CONF_OK) {
               switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr.c_str());
            }
            
            /* Set the delivery report callback.
            * This callback will be called once per message to inform
            * the application if delivery succeeded or failed.
            * See dr_msg_cb() above. */
            KafkaDeliveryReportCallback ex_dr_cb;
            conf->set("dr_cb", &ex_dr_cb, errstr);

            /*
            * Create producer instance.
            *
            * NOTE: rd_kafka_new() takes ownership of the conf object
            *       and the application must not reference it again after
            *       this call.
            */
            producer = RdKafka::Producer::create(conf, errstr);
            if (!producer) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create new producer: %s", errstr.c_str());
            }

            /* Create topic object that will be reused for each message
            * produced.
            *
            * Both the producer instance (rd_kafka_t) and topic objects (topic_t)
            * are long-lived objects that should be reused as much as possible.
            */
            std::string topic_str = std::string(globals.topic_prefix)+ "hostname";
            topic = RdKafka::Topic::create(producer, topic_str, tconf, errstr);
            if (!topic) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create topic %s object: %s", topic_str.c_str(), errstr.c_str());
            }

            _initialized = 1;
        }

        void PublishEvent(switch_event_t *event) {
            
            char *event_json = (char*)malloc(sizeof(char));
            switch_event_serialize_json(event, &event_json);
            int len = strlen(event_json);

            if(_initialized){
                RdKafka::ErrorCode resp = producer->produce(topic, RdKafka::Topic::PARTITION_UA,
                        RdKafka::Producer::RK_MSG_COPY /* Copy payload */,
                        event_json, len,
                        NULL, NULL);

                if (resp != RdKafka::ERR_NO_ERROR){
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to produce, with error %s", RdKafka::err2str(resp).c_str());
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, event_json);
                } else {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,"Produced message (%d bytes)\n ", len);
                }
                producer->poll(0);
            } else {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "PublishEvent without KafkaPublisher");
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, event_json);
            }
        }

        ~KafkaEventPublisher(){
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "KafkaEventPublisher Destroyed");
        }

        private:

        std::string errstr; 
        bool _initialized = 0;
        RdKafka::Producer *producer;
        RdKafka::Topic *topic;
    };

    class KafkaModule {
    public:

        KafkaModule(switch_loadable_module_interface_t **module_interface, switch_memory_pool_t *pool): _publisher() {
             
            // Subscribe to all switch events of any subclass
            // Store a pointer to ourself in the user data
            if (switch_event_bind_removable(modname, SWITCH_EVENT_ALL, SWITCH_EVENT_SUBCLASS_ANY, event_handler,
                                            static_cast<void*>(&_publisher), &_node)
                != SWITCH_STATUS_SUCCESS) {
                throw std::runtime_error("Couldn't bind to switch events.");
            }
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Subscribed to events\n");

            // Create our module interface registration
            *module_interface = switch_loadable_module_create_module_interface(pool, modname);

            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Module loaded\n");

        };

        void Shutdown() {
            // Send term message
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                              "Shutdown requested, sending term message to runloop\n");
        }

        ~KafkaModule() {
            // Unsubscribe from the switch events
            switch_event_unbind(&_node);
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Module shut down\n");
        }

    private:

        // Dispatches events to the publisher
        static void event_handler(switch_event_t *event) {
            try {
                KafkaEventPublisher *publisher = static_cast<KafkaEventPublisher*>(event->bind_user_data);
                publisher->PublishEvent(event);
            } catch (std::exception ex) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Error publishing event via 0MQ: %s\n",
                                  ex.what());
            } catch (...) { // Exceptions must not propogate to C caller
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Unknown error publishing event via 0MQ\n");
            }
        }

        switch_event_node_t *_node;
        KafkaEventPublisher _publisher;

    };


    //*****************************//
    //           GLOBALS           //
    //*****************************//
    std::auto_ptr<KafkaModule> module;


    //*****************************//
    //  Module interface funtions  //
    //*****************************//
    SWITCH_MODULE_LOAD_FUNCTION(mod_event_kafka_load) {
            try {
                module.reset(new KafkaModule(module_interface, pool));
                return SWITCH_STATUS_SUCCESS;
            } catch(...) { // Exceptions must not propogate to C caller
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error loading Kafka Event module\n");
                return SWITCH_STATUS_GENERR;
            }

    }


    SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_event_kafka_shutdown) {
            try {
                // Tell the module to shutdown
                module->Shutdown();
                // Free the module object
                module.reset();
            } catch(std::exception &ex) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error shutting down Kafka Event module: %s\n",
                                  ex.what());
            } catch(...) { // Exceptions must not propogate to C caller
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                  "Unknown error shutting down Kafka Event module\n");
            }
            return SWITCH_STATUS_SUCCESS;
    }

}

