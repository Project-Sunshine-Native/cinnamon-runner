#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "random.h"
#include "renderer.h"
#include "runner.h"
#include "text_utils.h"
#include "vm_builtins.h"

#ifdef PLATFORM_3DS
#include "3ds/3ds_renderer.h"
#endif

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#define WRITER_SELF_VARS(X) \
    X(formatted) X(special) X(mystring) X(button1) X(button2) X(button3) X(automash_timer) \
    X(skipme) X(skippable) X(pos) X(length) X(halt) X(siner) X(writingx) X(writingy) \
    X(wx) X(wy) X(wxskip) X(hspace) X(vspace) X(mycolor) X(myfont) X(shake) X(textscale) \
    X(colorchange) X(xcolor) X(accept) X(n) X(mychar) X(nextchar) X(nextchar2) \
    X(dialoguer) X(f) X(charline) X(originalcharline) X(specfade)

#define WRITER_GLOBAL_VARS(X) X(flag) X(debug) X(input_held) X(lang) X(fe) X(fc)

static struct {
#define DECLARE_ID(name) int32_t name;
    WRITER_SELF_VARS(DECLARE_ID)
    WRITER_GLOBAL_VARS(DECLARE_ID)
#undef DECLARE_ID
    NativeCallBinding confirm;
} cache;

enum {
    WRITER_GLYPH_CAPACITY = 256,
    WRITER_GLOW_PASSES = 9,
    WRITER_FLAG_OWNER = 0x38714,
    WRITER_WHITE = 0xFFFFFF,
    WRITER_DARK_GRAY = 0x404040,
    WRITER_NAVY = 0x800000,
};

typedef struct {
    uint32_t ink;
    bool tinted;
} WriterOutline;

static RendererGlyph writerGlyphs[WRITER_GLOW_PASSES * WRITER_GLYPH_CAPACITY];
static WriterOutline writerOutlines[WRITER_GLYPH_CAPACITY];

typedef struct {
    const char* bytes;
    uint16_t codepoint;
    uint8_t length;
} WriterChar;

typedef struct {
    GMLReal x, y;
    RValue halt, charline, xcolor;
    WriterChar current, command, argument;
    int32_t n, glyphCount, emotion, portrait, skippable, musicFlag;
    uint32_t drawColor;
    bool accept, drewCharacter, colorChange, haltChanged, charlineChanged, xcolorChanged;
} WriterFrame;

#define WRITER_GET(instance, name) Instance_getSelfVar((instance), cache.name)
#define WRITER_NUMBER(instance, name) RValue_toReal(WRITER_GET(instance, name))
#define WRITER_SET(instance, name, value) Instance_setSelfVar((instance), cache.name, RValue_makeReal((GMLReal)(value)))

static bool writerEqual(RValue value, GMLReal number, GMLReal epsilon) {
    return value.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(value), number, epsilon) == 0;
}

static WriterChar writerReadChar(const char* text, int32_t length, int32_t* offset) {
    int32_t start = *offset;
    WriterChar result = { .bytes = text + start };
    if (start < length) {
        result.codepoint = TextUtils_decodeUtf8(text, length, offset);
        result.length = (uint8_t)(*offset - start);
    }
    return result;
}

static void writerSaveChar(Instance* self, int32_t var, WriterChar character) {
    RValue previous = Instance_getSelfVar(self, var);
    if (previous.type == RVALUE_STRING && previous.string != nullptr &&
        strncmp(previous.string, character.bytes, character.length) == 0 &&
        previous.string[character.length] == '\0') return;

    char text[5];
    memcpy(text, character.bytes, character.length);
    text[character.length] = '\0';
    Instance_setSelfVar(self, var, RValue_makeString(text));
}

static int32_t writerPortrait(uint16_t ch) {
    switch (ch) {
        case '0': return 0;
        case 'S': return 1;
        case 'R': return 2;
        case 'N': return 3;
        case 'T': return 4;
        case 'L': return 5;
        case 'A': return 10;
        case 'a': return 11;
        case 'B': return 12;
        case 'r': return 15;
        case 'u': return 18;
        case 'K': return 20;
        default: return -1;
    }
}

static bool writerSetColor(uint16_t ch, RValue defaultColor, RValue* color) {
    switch (ch) {
        case 'R': *color = RValue_makeReal(0x0000FF); return true;
        case 'B': *color = RValue_makeReal(0xFF0000); return true;
        case 'Y': *color = RValue_makeReal(0x00FFFF); return true;
        case 'G': *color = RValue_makeReal(0x00FF00); return true;
        case 'W': *color = RValue_makeReal(0xFFFFFF); return true;
        case 'X': *color = RValue_makeReal(0); return true;
        case '0': *color = defaultColor; return true;
        default: return false;
    }
}

static int32_t writerEnglishSpacing(uint16_t ch) {
    switch (ch) {
        case 'w': return 2;
        case 'm': return 3;
        case 'i': case 'l': return -2;
        case 's': case 'j': return -1;
        default: return 0;
    }
}

static GMLReal writerShake(Random* random, GMLReal shake) {
    return ((GMLReal)Random_nextUInt32(random) / (GMLReal)BS_RAND_MAX) * shake;
}

static void writerAppendGlow(WriterFrame* frame, uint16_t ch, uint32_t color) {
    static const int8_t offsets[WRITER_GLOW_PASSES][2] = {
        {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {-1, 1}, {1, -1},
    };
    RendererGlyph* glyphs = &writerGlyphs[WRITER_GLOW_PASSES * frame->glyphCount++];
    for (int32_t i = 0; i < WRITER_GLOW_PASSES; ++i) {
        glyphs[i] = (RendererGlyph){ ch, (float)(frame->x + offsets[i][0]),
            (float)(frame->y + offsets[i][1]), color };
    }
}

static bool writerLayout(VMContext* ctx, const char* text, int32_t length, int32_t stop,
    int32_t font, RValue defaultColor, int32_t style, WriterFrame* frame, Random* random) {
    Instance* self = ctx->currentCallFrame->self;
    Instance* global = ctx->globalScopeInstance;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    GMLReal writingX = frame->x;
    GMLReal hspace = WRITER_NUMBER(self, hspace);
    GMLReal vspace = WRITER_NUMBER(self, vspace);
    GMLReal shake = WRITER_NUMBER(self, shake);
    bool wxskip = writerEqual(WRITER_GET(self, wxskip), 1, epsilon);
    bool dialoguer = writerEqual(WRITER_GET(self, dialoguer), 1, epsilon);
    RValue lang = WRITER_GET(global, lang);
    bool japanese = lang.type == RVALUE_STRING && lang.string != nullptr && strcmp(lang.string, "ja") == 0;
    bool englishSpacing = font == 11 && lang.type == RVALUE_STRING &&
        lang.string != nullptr && strcmp(lang.string, "en") == 0;

    int32_t offset = 0;
    uint16_t previous = 0;
    int32_t n = 1;
    for (; n < stop; ++n) {
        uint16_t before = previous;
        frame->current = writerReadChar(text, length, &offset);
        uint16_t ch = frame->current.codepoint;
        if (ch == 0xFFFD) return false;
        previous = ch;
        frame->accept = false;

        switch (ch) {
            case '&':
                frame->x = wxskip ? writingX + 58 : writingX;
                frame->y += vspace;
                continue;
            case '|':
                frame->x += hspace;
                continue;
            case '^':
                previous = writerReadChar(text, length, &offset).codepoint;
                if (previous == 0xFFFD) return false;
                ++n;
                continue;
            case '/':
                frame->halt = RValue_makeReal(text[offset] == '%' ? 2.0 : 1.0);
                frame->haltChanged = true;
                continue;
            case '%':
                if (before == '/') {
                    frame->halt = RValue_makeReal(2.0);
                    frame->haltChanged = true;
                }
                if (text[offset] == '%' || !writerEqual(frame->halt, 2, epsilon)) return false;
                continue;
            case '\\': {
                frame->command = writerReadChar(text, length, &offset);
                frame->argument = writerReadChar(text, length, &offset);
                uint16_t arg = frame->argument.codepoint;
                if (frame->command.codepoint == 0xFFFD || arg == 0xFFFD) return false;
                previous = arg;
                n += 2;
                switch (frame->command.codepoint) {
                    case 'E':
                        if (arg >= '0' && arg <= '9') frame->emotion = arg - '0';
                        else if (arg >= 'A' && arg <= 'E') frame->emotion = arg - 'A' + 10;
                        break;
                    case 'F': {
                        int32_t portrait = writerPortrait(arg);
                        if (portrait >= 0) frame->portrait = portrait;
                        if (dialoguer) {
                            bool noPortrait = frame->portrait >= 0 ? frame->portrait == 0
                                : writerEqual(WRITER_GET(global, fc), 0, epsilon);
                            frame->charline = noPortrait ? WRITER_GET(self, originalcharline) : RValue_makeReal(26.0);
                            frame->charlineChanged = true;
                            frame->x = noPortrait ? self->x : self->x + (GMLReal)58 * WRITER_NUMBER(self, f);
                        }
                        break;
                    }
                    case 'c':
                        frame->colorChange = true;
                        if (writerSetColor(arg, defaultColor, &frame->xcolor)) frame->xcolorChanged = true;
                        break;
                    case 's':
                        if (arg == '0' || arg == '1') frame->skippable = arg - '0';
                        break;
                    case 'M':
                        if (arg >= '0' && arg <= '9') frame->musicFlag = arg - '0';
                        break;
                    case 'f': case '*': case 'T': case 'C': case 'S': case 'I':
                        return false;
                }
                continue;
            }
        }

        frame->accept = true;
        frame->drewCharacter = true;
        if (frame->colorChange) frame->drawColor = RValue_toColour(frame->xcolor);
        bool drawable = ch != 0 && ch != '#' && ch != '\r' && ch != '\n';
        int32_t index = frame->glyphCount;
        uint32_t color = frame->drawColor;
        if (style == 1) {
            GMLReal shadowY = (frame->y + writerShake(random, shake)) + 1;
            GMLReal shadowX = (frame->x + writerShake(random, shake)) + 1;
            if (drawable) {
                bool tinted = color != WRITER_WHITE && color != 0;
                uint32_t ink = tinted ? RValue_toColour(frame->xcolor) : WRITER_NAVY;
                writerOutlines[index] = (WriterOutline){ink, tinted};
                writerGlyphs[2 * index] = (RendererGlyph){
                    ch, (float)shadowX, (float)shadowY, tinted ? ink : WRITER_DARK_GRAY,
                };
                index = 2 * index + 1;
                if (tinted) color = WRITER_WHITE;
            }
        }
        if (style == 2) {
            if (drawable) writerAppendGlow(frame, ch, color);
        } else {
            GMLReal y = frame->y + writerShake(random, shake);
            GMLReal x = frame->x + writerShake(random, shake);
            if (drawable) {
                writerGlyphs[index] = (RendererGlyph){ch, (float)x, (float)y, color};
                ++frame->glyphCount;
            }
        }
        frame->x += hspace;
        if (japanese && (ch < 256 || (ch >= 65377 && ch <= 65439))) {
            frame->x -= hspace / (GMLReal)2;
        } else if (englishSpacing) {
            frame->x += writerEnglishSpacing(ch);
        }
    }
    frame->n = n;
    return true;
}

static void writerDrawGlow(Renderer* renderer, int32_t count, GMLReal siner, GMLReal fade) {
    GMLReal wave = GMLReal_sin(siner / 14);
    float alpha[] = {
        (float)fade,
        (float)(((GMLReal)0.3 + wave * (GMLReal)0.1) * fade),
        (float)(((GMLReal)0.08 + wave * (GMLReal)0.04) * fade),
    };
#ifdef PLATFORM_3DS
    if (renderer->vtable->drawGlyphRun != nullptr && renderer->drawHalign == 0 && renderer->drawValign == 0) {
        C3D_drawGlyphGlowRun(renderer, writerGlyphs, count, alpha);
        return;
    }
#endif
    for (int32_t i = 0; i < count; ++i) {
        const RendererGlyph* glyph = &writerGlyphs[WRITER_GLOW_PASSES * i];
        char text[5];
        int32_t length = TextUtils_utf8EncodeCodepoint(glyph->character, text);
        text[length] = '\0';
        renderer->drawColor = glyph->color;
        for (int32_t pass = 0; pass < WRITER_GLOW_PASSES; ++pass) {
            renderer->drawAlpha = alpha[pass == 0 ? 0 : pass <= 4 ? 1 : 2];
            renderer->vtable->drawText(renderer, text, glyph[pass].x, glyph[pass].y, 1.0f, 1.0f, 0.0f, -1.0f);
        }
    }
}

static void writerDrawGlyphs(Renderer* renderer, int32_t count, float scale) {
    if (renderer->vtable->drawGlyphRun != nullptr && renderer->drawHalign == 0 && renderer->drawValign == 0) {
        if (count > 0) renderer->vtable->drawGlyphRun(renderer, writerGlyphs, count, scale, scale);
        return;
    }
    for (int32_t i = 0; i < count; ++i) {
        const RendererGlyph* glyph = &writerGlyphs[i];
        char text[5];
        int32_t length = TextUtils_utf8EncodeCodepoint(glyph->character, text);
        text[length] = '\0';
        renderer->drawColor = glyph->color;
        renderer->vtable->drawText(renderer, text, glyph->x, glyph->y, scale, scale, 0.0f, -1.0f);
    }
}

static void writerDrawOutlined(Renderer* renderer, int32_t count) {
#ifdef PLATFORM_3DS
    if (renderer->vtable->drawGlyphRun != nullptr && renderer->drawHalign == 0 && renderer->drawValign == 0) {
        float alpha = renderer->drawAlpha;
        for (int32_t i = 0; i < count;) {
            const RendererGlyph* shadow = &writerGlyphs[2 * i];
            bool tinted = writerOutlines[i].tinted;
            if (!tinted && alpha == 1.0f) {
                int32_t end = i + 1;
                while (end < count && !writerOutlines[end].tinted) ++end;
                renderer->drawAlpha = 1.0f;
                renderer->vtable->drawGlyphRun(renderer, shadow, 2 * (end - i), 1.0f, 1.0f);
                i = end;
                continue;
            }
            renderer->drawAlpha = tinted ? 0.3f : 1.0f;
            renderer->vtable->drawGlyphRun(renderer, shadow, 1, 1.0f, 1.0f);
            renderer->drawAlpha = tinted ? 1.0f : alpha;
            renderer->vtable->drawGlyphRun(renderer, shadow + 1, 1, 1.0f, 1.0f);
            ++i;
        }
        renderer->drawAlpha = alpha;
        return;
    }
#endif
    for (int32_t i = 0; i < count; ++i) {
        const RendererGlyph* shadow = &writerGlyphs[2 * i];
        const RendererGlyph* glyph = shadow + 1;
        WriterOutline outline = writerOutlines[i];
        char text[5];
        int32_t length = TextUtils_utf8EncodeCodepoint(glyph->character, text);
        text[length] = '\0';
        renderer->vtable->drawTextColor(renderer, text, shadow->x, shadow->y, 1.0f, 1.0f, 0.0f,
            shadow->color, shadow->color, outline.ink, outline.ink, outline.tinted ? 0.3f : 1.0f, -1.0f);
        if (outline.tinted) {
            renderer->vtable->drawTextColor(renderer, text, glyph->x, glyph->y, 1.0f, 1.0f, 0.0f,
                glyph->color, glyph->color, outline.ink, outline.ink, 1.0f, -1.0f);
        } else {
            renderer->drawColor = glyph->color;
            renderer->vtable->drawText(renderer, text, glyph->x, glyph->y, 1.0f, 1.0f, 0.0f, -1.0f);
        }
    }
}

static bool writerDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Instance* global = ctx->globalScopeInstance;
    Runner* runner = ctx->runner;
    Renderer* renderer = runner->renderer;
    if (self == nullptr || renderer == nullptr || renderer->vtable->drawText == nullptr) return false;

    GMLReal epsilon = runner->mathEpsilon;
    if (!writerEqual(WRITER_GET(self, formatted), 1, epsilon)) return false;
    RValue special = WRITER_GET(self, special);
    if (special.type != RVALUE_REAL && special.type != RVALUE_INT32) return false;
    GMLReal style = RValue_toReal(special);
    if (style != 0 && style != 1 && style != 2) return false;
    bool outlined = style == 1;
    if (outlined && renderer->vtable->drawTextColor == nullptr) return false;
    RValue source = WRITER_GET(self, mystring);
    if (source.type != RVALUE_STRING || source.string == nullptr) return false;

    RValue held = WRITER_GET(global, input_held);
    GMLArray* heldButtons = held.type == RVALUE_ARRAY ? held.array : nullptr;
    bool button2 = RValue_toBool(GMLArray_get(heldButtons, 5)) || RValue_toBool(GMLArray_get(heldButtons, 8));
    int32_t automash = -1;
    if (RValue_toBool(GMLArray_get(heldButtons, 6)) || RValue_toBool(GMLArray_get(heldButtons, 9))) {
        RValue flag = WRITER_GET(global, flag);
        GMLArray* flags = flag.type == RVALUE_ARRAY ? flag.array : nullptr;
        if (writerEqual(GMLArray_get(flags, 10), 1, epsilon) || RValue_toBool(WRITER_GET(global, debug))) {
            automash = writerEqual(WRITER_GET(self, automash_timer), 0, epsilon) ? 1 : 0;
            if (automash == 0) button2 = true;
        }
    }

    RValue halt = WRITER_GET(self, halt);
    GMLReal position = WRITER_NUMBER(self, pos);
    bool skip = writerEqual(WRITER_GET(self, skipme), 1, epsilon);
    if (button2 && writerEqual(halt, 0, epsilon) && writerEqual(WRITER_GET(self, skippable), 1, epsilon)) {
        RValue length = WRITER_GET(self, length);
        if (length.type != RVALUE_UNDEFINED &&
            NativeScript_compareReals(position, RValue_toReal(length), epsilon) < 0) skip = true;
    }
    const char* text = source.string;
    int32_t length = (int32_t)strlen(text);
    if (skip) position = (GMLReal)(TextUtils_utf8CodepointCount(text, length) + 1);
    if (!(position >= 0 && position <= WRITER_GLYPH_CAPACITY + 1)) return false;
    int32_t stop = (int32_t)position;
    if (NativeScript_compareReals((GMLReal)stop, position, epsilon) < 0) ++stop;

    int32_t font = RValue_toInt32(WRITER_GET(self, myfont));
    RValue defaultColor = WRITER_GET(self, mycolor);
    WriterFrame frame = {
        .x = WRITER_NUMBER(self, writingx), .y = WRITER_NUMBER(self, writingy),
        .halt = halt, .xcolor = WRITER_GET(self, xcolor), .emotion = -1,
        .portrait = -1, .skippable = -1, .musicFlag = -1, .drawColor = RValue_toColour(defaultColor),
    };
    Random random = runner->random;
    if (!writerLayout(ctx, text, length, stop, font, defaultColor, (int32_t)style, &frame, &random)) return false;

    RValue confirm = NativeScript_call(ctx, &cache.confirm, "gml_Script_button1_p", nullptr, 0);
    bool button1 = automash == 0 || writerEqual(confirm, 1, epsilon);
    RValue_free(&confirm);
    GMLReal siner = WRITER_NUMBER(self, siner);
    if (button1 && NativeScript_compareReals(siner, 0, epsilon) > 0 &&
        (writerEqual(frame.halt, 1, epsilon) || writerEqual(frame.halt, 2, epsilon))) return false;

    WRITER_SET(self, button1, button1);
    WRITER_SET(self, button2, button2);
    WRITER_SET(self, button3, 0);
    if (automash >= 0) WRITER_SET(self, automash_timer, automash);
    if (skip) {
        Instance_setSelfVar(self, cache.pos, RValue_makeInt32((int32_t)position));
        RValue off = RValue_makeReal(-1.0);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_ALARM, "alarm", off, 0);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_ALARM, "alarm", off, 1);
    }
    if (frame.current.bytes != nullptr) writerSaveChar(self, cache.mychar, frame.current);
    if (frame.command.bytes != nullptr) {
        writerSaveChar(self, cache.nextchar, frame.command);
        writerSaveChar(self, cache.nextchar2, frame.argument);
    }
    if (frame.emotion >= 0) WRITER_SET(global, fe, frame.emotion);
    if (frame.portrait >= 0) WRITER_SET(global, fc, frame.portrait);
    if (frame.musicFlag >= 0) {
        RValue* flags = IntRValueHashMap_getOrInsertUndefined(&global->selfVars, cache.flag);
        ctx->currentArrayOwner = (void*)(intptr_t)WRITER_FLAG_OWNER;
        VM_setArrayElement(ctx, flags, 20, RValue_makeInt32(frame.musicFlag));
    }
    if (frame.skippable >= 0) WRITER_SET(self, skippable, frame.skippable);
    if (frame.haltChanged) Instance_setSelfVar(self, cache.halt, frame.halt);
    if (frame.charlineChanged) Instance_setSelfVar(self, cache.charline, frame.charline);
    if (frame.xcolorChanged) Instance_setSelfVar(self, cache.xcolor, frame.xcolor);
    WRITER_SET(self, colorchange, frame.colorChange);
    WRITER_SET(self, accept, frame.accept);
    WRITER_SET(self, wx, frame.x);
    WRITER_SET(self, wy, frame.y);
    WRITER_SET(self, n, frame.n);
    WRITER_SET(self, skipme, 0);
    WRITER_SET(self, siner, siner + 1);
    runner->random = random;

    renderer->drawFont = font;
    if (outlined) {
        writerDrawOutlined(renderer, frame.glyphCount);
    } else if (style == 2) {
        if (frame.drewCharacter) {
            writerDrawGlow(renderer, frame.glyphCount, siner, WRITER_NUMBER(self, specfade));
            renderer->drawAlpha = 1.0f;
        }
    } else {
        writerDrawGlyphs(renderer, frame.glyphCount, (float)WRITER_NUMBER(self, textscale));
    }
    renderer->drawColor = frame.drawColor;
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeWriterDraw_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 1") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    cache.confirm = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (!NativeScript_canCall(ctx, &cache.confirm, "gml_Script_button1_p") ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_writer_Draw_0", writerDraw)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
#define RESOLVE_GLOBAL(name) cache.name = VM_getOrAllocateVarID(ctx, #name);
    WRITER_SELF_VARS(RESOLVE_SELF)
    WRITER_GLOBAL_VARS(RESOLVE_GLOBAL)
#undef RESOLVE_GLOBAL
#undef RESOLVE_SELF
}
