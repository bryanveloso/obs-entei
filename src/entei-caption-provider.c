#include "entei-caption-provider.h"
#include "websocket-client.h"
#include "plugin-support.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/dstr.h>
#include <jansson.h>

#define CAPTION_TIMEOUT_MS 10000
#define DEFAULT_WEBSOCKET_URL "ws://localhost:8889/events"

struct entei_caption_provider {
    obs_source_t *source;
    websocket_client_t *ws_client;
    
    pthread_mutex_t mutex;
    uint64_t last_caption_time;
    
    bool active;
    char *websocket_url;
    int reconnect_delay;
    bool show_partial;
    
    // Current caption state
    struct dstr current_caption;
    struct dstr pending_caption;
};

static void on_websocket_message(const char *message, void *user_data)
{
    struct entei_caption_provider *provider = user_data;
    
    // Parse JSON message
    json_error_t error;
    json_t *root = json_loads(message, 0, &error);
    if (!root) {
        blog(LOG_WARNING, "[Entei] Failed to parse JSON: %s", error.text);
        return;
    }
    
    // Extract fields
    const char *type = json_string_value(json_object_get(root, "type"));
    const char *text = json_string_value(json_object_get(root, "text"));
    json_t *timestamp_obj = json_object_get(root, "timestamp");
    json_t *is_final_obj = json_object_get(root, "is_final");
    
    if (!type || strcmp(type, "audio:transcription") != 0) {
        json_decref(root);
        return;
    }
    
    if (!text) {
        json_decref(root);
        return;
    }
    
    bool is_final = true;
    if (is_final_obj && json_is_boolean(is_final_obj)) {
        is_final = json_boolean_value(is_final_obj);
    }
    
    pthread_mutex_lock(&provider->mutex);
    
    if (is_final || provider->show_partial) {
        // Send caption to OBS
        struct obs_source_cea_708 caption = {0};
        caption.data = (uint8_t *)text;
        caption.size = strlen(text);
        caption.timestamp = os_gettime_ns();
        caption.format = OBS_SOURCE_CEA_708_TEXT;
        
        obs_output_t *output = obs_frontend_get_streaming_output();
        if (output) {
            obs_output_output_caption_text2(output, &caption);
            obs_output_release(output);
        }
        
        output = obs_frontend_get_recording_output();
        if (output) {
            obs_output_output_caption_text2(output, &caption);
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
    json_decref(root);
}

static void on_websocket_error(const char *error, void *user_data)
{
    blog(LOG_ERROR, "[Entei] WebSocket error: %s", error);
}

static void on_websocket_connection(bool connected, void *user_data)
{
    struct entei_caption_provider *provider = user_data;
    
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
    if (!provider || provider->active)
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
    
    // Recreate WebSocket client if URL changed
    if (url_changed && provider->ws_client) {
        bool was_active = provider->active;
        
        entei_caption_provider_stop(provider);
        websocket_client_destroy(provider->ws_client);
        
        provider->ws_client = websocket_client_create(provider->websocket_url);
        if (provider->ws_client) {
            websocket_client_set_message_callback(provider->ws_client, on_websocket_message, provider);
            websocket_client_set_error_callback(provider->ws_client, on_websocket_error, provider);
            websocket_client_set_connection_callback(provider->ws_client, on_websocket_connection, provider);
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
    
    obs_properties_add_text(props, "websocket_url", 
        obs_module_text("WebSocketURL"), OBS_TEXT_DEFAULT);
    
    obs_properties_add_int(props, "reconnect_delay",
        obs_module_text("ReconnectDelay"), 1, 60, 1);
    
    obs_properties_add_bool(props, "show_partial",
        obs_module_text("ShowPartialCaptions"));
    
    return props;
}

void entei_caption_provider_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "websocket_url", DEFAULT_WEBSOCKET_URL);
    obs_data_set_default_int(settings, "reconnect_delay", 5);
    obs_data_set_default_bool(settings, "show_partial", false);
}

const char *entei_caption_provider_get_name(void)
{
    return obs_module_text("EnteiCaptionProvider");
}