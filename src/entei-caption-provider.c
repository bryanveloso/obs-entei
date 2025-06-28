#include "entei-caption-provider.h"
#include "websocket-client.h"
#include "plugin-support.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/dstr.h>

#ifdef JANSSON_FOUND
#include <jansson.h>
#endif

#define CAPTION_TIMEOUT_MS 10000
#define DEFAULT_WEBSOCKET_URL "ws://localhost:8889/events"

struct entei_caption_provider {
	obs_source_t *source;
	websocket_client_t *ws_client;

	pthread_mutex_t mutex;
	uint64_t last_caption_time;

	bool active;
	bool enabled;
	char *websocket_url;
	int reconnect_delay;
	bool show_partial;

	// Current caption state
	struct dstr current_caption;
	struct dstr pending_caption;
};

// Thread-local storage for JSON parsing results
static __thread char tls_type_buf[64];
static __thread char tls_text_buf[4096];

// Safe JSON field extraction with bounds checking
static bool extract_json_string(const char *json, const char *field_name, char *output, size_t output_size)
{
	if (!json || !field_name || !output || output_size == 0)
		return false;

	// Find field
	char search_str[128];
	snprintf(search_str, sizeof(search_str), "\"%s\"", field_name);
	const char *field_start = strstr(json, search_str);
	if (!field_start)
		return false;

	// Find colon after field name
	const char *colon = strchr(field_start + strlen(search_str), ':');
	if (!colon)
		return false;

	// Skip whitespace after colon
	const char *value_start = colon + 1;
	while (*value_start && (*value_start == ' ' || *value_start == '\t'))
		value_start++;

	// Check if it's a string value
	if (*value_start != '"')
		return false;
	value_start++;

	// Find end of string value
	const char *value_end = value_start;
	while (*value_end) {
		if (*value_end == '"' && *(value_end - 1) != '\\')
			break;
		value_end++;
	}

	if (*value_end != '"')
		return false;

	// Calculate length and check bounds
	size_t len = value_end - value_start;
	if (len >= output_size)
		len = output_size - 1;

	// Copy with escape sequence handling
	size_t out_pos = 0;
	for (size_t i = 0; i < len && out_pos < output_size - 1; i++) {
		if (value_start[i] == '\\' && i + 1 < len) {
			switch (value_start[i + 1]) {
			case 'n':
				output[out_pos++] = '\n';
				i++;
				break;
			case 'r':
				output[out_pos++] = '\r';
				i++;
				break;
			case 't':
				output[out_pos++] = '\t';
				i++;
				break;
			case '"':
				output[out_pos++] = '"';
				i++;
				break;
			case '\\':
				output[out_pos++] = '\\';
				i++;
				break;
			default:
				output[out_pos++] = value_start[i];
				break;
			}
		} else {
			output[out_pos++] = value_start[i];
		}
	}
	output[out_pos] = '\0';

	return true;
}

// Safe JSON parser with proper bounds checking
static bool parse_transcription_json(const char *json, const char **type, const char **text, bool *is_final)
{
	// Default values
	*type = NULL;
	*text = NULL;
	*is_final = true;

	// Validate input
	if (!json || strlen(json) > 65536) // Reasonable max JSON size
		return false;

	// Extract type field
	if (extract_json_string(json, "type", tls_type_buf, sizeof(tls_type_buf))) {
		*type = tls_type_buf;
	}

	// Extract text field
	if (extract_json_string(json, "text", tls_text_buf, sizeof(tls_text_buf))) {
		*text = tls_text_buf;
	}

	// Find is_final field
	const char *final_start = strstr(json, "\"is_final\"");
	if (final_start) {
		const char *colon = strchr(final_start + 10, ':');
		if (colon) {
			const char *value = colon + 1;
			while (*value && (*value == ' ' || *value == '\t'))
				value++;
			if (strncmp(value, "false", 5) == 0) {
				*is_final = false;
			}
		}
	}

	return *type && *text;
}

static void on_websocket_message(const char *message, void *user_data)
{
	struct entei_caption_provider *provider = user_data;

	const char *type;
	const char *text;
	bool is_final;

	if (!parse_transcription_json(message, &type, &text, &is_final)) {
		blog(LOG_WARNING, "[Entei] Failed to parse message: %s", message);
		return;
	}

	if (strcmp(type, "audio:transcription") != 0) {
		return;
	}

	pthread_mutex_lock(&provider->mutex);

	if (is_final || provider->show_partial) {
		obs_output_t *output = obs_frontend_get_streaming_output();
		if (output) {
			obs_output_output_caption_text2(output, text, 0.0);
			obs_output_release(output);
		}

		output = obs_frontend_get_recording_output();
		if (output) {
			obs_output_output_caption_text2(output, text, 0.0);
			obs_output_release(output);
		}

		// Update current caption
		dstr_copy(&provider->current_caption, text);
		provider->last_caption_time = os_gettime_ns() / 1000000; // Convert to ms

		blog(LOG_INFO, "[Entei] Caption sent: %s", text);
	} else {
		// Store partial caption
		dstr_copy(&provider->pending_caption, text);
	}

	pthread_mutex_unlock(&provider->mutex);
}

static void on_websocket_error(const char *error, void *user_data)
{
	UNUSED_PARAMETER(user_data);
	blog(LOG_ERROR, "[Entei] WebSocket error: %s", error);
}

static void on_websocket_connection(bool connected, void *user_data)
{
	UNUSED_PARAMETER(user_data);

	if (connected) {
		blog(LOG_INFO, "[Entei] Caption provider connected to WebSocket");
	} else {
		blog(LOG_INFO, "[Entei] Caption provider disconnected from WebSocket");
	}
}

entei_caption_provider_t *entei_caption_provider_create(obs_data_t *settings)
{
	struct entei_caption_provider *provider = bzalloc(sizeof(struct entei_caption_provider));

	pthread_mutex_init(&provider->mutex, NULL);
	dstr_init(&provider->current_caption);
	dstr_init(&provider->pending_caption);

	// Load settings
	provider->enabled = obs_data_get_bool(settings, "enabled");
	const char *url = obs_data_get_string(settings, "websocket_url");
	if (!url || strlen(url) == 0) {
		url = DEFAULT_WEBSOCKET_URL;
	}
	provider->websocket_url = bstrdup(url);
	provider->reconnect_delay = (int)obs_data_get_int(settings, "reconnect_delay");
	provider->show_partial = obs_data_get_bool(settings, "show_partial");

	// Create WebSocket client
	provider->ws_client = websocket_client_create(provider->websocket_url);
	if (provider->ws_client) {
		websocket_client_set_message_callback(provider->ws_client, on_websocket_message, provider);
		websocket_client_set_error_callback(provider->ws_client, on_websocket_error, provider);
		websocket_client_set_connection_callback(provider->ws_client, on_websocket_connection, provider);
		websocket_client_set_auto_reconnect(provider->ws_client, true);
		websocket_client_set_reconnect_interval(provider->ws_client, provider->reconnect_delay);
	}

	blog(LOG_INFO, "[Entei] Caption provider created");
	return provider;
}

void entei_caption_provider_destroy(entei_caption_provider_t *provider)
{
	if (!provider)
		return;

	entei_caption_provider_stop(provider);

	if (provider->ws_client) {
		websocket_client_destroy(provider->ws_client);
	}

	pthread_mutex_destroy(&provider->mutex);
	dstr_free(&provider->current_caption);
	dstr_free(&provider->pending_caption);
	bfree(provider->websocket_url);
	bfree(provider);

	blog(LOG_INFO, "[Entei] Caption provider destroyed");
}

void entei_caption_provider_start(entei_caption_provider_t *provider)
{
	if (!provider || provider->active || !provider->enabled)
		return;

	provider->active = true;

	if (provider->ws_client) {
		websocket_client_connect(provider->ws_client);
	}

	blog(LOG_INFO, "[Entei] Caption provider started");
}

void entei_caption_provider_stop(entei_caption_provider_t *provider)
{
	if (!provider || !provider->active)
		return;

	provider->active = false;

	if (provider->ws_client) {
		websocket_client_disconnect(provider->ws_client);
	}

	// Clear any remaining captions
	pthread_mutex_lock(&provider->mutex);
	dstr_free(&provider->current_caption);
	dstr_free(&provider->pending_caption);
	dstr_init(&provider->current_caption);
	dstr_init(&provider->pending_caption);
	pthread_mutex_unlock(&provider->mutex);

	blog(LOG_INFO, "[Entei] Caption provider stopped");
}

bool entei_caption_provider_is_active(const entei_caption_provider_t *provider)
{
	return provider && provider->active;
}

void entei_caption_provider_update(entei_caption_provider_t *provider, obs_data_t *settings)
{
	if (!provider)
		return;

	pthread_mutex_lock(&provider->mutex);

	// Update settings
	bool old_enabled = provider->enabled;
	provider->enabled = obs_data_get_bool(settings, "enabled");

	const char *new_url = obs_data_get_string(settings, "websocket_url");
	if (!new_url || strlen(new_url) == 0) {
		new_url = DEFAULT_WEBSOCKET_URL;
	}

	bool url_changed = strcmp(provider->websocket_url, new_url) != 0;
	if (url_changed) {
		bfree(provider->websocket_url);
		provider->websocket_url = bstrdup(new_url);
	}

	provider->reconnect_delay = (int)obs_data_get_int(settings, "reconnect_delay");
	provider->show_partial = obs_data_get_bool(settings, "show_partial");

	pthread_mutex_unlock(&provider->mutex);

	// Handle enable/disable state change
	if (old_enabled != provider->enabled) {
		if (provider->enabled) {
			// Just enabled - check if we should start
			// Only start if streaming/recording is active
			obs_output_t *streaming_output = obs_frontend_get_streaming_output();
			obs_output_t *recording_output = obs_frontend_get_recording_output();
			bool should_start = (streaming_output && obs_output_active(streaming_output)) ||
					    (recording_output && obs_output_active(recording_output));
			if (streaming_output)
				obs_output_release(streaming_output);
			if (recording_output)
				obs_output_release(recording_output);

			if (should_start) {
				entei_caption_provider_start(provider);
			}
		} else if (!provider->enabled) {
			// Just disabled - stop connection
			entei_caption_provider_stop(provider);
		}
	}

	// Recreate WebSocket client if URL changed
	if (url_changed && provider->ws_client) {
		bool was_active = provider->active;

		entei_caption_provider_stop(provider);
		websocket_client_destroy(provider->ws_client);

		provider->ws_client = websocket_client_create(provider->websocket_url);
		if (provider->ws_client) {
			websocket_client_set_message_callback(provider->ws_client, on_websocket_message, provider);
			websocket_client_set_error_callback(provider->ws_client, on_websocket_error, provider);
			websocket_client_set_connection_callback(provider->ws_client, on_websocket_connection,
								 provider);
			websocket_client_set_auto_reconnect(provider->ws_client, true);
			websocket_client_set_reconnect_interval(provider->ws_client, provider->reconnect_delay);
		}

		if (was_active) {
			entei_caption_provider_start(provider);
		}
	} else if (provider->ws_client) {
		websocket_client_set_reconnect_interval(provider->ws_client, provider->reconnect_delay);
	}
}

obs_properties_t *entei_caption_provider_properties(void)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_bool(props, "enabled", obs_module_text("EnableCaptions"));

	obs_properties_add_text(props, "websocket_url", obs_module_text("WebSocketURL"), OBS_TEXT_DEFAULT);

	obs_properties_add_int(props, "reconnect_delay", obs_module_text("ReconnectDelay"), 1, 60, 1);

	obs_properties_add_bool(props, "show_partial", obs_module_text("ShowPartialCaptions"));

	return props;
}

void entei_caption_provider_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "enabled", false);
	obs_data_set_default_string(settings, "websocket_url", DEFAULT_WEBSOCKET_URL);
	obs_data_set_default_int(settings, "reconnect_delay", 5);
	obs_data_set_default_bool(settings, "show_partial", false);
}

const char *entei_caption_provider_get_name(void)
{
	return obs_module_text("EnteiCaptionProvider");
}
