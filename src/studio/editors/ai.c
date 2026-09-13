// MIT License
// Copyright (c) 2026 Vadim Grigoruk @nesbox / OpenTIC Contributors

#include "ai.h"
#include "studio/studio.h"
#include "studio/studio_hires.h"
#include "studio/system.h"
#include "studio/project.h"
#include "tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "studio/stb_truetype.h"

#if defined(USE_NAETT)
#include "naett.h"
#endif

// High-resolution 1080p screen layout dimensions
#define AI_FULL_WIDTH   1920
#define AI_FULL_HEIGHT  1080
#define AI_VIEW_WIDTH   1920
#define AI_VIEW_HEIGHT  1080
#define AI_OFFSET_LEFT  0
#define AI_OFFSET_TOP   0

#define AI_FONT_SIZE    38.0f
#define AI_LINE_HEIGHT  52

#define TOOLBAR_Y       0
#define TOOLBAR_H       72

#define CHAT_TOP        (TOOLBAR_Y + TOOLBAR_H + 20)
#define INPUT_H         76
#define INPUT_Y         (AI_FULL_HEIGHT - INPUT_H - 28)
#define CHAT_BOTTOM     (INPUT_Y - 16)
#define CHAT_VIEW_H     (CHAT_BOTTOM - CHAT_TOP)

#define CHAT_X_LEFT     120
#define CHAT_X_RIGHT    (AI_FULL_WIDTH - 120)

static void sendUserPrompt(AiEditor* ai);

// Palette color helper (Sweetie 16 fallback if cart palette is unavailable)
static u32 getAiPaletteColor(AiEditor* ai, u8 colorIndex)
{
    static const tic_rgb s_Sweetie16[16] = {
        {0x1a, 0x1c, 0x2c}, // 0: black
        {0x5d, 0x27, 0x5d}, // 1: purple
        {0xb1, 0x3e, 0x53}, // 2: red
        {0xef, 0x7d, 0x57}, // 3: orange
        {0xff, 0xcd, 0x75}, // 4: yellow
        {0xa7, 0xf0, 0x70}, // 5: light green
        {0x38, 0xb7, 0x64}, // 6: green
        {0x25, 0x71, 0x79}, // 7: dark blue
        {0x29, 0x36, 0x6f}, // 8: blue
        {0x3b, 0x5d, 0xc9}, // 9: light blue
        {0x41, 0xa6, 0xf6}, // 10: cyan
        {0x73, 0xef, 0xf7}, // 11: light cyan
        {0xf4, 0xf4, 0xf4}, // 12: white
        {0x94, 0xb0, 0xc2}, // 13: light grey
        {0x56, 0x6c, 0x86}, // 14: grey
        {0x33, 0x3c, 0x57}  // 15: dark grey
    };
    colorIndex &= 0x0F;
    if (ai && ai->studio)
    {
        const StudioConfig* cfg = getConfig(ai->studio);
        if (cfg && cfg->cart)
        {
            return tic_rgba(&cfg->cart->bank0.palette.vbank0.colors[colorIndex]);
        }
    }
    return tic_rgba(&s_Sweetie16[colorIndex]);
}

// -------------------------------------------------------------
// High-Res 512x288 Drawing Primitives
// -------------------------------------------------------------

static inline void hiresPixel(AiEditor* ai, s32 x, s32 y, u32 color)
{
    if (x >= 0 && x < AI_FULL_WIDTH && y >= 0 && y < AI_FULL_HEIGHT)
    {
        ai->hiresScreen[y * AI_FULL_WIDTH + x] = color;
    }
}

static void hiresRect(AiEditor* ai, s32 x, s32 y, s32 w, s32 h, u32 color)
{
    s32 x1 = x < 0 ? 0 : x;
    s32 y1 = y < 0 ? 0 : y;
    s32 x2 = (x + w) > AI_FULL_WIDTH ? AI_FULL_WIDTH : (x + w);
    s32 y2 = (y + h) > AI_FULL_HEIGHT ? AI_FULL_HEIGHT : (y + h);
    for (s32 py = y1; py < y2; py++)
    {
        u32* row = &ai->hiresScreen[py * AI_FULL_WIDTH];
        for (s32 px = x1; px < x2; px++)
        {
            row[px] = color;
        }
    }
}

static void hiresRectBorder(AiEditor* ai, s32 x, s32 y, s32 w, s32 h, u32 color)
{
    hiresRect(ai, x, y, w, 1, color);
    hiresRect(ai, x, y + h - 1, w, 1, color);
    hiresRect(ai, x, y, 1, h, color);
    hiresRect(ai, x + w - 1, y, 1, h, color);
}

static void hiresBitIcon2x(AiEditor* ai, s32 id, s32 x, s32 y, u32 color)
{
    if (!ai || !ai->studio) return;
    const StudioConfig* cfg = getConfig(ai->studio);
    if (!cfg || !cfg->cart) return;
    const tic_bank* bank = &cfg->cart->bank0;
    s32 src = 0;
    for (s32 sy = 0; sy < TIC_SPRITESIZE; sy++)
    {
        for (s32 sx = 0; sx < TIC_SPRITESIZE; sx++)
        {
            if (tic_tool_peek4(&bank->tiles.data[id].data, src++))
            {
                hiresRect(ai, x + sx * 2, y + sy * 2, 2, 2, color);
            }
        }
    }
}

// -------------------------------------------------------------
// UTF-8 & TrueType Font Rendering (1-bit Non-Antialiased)
// -------------------------------------------------------------

static u32 utf8_decode(const char** p)
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

static void loadFont(AiEditor* ai)
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

    if (ai->fontLoaded) return;

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
                ai->fontData = (u8*)malloc(size);
                if (ai->fontData && fread(ai->fontData, 1, size, f) == (size_t)size)
                {
                    stbtt_fontinfo* info = (stbtt_fontinfo*)malloc(sizeof(stbtt_fontinfo));
                    int offset = stbtt_GetFontOffsetForIndex(ai->fontData, 0);
                    if (info && stbtt_InitFont(info, ai->fontData, offset))
                    {
                        int ascent, descent, lineGap;
                        ai->fontInfo = info;
                        ai->fontScale = stbtt_ScaleForPixelHeight(info, AI_FONT_SIZE);
                        stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
                        ai->fontAscent = (float)ascent * ai->fontScale;
                        ai->fontLoaded = true;
                        printf("[AI Font] Successfully loaded Source Han Sans font: %s (size: %ld, scale: %f, ascent: %f)\n", FontPaths[i], size, ai->fontScale, ai->fontAscent);
                        fclose(f);
                        return;
                    }
                    if (info) free(info);
                }
                if (ai->fontData)
                {
                    free(ai->fontData);
                    ai->fontData = NULL;
                }
            }
            fclose(f);
        }
    }
    printf("[AI Font] WARNING: Failed to load TrueType font! Using fallback.\n");
}

static void drawFallbackAsciiChar(AiEditor* ai, char c, s32 x, s32 y, u32 color, s32 clipYTop, s32 clipYBottom)
{
    if (!ai || !ai->tic || c < 32 || c > 126) return;
    const u8* charData = &ai->tic->ram->font.regular.data[(u8)c * 8];
    for (s32 r = 0; r < 8; r++)
    {
        s32 py = y + r * 2;
        if (py >= clipYTop && py + 1 < clipYBottom)
        {
            u8 row = charData[r];
            for (s32 col = 0; col < 8; col++)
            {
                if (row & (0x80 >> col))
                {
                    s32 px = x + col * 2;
                    if (px >= AI_OFFSET_LEFT && px + 1 < AI_OFFSET_LEFT + AI_VIEW_WIDTH)
                    {
                        hiresPixel(ai, px, py, color);
                        hiresPixel(ai, px + 1, py, color);
                        hiresPixel(ai, px, py + 1, color);
                        hiresPixel(ai, px + 1, py + 1, color);
                    }
                }
            }
        }
    }
}

static s32 drawTextUTF8Ex(AiEditor* ai, const char* str, s32 firstX, s32 wrapX, s32 y, u32 color, s32 clipYTop, s32 clipYBottom)
{
    stbtt_fontinfo* info;
    s32 curX, curY;
    const char* p;

    if (!str || !*str) return 0;

    info = (stbtt_fontinfo*)ai->fontInfo;
    curX = firstX;
    curY = y;
    p = str;

    while (*p)
    {
        u32 cp;
        s32 charWidth = 8;

        if (*p == '\n')
        {
            p++;
            curX = wrapX;
            curY += AI_LINE_HEIGHT;
            continue;
        }

        cp = utf8_decode(&p);

        if (ai->fontLoaded && info)
        {
            int glyph = stbtt_FindGlyphIndex(info, (int)cp);
            if (glyph != 0)
            {
                int adv, lsb;
                int w, h, xoff, yoff;
                u8* bmp;

                stbtt_GetGlyphHMetrics(info, glyph, &adv, &lsb);
                charWidth = (s32)(adv * ai->fontScale + 0.5f) + 1;
                if (charWidth <= 1) charWidth = (cp < 128) ? 14 : 26;
                if (cp < 128 && charWidth < 12 && cp != ' ') charWidth = 12;
                if (cp >= 128 && charWidth < 22) charWidth = 22;

                if (curX + charWidth > CHAT_X_RIGHT)
                {
                    curX = wrapX;
                    curY += AI_LINE_HEIGHT;
                }

                bmp = stbtt_GetGlyphBitmap(info, ai->fontScale, ai->fontScale, glyph, &w, &h, &xoff, &yoff);
                if (bmp)
                {
                    s32 drawY = curY + (s32)ai->fontAscent + yoff;
                    s32 drawX = curX + xoff;
                    s32 by;

                    for (by = 0; by < h; by++)
                    {
                        s32 py = drawY + by;
                        if (py >= clipYTop && py < clipYBottom)
                        {
                            s32 bx;
                            for (bx = 0; bx < w; bx++)
                            {
                                s32 px = drawX + bx;
                                if (px >= 0 && px < AI_FULL_WIDTH)
                                {
                                    u8 alpha = bmp[by * w + bx];
                                    // Bolder threshold (45) with horizontal dilation (px and px+1)
                                    if (alpha >= 45)
                                    {
                                        hiresPixel(ai, px, py, color);
                                        if (px + 1 < AI_FULL_WIDTH)
                                        {
                                            hiresPixel(ai, px + 1, py, color);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    stbtt_FreeBitmap(bmp, NULL);
                }

                curX += charWidth;
                continue;
            }
        }

        // Fallback for basic ASCII
        if (curX + 14 > CHAT_X_RIGHT)
        {
            curX = wrapX;
            curY += AI_LINE_HEIGHT;
        }
        if (cp >= 32 && cp <= 126)
        {
            drawFallbackAsciiChar(ai, (char)cp, curX, curY + 1, color, clipYTop, clipYBottom);
        }
        curX += 14;
    }

    return (curY - y) + AI_LINE_HEIGHT;
}

static inline s32 drawTextUTF8(AiEditor* ai, const char* str, s32 x, s32 y, u32 color, s32 clipYTop, s32 clipYBottom)
{
    return drawTextUTF8Ex(ai, str, x, x, y, color, clipYTop, clipYBottom);
}

static s32 measureTextWidth(AiEditor* ai, const char* str)
{
    stbtt_fontinfo* info;
    s32 curX = 0;
    const char* p = str;
    if (!str || !*str) return 0;
    info = (stbtt_fontinfo*)ai->fontInfo;

    while (*p)
    {
        u32 cp = utf8_decode(&p);
        s32 charWidth = 14;
        if (ai->fontLoaded && info)
        {
            int glyph = stbtt_FindGlyphIndex(info, (int)cp);
            if (glyph != 0)
            {
                int adv, lsb;
                stbtt_GetGlyphHMetrics(info, glyph, &adv, &lsb);
                charWidth = (s32)(adv * ai->fontScale + 0.5f) + 1;
                if (charWidth <= 1) charWidth = (cp < 128) ? 14 : 26;
                if (cp < 128 && charWidth < 12 && cp != ' ') charWidth = 12;
                if (cp >= 128 && charWidth < 22) charWidth = 22;
            }
        }
        curX += charWidth;
    }
    return curX;
}

static s32 measureTextHeightEx(AiEditor* ai, const char* str, s32 firstX, s32 wrapX)
{
    stbtt_fontinfo* info;
    s32 curX, totalHeight;
    const char* p;

    if (!str || !*str) return AI_LINE_HEIGHT;

    info = (stbtt_fontinfo*)ai->fontInfo;
    curX = firstX;
    totalHeight = AI_LINE_HEIGHT;
    p = str;

    while (*p)
    {
        if (*p == '\n')
        {
            p++;
            curX = wrapX;
            totalHeight += AI_LINE_HEIGHT;
            continue;
        }

        u32 cp = utf8_decode(&p);
        s32 charWidth = 14;

        if (ai->fontLoaded && info)
        {
            int glyph = stbtt_FindGlyphIndex(info, (int)cp);
            if (glyph != 0)
            {
                int adv, lsb;
                stbtt_GetGlyphHMetrics(info, glyph, &adv, &lsb);
                charWidth = (s32)(adv * ai->fontScale + 0.5f) + 1;
                if (charWidth <= 1) charWidth = (cp < 128) ? 14 : 26;
                if (cp < 128 && charWidth < 12 && cp != ' ') charWidth = 12;
                if (cp >= 128 && charWidth < 22) charWidth = 22;
            }
        }

        if (curX + charWidth > CHAT_X_RIGHT)
        {
            curX = wrapX;
            totalHeight += AI_LINE_HEIGHT;
        }
        curX += charWidth;
    }

    return totalHeight;
}

static inline s32 measureTextHeight(AiEditor* ai, const char* str, s32 startX)
{
    return measureTextHeightEx(ai, str, startX, startX);
}

typedef struct
{
    s32 start;
    s32 end;
} InputLineInfo;

static s32 computeInputLayout(AiEditor* ai, InputLineInfo* lines, s32 maxLines, s32* outCursorX, s32* outCursorY, s32 maxLineW)
{
    stbtt_fontinfo* info = (stbtt_fontinfo*)ai->fontInfo;
    s32 lineCount = 0;
    s32 curLineStart = 0;
    s32 curW = 0;
    s32 cursorX = 0;
    s32 cursorLine = 0;
    bool cursorFound = false;

    const char* str = ai->input;
    const char* p = str;
    s32 byteIdx = 0;

    while (*p && lineCount < maxLines)
    {
        if (byteIdx == ai->cursor)
        {
            cursorX = curW;
            cursorLine = lineCount;
            cursorFound = true;
        }

        if (*p == '\n')
        {
            if (lineCount < maxLines)
            {
                lines[lineCount].start = curLineStart;
                lines[lineCount].end = byteIdx;
                lineCount++;
            }
            p++;
            byteIdx++;
            curLineStart = byteIdx;
            curW = 0;
            continue;
        }

        const char* charStart = p;
        u32 cp = utf8_decode(&p);
        s32 charBytes = (s32)(p - charStart);
        s32 charW = 22;
        if (ai->fontLoaded && info)
        {
            int glyph = stbtt_FindGlyphIndex(info, (int)cp);
            if (glyph != 0)
            {
                int adv, lsb;
                stbtt_GetGlyphHMetrics(info, glyph, &adv, &lsb);
                charW = (s32)(adv * ai->fontScale + 0.5f) + 1;
                if (charW <= 1) charW = (cp < 128) ? 22 : 44;
                if (cp < 128 && charW < 16 && cp != ' ') charW = 16;
                if (cp >= 128 && charW < 32) charW = 32;
            }
        }

        if (curW + charW > maxLineW && curW > 0)
        {
            if (lineCount < maxLines - 1)
            {
                lines[lineCount].start = curLineStart;
                lines[lineCount].end = byteIdx;
                lineCount++;
                curLineStart = byteIdx;
                curW = 0;
            }
        }

        curW += charW;
        byteIdx += charBytes;
    }

    if (!cursorFound)
    {
        cursorX = curW;
        cursorLine = lineCount;
    }

    if (lineCount < maxLines)
    {
        lines[lineCount].start = curLineStart;
        lines[lineCount].end = byteIdx;
        lineCount++;
    }

    if (lineCount == 0) lineCount = 1;
    if (cursorLine >= lineCount) cursorLine = lineCount - 1;

    if (outCursorX) *outCursorX = cursorX;
    if (outCursorY) *outCursorY = cursorLine;
    return lineCount;
}


// -------------------------------------------------------------
// Slash Commands Popup
// -------------------------------------------------------------

typedef struct {
    const char* cmd;
    const char* syntax;
    const char* desc;
    const char* insertText;
    bool requiresArgs;
} SlashCmd;

static const SlashCmd g_SlashCmds[] = {
    {"/key",    "/key <provider> <key>", "配置 API Key (DeepSeek/OpenAI)", "/key ",   true},
    {"/model",  "/model <name>",         "切换大模型 (如 deepseek-chat)",  "/model ", true},
    {"/status", "/status",               "查看当前配置与 Bridge 状态",     "/status", false},
    {"/undo",   "/undo",                 "撤销上次 AI 修改的代码",         "/undo",   false},
    {"/clear",  "/clear",                "清空对话历史",                   "/clear",  false},
    {"/help",   "/help",                 "使用说明帮助",                   "/help",   false}
};

#define SLASH_CMD_COUNT ((s32)(sizeof(g_SlashCmds) / sizeof(g_SlashCmds[0])))

static void updateSlashPopup(AiEditor* ai)
{
    ai->matchCount = 0;
    if (ai->input[0] == '/' && !strchr(ai->input, ' '))
    {
        s32 inLen = (s32)strlen(ai->input);
        for (s32 i = 0; i < SLASH_CMD_COUNT; i++)
        {
            if (strncmp(g_SlashCmds[i].cmd, ai->input, inLen) == 0)
            {
                ai->matchIndices[ai->matchCount++] = i;
            }
        }
        if (ai->matchCount > 0)
        {
            ai->popupActive = true;
            if (ai->popupIndex >= ai->matchCount)
                ai->popupIndex = 0;
            return;
        }
    }
    ai->popupActive = false;
    ai->popupIndex = 0;
}

static void selectSlashCommand(AiEditor* ai, s32 matchIdx)
{
    if (matchIdx < 0 || matchIdx >= ai->matchCount) return;
    s32 cmdIdx = ai->matchIndices[matchIdx];
    const SlashCmd* sc = &g_SlashCmds[cmdIdx];

    if (!sc->requiresArgs)
    {
        strncpy(ai->input, sc->cmd, AI_INPUT_MAX - 1);
        ai->input[AI_INPUT_MAX - 1] = '\0';
        ai->inputLen = (s32)strlen(ai->input);
        ai->cursor = ai->inputLen;
        ai->popupActive = false;
        sendUserPrompt(ai);
    }
    else
    {
        strncpy(ai->input, sc->insertText, AI_INPUT_MAX - 1);
        ai->input[AI_INPUT_MAX - 1] = '\0';
        ai->inputLen = (s32)strlen(ai->input);
        ai->cursor = ai->inputLen;
        ai->popupActive = false;
    }
}

static void drawSlashPopup(AiEditor* ai)
{
    if (!ai->popupActive || ai->matchCount <= 0) return;

    s32 count = ai->matchCount;
    s32 itemH = 48;
    s32 menuW = 1080;
    s32 headerH = 44;
    s32 menuH = headerH + count * itemH + 8;
    s32 menuX = CHAT_X_LEFT;
    s32 inputTop = ai->inputY > 0 ? ai->inputY : INPUT_Y;
    s32 menuY = inputTop - menuH - 8;

    u32 colBlack     = getAiPaletteColor(ai, tic_color_black);
    u32 colBlue      = getAiPaletteColor(ai, tic_color_blue);
    u32 colDarkBlue  = getAiPaletteColor(ai, tic_color_dark_blue);
    u32 colYellow    = getAiPaletteColor(ai, tic_color_yellow);
    u32 colLightBlue = getAiPaletteColor(ai, tic_color_light_blue);
    u32 colWhite     = getAiPaletteColor(ai, tic_color_white);
    u32 colLightGrey = getAiPaletteColor(ai, tic_color_light_grey);

    // Background & border
    hiresRect(ai, menuX, menuY, menuW, menuH, colBlack);
    hiresRectBorder(ai, menuX, menuY, menuW, menuH, colBlue);

    // Header bar
    hiresRect(ai, menuX + 1, menuY + 1, menuW - 2, headerH, colDarkBlue);
    drawTextUTF8(ai, "指令菜单 (↑↓选择, Tab/Enter确认, Esc关闭, 支持鼠标点击)", menuX + 16, menuY + 2, colYellow, menuY, menuY + headerH);

    // Mouse coordinates in 1920x1080
    s32 mx = ai->mouseX;
    s32 my = ai->mouseY;
    if (mx < 0 && ai->tic)
    {
        mx = ai->tic->ram->input.mouse.x * 1920 / 256;
        my = ai->tic->ram->input.mouse.y * 1080 / 144;
    }
    bool mclick = ai->tic ? (ai->tic->ram->input.mouse.left && !ai->prevMouseLeft) : false;

    for (s32 i = 0; i < count; i++)
    {
        s32 cmdIdx = ai->matchIndices[i];
        const SlashCmd* sc = &g_SlashCmds[cmdIdx];
        s32 iy = menuY + headerH + 2 + i * itemH;

        bool hovered = (mx >= menuX && mx < menuX + menuW && my >= iy && my < iy + itemH);
        if (hovered)
        {
            ai->popupIndex = i;
            if (mclick)
            {
                selectSlashCommand(ai, i);
                return;
            }
        }

        if (i == ai->popupIndex)
        {
            hiresRect(ai, menuX + 1, iy, menuW - 2, itemH, colDarkBlue);
            hiresRectBorder(ai, menuX + 1, iy, menuW - 2, itemH, colBlue);
            drawTextUTF8(ai, ">", menuX + 10, iy + 2, colYellow, iy, iy + itemH);
            drawTextUTF8(ai, sc->syntax, menuX + 32, iy + 2, colYellow, iy, iy + itemH);
            drawTextUTF8(ai, sc->desc, menuX + 420, iy + 2, colWhite, iy, iy + itemH);
        }
        else
        {
            drawTextUTF8(ai, sc->syntax, menuX + 32, iy + 2, colLightBlue, iy, iy + itemH);
            drawTextUTF8(ai, sc->desc, menuX + 420, iy + 2, colLightGrey, iy, iy + itemH);
        }
    }
}

// -------------------------------------------------------------
// Message & HTTP Backend Integration
// -------------------------------------------------------------

static void addMessage(AiEditor* ai, AiMsgType type, const char* text)
{
    AiMessage* msg;

    if (ai->messageCount >= AI_MAX_MESSAGES)
    {
        s32 i;
        for (i = 0; i < AI_MAX_MESSAGES - 1; i++)
            ai->messages[i] = ai->messages[i + 1];
        ai->messageCount = AI_MAX_MESSAGES - 1;
    }

    msg = &ai->messages[ai->messageCount++];
    msg->type = type;
    strncpy(msg->text, text, sizeof(msg->text) - 1);
    msg->text[sizeof(msg->text) - 1] = '\0';

    ai->scroll = 99999;
}

static void freeActiveRequest(AiEditor* ai)
{
#if defined(USE_NAETT)
    if (ai->activeReq)
    {
        naettRes* res = (naettRes*)ai->activeReq;
        naettReq* req = naettGetRequest(res);
        naettClose(res);
        naettFree(req);
        ai->activeReq = NULL;
    }
#endif
}

static void hotReloadCart(AiEditor* ai, const char* newCode)
{
    Studio* studio = ai->studio;
    tic_mem* tic = getMemory(studio);
    const char* cartPath = studio_get_cart_path(studio);
    const char* cartName = studio_get_cart_name(studio);

    if (!cartName || !cartName[0])
        cartName = "temp_game.lua";

    bool loaded = false;

    if (cartPath && cartPath[0])
    {
        FILE* f = fopen(cartPath, "rb");
        if (f)
        {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* buf = (char*)malloc(sz + 1);
            if (buf && fread(buf, 1, sz, f) == (size_t)sz)
            {
                buf[sz] = '\0';
                loaded = tic_project_load(cartName, buf, (s32)sz, &tic->cart);
            }
            if (buf) free(buf);
            fclose(f);
        }
    }

    if (!loaded && newCode && strlen(newCode) > 0)
    {
        loaded = tic_project_load(cartName, newCode, (s32)strlen(newCode), &tic->cart);
        if (!loaded)
        {
            s32 len = (s32)strlen(newCode);
            if (len > TIC_CODE_SIZE - 1) len = TIC_CODE_SIZE - 1;
            memcpy(tic->cart.code.data, newCode, len);
            tic->cart.code.data[len] = '\0';
        }
    }

    studio_update_code(studio);
    addMessage(ai, AI_MSG_STATUS, "[OpenTIC] 卡带已成功同步并热重载到内存！");
}

static void unescapeJsonString(char* dst, const char* src, s32 maxLen)
{
    s32 d = 0;
    while (*src && d < maxLen - 1)
    {
        if (*src == '\\' && *(src + 1) == 'n')
        {
            dst[d++] = '\n';
            src += 2;
        }
        else if (*src == '\\' && *(src + 1) == '"')
        {
            dst[d++] = '"';
            src += 2;
        }
        else if (*src == '\\' && *(src + 1) == '\\')
        {
            dst[d++] = '\\';
            src += 2;
        }
        else if (*src == '\\' && *(src + 1) == 't')
        {
            dst[d++] = ' ';
            src += 2;
        }
        else if (*src == '"')
        {
            break;
        }
        else
        {
            dst[d++] = *src++;
        }
    }
    dst[d] = '\0';
}

static void pollStatus(AiEditor* ai)
{
#if defined(USE_NAETT)
    naettReq* req = naettRequest("http://127.0.0.1:8080/status",
        naettMethod("GET"),
        naettTimeout(5000));
    if (req)
    {
        ai->activeReq = (void*)naettMake(req);
        ai->pollingStatus = true;
    }
#endif
}

static void pasteFromClipboard(AiEditor* ai)
{
    if (tic_sys_clipboard_has())
    {
        char* clip = tic_sys_clipboard_get();
        if (clip)
        {
            s32 len = (s32)strlen(clip);
            char clean[AI_INPUT_MAX];
            s32 dst = 0;

            for (s32 i = 0; i < len && dst < AI_INPUT_MAX - 1; i++)
            {
                if (clip[i] == '\r') continue;
                if (clip[i] == '\n')
                {
                    clean[dst++] = ' ';
                    continue;
                }
                clean[dst++] = clip[i];
            }
            clean[dst] = '\0';

            if (dst > 0)
            {
                ai_handle_text_input(ai, clean);
            }

            tic_sys_clipboard_free(clip);
        }
    }
}

static void copyToClipboard(AiEditor* ai)
{
    if (ai->inputLen > 0)
    {
        tic_sys_clipboard_set(ai->input);
    }
    else if (ai->messageCount > 0)
    {
        tic_sys_clipboard_set(ai->messages[ai->messageCount - 1].text);
    }
}

static void cutToClipboard(AiEditor* ai)
{
    if (ai->inputLen > 0)
    {
        tic_sys_clipboard_set(ai->input);
        ai->input[0] = '\0';
        ai->inputLen = 0;
        ai->cursor = 0;
    }
}

static void sendUserPrompt(AiEditor* ai)
{
#if defined(USE_NAETT)
    char json[4096];
    char escapedPrompt[2048];
    char* ep;
    char* ip;
    naettReq* req;
#endif

    if (ai->inputLen <= 0 || ai->isThinking) return;

    if (strcmp(ai->input, "/clear") == 0)
    {
        ai->messageCount = 0;
        ai->input[0] = '\0';
        ai->inputLen = 0;
        ai->cursor = 0;
        ai->popupActive = false;
        addMessage(ai, AI_MSG_AGENT, "已清空对话记录。Ready for your prompt!");
        return;
    }
    if (strcmp(ai->input, "/help") == 0)
    {
        addMessage(ai, AI_MSG_USER, ai->input);
        addMessage(ai, AI_MSG_AGENT,
            "【OpenTIC AI 助手 指令帮助】\n"
            "/key <provider> <key> : 配置 API Key (如 deepseek, openai)\n"
            "/key <key>            : 快速设置默认 Key (sk-xxxx)\n"
            "/model <name>         : 切换大模型 (如 deepseek-chat)\n"
            "/status               : 查看当前模型与 Bridge 状态\n"
            "/undo                 : 撤销上次 AI 修改的代码\n"
            "/clear                : 清空对话历史\n"
            "直接输入自然语言即可让 AI 实时修改当前游戏代码！");
        ai->input[0] = '\0';
        ai->inputLen = 0;
        ai->cursor = 0;
        ai->popupActive = false;
        return;
    }

    addMessage(ai, AI_MSG_USER, ai->input);
    ai->popupActive = false;

#if defined(USE_NAETT)
    studio_sync_cart_to_disk(ai->studio);

    const char* cartPath = studio_get_cart_path(ai->studio);
    if (!cartPath || !cartPath[0])
        cartPath = "temp_game.lua";

    char normPath[TICNAME_MAX * 2];
    {
        const char* s = cartPath;
        char* d = normPath;
        while (*s && (d - normPath < (s32)sizeof(normPath) - 1))
        {
            if (*s == '\\') *d++ = '/';
            else *d++ = *s;
            s++;
        }
        *d = '\0';
    }

    memset(escapedPrompt, 0, sizeof(escapedPrompt));
    ep = escapedPrompt;
    for (ip = ai->input; *ip && (ep - escapedPrompt < 2000); ip++)
    {
        if (*ip == '"' || *ip == '\\') *ep++ = '\\';
        if (*ip == '\n') { *ep++ = '\\'; *ep++ = 'n'; continue; }
        if (*ip == '\r') continue;
        *ep++ = *ip;
    }
    *ep = '\0';

    snprintf(json, sizeof(json),
        "{\"prompt\":\"%s\",\"file_path\":\"%s\",\"cart_code\":\"\"}",
        escapedPrompt, normPath);

    req = naettRequest("http://127.0.0.1:8080/chat",
        naettMethod("POST"),
        naettHeader("Content-Type", "application/json"),
        naettBody(json, (int)strlen(json)),
        naettTimeout(10000));

    if (req)
    {
        ai->activeReq = (void*)naettMake(req);
        ai->isThinking = true;
        ai->pollingStatus = false;
        ai->pollCooldown = 0;
        ai->thinkTicks = 0;
    }
    else
    {
        addMessage(ai, AI_MSG_ERROR, "无法创建网络请求。请检查网络。");
    }
#else
    addMessage(ai, AI_MSG_ERROR, "Error: Network support (USE_NAETT) is not enabled.");
#endif

    ai->input[0] = '\0';
    ai->inputLen = 0;
    ai->cursor = 0;
}

#define AI_KEY_HOLD 30
#define AI_KEY_PERIOD 5

static inline bool aiKeyWasPressed(tic_mem* tic, tic_key key)
{
    if (!tic) return false;
    return tic_api_keyp(tic, key, AI_KEY_HOLD, AI_KEY_PERIOD);
}

static inline bool keyWasPressedOnce(tic_mem* tic, s32 key)
{
    if (!tic) return false;
    return tic_api_keyp(tic, key, -1, -1);
}

static inline bool aiEnterWasPressed(tic_mem* tic)
{
    return keyWasPressedOnce(tic, tic_key_return) ||
           keyWasPressedOnce(tic, tic_key_numpadenter);
}

// -------------------------------------------------------------
// Toolbar Drawing & Interaction (2x resolution)
// -------------------------------------------------------------

static void drawToolbarHires(AiEditor* ai)
{
    if (!ai || !ai->studio) return;
    s32 mx = ai->mouseX;
    s32 my = ai->mouseY;
    if (mx < 0 && ai->tic)
    {
        mx = ai->tic->ram->input.mouse.x * 1920 / 256;
        my = ai->tic->ram->input.mouse.y * 1080 / 144;
    }
    bool mclick = ai->tic ? (ai->tic->ram->input.mouse.left && !ai->prevMouseLeft) : false;
    studio_hires_draw_toolbar(ai->studio, TIC_AI_MODE, mx, my, mclick);
}

// -------------------------------------------------------------
// Mouse Cursor Drawing (unified 1080p cursor)
// -------------------------------------------------------------

static void drawAiCursor(AiEditor* ai)
{
    if (!ai || !ai->studio) return;
    s32 mx = ai->mouseX;
    s32 my = ai->mouseY;
    if (mx < 0 && ai->tic)
    {
        mx = ai->tic->ram->input.mouse.x * 1920 / 256;
        my = ai->tic->ram->input.mouse.y * 1080 / 144;
    }
    studio_hires_draw_cursor(ai->studio, mx, my);
}



// -------------------------------------------------------------
// Editor Tick & Render
// -------------------------------------------------------------

static void tick(AiEditor* ai)
{
    tic_mem* tic;
    Studio* studio;
    s32 totalHeight;
    s32 curY;
    s32 i;

    ai->tickCounter++;
    tic = ai->tic;
    studio = ai->studio;

    // High-res screen from studio
    if (!ai->hiresScreen && ai->studio)
    {
        ai->hiresScreen = studio_hires_get_screen(ai->studio);
    }

    s32 mx = ai->mouseX;
    s32 my = ai->mouseY;
    if (mx < 0 && tic)
    {
        mx = tic->ram->input.mouse.x * 1920 / 256;
        my = tic->ram->input.mouse.y * 1080 / 144;
    }
    bool ldown = tic ? (tic->ram->input.mouse.left != 0) : false;
    bool rdown = tic ? (tic->ram->input.mouse.right != 0) : false;
    bool lclick = ldown && !ai->prevMouseLeft;
    bool rclick = rdown && !ai->prevMouseRight;

    s32 curInY = ai->inputY > 0 ? ai->inputY : INPUT_Y;
    s32 curInH = ai->inputHeight > 0 ? ai->inputHeight : INPUT_H;

    // Hover cursor over input bar
    if (mx >= AI_OFFSET_LEFT && mx < AI_OFFSET_LEFT + AI_VIEW_WIDTH && my >= curInY && my < curInY + curInH)
    {
        if (studio) setCursor(studio, tic_cursor_ibeam);
    }

    // 1. Process clipboard events
    if (studio)
    {
        switch (getClipboardEvent(studio))
        {
        case TIC_CLIPBOARD_COPY:
            copyToClipboard(ai);
            break;
        case TIC_CLIPBOARD_PASTE:
            pasteFromClipboard(ai);
            break;
        case TIC_CLIPBOARD_CUT:
            cutToClipboard(ai);
            break;
        default:
            break;
        }
    }

    // Left-click on input bar to place cursor
    if (lclick && mx >= AI_OFFSET_LEFT && mx < AI_OFFSET_LEFT + AI_VIEW_WIDTH && my >= curInY && my < curInY + curInH)
    {
        ai->cursor = ai->inputLen;
    }

    // Right-click on input bar to paste
    if (rclick && mx >= AI_OFFSET_LEFT && mx < AI_OFFSET_LEFT + AI_VIEW_WIDTH && my >= curInY && my < curInY + curInH)
    {
        pasteFromClipboard(ai);
    }

    // Ctrl+A or Ctrl+U to clear input line
    if (tic)
    {
        bool ctrl = tic_api_key(tic, tic_key_ctrl);
        if (ctrl && (keyWasPressedOnce(tic, tic_key_a) || keyWasPressedOnce(tic, tic_key_u)))
        {
            ai->input[0] = '\0';
            ai->inputLen = 0;
            ai->cursor = 0;
            ai->popupActive = false;
        }
    }

    // 2. Keyboard handling and popup management
    updateSlashPopup(ai);

    if (tic)
    {
        if (ai->popupActive)
        {
            if (aiKeyWasPressed(tic, tic_key_up))
            {
                if (ai->popupIndex > 0) ai->popupIndex--;
                else ai->popupIndex = ai->matchCount - 1;
            }
            else if (aiKeyWasPressed(tic, tic_key_down))
            {
                if (ai->popupIndex < ai->matchCount - 1) ai->popupIndex++;
                else ai->popupIndex = 0;
            }
            else if (keyWasPressedOnce(tic, tic_key_tab) || aiEnterWasPressed(tic))
            {
                selectSlashCommand(ai, ai->popupIndex);
            }
            else if (keyWasPressedOnce(tic, tic_key_escape))
            {
                ai->popupActive = false;
            }
            else if (aiKeyWasPressed(tic, tic_key_backspace))
            {
                if (ai->cursor > 0)
                {
                    s32 del = 1;
                    while (ai->cursor - del > 0 && ((unsigned char)ai->input[ai->cursor - del] & 0xC0) == 0x80)
                    {
                        del++;
                    }
                    memmove(&ai->input[ai->cursor - del], &ai->input[ai->cursor], ai->inputLen - ai->cursor + 1);
                    ai->cursor -= del;
                    ai->inputLen -= del;
                    updateSlashPopup(ai);
                }
            }
            else if (aiKeyWasPressed(tic, tic_key_left))
            {
                if (ai->cursor > 0) ai->cursor--;
            }
            else if (aiKeyWasPressed(tic, tic_key_right))
            {
                if (ai->cursor < ai->inputLen) ai->cursor++;
            }
        }
        else
        {
            if (aiEnterWasPressed(tic))
            {
                bool isShiftOrCtrl = tic_api_key(tic, tic_key_shift) || tic_api_key(tic, tic_key_ctrl);
                if (isShiftOrCtrl)
                {
                    if (ai->inputLen < AI_INPUT_MAX - 2)
                    {
                        memmove(&ai->input[ai->cursor + 1], &ai->input[ai->cursor], ai->inputLen - ai->cursor + 1);
                        ai->input[ai->cursor] = '\n';
                        ai->cursor++;
                        ai->inputLen++;
                        ai->input[ai->inputLen] = '\0';
                        updateSlashPopup(ai);
                    }
                }
                else
                {
                    sendUserPrompt(ai);
                }
            }
            else if (aiKeyWasPressed(tic, tic_key_backspace))
            {
                if (ai->cursor > 0)
                {
                    s32 del = 1;
                    while (ai->cursor - del > 0 && ((unsigned char)ai->input[ai->cursor - del] & 0xC0) == 0x80)
                    {
                        del++;
                    }
                    memmove(&ai->input[ai->cursor - del], &ai->input[ai->cursor], ai->inputLen - ai->cursor + 1);
                    ai->cursor -= del;
                    ai->inputLen -= del;
                    updateSlashPopup(ai);
                }
            }
            else if (aiKeyWasPressed(tic, tic_key_left))
            {
                if (ai->cursor > 0) ai->cursor--;
            }
            else if (aiKeyWasPressed(tic, tic_key_right))
            {
                if (ai->cursor < ai->inputLen) ai->cursor++;
            }
            else if (aiKeyWasPressed(tic, tic_key_up))
            {
                ai->scroll -= AI_LINE_HEIGHT;
            }
            else if (aiKeyWasPressed(tic, tic_key_down))
            {
                ai->scroll += AI_LINE_HEIGHT;
            }
        }
    }

#if defined(USE_NAETT)
    if (ai->activeReq)
    {
        naettRes* res = (naettRes*)ai->activeReq;
        if (naettComplete(res))
        {
            s32 status = naettGetStatus(res);
            s32 bodyLen = 0;
            const char* body = (const char*)naettGetBody(res, &bodyLen);

            if (status == 200 && body && bodyLen > 0)
            {
                if (!ai->pollingStatus)
                {
                    freeActiveRequest(ai);
                    ai->pollingStatus = true;
                    ai->pollCooldown = 6;
                }
                else
                {
                    if (strstr(body, "\"status\":\"done\""))
                    {
                        bool codeUpdated = (strstr(body, "\"code_updated\":true") != NULL);
                        const char* msgStart = strstr(body, "\"message\":\"");
                        char msgBuf[1024] = "已为您修改完成。";
                        if (msgStart)
                        {
                            unescapeJsonString(msgBuf, msgStart + 11, sizeof(msgBuf));
                        }

                        if (codeUpdated)
                        {
                            hotReloadCart(ai, NULL);
                        }

                        addMessage(ai, AI_MSG_AGENT, msgBuf);
                        freeActiveRequest(ai);
                        ai->isThinking = false;
                        ai->pollingStatus = false;
                        ai->thinkTicks = 0;
                    }
                    else if (strstr(body, "\"status\":\"error\""))
                    {
                        const char* msgStart = strstr(body, "\"message\":\"");
                        char errBuf[1024] = "Agent 执行遇到错误。";
                        if (msgStart)
                        {
                            unescapeJsonString(errBuf, msgStart + 11, sizeof(errBuf));
                        }
                        addMessage(ai, AI_MSG_ERROR, errBuf);
                        freeActiveRequest(ai);
                        ai->isThinking = false;
                        ai->pollingStatus = false;
                        ai->thinkTicks = 0;
                    }
                    else
                    {
                        freeActiveRequest(ai);
                        ai->pollCooldown = 15;
                    }
                }
            }
            else
            {
                freeActiveRequest(ai);
                ai->isThinking = false;
                ai->pollingStatus = false;
                ai->thinkTicks = 0;
                addMessage(ai, AI_MSG_ERROR, "无法连接 Bridge 服务 (http://127.0.0.1:8080)。已尝试自动启动 Bridge。");
#if defined(_WIN32)
                if (studio) startBridgeService(studio);
#endif
            }
        }
    }
    else if (ai->isThinking && ai->pollingStatus)
    {
        if (ai->pollCooldown > 0)
        {
            ai->pollCooldown--;
        }
        else
        {
            pollStatus(ai);
        }
    }
#endif

    if (ai->isThinking)
    {
        ai->thinkTicks++;
        if (ai->thinkTicks > 60 * 3600)
        {
            addMessage(ai, AI_MSG_ERROR, "请求超时 (60分钟)，已停止等待。请检查模型或网络配置。");
            freeActiveRequest(ai);
            ai->isThinking = false;
            ai->pollingStatus = false;
            ai->thinkTicks = 0;
        }
    }
    else
    {
        ai->thinkTicks = 0;
    }

    // 3. Render frame
    u32 colBlack     = getAiPaletteColor(ai, tic_color_black);
    u32 colDarkGrey  = getAiPaletteColor(ai, tic_color_dark_grey);
    u32 colGrey      = getAiPaletteColor(ai, tic_color_grey);
    u32 colWhite     = getAiPaletteColor(ai, tic_color_white);
    u32 colYellow    = getAiPaletteColor(ai, tic_color_yellow);
    u32 colLightBlue = getAiPaletteColor(ai, tic_color_light_blue);
    u32 colCyan      = getAiPaletteColor(ai, tic_color_cyan);
    u32 colPurple    = getAiPaletteColor(ai, tic_color_purple);
    u32 colGreen     = getAiPaletteColor(ai, tic_color_light_green);
    u32 colRed       = getAiPaletteColor(ai, tic_color_red);

    // Fill entire 512x288 buffer with border color
    hiresRect(ai, 0, 0, AI_FULL_WIDTH, AI_FULL_HEIGHT, colBlack);

    // Draw active area background
    hiresRect(ai, AI_OFFSET_LEFT, AI_OFFSET_TOP, AI_VIEW_WIDTH, AI_VIEW_HEIGHT, colBlack);

    // Draw top toolbar in high-res
    drawToolbarHires(ai);

    // Compute multi-line input layout and auto-expanding input height
    InputLineInfo inputLinesInfo[8];
    s32 cursorColX = 0;
    s32 cursorLineIdx = 0;
    s32 maxInputW = CHAT_X_RIGHT - CHAT_X_LEFT - 64;
    s32 numInputLines = computeInputLayout(ai, inputLinesInfo, 5, &cursorColX, &cursorLineIdx, maxInputW);
    ai->inputLines = numInputLines;
    ai->inputHeight = 76 + (numInputLines - 1) * AI_LINE_HEIGHT;
    ai->inputY = AI_FULL_HEIGHT - ai->inputHeight - 28;

    s32 chatBottom = ai->inputY - 16;
    s32 chatViewH = chatBottom - CHAT_TOP;

    // Calculate total height of messages
    totalHeight = 6;
    for (i = 0; i < ai->messageCount; i++)
    {
        AiMessage* msg = &ai->messages[i];
        s32 prefixW = (msg->type == AI_MSG_USER) ? measureTextWidth(ai, "[YOU]: ") : (msg->type == AI_MSG_AGENT ? measureTextWidth(ai, "[OpenTIC]: ") : 0);
        s32 firstX = CHAT_X_LEFT + prefixW;
        s32 wrapX = (msg->type == AI_MSG_USER || msg->type == AI_MSG_AGENT) ? (CHAT_X_LEFT + 24) : CHAT_X_LEFT;
        totalHeight += measureTextHeightEx(ai, msg->text, firstX, wrapX) + 6;
    }
    if (ai->isThinking)
    {
        totalHeight += AI_LINE_HEIGHT + 6;
    }

    ai->maxScroll = totalHeight > chatViewH ? totalHeight - chatViewH : 0;
    if (ai->scroll > ai->maxScroll) ai->scroll = ai->maxScroll;
    if (ai->scroll < 0) ai->scroll = 0;

    // Mouse wheel scrolling
    if (tic && tic->ram->input.mouse.scrolly != 0)
    {
        ai->scroll -= tic->ram->input.mouse.scrolly * AI_LINE_HEIGHT * 2;
        if (ai->scroll < 0) ai->scroll = 0;
        if (ai->scroll > ai->maxScroll) ai->scroll = ai->maxScroll;
    }

    // Render messages inside chat viewport
    curY = CHAT_TOP + 4 - ai->scroll;
    for (i = 0; i < ai->messageCount; i++)
    {
        AiMessage* msg = &ai->messages[i];
        s32 prefixW = (msg->type == AI_MSG_USER) ? measureTextWidth(ai, "[YOU]: ") : (msg->type == AI_MSG_AGENT ? measureTextWidth(ai, "[OpenTIC]: ") : 0);
        s32 firstX = CHAT_X_LEFT + prefixW;
        s32 wrapX = (msg->type == AI_MSG_USER || msg->type == AI_MSG_AGENT) ? (CHAT_X_LEFT + 24) : CHAT_X_LEFT;
        s32 msgH = measureTextHeightEx(ai, msg->text, firstX, wrapX);

        if (curY + msgH >= CHAT_TOP && curY < chatBottom)
        {
            // Right-click to copy message
            if (rclick && mx >= CHAT_X_LEFT && mx < CHAT_X_RIGHT && my >= curY && my < curY + msgH)
            {
                tic_sys_clipboard_set(msg->text);
            }

            if (msg->type == AI_MSG_USER)
            {
                drawTextUTF8(ai, "[YOU]:", CHAT_X_LEFT, curY, colLightBlue, CHAT_TOP, chatBottom);
                drawTextUTF8Ex(ai, msg->text, firstX, wrapX, curY, colCyan, CHAT_TOP, chatBottom);
            }
            else if (msg->type == AI_MSG_STATUS)
            {
                drawTextUTF8(ai, msg->text, CHAT_X_LEFT, curY, colGreen, CHAT_TOP, chatBottom);
            }
            else if (msg->type == AI_MSG_ERROR)
            {
                drawTextUTF8(ai, msg->text, CHAT_X_LEFT, curY, colRed, CHAT_TOP, chatBottom);
            }
            else
            {
                drawTextUTF8(ai, "[OpenTIC]:", CHAT_X_LEFT, curY, colPurple, CHAT_TOP, chatBottom);
                drawTextUTF8Ex(ai, msg->text, firstX, wrapX, curY, colWhite, CHAT_TOP, chatBottom);
            }
        }
        curY += msgH + 6;
    }

    if (ai->isThinking)
    {
        static const char* Dots[] = {"thinking .", "thinking ..", "thinking ...", "thinking ...."};
        const char* dot = Dots[(ai->tickCounter / 15) % 4];
        drawTextUTF8(ai, dot, CHAT_X_LEFT, curY, colYellow, CHAT_TOP, chatBottom);
    }

    // Bottom auto-expanding input bar
    s32 barW = CHAT_X_RIGHT - CHAT_X_LEFT;
    hiresRect(ai, CHAT_X_LEFT, ai->inputY, barW, ai->inputHeight, 0xff1e1e24);
    hiresRect(ai, CHAT_X_LEFT, ai->inputY, barW, 1, colGrey);
    hiresRectBorder(ai, CHAT_X_LEFT, ai->inputY, barW, ai->inputHeight, 0xff353748);

    drawTextUTF8(ai, ">", CHAT_X_LEFT + 20, ai->inputY + 12, colYellow, ai->inputY, ai->inputY + ai->inputHeight);
    if (ai->inputLen > 0 || ai->compositionLen > 0)
    {
        for (s32 li = 0; li < numInputLines; li++)
        {
            s32 lStart = inputLinesInfo[li].start;
            s32 lEnd = inputLinesInfo[li].end;
            s32 lineLen = lEnd - lStart;
            if (lineLen > 0)
            {
                char lineBuf[AI_INPUT_MAX];
                if (lineLen >= AI_INPUT_MAX) lineLen = AI_INPUT_MAX - 1;
                memcpy(lineBuf, &ai->input[lStart], lineLen);
                lineBuf[lineLen] = '\0';
                drawTextUTF8(ai, lineBuf, CHAT_X_LEFT + 56, ai->inputY + 12 + li * AI_LINE_HEIGHT, colWhite, ai->inputY, ai->inputY + ai->inputHeight);
            }
        }
    }
    else
    {
        drawTextUTF8(ai, "输入指令让 OpenTIC 修改代码或素材 (输入 / 唤起菜单)...", CHAT_X_LEFT + 56, ai->inputY + 12, colGrey, ai->inputY, ai->inputY + ai->inputHeight);
    }

    // Multi-line cursor & IME composition
    s32 curCursorX = CHAT_X_LEFT + 56 + cursorColX;
    s32 curCursorY = ai->inputY + 12 + cursorLineIdx * AI_LINE_HEIGHT;

    // Draw active composition text (Pinyin candidate preview with underline)
    if (ai->compositionLen > 0)
    {
        s32 compW = drawTextUTF8(ai, ai->composition, curCursorX, curCursorY, colYellow, ai->inputY, ai->inputY + ai->inputHeight);
        hiresRect(ai, curCursorX, curCursorY + 40, compW > 0 ? compW : 22, 3, colYellow);
        curCursorX += compW;
    }

    // Blinking cursor
    if ((ai->tickCounter / 20) % 2 == 0)
    {
        if (curCursorX < CHAT_X_RIGHT)
        {
            hiresRect(ai, curCursorX, curCursorY + 2, 4, AI_LINE_HEIGHT - 6, colWhite);
        }
    }

    // Draw slash popup above input bar (drawn last so it floats over chat)
    drawSlashPopup(ai);

    // Draw retro pixel cursor
    drawAiCursor(ai);

    ai->prevMouseLeft = ldown;
    ai->prevMouseRight = rdown;
}

void ai_handle_text_input(AiEditor* ai, const char* text)
{
    s32 addLen;
    if (!text || !*text) return;

    // Committed text clears composition
    ai->composition[0] = '\0';
    ai->compositionLen = 0;
    ai->compositionCursor = 0;

    addLen = (s32)strlen(text);
    if (ai->inputLen + addLen < AI_INPUT_MAX - 1)
    {
        memmove(&ai->input[ai->cursor + addLen], &ai->input[ai->cursor], ai->inputLen - ai->cursor + 1);
        memcpy(&ai->input[ai->cursor], text, addLen);
        ai->cursor += addLen;
        ai->inputLen += addLen;
        ai->input[ai->inputLen] = '\0';
        updateSlashPopup(ai);
    }
}

void ai_handle_text_editing(AiEditor* ai, const char* text, s32 start, s32 length)
{
    if (!ai) return;
    if (text && *text)
    {
        strncpy(ai->composition, text, sizeof(ai->composition) - 1);
        ai->composition[sizeof(ai->composition) - 1] = '\0';
        ai->compositionLen = (s32)strlen(ai->composition);
        ai->compositionCursor = start;
    }
    else
    {
        ai->composition[0] = '\0';
        ai->compositionLen = 0;
        ai->compositionCursor = 0;
    }
}

void ai_get_input_rect(AiEditor* ai, s32* x, s32* y, s32* w, s32* h)
{
    if (!ai) return;
    if (x) *x = CHAT_X_LEFT + 44;
    if (y) *y = ai->inputY > 0 ? (ai->inputY + 10) : (AI_FULL_HEIGHT - 64);
    if (w) *w = CHAT_X_RIGHT - CHAT_X_LEFT - 64;
    if (h) *h = ai->inputHeight > 0 ? ai->inputHeight : 54;
}

static void event(AiEditor* ai, StudioEvent ev)
{
    switch (ev)
    {
    case TIC_TOOLBAR_PASTE:
        pasteFromClipboard(ai);
        break;
    case TIC_TOOLBAR_COPY:
        copyToClipboard(ai);
        break;
    case TIC_TOOLBAR_CUT:
        cutToClipboard(ai);
        break;
    case TIC_TOOLBAR_UNDO:
        strncpy(ai->input, "/undo", sizeof(ai->input) - 1);
        ai->inputLen = (s32)strlen(ai->input);
        ai->cursor = ai->inputLen;
        sendUserPrompt(ai);
        break;
    default:
        break;
    }
}

bool studio_ai_has_hires(AiEditor* ai)
{
    return (ai && ai->hiresScreen != NULL);
}

const u32* studio_ai_get_screen(AiEditor* ai, s32* w, s32* h)
{
    if (ai && ai->hiresScreen)
    {
        if (w) *w = AI_FULL_WIDTH;
        if (h) *h = AI_FULL_HEIGHT;
        return ai->hiresScreen;
    }
    return NULL;
}

void initAi(AiEditor* ai, Studio* studio)
{
    ai->studio = studio;
    ai->tic = studio ? getMemory(studio) : NULL;
    ai->tick = tick;
    ai->event = event;

    ai->inputLines = 1;
    ai->inputHeight = 76;
    ai->inputY = AI_FULL_HEIGHT - 76 - 28;
    ai->composition[0] = '\0';
    ai->compositionLen = 0;
    ai->compositionCursor = 0;

    ai->hiresScreen = studio ? studio_hires_get_screen(studio) : NULL;

    if (!ai->fontLoaded)
    {
        loadFont(ai);
    }

#if defined(_WIN32)
    if (studio) startBridgeService(studio);
#endif

    if (ai->messageCount == 0)
    {
        addMessage(ai, AI_MSG_AGENT, "你好！我是 OpenTIC AI 助手。支持代码与素材（图块、精灵、地图、调色板）实时生成与修改。输入 / 可唤起指令菜单。");
    }
}

void freeAi(AiEditor* ai)
{
    if (ai->fontData)
    {
        free(ai->fontData);
        ai->fontData = NULL;
    }
    if (ai->fontInfo)
    {
        free(ai->fontInfo);
        ai->fontInfo = NULL;
    }
    ai->hiresScreen = NULL;
    freeActiveRequest(ai);
}
