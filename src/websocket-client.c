#include "websocket-client.h"
#include "plugin-support.h"
#include <obs-module.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <util/darray.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

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

struct websocket_client {
    char *url;
    char *host;
    int port;
    char *path;
    
    int socket_fd;
    pthread_t thread;
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

static bool parse_url(const char *url, char **host, int *port, char **path)
{
    const char *start = url;
    
    // Skip ws:// or wss://
    if (strncmp(start, "ws://", 5) == 0) {
        start += 5;
    } else if (strncmp(start, "wss://", 6) == 0) {
        start += 6;
        // Note: WSS not supported in this simple implementation
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
    
    if (colon && colon < slash) {
        *host = bstrndup(start, colon - start);
        char port_str[16];
        size_t port_len = slash - colon - 1;
        if (port_len >= sizeof(port_str))
            return false;
        memcpy(port_str, colon + 1, port_len);
        port_str[port_len] = '\0';
        *port = atoi(port_str);
    } else {
        *host = bstrndup(start, slash - start);
        *port = 80;
    }
    
    *path = bstrdup(*slash ? slash : "/");
    return true;
}

static bool send_websocket_handshake(struct websocket_client *client)
{
    struct dstr handshake;
    dstr_init(&handshake);
    
    // Generate WebSocket key (simplified - should use proper random)
    const char *ws_key = "dGhlIHNhbXBsZSBub25jZQ==";
    
    dstr_printf(&handshake,
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        client->path, client->host, client->port, ws_key);
    
    int sent = send(client->socket_fd, handshake.array, handshake.len, 0);
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
            
            if (client->on_message) {
                client->on_message(client->message_buffer.array, client->message_user_data);
            }
            
            dstr_free(&client->message_buffer);
            dstr_init(&client->message_buffer);
        } else {
            // Fragmented message
            dstr_ncat(&client->message_buffer, (char *)(data + pos), payload_len);
        }
    } else if (opcode == 0x8) { // Close frame
        client->connected = false;
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
        if (!client->connected && client->auto_reconnect) {
            websocket_client_connect(client);
            if (!client->connected) {
                os_sleep_ms(client->reconnect_interval_ms);
                continue;
            }
        }
        
        if (client->connected && client->socket_fd >= 0) {
            fd_set read_fds;
            struct timeval tv = {0, 100000}; // 100ms timeout
            
            FD_ZERO(&read_fds);
            FD_SET(client->socket_fd, &read_fds);
            
            int result = select(client->socket_fd + 1, &read_fds, NULL, NULL, &tv);
            if (result > 0 && FD_ISSET(client->socket_fd, &read_fds)) {
                int received = recv(client->socket_fd, client->recv_buffer, BUFFER_SIZE, 0);
                if (received > 0) {
                    process_websocket_frame(client, client->recv_buffer, received);
                } else if (received == 0 || (received < 0 && errno != EAGAIN)) {
                    // Connection closed
                    client->connected = false;
                    if (client->on_connection) {
                        client->on_connection(false, client->connection_user_data);
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
    client->socket_fd = -1;
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
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    return client;
}

void websocket_client_destroy(websocket_client_t *client)
{
    if (!client)
        return;
    
    websocket_client_disconnect(client);
    
    os_event_signal(client->stop_event);
    if (client->thread) {
        pthread_join(client->thread, NULL);
    }
    
    pthread_mutex_destroy(&client->mutex);
    os_event_destroy(client->stop_event);
    
    dstr_free(&client->message_buffer);
    bfree(client->url);
    bfree(client->host);
    bfree(client->path);
    bfree(client);
    
#ifdef _WIN32
    WSACleanup();
#endif
}

bool websocket_client_connect(websocket_client_t *client)
{
    if (!client || client->connected)
        return false;
    
    struct addrinfo hints = {0};
    struct addrinfo *result;
    char port_str[16];
    
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    snprintf(port_str, sizeof(port_str), "%d", client->port);
    
    if (getaddrinfo(client->host, port_str, &hints, &result) != 0) {
        if (client->on_error) {
            client->on_error("Failed to resolve host", client->error_user_data);
        }
        return false;
    }
    
    client->socket_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (client->socket_fd < 0) {
        freeaddrinfo(result);
        if (client->on_error) {
            client->on_error("Failed to create socket", client->error_user_data);
        }
        return false;
    }
    
    if (connect(client->socket_fd, result->ai_addr, result->ai_addrlen) < 0) {
        freeaddrinfo(result);
        close(client->socket_fd);
        client->socket_fd = -1;
        if (client->on_error) {
            client->on_error("Failed to connect to server", client->error_user_data);
        }
        return false;
    }
    
    freeaddrinfo(result);
    
    if (!send_websocket_handshake(client)) {
        close(client->socket_fd);
        client->socket_fd = -1;
        if (client->on_error) {
            client->on_error("WebSocket handshake failed", client->error_user_data);
        }
        return false;
    }
    
    client->connected = true;
    
    if (client->on_connection) {
        client->on_connection(true, client->connection_user_data);
    }
    
    if (!client->thread) {
        pthread_create(&client->thread, NULL, websocket_thread, client);
    }
    
    blog(LOG_INFO, "[Entei] WebSocket connected to %s", client->url);
    return true;
}

void websocket_client_disconnect(websocket_client_t *client)
{
    if (!client || !client->connected)
        return;
    
    client->connected = false;
    
    if (client->socket_fd >= 0) {
        // Send close frame
        uint8_t close_frame[2] = {0x88, 0x00}; // FIN + Close opcode, 0 length
        send(client->socket_fd, close_frame, 2, 0);
        
        close(client->socket_fd);
        client->socket_fd = -1;
    }
    
    if (client->on_connection) {
        client->on_connection(false, client->connection_user_data);
    }
    
    blog(LOG_INFO, "[Entei] WebSocket disconnected");
}

bool websocket_client_is_connected(const websocket_client_t *client)
{
    return client && client->connected;
}

void websocket_client_set_message_callback(websocket_client_t *client, 
                                          ws_message_callback callback, 
                                          void *user_data)
{
    if (!client)
        return;
    
    pthread_mutex_lock(&client->mutex);
    client->on_message = callback;
    client->message_user_data = user_data;
    pthread_mutex_unlock(&client->mutex);
}

void websocket_client_set_error_callback(websocket_client_t *client, 
                                       ws_error_callback callback, 
                                       void *user_data)
{
    if (!client)
        return;
    
    pthread_mutex_lock(&client->mutex);
    client->on_error = callback;
    client->error_user_data = user_data;
    pthread_mutex_unlock(&client->mutex);
}

void websocket_client_set_connection_callback(websocket_client_t *client,
                                            ws_connection_callback callback,
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
    
    client->auto_reconnect = enabled;
}

void websocket_client_set_reconnect_interval(websocket_client_t *client, uint32_t seconds)
{
    if (!client)
        return;
    
    client->reconnect_interval_ms = seconds * 1000;
}