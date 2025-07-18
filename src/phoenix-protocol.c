#include "phoenix-protocol.h"
#include <obs-module.h>
#include "plugin-support.h"
#include <stdlib.h>
#include <string.h>

char *phoenix_create_join_json(const char *join_ref, const char *msg_ref, const char *topic, cJSON *payload)
{
	// Phoenix message format: [join_ref, msg_ref, topic, event, payload]
	cJSON *array = cJSON_CreateArray();
	if (!array) {
		return NULL;
	}

	cJSON_AddItemToArray(array, cJSON_CreateString(join_ref ? join_ref : ""));
	cJSON_AddItemToArray(array, cJSON_CreateString(msg_ref ? msg_ref : ""));
	cJSON_AddItemToArray(array, cJSON_CreateString(topic ? topic : ""));
	cJSON_AddItemToArray(array, cJSON_CreateString(PHOENIX_EVENT_JOIN));

	if (payload) {
		cJSON_AddItemToArray(array, cJSON_Duplicate(payload, 1));
	} else {
		cJSON_AddItemToArray(array, cJSON_CreateObject());
	}

	char *json_string = cJSON_PrintUnformatted(array);
	cJSON_Delete(array);

	// Convert to OBS memory management
	if (json_string) {
		char *result = bstrdup(json_string);
		free(json_string);
		return result;
	}

	return NULL;
}

char *phoenix_create_leave_json(const char *msg_ref, const char *topic)
{
	// Phoenix message format: [null, msg_ref, topic, "phx_leave", {}]
	cJSON *array = cJSON_CreateArray();
	if (!array) {
		return NULL;
	}

	cJSON_AddItemToArray(array, cJSON_CreateNull());
	cJSON_AddItemToArray(array, cJSON_CreateString(msg_ref ? msg_ref : ""));
	cJSON_AddItemToArray(array, cJSON_CreateString(topic ? topic : ""));
	cJSON_AddItemToArray(array, cJSON_CreateString(PHOENIX_EVENT_LEAVE));
	cJSON_AddItemToArray(array, cJSON_CreateObject());

	char *json_string = cJSON_PrintUnformatted(array);
	cJSON_Delete(array);

	// Convert to OBS memory management
	if (json_string) {
		char *result = bstrdup(json_string);
		free(json_string);
		return result;
	}

	return NULL;
}

char *phoenix_create_heartbeat_json(const char *msg_ref)
{
	// Phoenix message format: [null, msg_ref, "phoenix", "heartbeat", {}]
	cJSON *array = cJSON_CreateArray();
	if (!array) {
		return NULL;
	}

	cJSON_AddItemToArray(array, cJSON_CreateNull());
	cJSON_AddItemToArray(array, cJSON_CreateString(msg_ref ? msg_ref : ""));
	cJSON_AddItemToArray(array, cJSON_CreateString(PHOENIX_TOPIC_PHOENIX));
	cJSON_AddItemToArray(array, cJSON_CreateString(PHOENIX_EVENT_HEARTBEAT));
	cJSON_AddItemToArray(array, cJSON_CreateObject());

	char *json_string = cJSON_PrintUnformatted(array);
	cJSON_Delete(array);

	// Convert to OBS memory management
	if (json_string) {
		char *result = bstrdup(json_string);
		free(json_string);
		return result;
	}

	return NULL;
}

bool phoenix_parse_message(const char *json, phoenix_message_t *message)
{
	if (!json || !message) {
		return false;
	}

	memset(message, 0, sizeof(phoenix_message_t));

	cJSON *array = cJSON_Parse(json);
	if (!array || !cJSON_IsArray(array)) {
		return false;
	}

	if (cJSON_GetArraySize(array) < 5) {
		cJSON_Delete(array);
		return false;
	}

	// Parse array elements: [join_ref, msg_ref, topic, event, payload]
	cJSON *join_ref_item = cJSON_GetArrayItem(array, 0);
	cJSON *msg_ref_item = cJSON_GetArrayItem(array, 1);
	cJSON *topic_item = cJSON_GetArrayItem(array, 2);
	cJSON *event_item = cJSON_GetArrayItem(array, 3);
	cJSON *payload_item = cJSON_GetArrayItem(array, 4);

	// Extract join_ref
	if (join_ref_item && cJSON_IsString(join_ref_item)) {
		const char *str = cJSON_GetStringValue(join_ref_item);
		if (str && strlen(str) > 0) {
			message->join_ref = bstrdup(str);
		}
	}

	// Extract msg_ref
	if (msg_ref_item && cJSON_IsString(msg_ref_item)) {
		const char *str = cJSON_GetStringValue(msg_ref_item);
		if (str) {
			message->msg_ref = bstrdup(str);
		}
	}

	// Extract topic
	if (topic_item && cJSON_IsString(topic_item)) {
		const char *str = cJSON_GetStringValue(topic_item);
		if (str) {
			message->topic = bstrdup(str);
		}
	}

	// Extract event
	if (event_item && cJSON_IsString(event_item)) {
		const char *str = cJSON_GetStringValue(event_item);
		if (str) {
			message->event = bstrdup(str);
		}
	}

	// Extract payload
	if (payload_item) {
		message->payload = cJSON_Duplicate(payload_item, 1);
	}

	cJSON_Delete(array);
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
		cJSON_Delete(message->payload);
		message->payload = NULL;
	}
}

bool phoenix_is_reply(const phoenix_message_t *message)
{
	return message && message->event && strcmp(message->event, PHOENIX_EVENT_REPLY) == 0;
}

bool phoenix_is_heartbeat_reply(const phoenix_message_t *message)
{
	return phoenix_is_reply(message) && message->topic && strcmp(message->topic, PHOENIX_TOPIC_PHOENIX) == 0;
}

bool phoenix_is_join_reply(const phoenix_message_t *message)
{
	return phoenix_is_reply(message) && message->topic && strcmp(message->topic, PHOENIX_TOPIC_PHOENIX) != 0;
}

const char *phoenix_get_reply_status(const phoenix_message_t *message)
{
	if (!phoenix_is_reply(message) || !message->payload) {
		return NULL;
	}

	cJSON *status = cJSON_GetObjectItem(message->payload, "status");
	if (cJSON_IsString(status)) {
		return cJSON_GetStringValue(status);
	}

	return NULL;
}

cJSON *phoenix_get_reply_response(const phoenix_message_t *message)
{
	if (!phoenix_is_reply(message) || !message->payload) {
		return NULL;
	}

	cJSON *response = cJSON_GetObjectItem(message->payload, "response");
	if (response) {
		return cJSON_Duplicate(response, 1);
	}

	return NULL;
}
