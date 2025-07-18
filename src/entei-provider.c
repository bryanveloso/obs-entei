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

static obs_properties_t *entei_caption_provider_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	
	obs_properties_t *props = obs_properties_create();
	
	obs_properties_add_text(props, "websocket_url", "WebSocket URL", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "status", "Connection Status", OBS_TEXT_INFO);
	
	return props;
}

static void entei_caption_provider_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "websocket_url", "ws://localhost:7175/socket/websocket");
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