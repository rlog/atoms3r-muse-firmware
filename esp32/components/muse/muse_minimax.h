#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MUSE_MINIMAX_CHUNK 1024
typedef struct {
    uint32_t job;
    size_t len;
    bool end;
    bool ok;
    uint8_t data[MUSE_MINIMAX_CHUNK];
} muse_minimax_packet_t;
/* One consumer (chat session, or the idle voice self-test). Zero means unavailable. */
uint32_t muse_minimax_start(const char *text);
bool muse_minimax_poll(uint32_t job, muse_minimax_packet_t *packet);
void muse_minimax_cancel(void);
#ifdef __cplusplus
}
#endif
