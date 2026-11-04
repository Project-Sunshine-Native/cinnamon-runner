// manually optimized native implementation of gml_Object_obj_base_writer_Draw_0.


#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "random.h"
#include "renderer.h"
#include "runner.h"
#include "text_utils.h"

#include "stb_ds.h"

#include <string.h>

typedef struct {
    const VMContext* owner;

    int32_t writingX;
    int32_t writingY;
    int32_t writingXEnd;
    int32_t vSpacing;
    int32_t vText;
    int32_t stringPos;
    int32_t originalString;
    int32_t myColor;
    int32_t myFont;
    int32_t shake;
    int32_t spacing;
    int32_t hTextScale;
    int32_t vTextScale;
    int32_t myX;
    int32_t myY;
    int32_t controllerConnected;
    int32_t halt;
    int32_t stringNo;
    int32_t myString;
    int32_t textSpeed;

    int32_t language;
    int32_t typer;
    int32_t globalControllerConnected;
    int32_t faceEmotion;
    int32_t faceChoice;
    int32_t faceChange;
    int32_t battleActive;
    int32_t battleTopObjectsShifted;
    int32_t idealBorder;

    int32_t fontPlain;
    int32_t fontPapyrus;
    int32_t fontComicSans;
} FastWriterCache;

static FastWriterCache fastWriterCache;
static NativeCallBinding replaceButtonsCall = NATIVE_CALL_BINDING_INIT;

typedef enum {
    FAST_WRITER_TERMINAL_NONE,
    FAST_WRITER_TERMINAL_HALT,
    FAST_WRITER_TERMINAL_PAGE,
    FAST_WRITER_TERMINAL_DESTROY
} FastWriterTerminal;

typedef struct {
    FastWriterTerminal terminal;
    int32_t haltValue;
} FastWriterPlan;

#define FAST_WRITER_GLYPH_CAPACITY 512
static RendererGlyph fastWriterGlyphs[FAST_WRITER_GLYPH_CAPACITY];

static int32_t resolveAsset(Runner* runner, const char* name) {
    ptrdiff_t slot = shgeti(runner->assetsByName, (char*)name);
    return slot >= 0 ? runner->assetsByName[slot].value : -1;
}

static void resolveCache(VMContext* ctx) {
    FastWriterCache* cache = &fastWriterCache;
    if (cache->owner == ctx) return;

    cache->owner = ctx;
#define RESOLVE(field, name) cache->field = VM_getOrAllocateVarID(ctx, name)
    RESOLVE(writingX, "writingx");
    RESOLVE(writingY, "writingy");
    RESOLVE(writingXEnd, "writingxend");
    RESOLVE(vSpacing, "vspacing");
    RESOLVE(vText, "vtext");
    RESOLVE(stringPos, "stringpos");
    RESOLVE(originalString, "originalstring");
    RESOLVE(myColor, "mycolor");
    RESOLVE(myFont, "myfont");
    RESOLVE(shake, "shake");
    RESOLVE(spacing, "spacing");
    RESOLVE(hTextScale, "htextscale");
    RESOLVE(vTextScale, "vtextscale");
    RESOLVE(myX, "myx");
    RESOLVE(myY, "myy");
    RESOLVE(controllerConnected, "controller_connected");
    RESOLVE(halt, "halt");
    RESOLVE(stringNo, "stringno");
    RESOLVE(myString, "mystring");
    RESOLVE(textSpeed, "textspeed");
    RESOLVE(language, "language");
    RESOLVE(typer, "typer");
    RESOLVE(globalControllerConnected, "controller_connected");
    RESOLVE(faceEmotion, "faceemotion");
    RESOLVE(faceChoice, "facechoice");
    RESOLVE(faceChange, "facechange");
    RESOLVE(battleActive, "_cinnamon_battle_active");
    RESOLVE(battleTopObjectsShifted, "_cinnamon_battle_top_objects_shifted");
    RESOLVE(idealBorder, "idealborder");
#undef RESOLVE

    cache->fontPlain = resolveAsset(ctx->runner, "fnt_plain");
    cache->fontPapyrus = resolveAsset(ctx->runner, "fnt_papyrus");
    cache->fontComicSans = resolveAsset(ctx->runner, "fnt_comicsans");
}

static RValue getVariable(Instance* instance, int32_t id) {
    return Instance_getSelfVar(instance, id);
}

static GMLReal getNumber(Instance* instance, int32_t id) {
    return RValue_toReal(getVariable(instance, id));
}

static void setNumber(Instance* instance, int32_t id, GMLReal value) {
    Instance_setSelfVar(instance, id, RValue_makeReal(value));
}

static bool isEnglish(Instance* global, int32_t languageId) {
    RValue language = getVariable(global, languageId);
    return language.type == RVALUE_STRING && language.string != nullptr &&
        strcmp(language.string, "en") == 0;
}

static bool isSupportedColorCommand(uint16_t command) {
    switch (command) {
        case 'R': case 'G': case 'W': case 'Y': case 'X':
        case 'B': case 'O': case 'L': case 'P': case 'p':
            return true;
        default:
            return false;
    }
}

static uint32_t colorForCommand(uint16_t command, uint32_t current) {
    switch (command) {
        case 'R': return 255u;
        case 'G': return 65280u;
        case 'W': return 16777215u;
        case 'Y': return 65535u;
        case 'X': return 0u;
        case 'B': return 16711680u;
        case 'O': return 4235519u;
        case 'L': return 16629774u;
        case 'P': return 16711935u;
        case 'p': return 13941759u;
        default: return current;
    }
}

static uint16_t peekCodepoint(const char* text, int32_t byteLength, int32_t bytePos) {
    if (bytePos < 0 || bytePos >= byteLength) return 0;
    return TextUtils_decodeUtf8(text, byteLength, &bytePos);
}

static bool planRenderPrefix(const char* text, int32_t byteLength, int32_t visibleCharacters,
    FastWriterPlan* plan) {
    plan->terminal = FAST_WRITER_TERMINAL_NONE;
    plan->haltValue = 0;

    int32_t bytePos = 0;
    int32_t characterIndex = 1;
    while (characterIndex <= visibleCharacters) {
        if (bytePos >= byteLength) return false;
        uint16_t character = TextUtils_decodeUtf8(text, byteLength, &bytePos);
        if (character > 0x7f) return false;

        if (character == '^') {
            uint16_t delay = peekCodepoint(text, byteLength, bytePos);
            if (delay == 0 || delay == '0') return false;
            (void)TextUtils_decodeUtf8(text, byteLength, &bytePos);
            characterIndex += 2;
            continue;
        }
        if (character == '\\') {
            if (bytePos >= byteLength) return false;
            uint16_t command = TextUtils_decodeUtf8(text, byteLength, &bytePos);
            characterIndex += 2;
            if (isSupportedColorCommand(command)) continue;
            if (command == 'E' || command == 'F') {
                uint16_t argument = peekCodepoint(text, byteLength, bytePos);
                if (argument < '0' || argument > '9') return false;
                (void)TextUtils_decodeUtf8(text, byteLength, &bytePos);
                characterIndex++;
                continue;
            }
            return false;
        }
        if (character == '/') {
            uint16_t next = peekCodepoint(text, byteLength, bytePos);
            plan->terminal = FAST_WRITER_TERMINAL_HALT;
            plan->haltValue = 1;
            if (next == '%') {
                plan->haltValue = 2;
            } else if (next == '^') {
                int32_t nextBytePos = bytePos;
                (void)TextUtils_decodeUtf8(text, byteLength, &nextBytePos);
                uint16_t delay = peekCodepoint(text, byteLength, nextBytePos);
                if (delay != '0') plan->haltValue = 4;
            } else if (next == '*') {
                plan->haltValue = 6;
            }
            return true;
        }
        if (character == '%') {
            plan->terminal = peekCodepoint(text, byteLength, bytePos) == '%'
                ? FAST_WRITER_TERMINAL_DESTROY
                : FAST_WRITER_TERMINAL_PAGE;
            return true;
        }
        // Icons, choices, and user events remain on bytecode. Declining here happens before any
        // visible or gameplay state has changed.
        if (character == '#') return false;
        characterIndex++;
    }
    return true;
}

static void setAlarm0(Instance* instance, int32_t value) {
    instance->alarm[0] = value;
    if (value > 0)
        instance->activeAlarmMask |= 1u;
    else
        instance->activeAlarmMask &= (uint16_t)~1u;
}

static GMLReal randomOffset(VMContext* ctx, GMLReal shake) {
    GMLReal random = (GMLReal)Random_nextUInt32(&ctx->runner->random) / (GMLReal)BS_RAND_MAX;
    return random * shake - shake * (GMLReal)0.5;
}

static void applyTyper18LeadingAdjustment(uint16_t character, GMLReal* letterX) {
    switch (character) {
        case 'l': case 'i': case 'I': case '!': case '.': case '?':
            *letterX += 2;
            break;
        case 'S': case 'D': case 'A': case '\'':
            *letterX += 1;
            break;
        default:
            break;
    }
}

static void applyFontSpacingAdjustment(int32_t font, const FastWriterCache* cache,
    uint16_t character, GMLReal* letterX) {
    if (font == cache->fontComicSans) {
        switch (character) {
            case 'w': case 'm': *letterX += 2; break;
            case 'i': case 'l': *letterX -= 2; break;
            case 's': case 'j': *letterX -= 1; break;
            default: break;
        }
        return;
    }
    if (font != cache->fontPapyrus) return;
    switch (character) {
        case 'D': case 'M': case 'C': case 'A': case 'H': case 'B': case 'G':
            *letterX += 1;
            break;
        case 'Q': *letterX += 3; break;
        case 'L': case 'K': case 'T': case 'J': case 'F':
            *letterX -= 1;
            break;
        case 'P': case 'R': *letterX -= 2; break;
        case '.': case '!': case '?': *letterX -= 3; break;
        case 'O': case 'W': *letterX += 2; break;
        case 'I': case '\'': *letterX -= 6; break;
        default: break;
    }
}

static GMLReal getIdealBorderTop(Instance* global, int32_t idealBorderId) {
    RValue idealBorder = getVariable(global, idealBorderId);
    if (idealBorder.type != RVALUE_ARRAY || idealBorder.array == nullptr) return 0;
    return RValue_toReal(GMLArray_get(idealBorder.array, 2));
}

static bool NativeGenerated_gml_object_obj_base_writer_draw_0_ca87fb81_handler(
    VMContext* ctx, RValue* outResult) {
    if (ctx == nullptr || ctx->currentCallFrame == nullptr ||
        ctx->dataWin->gen8.wadVersion != 17) return false;

    Instance* self = ctx->currentCallFrame->self;
    Instance* global = ctx->globalScopeInstance;
    Runner* runner = ctx->runner;
    if (self == nullptr || global == nullptr || runner == nullptr || runner->renderer == nullptr ||
        runner->renderer->vtable->drawText == nullptr) return false;

    resolveCache(ctx);
    FastWriterCache* cache = &fastWriterCache;

    if (RValue_toBool(getVariable(global, cache->battleActive))) return false;
    if (!isEnglish(global, cache->language)) return false;
    if (RValue_toBool(getVariable(self, cache->vText))) return false;

    GMLReal shake = getNumber(self, cache->shake);
    if (shake < 0 || shake > 38) return false;

    RValue original = getVariable(self, cache->originalString);
    if (original.type != RVALUE_STRING || original.string == nullptr) return false;
    const char* text = original.string;
    int32_t byteLength = (int32_t)strlen(text);
    int32_t visibleCharacters = RValue_toInt32(getVariable(self, cache->stringPos));
    FastWriterPlan plan;
    if (visibleCharacters < 0 || visibleCharacters > FAST_WRITER_GLYPH_CAPACITY ||
        !planRenderPrefix(text, byteLength, visibleCharacters, &plan)) return false;

    int32_t nextStringNo = 0;
    RValue nextPage = RValue_makeUndefined();
    if (plan.terminal == FAST_WRITER_TERMINAL_PAGE) {
        RValue strings = getVariable(self, cache->myString);
        if (strings.type != RVALUE_ARRAY || strings.array == nullptr) return false;
        nextStringNo = RValue_toInt32(getVariable(self, cache->stringNo)) + 1;
        nextPage = GMLArray_get(strings.array, nextStringNo);
        if (nextPage.type != RVALUE_STRING || nextPage.string == nullptr ||
            !NativeScript_canCall(ctx, &replaceButtonsCall,
                "gml_Script_scr_replace_buttons_pc")) return false;
    }

    // A controller transition rebuilds the string. Let bytecode handle that one frame.
    if (getNumber(self, cache->controllerConnected) !=
        getNumber(global, cache->globalControllerConnected)) return false;

    Renderer* renderer = runner->renderer;
    if (renderer->drawHalign != 0 || renderer->drawValign != 0) return false;
    GMLReal writingX = getNumber(self, cache->writingX);
    GMLReal writingY = getNumber(self, cache->writingY);
    GMLReal writingXEnd = getNumber(self, cache->writingXEnd);
    GMLReal vSpacing = getNumber(self, cache->vSpacing);
    GMLReal x = writingX;
    GMLReal y = writingY;
    if (RValue_toBool(getVariable(global, cache->battleTopObjectsShifted)) &&
        writingY < getIdealBorderTop(global, cache->idealBorder)) {
        y += 120;
    }

    uint32_t color = RValue_toColour(getVariable(self, cache->myColor));
    int32_t font = RValue_toInt32(getVariable(self, cache->myFont));
    GMLReal spacing = getNumber(self, cache->spacing);
    GMLReal hTextScale = getNumber(self, cache->hTextScale);
    GMLReal vTextScale = getNumber(self, cache->vTextScale);
    GMLReal typer = getNumber(global, cache->typer);

    GMLCamera* camera = Runner_getCameraForView(runner, runner->viewCurrent);
    GMLReal viewWidth = camera != nullptr ? (GMLReal)camera->viewWidth : 0;
    GMLReal applicationWidth = runner->applicationWidth > 0
        ? (GMLReal)runner->applicationWidth
        : (GMLReal)ctx->dataWin->gen8.defaultWindowWidth;
    if (viewWidth == 0 || applicationWidth <= 0) return false;
    GMLReal displayScale = applicationWidth / viewWidth;

    bool filteredFont = font == cache->fontPlain || font == cache->fontPapyrus ||
        font == cache->fontComicSans;
    int32_t glyphCount = 0;

    int32_t bytePos = 0;
    int32_t characterIndex = 1;
    while (characterIndex <= visibleCharacters && bytePos < byteLength) {
        uint16_t character = TextUtils_decodeUtf8(text, byteLength, &bytePos);

        if (character == '^') {
            (void)TextUtils_decodeUtf8(text, byteLength, &bytePos);
            characterIndex += 2;
            continue;
        }
        if (character == '\\') {
            uint16_t command = TextUtils_decodeUtf8(text, byteLength, &bytePos);
            characterIndex += 2;
            if (isSupportedColorCommand(command)) {
                color = colorForCommand(command, color);
            } else {
                uint16_t argument = TextUtils_decodeUtf8(text, byteLength, &bytePos);
                characterIndex++;
                if (command == 'E') {
                    setNumber(global, cache->faceEmotion, argument - '0');
                } else {
                    setNumber(global, cache->faceChoice, argument - '0');
                    setNumber(global, cache->faceChange, 1);
                }
            }
            continue;
        }
        if (character == '&') {
            x = writingX;
            y += vSpacing;
            characterIndex++;
            continue;
        }
        if (character == '/' || character == '%') break;

        if (x > writingXEnd) {
            x = writingX;
            y += vSpacing;
        }

        GMLReal letterX = x;
        if (typer == 18) applyTyper18LeadingAdjustment(character, &letterX);

        GMLReal offsetX = randomOffset(ctx, shake);
        GMLReal offsetY = randomOffset(ctx, shake);
        GMLReal finalX = GMLReal_bankersRound((letterX + offsetX) * displayScale) / displayScale;
        GMLReal finalY = GMLReal_bankersRound((y + offsetY) * displayScale) / displayScale;

        RendererGlyph* glyph = &fastWriterGlyphs[glyphCount++];
        glyph->character = character;
        glyph->x = (float)finalX;
        glyph->y = (float)finalY;
        glyph->color = color;

        letterX += spacing;
        applyFontSpacingAdjustment(font, cache, character, &letterX);
        x = letterX;
        characterIndex++;
    }

    if (glyphCount > 0) {
        renderer->drawFont = font;
        bool previousFilter = renderer->texFilter;
        if (filteredFont && !previousFilter && renderer->vtable->gpuSetTexFilter != nullptr) {
            renderer->vtable->gpuSetTexFilter(renderer, true);
        }

        if (renderer->vtable->drawGlyphRun != nullptr) {
            renderer->vtable->drawGlyphRun(renderer, fastWriterGlyphs, glyphCount,
                (float)hTextScale, (float)vTextScale);
        } else {
            for (int32_t i = 0; i < glyphCount; ++i) {
                RendererGlyph* glyph = &fastWriterGlyphs[i];
                renderer->drawColor = glyph->color;
                if (renderer->vtable->drawGlyph != nullptr) {
                    renderer->vtable->drawGlyph(renderer, glyph->character, glyph->x, glyph->y,
                        (float)hTextScale, (float)vTextScale);
                } else {
                    char textGlyph[5];
                    int32_t glyphLength = TextUtils_utf8EncodeCodepoint(
                        glyph->character, textGlyph);
                    textGlyph[glyphLength] = '\0';
                    renderer->vtable->drawText(renderer, textGlyph, glyph->x, glyph->y,
                        (float)hTextScale, (float)vTextScale, 0.0f, -1.0f);
                }
            }
        }
        renderer->drawColor = fastWriterGlyphs[glyphCount - 1].color;

        if (filteredFont && !previousFilter && renderer->vtable->gpuSetTexFilter != nullptr) {
            renderer->vtable->gpuSetTexFilter(renderer, false);
        }
    }
    setNumber(self, cache->myColor, (GMLReal)color);

    switch (plan.terminal) {
        case FAST_WRITER_TERMINAL_HALT:
            setNumber(self, cache->myX, x);
            setNumber(self, cache->myY, y);
            setNumber(self, cache->halt, plan.haltValue);
            break;

        case FAST_WRITER_TERMINAL_PAGE: {
            setNumber(self, cache->stringNo, nextStringNo);
            RValue replaced = NativeScript_call(ctx, &replaceButtonsCall,
                "gml_Script_scr_replace_buttons_pc", &nextPage, 1);
            Instance_setSelfVar(self, cache->originalString, replaced);
            RValue_free(&replaced);
            setNumber(self, cache->stringPos, 0);
            setNumber(self, cache->myX, writingX);
            GMLReal pageY = writingY;
            if (RValue_toBool(getVariable(global, cache->battleTopObjectsShifted)) &&
                writingY < getIdealBorderTop(global, cache->idealBorder)) {
                pageY += 120;
            }
            setNumber(self, cache->myY, pageY);
            setAlarm0(self, RValue_toInt32(getVariable(self, cache->textSpeed)));
            break;
        }

        case FAST_WRITER_TERMINAL_DESTROY:
            setNumber(self, cache->myX, x);
            setNumber(self, cache->myY, y);
            Runner_destroyInstance(runner, self, true);
            break;

        case FAST_WRITER_TERMINAL_NONE:
            setNumber(self, cache->myX, x);
            setNumber(self, cache->myY, y);
            break;
    }

    *outResult = RValue_makeReal(0);
    return true;
}

void NativeGenerated_gml_object_obj_base_writer_draw_0_ca87fb81_register(VMContext* ctx) {
    fastWriterCache.owner = nullptr;
    replaceButtonsCall = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_base_writer_Draw_0",
        NativeGenerated_gml_object_obj_base_writer_draw_0_ca87fb81_handler);
}
