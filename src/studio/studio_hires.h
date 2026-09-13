// OpenTIC Studio High-Resolution (512x288) TrueType Engine
#pragma once

#include "studio/studio.h"

#define STUDIO_HIRES_WIDTH 512
#define STUDIO_HIRES_HEIGHT 288
#define STUDIO_HIRES_OFFSET_LEFT 16
#define STUDIO_HIRES_OFFSET_TOP 8
#define STUDIO_HIRES_VIEW_WIDTH 480
#define STUDIO_HIRES_VIEW_HEIGHT 272
#define STUDIO_HIRES_TOOLBAR_Y 8
#define STUDIO_HIRES_TOOLBAR_H 16
#define STUDIO_HIRES_FONT_SIZE 15.0f
#define STUDIO_HIRES_LINE_HEIGHT 18

#ifdef __cplusplus
extern "C" {
#endif

void studio_hires_init(Studio* studio);
void studio_hires_free(Studio* studio);

u32* studio_hires_get_screen(Studio* studio);
bool studio_hires_is_font_loaded(Studio* studio);

u32 studio_hires_get_color(Studio* studio, u8 colorIndex);

void studio_hires_cls(Studio* studio, u32 color);
void studio_hires_pixel(Studio* studio, s32 x, s32 y, u32 color);
void studio_hires_rect(Studio* studio, s32 x, s32 y, s32 w, s32 h, u32 color);
void studio_hires_rect_border(Studio* studio, s32 x, s32 y, s32 w, s32 h, u32 color);
void studio_hires_icon2x(Studio* studio, s32 iconId, s32 x, s32 y, u32 color);

s32  studio_hires_draw_text(Studio* studio, const char* text, s32 x, s32 y, u32 color, s32 clipTop, s32 clipBottom);
s32  studio_hires_draw_char(Studio* studio, u32 codepoint, s32 x, s32 y, u32 color, s32 clipTop, s32 clipBottom);
s32  studio_hires_measure_text(Studio* studio, const char* text);

void studio_hires_draw_toolbar(Studio* studio, EditorMode currentMode, s32 mouseX, s32 mouseY, bool mouseClick);
void studio_hires_draw_cursor(Studio* studio, s32 mouseX, s32 mouseY);

struct Code;
struct Console;
void studio_hires_draw_code(Studio* studio, struct Code* code);
void studio_hires_draw_console(Studio* studio, struct Console* console);

void studio_hires_blit_2x(Studio* studio, const u32* src256x144);
void studio_hires_update_system_font(tic_mem* tic, tic_font* systemFont);

#ifdef __cplusplus
}
#endif
