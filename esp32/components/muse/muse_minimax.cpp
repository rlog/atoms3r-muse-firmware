#include "muse_proxy.h"
/* MiniMax HTTP SSE -> generation-tagged MP3 packets. No credentials in logs. */
#include "muse_minimax.h"
#include "muse_tts_config.h"
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "sdkconfig.h"

#if CONFIG_MUSE_MINIMAX_TTS
static const char *TAG = "muse_minimax";
static constexpr size_t LINE_CAP = 256 * 1024;
struct job_t { uint32_t id; char *text; char voice[MUSE_TTS_VOICE_MAX + 1]; };
static QueueHandle_t s_jobs, s_audio;
static std::atomic<uint32_t> s_generation{0};

static bool current(uint32_t id) { return id == s_generation.load(); }
static bool send_packet(muse_minimax_packet_t &p)
{
    while (current(p.job)) {
        if (xQueueSend(s_audio, &p, pdMS_TO_TICKS(20)) == pdTRUE) return true;
    }
    return false;
}
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* Reject malformed audio instead of emitting a corrupt MP3 prefix. */
static bool send_hex(uint32_t id, const char *hex)
{
    size_t n = strlen(hex);
    if (n & 1) return false;
    for (size_t i = 0; i < n; ++i) if (hex_digit(hex[i]) < 0) return false;
    muse_minimax_packet_t packet{};
    packet.job = id;
    for (size_t i = 0; i < n; i += 2) {
        packet.data[packet.len++] = (hex_digit(hex[i]) << 4) | hex_digit(hex[i + 1]);
        if (packet.len == sizeof(packet.data)) {
            if (!send_packet(packet)) return false;
            packet.len = 0;
        }
    }
    return !packet.len || send_packet(packet);
}
/* SSE lines may be split anywhere by TCP; only complete data lines reach here. */
static bool event(uint32_t id, const char *line, bool &finished)
{
    if (strncmp(line, "data:", 5)) return true;
    const char *payload = line + 5;
    while (*payload == ' ') ++payload;
    if (!strcmp(payload, "[DONE]")) return true;
    cJSON *root = cJSON_Parse(payload);
    if (!root) return false;
    cJSON *base = cJSON_GetObjectItemCaseSensitive(root, "base_resp");
    cJSON *code = cJSON_GetObjectItemCaseSensitive(base, "status_code");
    bool ok = !cJSON_IsNumber(code) || code->valueint == 0;
    if (!ok) ESP_LOGW(TAG, "API error code %d", code->valueint);
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    const char *audio = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "audio"));
    if (ok && audio) ok = send_hex(id, audio);
    cJSON *status = cJSON_GetObjectItemCaseSensitive(data, "status");
    if (ok && cJSON_IsNumber(status) && status->valueint == 2) finished = true;
    cJSON_Delete(root);
    return ok;
}
static bool synthesize(const job_t &job)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "model", CONFIG_MUSE_MINIMAX_MODEL);
    cJSON_AddStringToObject(root, "text", job.text);
    cJSON_AddBoolToObject(root, "stream", true);
    cJSON *options = cJSON_AddObjectToObject(root, "stream_options");
    cJSON_AddBoolToObject(options, "exclude_aggregated_audio", true);
    cJSON *voice = cJSON_AddObjectToObject(root, "voice_setting");
    cJSON_AddStringToObject(voice, "voice_id", job.voice);
    cJSON_AddNumberToObject(voice, "speed", 1);
    cJSON_AddNumberToObject(voice, "vol", 1);
    cJSON_AddNumberToObject(voice, "pitch", 0);
    cJSON *audio = cJSON_AddObjectToObject(root, "audio_setting");
    cJSON_AddNumberToObject(audio, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio, "bitrate", 64000);
    cJSON_AddStringToObject(audio, "format", "mp3");
    cJSON_AddNumberToObject(audio, "channel", 1);
    cJSON_AddStringToObject(root, "language_boost", "Chinese");
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return false;
    char url[384], origin[256];
    if (muse_proxy_http_url(CONFIG_MUSE_MINIMAX_URL, url, sizeof(url), origin, sizeof(origin)) != ESP_OK) { free(body); return false; }
    esp_http_client_config_t cfg{};
    cfg.url = url;
    cfg.common_name = origin;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.buffer_size = 2048;
    cfg.buffer_size_tx = 2048;
    cfg.disable_auto_redirect = true; /* never forward the bearer token to a redirect */
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    char authorization[sizeof(CONFIG_MUSE_MINIMAX_API_KEY) + 8];
    snprintf(authorization, sizeof(authorization), "Bearer %s", CONFIG_MUSE_MINIMAX_API_KEY);
    bool ok = http != nullptr;
    if (ok) {
        ok = esp_http_client_set_header(http, "Host", origin) == ESP_OK &&
             esp_http_client_set_header(http, "Authorization", authorization) == ESP_OK &&
             esp_http_client_set_header(http, "Content-Type", "application/json") == ESP_OK &&
             esp_http_client_open(http, strlen(body)) == ESP_OK;
    }
    if (ok) {
        size_t sent = 0, len = strlen(body);
        while (sent < len && current(job.id)) {
            int n = esp_http_client_write(http, body + sent, len - sent);
            if (n <= 0) { ok = false; break; }
            sent += n;
        }
        ok = ok && sent == len;
    }
    free(body);
    if (ok) {
        ok = esp_http_client_fetch_headers(http) >= 0;
        int status = esp_http_client_get_status_code(http);
        if (status != 200) { ESP_LOGW(TAG, "HTTP status %d", status); ok = false; }
    }
    char *line = ok ? static_cast<char *>(heap_caps_malloc(LINE_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) : nullptr;
    if (!line) ok = false;
    size_t used = 0;
    bool finished = false;
    char buffer[2048];
    int64_t deadline = esp_timer_get_time() + 120 * 1000000LL;
    while (ok && !finished && current(job.id) && esp_timer_get_time() < deadline) {
        int n = esp_http_client_read(http, buffer, sizeof(buffer));
        if (n <= 0) { ok = false; break; }
        for (int i = 0; i < n && ok && !finished; ++i) {
            char c = buffer[i];
            if (c == '\n') {
                line[used] = '\0';
                ok = event(job.id, line, finished);
                used = 0;
            } else if (c != '\r') {
                if (used + 1 >= LINE_CAP) { ok = false; break; }
                line[used++] = c;
            }
        }
    }
    free(line);
    if (http) esp_http_client_cleanup(http);
    return ok && finished && current(job.id);
}
static void worker(void *)
{
    job_t job;
    for (;;) {
        xQueueReceive(s_jobs, &job, portMAX_DELAY);
        if (current(job.id)) {
            ESP_LOGI(TAG, "synthesizing %u bytes, voice=%s", (unsigned)strlen(job.text), job.voice);
            bool ok = synthesize(job);
            muse_minimax_packet_t end{};
            end.job = job.id; end.end = true; end.ok = ok;
            send_packet(end);
            if (current(job.id)) ESP_LOGI(TAG, "synthesis %s", ok ? "complete" : "failed");
        }
        free(job.text);
    }
}
extern "C" uint32_t muse_minimax_start(const char *text)
{
    if (!text || !*text || !CONFIG_MUSE_MINIMAX_API_KEY[0]) return 0;
    if (!s_jobs) {
        s_jobs = xQueueCreate(1, sizeof(job_t));
        s_audio = xQueueCreateWithCaps(24, sizeof(muse_minimax_packet_t), MALLOC_CAP_SPIRAM);
        if (!s_jobs || !s_audio || xTaskCreate(worker, "minimax_tts", 12288, nullptr, 4, nullptr) != pdPASS) {
            if (s_jobs) vQueueDelete(s_jobs);
            if (s_audio) vQueueDeleteWithCaps(s_audio);
            s_jobs = s_audio = nullptr;
            return 0;
        }
    }
    char *copy = static_cast<char *>(heap_caps_malloc(strlen(text) + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!copy) return 0;
    strcpy(copy, text);
    uint32_t id = ++s_generation;
    job_t old;
    if (xQueueReceive(s_jobs, &old, 0) == pdTRUE) free(old.text);
    job_t job{};
    job.id = id; job.text = copy;
    muse_tts_get_voice(job.voice);
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) { free(copy); return 0; }
    return id;
}
extern "C" bool muse_minimax_poll(uint32_t job, muse_minimax_packet_t *packet)
{
    if (!s_audio) return false;
    while (xQueueReceive(s_audio, packet, 0) == pdTRUE) {
        if (packet->job == job) return true;
    }
    return false;
}
extern "C" void muse_minimax_cancel(void) { ++s_generation; }
#else
extern "C" uint32_t muse_minimax_start(const char *) { return 0; }
extern "C" bool muse_minimax_poll(uint32_t, muse_minimax_packet_t *) { return false; }
extern "C" void muse_minimax_cancel(void) {}
#endif
