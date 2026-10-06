#include "muse_tts_config.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdbool.h>
#if CONFIG_MUSE_MINIMAX_TTS
#include "nvs.h"
/* NVS serializes concurrent access. Each synthesis job snapshots the value. */
static bool valid_voice(const char *voice)
{
    if (!voice || !voice[0] || strlen(voice) > MUSE_TTS_VOICE_MAX) return false;
    for (const unsigned char *p = (const unsigned char *)voice; *p; ++p)
        if (*p < 32 || *p > 126 || *p == '"' || *p == '\\') return false;
    return true;
}
void muse_tts_get_voice(char out[MUSE_TTS_VOICE_MAX + 1])
{
    nvs_handle_t handle;
    char saved[MUSE_TTS_VOICE_MAX + 1];
    size_t size = sizeof(saved);
    bool loaded = false;
    if (nvs_open("muse_tts", NVS_READONLY, &handle) == ESP_OK) {
        loaded = nvs_get_str(handle, "voice", saved, &size) == ESP_OK && valid_voice(saved);
        nvs_close(handle);
    }
    const char *voice = loaded ? saved : CONFIG_MUSE_MINIMAX_VOICE;
    size_t len = strlen(voice);
    if (len > MUSE_TTS_VOICE_MAX) len = MUSE_TTS_VOICE_MAX;
    memcpy(out, voice, len);
    out[len] = '\0';
}
esp_err_t muse_tts_set_voice(const char *voice)
{
    if (!valid_voice(voice)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("muse_tts", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "voice", voice);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
#else
void muse_tts_get_voice(char out[MUSE_TTS_VOICE_MAX + 1]) { out[0] = '\0'; }
esp_err_t muse_tts_set_voice(const char *voice) { (void)voice; return ESP_ERR_NOT_SUPPORTED; }
#endif
