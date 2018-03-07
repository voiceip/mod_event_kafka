#ifndef MOD_EVENT_KAFKA_H
#define MOD_EVENT_KAFKA_H

namespace mod_event_kafka {
	
// static const char MODULE_TERM_REQ_MESSAGE = 1;
// static const char MODULE_TERM_ACK_MESSAGE = 2;

// static const char *TERM_URI = "inproc://mod_event_kafka_term";

SWITCH_MODULE_LOAD_FUNCTION(mod_event_kafka_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_event_kafka_shutdown);

extern "C" {
SWITCH_MODULE_DEFINITION(mod_event_kafka, mod_event_kafka_load, mod_event_kafka_shutdown, NULL);
};

}

#endif // MOD_EVENT_KAFKA_H