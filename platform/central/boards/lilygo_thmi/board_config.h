// LilyGo T-HMI : ESP32-S3, ST7789 2,8" 320x240 sur bus parallèle 8 bits (paysage), tactile résistif XPT2046,
// microSD sur un bit, bouton BOOT. Brochage et réglages : dépôt Xinyuan-LilyGO/T-HMI.
#pragma once
#define HP_BOARD_ID "lilygo-t-hmi"
#define HP_BOARD_NAME "LilyGo T-HMI"
// Écran et fonctions : lus par le tronc commun.
#define LCD_W 320
#define LCD_H 240
#define HP_HAS_TOUCH 1
#define HP_PIN_BUTTON 0
#define SD_MOUNT "/sd"
// Brochage : utilisé par board.cpp seulement.
#define PIN_PWR_EN 10        // à l'état haut pour fonctionner sur batterie
#define PIN_PWR_ON 14        // à l'état haut pour rester allumée sur l'alimentation 5 V
#define PIN_LCD_DATA {48, 47, 39, 40, 41, 42, 45, 46}
#define PIN_LCD_WR 8
#define PIN_LCD_CS 6
#define PIN_LCD_DC 7
#define PIN_LCD_BL 38
#define LCD_PCLK_HZ (5 * 1000 * 1000)
#define LCD_MADCTL 0xA0      // paysage des exemples LilyGo ; 0x60 pour tourner de 180°
#define PIN_TOUCH_SCLK 1
#define PIN_TOUCH_MOSI 3
#define PIN_TOUCH_MISO 4
#define PIN_TOUCH_CS 2
#define PIN_TOUCH_IRQ 9
// Valeurs brutes (12 bits) aux bords de l'écran, dans le sens des pixels (calibration par défaut LilyGo).
#define TOUCH_RAW90_LEFT 3754
#define TOUCH_RAW90_RIGHT 622
#define TOUCH_RAWD0_TOP 570
#define TOUCH_RAWD0_BOTTOM 3576
#define PIN_SD_CLK 12
#define PIN_SD_CMD 11
#define PIN_SD_D0 13
