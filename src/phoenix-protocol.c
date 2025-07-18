#include "phoenix-protocol.h"
#include <obs-module.h>
#include "plugin-support.h"
#include <stdlib.h>
#include <string.h>

char *phoenix_create_join_json(const char *join_ref, const char *msg_ref, const char *topic, cJSON *payload)
{
	(void)join_ref; // Not used in current implementation
	// Phoenix message format: {"topic": "...", "event": "phx_join", "payload": {}, "ref": "..."}
	cJSON *object = cJSON_CreateObject();
	if (!object) {
		return NULL;
	}

	cJSON_AddStringToObject(object, "topic", topic ? topic : "");
	cJSON_AddStringToObject(object, "event", PHOENIX_EVENT_JOIN);
	cJSON_AddStringToObject(object, "ref", msg_ref ? msg_ref : "");

	if (payload) {
		cJSON_AddItemToObject(object, "payload", cJSON_Duplicate(payload, 1));
	} else {
		cJSON_AddItemToObject(object, "payload", cJSON_CreateObject());
	}

	// Note: join_ref is not used in object format, only ref

	char *json_string = cJSON_PrintUnformatted(object);
	cJSON_Delete(object);

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
	// Phoenix message format: {"topic": "...", "event": "phx_leave", "payload": {}, "ref": "..."}
	cJSON *object = cJSON_CreateObject();
	if (!object) {
		return NULL;
	}

	cJSON_AddStringToObject(object, "topic", topic ? topic : "");
	cJSON_AddStringToObject(object, "event", PHOENIX_EVENT_LEAVE);
	cJSON_AddStringToObject(object, "ref", msg_ref ? msg_ref : "");
	cJSON_AddItemToObject(object, "payload", cJSON_CreateObject());

	char *json_string = cJSON_PrintUnformatted(object);
	cJSON_Delete(object);

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
	// Phoenix message format: {"topic": "phoenix", "event": "heartbeat", "payload": {}, "ref": "..."}
	cJSON *object = cJSON_CreateObject();
	if (!object) {
		return NULL;
	}

	cJSON_AddStringToObject(object, "topic", PHOENIX_TOPIC_PHOENIX);
	cJSON_AddStringToObject(object, "event", PHOENIX_EVENT_HEARTBEAT);
	cJSON_AddStringToObject(object, "ref", msg_ref ? msg_ref : "");
	cJSON_AddItemToObject(object, "payload", cJSON_CreateObject());

	char *json_string = cJSON_PrintUnformatted(object);
	cJSON_Delete(object);

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

	cJSON *object = cJSON_Parse(json);
	if (!object || !cJSON_IsObject(object)) {
		return false;
	}

	// Parse object fields: {"topic": "...", "event": "...", "payload": {}, "ref": "..."}
	cJSON *topic_item = cJSON_GetObjectItem(object, "topic");
	cJSON *event_item = cJSON_GetObjectItem(object, "event");
	cJSON *ref_item = cJSON_GetObjectItem(object, "ref");
	cJSON *payload_item = cJSON_GetObjectItem(object, "payload");

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

	// Extract ref (used as msg_ref)
	if (ref_item && cJSON_IsString(ref_item)) {
		const char *str = cJSON_GetStringValue(ref_item);
		if (str) {
			message->msg_ref = bstrdup(str);
		}
	}

	// Extract payload
	if (payload_item) {
		message->payload = cJSON_Duplicate(payload_item, 1);
	}

	// Note: join_ref is not used in object format replies
	message->join_ref = NULL;

	cJSON_Delete(object);
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
