#include "websocket-client.h"
#include "plugin-support.h"
#include <obs-module.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <util/darray.h>
#include <util/base.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define close(x) closesocket(x)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

#define RECONNECT_DELAY_MS 5000
#define BUFFER_SIZE 65536

#ifdef _WIN32
static volatile long ws_init_count = 0;
static pthread_mutex_t ws_init_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

struct websocket_client {
	char *url;
	char *host;
	int port;
	char *path;

#ifdef _WIN32
	SOCKET socket_fd;
#else
	int socket_fd;
#endif
	pthread_t thread;
	bool thread_started;
	pthread_mutex_t mutex;
	os_event_t *stop_event;

	bool connected;
	bool auto_reconnect;
	uint32_t reconnect_interval_ms;

	ws_message_callback on_message;
	ws_error_callback on_error;
	ws_connection_callback on_connection;

	void *message_user_data;
	void *error_user_data;
	void *connection_user_data;

	uint8_t recv_buffer[BUFFER_SIZE];
	struct dstr message_buffer;
};

static bool validate_hostname(const char *hostname, size_t len)
{
	if (len == 0 || len > 253) // Max DNS hostname length
		return false;

	// Check for valid characters
	for (size_t i = 0; i < len; i++) {
		char c = hostname[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
		      c == '.'))
			return false;
	}

	// Cannot start or end with hyphen or dot
	if (hostname[0] == '-' || hostname[0] == '.' || hostname[len - 1] == '-' || hostname[len - 1] == '.')
		return false;

	// Check for consecutive dots
	for (size_t i = 1; i < len; i++) {
		if (hostname[i] == '.' && hostname[i - 1] == '.')
			return false;
	}

	return true;
}

static bool parse_url(const char *url, char **host, int *port, char **path)
{
	if (!url || !host || !port || !path)
		return false;

	// Validate URL length
	size_t url_len = strlen(url);
	if (url_len < 10 || url_len > 2048) // Reasonable limits
		return false;

	const char *start = url;

	// Skip ws:// or wss://
	if (strncmp(start, "ws://", 5) == 0) {
		start += 5;
	} else if (strncmp(start, "wss://", 6) == 0) {
		start += 6;
		// Note: WSS not supported in this simple implementation
		blog(LOG_WARNING, "[Entei] WSS (secure WebSocket) not supported, use WS instead");
		return false;
	} else {
		return false;
	}

	// Find host and port
	const char *colon = strchr(start, ':');
	const char *slash = strchr(start, '/');

	if (!slash) {
		slash = start + strlen(start);
	}

	// Extract and validate hostname
	size_t host_len;
	if (colon && colon < slash) {
		host_len = colon - start;
	} else {
		host_len = slash - start;
	}

	if (host_len == 0 || host_len > 253)
		return false;

	// Validate hostname characters
	if (!validate_hostname(start, host_len))
		return false;

	*host = bmalloc(host_len + 1);
	memcpy(*host, start, host_len);
	(*host)[host_len] = '\0';

	// Extract and validate port
	if (colon && colon < slash) {
		char port_str[16];
		size_t port_len = slash - colon - 1;
		if (port_len == 0 || port_len >= sizeof(port_str)) {
			bfree(*host);
			*host = NULL;
			return false;
		}
		memcpy(port_str, colon + 1, port_len);
		port_str[port_len] = '\0';

		// Validate port is numeric
		for (size_t i = 0; i < port_len; i++) {
			if (!isdigit(port_str[i])) {
				bfree(*host);
				*host = NULL;
				return false;
			}
		}

		*port = atoi(port_str);
		if (*port < 1 || *port > 65535) {
			bfree(*host);
			*host = NULL;
			return false;
		}
	} else {
		*port = 80;
	}

	// Extract path
	*path = bstrdup(*slash ? slash : "/");
	return true;
}

static void generate_websocket_key(char *key_out, size_t key_out_size)
{
	// Generate 16 random bytes
	uint8_t random_bytes[16];
	static bool seeded = false;

	if (!seeded) {
		srand((unsigned int)time(NULL) + (unsigned int)os_gettime_ns());
		seeded = true;
	}

	for (int i = 0; i < 16; i++) {
		random_bytes[i] = (uint8_t)(rand() & 0xFF);
	}

	// Base64 encode the random bytes
	base64_encode(random_bytes, 16, key_out, key_out_size);
}

static bool send_websocket_handshake(struct websocket_client *client)
{
	struct dstr handshake;
	dstr_init(&handshake);

	// Generate secure WebSocket key
	char ws_key[32];
	generate_websocket_key(ws_key, sizeof(ws_key));

	dstr_printf(&handshake,
		    "GET %s HTTP/1.1\r\n"
		    "Host: %s:%d\r\n"
		    "Upgrade: websocket\r\n"
		    "Connection: Upgrade\r\n"
		    "Sec-WebSocket-Key: %s\r\n"
		    "Sec-WebSocket-Version: 13\r\n"
		    "\r\n",
		    client->path, client->host, client->port, ws_key);

	int sent = send(client->socket_fd, handshake.array, (int)handshake.len, 0);
	dstr_free(&handshake);

	if (sent < 0) {
		return false;
	}

	// Read response
	char response[1024];
	int received = recv(client->socket_fd, response, sizeof(response) - 1, 0);
	if (received <= 0) {
		return false;
	}
	response[received] = '\0';

	// Check for successful upgrade
	return strstr(response, "HTTP/1.1 101") != NULL;
}

static void process_websocket_frame(struct websocket_client *client, const uint8_t *data, size_t len)
{
	if (len < 2)
		return;

	bool fin = (data[0] & 0x80) != 0;
	int opcode = data[0] & 0x0F;
	bool masked = (data[1] & 0x80) != 0;
	uint64_t payload_len = data[1] & 0x7F;
	size_t pos = 2;

	if (payload_len == 126) {
		if (len < pos + 2)
			return;
		payload_len = (data[pos] << 8) | data[pos + 1];
		pos += 2;
	} else if (payload_len == 127) {
		if (len < pos + 8)
			return;
		payload_len = 0;
		for (int i = 0; i < 8; i++) {
			payload_len = (payload_len << 8) | data[pos + i];
		}
		pos += 8;
	}

	if (masked) {
		pos += 4; // Skip mask key
	}

	if (len < pos + payload_len)
		return;

	if (opcode == 0x1) { // Text frame
		if (fin) {
			// Complete message
			dstr_ncat(&client->message_buffer, (char *)(data + pos), payload_len);

			pthread_mutex_lock(&client->mutex);
			ws_message_callback callback = client->on_message;
			void *user_data = client->message_user_data;
			pthread_mutex_unlock(&client->mutex);
			if (callback) {
				callback(client->message_buffer.array, user_data);
			}

			dstr_free(&client->message_buffer);
			dstr_init(&client->message_buffer);
		} else {
			// Fragmented message
			dstr_ncat(&client->message_buffer, (char *)(data + pos), payload_len);
		}
	} else if (opcode == 0x8) { // Close frame
		pthread_mutex_lock(&client->mutex);
		client->connected = false;
		pthread_mutex_unlock(&client->mutex);
	} else if (opcode == 0x9) { // Ping frame
		// Send pong
		uint8_t pong[2] = {0x8A, 0x00}; // FIN + Pong opcode, 0 length
		send(client->socket_fd, pong, 2, 0);
	}
}

static void *websocket_thread(void *data)
{
	struct websocket_client *client = data;

	while (os_event_timedwait(client->stop_event, 100) == ETIMEDOUT) {
		pthread_mutex_lock(&client->mutex);
		bool connected = client->connected;
		bool auto_reconnect = client->auto_reconnect;
		uint32_t reconnect_ms = client->reconnect_interval_ms;
		pthread_mutex_unlock(&client->mutex);

		if (!connected && auto_reconnect) {
			websocket_client_connect(client);
			pthread_mutex_lock(&client->mutex);
			connected = client->connected;
			pthread_mutex_unlock(&client->mutex);
			if (!connected) {
				os_sleep_ms(reconnect_ms);
				continue;
			}
		}

		if (client->connected &&
#ifdef _WIN32
		    client->socket_fd != INVALID_SOCKET
#else
		    client->socket_fd >= 0
#endif
		) {
			fd_set read_fds;
			struct timeval tv = {0, 100000}; // 100ms timeout

			FD_ZERO(&read_fds);
			FD_SET(client->socket_fd, &read_fds);

			int result = select((int)(client->socket_fd + 1), &read_fds, NULL, NULL, &tv);
			if (result > 0 && FD_ISSET(client->socket_fd, &read_fds)) {
				int received = recv(client->socket_fd, client->recv_buffer, BUFFER_SIZE, 0);
				if (received > 0) {
					process_websocket_frame(client, client->recv_buffer, received);
				} else if (received == 0 || (received < 0 && errno != EAGAIN)) {
					// Connection closed
					pthread_mutex_lock(&client->mutex);
					client->connected = false;
					ws_connection_callback callback = client->on_connection;
					void *user_data = client->connection_user_data;
					pthread_mutex_unlock(&client->mutex);
					if (callback) {
						callback(false, user_data);
					}
					blog(LOG_INFO, "[Entei] WebSocket connection closed");
				}
			}
		}
	}

	return NULL;
}

websocket_client_t *websocket_client_create(const char *url)
{
	struct websocket_client *client = bzalloc(sizeof(struct websocket_client));
	client->url = bstrdup(url);
	client->auto_reconnect = true;
	client->reconnect_interval_ms = RECONNECT_DELAY_MS;
#ifdef _WIN32
	client->socket_fd = INVALID_SOCKET;
#else
	client->socket_fd = -1;
#endif
	dstr_init(&client->message_buffer);

	if (!parse_url(url, &client->host, &client->port, &client->path)) {
		blog(LOG_ERROR, "[Entei] Failed to parse WebSocket URL: %s", url);
		bfree(client->url);
		bfree(client);
		return NULL;
	}

	pthread_mutex_init(&client->mutex, NULL);
	os_event_init(&client->stop_event, OS_EVENT_TYPE_MANUAL);

#ifdef _WIN32
	pthread_mutex_lock(&ws_init_mutex);
	if (os_atomic_inc_long(&ws_init_count) == 1) {
		WSADATA wsaData;
		int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (result != 0) {
			blog(LOG_ERROR, "[Entei] WSAStartup failed: %d", result);
			os_atomic_dec_long(&ws_init_count);
			pthread_mutex_unlock(&ws_init_mutex);
			pthread_mutex_destroy(&client->mutex);
			os_event_destroy(client->stop_event);
			dstr_free(&client->message_buffer);
			bfree(client->path);
			bfree(client->host);
			bfree(client->url);
			bfree(client);
			return NULL;
		}
	}
	pthread_mutex_unlock(&ws_init_mutex);
#endif

	return client;
}

void websocket_client_destroy(websocket_client_t *client)
{
	if (!client)
		return;

	websocket_client_disconnect(client);

	os_event_signal(client->stop_event);
	pthread_mutex_lock(&client->mutex);
	bool thread_started = client->thread_started;
	pthread_mutex_unlock(&client->mutex);
	if (thread_started) {
		pthread_join(client->thread, NULL);
		pthread_mutex_lock(&client->mutex);
		client->thread_started = false;
		pthread_mutex_unlock(&client->mutex);
	}

	pthread_mutex_destroy(&client->mutex);
	os_event_destroy(client->stop_event);

	// Free resources in reverse order of allocation
	dstr_free(&client->message_buffer);
	bfree(client->path);
	bfree(client->host);
	bfree(client->url);

#ifdef _WIN32
	pthread_mutex_lock(&ws_init_mutex);
	if (os_atomic_dec_long(&ws_init_count) == 0) {
		WSACleanup();
	}
	pthread_mutex_unlock(&ws_init_mutex);
#endif

	bfree(client);
}

bool websocket_client_connect(websocket_client_t *client)
{
	if (!client)
		return false;

	pthread_mutex_lock(&client->mutex);
	if (client->connected) {
		pthread_mutex_unlock(&client->mutex);
		return false;
	}
	pthread_mutex_unlock(&client->mutex);

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	struct addrinfo *result;
	char port_str[16];

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	snprintf(port_str, sizeof(port_str), "%d", client->port);

	if (getaddrinfo(client->host, port_str, &hints, &result) != 0) {
		pthread_mutex_lock(&client->mutex);
		ws_error_callback callback = client->on_error;
		void *user_data = client->error_user_data;
		pthread_mutex_unlock(&client->mutex);
		if (callback) {
			callback("Failed to resolve host", user_data);
		}
		return false;
	}

	client->socket_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
#ifdef _WIN32
	if (client->socket_fd == INVALID_SOCKET) {
#else
	if (client->socket_fd < 0) {
#endif
		freeaddrinfo(result);
		pthread_mutex_lock(&client->mutex);
		ws_error_callback callback = client->on_error;
		void *user_data = client->error_user_data;
		pthread_mutex_unlock(&client->mutex);
		if (callback) {
			callback("Failed to create socket", user_data);
		}
		return false;
	}

	if (connect(client->socket_fd, result->ai_addr, (int)result->ai_addrlen) < 0) {
		freeaddrinfo(result);
		close(client->socket_fd);
#ifdef _WIN32
		client->socket_fd = INVALID_SOCKET;
#else
		client->socket_fd = -1;
#endif
		pthread_mutex_lock(&client->mutex);
		ws_error_callback callback = client->on_error;
		void *user_data = client->error_user_data;
		pthread_mutex_unlock(&client->mutex);
		if (callback) {
			callback("Failed to connect to server", user_data);
		}
		return false;
	}

	freeaddrinfo(result);

	if (!send_websocket_handshake(client)) {
		close(client->socket_fd);
#ifdef _WIN32
		client->socket_fd = INVALID_SOCKET;
#else
		client->socket_fd = -1;
#endif
		pthread_mutex_lock(&client->mutex);
		ws_error_callback callback = client->on_error;
		void *user_data = client->error_user_data;
		pthread_mutex_unlock(&client->mutex);
		if (callback) {
			callback("WebSocket handshake failed", user_data);
		}
		return false;
	}

	pthread_mutex_lock(&client->mutex);
	client->connected = true;
	ws_connection_callback callback = client->on_connection;
	void *user_data = client->connection_user_data;
	pthread_mutex_unlock(&client->mutex);

	if (callback) {
		callback(true, user_data);
	}

	pthread_mutex_lock(&client->mutex);
	if (!client->thread_started) {
		pthread_create(&client->thread, NULL, websocket_thread, client);
		client->thread_started = true;
	}
	pthread_mutex_unlock(&client->mutex);

	blog(LOG_INFO, "[Entei] WebSocket connected to %s", client->url);
	return true;
}

void websocket_client_disconnect(websocket_client_t *client)
{
	if (!client)
		return;

	pthread_mutex_lock(&client->mutex);
	if (!client->connected) {
		pthread_mutex_unlock(&client->mutex);
		return;
	}
	client->connected = false;
	pthread_mutex_unlock(&client->mutex);

#ifdef _WIN32
	if (client->socket_fd != INVALID_SOCKET) {
#else
	if (client->socket_fd >= 0) {
#endif
		// Send close frame
		uint8_t close_frame[2] = {0x88, 0x00}; // FIN + Close opcode, 0 length
		send(client->socket_fd, close_frame, 2, 0);

		close(client->socket_fd);
#ifdef _WIN32
		client->socket_fd = INVALID_SOCKET;
#else
		client->socket_fd = -1;
#endif
	}

	pthread_mutex_lock(&client->mutex);
	ws_connection_callback callback = client->on_connection;
	void *user_data = client->connection_user_data;
	pthread_mutex_unlock(&client->mutex);
	if (callback) {
		callback(false, user_data);
	}

	blog(LOG_INFO, "[Entei] WebSocket disconnected");
}

bool websocket_client_is_connected(const websocket_client_t *client)
{
	if (!client)
		return false;

	pthread_mutex_lock((pthread_mutex_t *)&client->mutex); // Cast away const
	bool connected = client->connected;
	pthread_mutex_unlock((pthread_mutex_t *)&client->mutex);
	return connected;
}

void websocket_client_set_message_callback(websocket_client_t *client, ws_message_callback callback, void *user_data)
{
	if (!client)
		return;

	pthread_mutex_lock(&client->mutex);
	client->on_message = callback;
	client->message_user_data = user_data;
	pthread_mutex_unlock(&client->mutex);
}

void websocket_client_set_error_callback(websocket_client_t *client, ws_error_callback callback, void *user_data)
{
	if (!client)
		return;

	pthread_mutex_lock(&client->mutex);
	client->on_error = callback;
	client->error_user_data = user_data;
	pthread_mutex_unlock(&client->mutex);
}

void websocket_client_set_connection_callback(websocket_client_t *client, ws_connection_callback callback,
					      void *user_data)
{
	if (!client)
		return;

	pthread_mutex_lock(&client->mutex);
	client->on_connection = callback;
	client->connection_user_data = user_data;
	pthread_mutex_unlock(&client->mutex);
}

void websocket_client_set_auto_reconnect(websocket_client_t *client, bool enabled)
{
	if (!client)
		return;

	pthread_mutex_lock(&client->mutex);
	client->auto_reconnect = enabled;
	pthread_mutex_unlock(&client->mutex);
}

void websocket_client_set_reconnect_interval(websocket_client_t *client, uint32_t seconds)
{
	if (!client)
		return;

	pthread_mutex_lock(&client->mutex);
	client->reconnect_interval_ms = seconds * 1000;
	pthread_mutex_unlock(&client->mutex);
}
