#ifndef MOD_EVENT_KAFKA_H
#define MOD_EVENT_KAFKA_H

extern "C" { 
	#include "librdkafka/rdkafka.h"
}

namespace mod_event_kafka {

	static struct {
		char *brokers;
		char *topic_prefix;
		char *topic;
		char *username;
		char *password;
		int buffer_size;
		char *compression;
		char *event_filter;
	} globals;


	static struct {
		/* Array to store the possible event subscriptions */
		int event_subscriptions;
		switch_event_node_t *event_nodes[SWITCH_EVENT_ALL];
		switch_event_types_t event_ids[SWITCH_EVENT_ALL];
		switch_event_node_t *eventNode;
	} profile;

	SWITCH_MODULE_LOAD_FUNCTION(mod_event_kafka_load);
	SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_event_kafka_shutdown);

	extern "C" {
		SWITCH_MODULE_DEFINITION(mod_event_kafka, mod_event_kafka_load, mod_event_kafka_shutdown, NULL);
	};
};

#endif // MOD_EVENT_KAFKA_H
