// Waveshare ESP32-C6-LCD-1.47 : ST7789 172x320 en SPI (portrait), microSD sur le même bus, bouton BOOT.
#pragma once
#define HP_BOARD_ID "waveshare-c6"
#define HP_BOARD_NAME "Waveshare ESP32-C6-LCD-1.47"
// Écran et fonctions : lus par le tronc commun.
#define LCD_W 172
#define LCD_H 320
#define HP_HAS_TOUCH 0
#define HP_PIN_BUTTON 9
#define SD_MOUNT "/sd"
// Brochage : utilisé par board.cpp seulement.
#define PIN_SPI_SCLK 7
#define PIN_SPI_MOSI 6
#define PIN_SPI_MISO 5
#define PIN_SD_CS 4
#define PIN_LCD_CS 14
#define PIN_LCD_DC 15
#define PIN_LCD_RST 21
#define PIN_LCD_BL 22
#define LCD_X_OFFSET 34
#define LCD_Y_OFFSET 0
#define LCD_INVERT 1
