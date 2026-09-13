// OpenTIC Studio High-Resolution (512x288) TrueType Engine Implementation
#include "studio_hires.h"

#include "stb_truetype.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(_WIN32)
#include <windows.h>
#endif

typedef struct
{
    u32* hiresBuffer;
    u8* fontData;
    stbtt_fontinfo fontInfo;
    float fontScale;
    float fontAscent;
    bool fontLoaded;
    bool initialized;
} StudioHiresContext;

static StudioHiresContext s_hiresCtx = {0};

static u32 utf8_decode_codepoint(const char** p)
{
    const unsigned char* s = (const unsigned char*)*p;
    u32 cp = 0;
    if (*s < 0x80)
    {
        cp = *s;
        (*p) += 1;
    }
    else if ((*s & 0xE0) == 0xC0)
    {
        cp = ((*s & 0x1F) << 6) | (s[1] & 0x3F);
        (*p) += 2;
    }
    else if ((*s & 0xF0) == 0xE0)
    {
        cp = ((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        (*p) += 3;
    }
    else if ((*s & 0xF8) == 0xF0)
    {
        cp = ((*s & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        (*p) += 4;
    }
    else
    {
        cp = *s;
        (*p) += 1;
    }
    return cp;
}

static void loadStudioFont(void)
{
    char exeFontPath[512] = {0};
    s32 i;
#if defined(_WIN32)
    char exePath[512];
    if (GetModuleFileNameA(NULL, exePath, sizeof(exePath)))
    {
        char* lastSlash = strrchr(exePath, '\\');
        if (!lastSlash) lastSlash = strrchr(exePath, '/');
        if (lastSlash)
        {
            *(lastSlash + 1) = '\0';
            snprintf(exeFontPath, sizeof(exeFontPath), "%sassets/SourceHanSansSC-Regular.otf", exePath);
        }
    }
#endif

    const char* FontPaths[] = {
        exeFontPath,
        "assets/SourceHanSansSC-Regular.otf",
        "../assets/SourceHanSansSC-Regular.otf",
        "../../assets/SourceHanSansSC-Regular.otf",
        "E:/OpenTIC/assets/SourceHanSansSC-Regular.otf",
        "E:/OpenTIC/build_msvc/bin/assets/SourceHanSansSC-Regular.otf",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/msyh.ttc"
    };

    if (s_hiresCtx.fontLoaded) return;

    for (i = 0; i < (s32)(sizeof(FontPaths)/sizeof(FontPaths[0])); i++)
    {
        FILE* f;
        if (!FontPaths[i] || !FontPaths[i][0]) continue;
        f = fopen(FontPaths[i], "rb");
        if (f)
        {
            long size;
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            fseek(f, 0, SEEK_SET);

            if (size > 0 && size < 64 * 1024 * 1024)
            {
                s_hiresCtx.fontData = (u8*)malloc(size);
                if (s_hiresCtx.fontData && fread(s_hiresCtx.fontData, 1, size, f) == (size_t)size)
                {
                    int offset = stbtt_GetFontOffsetForIndex(s_hiresCtx.fontData, 0);
                    if (stbtt_InitFont(&s_hiresCtx.fontInfo, s_hiresCtx.fontData, offset))
                    {
                        int ascent, descent, lineGap;
                        s_hiresCtx.fontScale = stbtt_ScaleForPixelHeight(&s_hiresCtx.fontInfo, STUDIO_HIRES_FONT_SIZE);
                        stbtt_GetFontVMetrics(&s_hiresCtx.fontInfo, &ascent, &descent, &lineGap);
                        s_hiresCtx.fontAscent = (float)ascent * s_hiresCtx.fontScale;
                        s_hiresCtx.fontLoaded = true;
                        printf("[Studio Font] Successfully loaded Source Han Sans: %s (scale: %f, ascent: %f)\n", FontPaths[i], s_hiresCtx.fontScale, s_hiresCtx.fontAscent);
                        fclose(f);
                        return;
                    }
                }
                if (s_hiresCtx.fontData)
                {
                    free(s_hiresCtx.fontData);
                    s_hiresCtx.fontData = NULL;
                }
            }
            fclose(f);
        }
    }
    printf("[Studio Font] WARNING: Failed to load TrueType font! Using fallback.\n");
}

void studio_hires_init(Studio* studio)
{
    if (!s_hiresCtx.hiresBuffer)
    {
        s_hiresCtx.hiresBuffer = (u32*)calloc(STUDIO_HIRES_WIDTH * STUDIO_HIRES_HEIGHT, sizeof(u32));
    }
    if (!s_hiresCtx.fontLoaded)
    {
        loadStudioFont();
    }
    s_hiresCtx.initialized = true;
}

void studio_hires_free(Studio* studio)
{
    if (s_hiresCtx.hiresBuffer)
    {
        free(s_hiresCtx.hiresBuffer);
        s_hiresCtx.hiresBuffer = NULL;
    }
    if (s_hiresCtx.fontData)
    {
        free(s_hiresCtx.fontData);
        s_hiresCtx.fontData = NULL;
    }
    s_hiresCtx.fontLoaded = false;
    s_hiresCtx.initialized = false;
}

u32* studio_hires_get_screen(Studio* studio)
{
    if (!s_hiresCtx.hiresBuffer)
    {
        studio_hires_init(studio);
    }
    return s_hiresCtx.hiresBuffer;
}

bool studio_hires_is_font_loaded(Studio* studio)
{
    return s_hiresCtx.fontLoaded;
}

u32 studio_hires_get_color(Studio* studio, u8 colorIndex)
{
    if (!studio) return 0xff000000;
    const StudioConfig* cfg = getConfig(studio);
    if (!cfg || !cfg->cart) return 0xff000000;
    const tic_palette* pal = &cfg->cart->bank0.palette.vbank0;
    return tic_rgba(&pal->colors[colorIndex & 0x0f]);
}

void studio_hires_cls(Studio* studio, u32 color)
{
    u32* screen = studio_hires_get_screen(studio);
    if (!screen) return;
    for (s32 i = 0; i < STUDIO_HIRES_WIDTH * STUDIO_HIRES_HEIGHT; i++)
    {
        screen[i] = color;
    }
}

void studio_hires_pixel(Studio* studio, s32 x, s32 y, u32 color)
{
    if (x >= 0 && x < STUDIO_HIRES_WIDTH && y >= 0 && y < STUDIO_HIRES_HEIGHT)
    {
        u32* screen = studio_hires_get_screen(studio);
        if (screen) screen[y * STUDIO_HIRES_WIDTH + x] = color;
    }
}

void studio_hires_rect(Studio* studio, s32 x, s32 y, s32 w, s32 h, u32 color)
{
    u32* screen = studio_hires_get_screen(studio);
    if (!screen) return;
    s32 x1 = x < 0 ? 0 : x;
    s32 y1 = y < 0 ? 0 : y;
    s32 x2 = (x + w) > STUDIO_HIRES_WIDTH ? STUDIO_HIRES_WIDTH : (x + w);
    s32 y2 = (y + h) > STUDIO_HIRES_HEIGHT ? STUDIO_HIRES_HEIGHT : (y + h);
    for (s32 py = y1; py < y2; py++)
    {
        u32* row = &screen[py * STUDIO_HIRES_WIDTH];
        for (s32 px = x1; px < x2; px++)
        {
            row[px] = color;
        }
    }
}

void studio_hires_rect_border(Studio* studio, s32 x, s32 y, s32 w, s32 h, u32 color)
{
    studio_hires_rect(studio, x, y, w, 1, color);
    studio_hires_rect(studio, x, y + h - 1, w, 1, color);
    studio_hires_rect(studio, x, y, 1, h, color);
    studio_hires_rect(studio, x + w - 1, y, 1, h, color);
}

void studio_hires_icon2x(Studio* studio, s32 iconId, s32 x, s32 y, u32 color)
{
    if (!studio) return;
    const StudioConfig* cfg = getConfig(studio);
    if (!cfg || !cfg->cart) return;
    const tic_bank* bank = &cfg->cart->bank0;
    s32 src = 0;
    for (s32 sy = 0; sy < TIC_SPRITESIZE; sy++)
    {
        for (s32 sx = 0; sx < TIC_SPRITESIZE; sx++)
        {
            if (tic_tool_peek4(&bank->tiles.data[iconId].data, src++))
            {
                studio_hires_rect(studio, x + sx * 2, y + sy * 2, 2, 2, color);
            }
        }
    }
}

static void drawFallbackAsciiChar(Studio* studio, char c, s32 x, s32 y, u32 color, s32 clipTop, s32 clipBottom)
{
    if (!studio) return;
    tic_mem* tic = getMemory(studio);
    if (!tic || c < 32 || c > 126) return;
    const u8* charData = &tic->ram->font.regular.data[(u8)c * 8];
    for (s32 r = 0; r < 8; r++)
    {
        s32 py = y + r * 2;
        if (py >= clipTop && py + 1 < clipBottom)
        {
            u8 row = charData[r];
            for (s32 col = 0; col < 8; col++)
            {
                if (row & (1 << col))
                {
                    studio_hires_rect(studio, x + col * 2, py, 2, 2, color);
                }
            }
        }
    }
}

s32 studio_hires_draw_char(Studio* studio, u32 codepoint, s32 x, s32 y, u32 color, s32 clipTop, s32 clipBottom)
{
    if (!s_hiresCtx.fontLoaded)
    {
        drawFallbackAsciiChar(studio, (char)(codepoint < 128 ? codepoint : '?'), x, y, color, clipTop, clipBottom);
        return 12;
    }

    int adv, lsb, x0, y0, x1, y1;
    stbtt_GetCodepointHMetrics(&s_hiresCtx.fontInfo, codepoint, &adv, &lsb);
    stbtt_GetCodepointBitmapBox(&s_hiresCtx.fontInfo, codepoint, s_hiresCtx.fontScale, s_hiresCtx.fontScale, &x0, &y0, &x1, &y1);

    int gw = x1 - x0;
    int gh = y1 - y0;
    int charAdvance = (int)(adv * s_hiresCtx.fontScale + 0.5f);
    if (charAdvance < 7) charAdvance = (codepoint >= 0x4E00) ? 14 : 8;

    if (gw > 0 && gh > 0)
    {
        unsigned char* bmp = (unsigned char*)calloc(gw * gh, 1);
        if (bmp)
        {
            stbtt_MakeCodepointBitmap(&s_hiresCtx.fontInfo, bmp, gw, gh, gw, s_hiresCtx.fontScale, s_hiresCtx.fontScale, codepoint);
            int originY = y + (int)s_hiresCtx.fontAscent;
            int px0 = x + x0;
            int py0 = originY + y0;

            for (int r = 0; r < gh; r++)
            {
                int py = py0 + r;
                if (py >= clipTop && py < clipBottom && py >= 0 && py < STUDIO_HIRES_HEIGHT)
                {
                    for (int c = 0; c < gw; c++)
                    {
                        if (bmp[r * gw + c] >= 128)
                        {
                            int px = px0 + c;
                            if (px >= 0 && px < STUDIO_HIRES_WIDTH)
                            {
                                studio_hires_pixel(studio, px, py, color);
                            }
                        }
                    }
                }
            }
            free(bmp);
        }
    }
    return charAdvance;
}

s32 studio_hires_measure_text(Studio* studio, const char* text)
{
    if (!text || !*text) return 0;
    s32 width = 0;
    const char* ptr = text;
    while (*ptr)
    {
        u32 cp = utf8_decode_codepoint(&ptr);
        if (cp == '\0') break;
        if (s_hiresCtx.fontLoaded)
        {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&s_hiresCtx.fontInfo, cp, &adv, &lsb);
            int charAdvance = (int)(adv * s_hiresCtx.fontScale + 0.5f);
            if (charAdvance < 7) charAdvance = (cp >= 0x4E00) ? 14 : 8;
            width += charAdvance;
        }
        else
        {
            width += (cp >= 0x4E00) ? 16 : 8;
        }
    }
    return width;
}

s32 studio_hires_draw_text(Studio* studio, const char* text, s32 x, s32 y, u32 color, s32 clipTop, s32 clipBottom)
{
    if (!text || !*text) return 0;
    s32 curX = x;
    const char* ptr = text;
    while (*ptr)
    {
        u32 cp = utf8_decode_codepoint(&ptr);
        if (cp == '\0') break;
        curX += studio_hires_draw_char(studio, cp, curX, y, color, clipTop, clipBottom);
    }
    return curX - x;
}

void studio_hires_draw_toolbar(Studio* studio, EditorMode currentMode, s32 mouseX, s32 mouseY, bool mouseClick)
{
    static const EditorMode Modes[] = {TIC_CODE_MODE, TIC_SPRITE_MODE, TIC_MAP_MODE, TIC_SFX_MODE, TIC_MUSIC_MODE, TIC_AI_MODE};
    static const u8 Icons[] = {tic_icon_code, tic_icon_sprite, tic_icon_map, tic_icon_sfx, tic_icon_music, tic_icon_ai};
    static const char* Tips[] = {"CODE EDITOR [F1]", "SPRITE EDITOR [F2]", "MAP EDITOR [F3]", "SFX EDITOR [F4]", "MUSIC EDITOR [F5]", "AI ASSISTANT [F6]"};

    s32 count = (s32)(sizeof(Modes) / sizeof(Modes[0]));
    s32 btnW = 16;
    u32 colWhite     = studio_hires_get_color(studio, tic_color_white);
    u32 colGrey      = studio_hires_get_color(studio, tic_color_grey);
    u32 colLightGrey = studio_hires_get_color(studio, tic_color_light_grey);
    u32 colBlack     = studio_hires_get_color(studio, tic_color_black);

    studio_hires_rect(studio, STUDIO_HIRES_OFFSET_LEFT, STUDIO_HIRES_TOOLBAR_Y, STUDIO_HIRES_VIEW_WIDTH, STUDIO_HIRES_TOOLBAR_H, colWhite);

    for (s32 i = 0; i < count; i++)
    {
        s32 bx = STUDIO_HIRES_OFFSET_LEFT + i * btnW;
        s32 by = STUDIO_HIRES_TOOLBAR_Y;
        bool over = (mouseX >= bx && mouseX < bx + btnW && mouseY >= by && mouseY < by + STUDIO_HIRES_TOOLBAR_H);

        if (over && studio)
        {
            setCursor(studio, tic_cursor_hand);
            showTooltip(studio, Tips[i]);
            if (mouseClick)
            {
                setStudioMode(studio, Modes[i]);
                return;
            }
        }

        bool isCurrentMode = (Modes[i] == currentMode);
        if (isCurrentMode)
        {
            studio_hires_icon2x(studio, tic_icon_tab, bx, by, colGrey);
            studio_hires_icon2x(studio, Icons[i], bx, by + 1, colBlack);
            studio_hires_icon2x(studio, Icons[i], bx, by, colWhite);
        }
        else
        {
            studio_hires_icon2x(studio, Icons[i], bx, by, over ? colGrey : colLightGrey);
        }
    }

    const char* title = "OpenTIC 幻想控制台";
    if (currentMode == TIC_CODE_MODE) title = "OpenTIC 代码编辑器 [F1]";
    else if (currentMode == TIC_SPRITE_MODE) title = "OpenTIC 精灵编辑器 [F2]";
    else if (currentMode == TIC_MAP_MODE) title = "OpenTIC 地图编辑器 [F3]";
    else if (currentMode == TIC_SFX_MODE) title = "OpenTIC 音效编辑器 [F4]";
    else if (currentMode == TIC_MUSIC_MODE) title = "OpenTIC 音乐编辑器 [F5]";
    else if (currentMode == TIC_AI_MODE) title = "OpenTIC AI 助手 [F6]";
    else if (currentMode == TIC_CONSOLE_MODE) title = "OpenTIC 控制台";

    studio_hires_draw_text(studio, title, STUDIO_HIRES_OFFSET_LEFT + count * btnW + 8, STUDIO_HIRES_TOOLBAR_Y + 1, colBlack, STUDIO_HIRES_TOOLBAR_Y, STUDIO_HIRES_TOOLBAR_Y + STUDIO_HIRES_TOOLBAR_H);
}

void studio_hires_draw_cursor(Studio* studio, s32 mouseX, s32 mouseY)
{
    if (!studio) return;
    tic_mem* tic = getMemory(studio);
    const StudioConfig* cfg = getConfig(studio);
    if (!tic || !cfg || !cfg->cart) return;

    if (mouseX >= 0 && mouseX < STUDIO_HIRES_WIDTH && mouseY >= 0 && mouseY < STUDIO_HIRES_HEIGHT)
    {
        s32 sprite = CLAMP(tic->ram->vram.vars.cursor.sprite, 0, TIC_BANK_SPRITES - 1);
        const tic_bank* bank = &cfg->cart->bank0;
        tic_point hot = (tic_point[]){ {0, 0}, {3, 0}, {2, 3} }[CLAMP(sprite, 0, 2)];
        const tic_palette* pal = &bank->palette.vbank0;
        const tic_tile* tile = &bank->sprites.data[sprite];
        s32 sx = mouseX - hot.x * 2;
        s32 sy = mouseY - hot.y * 2;

        for (s32 y = 0; y < TIC_SPRITESIZE; y++)
        {
            for (s32 x = 0; x < TIC_SPRITESIZE; x++)
            {
                u8 c = tic_tool_peek4(tile->data, y * TIC_SPRITESIZE + x);
                if (c)
                {
                    studio_hires_rect(studio, sx + x * 2, sy + y * 2, 2, 2, tic_rgba(&pal->colors[c]));
                }
            }
        }
    }
}

void studio_hires_blit_2x(Studio* studio, const u32* src256x144)
{
    if (!studio || !src256x144) return;
    u32* dst = studio_hires_get_screen(studio);
    if (!dst) return;

    for (s32 y = 0; y < TIC80_FULLHEIGHT; y++)
    {
        const u32* srcRow = &src256x144[y * TIC80_FULLWIDTH];
        u32* dstRow1 = &dst[(y * 2) * STUDIO_HIRES_WIDTH];
        u32* dstRow2 = &dst[(y * 2 + 1) * STUDIO_HIRES_WIDTH];

        for (s32 x = 0; x < TIC80_FULLWIDTH; x++)
        {
            u32 pixel = srcRow[x];
            dstRow1[x * 2]     = pixel;
            dstRow1[x * 2 + 1] = pixel;
            dstRow2[x * 2]     = pixel;
            dstRow2[x * 2 + 1] = pixel;
        }
    }
}

void studio_hires_update_system_font(tic_mem* tic, tic_font* systemFont)
{
    if (!tic || !systemFont) return;
    if (!s_hiresCtx.fontLoaded)
    {
        loadStudioFont();
    }
    if (!s_hiresCtx.fontLoaded) return;

    float scale8 = stbtt_ScaleForPixelHeight(&s_hiresCtx.fontInfo, 7.5f);
    int ascent8, descent8, lineGap8;
    stbtt_GetFontVMetrics(&s_hiresCtx.fontInfo, &ascent8, &descent8, &lineGap8);
    int originY8 = (int)((float)ascent8 * scale8 + 0.5f);

    u8* fontData = (u8*)systemFont;

    for (s32 c = 32; c <= 126; c++)
    {
        int adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&s_hiresCtx.fontInfo, c, &adv, &lsb);
        stbtt_GetCodepointBitmapBox(&s_hiresCtx.fontInfo, c, scale8, scale8, &x0, &y0, &x1, &y1);

        int gw = x1 - x0;
        int gh = y1 - y0;

        if (gw > 0 && gh > 0 && gw <= 8 && gh <= 8)
        {
            u8 bmp[64] = {0};
            stbtt_MakeCodepointBitmap(&s_hiresCtx.fontInfo, bmp, gw, gh, gw, scale8, scale8, c);

            for (s32 row = 0; row < 8; row++)
                fontData[c * 8 + row] = 0;

            int px0 = x0;
            int py0 = originY8 + y0;
            if (px0 < 0) px0 = 0;
            if (py0 < 0) py0 = 0;

            for (int r = 0; r < gh; r++)
            {
                int py = py0 + r;
                if (py >= 0 && py < 8)
                {
                    for (int col = 0; col < gw; col++)
                    {
                        int px = px0 + col;
                        if (px >= 0 && px < 8)
                        {
                            if (bmp[r * gw + col] >= 80)
                            {
                                fontData[c * 8 + py] |= (1 << px);
                            }
                        }
                    }
                }
            }
        }
    }
    tic->ram->font = *systemFont;
}
