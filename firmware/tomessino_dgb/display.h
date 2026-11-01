#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

// ================================================================
// DISPLAY SEITEN
// ================================================================
enum Page { PAGE_MINER=0, PAGE_CLOCK=1, PAGE_PRICE=2, PAGE_WEATHER=3, PAGE_RANK=4, PAGE_COUNT=5 };
extern Page curPage;

// ================================================================
// FARBEN (Custom Cyberpunk-Blau Design, RGB565)
// ================================================================
#define C_BG         0x0000   // Reines Schwarz (Foto-Hintergrund)
#define C_BG2        0x0010   // Fast-Schwarz mit Blaustich (Panels)
#define C_TEXT       0xFFFF   // Weiß
#define C_MUTED      0x7BEF   // Grau
#define C_ACCENT     0x047F   // Helles DGB-Blau (#0080FF-ish)
#define C_ACCENT_DK  0x0259   // Dunkleres Blau für Rahmen
#define C_DOTS       0x0215   // Weltkarte-Punkte (sehr dunkles Blau)
#define C_GREEN      0x07E0   // Grün (Preis steigt)
#define C_RED        0xF800   // Rot
#define C_YELLOW     0xFFE0   // Gelb
#define C_ORANGE     0xFD20   // Orange
#define C_CYAN       0x07FF   // Cyan (Uhrzeit/Preis)
#define C_SEP        0x0259   // Trennlinie blau
#define C_GOOD       0x07E0
#define C_BAD        0xF800

extern TFT_eSPI        tft;
extern SPIClass        touchSPI;
extern XPT2046_Touchscreen touch;

extern uint32_t tDisplay;
extern uint32_t tLastTouch;
extern bool     configMode;
extern bool     passReset;

void displayRequestPage(int page);  // Seite wählen und Display wecken (aus jedem Task)
void displayRequestColorTest(int invert, int bgr, int bars);  // Farbtest (Gerätetest)
void inputTick();
void tftSplash();
void tftWifiSetup();
void tftConnected();
void tftUpdate();
