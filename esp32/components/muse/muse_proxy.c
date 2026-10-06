/* SPDX-License-Identifier: Apache-2.0 */
#include "muse_proxy.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

#if CONFIG_MUSE_SS2022_PROXY
#include "muse_ss2022.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "psa/crypto.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "mbedtls/base64.h"

#define ROUTES 6
#define CONNECTIONS 4
#define IO_SIZE 2048
static const char *TAG = "muse_proxy";
typedef struct { char host[256]; uint16_t port, local; int fd; } route_t;
typedef struct { int client; route_t *route; } job_t;
static route_t s_routes[ROUTES];
static uint8_t s_psk[16];
static bool s_initialized;
static StaticSemaphore_t s_lock_mem;
static SemaphoreHandle_t s_lock;
static portMUX_TYPE s_guard = portMUX_INITIALIZER_UNLOCKED;
static unsigned s_active, s_total, s_errors;

static void *big_alloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static bool send_all(int fd, const uint8_t *p, size_t n) {
    while (n) {
        ssize_t done = send(fd, p, n, 0);
        if (done < 0 && errno == EINTR) continue;
        if (done <= 0) return false;
        p += done; n -= done;
    }
    return true;
}
static bool emit(void *arg, const uint8_t *p, size_t n) { return send_all(*(int *)arg, p, n); }
static void io_timeout(int fd) {
    struct timeval tv = { .tv_sec = 5 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int yes = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
}
static int dial(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(CONFIG_MUSE_SS2022_PORT) };
    if (inet_pton(AF_INET, CONFIG_MUSE_SS2022_SERVER, &a.sin_addr) != 1) { close(fd); return -1; }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(fd, (struct sockaddr *)&a, sizeof(a));
    if (rc < 0 && errno == EINPROGRESS) {
        fd_set w; FD_ZERO(&w); FD_SET(fd, &w);
        struct timeval tv = { .tv_sec = 12 };
        rc = select(fd + 1, NULL, &w, NULL, &tv);
        int error = 0; socklen_t n = sizeof(error);
        if (rc > 0 && (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &n) || error)) rc = -1;
        else if (rc > 0) rc = 0;
        else rc = -1;
    }
    if (rc < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, flags); io_timeout(fd); return fd;
}
static void bridge(void *arg) {
    job_t *j = arg;
    int local = j->client, remote = -1;
    route_t *route = j->route;
    free(j);
    ss2022_cipher_t tx = {0};
    ss2022_rx_t *rx = big_alloc(sizeof(*rx));
    uint8_t *input = big_alloc(IO_SIZE), *wire = big_alloc(IO_SIZE + 1200);
    bool ok = false, rx_ready = false;
    uint8_t salt[16], padding[64];
    esp_fill_random(salt, sizeof(salt)); esp_fill_random(padding, sizeof(padding));
    size_t wire_len = 0;
    if (!rx || !input || !wire) goto done;
    ss2022_rx_init(rx, s_psk, salt); rx_ready = true;
    if (!ss2022_cipher_init(&tx, s_psk, salt)) goto done;
    remote = dial();
    if (remote < 0) { ESP_LOGW(TAG, "node TCP connect failed"); goto done; }
    io_timeout(local);
    if (!ss2022_request(&tx, salt, route->host, route->port, time(NULL), padding,
                         1 + padding[0] % sizeof(padding), wire, IO_SIZE + 1200, &wire_len) ||
        !send_all(remote, wire, wire_len)) goto done;
    ESP_LOGI(TAG, "SS2022 tunnel -> %s:%u", route->host, route->port);
    int64_t last_io = esp_timer_get_time();
    for (;;) {
        fd_set read; FD_ZERO(&read); FD_SET(local, &read); FD_SET(remote, &read);
        struct timeval tv = { .tv_sec = 1 };
        int rc = select((local > remote ? local : remote) + 1, &read, NULL, NULL, &tv);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0 || esp_timer_get_time() - last_io > 180000000LL) break;
        if (!rc) continue;
        if (FD_ISSET(local, &read)) {
            ssize_t n = recv(local, input, IO_SIZE, 0);
            if (n == 0) { ok = true; break; }
            if (n < 0 || !ss2022_payload(&tx, input, n, wire, IO_SIZE + 1200, &wire_len) ||
                !send_all(remote, wire, wire_len)) break;
            last_io = esp_timer_get_time();
        }
        if (FD_ISSET(remote, &read)) {
            ssize_t n = recv(remote, input, IO_SIZE, 0);
            if (n == 0) { ok = true; break; }
            if (n < 0 || !ss2022_feed(rx, input, n, time(NULL), emit, &local)) {
                ESP_LOGW(TAG, "response authentication/IO failed for %s", route->host); break;
            }
            last_io = esp_timer_get_time();
        }
    }
done:
    if (remote >= 0) close(remote);
    close(local);
    ss2022_cipher_free(&tx);
    if (rx_ready) ss2022_rx_free(rx);
    free(rx); free(input); free(wire);
    taskENTER_CRITICAL(&s_guard); s_active--; if (!ok) s_errors++; taskEXIT_CRITICAL(&s_guard);
    vTaskDeleteWithCaps(NULL);
}
static void listener(void *arg) {
    route_t *r = arg;
    for (;;) {
        int fd = accept(r->fd, NULL, NULL);
        if (fd < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        taskENTER_CRITICAL(&s_guard);
        bool room = s_active < CONNECTIONS;
        if (room) { s_active++; s_total++; }
        taskEXIT_CRITICAL(&s_guard);
        if (!room) { close(fd); continue; }
        job_t *j = malloc(sizeof(*j));
        if (j) { j->client = fd; j->route = r; }
        if (!j || xTaskCreateWithCaps(bridge, "ss2022_io", 8192, j, 5, NULL,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            free(j); close(fd);
            taskENTER_CRITICAL(&s_guard); s_active--; s_errors++; taskEXIT_CRITICAL(&s_guard);
        }
    }
}
static esp_err_t initialize(void) {
    if (s_initialized) return ESP_OK;
    size_t n = 0;
    if (mbedtls_base64_decode(s_psk, sizeof(s_psk), &n,
        (const unsigned char *)CONFIG_MUSE_SS2022_KEY, strlen(CONFIG_MUSE_SS2022_KEY)) || n != 16) return ESP_ERR_INVALID_ARG;
    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "ntp.aliyun.com");
        esp_sntp_setservername(1, "time.cloudflare.com");
        esp_sntp_init();
    }
    s_initialized = true;
    return ESP_OK;
}
#endif

esp_err_t muse_proxy_route(const char *host, uint16_t port, uint16_t *local_port) {
    if (!host || !local_port || !host[0] || strlen(host) > 255 || port != 443) return ESP_ERR_INVALID_ARG;
#if CONFIG_MUSE_SS2022_PROXY
    taskENTER_CRITICAL(&s_guard);
    if (!s_lock) s_lock = xSemaphoreCreateMutexStatic(&s_lock_mem);
    taskEXIT_CRITICAL(&s_guard);
    if (!s_lock) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t result = initialize();
    /* No TLS/SS traffic with a 1970 timestamp. SNTP refreshes after reconnect. */
    for (int i = 0; result == ESP_OK && time(NULL) < 1735689600 && i < 150; i++) vTaskDelay(pdMS_TO_TICKS(100));
    if (result == ESP_OK && time(NULL) < 1735689600) { ESP_LOGW(TAG, "waiting for SNTP clock"); result = ESP_ERR_TIMEOUT; }
    if (result != ESP_OK) goto done;
    for (int i = 0; i < ROUTES; i++) {
        route_t *r = &s_routes[i];
        if (r->host[0] && !strcmp(r->host, host) && r->port == port) { *local_port = r->local; goto done; }
    }
    result = ESP_ERR_NO_MEM;
    for (int i = 0; i < ROUTES; i++) {
        route_t *r = &s_routes[i];
        if (r->host[0]) continue;
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) break;
        struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = 0 };
        socklen_t n = sizeof(a);
        if (bind(fd, (struct sockaddr *)&a, sizeof(a)) || listen(fd, 2) || getsockname(fd, (struct sockaddr *)&a, &n)) { close(fd); break; }
        strlcpy(r->host, host, sizeof(r->host)); r->port = port; r->local = ntohs(a.sin_port); r->fd = fd;
        if (xTaskCreate(listener, "ss2022_accept", 3072, r, 4, NULL) != pdPASS) { close(fd); memset(r, 0, sizeof(*r)); break; }
        *local_port = r->local; result = ESP_OK; break;
    }
done:
    xSemaphoreGive(s_lock); return result;
#else
    *local_port = port; return ESP_OK;
#endif
}
esp_err_t muse_proxy_http_url(const char *url, char *out, size_t cap, char *origin, size_t origin_cap) {
    if (!url || !out || !origin || strncmp(url, "https://", 8)) return ESP_ERR_INVALID_ARG;
    const char *path = strchr(url + 8, '/');
    size_t n = path ? (size_t)(path - url - 8) : strlen(url + 8);
    if (!n || n >= origin_cap || memchr(url + 8, ':', n) || memchr(url + 8, '@', n)) return ESP_ERR_INVALID_ARG;
    memcpy(origin, url + 8, n); origin[n] = 0;
#if CONFIG_MUSE_SS2022_PROXY
    uint16_t port;
    esp_err_t err = muse_proxy_route(origin, 443, &port);
    if (err != ESP_OK) return err;
    int written = snprintf(out, cap, "https://127.0.0.1:%u%s", port, path ? path : "/");
#else
    int written = snprintf(out, cap, "%s", url);
#endif
    return written >= 0 && (size_t)written < cap ? ESP_OK : ESP_ERR_INVALID_SIZE;
}
void muse_proxy_status(void) {
#if CONFIG_MUSE_SS2022_PROXY
    taskENTER_CRITICAL(&s_guard); unsigned active = s_active, total = s_total, errors = s_errors; taskEXIT_CRITICAL(&s_guard);
    printf("@proxy {\"enabled\":true,\"method\":\"2022-blake3-aes-128-gcm\",\"active\":%u,\"total\":%u,\"errors\":%u,\"clock\":%lld}\n", active, total, errors, (long long)time(NULL));
#else
    printf("@proxy {\"enabled\":false}\n");
#endif
    fflush(stdout);
}
