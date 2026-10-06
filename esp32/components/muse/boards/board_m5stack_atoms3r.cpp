/* AtomS3R + Atomic Echo Base port.
 * Display/backlight autodetection: https://github.com/m5stack/M5GFX src/M5GFX.cpp
 * Button and audio pin map: https://github.com/m5stack/M5Unified src/M5Unified.inl
 * M5GFX handles both GC9107 and ST7735 AtomS3R hardware revisions. */
#include "M5GFX.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "muse_board.h"
#include "muse_mem.h"

extern "C" esp_err_t atoms3r_audio_init(esp_codec_dev_handle_t *, esp_codec_dev_handle_t *);
static M5GFX lcd;
static SemaphoreHandle_t lock;
static muse_gpio_button_t button;

static bool display_lock(int ms)
{
    return xSemaphoreTakeRecursive(lock, ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(ms)) == pdTRUE;
}
static void display_unlock(void) { xSemaphoreGiveRecursive(lock); }
static uint32_t tick(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels)
{
    lcd.pushImage(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1,
                  (uint16_t *)pixels);
    lv_display_flush_ready(disp);
}
static void ui_task(void *)
{
    for (;;) {
        display_lock(-1);
        uint32_t delay = lv_timer_handler();
        display_unlock();
        vTaskDelay(pdMS_TO_TICKS(delay < 5 ? 5 : delay > 20 ? 20 : delay));
    }
}
static esp_err_t init(void)
{
    lock = xSemaphoreCreateRecursiveMutex();
    if (!lock) return ESP_ERR_NO_MEM;
    return muse_gpio_button_init(&button, GPIO_NUM_41);
}
static lv_display_t *display_start(lv_indev_t **touch)
{
    *touch = nullptr;
    if (!lcd.init() || lcd.getBoard() != m5gfx::board_t::board_M5AtomS3R) {
        ESP_LOGE("board", "AtomS3R panel detection failed");
        return nullptr;
    }
    lcd.setRotation(0);
    /* LVGL supplies native little-endian RGB565; M5GFX converts to panel order. */
    lcd.setSwapBytes(true);
    lcd.setBrightness(160);
    lcd.fillScreen(0);
    ESP_LOGI("board", "AtomS3R LCD: %dx%d, vendor autodetection complete", lcd.width(), lcd.height());
    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t *disp = lv_display_create(128, 128);
    if (!disp) return nullptr;
    void *buf = heap_caps_malloc(128 * 32 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) { lv_display_delete(disp); return nullptr; }
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf, nullptr, 128 * 32 * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush);
    if (xTaskCreatePinnedToCore(ui_task, "atoms3r_lvgl", 6144, nullptr, MUSE_UI_PRIORITY,
                               nullptr, MUSE_UI_CORE) != pdPASS) return nullptr;
    return disp;
}
static void brightness(int pct) { lcd.setBrightness(pct * 255 / 100); }
static void panel_sleep(bool sleep) { if (sleep) lcd.sleep(); else lcd.wakeup(); }
static unsigned buttons(void) { return muse_gpio_button_poll(&button); }
static esp_err_t read_power(muse_power_t *p)
{
    p->usb = true; p->charging = false; p->battery_pct = -1; p->battery_mv = 0;
    return ESP_OK;
}
static esp_err_t power_off(void)
{
    brightness(0);
    lcd.sleep();
    while (gpio_get_level(GPIO_NUM_41) == 0) vTaskDelay(pdMS_TO_TICKS(20));
    /* GPIO41 is not an RTC pad on S3. Use the physical reset button to wake. */
    esp_deep_sleep_start();
    return ESP_FAIL;
}
static const muse_board_t board = {
    .name = "M5Stack AtomS3R + Atomic Echo Base",
    .width = 128, .height = 128, .round = false, .touch = false,
    .diagonal_in = 0.85f, .keyboard = false,
    .talk_button = "screen", .aux_button = nullptr,
    .talk_hint = { LV_ALIGN_BOTTOM_RIGHT, -8, -4 }, .aux_hint = {},
    .frame_ms = 40, .avatar_px = 0,
    .init = init, .display_start = display_start,
    .display_lock = display_lock, .display_unlock = display_unlock,
    .set_brightness = brightness, .panel_sleep = panel_sleep, .display_pause = nullptr,
    .audio_init = atoms3r_audio_init, .mic_slot = 0, .set_mic_gain = nullptr,
    .poll_buttons = buttons, .wait_buttons = nullptr, .read_power = read_power,
    .power_off = power_off,
};
extern "C" const muse_board_t *muse_board_get(void) { return &board; }
