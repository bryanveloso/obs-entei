#include "phoenix-protocol.h"
#include <obs-module.h>
#include "plugin-support.h"
#include <stdlib.h>
#include <string.h>

static void extract_string_field(cJSON *item, char **dest)
{
	if (item && cJSON_IsString(item)) {
		const char *str = cJSON_GetStringValue(item);
		if (str) {
			*dest = bstrdup(str);
		}
	}
}

static void extract_payload_field(cJSON *item, cJSON **dest)
{
	if (item) {
		*dest = cJSON_Duplicate(item, 1);
	}
}

char *phoenix_create_join_json(const char *join_ref, const char *msg_ref, const char *topic, cJSON *payload)
{
	// Phoenix 1.7 object format
	cJSON *object = cJSON_CreateObject();
	if (!object) {
		return NULL;
	}

	// Add join_ref (can be null)
	if (join_ref) {
		cJSON_AddStringToObject(object, "join_ref", join_ref);
	} else {
		cJSON_AddNullToObject(object, "join_ref");
	}

	// Add ref
	cJSON_AddStringToObject(object, "ref", msg_ref ? msg_ref : "");

	// Add topic
	cJSON_AddStringToObject(object, "topic", topic ? topic : "");

	// Add event
	cJSON_AddStringToObject(object, "event", PHOENIX_EVENT_JOIN);

	// Add payload
	if (payload) {
		cJSON_AddItemToObject(object, "payload", cJSON_Duplicate(payload, 1));
	} else {
		cJSON_AddItemToObject(object, "payload", cJSON_CreateObject());
	}

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
	// Phoenix 1.7 object format
	cJSON *object = cJSON_CreateObject();
	if (!object) {
		return NULL;
	}

	// Add join_ref (null for leave)
	cJSON_AddNullToObject(object, "join_ref");

	// Add ref
	cJSON_AddStringToObject(object, "ref", msg_ref ? msg_ref : "");

	// Add topic
	cJSON_AddStringToObject(object, "topic", topic ? topic : "");

	// Add event
	cJSON_AddStringToObject(object, "event", PHOENIX_EVENT_LEAVE);

	// Add payload
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
	// Phoenix 1.7 object format
	cJSON *object = cJSON_CreateObject();
	if (!object) {
		return NULL;
	}

	// Add join_ref (null for heartbeat)
	cJSON_AddNullToObject(object, "join_ref");

	// Add ref
	cJSON_AddStringToObject(object, "ref", msg_ref ? msg_ref : "");

	// Add topic
	cJSON_AddStringToObject(object, "topic", PHOENIX_TOPIC_PHOENIX);

	// Add event
	cJSON_AddStringToObject(object, "event", PHOENIX_EVENT_HEARTBEAT);

	// Add payload
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

	cJSON *root = cJSON_Parse(json);
	if (!root) {
		return false;
	}

	// Try to parse as object format first (Phoenix 1.7)
	if (cJSON_IsObject(root)) {
		// Parse object format: {"join_ref": ..., "ref": ..., "topic": ..., "event": ..., "payload": ...}
		extract_string_field(cJSON_GetObjectItem(root, "join_ref"), &message->join_ref);
		extract_string_field(cJSON_GetObjectItem(root, "ref"), &message->msg_ref);
		extract_string_field(cJSON_GetObjectItem(root, "topic"), &message->topic);
		extract_string_field(cJSON_GetObjectItem(root, "event"), &message->event);
		extract_payload_field(cJSON_GetObjectItem(root, "payload"), &message->payload);

		// Validate that essential fields were found
		if (!message->event || !message->topic || !message->msg_ref) {
			cJSON_Delete(root);
			phoenix_message_free(message);
			memset(message, 0, sizeof(phoenix_message_t));
			return false;
		}
	}
	// Fall back to array format for backward compatibility
	else if (cJSON_IsArray(root)) {
		// Parse array format: [join_ref, ref, topic, event, payload]
		if (cJSON_GetArraySize(root) < 5) {
			cJSON_Delete(root);
			return false;
		}

		extract_string_field(cJSON_GetArrayItem(root, 0), &message->join_ref);
		extract_string_field(cJSON_GetArrayItem(root, 1), &message->msg_ref);
		extract_string_field(cJSON_GetArrayItem(root, 2), &message->topic);
		extract_string_field(cJSON_GetArrayItem(root, 3), &message->event);
		extract_payload_field(cJSON_GetArrayItem(root, 4), &message->payload);
	} else {
		cJSON_Delete(root);
		return false;
	}

	cJSON_Delete(root);
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
