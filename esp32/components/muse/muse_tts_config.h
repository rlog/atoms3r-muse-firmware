#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MUSE_TTS_VOICE_MAX 128
void muse_tts_get_voice(char out[MUSE_TTS_VOICE_MAX + 1]);
esp_err_t muse_tts_set_voice(const char *voice);
#ifdef __cplusplus
}
#endif
