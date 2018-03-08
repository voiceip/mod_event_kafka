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
#include <string>
#include <thread>
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
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "event_kafka.conf loaded [brokers: %s, prefix: %s]", globals.brokers, globals.topic_prefix);
        }
        return SWITCH_STATUS_SUCCESS;
    }


    class KafkaEventPublisher {

        static void dr_msg_cb (rd_kafka_t *rk, const rd_kafka_message_t *rkmessage, void *opaque) {
            if (rkmessage->err)
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, " Message delivery failed %s \n",rd_kafka_err2str(rkmessage->err));
            else
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,  "Message delivered (%zd bytes, partition %d, offset  %" PRId64 ") \n",rkmessage->len, rkmessage->partition, rkmessage->offset);
        }
    };

        public:
        KafkaEventPublisher(){

            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "KafkaEventPublisher Initialising...");

            load_config(SWITCH_FALSE);
            
            conf = rd_kafka_conf_new();

            if (rd_kafka_conf_set(conf, "metadata.broker.list", globals.brokers, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
               switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr);
            }

            if (rd_kafka_conf_set(conf, "queue.buffering.max.messages", "5", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr);
            }

            rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);

            producer = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
            if (!producer) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create new producer: %s", errstr);
            }

            std::string topic_str = std::string(globals.topic_prefix) + "_" + std::string(switch_core_get_switchname());
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "KafkaEventPublisher Topic : %s", topic_str.c_str());

            // topic = RdKafka::Topic::create(producer, topic_str, tconf, errstr);
            topic = rd_kafka_topic_new(producer, topic_str.c_str(), NULL);
            if (!topic) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create topic %s object: %s", topic_str.c_str(),  rd_kafka_err2str(rd_kafka_last_error()));
            }

                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create topic %s object: %s", topic.c_str(),  rd_kafka_err2str(rd_kafka_last_error()));
                    rd_kafka_destroy(rk);
            }

            _initialized = 1;
        }

        void PublishEvent(switch_event_t *event) {

            char *event_json = (char*)malloc(sizeof(char));
            switch_event_serialize_json(event, &event_json);
            size_t len = strlen(event_json);

            if(_initialized){
                int resp = send(event_json,0);
                if (resp == -1){
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to produce, with error %s", rd_kafka_err2str(rd_kafka_last_error()));
                } else {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"Produced message (%zu bytes)", len);
                }
                rd_kafka_poll(producer, 0);
            } else {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "PublishEvent without active KafkaPublisher");
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, event_json);
            }
        }

        void Shutdown(){
            //flush within 100ms
            rd_kafka_flush(producer, 100);
        }

        ~KafkaEventPublisher(){
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "KafkaEventPublisher Destroyed");
            rd_kafka_topic_destroy(topic);
            rd_kafka_destroy(producer);
        }

        private:

        int send(char *data, int currentCount = 0){
            if(++currentCount <= max_retry_limit){
                int result = rd_kafka_produce(topic, RD_KAFKA_PARTITION_UA,
                            RD_KAFKA_MSG_F_COPY /* Copy payload */,
                            (void *)data, strlen(data),
                            /* Optional key and its length */
                            NULL, 0,
                            /* Message opaque, provided in
                             * delivery report callback as
                             * msg_opaque. */
                            NULL);

                auto last_error = rd_kafka_last_error();
                if (result != -1){
                    return result;
                } else if(last_error == RD_KAFKA_RESP_ERR__QUEUE_FULL) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"queue.buffering.max.messages limit reached, waiting 1sec to flush out.");
                    std::thread([this, data, currentCount]() { 
                        //localqueue is full, hold and flush them.
                        rd_kafka_poll(producer, 1000/*block for max 1000ms*/);
                        send(data,currentCount); 
                    })
                    .detach(); //TODO: limit number of forked threads
                    return result;
                } else {
                    //not handing other unknown errors
                    return result;
                }
            } else {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "KafkaEventPublisher send max_retry_limit hit");
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, data);
            }
            return 0;    
        }   

        int max_retry_limit = 3;
        bool _initialized = 0;

        rd_kafka_t *producer;    
        rd_kafka_topic_t *topic;  
        rd_kafka_conf_t *conf; 
        char errstr[512]; 
       
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

            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Module loaded completed\n");

        };

        void Shutdown() {
            // Send term message
            _publisher.Shutdown();
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Shutdown requested, flushing publisher\n");
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

