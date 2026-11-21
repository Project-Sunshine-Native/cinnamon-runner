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

enum {
    DARK_WHITE = 0xFFFFFF,
    DARK_GRAY = 0x808080,
    DARK_LIGHT_GRAY = 0xC0C0C0,
    DARK_ORANGE = 4235519,
    DARK_BUTTON_ARRAY_OWNER = 0x28e4d,
};

#define DARK_SELF_VARS(X) \
    X(xx) X(yy) X(tp) X(tpy) X(tpoff) X(bp) X(bpy) X(bpoff) X(drawchar) \
    X(deschaver) X(_spr_darkmenudesc) X(off) X(spritemx) X(bcolor) \
    X(sm) X(xheart) X(yheart) X(itemdesc) X(keyitemname) X(keyitemusable) X(keyitemdesc)

#define DARK_GLOBAL_VARS(X) X(menuno) X(menucoord) X(gold) X(lang) X(submenu) X(submenucoord) X(itemname)

static struct {
#define DECLARE_ID(name) int32_t name;
    DARK_SELF_VARS(DECLARE_ID)
    DARK_GLOBAL_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t heart, buttons[5];
    NativeVarBinding msprite;
    NativeCallBinding charbox, darkbox, drawFont, langString, itemName, keyItemInfo;
} cache;

typedef struct {
    VMContext* ctx;
    Instance* self;
    Instance* globals;
    Renderer* renderer;
    GMLReal x, y, epsilon;
} DarkDraw;

#define GET_SELF(draw, name) Instance_getSelfVar((draw)->self, cache.name)
#define GET_GLOBAL(draw, name) Instance_getSelfVar((draw)->globals, cache.name)
#define SET_REAL(draw, name, value) Instance_setSelfVar((draw)->self, cache.name, RValue_makeReal(value))

static GMLArray* darkArray(RValue value) {
    return value.type == RVALUE_ARRAY ? value.array : nullptr;
}

static bool darkEqual(const DarkDraw* draw, RValue value, GMLReal number) {
    return value.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(value), number, draw->epsilon) == 0;
}

static void darkRectangle(const DarkDraw* draw, GMLReal x1, GMLReal y1, GMLReal x2, GMLReal y2) {
    Renderer* renderer = draw->renderer;
    renderer->vtable->drawRectangle(renderer, (float)x1, (float)y1, (float)x2, (float)y2,
        renderer->drawColor, renderer->drawAlpha, false);
}

static PreprocessedText darkPrepareText(const DarkDraw* draw, RValue value) {
    if (value.type == RVALUE_STRING) {
        return TextUtils_preprocessGmlText(value.string != nullptr ? value.string : "");
    }
    return (PreprocessedText){ .text = RValue_toString(value, draw->ctx->dataWin), .owning = true };
}

static void darkText(const DarkDraw* draw, GMLReal x, GMLReal y, const char* text) {
    Renderer* renderer = draw->renderer;
    renderer->vtable->drawText(renderer, text, (float)x, (float)y, 1.0f, 1.0f, 0.0f, -1.0f);
}

static void darkLabel(const DarkDraw* draw, GMLReal x, GMLReal y, RValue value) {
    PreprocessedText text = darkPrepareText(draw, value);
    darkText(draw, x, y, text.text);
    PreprocessedText_free(text);
}

static void darkMenuFont(const DarkDraw* draw) {
    RValue font = RValue_makeString("mainbig");
    RValue result = NativeScript_call(draw->ctx, &cache.drawFont, "gml_Script_scr_84_set_draw_font", &font, 1);
    RValue_free(&result);
}

static RValue darkLocalized(const DarkDraw* draw, const char* key) {
    RValue arg = RValue_makeString(key);
    return NativeScript_call(draw->ctx, &cache.langString, "gml_Script_scr_84_get_lang_string", &arg, 1);
}

static void darkDrawHud(const DarkDraw* draw, RValue menu, GMLReal tp, GMLReal bp) {
    if (!darkEqual(draw, GET_SELF(draw, drawchar), 1)) return;

    Renderer* renderer = draw->renderer;
    GMLReal x = draw->x, y = draw->y;
    renderer->drawColor = 0;
    darkRectangle(draw, x - 10, (y + 480) - bp, x + 650, y + 500);
    darkRectangle(draw, x - 10, y + tp, x + 650, y - 10);
    RValue result = NativeScript_call(draw->ctx, &cache.charbox, "gml_Script_scr_charbox", nullptr, 0);
    RValue_free(&result);

    if (darkEqual(draw, menu, 0)) SET_REAL(draw, deschaver, 0);
    if (!darkEqual(draw, GET_SELF(draw, deschaver), 0)) return;

    RValue selected = GMLArray_get(darkArray(GET_GLOBAL(draw, menucoord)), 0);
    int32_t frame = RValue_toInt32(selected);
    if (frame < 0) frame = (int32_t)draw->self->imageIndex;
    Renderer_drawSpriteExt(renderer, RValue_toInt32(GET_SELF(draw, _spr_darkmenudesc)), frame,
        (float)(x + 20), (float)((y + tp) - 56), 2, 2, 0, DARK_WHITE, 1);

    draw->ctx->currentArrayOwner = (void*)(intptr_t)DARK_BUTTON_ARRAY_OWNER;
    for (int32_t i = 0; i < 5; ++i) {
        NativeScript_setVariableIndex(draw->ctx, draw->self, &cache.msprite, "msprite",
            RValue_makeInt32(i), RValue_makeInt32(cache.buttons[i]));
    }
    RValue activeButton = RValue_makeReal(RValue_toReal(menu) - 1);
    for (int32_t i = 0; i < 5; ++i) {
        int32_t off = darkEqual(draw, selected, i) ? 0 : 1;
        if (darkEqual(draw, activeButton, i)) off = 2;
        int32_t shift = i >= 2 ? -100 : 0;
        SET_REAL(draw, off, off);
        SET_REAL(draw, spritemx, shift);
        if (i != 2) {
            Renderer_drawSpriteExt(renderer, cache.buttons[i], off,
                (float)(x + 120 + i * 100 + shift), (float)((y + tp) - 60), 2, 2, 0, DARK_WHITE, 1);
        }
    }

    renderer->drawColor = DARK_WHITE;
    darkMenuFont(draw);
    RValue prefix = darkLocalized(draw, "obj_darkcontroller_slash_Draw_0_gml_47_0");
    RValue gold = RValue_makeOwnedString(RValue_toString(GET_GLOBAL(draw, gold), draw->ctx->dataWin));
    RValue label = NativeScript_binary(draw->ctx, NATIVE_OP_ADD, prefix, gold);
    darkLabel(draw, x + 520, (y + tp) - 60, label);
    RValue_free(&label);
    RValue_free(&gold);
    RValue_free(&prefix);
}

static void darkItemCursor(const DarkDraw* draw, RValue slot, bool japanese, bool show) {
    GMLReal selected = RValue_toReal(slot);
    Instance_setSelfVar(draw->self, cache.sm, slot);
    GMLReal y = (GMLReal_floor(selected / 2) * 30) + 162 + draw->y;
    bool right = false;
    for (int32_t i = 1; i < 12; i += 2) {
        if (darkEqual(draw, slot, i)) {
            right = true;
            break;
        }
    }
    GMLReal x = (right ? (japanese ? 334 : 330) : (japanese ? 72 : 120)) + draw->x;
    SET_REAL(draw, yheart, y);
    SET_REAL(draw, xheart, x);
    if (show) Renderer_drawSprite(draw->renderer, cache.heart, 0, (float)x, (float)y);
}

static void darkItemPrompt(const DarkDraw* draw, bool japanese, int32_t slot) {
    RValue item = GMLArray_get(darkArray(GET_GLOBAL(draw, itemname)), slot);
    RValue prompt = darkLocalized(draw, "obj_darkcontroller_slash_Draw_0_gml_829_0");
    if (japanese) {
        PreprocessedText suffix = darkPrepareText(draw, prompt);
        RValue label = NativeScript_binary(draw->ctx, NATIVE_OP_ADD, item, RValue_makeString(suffix.text));
        darkText(draw, draw->x + 20, draw->y + 10, label.string);
        RValue_free(&label);
        PreprocessedText_free(suffix);
    } else {
        RValue prefix = NativeScript_binary(draw->ctx, NATIVE_OP_ADD, prompt, item);
        RValue label = NativeScript_binary(draw->ctx, NATIVE_OP_ADD, prefix, RValue_makeString("?"));
        darkLabel(draw, draw->x + 20, draw->y + 10, label);
        RValue_free(&label);
        RValue_free(&prefix);
    }
    RValue_free(&prompt);
}

static void darkItemList(const DarkDraw* draw, bool japanese, bool keyItems, RValue submenu) {
    Renderer* renderer = draw->renderer;
    GMLArray* names = darkArray(keyItems ? GET_SELF(draw, keyitemname) : GET_GLOBAL(draw, itemname));
    bool showUsable = keyItems && darkEqual(draw, submenu, 4);
    GMLArray* usable = showUsable ? darkArray(GET_SELF(draw, keyitemusable)) : nullptr;
    GMLReal firstColumn = japanese ? 92 : 146;
    PreprocessedText labels[12];

    renderer->drawColor = RValue_toColour(GET_SELF(draw, bcolor));
    for (int32_t i = 0; i < 12; ++i) {
        labels[i] = darkPrepareText(draw, GMLArray_get(names, i));
        darkText(draw, draw->x + ((i & 1) ? 358 : firstColumn + 2),
            draw->y + 154 + 30 * (i / 2), labels[i].text);
    }

    renderer->drawColor = darkEqual(draw, submenu, 1) ? DARK_GRAY : DARK_WHITE;
    for (int32_t i = 0; i < 12; ++i) {
        if (showUsable) {
            renderer->drawColor = darkEqual(draw, GMLArray_get(usable, i), 1) ? DARK_WHITE : DARK_LIGHT_GRAY;
        }
        darkText(draw, draw->x + ((i & 1) ? 356 : firstColumn),
            draw->y + 152 + 30 * (i / 2), labels[i].text);
        PreprocessedText_free(labels[i]);
    }
}

static void darkDrawInventory(const DarkDraw* draw) {
    static const char* const tabLabels[] = {
        "obj_darkcontroller_slash_Draw_0_gml_807_0",
        "obj_darkcontroller_slash_Draw_0_gml_809_0",
        "obj_darkcontroller_slash_Draw_0_gml_811_0",
    };
    GMLReal x = draw->x, y = draw->y;
    Renderer* renderer = draw->renderer;
    RValue lang = GET_GLOBAL(draw, lang);
    bool japanese = lang.type == RVALUE_STRING && lang.string != nullptr && strcmp(lang.string, "ja") == 0;
    renderer->drawColor = 0;
    darkRectangle(draw, x + (japanese ? 46 : 70), y + 90, x + (japanese ? 594 : 570), y + 360);
    RValue box[] = {
        RValue_makeReal(x + (japanese ? 36 : 60)), RValue_makeReal(y + 80),
        RValue_makeReal(x + (japanese ? 604 : 580)), RValue_makeReal(y + 370),
    };
    RValue result = NativeScript_call(draw->ctx, &cache.darkbox, "gml_Script_scr_darkbox", box, 4);
    RValue_free(&result);
    darkMenuFont(draw);
    result = NativeScript_call(draw->ctx, &cache.itemName, "gml_Script_scr_itemname", nullptr, 0);
    RValue_free(&result);

    RValue submenu = GET_GLOBAL(draw, submenu);
    GMLReal page = RValue_toReal(submenu);
    GMLArray* coords = darkArray(GET_GLOBAL(draw, submenucoord));
    RValue tab = GMLArray_get(coords, 1);
    if (darkEqual(draw, submenu, 1)) {
        Renderer_drawSprite(renderer, cache.heart, 0,
            (float)(x + (japanese ? 134 : 155) + 120 * RValue_toReal(tab)), (float)(y + 120));
    }
    renderer->drawColor = DARK_WHITE;
    for (int32_t i = 0; i < 3; ++i) {
        if (submenu.type != RVALUE_UNDEFINED && NativeScript_compareReals(page, 1, draw->epsilon) > 0) {
            renderer->drawColor = darkEqual(draw, tab, i) ? DARK_ORANGE : DARK_GRAY;
        }
        RValue label = darkLocalized(draw, tabLabels[i]);
        darkLabel(draw, x + (japanese ? 158 : 180) + 120 * i, y + 110, label);
        RValue_free(&label);
    }

    RValue slot = GMLArray_get(coords, 2);
    if (NativeScript_compareReals(page, 2, draw->epsilon) >= 0 &&
        NativeScript_compareReals(page, 6, draw->epsilon) <= 0 && !darkEqual(draw, submenu, 4)) {
        darkItemCursor(draw, slot, japanese, darkEqual(draw, submenu, 2) || darkEqual(draw, submenu, 3));
        renderer->drawColor = DARK_WHITE;
        darkLabel(draw, x + 20, y + 10, GMLArray_get(darkArray(GET_SELF(draw, itemdesc)), RValue_toInt32(slot)));
    }
    if (darkEqual(draw, submenu, 7)) {
        renderer->drawColor = DARK_WHITE;
        darkItemPrompt(draw, japanese, RValue_toInt32(slot));
    }

    bool keyItems = darkEqual(draw, tab, 2);
    if (keyItems) {
        result = NativeScript_call(draw->ctx, &cache.keyItemInfo, "gml_Script_scr_keyiteminfo_all", nullptr, 0);
        RValue_free(&result);
    }
    darkItemList(draw, japanese, keyItems, submenu);

    if (keyItems && darkEqual(draw, submenu, 4)) {
        slot = GMLArray_get(coords, 4);
        darkItemCursor(draw, slot, japanese, true);
        renderer->drawColor = DARK_WHITE;
        darkLabel(draw, x + 20, y + 10, GMLArray_get(darkArray(GET_SELF(draw, keyitemdesc)), RValue_toInt32(slot)));
    }
}

static bool darkcontrollerDraw(VMContext* ctx, RValue* outResult) {
    Runner* runner = ctx->runner;
    if (runner->renderer == nullptr) return false;
    DarkDraw draw = {
        .ctx = ctx, .self = ctx->currentCallFrame->self, .globals = ctx->globalScopeInstance,
        .renderer = runner->renderer, .epsilon = runner->mathEpsilon,
    };
    RValue menu = GET_GLOBAL(&draw, menuno);
    bool inventory = darkEqual(&draw, menu, 1);
    if (!inventory && !darkEqual(&draw, menu, -1) &&
        !darkEqual(&draw, menu, 0) && !darkEqual(&draw, menu, 3)) return false;

    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    draw.x = camera != nullptr ? camera->viewX : -1;
    draw.y = camera != nullptr ? camera->viewY : -1;
    GMLReal tp = RValue_toReal(GET_SELF(&draw, tp));
    GMLReal bp = RValue_toReal(GET_SELF(&draw, bp));
    SET_REAL(&draw, xx, draw.x);
    SET_REAL(&draw, yy, draw.y);
    SET_REAL(&draw, tpoff, (tp - RValue_toReal(GET_SELF(&draw, tpy))) + draw.y);
    SET_REAL(&draw, bpoff, (-bp + RValue_toReal(GET_SELF(&draw, bpy))) + draw.y);
    darkDrawHud(&draw, menu, tp, bp);
    if (inventory) darkDrawInventory(&draw);

    *outResult = RValue_makeUndefined();
    return true;
}

static int32_t darkSpriteID(const DataWin* dataWin, const char* name) {
    for (uint32_t i = 0; i < dataWin->sprt.count; ++i) {
        const Sprite* sprite = &dataWin->sprt.sprites[i];
        if (sprite->present && strcmp(sprite->name, name) == 0) return (int32_t)i;
    }
    return -1;
}

void NativeDarkcontrollerDraw_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 1") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    static const char* const buttonSprites[] = {
        "spr_darkitembt", "spr_darkequipbt", "spr_darktalkbt", "spr_darktechbt", "spr_darkconfigbt",
    };
    for (int32_t i = 0; i < 5; ++i) {
        cache.buttons[i] = darkSpriteID(ctx->dataWin, buttonSprites[i]);
        if (cache.buttons[i] < 0) return;
    }
    cache.heart = darkSpriteID(ctx->dataWin, "spr_heart");
    cache.msprite = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    cache.charbox = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.darkbox = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.drawFont = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.langString = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.itemName = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.keyItemInfo = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (cache.heart < 0 ||
        !NativeScript_canCall(ctx, &cache.charbox, "gml_Script_scr_charbox") ||
        !NativeScript_canCall(ctx, &cache.darkbox, "gml_Script_scr_darkbox") ||
        !NativeScript_canCall(ctx, &cache.drawFont, "gml_Script_scr_84_set_draw_font") ||
        !NativeScript_canCall(ctx, &cache.langString, "gml_Script_scr_84_get_lang_string") ||
        !NativeScript_canCall(ctx, &cache.itemName, "gml_Script_scr_itemname") ||
        !NativeScript_canCall(ctx, &cache.keyItemInfo, "gml_Script_scr_keyiteminfo_all") ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_darkcontroller_Draw_0", darkcontrollerDraw)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
#define RESOLVE_GLOBAL(name) cache.name = VM_getOrAllocateVarID(ctx, #name);
    DARK_SELF_VARS(RESOLVE_SELF)
    DARK_GLOBAL_VARS(RESOLVE_GLOBAL)
#undef RESOLVE_GLOBAL
#undef RESOLVE_SELF
}
