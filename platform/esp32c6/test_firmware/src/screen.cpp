#include "screen.h"
#include "board.h"
#include "gfx.h"
void hpScreenMessage(const char*a,const char*b){lcdFill(C_BG);lcdText(18,"HOMEPLANNER",2,C_CYAN,C_BG);lcdText(72,a,1,C_WHITE,C_BG);if(b)lcdText(92,b,1,C_YELLOW,C_BG);}
void hpScreenStatus(bool sd,bool journal,bool ap,const String& ip,const String& wifi,const String& key){lcdFill(C_BG);lcdText(8,"HOMEPLANNER",2,C_CYAN,C_BG);lcdText(44,ap?"WIFI: CONFIG AP":"WIFI: CONNECTED",1,ap?C_YELLOW:C_GREEN,C_BG);lcdText(62,wifi.c_str(),1,C_WHITE,C_BG);lcdText(88,ip.c_str(),ip.length()>13?1:2,C_CYAN,C_BG);lcdText(132,sd?"SD: OK":"SD: ERROR / ABSENT",1,sd?C_GREEN:C_ORANGE,C_BG);lcdText(150,journal?"JOURNAL: OK":"JOURNAL: UNAVAILABLE",1,journal?C_GREEN:C_ORANGE,C_BG);if(ap){lcdText(190,"PORTAL: 192.168.4.1",1,C_GREY,C_BG);}else{lcdText(190,"PAIRING CODE",1,C_GREY,C_BG);lcdText(208,key.c_str(),2,C_YELLOW,C_BG);}}
