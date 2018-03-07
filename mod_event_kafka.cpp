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

#include <switch.h>
#include <memory>
#include <exception>
#include <stdexcept>
#include <string>
#include <sstream>
#include <iostream>

#include "mod_event_kafka.h"
 
namespace mod_event_kafka {

    static switch_xml_config_item_t instructions[] = {
        /* parameter name        type                 reloadable   pointer                         default value     options structure */
        SWITCH_CONFIG_ITEM("bootstrap-servers", SWITCH_CONFIG_STRING, CONFIG_RELOADABLE, &globals.brokers,
                            "localhost:9092", NULL, "bootstrap-servers", "Kafka Bootstrap Brokers"),
        SWITCH_CONFIG_ITEM("topic-prefix", SWITCH_CONFIG_STRING, CONFIG_RELOADABLE, &globals.topic_prefix,
                            "fs", NULL, "topic-prefix", "Kafka Topic Prefix"),
        SWITCH_CONFIG_ITEM_END()
    };

    static switch_status_t do_config(switch_bool_t reload)
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


    class KafkaEventPublisher {
       
        /**
         * @brief Message delivery report callback.
         *
         * This callback is called exactly once per message, indicating if
         * the message was succesfully delivered
         * (rkmessage->err == RD_KAFKA_RESP_ERR_NO_ERROR) or permanently
         * failed delivery (rkmessage->err != RD_KAFKA_RESP_ERR_NO_ERROR).
         *
         * The callback is triggered from rd_kafka_poll() and executes on
         * the application's thread.
         */
        static void dr_msg_cb (rd_kafka_t *rk,
                            const rd_kafka_message_t *rkmessage, void *opaque) {
                if (rkmessage->err)
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, " Message delivery failed %s \n",rd_kafka_err2str(rkmessage->err));
                else
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, " Message delivered (%zd bytes, partition %d) \n",rkmessage->len, rkmessage->partition);
                /* The rkmessage is destroyed automatically by librdkafka */

        }


        public:
        KafkaEventPublisher(){
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "KafkaEventPublisher Initialising...");

            //read config
            do_config(SWITCH_FALSE);
            
            conf = rd_kafka_conf_new();

            //throw std::runtime_error("errstr forced error");
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, globals.brokers);

            if (rd_kafka_conf_set(conf, "metadata.broker.list", globals.brokers,
                              errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr);
            }

            if (rd_kafka_conf_set(conf, "queue.buffering.max.messages", "5",
                              errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr);
            }
            

            /* Set the delivery report callback.
            * This callback will be called once per message to inform
            * the application if delivery succeeded or failed.
            * See dr_msg_cb() above. */
            rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);

            /*
            * Create producer instance.
            *
            * NOTE: rd_kafka_new() takes ownership of the conf object
            *       and the application must not reference it again after
            *       this call.
            */
            rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
            if (!rk) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create new producer: %s", errstr);
            }


            /* Create topic object that will be reused for each message
            * produced.
            *
            * Both the producer instance (rd_kafka_t) and topic objects (topic_t)
            * are long-lived objects that should be reused as much as possible.
            */
            std::string topic = std::string(globals.topic_prefix)+ "hostname";
            rkt = rd_kafka_topic_new(rk, topic.c_str(), NULL);
            if (!rkt) {

                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create topic %s object: %s", topic.c_str(),  rd_kafka_err2str(rd_kafka_last_error()));
                    rd_kafka_destroy(rk);
            }
            _initialized = 1;
        }

        void PublishEvent(switch_event_t *event) {
            
            char *event_json = (char*)malloc(sizeof(char));
            switch_event_serialize_json(event, &event_json);
            int len = strlen(event_json);

            if(_initialized){
                if (rd_kafka_produce(
                            /* Topic object */
                            rkt,
                            /* Use builtin partitioner to select partition*/
                            RD_KAFKA_PARTITION_UA,
                            /* Make a copy of the payload. */
                            RD_KAFKA_MSG_F_COPY,
                            /* Message payload (value) and length */
                            (void *)event_json, len,
                            /* Optional key and its length */
                            NULL, 0,
                            /* Message opaque, provided in
                             * delivery report callback as
                             * msg_opaque. */
                            NULL) == -1) {

                        /**
                         * Failed to *enqueue* message for producing.
                         */
                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to produce to topic %s: %s",  
                            rd_kafka_topic_name(rkt), rd_kafka_err2str(rd_kafka_last_error()));

                        /* Poll to handle delivery reports */
                        if (rd_kafka_last_error() ==
                            RD_KAFKA_RESP_ERR__QUEUE_FULL) {
                                /* If the internal queue is full, wait for
                                 * messages to be delivered and then retry.
                                 * The internal queue represents both
                                 * messages to be sent and messages that have
                                 * been sent or failed, awaiting their
                                 * delivery report callback to be called.
                                 *
                                 * The internal queue is limited by the
                                 * configuration property
                                 * queue.buffering.max.messages */
                                rd_kafka_poll(rk, 1000/*block for max 1000ms*/);
                                //goto retry;
                        }
                        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, event_json);
                } else {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,"Enqueued message (%d bytes) for topic %s\n ", len, rd_kafka_topic_name(rkt));
                }


                /* A producer application should continually serve
                 * the delivery report queue by calling rd_kafka_poll()
                 * at frequent intervals.
                 * Either put the poll call in your main loop, or in a
                 * dedicated thread, or call it after every
                 * rd_kafka_produce() call.
                 * Just make sure that rd_kafka_poll() is still called
                 * during periods where you are not producing any messages
                 * to make sure previously produced messages have their
                 * delivery report callback served (and any other callbacks
                 * you register). */
                rd_kafka_poll(rk, 0/*non-blocking*/);


            } else {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "PublishEvent without KafkaPublisher");
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, event_json);
            }
        }

        ~KafkaEventPublisher(){
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "KafkaEventPublisher Destroyed");
        }

        private:
        rd_kafka_t *rk;         /* Producer instance handle */
        rd_kafka_topic_t *rkt;  /* Topic object */
        rd_kafka_conf_t *conf;  /* Temporary configuration object */
        char errstr[512]; 
        bool _initialized = 0;
        char buf[4096];          /* Message value temporary buffer */

   
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
                //ZmqEventPublisher *publisher = static_cast<ZmqEventPublisher*>(event->bind_user_data);
                //publisher->PublishEvent(event);
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

