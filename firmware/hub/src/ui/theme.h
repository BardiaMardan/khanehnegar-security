// =============================================================================
//  theme.h — palette, panel geometry, and the two timings the UI shares
// =============================================================================

#pragma once

// ========== [Palette — RGB565] ==========
#define COL_BG      0x0000
#define COL_PANEL   0x18C3
#define COL_TEXT    0xFFFF
#define COL_DIM     0x8410
#define COL_OK      0x0660
#define COL_WARN    0xFD20
#define COL_BAD     0xF800
#define COL_ACCENT  0x04FF

// ========== [Panel geometry] ==========
#define SCR_W       240
#define SCR_H       320
#define NAV_TOP     272
#define TAB_W       60
#define BANNER_H    52

// ========== [Toast] ==========
#define TOAST_MS    2200
#define TOAST_Y     118
#define TOAST_H     46
