/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/* A local-only TCP bridge; TLS callers keep the original SNI/CN and Host.
 * Errors fail closed: never silently send application traffic directly. */
esp_err_t muse_proxy_route(const char *host, uint16_t port, uint16_t *local_port);
esp_err_t muse_proxy_http_url(const char *url, char *out, size_t cap,
                              char *origin, size_t origin_cap);
void muse_proxy_status(void);
#ifdef __cplusplus
}
#endif
