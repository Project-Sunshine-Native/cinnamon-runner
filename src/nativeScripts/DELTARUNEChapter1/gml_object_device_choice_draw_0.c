#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "text_utils.h"

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#define DEVICE_CHOICE_VARS(X) \
    X(fadebuffer) X(xfade) X(TYPE) X(DRAWHEART) X(HEARTX) X(HEARTY) \
    X(xoff) X(yoff) X(XMAX) X(YMAX) X(CURX) X(CURY) X(i) X(j) \
    X(NAMEX) X(NAMEY) X(NAME) X(NAMESTRING) X(STRINGMAX) X(PLAYERNAMEY)

static struct {
#define DECLARE_ID(name) int32_t name;
    DEVICE_CHOICE_VARS(DECLARE_ID)
#undef DECLARE_ID
    NativeCallBinding drawFont;
    BuiltinFunc stringWidth;
    int32_t soulBlur;
} cache;

#define GET(name) Instance_getSelfVar(self, cache.name)
#define REAL(name) RValue_toReal(GET(name))
#define SET(name, value) Instance_setSelfVar(self, cache.name, RValue_makeReal(value))

static bool choiceEqual(RValue value, GMLReal number, GMLReal epsilon) {
    return value.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(value), number, epsilon) == 0;
}

static GMLArray* choiceArray(RValue value) {
    return value.type == RVALUE_ARRAY ? value.array : nullptr;
}

static RValue choiceCell(GMLArray* array, int32_t col, int32_t row) {
    return GMLArray_get(choiceArray(GMLArray_get(array, col)), row);
}

static PreprocessedText choiceLabel(DataWin* data, RValue value) {
    if (value.type == RVALUE_STRING) {
        return TextUtils_preprocessGmlText(value.string != nullptr ? value.string : "");
    }
    return (PreprocessedText){ .text = RValue_toString(value, data), .owning = true };
}

static void choiceDrawCell(Renderer* renderer, GMLArray* names, GMLArray* xs, GMLArray* ys,
    int32_t col, int32_t row, bool naming) {
    PreprocessedText label = choiceLabel(renderer->dataWin, choiceCell(names, col, row));
    const char* text = label.text;
    if (naming && text[0] == '(') {
        int32_t length = (int32_t)strlen(text);
        int32_t prefix = TextUtils_utf8AdvanceCodepoints(text, length, 3);
        if (prefix < length) text += prefix;
    }
    if (!naming || !((text[0] == '<' || text[0] == '>') && text[1] == '\0')) {
        float x = (float)RValue_toReal(choiceCell(xs, col, row));
        float y = (float)RValue_toReal(choiceCell(ys, col, row));
        renderer->vtable->drawText(renderer, text, x, y, 1.0f, 1.0f, 0.0f, -1.0f);
    }
    PreprocessedText_free(label);
}

static bool deviceChoiceDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    Renderer* renderer = runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;

    GMLReal epsilon = runner->mathEpsilon;
    RValue type = GET(TYPE);
    int cmp = NativeScript_compareReals(RValue_toReal(type), 2, epsilon);
    bool rowMenu = type.type != RVALUE_UNDEFINED && cmp < 0;
    bool columnMenu = choiceEqual(type, 2, epsilon);
    bool naming = choiceEqual(type, 3, epsilon);
    GMLReal xmax = REAL(XMAX), ymax = REAL(YMAX);
    if (((rowMenu || naming) && !(xmax < INT32_MAX)) ||
        ((columnMenu || naming) && !(ymax < INT32_MAX))) return false;

    RValue font = RValue_makeString("main");
    RValue result = NativeScript_call(ctx, &cache.drawFont, "gml_Script_scr_84_set_draw_font", &font, 1);
    RValue_free(&result);

    GMLReal fade = (10 - REAL(fadebuffer)) / 10;
    if (NativeScript_compareReals(fade, 1, epsilon) > 0) fade = 1;
    SET(xfade, fade);
    if (rowMenu || columnMenu || naming) {
        if (choiceEqual(GET(DRAWHEART), 1, epsilon)) {
            GMLReal x = REAL(HEARTX), y = REAL(HEARTY);
            if (!naming) {
                x += REAL(xoff);
                y += REAL(yoff);
            }
            Renderer_drawSpriteExt(renderer, cache.soulBlur, 0,
                (float)x, (float)y, 1, 1, 0, 0xFFFFFF, (float)((naming ? 0.5 : 0.6) * fade));
        }

        renderer->drawAlpha = (float)fade;
        GMLArray* names = choiceArray(GET(NAME));
        GMLArray* xs = choiceArray(GET(NAMEX));
        GMLArray* ys = choiceArray(GET(NAMEY));
        RValue curX = GET(CURX), curY = GET(CURY);
        int32_t i = 0;
        if (rowMenu || columnMenu) {
            GMLReal end = rowMenu ? xmax : ymax;
            for (; NativeScript_compareReals(i, end, epsilon) <= 0; ++i) {
                renderer->drawColor = choiceEqual(rowMenu ? curX : curY, i, epsilon) ? 0x00FFFF : 0xFFFFFF;
                choiceDrawCell(renderer, names, xs, ys, rowMenu ? i : 0, columnMenu ? i : 0, false);
            }
            SET(i, i);
        } else {
            int32_t j = 0;
            for (; NativeScript_compareReals(j, ymax, epsilon) <= 0; ++j) {
                bool selectedRow = choiceEqual(curY, j, epsilon);
                for (i = 0; NativeScript_compareReals(i, xmax, epsilon) <= 0; ++i) {
                    renderer->drawColor = selectedRow && choiceEqual(curX, i, epsilon) ? 0x00FFFF : 0xFFFFFF;
                    choiceDrawCell(renderer, names, xs, ys, i, j, true);
                }
            }
            if (j != 0) SET(i, i);
            SET(j, j);

            RValue name = GET(NAMESTRING);
            const char* text = name.type == RVALUE_STRING && name.string != nullptr ? name.string : "";
            int32_t length = TextUtils_utf8CodepointCount(text, (int32_t)strlen(text));
            renderer->drawColor = choiceEqual(GET(STRINGMAX), length, epsilon) ? 0x00FFFF : 0xFFFFFF;
            GMLReal width = RValue_toReal(cache.stringWidth(ctx, &name, 1));
            PreprocessedText label = choiceLabel(ctx->dataWin, name);
            renderer->vtable->drawText(renderer, label.text, (float)((320 - width) / 2), (float)REAL(PLAYERNAMEY),
                1.0f, 1.0f, 0.0f, -1.0f);
            PreprocessedText_free(label);
        }
        renderer->drawAlpha = 1.0f;
    }

    *outResult = RValue_makeUndefined();
    return true;
}

void NativeDeviceChoiceDraw_register(VMContext* ctx) {
    if (!DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;
    cache.soulBlur = -1;
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
        if (sprite->present && strcmp(sprite->name, "IMAGE_SOUL_BLUR") == 0) {
            cache.soulBlur = (int32_t)i;
            break;
        }
    }
    cache.drawFont = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.stringWidth = VM_findBuiltin(ctx, "string_width");
    if (cache.soulBlur < 0 || cache.stringWidth == nullptr ||
        !NativeScript_canCall(ctx, &cache.drawFont, "gml_Script_scr_84_set_draw_font") ||
        !VM_registerNativeScript(ctx, "gml_Object_DEVICE_CHOICE_Draw_0", deviceChoiceDraw)) return;

#define RESOLVE(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
    DEVICE_CHOICE_VARS(RESOLVE)
#undef RESOLVE
}
