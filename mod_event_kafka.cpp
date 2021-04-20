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

    constexpr std::size_t UUID_LENGTH = 36;

    template <typename T, std::size_t Muliplier = 1>
    T* malloc_new()
    {
        return static_cast<T*>(std::malloc(sizeof(T) * Muliplier));
    }

    static switch_xml_config_item_t instructions[] = {
        SWITCH_CONFIG_ITEM("bootstrap-servers", SWITCH_CONFIG_STRING, CONFIG_RELOADABLE, &globals.brokers,
                            "localhost:9092", NULL, "bootstrap-servers", "Kafka Bootstrap Brokers"),
        SWITCH_CONFIG_ITEM("username", SWITCH_CONFIG_STRING, CONFIG_RELOADABLE, &globals.username, "", NULL, "username", "Username"),
        SWITCH_CONFIG_ITEM("password", SWITCH_CONFIG_STRING, CONFIG_RELOADABLE, &globals.password, "", NULL, "password", "Password"),
        SWITCH_CONFIG_ITEM("topic", SWITCH_CONFIG_STRING, CONFIG_RELOADABLE, &globals.topic,
                            "", NULL, "topic", "Kafka Topic"),
        SWITCH_CONFIG_ITEM("topic-prefix", SWITCH_CONFIG_STRING, CONFIG_RELOADABLE, &globals.topic_prefix,
                            "fs", NULL, "topic-prefix", "Kafka Topic Prefix"),
        SWITCH_CONFIG_ITEM("buffer-size", SWITCH_CONFIG_INT, CONFIG_RELOADABLE, &globals.buffer_size,
                            10, NULL, "buffer-size", "queue.buffering.max.messages"),
        SWITCH_CONFIG_ITEM_END()
    };

    static switch_status_t load_config(switch_bool_t reload)
    {
        memset(&globals, 0, sizeof(globals));
        if (switch_xml_config_parse_module_settings("event_kafka.conf", reload, instructions) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Could not open event_kafka.conf\n");
            return SWITCH_STATUS_FALSE;
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "event_kafka.conf loaded [brokers: %s, topic/topic_prefix: %s/%s, username: %s, buffer-size: %d]", globals.brokers, globals.topic, globals.topic_prefix, globals.username, globals.buffer_size);
        }
        return SWITCH_STATUS_SUCCESS;
    }


    class KafkaEventPublisher {

        
        public:
        KafkaEventPublisher(){

            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "KafkaEventPublisher Initialising...");

            load_config(SWITCH_FALSE);
            
            conf = rd_kafka_conf_new();

            if (rd_kafka_conf_set(conf, "metadata.broker.list", globals.brokers, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
               switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr);
            }

            if (rd_kafka_conf_set(conf, "queue.buffering.max.messages", std::to_string(globals.buffer_size).c_str(), errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr);
            }

            if (rd_kafka_conf_set(conf, "max.in.flight", "1000", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr);
            }

            if (rd_kafka_conf_set(conf, "compression.codec", "snappy", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr);
            }

            if (globals.username && globals.username[0] != "\0") {
                //username is set, set authentication params
                if (rd_kafka_conf_set(conf, "sasl.mechanism", "PLAIN", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr);
                }

                if (rd_kafka_conf_set(conf, "security.protocol", "SASL_PLAINTEXT", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr);
                }

                if (rd_kafka_conf_set(conf, "sasl.username", globals.username, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr);
                }

                if (rd_kafka_conf_set(conf, "sasl.password", globals.password, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, errstr);
                }
            }

            rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);

            producer = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
            if (!producer) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create new producer: %s \n", errstr);
            }

             //build topic if not defined
            if (globals.topic && globals.topic[0] == "\0") {
                //topic is not defined
                std::string topic_str = std::string(globals.topic_prefix) + "_" + std::string(switch_core_get_switchname());
                globals.topic = topic_str.c_str();
            }

            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "KafkaEventPublisher Topic : %s \n", globals.topic);

            rd_kafka_topic_conf_t *tconf = rd_kafka_topic_conf_new();
            rd_kafka_topic_conf_set(tconf, "message.timeout.ms", "30000", NULL, 0);

            topic = rd_kafka_topic_new(producer, globals.topic, NULL);
            if (!topic) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create topic %s object: %s \n", globals.topic,  rd_kafka_err2str(rd_kafka_last_error()));
            }

            _initialized = true;
        }

        void PublishEvent(switch_event_t *event) {

            char *uuid = switch_event_get_header(event, "Channel-Call-UUID");
            char *event_json = malloc_new<char>();

            const switch_status_t json_status = switch_event_serialize_json(event, &event_json);
            if (json_status == SWITCH_STATUS_FALSE) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "json serialization failed in switch\n");
                std::free(event_json);
                return;
            }

            if(_initialized){
                int resp = send(event_json, uuid ,0);
                if (resp == -1){
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to produce, with error %s \n", rd_kafka_err2str(rd_kafka_last_error()));
                } else {
                    //size_t len = strlen(event_json);
                    //switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"Produced message (%zu bytes)", len);
                }
                rd_kafka_poll(producer, 0);
            } else {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "PublishEvent without active KafkaPublisher\n %s \n",event_json);
                delete uuid;
                std::free(event_json);
            }
        }

        void Shutdown(){
            //flush within 100ms
            rd_kafka_flush(producer, 100);
        }

        ~KafkaEventPublisher(){
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "KafkaEventPublisher Destroyed\n");
            rd_kafka_topic_destroy(topic);
            rd_kafka_destroy(producer);
        }

        private:

        static void dr_msg_cb (rd_kafka_t *rk, const rd_kafka_message_t *rkmessage, void *opaque) {
            if (rkmessage->err) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, " Message delivery failed %s \n",rd_kafka_err2str(rkmessage->err));
            }
            else {
                //switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,  "Message delivered (%zd bytes, partition %d, offset  %" PRId64 ") \n",rkmessage->len, rkmessage->partition, rkmessage->offset);
                // rd_kafka_message_destroy ((rd_kafka_message_t *)rkmessage);
            }
        }

        int send(char *data, char *key, int currentCount){
            if(++currentCount <= max_retry_limit){
                int key_length = key == NULL ? 0 : strlen(key);
                int result = rd_kafka_produce(topic, RD_KAFKA_PARTITION_UA,
                            RD_KAFKA_MSG_F_FREE /* Auto Clear Payload */,
                            (void *)data, strlen(data),
                            (const void *)key, key_length,
                            /* Message opaque, provided in
                             * delivery report callback as
                             * msg_opaque. */
                            NULL);

                auto last_error = rd_kafka_last_error();
                if(result == 0){
                    return result;
                } else if(last_error == RD_KAFKA_RESP_ERR__QUEUE_FULL) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,"queue.buffering.max.messages limit reached, waiting 1sec to flush out.\n");
                    std::thread([this, data, currentCount, key]() { 
                        //localqueue is full, hold and flush them.
                        rd_kafka_poll(producer, 1000/*block for max 1000ms*/);
                        send(data, key, currentCount); 
                    })
                    .detach(); //TODO: limit number of forked threads
                    return result;
                } else {
                    //not handing other unknown errors
                    return result;
                }
            } else {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "KafkaEventPublisher send max_retry_limit hit.\n");
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "%s\n",data);
                // delete data; //TODO: Doesn't work, throws segment fault.
                // delete key;
            }
            return 0;    
        }   

        int max_retry_limit = 3;
        bool _initialized = false;

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
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Error publishing event to Kafka: %s\n", ex.what());
            } catch (...) { // Exceptions must not propogate to C caller
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Unknown error publishing event to Kafka\n");
            }
        }

        switch_event_node_t *_node;
        KafkaEventPublisher _publisher;

    };


    //*****************************//
    //           GLOBALS           //
    //*****************************//
    std::unique_ptr<KafkaModule> module;


    //*****************************//
    //  Module interface funtions  //
    //*****************************//
    SWITCH_MODULE_LOAD_FUNCTION(mod_event_kafka_load) {
            try {
                module = std::make_unique<KafkaModule>(module_interface, pool);
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
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Unknown error shutting down Kafka Event module\n");
            }
            return SWITCH_STATUS_SUCCESS;
    }

}

