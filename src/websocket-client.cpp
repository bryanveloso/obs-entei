#include "websocket-client.h"
#include <obs-module.h>
#include "plugin-support.h"

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/client.hpp>

#include <thread>
#include <memory>
#include <string>
#include <mutex>

typedef websocketpp::client<websocketpp::config::asio> ws_client_t;
typedef websocketpp::config::asio::message_type::ptr message_ptr;

struct websocket_client {
	std::string url;
	std::string host;
	std::string path;
	int port;
	bool connected;
	bool should_stop;

	// WebSocket++ objects
	std::unique_ptr<ws_client_t> ws_client;
	websocketpp::connection_hdl connection_hdl;
	std::unique_ptr<std::thread> worker_thread;
	std::unique_ptr<asio::io_context> io_context;

	// Callbacks
	websocket_message_callback_t message_callback;
	void *message_user_data;
	websocket_connect_callback_t connect_callback;
	void *connect_user_data;

	std::mutex callback_mutex;
};

static bool parse_url(const std::string &url, std::string &host, std::string &path, int &port)
{
	if (url.substr(0, 5) == "ws://") {
		port = 80;
		size_t start = 5;
		size_t slash_pos = url.find('/', start);
		size_t colon_pos = url.find(':', start);

		if (colon_pos != std::string::npos && (slash_pos == std::string::npos || colon_pos < slash_pos)) {
			host = url.substr(start, colon_pos - start);
			size_t port_end = (slash_pos != std::string::npos) ? slash_pos : url.length();
			port = std::stoi(url.substr(colon_pos + 1, port_end - colon_pos - 1));
		} else if (slash_pos != std::string::npos) {
			host = url.substr(start, slash_pos - start);
		} else {
			host = url.substr(start);
		}

		if (slash_pos != std::string::npos) {
			path = url.substr(slash_pos);
		} else {
			path = "/";
		}
		return true;
	}

	return false;
}

extern "C" {

struct websocket_client *websocket_client_create(const char *url)
{
	if (!url) {
		obs_log(LOG_ERROR, "WebSocket URL cannot be null");
		return nullptr;
	}

	auto *client = new websocket_client();
	if (!client) {
		obs_log(LOG_ERROR, "Failed to allocate websocket_client");
		return nullptr;
	}

	client->url = url;
	client->connected = false;
	client->should_stop = false;
	client->message_callback = nullptr;
	client->message_user_data = nullptr;
	client->connect_callback = nullptr;
	client->connect_user_data = nullptr;

	if (!parse_url(client->url, client->host, client->path, client->port)) {
		obs_log(LOG_ERROR, "Failed to parse WebSocket URL: %s", url);
		delete client;
		return nullptr;
	}

	obs_log(LOG_INFO, "WebSocket client created for URL: %s (host: %s, port: %d, path: %s)", client->url.c_str(),
		client->host.c_str(), client->port, client->path.c_str());

	return client;
}

void websocket_client_destroy(struct websocket_client *client)
{
	if (!client)
		return;

	client->should_stop = true;

	if (client->worker_thread && client->worker_thread->joinable()) {
		if (client->io_context) {
			client->io_context->stop();
		}
		client->worker_thread->join();
	}

	obs_log(LOG_INFO, "WebSocket client destroyed");
	delete client;
}

bool websocket_client_connect(struct websocket_client *client)
{
	if (!client) {
		obs_log(LOG_ERROR, "WebSocket client is null");
		return false;
	}

	try {
		client->io_context = std::make_unique<asio::io_context>();

		client->ws_client = std::make_unique<ws_client_t>();
		client->ws_client->clear_access_channels(websocketpp::log::alevel::all);
		client->ws_client->clear_error_channels(websocketpp::log::elevel::all);
		client->ws_client->init_asio(client->io_context.get());

		client->ws_client->set_open_handler([client](websocketpp::connection_hdl hdl) {
			std::lock_guard<std::mutex> lock(client->callback_mutex);
			obs_log(LOG_INFO, "WebSocket connection established");
			client->connected = true;
			client->connection_hdl = hdl;
			if (client->connect_callback) {
				client->connect_callback(true, client->connect_user_data);
			}
		});

		client->ws_client->set_fail_handler([client](websocketpp::connection_hdl hdl) {
			(void)hdl;
			std::lock_guard<std::mutex> lock(client->callback_mutex);
			obs_log(LOG_ERROR, "WebSocket connection failed");
			client->connected = false;
			if (client->connect_callback) {
				client->connect_callback(false, client->connect_user_data);
			}
		});

		client->ws_client->set_close_handler([client](websocketpp::connection_hdl hdl) {
			(void)hdl;
			std::lock_guard<std::mutex> lock(client->callback_mutex);
			obs_log(LOG_INFO, "WebSocket connection closed");
			client->connected = false;
			if (client->connect_callback) {
				client->connect_callback(false, client->connect_user_data);
			}
		});

		client->ws_client->set_message_handler([client](websocketpp::connection_hdl hdl, message_ptr msg) {
			(void)hdl;
			std::lock_guard<std::mutex> lock(client->callback_mutex);
			if (client->message_callback) {
				const std::string &payload = msg->get_payload();
				client->message_callback(payload.c_str(), payload.size(), client->message_user_data);
			}
		});

		std::string uri = client->url;
		websocketpp::lib::error_code ec;
		auto con = client->ws_client->get_connection(uri, ec);
		if (ec) {
			obs_log(LOG_ERROR, "Failed to create WebSocket connection: %s", ec.message().c_str());
			return false;
		}

		con->add_subprotocol("phoenix");
		client->ws_client->connect(con);

		// Start worker thread
		client->worker_thread = std::make_unique<std::thread>([client]() {
			try {
				client->io_context->run();
			} catch (const std::exception &e) {
				obs_log(LOG_ERROR, "WebSocket worker thread exception: %s", e.what());
			}
		});

		obs_log(LOG_INFO, "WebSocket client connecting to %s:%d%s", client->host.c_str(), client->port,
			client->path.c_str());
		return true;

	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "WebSocket connection exception: %s", e.what());
		return false;
	}
}

void websocket_client_disconnect(struct websocket_client *client)
{
	if (!client)
		return;

	try {
		if (client->connected && client->ws_client) {
			websocketpp::lib::error_code ec;
			client->ws_client->close(client->connection_hdl, websocketpp::close::status::going_away, "",
						 ec);
		}

		client->connected = false;
		obs_log(LOG_INFO, "WebSocket client disconnect requested");

	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "WebSocket disconnect exception: %s", e.what());
	}
}

bool websocket_client_is_connected(struct websocket_client *client)
{
	return client ? client->connected : false;
}

void websocket_client_send(struct websocket_client *client, const char *message)
{
	if (!client || !message) {
		obs_log(LOG_WARNING, "Invalid parameters for websocket_client_send");
		return;
	}

	if (!client->connected) {
		obs_log(LOG_WARNING, "Cannot send message: WebSocket not connected");
		return;
	}

	try {
		websocketpp::lib::error_code ec;
		if (client->ws_client) {
			client->ws_client->send(client->connection_hdl, message, websocketpp::frame::opcode::text, ec);
		}

		if (ec) {
			obs_log(LOG_ERROR, "Failed to send WebSocket message: %s", ec.message().c_str());
		} else {
			obs_log(LOG_INFO, "WebSocket message sent: %s", message);
		}

	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "WebSocket send exception: %s", e.what());
	}
}

void websocket_client_set_message_callback(struct websocket_client *client, websocket_message_callback_t callback,
					   void *user_data)
{
	if (!client)
		return;

	std::lock_guard<std::mutex> lock(client->callback_mutex);
	client->message_callback = callback;
	client->message_user_data = user_data;
}

void websocket_client_set_connect_callback(struct websocket_client *client, websocket_connect_callback_t callback,
					   void *user_data)
{
	if (!client)
		return;

	std::lock_guard<std::mutex> lock(client->callback_mutex);
	client->connect_callback = callback;
	client->connect_user_data = user_data;
}

} // extern "C"