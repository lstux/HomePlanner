// ST7789 + SDSPI board support adapted from lstux/Waveshare-C6-1.47 sd-server.
#include "board.h"
#include "pins.h"
#include "gfx.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include <string.h>
#define BUS_HOST SPI2_HOST
static spi_device_handle_t lcd=nullptr; static uint16_t* band=nullptr; static bool lcdOk=false; static sdmmc_card_t* card=nullptr;
static const uint8_t font[36][5]={
{0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
{0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},{0x72,0x49,0x49,0x49,0x46},{0x21,0x41,0x49,0x4d,0x33},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x31},{0x41,0x21,0x11,0x09,0x07},{0x36,0x49,0x49,0x49,0x36},{0x46,0x49,0x49,0x29,0x1e}};
static void IRAM_ATTR pre(spi_transaction_t*t){gpio_set_level((gpio_num_t)PIN_LCD_DC,(int)(intptr_t)t->user);}
static void send(const void*p,size_t n,int dc){if(!lcd||!n)return;spi_transaction_t t={};t.length=n*8;t.tx_buffer=p;t.user=(void*)(intptr_t)dc;spi_device_polling_transmit(lcd,&t);}
static void cmd(uint8_t c){send(&c,1,0);} static void cd(uint8_t c,const uint8_t*d,size_t n){cmd(c);if(n)send(d,n,1);}
#define CMD(c,...) do{static const uint8_t d[]={__VA_ARGS__};cd(c,d,sizeof(d));}while(0)
static void window(int x0,int y0,int x1,int y1){x0+=LCD_X_OFFSET;x1+=LCD_X_OFFSET;uint8_t a[4]={(uint8_t)(x0>>8),(uint8_t)x0,(uint8_t)(x1>>8),(uint8_t)x1},b[4]={(uint8_t)(y0>>8),(uint8_t)y0,(uint8_t)(y1>>8),(uint8_t)y1};cd(0x2a,a,4);cd(0x2b,b,4);cmd(0x2c);}
static void initLcd(){pinMode(PIN_LCD_DC,OUTPUT);pinMode(PIN_LCD_RST,OUTPUT);digitalWrite(PIN_LCD_RST,HIGH);delay(10);digitalWrite(PIN_LCD_RST,LOW);delay(20);digitalWrite(PIN_LCD_RST,HIGH);delay(120);cmd(1);delay(150);cmd(0x11);delay(120);CMD(0x3a,0x55);CMD(0x36,0x00);CMD(0xb2,0x0c,0x0c,0x00,0x33,0x33);CMD(0xb7,0x35);CMD(0xbb,0x35);CMD(0xc0,0x2c);CMD(0xc2,0x01);CMD(0xc3,0x13);CMD(0xc4,0x20);CMD(0xc6,0x0f);CMD(0xd0,0xa4,0xa1);CMD(0xe0,0xd0,0x08,0x11,0x08,0x0c,0x15,0x39,0x33,0x50,0x36,0x13,0x14,0x29,0x2d);CMD(0xe1,0xd0,0x08,0x10,0x08,0x06,0x06,0x39,0x44,0x51,0x0b,0x16,0x14,0x2f,0x31);cmd(LCD_INVERT?0x21:0x20);cmd(0x29);delay(20);}
void lcdBacklight(uint8_t p){if(p>100)p=100;ledcWrite(PIN_LCD_BL,(uint32_t)p*255/100);}
static uint16_t sw(uint16_t c){return(c>>8)|(c<<8);}
void lcdFill(uint16_t color){if(!lcdOk)return;uint16_t c=sw(color);for(int i=0;i<LCD_W*20;i++)band[i]=c;window(0,0,LCD_W-1,LCD_H-1);for(int y=0;y<LCD_H;y+=20)send(band,(size_t)LCD_W*(y+20>LCD_H?LCD_H-y:20)*2,1);}
static int glyph(char c){if(c>='A'&&c<='Z')return c-'A';if(c>='0'&&c<='9')return 26+c-'0';return -1;}
void lcdText(int y,const char*s,uint8_t scale,uint16_t fg,uint16_t bg){if(!lcdOk||!s||scale<1)return;int h=8*scale;if(y<0||y+h>LCD_H)return;uint16_t b=sw(bg),f=sw(fg);for(int j=0;j<h;j++)for(int x=0;x<LCD_W;x++)band[j*LCD_W+x]=b;int x=2;for(;*s&&x<LCD_W-5*scale;s++){char c=*s;if(c>='a'&&c<='z')c-=32;int g=glyph(c);if(g>=0){for(int col=0;col<5;col++)for(int row=0;row<7;row++)if(font[g][col]&(1<<row))for(int yy=0;yy<scale;yy++)for(int xx=0;xx<scale;xx++){int px=x+col*scale+xx,py=row*scale+yy;if(px<LCD_W&&py<h)band[py*LCD_W+px]=f;}}x+=6*scale;}window(0,y,LCD_W-1,y+h-1);send(band,(size_t)LCD_W*h*2,1);}
bool boardInit(){pinMode(PIN_SD_CS,OUTPUT);digitalWrite(PIN_SD_CS,HIGH);pinMode(PIN_LCD_CS,OUTPUT);digitalWrite(PIN_LCD_CS,HIGH);spi_bus_config_t bus={};bus.mosi_io_num=PIN_SPI_MOSI;bus.miso_io_num=PIN_SPI_MISO;bus.sclk_io_num=PIN_SPI_SCLK;bus.quadwp_io_num=-1;bus.quadhd_io_num=-1;bus.max_transfer_sz=4096;if(spi_bus_initialize(BUS_HOST,&bus,SPI_DMA_CH_AUTO)!=ESP_OK)return false;spi_device_interface_config_t dev={};dev.clock_speed_hz=40000000;dev.mode=0;dev.spics_io_num=PIN_LCD_CS;dev.queue_size=2;dev.pre_cb=pre;if(spi_bus_add_device(BUS_HOST,&dev,&lcd)!=ESP_OK)return false;band=(uint16_t*)heap_caps_malloc((size_t)LCD_W*20*2,MALLOC_CAP_DMA|MALLOC_CAP_8BIT);if(!band)return false;ledcAttach(PIN_LCD_BL,5000,8);lcdBacklight(0);initLcd();lcdOk=true;lcdFill(C_BLACK);lcdBacklight(70);return true;}
bool sdMounted(){return card!=nullptr;}
bool sdMount(uint32_t khz){if(card)return true;sdmmc_host_t host=SDSPI_HOST_DEFAULT();host.slot=BUS_HOST;host.max_freq_khz=(int)khz;sdspi_device_config_t slot=SDSPI_DEVICE_CONFIG_DEFAULT();slot.gpio_cs=(gpio_num_t)PIN_SD_CS;slot.host_id=BUS_HOST;esp_vfs_fat_mount_config_t mc={};mc.format_if_mount_failed=false;mc.max_files=4;sdmmc_card_t*c=nullptr;esp_err_t e=esp_vfs_fat_sdspi_mount(SD_MOUNT,&host,&slot,&mc,&c);if(e!=ESP_OK){Serial.printf("[SD] mount failed: %s\n",esp_err_to_name(e));return false;}card=c;Serial.printf("[SD] mounted %s\n",c->cid.name);return true;}
