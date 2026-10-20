#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#include "board_aipi_lite.h"
#include "progre_audio.h"
#include "progre_wifi.h"
#include "progre_bridge.h"
#include "progre_display.h"
#include "progre_usb.h"

static const char *TAG = "PROGRE";

#define BUTTON_DEBOUNCE_MS 30
#define MAIN_POLL_MS       10

static void talk_button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PROGRE_BUTTON_TALK_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));
}

static bool talk_button_raw_pressed(void)
{
    return gpio_get_level(PROGRE_BUTTON_TALK_PIN) == 0;
}


enum {
    PROGRE_CAPTURE_MAX_SECONDS = 8,
    PROGRE_CAPTURE_MAX_FRAMES =
        PROGRE_AUDIO_SAMPLE_RATE *
        PROGRE_CAPTURE_MAX_SECONDS,};

static int16_t *s_voice_capture;


void app_main(void)
{
    /* Keep battery power after the user releases the hardware power button. */
    gpio_set_level(PROGRE_POWER_HOLD_PIN, 1);
    gpio_set_direction(PROGRE_POWER_HOLD_PIN, GPIO_MODE_OUTPUT);
    s_voice_capture = heap_caps_malloc(
        PROGRE_CAPTURE_MAX_FRAMES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    ESP_ERROR_CHECK(s_voice_capture ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "        PROGRE OS v1.1");
    ESP_LOGI(TAG, "       LOCAL ROBOT COMPANION");
    ESP_LOGI(TAG, "================================");

    ESP_ERROR_CHECK(progre_display_init());
    talk_button_init();

    progre_display_show_companion(47, false, false, false);

    ESP_LOGI(TAG, "Display active.");
    ESP_LOGI(TAG, "Talk button active on GPIO%d.", PROGRE_BUTTON_TALK_PIN);
    ESP_LOGI(TAG, "Reset reason: %d; internal heap: %u", esp_reset_reason(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    bool microphone_ready = false;

    esp_err_t audio_probe = progre_audio_probe_codec();
    if (audio_probe == ESP_OK) {
        ESP_LOGI(TAG, "Audio codec discovery PASS.");

        esp_err_t microphone_init = progre_audio_init_microphone();
        if (microphone_init == ESP_OK) {
            microphone_ready = true;
            ESP_LOGI(TAG, "Microphone initialization PASS.");
        } else {
            ESP_LOGW(TAG, "Microphone initialization unavailable: %s",
                     esp_err_to_name(microphone_init));
        }
    } else {
        ESP_LOGW(TAG, "Audio codec discovery unavailable: %s",
                 esp_err_to_name(audio_probe));
    }

    esp_err_t wifi_err = progre_wifi_init();
    ESP_ERROR_CHECK(progre_usb_control_init());
    progre_bridge_init();
    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi manager started.");
    } else {
        ESP_LOGE(TAG, "Wi-Fi initialization failed: %s",
                 esp_err_to_name(wifi_err));
    }


    bool stable_pressed = talk_button_raw_pressed();
    bool candidate_pressed = stable_pressed;

    TickType_t candidate_since = xTaskGetTickCount();
    size_t voice_capture_frames = 0;
    bool voice_capture_full = false;

    if (stable_pressed) {
        ESP_LOGI(TAG, "Talk button held at startup.");
        progre_display_show_companion(47, true, false, false);
    }

    while (1) {
        TickType_t now = xTaskGetTickCount();
        bool raw_pressed = talk_button_raw_pressed();

        /*
         * Debounce GPIO42. A raw state must remain unchanged for
         * BUTTON_DEBOUNCE_MS before becoming the accepted state.
         */
        if (raw_pressed != candidate_pressed) {
            candidate_pressed = raw_pressed;
            candidate_since = now;
        }

        if (candidate_pressed != stable_pressed &&
            (now - candidate_since) >= pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {

            stable_pressed = candidate_pressed;

            if (stable_pressed) {
                ESP_LOGI(TAG, "Talk button PRESSED — listening");
                voice_capture_frames = 0;
                voice_capture_full = false;
                progre_display_show_companion(47, true, false, false);
            } else {
                ESP_LOGI(
                    TAG,
                    "Talk button RELEASED — captured %u frames",
                    (unsigned)voice_capture_frames
                );


                if (microphone_ready &&
                    voice_capture_frames > 0) {

                    progre_display_set_thinking(true);
                    esp_err_t conversation_err =
                        progre_bridge_exchange_audio_stream(
                            s_voice_capture,
                            voice_capture_frames
                        );

                    if (conversation_err != ESP_OK) {
                        ESP_LOGW(
                            TAG,
                            "Conversation stream failed: %s",
                            esp_err_to_name(conversation_err)
                        );
                    }

                    ESP_LOGI(TAG, "Voice task stack remaining: %u bytes; minimum heap: %u",
                             (unsigned)uxTaskGetStackHighWaterMark(NULL),
                             (unsigned)esp_get_minimum_free_heap_size());
                    progre_display_show_companion(47, false, false, false);
                }
            }
        }

        /*
         * Capture microphone PCM while Talk is held.
         *
         * The public audio API extracts one slot from the duplicated
         * stereo microphone stream, producing 16 kHz signed-16 mono.
         */
        if (stable_pressed &&
            microphone_ready &&
            !voice_capture_full) {

            size_t remaining =
                PROGRE_CAPTURE_MAX_FRAMES -
                voice_capture_frames;

            if (remaining > 0) {

                size_t captured_now = 0;

                esp_err_t capture_result =
                    progre_audio_capture_mono(
                        &s_voice_capture[
                            voice_capture_frames
                        ],
                        remaining,
                        &captured_now,
                        50
                    );

                if (capture_result == ESP_OK) {
                    voice_capture_frames +=
                        captured_now;
                } else if (
                    capture_result != ESP_ERR_TIMEOUT
                ) {
                    ESP_LOGW(
                        TAG,
                        "Microphone capture unavailable: %s",
                        esp_err_to_name(capture_result)
                    );
                }
            }

            if (voice_capture_frames >=
                PROGRE_CAPTURE_MAX_FRAMES) {

                voice_capture_full = true;

                ESP_LOGI(
                    TAG,
                    "Voice capture reached %d-second commissioning limit",
                    PROGRE_CAPTURE_MAX_SECONDS
                );
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_POLL_MS));
    }
}
