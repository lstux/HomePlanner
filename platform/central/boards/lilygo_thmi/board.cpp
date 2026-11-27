// LilyGo T-HMI : écran sur le bus i80, tactile XPT2046 sur SPI, microSD en SD/MMC un bit.
// Séquence d'initialisation de l'écran reprise des exemples LilyGo (T-HMI, LVGL).
#include "board.h"
#include <SPI.h>
#include <string.h>
#include "draw.h"
#include "gfx.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

namespace {
esp_lcd_panel_io_handle_t io = nullptr;
SemaphoreHandle_t sent = nullptr;
uint16_t* band = nullptr;
bool lcdOk = false;
sdmmc_card_t* card = nullptr;
SPIClass touchSpi(FSPI);

bool IRAM_ATTR colorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(sent, &woken);
  return woken == pdTRUE;
}

struct Command { uint8_t code, length, data[14]; uint16_t wait; };
const Command kInit[] = {
    {0x01, 0, {}, 150}, {0x11, 0, {}, 120}, {0x36, 1, {LCD_MADCTL}, 0}, {0x3A, 1, {0x05}, 0},
    {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 0}, {0xB7, 1, {0x35}, 0}, {0xBB, 1, {0x28}, 0},
    {0xC0, 1, {0x2C}, 0}, {0xC2, 1, {0x01}, 0}, {0xC3, 1, {0x0B}, 0}, {0xC4, 1, {0x20}, 0},
    {0xC6, 1, {0x0F}, 0}, {0xD0, 2, {0xA4, 0xA1}, 0},
    {0xE0, 14, {0xD0, 0x01, 0x08, 0x0F, 0x11, 0x2A, 0x36, 0x55, 0x44, 0x3A, 0x0B, 0x06, 0x11, 0x20}, 0},
    {0xE1, 14, {0xD0, 0x02, 0x07, 0x0A, 0x0B, 0x18, 0x34, 0x43, 0x4A, 0x2B, 0x1B, 0x1C, 0x22, 0x1F}, 0},
    {0x29, 0, {}, 20}};

bool initLcd() {
  esp_lcd_i80_bus_handle_t bus = nullptr;
  esp_lcd_i80_bus_config_t busConfig = {};
  busConfig.clk_src = LCD_CLK_SRC_DEFAULT;
  busConfig.dc_gpio_num = static_cast<gpio_num_t>(PIN_LCD_DC);
  busConfig.wr_gpio_num = static_cast<gpio_num_t>(PIN_LCD_WR);
  const int data[8] = PIN_LCD_DATA;
  for (int i = 0; i < 8; ++i) busConfig.data_gpio_nums[i] = static_cast<gpio_num_t>(data[i]);
  busConfig.bus_width = 8;
  busConfig.max_transfer_bytes = LCD_W * HP_SLICE * 2;
  if (esp_lcd_new_i80_bus(&busConfig, &bus) != ESP_OK) return false;
  esp_lcd_panel_io_i80_config_t config = {};
  config.cs_gpio_num = static_cast<gpio_num_t>(PIN_LCD_CS);
  config.pclk_hz = LCD_PCLK_HZ;
  config.trans_queue_depth = 4;
  config.on_color_trans_done = colorDone;
  config.lcd_cmd_bits = 8;
  config.lcd_param_bits = 8;
  config.dc_levels.dc_data_level = 1;
  config.flags.swap_color_bytes = 1;
  if (esp_lcd_new_panel_io_i80(bus, &config, &io) != ESP_OK) return false;
  for (const Command& c : kInit) {
    if (esp_lcd_panel_io_tx_param(io, c.code, c.data, c.length) != ESP_OK) return false;
    if (c.wait) delay(c.wait);
  }
  return true;
}

uint16_t touchSample(uint8_t command) {
  touchSpi.transfer(command);
  return touchSpi.transfer16(0) >> 3;
}
// Moyenne des deux valeurs les plus proches sur trois : écarte une mesure isolée.
int bestOfThree(int a, int b, int c) {
  const int ab = abs(a - b), ac = abs(a - c), bc = abs(b - c);
  if (ab <= ac && ab <= bc) return (a + b) / 2;
  return ac <= bc ? (a + c) / 2 : (b + c) / 2;
}
}  // namespace

void boardPower() {
  pinMode(PIN_PWR_ON, OUTPUT);
  digitalWrite(PIN_PWR_ON, HIGH);
  pinMode(PIN_PWR_EN, OUTPUT);
  digitalWrite(PIN_PWR_EN, HIGH);
}

void lcdBacklight(uint8_t percent) {
  if (percent > 100) percent = 100;
  ledcWrite(PIN_LCD_BL, static_cast<uint32_t>(percent) * 255 / 100);
}

void lcdBlit(int x, int y, int w, int h, const uint16_t* pixels) {
  if (!lcdOk || w < 1 || h < 1 || w > LCD_W || h > HP_SLICE) return;
  memcpy(band, pixels, static_cast<size_t>(w) * h * 2);
  const int x1 = x + w - 1, y1 = y + h - 1;
  const uint8_t columns[4] = {uint8_t(x >> 8), uint8_t(x), uint8_t(x1 >> 8), uint8_t(x1)};
  const uint8_t rows[4] = {uint8_t(y >> 8), uint8_t(y), uint8_t(y1 >> 8), uint8_t(y1)};
  esp_lcd_panel_io_tx_param(io, 0x2A, columns, 4);
  esp_lcd_panel_io_tx_param(io, 0x2B, rows, 4);
  // Attendre la fin du transfert avant de réutiliser le tampon.
  if (esp_lcd_panel_io_tx_color(io, 0x2C, band, static_cast<size_t>(w) * h * 2) == ESP_OK)
    xSemaphoreTake(sent, pdMS_TO_TICKS(200));
}

bool boardInit() {
  ledcAttach(PIN_LCD_BL, 5000, 8);
  lcdBacklight(0);
  pinMode(PIN_TOUCH_CS, OUTPUT);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  pinMode(PIN_TOUCH_IRQ, INPUT_PULLUP);
  touchSpi.begin(PIN_TOUCH_SCLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI);
  sent = xSemaphoreCreateBinary();
  band = static_cast<uint16_t*>(heap_caps_malloc(LCD_W * HP_SLICE * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  delay(100);
  if (!sent || !band || !initLcd()) return false;
  lcdOk = true;
  drawFill(C_BLACK);
  lcdBacklight(70);
  return true;
}

bool touchRead(int& x, int& y, int& rawA, int& rawB) {
  if (digitalRead(PIN_TOUCH_IRQ) == HIGH) return false;
  int a[3], b[3];
  touchSpi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TOUCH_CS, LOW);
  for (int i = 0; i < 3; ++i) {
    a[i] = touchSample(0x90);
    b[i] = touchSample(0xD0);
  }
  digitalWrite(PIN_TOUCH_CS, HIGH);
  touchSpi.endTransaction();
  rawA = bestOfThree(a[0], a[1], a[2]);
  rawB = bestOfThree(b[0], b[1], b[2]);
  if (rawA < 50 || rawB < 50 || rawA > 4045 || rawB > 4045) return false;  // contact trop léger
  x = constrain(map(rawA, TOUCH_RAW90_LEFT, TOUCH_RAW90_RIGHT, 0, LCD_W - 1), 0, LCD_W - 1);
  y = constrain(map(rawB, TOUCH_RAWD0_TOP, TOUCH_RAWD0_BOTTOM, 0, LCD_H - 1), 0, LCD_H - 1);
  return true;
}

bool sdMounted() { return card != nullptr; }

bool sdMount() {
  if (card) return true;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = static_cast<gpio_num_t>(PIN_SD_CLK);
  slot.cmd = static_cast<gpio_num_t>(PIN_SD_CMD);
  slot.d0 = static_cast<gpio_num_t>(PIN_SD_D0);
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_vfs_fat_mount_config_t mount = {};
  mount.format_if_mount_failed = false;
  mount.max_files = 4;
  sdmmc_card_t* found = nullptr;
  const esp_err_t error = esp_vfs_fat_sdmmc_mount(SD_MOUNT, &host, &slot, &mount, &found);
  if (error != ESP_OK) {
    Serial.printf("[SD] mount failed: %s\n", esp_err_to_name(error));
    return false;
  }
  card = found;
  Serial.printf("[SD] mounted %s\n", found->cid.name);
  return true;
}
