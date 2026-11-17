# SPDX-License-Identifier: Apache-2.0
"""Exercise MiniMax task startup with no contiguous internal space for its stack."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class MiniMaxWorker(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        out = Path(cls.tmp.name)
        source = (ROOT/'components/muse/muse_minimax.cpp').read_text()
        start = source.index('extern "C" uint32_t muse_minimax_start(')
        end = source.index('extern "C" bool muse_minimax_poll(', start)
        job_start = source.index('struct job_t {')
        job = source[job_start:source.index('};',job_start)+2]
        voice = next(line for line in (ROOT/'components/muse/muse_tts_config.h').read_text().splitlines()
                     if line.startswith('#define MUSE_TTS_VOICE_MAX '))
        harness = r'''
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include "muse_minimax.h"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define CONFIG_MUSE_MINIMAX_API_KEY "synthetic-test-key"
#define ESP_LOGE(...) ((void)0)
#define pdTRUE 1
#define pdPASS 1
''' + voice + '\n' + job + r'''
struct Queue { bool occupied = false; job_t job{}; };
static Queue *s_jobs, *s_audio;
static std::atomic<uint32_t> s_generation{0};
static int live_queues, internal_attempts, stack_attempts;
static bool fail_task;
static Queue *make_queue() { ++live_queues; return new Queue; }
static Queue *xQueueCreate(int, size_t) { return make_queue(); }
static Queue *xQueueCreateWithCaps(int, size_t, int) { return make_queue(); }
static void vQueueDelete(Queue *q) { --live_queues; delete q; }
static void vQueueDeleteWithCaps(Queue *q) { vQueueDelete(q); }
static int xQueueReceive(Queue *q, job_t *job, int) {
    if (!q->occupied) return 0;
    *job = q->job; q->occupied = false; return pdTRUE;
}
static int xQueueSend(Queue *q, const job_t *job, int) {
    q->job = *job; q->occupied = true; return pdTRUE;
}
static int xTaskCreate(void (*)(void *), const char *, unsigned, void *, int, void *) {
    ++internal_attempts; return 0; // internal heap cannot satisfy a 12 KiB stack
}
static int xTaskCreateWithCaps(void (*)(void *), const char *, unsigned bytes, void *, int, void *, int caps) {
    ++stack_attempts;
    return !fail_task && bytes >= 12288 && (caps & MALLOC_CAP_SPIRAM) ? pdPASS : 0;
}
static void *heap_caps_malloc(size_t n, int) { return malloc(n); }
static void muse_tts_get_voice(char *out) { strcpy(out,"saved-voice"); }
static void worker(void *) {}
''' + source[start:end] + r'''
int main(int argc, char **argv) {
    assert(argc == 2);
    fail_task = atoi(argv[1]) == 2;
    uint32_t id = muse_minimax_start("hello");
    assert(internal_attempts == 0 && stack_attempts == 1);
    if (fail_task) {
        assert(id == 0 && !s_jobs && !s_audio && live_queues == 0);
    } else {
        assert(id && live_queues == 2 && s_jobs->occupied);
        assert(!strcmp(s_jobs->job.text,"hello") && !strcmp(s_jobs->job.voice,"saved-voice"));
        free(s_jobs->job.text); vQueueDelete(s_jobs); vQueueDeleteWithCaps(s_audio);
        assert(live_queues == 0);
    }
}
'''
        (out/'worker.cpp').write_text(harness)
        cls.binary=out/'worker'
        subprocess.run([*shlex.split(os.environ.get('CXX','c++')), '-std=c++17',
                        '-Wall','-Wextra','-Werror','-Wno-unused-function',
                        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',
                        '-I',str(ROOT/'components/muse'),str(out/'worker.cpp'),'-o',str(cls.binary)],check=True)

    def test_worker_starts_when_only_psram_can_fit_the_stack(self):
        subprocess.run([str(self.binary),'1'],check=True)

    def test_failed_task_creation_cleans_up_queues(self):
        subprocess.run([str(self.binary),'2'],check=True)
