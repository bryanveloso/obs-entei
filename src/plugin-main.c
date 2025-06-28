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
#include <util/config-file.h>
#include <util/platform.h>
#include <plugin-support.h>
#include "entei-caption-provider.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static entei_caption_provider_t *caption_provider = NULL;
static obs_source_t *settings_source = NULL;

// Dummy source functions for settings
static const char *entei_settings_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("EnteiCaptionSettings");
}

static void *entei_settings_create(obs_data_t *settings, obs_source_t *source)
{
    UNUSED_PARAMETER(settings);
    UNUSED_PARAMETER(source);
    return (void *)1; // Return non-null
}

static void entei_settings_destroy(void *data)
{
    UNUSED_PARAMETER(data);
}

static void entei_settings_update(void *data, obs_data_t *settings)
{
    UNUSED_PARAMETER(data);
    
    // Update the actual caption provider
    if (caption_provider) {
        entei_caption_provider_update(caption_provider, settings);
    } else {
        // Create provider if it doesn't exist
        caption_provider = entei_caption_provider_create(settings);
    }
    
    // Save settings to file
    const char *settings_json = obs_data_get_json(settings);
    if (settings_json) {
        char *file = obs_module_config_path("settings.json");
        if (file) {
            os_quick_write_utf8_file(file, settings_json, strlen(settings_json), false);
            bfree(file);
        }
    }
}

static obs_properties_t *entei_settings_properties(void *data)
{
    UNUSED_PARAMETER(data);
    return entei_caption_provider_properties();
}

static void entei_settings_defaults(obs_data_t *settings)
{
    entei_caption_provider_defaults(settings);
}

static struct obs_source_info entei_settings_source_info = {
    .id = "entei_caption_settings",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_DO_NOT_DUPLICATE,
    .get_name = entei_settings_get_name,
    .create = entei_settings_create,
    .destroy = entei_settings_destroy,
    .update = entei_settings_update,
    .get_properties = entei_settings_properties,
    .get_defaults = entei_settings_defaults,
};

// Tools menu callback
static void entei_settings_callback(void *data)
{
    UNUSED_PARAMETER(data);
    
    if (settings_source) {
        obs_frontend_open_source_properties(settings_source);
    }
}

// Load settings from config
static void load_settings(void)
{
    obs_data_t *settings = NULL;
    
    char *file = obs_module_config_path("settings.json");
    if (file) {
        char *settings_json = os_quick_read_utf8_file(file);
        if (settings_json) {
            settings = obs_data_create_from_json(settings_json);
            bfree(settings_json);
        }
        bfree(file);
    }
    
    if (!settings) {
        settings = obs_data_create();
        entei_caption_provider_defaults(settings);
    }
    
    // Create caption provider with loaded settings
    caption_provider = entei_caption_provider_create(settings);
    
    // Apply settings to the settings source
    if (settings_source) {
        obs_source_update(settings_source, settings);
    }
    
    obs_data_release(settings);
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
        
    default:
        // Ignore other events
        break;
    }
}

bool obs_module_load(void)
{
    blog(LOG_INFO, "[Entei] Loading plugin (version %s)", PLUGIN_VERSION);
    blog(LOG_INFO, "[Entei] Network Transcript Interface caption provider for OBS Studio");
    
    // Register the settings source
    obs_register_source(&entei_settings_source_info);
    
    // Create the settings source
    settings_source = obs_source_create_private("entei_caption_settings", "Entei Caption Settings", NULL);
    
    // Load saved settings and create caption provider
    load_settings();
    
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
    
    // Clean up
    if (settings_source) {
        obs_source_release(settings_source);
        settings_source = NULL;
    }
    
    if (caption_provider) {
        entei_caption_provider_destroy(caption_provider);
        caption_provider = NULL;
    }
    
    obs_frontend_remove_event_callback(on_event, NULL);
    
    blog(LOG_INFO, "[Entei] Plugin unloaded");
}
