#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "text_utils.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#define CHARBOX_SELF_VARS(X) \
    X(c) X(havechar) X(hpcolor) X(charcolor) X(gc) X(xchunk) X(charpos) X(chartotal) \
    X(xx) X(yy) X(bp) X(bpoff) X(bcolor) X(mmy) X(btc) X(spare_glow) X(pacify_glow) \
    X(sglowi) X(b_offset)

#define CHARBOX_GLOBAL_VARS(X) \
    X(charturn) X(myfight) X(bmenucoord) X(fighting) X(monster) X(mercymod) \
    X(monsterstatus) X(tension) X(encounterno) X(time) X(charselect) X(faceaction) \
    X(hpfont) X(hp) X(maxhp) X(chemg_sprite_map)

enum {
    CHARBOX_MMY_OWNER = 0x858838,
    CHARBOX_BTC_OWNER = 0x858839,
    CHARBOX_FIGHT = 0,
    CHARBOX_ACT,
    CHARBOX_TECH,
    CHARBOX_ITEM,
    CHARBOX_SPARE,
    CHARBOX_DEFEND,
    CHARBOX_KRIS_NAME,
    CHARBOX_SUSIE_NAME,
    CHARBOX_RALSEI_NAME,
    CHARBOX_LOCAL_SPRITES,
};

static const char* const localizedSprites[CHARBOX_LOCAL_SPRITES] = {
    "spr_btfight", "spr_btact", "spr_bttech", "spr_btitem", "spr_btspare", "spr_btdefend",
    "spr_bnamekris", "spr_bnamesusie", "spr_bnameralsei",
};

static struct {
#define DECLARE_ID(name) int32_t name;
    CHARBOX_SELF_VARS(DECLARE_ID)
    CHARBOX_GLOBAL_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t heads[3], hpname, hpslash;
    BuiltinFunc mapFind;
    NativeCallBinding selectionMatrix;
} cache;

typedef struct {
    VMContext* ctx;
    Instance* self;
    Instance* global;
    Renderer* renderer;
    GMLReal x, y, bp, bpoff, epsilon;
    unsigned resolvedSprites;
    int32_t sprites[CHARBOX_LOCAL_SPRITES];
} CharboxDraw;

#define CHARBOX_GET(inst, name) Instance_getSelfVar((inst), cache.name)
#define CHARBOX_REAL(inst, name) RValue_toReal(CHARBOX_GET(inst, name))
#define CHARBOX_SET(inst, name, value) Instance_setSelfVar((inst), cache.name, RValue_makeReal((GMLReal)(value)))

static GMLArray* charboxArray(RValue value) {
    return value.type == RVALUE_ARRAY ? value.array : nullptr;
}

static bool charboxEqual(RValue a, RValue b, GMLReal epsilon) {
    if (a.type == RVALUE_UNDEFINED || b.type == RVALUE_UNDEFINED) return a.type == b.type;
    return NativeScript_compareReals(RValue_toReal(a), RValue_toReal(b), epsilon) == 0;
}

static int32_t charboxXChunk(RValue partySize, RValue position, GMLReal epsilon) {
    if (charboxEqual(partySize, RValue_makeReal(3.0), epsilon)) {
        if (charboxEqual(position, RValue_makeReal(1.0), epsilon)) return 212;
        if (charboxEqual(position, RValue_makeReal(2.0), epsilon)) return 424;
    } else if (charboxEqual(partySize, RValue_makeReal(2.0), epsilon)) {
        if (charboxEqual(position, RValue_makeReal(0.0), epsilon)) return 106;
        if (charboxEqual(position, RValue_makeReal(1.0), epsilon)) return 326;
    } else if (charboxEqual(partySize, RValue_makeReal(1.0), epsilon) &&
        charboxEqual(position, RValue_makeReal(0.0), epsilon)) {
        return 212;
    }
    return 0;
}

static int32_t charboxLocalizedSprite(CharboxDraw* draw, int32_t slot) {
    unsigned bit = 1u << slot;
    if ((draw->resolvedSprites & bit) == 0) {
        RValue args[] = {
            CHARBOX_GET(draw->global, chemg_sprite_map),
            RValue_makeString(localizedSprites[slot]),
        };
        RValue result = cache.mapFind(draw->ctx, args, 2);
        draw->sprites[slot] = RValue_toInt32(result);
        RValue_free(&result);
        draw->resolvedSprites |= bit;
    }
    return draw->sprites[slot];
}

static void charboxRectangle(Renderer* renderer, GMLReal x1, GMLReal y1, GMLReal x2, GMLReal y2) {
    renderer->vtable->drawRectangle(renderer, (float)x1, (float)y1, (float)x2, (float)y2,
        renderer->drawColor, renderer->drawAlpha, false);
}

static void charboxDrawHP(CharboxDraw* draw, RValue value, GMLReal x, GMLReal y) {
    Renderer* renderer = draw->renderer;
    if (value.type == RVALUE_INT32 || value.type == RVALUE_REAL) {
        GMLReal number = RValue_toReal(value);
        if (number >= 0 && number <= 9999 && (GMLReal)(uint32_t)number == number) {
            uint32_t hp = (uint32_t)number;
            char digits[5];
            char* start = digits + sizeof(digits) - 1;
            *start = '\0';
            do {
                *--start = (char)('0' + hp % 10);
                hp /= 10;
            } while (hp != 0);
            renderer->vtable->drawText(renderer, start, (float)x, (float)y, 1.0f, 1.0f, 0.0f, -1.0f);
            return;
        }
    }

    PreprocessedText text = value.type == RVALUE_STRING
        ? TextUtils_preprocessGmlText(value.string != nullptr ? value.string : "")
        : (PreprocessedText){ .text = RValue_toString(value, draw->ctx->dataWin), .owning = true };
    renderer->vtable->drawText(renderer, text.text, (float)x, (float)y, 1.0f, 1.0f, 0.0f, -1.0f);
    PreprocessedText_free(text);
}

static GMLReal charboxUpdateOffset(CharboxDraw* draw, int32_t character, bool selected) {
    RValue* slot = IntRValueHashMap_getOrInsertUndefined(&draw->self->selfVars, cache.mmy);
    RValue previous = GMLArray_get(charboxArray(*slot), character);
    GMLReal offset = RValue_toReal(previous);
    GMLReal epsilon = draw->epsilon;
    if (selected) {
        if (previous.type == RVALUE_UNDEFINED) return offset;
        bool moved = NativeScript_compareReals(offset, -32, epsilon) > 0;
        if (moved) {
            draw->ctx->currentArrayOwner = (void*)(intptr_t)CHARBOX_MMY_OWNER;
            offset -= 2;
        }
        if (NativeScript_compareReals(offset, -24, epsilon) > 0) offset -= 4;
        if (NativeScript_compareReals(offset, -16, epsilon) > 0) offset -= 6;
        if (NativeScript_compareReals(offset, -8, epsilon) > 0) offset -= 8;
        if (NativeScript_compareReals(offset, -32, epsilon) < 0) {
            offset = -64;
        } else if (!moved) {
            return offset;
        }
    } else {
        draw->ctx->currentArrayOwner = (void*)(intptr_t)CHARBOX_MMY_OWNER;
        offset = previous.type != RVALUE_UNDEFINED && NativeScript_compareReals(offset, -14, epsilon) < 0
            ? offset + 15 : 0;
    }
    VM_setArrayElement(draw->ctx, slot, character, RValue_makeReal(offset));
    return offset;
}

static int32_t charboxUpdateButtons(CharboxDraw* draw, bool selected, RValue turn) {
    int32_t button = -1;
    if (selected) {
        GMLArray* rows = charboxArray(CHARBOX_GET(draw->global, bmenucoord));
        GMLArray* row = charboxArray(GMLArray_get(rows, 0));
        button = RValue_toInt32(GMLArray_get(row, RValue_toInt32(turn)));
    }

    draw->ctx->currentArrayOwner = (void*)(intptr_t)CHARBOX_BTC_OWNER;
    RValue* slot = IntRValueHashMap_getOrInsertUndefined(&draw->self->selfVars, cache.btc);
    VM_setArrayElement(draw->ctx, slot, 4, RValue_makeReal(button == 4));
    for (int32_t i = 0; i < 4; ++i) {
        RValue_copyIntoSlot(GMLArray_slot(slot->array, i), RValue_makeReal(button == i));
    }
    if (selected && (uint32_t)button >= 5) {
        VM_setArrayElement(draw->ctx, slot, button, RValue_makeReal(1.0));
    }
    return button;
}

static void charboxFindGlow(CharboxDraw* draw, bool* spare, bool* pacify) {
    Instance* global = draw->global;
    GMLReal epsilon = draw->epsilon;
    GMLArray* monsters = charboxArray(CHARBOX_GET(global, monster));
    GMLArray* mercy = charboxArray(CHARBOX_GET(global, mercymod));
    GMLArray* status = charboxArray(CHARBOX_GET(global, monsterstatus));
    RValue tension = CHARBOX_GET(global, tension);
    bool canPacify = tension.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(tension), 40, epsilon) >= 0 &&
        !charboxEqual(CHARBOX_GET(global, encounterno), RValue_makeReal(31.0), epsilon);

    for (int32_t i = 0; i < 3; ++i) {
        if (!charboxEqual(GMLArray_get(monsters, i), RValue_makeReal(1.0), epsilon)) continue;
        RValue amount = GMLArray_get(mercy, i);
        if (amount.type != RVALUE_UNDEFINED &&
            NativeScript_compareReals(RValue_toReal(amount), 100, epsilon) >= 0) *spare = true;
        if (canPacify && charboxEqual(GMLArray_get(status, i), RValue_makeReal(1.0), epsilon)) *pacify = true;
    }
}

static void charboxDrawButtons(CharboxDraw* draw, int32_t character, int32_t button, GMLReal left,
    bool spare, bool pacify) {
    Renderer* renderer = draw->renderer;
    int32_t action = character == 0 ? CHARBOX_ACT : CHARBOX_TECH;
    const int32_t sprites[5] = {CHARBOX_FIGHT, action, CHARBOX_ITEM, CHARBOX_SPARE, CHARBOX_DEFEND};
    float y = (float)((485 - draw->bp) + draw->y);
    for (int32_t i = 0; i < 5; ++i) {
        int32_t sprite = charboxLocalizedSprite(draw, sprites[i]);
        Renderer_drawSprite(renderer, sprite, button == i, (float)(left + (15 + i * 35)), y);
    }

    if (spare || pacify) {
        GMLReal time = CHARBOX_REAL(draw->global, time);
        float alpha = (float)((GMLReal)0.4 + GMLReal_sin(time / (GMLReal)6) * (GMLReal)0.4);
        if (spare) {
            Renderer_drawSpriteExt(renderer, draw->sprites[CHARBOX_SPARE], 2,
                (float)(left + 120), y, 1.0f, 1.0f, 0.0f, 0xFFFFFFu, alpha);
        }
        if (pacify) {
            Renderer_drawSpriteExt(renderer, draw->sprites[CHARBOX_TECH], 2,
                (float)(left + 50), y, 1.0f, 1.0f, 0.0f, 0xFFFFFFu, alpha);
        }
    }
}

static void charboxDrawPanel(CharboxDraw* draw, int32_t character, RValue position, int32_t chunk,
    GMLReal offset, GMLReal bottom, uint32_t color, bool selected) {
    Renderer* renderer = draw->renderer;
    Instance* global = draw->global;
    GMLReal left = draw->x + chunk;
    GMLReal top = 480 - draw->bp;
    GMLReal lower = draw->bpoff + bottom;
    GMLReal epsilon = draw->epsilon;

    RValue select = CHARBOX_GET(global, charselect);
    renderer->drawColor = selected || charboxEqual(select, position, epsilon) ||
        charboxEqual(select, RValue_makeReal(3.0), epsilon) ? color : RValue_toColour(CHARBOX_GET(draw->self, bcolor));
    charboxRectangle(renderer, left, ((top - 2) + draw->y) + offset, left + 212, top + draw->y);
    renderer->drawColor = 0;
    GMLReal innerTop = (top + draw->y) + offset;
    charboxRectangle(renderer, left + 2, innerTop, left + 210, innerTop + 33);
    CHARBOX_SET(draw->self, b_offset, bottom);

    GMLArray* faces = charboxArray(CHARBOX_GET(global, faceaction));
    int32_t frame = RValue_toInt32(GMLArray_get(faces, RValue_toInt32(position)));
    if (frame < 0) frame = (int32_t)draw->self->imageIndex;
    Renderer_drawSprite(renderer, cache.heads[character], frame, (float)((draw->x + 13) + chunk), (float)(lower + offset));
    int32_t name = charboxLocalizedSprite(draw, CHARBOX_KRIS_NAME + character);
    Renderer_drawSprite(renderer, name, 0, (float)((draw->x + 51) + chunk), (float)((lower + 3) + offset));
    Renderer_drawSprite(renderer, cache.hpname, 0, (float)((draw->x + 109) + chunk), (float)((lower + 11) + offset));

    renderer->drawColor = 0xFFFFFFu;
    renderer->drawFont = RValue_toInt32(CHARBOX_GET(global, hpfont));
    RValue current = GMLArray_get(charboxArray(CHARBOX_GET(global, hp)), character + 1);
    RValue maximum = GMLArray_get(charboxArray(CHARBOX_GET(global, maxhp)), character + 1);
    GMLReal hp = RValue_toReal(current);
    GMLReal max = RValue_toReal(maximum);
    GMLReal fraction = hp / max;
    if (NativeScript_compareReals(fraction, (GMLReal)0.25, epsilon) <= 0) renderer->drawColor = 0x00FFFFu;
    if (current.type != RVALUE_UNDEFINED && NativeScript_compareReals(hp, 0, epsilon) <= 0) renderer->drawColor = 0x0000FFu;

    renderer->drawHalign = 2;
    GMLReal textY = (lower - 2) + offset;
    charboxDrawHP(draw, current, (draw->x + 160) + chunk, textY);
    Renderer_drawSprite(renderer, cache.hpslash, 0, (float)((draw->x + 159) + chunk), (float)((lower - 4) + offset));
    charboxDrawHP(draw, maximum, (draw->x + 205) + chunk, textY);
    renderer->drawHalign = 0;
    renderer->drawColor = 0x000080u;
    GMLReal barY = (lower + 11) + offset;
    GMLReal barBottom = (lower + 19) + offset;
    charboxRectangle(renderer, (draw->x + 128) + chunk, barY, (draw->x + 203) + chunk, barBottom);
    if (current.type != RVALUE_UNDEFINED && maximum.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(hp, 0, epsilon) > 0 && NativeScript_compareReals(max, 0, epsilon) > 0) {
        renderer->drawColor = color;
        GMLReal fill = GMLReal_ceil(fraction * (GMLReal)75);
        charboxRectangle(renderer, (draw->x + 128) + chunk, barY, (left + 128) + fill, barBottom);
    }
}

static bool charboxDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    if (self == nullptr || runner->renderer == nullptr) return false;
    Instance* global = ctx->globalScopeInstance;
    GMLReal epsilon = runner->mathEpsilon;
    CharboxDraw draw = {
        .ctx = ctx, .self = self, .global = global, .renderer = runner->renderer,
        .x = CHARBOX_REAL(self, xx), .y = CHARBOX_REAL(self, yy),
        .bp = CHARBOX_REAL(self, bp), .bpoff = CHARBOX_REAL(self, bpoff), .epsilon = epsilon,
    };
    GMLArray* present = charboxArray(CHARBOX_GET(self, havechar));
    GMLArray* positions = charboxArray(CHARBOX_GET(self, charpos));
    GMLArray* colors = charboxArray(CHARBOX_GET(self, hpcolor));
    RValue partySize = CHARBOX_GET(self, chartotal);
    RValue turn = CHARBOX_GET(global, charturn);
    RValue fighting = CHARBOX_GET(global, fighting);
    bool inBattle = charboxEqual(fighting, RValue_makeReal(1.0), epsilon);
    int32_t bottom = inBattle ? 336 : charboxEqual(fighting, RValue_makeReal(0.0), epsilon) ? 430 : 480;
    bool glowReady = false, spare = false, pacify = false;

    for (int32_t c = 0; c < 3; ++c) {
        CHARBOX_SET(self, c, c);
        if (!charboxEqual(GMLArray_get(present, c), RValue_makeReal(1.0), epsilon)) continue;
        RValue tint = GMLArray_get(colors, c);
        Instance_setSelfVar(self, cache.charcolor, tint);
        Instance_setSelfVar(self, cache.gc, turn);
        RValue position = GMLArray_get(positions, c);
        int32_t chunk = charboxXChunk(partySize, position, epsilon);
        CHARBOX_SET(self, xchunk, chunk);
        bool selected = charboxEqual(turn, position, epsilon);
        if (selected && charboxEqual(CHARBOX_GET(global, myfight), RValue_makeReal(0.0), epsilon)) {
            RValue args[] = {RValue_makeReal(draw.x + chunk), RValue_makeReal((480 - draw.bp) + draw.y)};
            RValue result = NativeScript_call(ctx, &cache.selectionMatrix, "gml_Script_scr_selectionmatrix", args, 2);
            RValue_free(&result);
        }

        GMLReal offset = charboxUpdateOffset(&draw, c, selected);
        int32_t button = charboxUpdateButtons(&draw, selected, turn);
        if (inBattle) {
            if (!glowReady) {
                charboxFindGlow(&draw, &spare, &pacify);
                glowReady = true;
            }
            CHARBOX_SET(self, spare_glow, spare);
            CHARBOX_SET(self, pacify_glow, c == 2 && pacify);
            CHARBOX_SET(self, sglowi, 3);
            charboxDrawButtons(&draw, c, button, draw.x + chunk, selected && spare, selected && c == 2 && pacify);
        }
        charboxDrawPanel(&draw, c, position, chunk, offset, bottom, RValue_toColour(tint), selected);
    }
    CHARBOX_SET(self, c, 3);
    *outResult = RValue_makeUndefined();
    return true;
}

static int32_t charboxSpriteID(const DataWin* dataWin, const char* name) {
    for (uint32_t i = 0; i < dataWin->sprt.count; ++i) {
        const Sprite* sprite = &dataWin->sprt.sprites[i];
        if (sprite->present && strcmp(sprite->name, name) == 0) return (int32_t)i;
    }
    return -1;
}

void NativeCharbox_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 1") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    cache.selectionMatrix = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.mapFind = VM_findBuiltin(ctx, "ds_map_find_value");
    if (cache.mapFind == nullptr ||
        !NativeScript_canCall(ctx, &cache.selectionMatrix, "gml_Script_scr_selectionmatrix") ||
        !VM_registerNativeScript(ctx, "gml_Script_scr_charbox", charboxDraw)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
#define RESOLVE_GLOBAL(name) cache.name = VM_getOrAllocateVarID(ctx, #name);
    CHARBOX_SELF_VARS(RESOLVE_SELF)
    CHARBOX_GLOBAL_VARS(RESOLVE_GLOBAL)
#undef RESOLVE_GLOBAL
#undef RESOLVE_SELF

    cache.heads[0] = charboxSpriteID(ctx->dataWin, "spr_headkris");
    cache.heads[1] = charboxSpriteID(ctx->dataWin, "spr_headsusie");
    cache.heads[2] = charboxSpriteID(ctx->dataWin, "spr_headralsei");
    cache.hpname = charboxSpriteID(ctx->dataWin, "spr_hpname");
    cache.hpslash = charboxSpriteID(ctx->dataWin, "spr_hpslash");
}
