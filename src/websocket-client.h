#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct websocket_client;

struct websocket_client *websocket_client_create(const char *url);
void websocket_client_destroy(struct websocket_client *client);
bool websocket_client_connect(struct websocket_client *client);
void websocket_client_disconnect(struct websocket_client *client);
bool websocket_client_is_connected(struct websocket_client *client);

#ifdef __cplusplus
}
#endif