#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ws_message_callback)(const char *message, void *user_data);
typedef void (*ws_error_callback)(const char *error, void *user_data);
typedef void (*ws_connection_callback)(bool connected, void *user_data);

typedef struct websocket_client websocket_client_t;

websocket_client_t *websocket_client_create(const char *url);
void websocket_client_destroy(websocket_client_t *client);

bool websocket_client_connect(websocket_client_t *client);
void websocket_client_disconnect(websocket_client_t *client);
bool websocket_client_is_connected(const websocket_client_t *client);

void websocket_client_set_message_callback(websocket_client_t *client, 
                                          ws_message_callback callback, 
                                          void *user_data);
void websocket_client_set_error_callback(websocket_client_t *client, 
                                       ws_error_callback callback, 
                                       void *user_data);
void websocket_client_set_connection_callback(websocket_client_t *client,
                                            ws_connection_callback callback,
                                            void *user_data);

void websocket_client_set_auto_reconnect(websocket_client_t *client, bool enabled);
void websocket_client_set_reconnect_interval(websocket_client_t *client, uint32_t seconds);

#ifdef __cplusplus
}
#endif
