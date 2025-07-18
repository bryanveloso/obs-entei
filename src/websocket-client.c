#include "websocket-client.h"
#include <obs-module.h>
#include "plugin-support.h"
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBWEBSOCKETS
#include <libwebsockets.h>
#endif

struct websocket_client {
	char *url;
	char *host;
	char *path;
	int port;
	bool connected;
	bool ssl;
	
#ifdef HAVE_LIBWEBSOCKETS
	struct lws_context *context;
	struct lws *wsi;
	struct lws_protocols protocols[2];
#endif
	
	websocket_message_callback_t message_callback;
	void *message_user_data;
	websocket_connect_callback_t connect_callback;
	void *connect_user_data;
};

#ifdef HAVE_LIBWEBSOCKETS
static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason,
                             void *user, void *in, size_t len)
{
	struct websocket_client *client = (struct websocket_client *)lws_context_user(lws_get_context(wsi));
	
	switch (reason) {
	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		obs_log(LOG_INFO, "WebSocket connection established");
		client->connected = true;
		if (client->connect_callback) {
			client->connect_callback(true, client->connect_user_data);
		}
		break;
		
	case LWS_CALLBACK_CLIENT_RECEIVE:
		obs_log(LOG_INFO, "WebSocket message received: %.*s", (int)len, (char *)in);
		if (client->message_callback) {
			client->message_callback((const char *)in, len, client->message_user_data);
		}
		break;
		
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		obs_log(LOG_ERROR, "WebSocket connection error");
		client->connected = false;
		if (client->connect_callback) {
			client->connect_callback(false, client->connect_user_data);
		}
		break;
		
	case LWS_CALLBACK_CLOSED:
		obs_log(LOG_INFO, "WebSocket connection closed");
		client->connected = false;
		if (client->connect_callback) {
			client->connect_callback(false, client->connect_user_data);
		}
		break;
		
	default:
		break;
	}
	
	return 0;
}
#endif

static bool parse_url(const char *url, char **host, char **path, int *port, bool *ssl)
{
	if (!url || !host || !path || !port || !ssl)
		return false;
		
	*host = NULL;
	*path = NULL;
	*port = 80;
	*ssl = false;
	
	if (strncmp(url, "ws://", 5) == 0) {
		url += 5;
		*ssl = false;
		*port = 80;
	} else if (strncmp(url, "wss://", 6) == 0) {
		url += 6;
		*ssl = true;
		*port = 443;
	} else {
		return false;
	}
	
	const char *path_start = strchr(url, '/');
	const char *port_start = strchr(url, ':');
	
	if (port_start && (!path_start || port_start < path_start)) {
		size_t host_len = port_start - url;
		*host = malloc(host_len + 1);
		strncpy(*host, url, host_len);
		(*host)[host_len] = '\0';
		
		*port = atoi(port_start + 1);
	} else if (path_start) {
		size_t host_len = path_start - url;
		*host = malloc(host_len + 1);
		strncpy(*host, url, host_len);
		(*host)[host_len] = '\0';
	} else {
		*host = strdup(url);
	}
	
	if (path_start) {
		*path = strdup(path_start);
	} else {
		*path = strdup("/");
	}
	
	return true;
}

struct websocket_client *websocket_client_create(const char *url)
{
	struct websocket_client *client = malloc(sizeof(struct websocket_client));
	if (!client)
		return NULL;
		
	memset(client, 0, sizeof(struct websocket_client));
	
	client->url = strdup(url ? url : "");
	client->connected = false;
	
	if (!parse_url(client->url, &client->host, &client->path, &client->port, &client->ssl)) {
		obs_log(LOG_ERROR, "Failed to parse WebSocket URL: %s", client->url);
		free(client->url);
		free(client);
		return NULL;
	}
	
#ifdef HAVE_LIBWEBSOCKETS
	client->protocols[0].name = "phoenix";
	client->protocols[0].callback = websocket_callback;
	client->protocols[0].per_session_data_size = 0;
	client->protocols[0].rx_buffer_size = 0;
	client->protocols[1].name = NULL;
#endif
	
	obs_log(LOG_INFO, "WebSocket client created for URL: %s (host: %s, port: %d, path: %s)", 
	        client->url, client->host, client->port, client->path);
	
	return client;
}

void websocket_client_destroy(struct websocket_client *client)
{
	if (!client)
		return;
		
#ifdef HAVE_LIBWEBSOCKETS
	if (client->wsi) {
		lws_close_reason(client->wsi, LWS_CLOSE_STATUS_GOINGAWAY, NULL, 0);
		client->wsi = NULL;
	}
	
	if (client->context) {
		lws_context_destroy(client->context);
		client->context = NULL;
	}
#endif
	
	free(client->url);
	free(client->host);
	free(client->path);
	
	obs_log(LOG_INFO, "WebSocket client destroyed");
	
	free(client);
}

bool websocket_client_connect(struct websocket_client *client)
{
	if (!client)
		return false;
		
#ifdef HAVE_LIBWEBSOCKETS
	struct lws_context_creation_info info;
	memset(&info, 0, sizeof(info));
	
	info.port = CONTEXT_PORT_NO_LISTEN;
	info.protocols = client->protocols;
	info.gid = -1;
	info.uid = -1;
	info.user = client;
	
	client->context = lws_create_context(&info);
	if (!client->context) {
		obs_log(LOG_ERROR, "Failed to create libwebsockets context");
		return false;
	}
	
	struct lws_client_connect_info ccinfo;
	memset(&ccinfo, 0, sizeof(ccinfo));
	
	ccinfo.context = client->context;
	ccinfo.address = client->host;
	ccinfo.port = client->port;
	ccinfo.path = client->path;
	ccinfo.host = client->host;
	ccinfo.origin = client->host;
	ccinfo.protocol = "phoenix";
	ccinfo.ssl_connection = client->ssl ? LCCSCF_USE_SSL : 0;
	
	client->wsi = lws_client_connect_via_info(&ccinfo);
	if (!client->wsi) {
		obs_log(LOG_ERROR, "Failed to connect to WebSocket server");
		lws_context_destroy(client->context);
		client->context = NULL;
		return false;
	}
	
	obs_log(LOG_INFO, "WebSocket client connecting to %s:%d%s", client->host, client->port, client->path);
	return true;
#else
	obs_log(LOG_ERROR, "WebSocket client connect requested but libwebsockets not available");
	return false;
#endif
}

void websocket_client_disconnect(struct websocket_client *client)
{
	if (!client)
		return;
		
#ifdef HAVE_LIBWEBSOCKETS
	if (client->wsi) {
		lws_close_reason(client->wsi, LWS_CLOSE_STATUS_GOINGAWAY, NULL, 0);
		client->wsi = NULL;
	}
#endif
	
	client->connected = false;
	obs_log(LOG_INFO, "WebSocket client disconnect requested");
}

bool websocket_client_is_connected(struct websocket_client *client)
{
	return client ? client->connected : false;
}

void websocket_client_send(struct websocket_client *client, const char *message)
{
	if (!client || !message)
		return;
		
#ifdef HAVE_LIBWEBSOCKETS
	if (!client->wsi || !client->connected) {
		obs_log(LOG_WARNING, "Cannot send message: WebSocket not connected");
		return;
	}
	
	size_t len = strlen(message);
	unsigned char *buf = malloc(LWS_PRE + len);
	if (!buf) {
		obs_log(LOG_ERROR, "Failed to allocate buffer for WebSocket message");
		return;
	}
	
	memcpy(buf + LWS_PRE, message, len);
	
	int result = lws_write(client->wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
	free(buf);
	
	if (result < 0) {
		obs_log(LOG_ERROR, "Failed to send WebSocket message");
	} else {
		obs_log(LOG_INFO, "WebSocket message sent: %s", message);
	}
#else
	obs_log(LOG_ERROR, "WebSocket send requested but libwebsockets not available");
#endif
}

void websocket_client_set_message_callback(struct websocket_client *client, websocket_message_callback_t callback, void *user_data)
{
	if (!client)
		return;
		
	client->message_callback = callback;
	client->message_user_data = user_data;
}

void websocket_client_set_connect_callback(struct websocket_client *client, websocket_connect_callback_t callback, void *user_data)
{
	if (!client)
		return;
		
	client->connect_callback = callback;
	client->connect_user_data = user_data;
}