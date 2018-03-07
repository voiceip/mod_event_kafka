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

template<class T>
std::string toString(const T &value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

namespace mod_event_kafka {

    class KafkaEventPublisher {


    };

    class KafkaModule {
    public:

        KafkaModule(switch_loadable_module_interface_t **module_interface, switch_memory_pool_t *pool) {

            // Subscribe to all switch events of any subclass
            // Store a pointer to ourself in the user data
            if (switch_event_bind_removable(modname, SWITCH_EVENT_ALL, SWITCH_EVENT_SUBCLASS_ANY, event_handler,
                                            nullptr, &_node)
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
                char *event_json = (char*)malloc(sizeof(char));
            	switch_event_serialize_json(event, &event_json);

                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, event_json);

            } catch (std::exception ex) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Error publishing event via 0MQ: %s\n",
                                  ex.what());
            } catch (...) { // Exceptions must not propogate to C caller
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Unknown error publishing event via 0MQ\n");
            }
        }

        switch_event_node_t *_node;

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

