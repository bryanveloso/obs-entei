/*
Entei Caption Provider for OBS Studio
Copyright (C) 2024 Bryan Veloso <bryan@avalonstar.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include "entei-caption-provider.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static entei_caption_provider_t *caption_provider = NULL;
static obs_data_t *saved_settings = NULL;

// Settings dialog
static void entei_settings_callback(void *data)
{
    UNUSED_PARAMETER(data);
    
    obs_frontend_push_ui_translation(obs_module_get_string);
    
    // Create properties
    obs_properties_t *props = entei_caption_provider_properties();
    
    // Get current settings
    if (!saved_settings) {
        saved_settings = obs_data_create();
        entei_caption_provider_defaults(saved_settings);
    }
    
    // Show properties dialog
    bool settings_changed = obs_frontend_open_source_properties_dialog(
        NULL, 
        obs_module_text("EnteiCaptionSettings"),
        saved_settings,
        props,
        NULL
    );
    
    obs_properties_destroy(props);
    
    if (settings_changed) {
        // Apply new settings
        if (caption_provider) {
            entei_caption_provider_update(caption_provider, saved_settings);
        } else {
            // Create provider if it doesn't exist
            caption_provider = entei_caption_provider_create(saved_settings);
        }
        
        // Save settings to config
        char *settings_json = obs_data_get_json(saved_settings);
        if (settings_json) {
            config_t *config = obs_frontend_get_global_config();
            if (config) {
                config_set_string(config, "EnteiCaptionProvider", "Settings", settings_json);
            }
            bfree(settings_json);
        }
    }
    
    obs_frontend_pop_ui_translation();
}

// Load settings from config
static void load_settings(void)
{
    config_t *config = obs_frontend_get_global_config();
    if (!config)
        return;
    
    const char *settings_json = config_get_string(config, "EnteiCaptionProvider", "Settings");
    if (settings_json && strlen(settings_json) > 0) {
        saved_settings = obs_data_create_from_json(settings_json);
    }
    
    if (!saved_settings) {
        saved_settings = obs_data_create();
        entei_caption_provider_defaults(saved_settings);
    }
}

// Frontend event handling
static void on_event(enum obs_frontend_event event, void *private_data)
{
    UNUSED_PARAMETER(private_data);
    
    switch (event) {
    case OBS_FRONTEND_EVENT_STREAMING_STARTING:
        blog(LOG_INFO, "[Entei] Streaming starting");
        if (caption_provider && !entei_caption_provider_is_active(caption_provider)) {
            entei_caption_provider_start(caption_provider);
        }
        break;
        
    case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
        blog(LOG_INFO, "[Entei] Streaming stopping");
        if (caption_provider && entei_caption_provider_is_active(caption_provider)) {
            entei_caption_provider_stop(caption_provider);
        }
        break;
        
    case OBS_FRONTEND_EVENT_RECORDING_STARTING:
        blog(LOG_INFO, "[Entei] Recording starting");
        if (caption_provider && !entei_caption_provider_is_active(caption_provider)) {
            entei_caption_provider_start(caption_provider);
        }
        break;
        
    case OBS_FRONTEND_EVENT_RECORDING_STOPPING:
        blog(LOG_INFO, "[Entei] Recording stopping");
        // Don't stop if we're still streaming
        obs_output_t *streaming_output = obs_frontend_get_streaming_output();
        bool still_streaming = streaming_output && obs_output_active(streaming_output);
        if (streaming_output) {
            obs_output_release(streaming_output);
        }
        
        if (!still_streaming && caption_provider && entei_caption_provider_is_active(caption_provider)) {
            entei_caption_provider_stop(caption_provider);
        }
        break;
    }
}

bool obs_module_load(void)
{
    blog(LOG_INFO, "[Entei] Loading plugin (version %s)", PLUGIN_VERSION);
    blog(LOG_INFO, "[Entei] Network Transcript Interface caption provider for OBS Studio");
    
    // Load saved settings
    load_settings();
    
    // Create caption provider
    caption_provider = entei_caption_provider_create(saved_settings);
    
    // Add Tools menu item
    obs_frontend_add_tools_menu_item(
        obs_module_text("EnteiCaptionProvider"),
        entei_settings_callback,
        NULL
    );
    
    // Subscribe to frontend events
    obs_frontend_add_event_callback(on_event, NULL);
    
    blog(LOG_INFO, "[Entei] Plugin loaded successfully");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[Entei] Unloading plugin");
    
    // Save settings before unloading
    if (saved_settings) {
        char *settings_json = obs_data_get_json(saved_settings);
        if (settings_json) {
            config_t *config = obs_frontend_get_global_config();
            if (config) {
                config_set_string(config, "EnteiCaptionProvider", "Settings", settings_json);
            }
            bfree(settings_json);
        }
        obs_data_release(saved_settings);
        saved_settings = NULL;
    }
    
    // Clean up
    if (caption_provider) {
        entei_caption_provider_destroy(caption_provider);
        caption_provider = NULL;
    }
    
    obs_frontend_remove_event_callback(on_event, NULL);
    
    blog(LOG_INFO, "[Entei] Plugin unloaded");
}