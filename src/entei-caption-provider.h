#pragma once

#include <obs-module.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct entei_caption_provider entei_caption_provider_t;

entei_caption_provider_t *entei_caption_provider_create(obs_data_t *settings);
void entei_caption_provider_destroy(entei_caption_provider_t *provider);

void entei_caption_provider_start(entei_caption_provider_t *provider);
void entei_caption_provider_stop(entei_caption_provider_t *provider);
bool entei_caption_provider_is_active(const entei_caption_provider_t *provider);

void entei_caption_provider_update(entei_caption_provider_t *provider, obs_data_t *settings);
obs_properties_t *entei_caption_provider_properties(void);
void entei_caption_provider_defaults(obs_data_t *settings);

const char *entei_caption_provider_get_name(void);

#ifdef __cplusplus
}
#endif