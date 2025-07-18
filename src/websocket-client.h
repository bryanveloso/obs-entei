#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct websocket_client;

typedef void (*websocket_message_callback_t)(const char *message, size_t len, void *user_data);
typedef void (*websocket_connect_callback_t)(bool connected, void *user_data);

struct websocket_client *websocket_client_create(const char *url);
void websocket_client_destroy(struct websocket_client *client);
bool websocket_client_connect(struct websocket_client *client);
void websocket_client_disconnect(struct websocket_client *client);
bool websocket_client_is_connected(struct websocket_client *client);
void websocket_client_send(struct websocket_client *client, const char *message);
void websocket_client_set_message_callback(struct websocket_client *client, websocket_message_callback_t callback, void *user_data);
void websocket_client_set_connect_callback(struct websocket_client *client, websocket_connect_callback_t callback, void *user_data);

#ifdef __cplusplus
}
#endif