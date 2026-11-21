#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "stb_ds.h"
#include "text_utils.h"
#include "utils.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

enum {
    BATTLE_WHITE = 0xFFFFFF,
    BATTLE_GRAY = 0x808080,
    BATTLE_MAROON = 0x000080,
    BATTLE_LIME = 0x00FF00,
    BATTLE_YELLOW = 0x00FFFF,
    BATTLE_ORANGE = 4235519,
    BATTLE_ITEM_WIDTH_OWNER = 0x28f25,
};

#define BATTLE_SELF_VARS(X) \
    X(xx) X(yy) X(tp) X(tpy) X(tpoff) X(bp) X(bpy) X(bpoff) X(intro) X(bcolor) \
    X(i) X(flash) X(fsiner) X(becomeflash) X(mercydraw) X(tireddraw) \
    X(mnamecolor1) X(mnamecolor2) X(aqcolor) X(namewidth) X(mercyamt) \
    X(thischar) X(spellcoord) X(page) X(icx) X(icy) X(pacify_glow) X(sglowi) \
    X(thiscost) X(itemcoord) X(jp_string_lengthtest) X(jp_string_xscale) \
    X(tempitemnameb) X(tempitemdescb) X(s_siner) X(actcoord) X(cant) X(chartime) \
    X(charoffset) X(xoffset) X(yoffset) X(drawsus) X(drawral) X(susblend) X(ralblend) \
    X(havechar) X(jp_string_lengthtest_act)

#define BATTLE_GLOBAL_VARS(X) \
    X(lang) X(bmenuno) X(myfight) X(bmenucoord) X(charturn) X(monsterinstance) \
    X(monster) X(monsterstatus) X(mercymod) X(mercymax) X(monstername) \
    X(monstercomment) X(monsterhp) X(monstermaxhp) X(encounterno) \
    X(tension) X(spellcost) X(spell) X(spellnameb) X(spelldescb) X(maxtension) \
    X(item) X(actactor) X(hp) X(actcost) X(actname) X(actdesc) X(tensionselect) \
    X(charinstance) X(charname) X(maxhp)

static struct {
#define DECLARE_ID(name) int32_t name;
    BATTLE_SELF_VARS(DECLARE_ID)
    BATTLE_GLOBAL_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t partyChar, heart, tiredMark, spareStar, moreArrow, headSusie, headRalsei;
    uint32_t tiredColor;
    BuiltinFunc stringWidth;
    NativeCallBinding charbox, drawFont, langString;
} cache;

typedef struct {
    VMContext* ctx;
    Instance* self;
    Instance* globals;
    Renderer* renderer;
    GMLReal x, y, epsilon;
    int32_t turn, spellOffset;
    bool japanese;
} BattleDraw;

#define GET_SELF(draw, name) Instance_getSelfVar((draw)->self, cache.name)
#define GET_GLOBAL(draw, name) Instance_getSelfVar((draw)->globals, cache.name)
#define SET_REAL(draw, name, value) Instance_setSelfVar((draw)->self, cache.name, RValue_makeReal((GMLReal)(value)))

static GMLArray* battleArray(RValue value) {
    return value.type == RVALUE_ARRAY ? value.array : nullptr;
}

static GMLArray* battleRow(RValue value, int32_t row) {
    return battleArray(GMLArray_get(battleArray(value), row));
}

static bool battleEqual(const BattleDraw* draw, RValue value, GMLReal number) {
    return value.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(value), number, draw->epsilon) == 0;
}

static bool battleLess(const BattleDraw* draw, RValue a, RValue b) {
    return a.type != RVALUE_UNDEFINED && b.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(a), RValue_toReal(b), draw->epsilon) < 0;
}

static RValue battleChoice(const BattleDraw* draw, int32_t menu) {
    return GMLArray_get(battleRow(GET_GLOBAL(draw, bmenucoord), menu), draw->turn);
}

static void battleRectangle(const BattleDraw* draw, GMLReal x1, GMLReal y1, GMLReal x2, GMLReal y2) {
    Renderer* renderer = draw->renderer;
    renderer->vtable->drawRectangle(renderer, (float)x1, (float)y1, (float)x2, (float)y2,
        renderer->drawColor, renderer->drawAlpha, false);
}

static PreprocessedText battleText(const BattleDraw* draw, RValue value) {
    if (value.type == RVALUE_STRING) {
        return TextUtils_preprocessGmlText(value.string != nullptr ? value.string : "");
    }
    return (PreprocessedText){ .text = RValue_toString(value, draw->ctx->dataWin), .owning = true };
}

static void battleDrawText(const BattleDraw* draw, const char* text, GMLReal x, GMLReal y, GMLReal scale) {
    Renderer* renderer = draw->renderer;
    renderer->vtable->drawText(renderer, text, (float)x, (float)y, (float)scale, 1.0f, 0.0f, -1.0f);
}

static void battleDrawLabel(const BattleDraw* draw, RValue value, GMLReal x, GMLReal y) {
    PreprocessedText label = battleText(draw, value);
    battleDrawText(draw, label.text, x, y, 1.0);
    PreprocessedText_free(label);
}

static GMLReal battleTextWidth(const BattleDraw* draw, const char* text) {
    RValue arg = RValue_makeString(text);
    return RValue_toReal(cache.stringWidth(draw->ctx, &arg, 1));
}

static void battleMenuFont(const BattleDraw* draw) {
    RValue arg = RValue_makeString("mainbig");
    RValue result = NativeScript_call(draw->ctx, &cache.drawFont, "gml_Script_scr_84_set_draw_font", &arg, 1);
    RValue_free(&result);
}

static void battleDrawCost(const BattleDraw* draw, RValue cost, GMLReal x, const char* langKey) {
    GMLReal max = RValue_toReal(GET_GLOBAL(draw, maxtension));
    GMLReal percent = GMLReal_bankersRound((RValue_toReal(cost) / max) * (GMLReal)100);
    RValue value = RValue_makeReal(percent);
    Instance_setSelfVar(draw->self, cache.thiscost, value);
    draw->renderer->drawColor = BATTLE_ORANGE;

    RValue key = RValue_makeString(langKey);
    RValue suffix = NativeScript_call(draw->ctx, &cache.langString, "gml_Script_scr_84_get_lang_string", &key, 1);
    RValue label;
    if (suffix.type == RVALUE_STRING) {
        const char* ending = suffix.string != nullptr ? suffix.string : "";
        char* text = RValue_toString(value, draw->ctx->dataWin);
        size_t length = strlen(text);
        size_t endingLength = strlen(ending) + 1;
        text = (char*)safeRealloc(text, length + endingLength);
        memcpy(text + length, ending, endingLength);
        label = RValue_makeOwnedString(text);
    } else {
        label = RValue_makeReal(percent + RValue_toReal(suffix));
    }
    battleDrawLabel(draw, label, x, draw->y + 440);
    RValue_free(&label);
    RValue_free(&suffix);
}

static void battleFlashTarget(const BattleDraw* draw, RValue target) {
    if (target.type == RVALUE_UNDEFINED) return;
    int32_t id = RValue_toInt32(target);
    if (id < INSTANCE_ID_BASE) return;
    Instance* instance = hmget(draw->ctx->runner->instancesById, id);
    if (instance == nullptr || !instance->active) return;

    if (battleEqual(draw, Instance_getSelfVar(instance, cache.flash), 0)) {
        Instance_setSelfVar(instance, cache.fsiner, RValue_makeReal(0.0));
    }
    Instance_setSelfVar(instance, cache.flash, RValue_makeReal(1.0));
    Instance_setSelfVar(instance, cache.becomeflash, RValue_makeReal(1.0));
}

static void battleGridCursor(const BattleDraw* draw, int32_t choice, int32_t rightX) {
    int32_t x = choice == 1 || choice == 3 || choice == 5 ? rightX : 10;
    int32_t y = choice > 3 ? 445 : choice > 1 ? 415 : 385;
    SET_REAL(draw, icx, x);
    SET_REAL(draw, icy, y);
    Renderer_drawSprite(draw->renderer, cache.heart, 0, (float)(draw->x + x), (float)(draw->y + y));
    battleMenuFont(draw);
}

static void battleTargetCursor(const BattleDraw* draw, RValue choice) {
    Renderer_drawSprite(draw->renderer, cache.heart, 0,
        (float)(draw->x + 55), (float)((draw->y + 385) + RValue_toReal(choice) * (GMLReal)30));
    battleMenuFont(draw);
}

static void battleDrawEnemies(const BattleDraw* draw, int32_t menu) {
    RValue choice = battleChoice(draw, menu);
    battleTargetCursor(draw, choice);

    Renderer* renderer = draw->renderer;
    GMLArray* monsters = battleArray(GET_GLOBAL(draw, monster));
    GMLArray* status = battleArray(GET_GLOBAL(draw, monsterstatus));
    GMLArray* mercy = battleArray(GET_GLOBAL(draw, mercymod));
    GMLArray* maxMercy = battleArray(GET_GLOBAL(draw, mercymax));
    GMLArray* names = battleArray(GET_GLOBAL(draw, monstername));
    GMLArray* descriptions = battleArray(GET_GLOBAL(draw, monstercomment));
    GMLArray* hp = battleArray(GET_GLOBAL(draw, monsterhp));
    GMLArray* maxHp = battleArray(GET_GLOBAL(draw, monstermaxhp));
    GMLArray* targets = battleArray(GET_GLOBAL(draw, monsterinstance));
    RValue target = GMLArray_get(targets, RValue_toInt32(choice));
    bool allowTiredColor = !battleEqual(draw, GET_GLOBAL(draw, encounterno), 31);

    for (int32_t i = 0; i < 3; ++i) {
        SET_REAL(draw, i, i);
        battleFlashTarget(draw, target);
        if (!battleEqual(draw, GMLArray_get(monsters, i), 1)) continue;

        renderer->drawColor = BATTLE_WHITE;
        bool tired = battleEqual(draw, GMLArray_get(status, i), 1);
        RValue amount = GMLArray_get(mercy, i);
        RValue max = GMLArray_get(maxMercy, i);
        bool spare = amount.type == RVALUE_UNDEFINED || max.type == RVALUE_UNDEFINED
            ? amount.type == max.type : !battleLess(draw, amount, max);
        SET_REAL(draw, mercydraw, spare);
        SET_REAL(draw, tireddraw, tired);
        SET_REAL(draw, aqcolor, cache.tiredColor);

        PreprocessedText name = battleText(draw, GMLArray_get(names, i));
        GMLReal width = battleTextWidth(draw, name.text);
        SET_REAL(draw, namewidth, width);
        GMLReal nameX = draw->x + 80;
        GMLReal textY = (draw->y + 375) + i * 30;
        GMLReal iconY = (draw->y + 385) + i * 30;
        uint32_t color1 = BATTLE_WHITE, color2 = BATTLE_WHITE;
        if (tired) {
            if (allowTiredColor) {
                renderer->drawColor = cache.tiredColor;
                color1 = cache.tiredColor;
                color2 = cache.tiredColor;
            }
            Renderer_drawSprite(renderer, cache.tiredMark, 0, (float)(nameX + width + 40), (float)iconY);
        }
        if (spare) {
            renderer->drawColor = BATTLE_YELLOW;
            color1 = BATTLE_YELLOW;
            if (!tired) color2 = BATTLE_YELLOW;
            Renderer_drawSprite(renderer, cache.spareStar, 0, (float)(nameX + width + 20), (float)iconY);
        }
        SET_REAL(draw, mnamecolor1, color1);
        SET_REAL(draw, mnamecolor2, color2);
        renderer->vtable->drawTextColor(renderer, name.text, (float)nameX, (float)textY,
            1.0f, 1.0f, 0.0f, color1, color2, color2, color1, 1.0f, -1.0f);
        PreprocessedText_free(name);

        renderer->drawColor = BATTLE_GRAY;
        battleDrawLabel(draw, GMLArray_get(descriptions, i), nameX + width + 60, textY);
        GMLReal barY = (draw->y + 380) + i * 30;
        renderer->drawColor = BATTLE_MAROON;
        battleRectangle(draw, draw->x + 510, barY, draw->x + 590, barY + 15);
        renderer->drawColor = BATTLE_LIME;
        GMLReal fraction = RValue_toReal(GMLArray_get(hp, i)) / RValue_toReal(GMLArray_get(maxHp, i));
        battleRectangle(draw, draw->x + 510, barY, (draw->x + 510) + fraction * (GMLReal)80, barY + 15);
        if (!battleLess(draw, amount, RValue_makeReal(100.0)) && amount.type != RVALUE_UNDEFINED) {
            amount = RValue_makeReal(100.0);
        }
        Instance_setSelfVar(draw->self, cache.mercyamt, amount);
    }
    SET_REAL(draw, i, 3);
}

static bool battlePacifyGlow(const BattleDraw* draw) {
    GMLArray* monsters = battleArray(GET_GLOBAL(draw, monster));
    GMLArray* status = battleArray(GET_GLOBAL(draw, monsterstatus));
    bool glow = false;
    if (!battleEqual(draw, GET_GLOBAL(draw, encounterno), 31)) {
        for (int32_t i = 0; i < 3; ++i) {
            if (battleEqual(draw, GMLArray_get(monsters, i), 1) &&
                battleEqual(draw, GMLArray_get(status, i), 1)) {
                glow = true;
                break;
            }
        }
    }
    SET_REAL(draw, sglowi, 3);
    SET_REAL(draw, pacify_glow, glow);
    return glow;
}

static void battleDrawSpells(const BattleDraw* draw) {
    RValue character = GMLArray_get(battleArray(GET_GLOBAL(draw, partyChar)), draw->turn);
    Instance_setSelfVar(draw->self, cache.thischar, character);
    int32_t who = RValue_toInt32(character);
    int32_t choice = RValue_toInt32(battleChoice(draw, 2));
    int32_t page = choice > 5;
    choice -= page * 6;
    SET_REAL(draw, spellcoord, choice);
    SET_REAL(draw, page, page);
    battleGridCursor(draw, choice, 230);

    Renderer* renderer = draw->renderer;
    RValue tension = GET_GLOBAL(draw, tension);
    GMLArray* costs = battleRow(GET_GLOBAL(draw, spellcost), who);
    GMLArray* spells = battleRow(GET_GLOBAL(draw, spell), who);
    GMLArray* names = battleRow(GET_GLOBAL(draw, spellnameb), who);
    for (int32_t i = 0; i < 3; ++i) {
        SET_REAL(draw, i, i);
        int32_t slot = page * 6 + i * 2;
        GMLReal y = (draw->y + 375) + i * 30;
        renderer->drawColor = BATTLE_WHITE;
        if (battleLess(draw, tension, GMLArray_get(costs, slot))) {
            renderer->drawColor = BATTLE_GRAY;
        } else if (battleEqual(draw, GMLArray_get(spells, slot), 3) && battlePacifyGlow(draw)) {
            renderer->drawColor = cache.tiredColor;
        }
        battleDrawLabel(draw, GMLArray_get(names, slot), draw->x + 30, y);
        renderer->drawColor = battleLess(draw, tension, GMLArray_get(costs, slot + 1))
            ? BATTLE_GRAY : BATTLE_WHITE;
        battleDrawLabel(draw, GMLArray_get(names, slot + 1), draw->x + 260, y);
    }
    SET_REAL(draw, i, 3);

    int32_t slot = page * 6 + choice;
    GMLReal x = draw->x + draw->spellOffset;
    renderer->drawColor = BATTLE_GRAY;
    battleDrawLabel(draw, GMLArray_get(battleRow(GET_GLOBAL(draw, spelldescb), who), slot), x, draw->y + 375);
    battleDrawCost(draw, GMLArray_get(costs, slot), x, "obj_battlecontroller_slash_Draw_0_gml_171_0");
}

static void battleDrawJapaneseItems(const BattleDraw* draw, RValue left, RValue right, GMLReal y) {
    draw->ctx->currentArrayOwner = (void*)(intptr_t)BATTLE_ITEM_WIDTH_OWNER;
    RValue* widths = IntRValueHashMap_getOrInsertUndefined(&draw->self->selfVars, cache.jp_string_lengthtest);
    PreprocessedText labels[2] = { battleText(draw, left), battleText(draw, right) };
    GMLReal width[2];
    for (int32_t col = 0; col < 2; ++col) {
        width[col] = battleTextWidth(draw, labels[col].text);
        VM_setArrayElement(draw->ctx, widths, col, RValue_makeReal(width[col]));
    }
    for (int32_t col = 0; col < 2; ++col) {
        GMLReal scale = 1.0;
        if (NativeScript_compareReals(width[col], 200, draw->epsilon) >= 0) {
            scale = (GMLReal)200 / width[col];
            SET_REAL(draw, jp_string_xscale, scale);
        }
        battleDrawText(draw, labels[col].text, draw->x + (col == 0 ? 30 : 260), y, scale);
        PreprocessedText_free(labels[col]);
    }
}

static void battleDrawItems(const BattleDraw* draw) {
    int32_t choice = RValue_toInt32(battleChoice(draw, 4));
    int32_t page = choice > 5;
    choice -= page * 6;
    SET_REAL(draw, itemcoord, choice);
    SET_REAL(draw, page, page);
    battleGridCursor(draw, choice, draw->japanese ? 240 : 230);

    Renderer* renderer = draw->renderer;
    RValue names = GET_SELF(draw, tempitemnameb);
    for (int32_t i = 0; i < 3; ++i) {
        SET_REAL(draw, i, i);
        int32_t slot = page * 6 + i * 2;
        GMLReal y = (draw->y + 375) + i * 30;
        RValue left = GMLArray_get(battleRow(names, slot), draw->turn);
        RValue right = GMLArray_get(battleRow(names, slot + 1), draw->turn);
        renderer->drawColor = BATTLE_WHITE;
        if (draw->japanese) {
            battleDrawJapaneseItems(draw, left, right, y);
        } else {
            battleDrawLabel(draw, left, draw->x + 30, y);
            battleDrawLabel(draw, right, draw->x + 260, y);
        }
    }
    SET_REAL(draw, i, 3);

    if (page == 1 || !battleEqual(draw, GMLArray_get(battleArray(GET_GLOBAL(draw, item)), 6), 0)) {
        GMLReal wave = GMLReal_sin(RValue_toReal(GET_SELF(draw, s_siner)) / (GMLReal)10) * (GMLReal)2;
        if (page == 0) {
            Renderer_drawSprite(renderer, cache.moreArrow, 0, (float)(draw->x + 470), (float)((draw->y + 445) + wave));
        } else {
            Renderer_drawSpriteExt(renderer, cache.moreArrow, 0, (float)(draw->x + 470),
                (float)((draw->y + 395) - wave), 1.0f, -1.0f, 0.0f, BATTLE_WHITE, 1.0f);
        }
    }
    renderer->drawColor = BATTLE_GRAY;
    GMLArray* descriptions = battleRow(GET_SELF(draw, tempitemdescb), page * 6 + choice);
    battleDrawLabel(draw, GMLArray_get(descriptions, draw->turn), draw->x + draw->spellOffset, draw->y + 375);
}

static void battleDrawActs(const BattleDraw* draw) {
    int32_t choice = RValue_toInt32(battleChoice(draw, 9));
    SET_REAL(draw, actcoord, choice);
    SET_REAL(draw, page, 0);
    battleGridCursor(draw, choice, 240);

    Renderer* renderer = draw->renderer;
    int32_t enemy = RValue_toInt32(battleChoice(draw, 11));
    GMLArray* actors = battleRow(GET_GLOBAL(draw, actactor), enemy);
    GMLArray* costs = battleRow(GET_GLOBAL(draw, actcost), enemy);
    GMLArray* names = battleRow(GET_GLOBAL(draw, actname), enemy);
    GMLArray* haveChar = battleArray(GET_SELF(draw, havechar));
    GMLArray* hp = battleArray(GET_GLOBAL(draw, hp));
    RValue tension = GET_GLOBAL(draw, tension);
    RValue zero = RValue_makeReal(0.0);

    for (int32_t i = 0; i < 6; ++i) {
        SET_REAL(draw, i, i);
        RValue actor = GMLArray_get(actors, i);
        bool both = battleEqual(draw, actor, 4);
        bool susie = both || battleEqual(draw, actor, 2);
        bool ralsei = both || battleEqual(draw, actor, 3);
        bool noSusie = susie && (battleEqual(draw, GMLArray_get(haveChar, 1), 0) ||
            battleLess(draw, GMLArray_get(hp, 2), zero) || battleEqual(draw, GMLArray_get(hp, 2), 0));
        bool noRalsei = ralsei && (battleEqual(draw, GMLArray_get(haveChar, 2), 0) ||
            battleLess(draw, GMLArray_get(hp, 3), zero) || battleEqual(draw, GMLArray_get(hp, 3), 0));
        bool cant = noSusie || noRalsei || battleLess(draw, tension, GMLArray_get(costs, i));
        GMLReal charOffset = both ? GMLReal_bankersRound((GMLReal)36 * (GMLReal)1.8) : susie || ralsei ? 36 : 0;
        int32_t xOffset = (i & 1) ? 230 : 0;
        int32_t yOffset = (i / 2) * 30;
        uint32_t susBlend = noSusie ? 8421504 : BATTLE_WHITE;
        SET_REAL(draw, cant, cant);
        Instance_setSelfVar(draw->self, cache.chartime, actor);
        SET_REAL(draw, charoffset, charOffset);
        SET_REAL(draw, xoffset, xOffset);
        SET_REAL(draw, yoffset, yOffset);
        SET_REAL(draw, drawsus, susie);
        SET_REAL(draw, drawral, ralsei);
        SET_REAL(draw, susblend, susBlend);
        SET_REAL(draw, ralblend, noRalsei ? 8421504 : BATTLE_WHITE);

        renderer->drawColor = cant ? BATTLE_GRAY : BATTLE_WHITE;
        GMLReal y = (draw->y + 375) + yOffset;
        if (susie) {
            Renderer_drawSpriteExt(renderer, cache.headSusie, 0, (float)((draw->x + 30) + xOffset),
                (float)y, 1.0f, 1.0f, 0.0f, susBlend, 1.0f);
        }
        if (ralsei) {
            Renderer_drawSpriteExt(renderer, cache.headRalsei, 0, (float)((draw->x + (both ? 60 : 30)) + xOffset),
                (float)y, 1.0f, 1.0f, 0.0f, susBlend, 1.0f);
        }

        PreprocessedText name = battleText(draw, GMLArray_get(names, i));
        GMLReal scale = 1.0;
        if (draw->japanese) {
            GMLReal width = battleTextWidth(draw, name.text);
            SET_REAL(draw, jp_string_lengthtest_act, width);
            if (xOffset == 0 && NativeScript_compareReals((width + 30) + charOffset, 235, draw->epsilon) >= 0) {
                scale = (206 - charOffset) / width;
                SET_REAL(draw, jp_string_xscale, scale);
            }
        }
        battleDrawText(draw, name.text, ((draw->x + 30) + charOffset) + xOffset, y, scale);
        PreprocessedText_free(name);
    }
    SET_REAL(draw, i, 6);

    renderer->drawColor = BATTLE_GRAY;
    GMLArray* descriptions = battleRow(GET_GLOBAL(draw, actdesc), enemy);
    battleDrawLabel(draw, GMLArray_get(descriptions, choice), draw->x + 500, draw->y + 375);
    if (battleLess(draw, zero, GET_GLOBAL(draw, tensionselect))) {
        battleDrawCost(draw, GMLArray_get(costs, choice), draw->x + 500, "obj_battlecontroller_slash_Draw_0_gml_280_0");
    }
    battleFlashTarget(draw, GMLArray_get(battleArray(GET_GLOBAL(draw, monsterinstance)), enemy));
}

static void battleDrawParty(const BattleDraw* draw, int32_t menu) {
    RValue choice = battleChoice(draw, menu);
    battleTargetCursor(draw, choice);

    Renderer* renderer = draw->renderer;
    GMLArray* party = battleArray(GET_GLOBAL(draw, partyChar));
    GMLArray* names = battleArray(GET_GLOBAL(draw, charname));
    GMLArray* hp = battleArray(GET_GLOBAL(draw, hp));
    GMLArray* maxHp = battleArray(GET_GLOBAL(draw, maxhp));
    RValue target = GMLArray_get(battleArray(GET_GLOBAL(draw, charinstance)), RValue_toInt32(choice));
    for (int32_t i = 0; i < 3; ++i) {
        SET_REAL(draw, i, i);
        RValue character = GMLArray_get(party, i);
        if (battleEqual(draw, character, 0)) continue;

        battleFlashTarget(draw, target);
        int32_t who = RValue_toInt32(character);
        renderer->drawColor = BATTLE_WHITE;
        battleDrawLabel(draw, GMLArray_get(names, who), draw->x + 80, (draw->y + 375) + i * 30);
        GMLReal y = (draw->y + 380) + i * 30;
        renderer->drawColor = BATTLE_MAROON;
        battleRectangle(draw, draw->x + 400, y, draw->x + 500, y + 15);
        renderer->drawColor = BATTLE_LIME;
        GMLReal fraction = RValue_toReal(GMLArray_get(hp, who)) / RValue_toReal(GMLArray_get(maxHp, who));
        battleRectangle(draw, draw->x + 400, y, (draw->x + 400) + fraction * (GMLReal)100, y + 15);
    }
    SET_REAL(draw, i, 3);
}

static void battleDrawPanel(const BattleDraw* draw) {
    GMLReal bp = RValue_toReal(GET_SELF(draw, bp));
    GMLReal bpy = RValue_toReal(GET_SELF(draw, bpy));
    GMLReal tp = RValue_toReal(GET_SELF(draw, tp));
    GMLReal tpy = RValue_toReal(GET_SELF(draw, tpy));
    SET_REAL(draw, xx, draw->x);
    SET_REAL(draw, yy, draw->y);
    SET_REAL(draw, tpoff, (tp - tpy) + draw->y);
    SET_REAL(draw, bpoff, (-bp + bpy) + draw->y);

    RValue intro = GET_SELF(draw, intro);
    if (battleEqual(draw, intro, 1)) {
        GMLReal gap = bpy - bp;
        if (NativeScript_compareReals(bp, bpy - 1, draw->epsilon) < 0) {
            bp += NativeScript_compareReals(gap, 40, draw->epsilon) < 0
                ? GMLReal_bankersRound(gap / (GMLReal)2.5) : 30;
        } else {
            bp = bpy;
        }
        SET_REAL(draw, bp, bp);
        if (NativeScript_compareReals(bp, bpy, draw->epsilon) == 0) SET_REAL(draw, intro, 0);
    } else if (battleEqual(draw, intro, 2)) {
        GMLReal gap = bpy - bp;
        if (NativeScript_compareReals(bp, 0, draw->epsilon) > 0) {
            bp -= GMLReal_bankersRound(gap / (GMLReal)5) > 15
                ? GMLReal_bankersRound(gap / (GMLReal)2.5) : 30;
        } else {
            bp = 0;
        }
        SET_REAL(draw, bp, bp);
    }

    Renderer* renderer = draw->renderer;
    GMLReal left = draw->x - 10, right = draw->x + 700;
    GMLReal top = 480 - bp;
    renderer->drawColor = 0;
    battleRectangle(draw, left, 481 + draw->y, right, top + draw->y);
    renderer->drawColor = RValue_toColour(GET_SELF(draw, bcolor));
    battleRectangle(draw, left, (top - 2) + draw->y, right, top + draw->y);
    battleRectangle(draw, left, (top + 34) + draw->y, right, (top + 36) + draw->y);
    RValue result = NativeScript_call(draw->ctx, &cache.charbox, "gml_Script_scr_charbox", nullptr, 0);
    RValue_free(&result);
}

static bool battlecontrollerDraw(VMContext* ctx, RValue* outResult) {
    Runner* runner = ctx->runner;
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr || runner->renderer == nullptr) return false;

    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    BattleDraw draw = {
        .ctx = ctx, .self = self, .globals = ctx->globalScopeInstance, .renderer = runner->renderer,
        .x = camera != nullptr ? (GMLReal)camera->viewX : -1,
        .y = camera != nullptr ? (GMLReal)camera->viewY : -1,
        .epsilon = runner->mathEpsilon,
    };
    RValue lang = GET_GLOBAL(&draw, lang);
    draw.japanese = lang.type == RVALUE_STRING && lang.string != nullptr && strcmp(lang.string, "ja") == 0;
    draw.spellOffset = draw.japanese ? 496 : 500;
    battleDrawPanel(&draw);

    if (battleEqual(&draw, GET_GLOBAL(&draw, myfight), 0)) {
        draw.turn = RValue_toInt32(GET_GLOBAL(&draw, charturn));
        RValue menu = GET_GLOBAL(&draw, bmenuno);
        if (battleEqual(&draw, menu, 1) || battleEqual(&draw, menu, 3) ||
            battleEqual(&draw, menu, 11) || battleEqual(&draw, menu, 12)) {
            battleDrawEnemies(&draw, RValue_toInt32(menu));
        } else if (battleEqual(&draw, menu, 2)) {
            battleDrawSpells(&draw);
        } else if (battleEqual(&draw, menu, 4)) {
            battleDrawItems(&draw);
        } else if (battleEqual(&draw, menu, 9)) {
            battleDrawActs(&draw);
        } else if (battleEqual(&draw, menu, 7) || battleEqual(&draw, menu, 8)) {
            battleDrawParty(&draw, RValue_toInt32(menu));
        }
    }
    *outResult = RValue_makeUndefined();
    return true;
}

static int32_t battleGlobalVarID(VMContext* ctx, const char* name) {
    for (uint32_t i = 0; i < ctx->dataWin->vari.variableCount; ++i) {
        const Variable* var = &ctx->dataWin->vari.variables[i];
        if (var->instanceType == INSTANCE_GLOBAL && var->varID >= 0 && strcmp(var->name, name) == 0) {
            return var->varID;
        }
    }
    return VM_getOrAllocateVarID(ctx, name);
}

static int32_t battleSpriteID(const DataWin* dataWin, const char* name) {
    for (uint32_t i = 0; i < dataWin->sprt.count; ++i) {
        const Sprite* sprite = &dataWin->sprt.sprites[i];
        if (sprite->present && strcmp(sprite->name, name) == 0) return (int32_t)i;
    }
    return -1;
}

void NativeBattlecontrollerDraw_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 1") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    cache.charbox = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.drawFont = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.langString = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.stringWidth = VM_findBuiltin(ctx, "string_width");
    if (cache.stringWidth == nullptr ||
        !NativeScript_canCall(ctx, &cache.charbox, "gml_Script_scr_charbox") ||
        !NativeScript_canCall(ctx, &cache.drawFont, "gml_Script_scr_84_set_draw_font") ||
        !NativeScript_canCall(ctx, &cache.langString, "gml_Script_scr_84_get_lang_string")) return;
    if (!VM_registerNativeScript(ctx, "gml_Object_obj_battlecontroller_Draw_0", battlecontrollerDraw)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
#define RESOLVE_GLOBAL(name) cache.name = battleGlobalVarID(ctx, #name);
    BATTLE_SELF_VARS(RESOLVE_SELF)
    BATTLE_GLOBAL_VARS(RESOLVE_GLOBAL)
#undef RESOLVE_GLOBAL
#undef RESOLVE_SELF

    cache.partyChar = battleGlobalVarID(ctx, "char");
    cache.heart = battleSpriteID(ctx->dataWin, "spr_heart");
    cache.tiredMark = battleSpriteID(ctx->dataWin, "spr_tiredmark");
    cache.spareStar = battleSpriteID(ctx->dataWin, "spr_sparestar");
    cache.moreArrow = battleSpriteID(ctx->dataWin, "spr_morearrow");
    cache.headSusie = battleSpriteID(ctx->dataWin, "spr_headsusie");
    cache.headRalsei = battleSpriteID(ctx->dataWin, "spr_headralsei");
    cache.tiredColor = (uint32_t)Color_lerp(0xFFFF00, 0xFF0000, 0.3f);
}
