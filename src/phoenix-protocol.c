#include "phoenix-protocol.h"
#include <obs-module.h>
#include "plugin-support.h"
#include <stdlib.h>
#include <string.h>

obs_data_array_t *phoenix_create_join(const char *join_ref, const char *msg_ref, 
                                      const char *topic, obs_data_t *payload)
{
	obs_data_array_t *array = obs_data_array_create();
	
	obs_data_t *join_ref_item = obs_data_create();
	obs_data_set_string(join_ref_item, "value", join_ref ? join_ref : "");
	obs_data_array_push_back_string(array, join_ref ? join_ref : "");
	obs_data_release(join_ref_item);
	
	obs_data_array_push_back_string(array, msg_ref ? msg_ref : "");
	obs_data_array_push_back_string(array, topic ? topic : "");
	obs_data_array_push_back_string(array, PHOENIX_EVENT_JOIN);
	
	if (payload) {
		obs_data_array_push_back_obj(array, payload);
	} else {
		obs_data_t *empty_payload = obs_data_create();
		obs_data_array_push_back_obj(array, empty_payload);
		obs_data_release(empty_payload);
	}
	
	return array;
}

obs_data_array_t *phoenix_create_leave(const char *msg_ref, const char *topic)
{
	obs_data_array_t *array = obs_data_array_create();
	
	obs_data_array_push_back_string(array, ""); // null join_ref for leave
	obs_data_array_push_back_string(array, msg_ref ? msg_ref : "");
	obs_data_array_push_back_string(array, topic ? topic : "");
	obs_data_array_push_back_string(array, PHOENIX_EVENT_LEAVE);
	
	obs_data_t *empty_payload = obs_data_create();
	obs_data_array_push_back_obj(array, empty_payload);
	obs_data_release(empty_payload);
	
	return array;
}

obs_data_array_t *phoenix_create_heartbeat(const char *msg_ref)
{
	obs_data_array_t *array = obs_data_array_create();
	
	obs_data_array_push_back_string(array, ""); // null join_ref for heartbeat
	obs_data_array_push_back_string(array, msg_ref ? msg_ref : "");
	obs_data_array_push_back_string(array, PHOENIX_TOPIC_PHOENIX);
	obs_data_array_push_back_string(array, PHOENIX_EVENT_HEARTBEAT);
	
	obs_data_t *empty_payload = obs_data_create();
	obs_data_array_push_back_obj(array, empty_payload);
	obs_data_release(empty_payload);
	
	return array;
}

char *phoenix_message_to_json(obs_data_array_t *message)
{
	if (!message) {
		return NULL;
	}
	
	obs_data_t *wrapper = obs_data_create();
	obs_data_set_array(wrapper, "message", message);
	
	const char *json = obs_data_get_json(wrapper);
	char *result = NULL;
	
	if (json) {
		// Extract just the array part from {"message":[...]}
		const char *array_start = strchr(json, '[');
		const char *array_end = strrchr(json, ']');
		
		if (array_start && array_end && array_end > array_start) {
			size_t array_len = array_end - array_start + 1;
			result = bmalloc(array_len + 1);
			if (result) {
				memcpy(result, array_start, array_len);
				result[array_len] = '\0';
			}
		}
	}
	
	obs_data_release(wrapper);
	return result;
}

bool phoenix_parse_message(const char *json, phoenix_message_t *message)
{
	if (!json || !message) {
		return false;
	}
	
	memset(message, 0, sizeof(phoenix_message_t));
	
	obs_data_array_t *array = NULL;
	obs_data_t *data = obs_data_create_from_json(json);
	
	if (!data) {
		return false;
	}
	
	// Check if it's a direct array or wrapped in an object
	array = obs_data_get_array(data, "message");
	if (!array) {
		// Try parsing as direct array by wrapping it
		obs_data_release(data);
		
		char *wrapped_json = bmalloc(strlen(json) + 20);
		if (!wrapped_json) {
			return false;
		}
		
		sprintf(wrapped_json, "{\"array\":%s}", json);
		data = obs_data_create_from_json(wrapped_json);
		bfree(wrapped_json);
		
		if (!data) {
			return false;
		}
		
		array = obs_data_get_array(data, "array");
	}
	
	if (!array || obs_data_array_count(array) < 5) {
		obs_data_release(data);
		if (array) obs_data_array_release(array);
		return false;
	}
	
	// Parse array elements: [join_ref, msg_ref, topic, event, payload]
	obs_data_t *item;
	
	// join_ref (index 0)
	item = obs_data_array_item(array, 0);
	if (item) {
		const char *join_ref = obs_data_get_string(item, "value");
		if (!join_ref) join_ref = obs_data_get_string(item, "");
		if (join_ref && strlen(join_ref) > 0) {
			message->join_ref = bstrdup(join_ref);
		}
		obs_data_release(item);
	}
	
	// msg_ref (index 1)
	item = obs_data_array_item(array, 1);
	if (item) {
		const char *msg_ref = obs_data_get_string(item, "value");
		if (!msg_ref) msg_ref = obs_data_get_string(item, "");
		if (msg_ref) {
			message->msg_ref = bstrdup(msg_ref);
		}
		obs_data_release(item);
	}
	
	// topic (index 2)
	item = obs_data_array_item(array, 2);
	if (item) {
		const char *topic = obs_data_get_string(item, "value");
		if (!topic) topic = obs_data_get_string(item, "");
		if (topic) {
			message->topic = bstrdup(topic);
		}
		obs_data_release(item);
	}
	
	// event (index 3)
	item = obs_data_array_item(array, 3);
	if (item) {
		const char *event = obs_data_get_string(item, "value");
		if (!event) event = obs_data_get_string(item, "");
		if (event) {
			message->event = bstrdup(event);
		}
		obs_data_release(item);
	}
	
	// payload (index 4)
	item = obs_data_array_item(array, 4);
	if (item) {
		message->payload = item;
		obs_data_addref(message->payload);
		obs_data_release(item);
	}
	
	obs_data_array_release(array);
	obs_data_release(data);
	
	return true;
}

void phoenix_message_free(phoenix_message_t *message)
{
	if (!message) {
		return;
	}
	
	if (message->join_ref) {
		bfree(message->join_ref);
		message->join_ref = NULL;
	}
	
	if (message->msg_ref) {
		bfree(message->msg_ref);
		message->msg_ref = NULL;
	}
	
	if (message->topic) {
		bfree(message->topic);
		message->topic = NULL;
	}
	
	if (message->event) {
		bfree(message->event);
		message->event = NULL;
	}
	
	if (message->payload) {
		obs_data_release(message->payload);
		message->payload = NULL;
	}
}

bool phoenix_is_reply(const phoenix_message_t *message)
{
	return message && message->event && 
	       strcmp(message->event, PHOENIX_EVENT_REPLY) == 0;
}

bool phoenix_is_heartbeat_reply(const phoenix_message_t *message)
{
	return phoenix_is_reply(message) && 
	       message->topic && 
	       strcmp(message->topic, PHOENIX_TOPIC_PHOENIX) == 0;
}

bool phoenix_is_join_reply(const phoenix_message_t *message)
{
	return phoenix_is_reply(message) && 
	       message->topic && 
	       strcmp(message->topic, PHOENIX_TOPIC_PHOENIX) != 0;
}

const char *phoenix_get_reply_status(const phoenix_message_t *message)
{
	if (!phoenix_is_reply(message) || !message->payload) {
		return NULL;
	}
	
	return obs_data_get_string(message->payload, "status");
}

obs_data_t *phoenix_get_reply_response(const phoenix_message_t *message)
{
	if (!phoenix_is_reply(message) || !message->payload) {
		return NULL;
	}
	
	obs_data_t *response = obs_data_get_obj(message->payload, "response");
	if (response) {
		obs_data_addref(response);
	}
	
	return response;
}