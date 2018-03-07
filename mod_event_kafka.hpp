#ifndef MOD_EVENT_KAFKA_H
#define MOD_EVENT_KAFKA_H
 
#include "librdkafka/rdkafkacpp.h"

namespace mod_event_kafka {

	static struct {
		char *brokers;
		char *topic_prefix;
	} globals;

	SWITCH_MODULE_LOAD_FUNCTION(mod_event_kafka_load);
	SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_event_kafka_shutdown);

	extern "C" {
		SWITCH_MODULE_DEFINITION(mod_event_kafka, mod_event_kafka_load, mod_event_kafka_shutdown, NULL);
	};
};

#endif // MOD_EVENT_KAFKA_H