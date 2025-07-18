#pragma once

#include <obs-data.h>
#include <stdbool.h>

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
	obs_data_t *payload;
} phoenix_message_t;

// Phoenix protocol functions
obs_data_array_t *phoenix_create_join(const char *join_ref, const char *msg_ref, 
                                      const char *topic, obs_data_t *payload);
obs_data_array_t *phoenix_create_leave(const char *msg_ref, const char *topic);
obs_data_array_t *phoenix_create_heartbeat(const char *msg_ref);

char *phoenix_message_to_json(obs_data_array_t *message);

bool phoenix_parse_message(const char *json, phoenix_message_t *message);
void phoenix_message_free(phoenix_message_t *message);

bool phoenix_is_reply(const phoenix_message_t *message);
bool phoenix_is_heartbeat_reply(const phoenix_message_t *message);
bool phoenix_is_join_reply(const phoenix_message_t *message);

const char *phoenix_get_reply_status(const phoenix_message_t *message);
obs_data_t *phoenix_get_reply_response(const phoenix_message_t *message);

#ifdef __cplusplus
}
#endif