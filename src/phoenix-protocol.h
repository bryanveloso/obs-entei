#pragma once

#include <stdbool.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// Phoenix message events
#define PHOENIX_EVENT_JOIN "phx_join"
#define PHOENIX_EVENT_LEAVE "phx_leave"
#define PHOENIX_EVENT_HEARTBEAT "heartbeat"
#define PHOENIX_EVENT_REPLY "phx_reply"

// Phoenix topics
#define PHOENIX_TOPIC_PHOENIX "phoenix"

// Phoenix message structure: [join_ref, msg_ref, topic, event, payload]
typedef struct {
	char *join_ref;
	char *msg_ref;
	char *topic;
	char *event;
	cJSON *payload;
} phoenix_message_t;

// Phoenix protocol functions - return JSON strings directly
char *phoenix_create_join_json(const char *join_ref, const char *msg_ref, const char *topic, cJSON *payload);
char *phoenix_create_leave_json(const char *msg_ref, const char *topic);
char *phoenix_create_heartbeat_json(const char *msg_ref);

bool phoenix_parse_message(const char *json, phoenix_message_t *message);
void phoenix_message_free(phoenix_message_t *message);

bool phoenix_is_reply(const phoenix_message_t *message);
bool phoenix_is_heartbeat_reply(const phoenix_message_t *message);
bool phoenix_is_join_reply(const phoenix_message_t *message);

const char *phoenix_get_reply_status(const phoenix_message_t *message);
cJSON *phoenix_get_reply_response(const phoenix_message_t *message);

#ifdef __cplusplus
}
#endif
