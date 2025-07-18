#include "entei-provider.h"
#include "plugin-support.h"
#include "websocket-client.h"

struct entei_caption_provider {
	obs_source_t *source;
	struct websocket_client *client;
	char *websocket_url;
};

static const char *entei_caption_provider_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Entei Caption Provider";
}

static void *entei_caption_provider_create(obs_data_t *settings, obs_source_t *source)
{
	struct entei_caption_provider *provider = malloc(sizeof(struct entei_caption_provider));
	provider->source = source;
	provider->client = NULL;
	provider->websocket_url = NULL;
	
	const char *url = obs_data_get_string(settings, "websocket_url");
	if (url && strlen(url) > 0) {
		provider->websocket_url = strdup(url);
		provider->client = websocket_client_create(url);
		
		if (provider->client) {
			websocket_client_set_connect_callback(provider->client, on_websocket_connect, provider);
			websocket_client_set_message_callback(provider->client, on_websocket_message, provider);
		}
	}
	
	obs_log(LOG_INFO, "Entei caption provider created");
	
	return provider;
}

static void entei_caption_provider_destroy(void *data)
{
	struct entei_caption_provider *provider = data;
	
	if (provider->client) {
		websocket_client_destroy(provider->client);
	}
	
	free(provider->websocket_url);
	
	obs_log(LOG_INFO, "Entei caption provider destroyed");
	
	free(provider);
}

static void on_websocket_connect(bool connected, void *user_data)
{
	struct entei_caption_provider *provider = (struct entei_caption_provider *)user_data;
	
	if (connected) {
		obs_log(LOG_INFO, "Entei provider: WebSocket connected successfully");
	} else {
		obs_log(LOG_WARNING, "Entei provider: WebSocket connection failed or disconnected");
	}
}

static void on_websocket_message(const char *message, size_t len, void *user_data)
{
	struct entei_caption_provider *provider = (struct entei_caption_provider *)user_data;
	
	obs_log(LOG_INFO, "Entei provider received message: %.*s", (int)len, message);
	
	// TODO: Parse Phoenix message and extract transcription text
	// TODO: Forward to OBS caption system
}

static bool connect_button_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	
	struct entei_caption_provider *provider = (struct entei_caption_provider *)data;
	
	if (!provider->client) {
		obs_log(LOG_ERROR, "No WebSocket client available");
		return false;
	}
	
	if (websocket_client_is_connected(provider->client)) {
		obs_log(LOG_INFO, "Disconnecting from WebSocket server");
		websocket_client_disconnect(provider->client);
	} else {
		obs_log(LOG_INFO, "Connecting to WebSocket server");
		websocket_client_connect(provider->client);
	}
	
	return true;
}

static obs_properties_t *entei_caption_provider_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	
	obs_properties_add_text(props, "websocket_url", "WebSocket URL", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "status", "Connection Status", OBS_TEXT_INFO);
	obs_properties_add_button(props, "connect_button", "Connect/Disconnect", connect_button_clicked);
	
	return props;
}

static void entei_caption_provider_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "websocket_url", "ws://saya:7175/socket/websocket");
	obs_data_set_default_string(settings, "status", "Disconnected");
}

static struct obs_source_info entei_caption_provider_info = {
	.id = "entei_caption_provider",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_DO_NOT_DUPLICATE,
	.get_name = entei_caption_provider_get_name,
	.create = entei_caption_provider_create,
	.destroy = entei_caption_provider_destroy,
	.get_properties = entei_caption_provider_get_properties,
	.get_defaults = entei_caption_provider_get_defaults
};

void register_entei_caption_provider(void)
{
	obs_register_source(&entei_caption_provider_info);
}