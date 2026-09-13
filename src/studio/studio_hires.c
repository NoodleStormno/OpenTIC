// OpenTIC Studio High-Resolution (512x288) TrueType Engine Implementation
#include "studio_hires.h"

#if defined(BUILD_EDITORS)
#include "editors/code.h"
#endif

#if defined(BUILD_EDITORS) || defined(BUILD_SURF)
#include "screens/console.h"
#endif

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

void studio_hires_icon4x(Studio* studio, s32 iconId, s32 x, s32 y, u32 color)
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
                studio_hires_rect(studio, x + sx * 4, y + sy * 4, 4, 4, color);
            }
        }
    }
}

void studio_hires_icon6x(Studio* studio, s32 iconId, s32 x, s32 y, u32 color)
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
                studio_hires_rect(studio, x + sx * 6, y + sy * 6, 6, 6, color);
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
        s32 py = y + r * 3;
        if (py >= clipTop && py + 2 < clipBottom)
        {
            u8 row = charData[r];
            for (s32 col = 0; col < 8; col++)
            {
                if (row & (1 << col))
                {
                    studio_hires_rect(studio, x + col * 3, py, 3, 3, color);
                }
            }
        }
    }
}

s32 studio_hires_draw_char_cell(Studio* studio, u32 codepoint, s32 x, s32 y, s32 cellW, u32 color, s32 clipTop, s32 clipBottom)
{
    if (codepoint == 0xFEFF || codepoint == 0x200B || codepoint == '\r' || codepoint == 0)
        return 0;

    if (!s_hiresCtx.fontLoaded)
    {
        drawFallbackAsciiChar(studio, (char)(codepoint < 128 ? codepoint : '?'), x, y, color, clipTop, clipBottom);
        return cellW > 0 ? cellW : 14;
    }

    int adv, lsb, x0, y0, x1, y1;
    stbtt_GetCodepointHMetrics(&s_hiresCtx.fontInfo, codepoint, &adv, &lsb);
    stbtt_GetCodepointBitmapBox(&s_hiresCtx.fontInfo, codepoint, s_hiresCtx.fontScale, s_hiresCtx.fontScale, &x0, &y0, &x1, &y1);

    int gw = x1 - x0;
    int gh = y1 - y0;
    int charAdvance = (int)(adv * s_hiresCtx.fontScale + 0.5f);
    if (charAdvance <= 0) charAdvance = (codepoint >= 0x4E00) ? 22 : 12;

    int xOffset = 0;
    if (cellW > 0 && codepoint != ' ')
    {
        xOffset = (cellW - charAdvance) / 2;
        if (xOffset < 0) xOffset = 0;
    }

    if (gw > 0 && gh > 0)
    {
        unsigned char* bmp = (unsigned char*)calloc(gw * gh, 1);
        if (bmp)
        {
            stbtt_MakeCodepointBitmap(&s_hiresCtx.fontInfo, bmp, gw, gh, gw, s_hiresCtx.fontScale, s_hiresCtx.fontScale, codepoint);
            int originY = y + (int)s_hiresCtx.fontAscent;
            int px0 = x + xOffset + x0;
            int py0 = originY + y0;

            for (int r = 0; r < gh; r++)
            {
                int py = py0 + r;
                if (py >= clipTop && py < clipBottom && py >= 0 && py < STUDIO_HIRES_HEIGHT)
                {
                    for (int c = 0; c < gw; c++)
                    {
                        u8 alpha = bmp[r * gw + c];
                        // Crisp threshold 70 preserves all thin vertical strokes
                        if (alpha >= 70)
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

s32 studio_hires_draw_char(Studio* studio, u32 codepoint, s32 x, s32 y, u32 color, s32 clipTop, s32 clipBottom)
{
    return studio_hires_draw_char_cell(studio, codepoint, x, y, 0, color, clipTop, clipBottom);
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
            if (charAdvance < 12) charAdvance = (cp >= 0x4E00) ? 32 : 16;
            width += charAdvance;
        }
        else
        {
            width += (cp >= 0x4E00) ? 32 : 16;
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
    static const char* Tips[] = {"代码编辑器 [F1]", "精灵编辑器 [F2]", "地图编辑器 [F3]", "音效编辑器 [F4]", "音乐编辑器 [F5]", "AI 助手 [F6]"};

    s32 count = (s32)(sizeof(Modes) / sizeof(Modes[0]));
    s32 btnW = 72;
    s32 btnH = STUDIO_HIRES_TOOLBAR_H;
    u32 colWhite     = studio_hires_get_color(studio, tic_color_white);
    u32 colGrey      = studio_hires_get_color(studio, tic_color_grey);
    u32 colLightGrey = studio_hires_get_color(studio, tic_color_light_grey);
    u32 colDarkGrey  = studio_hires_get_color(studio, tic_color_dark_grey);
    u32 colYellow    = studio_hires_get_color(studio, tic_color_yellow);

    // Modern sleek dark toolbar
    studio_hires_rect(studio, 0, 0, STUDIO_HIRES_WIDTH, btnH, 0xff1c1d24);
    studio_hires_rect(studio, 0, btnH - 1, STUDIO_HIRES_WIDTH, 1, colDarkGrey);

    for (s32 i = 0; i < count; i++)
    {
        s32 bx = i * btnW;
        s32 by = 0;
        bool over = (mouseX >= bx && mouseX < bx + btnW && mouseY >= by && mouseY < by + btnH);

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
        s32 iconX = bx + (btnW - 48) / 2;
        s32 iconY = by + (btnH - 48) / 2;

        if (isCurrentMode)
        {
            studio_hires_rect(studio, bx, by, btnW, btnH, 0xff2a2c3a);
            studio_hires_rect(studio, bx, by + btnH - 4, btnW, 4, colYellow);
            studio_hires_icon6x(studio, Icons[i], iconX, iconY, colWhite);
        }
        else
        {
            if (over) studio_hires_rect(studio, bx, by, btnW, btnH, 0xff242632);
            studio_hires_icon6x(studio, Icons[i], iconX, iconY, over ? colWhite : colLightGrey);
        }
    }

    const char* title = "OpenTIC 幻想控制台";
    if (currentMode == TIC_CODE_MODE) title = "OpenTIC 代码编辑器 [F1]";
    else if (currentMode == TIC_SPRITE_MODE) title = "OpenTIC 精灵编辑器 [F2]";
    else if (currentMode == TIC_MAP_MODE) title = "OpenTIC 地图编辑器 [F3]";
    else if (currentMode == TIC_SFX_MODE) title = "OpenTIC 音效编辑器 [F4]";
    else if (currentMode == TIC_MUSIC_MODE) title = "OpenTIC 音乐编辑器 [F5]";
    else if (currentMode == TIC_AI_MODE) title = "OpenTIC AI 助手 [F6]";
    else if (currentMode == TIC_CONSOLE_MODE) title = "OpenTIC 终端控制台";

    studio_hires_draw_text(studio, title, count * btnW + 28, 20, colWhite, 0, btnH);
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
        s32 scale = 3;
        s32 sx = mouseX - hot.x * scale;
        s32 sy = mouseY - hot.y * scale;

        for (s32 y = 0; y < TIC_SPRITESIZE; y++)
        {
            for (s32 x = 0; x < TIC_SPRITESIZE; x++)
            {
                u8 c = tic_tool_peek4(tile->data, y * TIC_SPRITESIZE + x);
                if (c)
                {
                    studio_hires_rect(studio, sx + x * scale, sy + y * scale, scale, scale, tic_rgba(&pal->colors[c]));
                }
            }
        }
    }
}

void studio_hires_blit_to_1080p(Studio* studio, const u32* src256x144)
{
    if (!studio || !src256x144) return;
    u32* dst = studio_hires_get_screen(studio);
    if (!dst) return;

    u32 borderColor = src256x144[0];

    // Blit retro 256x144 canvas into Y = 72..1080 (height = 1008, 7x scale)
    // Horizontal: 256 * 7 = 1792, centered at X = 64..1856 (left/right margin = 64px)
    for (s32 dy = 0; dy < STUDIO_HIRES_HEIGHT; dy++)
    {
        u32* dstRow = &dst[dy * STUDIO_HIRES_WIDTH];
        if (dy < STUDIO_HIRES_TOOLBAR_H)
        {
            continue;
        }

        s32 sy = (dy - STUDIO_HIRES_TOOLBAR_H) / 7;
        if (sy >= TIC80_FULLHEIGHT) sy = TIC80_FULLHEIGHT - 1;
        const u32* srcRow = &src256x144[sy * TIC80_FULLWIDTH];

        // Left margin (0..63)
        for (s32 dx = 0; dx < 64; dx++)
            dstRow[dx] = borderColor;

        // Centered 7x content (64..1855)
        for (s32 dx = 64; dx < 1856; dx++)
        {
            s32 sx = (dx - 64) / 7;
            if (sx >= TIC80_FULLWIDTH) sx = TIC80_FULLWIDTH - 1;
            dstRow[dx] = srcRow[sx];
        }

        // Right margin (1856..1919)
        for (s32 dx = 1856; dx < STUDIO_HIRES_WIDTH; dx++)
            dstRow[dx] = borderColor;
    }
}

void studio_hires_blit_2x(Studio* studio, const u32* src256x144)
{
    studio_hires_blit_to_1080p(studio, src256x144);
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

#if defined(BUILD_EDITORS)
void studio_hires_draw_code(Studio* studio, struct Code* code)
{
    if (!studio || !code) return;

    u32* screen = studio_hires_get_screen(studio);
    if (!screen) return;

    const StudioConfig* cfg = getConfig(studio);
    if (!cfg) return;

    const u8* syntaxColors = (const u8*)&cfg->theme.code;
    u32 colBG        = studio_hires_get_color(studio, cfg->theme.code.BG);
    u32 colCursor    = studio_hires_get_color(studio, cfg->theme.code.cursor);
    u32 colSelect    = studio_hires_get_color(studio, cfg->theme.code.select);
    u32 colDarkGrey  = studio_hires_get_color(studio, tic_color_dark_grey);
    u32 colGrey      = studio_hires_get_color(studio, tic_color_grey);
    u32 colLightGrey = studio_hires_get_color(studio, tic_color_light_grey);
    u32 colWhite     = studio_hires_get_color(studio, tic_color_white);
    u32 colYellow    = studio_hires_get_color(studio, tic_color_yellow);
    u32 colBlack     = studio_hires_get_color(studio, tic_color_black);
    u32 colCyan      = studio_hires_get_color(studio, tic_color_cyan);

    // 1. Fill entire 1920x1080 workspace
    studio_hires_cls(studio, colBG);

    // 2. Draw Top Toolbar (with mode selector and title)
    tic_mem* tic = getMemory(studio);
    s32 mx = 0, my = 0;
    studio_get_hires_mouse(studio, &mx, &my);
    if (mx < 0 && tic)
    {
        mx = tic->ram->input.mouse.x * 1920 / 256;
        my = tic->ram->input.mouse.y * 1080 / 144;
    }
    bool click = tic ? (bool)tic->ram->input.mouse.left : false;
    studio_hires_draw_toolbar(studio, TIC_CODE_MODE, mx, my, click);

    // Right-side code editor action buttons on toolbar
    static const u8 CodeIcons[] = {tic_icon_hand, tic_icon_find, tic_icon_goto, tic_icon_bookmark, tic_icon_outline, tic_icon_run};
    static const char* CodeTips[] = {"拖拽模式 [右键]", "查找 [Ctrl+F]", "跳转行号 [Ctrl+G]", "书签 [Ctrl+B]", "大纲 [Ctrl+O]", "运行卡带 [Ctrl+R]"};
    enum { CodeBtnCount = COUNT_OF(CodeIcons) };
    s32 btnW = 64;
    s32 rightStart = STUDIO_HIRES_WIDTH - CodeBtnCount * btnW - 16;
    for (s32 bi = 0; bi < CodeBtnCount; bi++)
    {
        s32 bx = rightStart + bi * btnW;
        s32 by = 0;
        bool over = (mx >= bx && mx < bx + btnW && my >= by && my < by + STUDIO_HIRES_TOOLBAR_H);
        if (over)
        {
            setCursor(studio, tic_cursor_hand);
            showTooltip(studio, CodeTips[bi]);
            if (click)
            {
                if (bi == 5) { setStudioMode(studio, TIC_RUN_MODE); return; }
                else if (bi == 1) { code->mode = TEXT_FIND_MODE; }
                else if (bi == 2) { code->mode = TEXT_GOTO_MODE; }
            }
        }
        if (over) studio_hires_rect(studio, bx, by, btnW, STUDIO_HIRES_TOOLBAR_H, 0xff242632);
        studio_hires_icon6x(studio, CodeIcons[bi], bx + (btnW - 48) / 2, by + (STUDIO_HIRES_TOOLBAR_H - 48) / 2, over ? colWhite : colLightGrey);
    }

    // 3. Layout dimensions for 1080p
    s32 statusH = 48;
    s32 gutterX = 0;
    s32 gutterW = 100;
    s32 gutterY = STUDIO_HIRES_TOOLBAR_H;
    s32 gutterH = STUDIO_HIRES_HEIGHT - STUDIO_HIRES_TOOLBAR_H - statusH;

    s32 codeX = gutterX + gutterW + 20;
    s32 codeY = gutterY + 12;
    s32 codeW = STUDIO_HIRES_WIDTH - codeX - 20;
    s32 codeH = gutterH - 24;
    s32 lineH = STUDIO_HIRES_LINE_HEIGHT;
    s32 visibleRows = codeH / lineH;

    // Draw Gutter Background & Separator Line
    studio_hires_rect(studio, gutterX, gutterY, gutterW, gutterH, 0xff14151a);
    studio_hires_rect(studio, gutterX + gutterW - 1, gutterY, 1, gutterH, colDarkGrey);

    // 4. Find all line starts in code->src
    const char* src = code->src ? code->src : "";

    const char* currentLinePtr = src;
    s32 currentLineIdx = 0;
    while (*currentLinePtr && currentLineIdx < code->scroll.y)
    {
        if (*currentLinePtr == '\n') currentLineIdx++;
        currentLinePtr++;
    }

    const char* selStart = MIN(code->cursor.selection, code->cursor.position);
    const char* selEnd   = MAX(code->cursor.selection, code->cursor.position);
    bool hasSelection = (code->cursor.selection != NULL && selStart < selEnd);

    // Render visible lines
    const char* linePtr = currentLinePtr;
    for (s32 row = 0; row < visibleRows && *linePtr; row++)
    {
        s32 lineIdx = code->scroll.y + row;
        const char* nextLinePtr = strchr(linePtr, '\n');
        const char* lineEnd = nextLinePtr ? nextLinePtr : (linePtr + strlen(linePtr));

        bool isCurrentLine = (code->cursor.position >= linePtr && code->cursor.position <= lineEnd);

        // Draw Line Number in Gutter
        char numStr[16];
        snprintf(numStr, sizeof(numStr), "%4d", lineIdx + 1);
        studio_hires_draw_text(studio, numStr, gutterX + 16, codeY + row * lineH + 4, isCurrentLine ? colYellow : colDarkGrey, gutterY, gutterY + gutterH);

        // Check if bookmarked
        s32 lineStartOffset = (s32)(linePtr - src);
        if (lineStartOffset >= 0 && lineStartOffset < TIC_CODE_SIZE && code->state && code->state[lineStartOffset].bookmark)
        {
            studio_hires_rect(studio, gutterX + 6, codeY + row * lineH + 8, 8, 24, colYellow);
        }

        // Draw Code Text
        const char* cpPtr = linePtr;
        s32 col = 0;
        while (cpPtr < lineEnd)
        {
            const char* charStart = cpPtr;
            u32 cp = utf8_decode_codepoint(&cpPtr);
            if (cp == 0xFEFF || cp == 0x200B || cp == '\r' || cp == 0) continue;
            s32 charOffset = (s32)(charStart - src);

            s32 tabCols = 1;
            s32 cw = (cp >= 0x80) ? 36 : 18;
            if (cp == '\t')
            {
                tabCols = 4 - (col % 4);
                if (tabCols <= 0) tabCols = 4;
                cw = tabCols * 18;
            }

            s32 drawX = codeX + (col - code->scroll.x) * 18;
            s32 drawY = codeY + row * lineH;

            // Selection Highlight
            if (hasSelection && charStart >= selStart && charStart < selEnd)
            {
                if (drawX + cw > codeX && drawX < codeX + codeW)
                {
                    studio_hires_rect(studio, drawX, drawY, cw, lineH, colSelect);
                }
            }

            // Draw Character
            if (drawX + cw > codeX && drawX < codeX + codeW)
            {
                if (cp != ' ' && cp != '\t')
                {
                    u8 syn = (charOffset < TIC_CODE_SIZE && code->state) ? code->state[charOffset].syntax : 0;
                    u8 palIdx = syntaxColors[syn & 7];
                    u32 color = studio_hires_get_color(studio, palIdx);
                    studio_hires_draw_char_cell(studio, cp, drawX, drawY, cw, color, codeY, codeY + codeH);
                }
            }

            // Cursor check
            if (charStart == code->cursor.position)
            {
                if ((code->tickCounter / 30) % 2 == 0)
                {
                    if (drawX >= codeX && drawX < codeX + codeW)
                    {
                        studio_hires_rect(studio, drawX, drawY + 2, 4, lineH - 4, colCursor);
                    }
                }
            }

            col += (cp == '\t') ? tabCols : (cp >= 0x80 ? 2 : 1);
        }

        // Cursor at end of line check
        if (code->cursor.position == lineEnd)
        {
            s32 drawX = codeX + (col - code->scroll.x) * 18;
            s32 drawY = codeY + row * lineH;
            if ((code->tickCounter / 30) % 2 == 0)
            {
                if (drawX >= codeX && drawX < codeX + codeW)
                {
                    studio_hires_rect(studio, drawX, drawY + 2, 4, lineH - 4, colCursor);
                }
            }
        }

        if (!nextLinePtr) break;
        linePtr = nextLinePtr + 1;
    }

    // 5. Bottom Status Bar
    s32 statusY = STUDIO_HIRES_HEIGHT - statusH;
    studio_hires_rect(studio, 0, statusY, STUDIO_HIRES_WIDTH, statusH, colDarkGrey);
    studio_hires_rect(studio, 0, statusY, STUDIO_HIRES_WIDTH, 1, colGrey);

    if (code->status.line[0])
    {
        studio_hires_draw_text(studio, code->status.line, 24, statusY + 10, colWhite, statusY, statusY + statusH);
    }
    studio_hires_draw_text(studio, "OpenTIC 幻想控制台 | Lua 5.3", STUDIO_HIRES_WIDTH / 2 - 160, statusY + 10, colLightGrey, statusY, statusY + statusH);
    if (code->status.size[0])
    {
        studio_hires_draw_text(studio, code->status.size, STUDIO_HIRES_WIDTH - 280, statusY + 10, colWhite, statusY, statusY + statusH);
    }

    // 6. Modal Popups (Find, Replace, Goto)
    if (code->mode == TEXT_FIND_MODE)
    {
        s32 pw = 720;
        s32 ph = 68;
        s32 px = (STUDIO_HIRES_WIDTH - pw) / 2;
        s32 py = 96;
        studio_hires_rect(studio, px, py, pw, ph, colBlack);
        studio_hires_rect_border(studio, px, py, pw, ph, colYellow);
        studio_hires_draw_text(studio, "查找: ", px + 20, py + 18, colYellow, py, py + ph);
        studio_hires_draw_text(studio, code->popup.text, px + 100, py + 18, colWhite, py, py + ph);
    }
    else if (code->mode == TEXT_REPLACE_MODE)
    {
        s32 pw = 720;
        s32 ph = 68;
        s32 px = (STUDIO_HIRES_WIDTH - pw) / 2;
        s32 py = 96;
        studio_hires_rect(studio, px, py, pw, ph, colBlack);
        studio_hires_rect_border(studio, px, py, pw, ph, colYellow);
        studio_hires_draw_text(studio, "替换: ", px + 20, py + 18, colYellow, py, py + ph);
        studio_hires_draw_text(studio, code->popup.text, px + 100, py + 18, colWhite, py, py + ph);
    }
    else if (code->mode == TEXT_GOTO_MODE)
    {
        s32 pw = 720;
        s32 ph = 68;
        s32 px = (STUDIO_HIRES_WIDTH - pw) / 2;
        s32 py = 96;
        studio_hires_rect(studio, px, py, pw, ph, colBlack);
        studio_hires_rect_border(studio, px, py, pw, ph, colCyan);
        studio_hires_draw_text(studio, "跳转行号: ", px + 20, py + 18, colCyan, py, py + ph);
        studio_hires_draw_text(studio, code->popup.text, px + 160, py + 18, colWhite, py, py + ph);
    }

    // 7. High-Res Mouse Cursor
    studio_hires_draw_cursor(studio, mx, my);
}
#endif

#if defined(BUILD_EDITORS) || defined(BUILD_SURF)
void studio_hires_draw_console(Studio* studio, struct Console* console)
{
    if (!studio || !console || !console->text || !console->color) return;

    u32* screen = studio_hires_get_screen(studio);
    if (!screen) return;

    u32 colBG = studio_hires_get_color(studio, tic_color_black);
    u32 colCursor = studio_hires_get_color(studio, tic_color_red);
    u32 colWhite = studio_hires_get_color(studio, tic_color_white);

    // Clear screen with Console background color
    studio_hires_cls(studio, colBG);

    // 40 columns * 36px = 1440px, centered horizontally: (1920 - 1440) / 2 = 240
    s32 startX = 240;
    s32 startY = 88;
    s32 colW = 36;
    s32 rowH = STUDIO_HIRES_LINE_HEIGHT;

    s32 cursorCol = (s32)console->cursor.pos.x;
    s32 cursorRow = (s32)console->cursor.pos.y - console->scroll.pos;

    const char* selStart = console->select.start;
    const char* selEnd = console->select.end;
    if (selStart && selEnd && selStart > selEnd)
    {
        const char* tmp = selStart;
        selStart = selEnd;
        selEnd = tmp;
    }

    s32 visibleRows = (STUDIO_HIRES_HEIGHT - startY - 40) / rowH;
    if (visibleRows > 22) visibleRows = 22;

    for (s32 r = 0; r < visibleRows; r++)
    {
        s32 rowIdx = console->scroll.pos + r;
        if (rowIdx < 0) continue;

        for (s32 c = 0; c < STUDIO_TEXT_BUFFER_WIDTH; c++)
        {
            s32 bufIdx = rowIdx * STUDIO_TEXT_BUFFER_WIDTH + c;
            char sym = console->text[bufIdx];
            u8 palIdx = console->color[bufIdx];

            s32 px = startX + c * colW;
            s32 py = startY + r * rowH;

            const char* ptr = &console->text[bufIdx];
            bool isSelected = (selStart && selEnd && ptr >= selStart && ptr <= selEnd);

            if (isSelected)
            {
                studio_hires_rect(studio, px, py, colW, rowH, colWhite);
            }

            if (sym && sym != ' ')
            {
                u32 charCol = studio_hires_get_color(studio, isSelected ? tic_color_black : palIdx);
                studio_hires_draw_char_cell(studio, (u32)(u8)sym, px, py, colW, charCol, startY, startY + visibleRows * rowH);
            }

            // Draw cursor
            if (c == cursorCol && r == cursorRow)
            {
                if ((console->tickCounter / 30) % 2 == 0)
                {
                    studio_hires_rect(studio, px, py + rowH - 6, colW, 6, colCursor);
                }
            }
        }
    }

    // High-Res Cursor
    tic_mem* tic = getMemory(studio);
    s32 mx = 0, my = 0;
    studio_get_hires_mouse(studio, &mx, &my);
    if (mx < 0 && tic)
    {
        mx = tic->ram->input.mouse.x * 1920 / 256;
        my = tic->ram->input.mouse.y * 1080 / 144;
    }
    studio_hires_draw_cursor(studio, mx, my);
}
#endif
