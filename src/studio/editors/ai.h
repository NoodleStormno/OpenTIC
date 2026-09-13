// MIT License
// Copyright (c) 2026 Vadim Grigoruk @nesbox / OpenTIC Contributors

#pragma once

#include "studio/studio.h"

typedef struct AiEditor AiEditor;

typedef enum
{
    AI_MSG_USER,
    AI_MSG_AGENT,
    AI_MSG_STATUS,
    AI_MSG_ERROR
} AiMsgType;

typedef struct
{
    AiMsgType type;
    char text[1024];
} AiMessage;

#define AI_MAX_MESSAGES 64
#define AI_INPUT_MAX 1024

struct AiEditor
{
    Studio* studio;
    tic_mem* tic;

    s32 mouseX;
    s32 mouseY;
    bool prevMouseLeft;
    bool prevMouseRight;

    AiMessage messages[AI_MAX_MESSAGES];
    s32 messageCount;

    char input[AI_INPUT_MAX];
    s32 inputLen;
    s32 cursor;

    s32 scroll;
    s32 maxScroll;

    bool isThinking;
    s32 tickCounter;

    void* activeReq; // naettRes*
    bool pollingStatus;
    s32 thinkTicks;
    s32 pollCooldown;

    // Font rendering via stb_truetype
    bool fontLoaded;
    u8* fontData;
    void* fontInfo; // stbtt_fontinfo*
    float fontScale;
    float fontAscent;

    // Slash command popup
    bool popupActive;
    s32 popupIndex;
    s32 matchIndices[16];
    s32 matchCount;

    u32* hiresScreen;

    // Input box dynamic multi-line & wrapping
    s32 inputLines;
    s32 inputHeight;
    s32 inputY;

    // IME composition support
    char composition[256];
    s32 compositionCursor;
    s32 compositionLen;

    void (*tick)(AiEditor*);
    void (*event)(AiEditor*, StudioEvent);
};

void initAi(AiEditor* ai, Studio* studio);
void freeAi(AiEditor* ai);
void ai_handle_text_input(AiEditor* ai, const char* text);
void ai_handle_text_editing(AiEditor* ai, const char* text, s32 start, s32 length);
void ai_get_input_rect(AiEditor* ai, s32* x, s32* y, s32* w, s32* h);
bool studio_ai_has_hires(AiEditor* ai);
const u32* studio_ai_get_screen(AiEditor* ai, s32* w, s32* h);
