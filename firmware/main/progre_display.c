#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_aipi_lite.h"
#include "progre_display.h"

static const char *TAG = "PROGRE_DISPLAY";

static spi_device_handle_t lcd_spi;

#define CMD_SWRESET 0x01
#define CMD_SLPOUT  0x11
#define CMD_INVOFF  0x20
#define CMD_DISPON  0x29
#define CMD_CASET   0x2A
#define CMD_RASET   0x2B
#define CMD_RAMWR   0x2C
#define CMD_MADCTL  0x36
#define CMD_COLMOD  0x3A

#define COLOR_BLACK   0x0000
#define COLOR_DARK_GRAY 0x3186
#define COLOR_WHITE   0xFFFF

static esp_err_t lcd_tx(const void *data, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };

    return spi_device_transmit(lcd_spi, &t);
}

static esp_err_t lcd_cmd(uint8_t cmd)
{
    gpio_set_level(PROGRE_LCD_PIN_DC, 0);
    return lcd_tx(&cmd, 1);
}

static esp_err_t lcd_data(const void *data, size_t len)
{
    gpio_set_level(PROGRE_LCD_PIN_DC, 1);
    return lcd_tx(data, len);
}

static void lcd_reset(void)
{
    gpio_set_level(PROGRE_LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    gpio_set_level(PROGRE_LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void lcd_set_window(int x0, int y0, int x1, int y1)
{
    uint8_t data[4];

    lcd_cmd(CMD_CASET);

    data[0] = (x0 >> 8) & 0xFF;
    data[1] = x0 & 0xFF;
    data[2] = (x1 >> 8) & 0xFF;
    data[3] = x1 & 0xFF;
    lcd_data(data, sizeof(data));

    lcd_cmd(CMD_RASET);

    data[0] = (y0 >> 8) & 0xFF;
    data[1] = y0 & 0xFF;
    data[2] = (y1 >> 8) & 0xFF;
    data[3] = y1 & 0xFF;
    lcd_data(data, sizeof(data));

    lcd_cmd(CMD_RAMWR);
}

static uint8_t frame[PROGRE_LCD_HEIGHT][PROGRE_LCD_WIDTH * 2];

static void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0 || y < 0 || x >= PROGRE_LCD_WIDTH || y >= PROGRE_LCD_HEIGHT ||
        w <= 0 || h <= 0) return;
    if (x + w > PROGRE_LCD_WIDTH) w = PROGRE_LCD_WIDTH - x;
    if (y + h > PROGRE_LCD_HEIGHT) h = PROGRE_LCD_HEIGHT - y;
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) {
            frame[yy][xx * 2] = color >> 8;
            frame[yy][xx * 2 + 1] = color & 0xff;
        }
}

static void lcd_flush(void)
{
    lcd_set_window(0, 0, PROGRE_LCD_WIDTH - 1, PROGRE_LCD_HEIGHT - 1);
    gpio_set_level(PROGRE_LCD_PIN_DC, 1);
    for (int y = 0; y < PROGRE_LCD_HEIGHT; ++y)
        lcd_tx(frame[y], sizeof(frame[y]));
}

static void lcd_clear(uint16_t color)
{
    lcd_fill_rect(
        0,
        0,
        PROGRE_LCD_WIDTH,
        PROGRE_LCD_HEIGHT,
        color
    );
}

/* Only the display task owns SPI; audio and USB update this small state. */
static portMUX_TYPE face_lock = portMUX_INITIALIZER_UNLOCKED;
static bool face_listening, face_thinking;
static unsigned face_level;
static uint16_t face_color = COLOR_WHITE;

void progre_display_show_companion(int x, bool active, bool blink, bool step)
{
    (void)x; (void)blink; (void)step;
    portENTER_CRITICAL(&face_lock);
    face_listening = active;
    face_thinking = false;
    face_level = 0;
    portEXIT_CRITICAL(&face_lock);
}

void progre_display_show_banner(const char *text, int scroll_x)
{
    /* The face replaces the old six-glyph scrolling caption. */
    (void)text; (void)scroll_x;
}

void progre_display_set_thinking(bool thinking)
{
    portENTER_CRITICAL(&face_lock);
    face_thinking = thinking;
    portEXIT_CRITICAL(&face_lock);
}

void progre_display_set_level(unsigned level)
{
    portENTER_CRITICAL(&face_lock);
    face_level = level > 100 ? 100 : level;
    face_thinking = false;
    portEXIT_CRITICAL(&face_lock);
}

void progre_display_set_color(uint16_t color)
{
    portENTER_CRITICAL(&face_lock);
    face_color = color;
    portEXIT_CRITICAL(&face_lock);
}

static void display_task(void *arg)
{
    (void)arg;
    while (true) {
        unsigned ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool listening, thinking;
        unsigned level;
        uint16_t color;
        portENTER_CRITICAL(&face_lock);
        listening = face_listening;
        thinking = face_thinking;
        level = face_level;
        color = face_color;
        portEXIT_CRITICAL(&face_lock);
        /* A four-second triangle breath; short blink every ~4.7 seconds. */
        unsigned phase = (ms % 4000) / 500;
        int breath = phase < 4 ? phase : 7 - phase;
        bool blink = !listening && !thinking && ms % 4700 < 160;
        int eye_h = blink ? 3 : 28 + breath + (listening ? 6 : 0);
        int y = 43 - eye_h / 2;
        lcd_clear(COLOR_BLACK);
        for (int eye = 0; eye < 2; ++eye) {
            int x = 23 + eye * 58;
            lcd_fill_rect(x + 4, y, 20, eye_h, color);
            if (!blink) lcd_fill_rect(x, y + 4, 28, eye_h - 8, color);
        }
        int mouth_h = level ? 3 + level / 7 : 3;
        lcd_fill_rect(44, 89 - mouth_h / 2, 40, mouth_h, color);
        if (thinking) {
            int dot = (ms / 300) % 3;
            for (int i = 0; i < 3; ++i)
                lcd_fill_rect(49 + i * 13, 108, 5, 5,
                              i == dot ? color : COLOR_DARK_GRAY);
        } else if (listening) {
            lcd_fill_rect(42, 109, 44, 2, color);
        }
        lcd_flush();
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

esp_err_t progre_display_init(void)
{
    ESP_LOGI(TAG, "Initializing AIPI Lite ST7735 display");

    gpio_config_t gpio_cfg = {
        .pin_bit_mask =
            (1ULL << PROGRE_LCD_PIN_DC) |
            (1ULL << PROGRE_LCD_PIN_RST) |
            (1ULL << PROGRE_LCD_PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));

    gpio_set_level(PROGRE_LCD_PIN_BL, 0);
    gpio_set_level(PROGRE_LCD_PIN_DC, 0);
    gpio_set_level(PROGRE_LCD_PIN_RST, 1);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PROGRE_LCD_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PROGRE_LCD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PROGRE_LCD_WIDTH * 2 + 8,
    };

    ESP_ERROR_CHECK(
        spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO)
    );

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = PROGRE_LCD_SPI_HZ,
        .mode = 0,
        .spics_io_num = PROGRE_LCD_PIN_CS,
        .queue_size = 1,
    };

    ESP_ERROR_CHECK(
        spi_bus_add_device(SPI2_HOST, &dev_cfg, &lcd_spi)
    );

    lcd_reset();

    ESP_ERROR_CHECK(lcd_cmd(CMD_SWRESET));
    vTaskDelay(pdMS_TO_TICKS(150));

    ESP_ERROR_CHECK(lcd_cmd(CMD_SLPOUT));
    vTaskDelay(pdMS_TO_TICKS(120));

    /*
     * 16-bit RGB565.
     */
    uint8_t colmod = 0x05;
    ESP_ERROR_CHECK(lcd_cmd(CMD_COLMOD));
    ESP_ERROR_CHECK(lcd_data(&colmod, 1));

    /*
     * Rotation 90 degrees.
     * MX + MV, RGB order.
     */
    uint8_t madctl = 0x60;
    ESP_ERROR_CHECK(lcd_cmd(CMD_MADCTL));
    ESP_ERROR_CHECK(lcd_data(&madctl, 1));

    ESP_ERROR_CHECK(lcd_cmd(CMD_INVOFF));

    ESP_ERROR_CHECK(lcd_cmd(CMD_DISPON));
    vTaskDelay(pdMS_TO_TICKS(20));

    lcd_clear(COLOR_BLACK);

    gpio_set_level(PROGRE_LCD_PIN_BL, 1);

    ESP_LOGI(TAG, "Display initialized");

    if (xTaskCreate(display_task, "progre_face", 3072, NULL, 2, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;

    return ESP_OK;
}
