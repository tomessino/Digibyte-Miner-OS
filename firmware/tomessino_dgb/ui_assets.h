// ERZEUGT von tools/design/gen_assets.py, nicht von Hand aendern.
#pragma once
#include <stdint.h>

struct UiGlyph { uint32_t off; uint8_t w, h; int8_t x, y; uint8_t adv; };
struct UiFont  { const UiGlyph* glyphs; const uint8_t* data; const uint8_t* map; uint8_t cap; };
struct UiIcon  { uint8_t w, h; const uint16_t* rgb; const uint8_t* a; };

extern const uint16_t ui_bg_miner[76800];
extern const uint16_t ui_bg_clock[76800];
extern const uint16_t ui_bg_markets[76800];
extern const uint16_t ui_bg_weather[76800];
extern const uint16_t ui_bg_rank[76800];

extern const UiFont F_S11;
extern const UiFont F_S13;
extern const UiFont F_S15;
extern const UiFont F_S18;
extern const UiFont F_S38;
extern const UiFont F_B15;
extern const UiFont F_SEG12;
extern const UiFont F_SEG14;
extern const UiFont F_SEG18;
extern const UiFont F_SEG26;
extern const UiFont F_SEG30;
extern const UiFont F_SEG44;
extern const UiFont F_SEG90;

enum UiWeatherIcon { ICON_CLEAR, ICON_PARTLY, ICON_CLOUD, ICON_FOG, ICON_RAIN, ICON_SNOW, ICON_THUNDER };
extern const UiIcon ui_icons_big[7];
extern const UiIcon ui_icons_small[7];

struct UiMask  { uint8_t w, h; const uint8_t* a; };
enum UiMaskId { MASK_JOBS, MASK_NET, MASK_BEST, MASK_SHARES };
extern const UiMask ui_masks[4];
