// ================================================================
// TFT_eSPI User_Setup.h
// Board  : ESP32-2432S028 (CYD - Cheap Yellow Display)
// Display: ST7789 2.8" 320x240
// ================================================================

// --- Treiber ---
#define ST7789_DRIVER

// --- Auflösung ---
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// --- SPI Pins (CYD Hardware) ---
#define TFT_MOSI   13
#define TFT_SCLK   14
#define TFT_CS     15
#define TFT_DC      2
#define TFT_RST    -1

// --- Backlight ---
#define TFT_BL     21
#define TFT_BACKLIGHT_ON HIGH

// --- SPI Einstellungen ---
// SPI_MODE 3 ist PFLICHT für ST7789 auf CYD, sonst weißer Screen!
#define SPI_FREQUENCY        40000000
#define SPI_READ_FREQUENCY   20000000
#define SPI_TOUCH_FREQUENCY   2500000
#define SPI_MODE 3

// --- Fonts ---
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
