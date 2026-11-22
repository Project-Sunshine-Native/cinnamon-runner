#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "stb_ds.h"
#include "text_utils.h"
#include "utils.h"

#include <stdio.h>
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
    BATTLE_NAME_WIDTH_OWNER = 0x28f25,
    BATTLE_ACT_OWNER = 0x28f26,
};

#define BATTLE_SELF_VARS(X) \
    X(xx) X(yy) X(tp) X(tpy) X(tpoff) X(bp) X(bpy) X(bpoff) X(intro) X(bcolor) \
    X(i) X(flash) X(fsiner) X(becomeflash) X(mercydraw) X(tireddraw) \
    X(mnamecolor1) X(mnamecolor2) X(aqcolor) X(namewidth) X(mercyamt) \
    X(thischar) X(spellcoord) X(page) X(icx) X(icy) X(pacify_glow) X(sglowi) \
    X(thiscost) X(itemcoord) \
    X(tempitemnameb) X(tempitemdescb) X(s_siner) X(actcoord) X(cant) X(chartime) \
    X(charoffset) X(xoffset) X(yoffset) X(drawsus) X(drawral) X(susblend) X(ralblend) \
    X(havechar) X(hidemercy) X(cantspare) X(namewidthb) X(hpcolorsoft) \
    X(thisenemy) X(canact) X(acttpcost) X(actsimul) X(actname) X(actdesc) X(drawnoe) X(noeblend) \
    X(gigaqueencon) X(gigaqueentimer) X(gigaqueeny)

#define BATTLE_GLOBAL_VARS(X) \
    X(lang) X(bmenuno) X(myfight) X(bmenucoord) X(charturn) X(monsterinstance) \
    X(monster) X(monsterstatus) X(mercymod) X(mercymax) X(monstername) \
    X(monstercomment) X(monsterhp) X(monstermaxhp) X(encounterno) \
    X(tension) X(spellcost) X(spell) X(spellnameb) X(spelldescb) X(maxtension) \
    X(item) X(actactor) X(hp) X(actcost) X(tensionselect) \
    X(charinstance) X(charname) X(maxhp) X(chapter) X(flag) \
    X(battlespellspecial) X(battlespellcost) X(battlespell) X(battlespellname) X(battlespelldesc) \
    X(canactsus) X(canactral) X(canactnoe) \
    X(actcostsus) X(actcostral) X(actcostnoe) \
    X(actsimulsus) X(actsimulral) X(actsimulnoe) \
    X(actnamesus) X(actnameral) X(actnamenoe) \
    X(actdescsus) X(actdescral) X(actdescnoe) X(monsterx) X(monstery)

static struct {
#define DECLARE_ID(name) int32_t name;
    BATTLE_SELF_VARS(DECLARE_ID)
    BATTLE_GLOBAL_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t g_canact, g_actsimul, g_actname, g_actdesc;
    int32_t partyChar, heart, tiredMark, spareStar, moreArrow, headSusie, headRalsei, headNoelle;
    int32_t gigaQueenObject, queenObject, berdlyObject, spamtonObject;
    int32_t savemeactcon, shieldhp, shieldacthp, shieldactmaxhp, bardlymercy;
    int32_t sactionboxx, sactionboxy;
    uint32_t tiredColor;
    BuiltinFunc stringWidth;
    NativeCallBinding charbox, drawFont, localization;
    BuiltinFunc keyboardCheck;
} cache;

typedef struct {
    VMContext* ctx;
    Instance* self;
    Instance* globals;
    Renderer* renderer;
    GMLReal x, y, epsilon;
    int32_t turn, spellOffset;
    bool japanese;
    Instance* spamton;
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

static void battleDrawCost(const BattleDraw* draw, RValue cost, GMLReal x, bool showZero) {
    GMLReal max = RValue_toReal(GET_GLOBAL(draw, maxtension));
    GMLReal percent = GMLReal_bankersRound((RValue_toReal(cost) / max) * (GMLReal)100);
    SET_REAL(draw, thiscost, percent);
    if (!showZero && percent <= 0) return;

    char label[40];
    snprintf(label, sizeof(label), "%g%% TP", (double)percent);
    draw->renderer->drawColor = BATTLE_ORANGE;
    battleDrawText(draw, label, x, draw->y + 440, 1.0);
}

static void battleDrawPercent(const BattleDraw* draw, GMLReal value, GMLReal x, GMLReal y) {
    char label[40];
    snprintf(label, sizeof(label), "%g%%", (double)GMLReal_ceil(value));
    draw->renderer->vtable->drawText(draw->renderer, label, (float)x, (float)y, 1.0f, 0.5f, 0.0f, -1.0f);
}

static RValue battleLocalize(const BattleDraw* draw, const char* fallback, const char* key) {
    RValue args[] = {RValue_makeString(fallback), RValue_makeString(key)};
    return NativeScript_call(draw->ctx, &cache.localization, "gml_Script_stringsetloc", args, 2);
}

static void battleDrawLocalized(const BattleDraw* draw, const char* fallback, const char* key,
    GMLReal x, GMLReal y, GMLReal scaleX, GMLReal scaleY) {
    RValue value = battleLocalize(draw, fallback, key);
    PreprocessedText label = battleText(draw, value);
    draw->renderer->vtable->drawText(draw->renderer, label.text, (float)x, (float)y,
        (float)scaleX, (float)scaleY, 0.0f, -1.0f);
    PreprocessedText_free(label);
    RValue_free(&value);
}

static void battleDrawWrapped(const BattleDraw* draw, RValue value, GMLReal x, GMLReal y, GMLReal maxWidth) {
    PreprocessedText label = battleText(draw, value);
    GMLReal width = battleTextWidth(draw, label.text);
    GMLReal scale = width >= maxWidth ? maxWidth / width : 1.0;
    battleDrawText(draw, label.text, x, y, scale);
    PreprocessedText_free(label);
}

static Instance* battleObject(const BattleDraw* draw, int32_t id) {
    if (id < 0) return nullptr;
    Instance** instances = draw->ctx->runner->instancesByObject[id];
    for (size_t i = 0; i < arrlenu(instances); ++i) {
        if (instances[i]->active && !instances[i]->destroyed) return instances[i];
    }
    return nullptr;
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
    GMLArray* cannotSpare = battleArray(GET_SELF(draw, cantspare));
    bool hideMercy = battleEqual(draw, GET_SELF(draw, hidemercy), 1);
    RValue target = GMLArray_get(targets, RValue_toInt32(choice));
    bool allowTiredColor = !battleEqual(draw, GET_GLOBAL(draw, encounterno), 31);
    GMLReal nameMax = 0;
    GMLReal nameWidths[3];

    RValue* widths = IntRValueHashMap_getOrInsertUndefined(&draw->self->selfVars, cache.namewidthb);
    draw->ctx->currentArrayOwner = (void*)(intptr_t)BATTLE_NAME_WIDTH_OWNER;
    for (int32_t i = 0; i < 3; ++i) {
        PreprocessedText label = battleText(draw, GMLArray_get(names, i));
        GMLReal width = battleTextWidth(draw, label.text);
        VM_setArrayElement(draw->ctx, widths, i, RValue_makeReal(width));
        nameWidths[i] = width;
        if (width > nameMax) nameMax = width;
        PreprocessedText_free(label);
    }

    for (int32_t i = 0; i < 3; ++i) {
        SET_REAL(draw, i, i);
        battleFlashTarget(draw, target);
        if (!battleEqual(draw, GMLArray_get(monsters, i), 1)) continue;

        renderer->drawColor = BATTLE_WHITE;
        bool tired = battleEqual(draw, GMLArray_get(status, i), 1);
        RValue amount = GMLArray_get(mercy, i);
        RValue max = GMLArray_get(maxMercy, i);
        bool spare = !battleLess(draw, amount, max);
        SET_REAL(draw, mercydraw, spare);
        SET_REAL(draw, tireddraw, tired);
        SET_REAL(draw, aqcolor, cache.tiredColor);

        PreprocessedText name = battleText(draw, GMLArray_get(names, i));
        GMLReal width = nameWidths[i];
        SET_REAL(draw, namewidth, width);
        GMLReal nameX = draw->x + 80;
        GMLReal textY = (draw->y + 375) + i * 30;
        GMLReal barY = (draw->y + 380) + i * 30;
        GMLReal iconY = (draw->y + 385) + i * 30;
        uint32_t color1 = BATTLE_WHITE, color2 = BATTLE_WHITE;
        if (tired) {
            if (allowTiredColor) {
                renderer->drawColor = cache.tiredColor;
                color1 = color2 = cache.tiredColor;
            }
            Renderer_drawSprite(renderer, cache.tiredMark, 0, (float)(nameX + width + 40), (float)iconY);
        }
        if (spare) {
            renderer->drawColor = BATTLE_YELLOW;
            color1 = BATTLE_YELLOW;
            if (!tired) color2 = BATTLE_YELLOW;
            if (!hideMercy) Renderer_drawSprite(renderer, cache.spareStar, 0,
                (float)(nameX + width + 20), (float)iconY);
        }
        SET_REAL(draw, mnamecolor1, color1);
        SET_REAL(draw, mnamecolor2, color2);
        renderer->vtable->drawTextColor(renderer, name.text, (float)nameX, (float)textY,
            1, 1, 0, color1, color2, color2, color1, 1, -1);
        PreprocessedText_free(name);

        if (menu != 13) {
            renderer->drawColor = BATTLE_GRAY;
            RValue comment = GMLArray_get(descriptions, i);
            if (!draw->japanese) {
                battleDrawLabel(draw, comment, nameX + width + 60, textY);
            } else {
                PreprocessedText label = battleText(draw, comment);
                if (80 + width + 60 + battleTextWidth(draw, label.text) / 2 < 415)
                    battleDrawText(draw, label.text, nameX + width + 60, textY, 0.5);
                PreprocessedText_free(label);
            }
            renderer->drawColor = BATTLE_MAROON;
            battleRectangle(draw, draw->x + 420, barY, draw->x + 500, barY + 15);
            renderer->drawColor = BATTLE_LIME;
            GMLReal fraction = RValue_toReal(GMLArray_get(hp, i)) / RValue_toReal(GMLArray_get(maxHp, i));
            battleRectangle(draw, draw->x + 420, barY, (draw->x + 420) + fraction * (GMLReal)80, barY + 15);
            renderer->drawColor = BATTLE_WHITE;
            battleDrawLocalized(draw, "HP", "obj_battlecontroller_slash_Draw_0_gml_173_0",
                draw->x + 424, draw->y + 364, 1, 0.5);
            battleDrawPercent(draw, fraction * 100, draw->x + 424, barY);
        } else {
            int32_t character = RValue_toInt32(GMLArray_get(battleArray(GET_GLOBAL(draw, partyChar)), draw->turn));
            RValue plain = battleLocalize(draw, "Standard", "obj_battlecontroller_slash_Draw_0_gml_172_0");
            RValue action = plain;
            switch (character) {
                case 2: action = GMLArray_get(battleRow(GET_GLOBAL(draw, actnamesus), i),
                    RValue_toInt32(battleChoice(draw, 2))); break;
                case 3: action = GMLArray_get(battleRow(GET_GLOBAL(draw, actnameral), i),
                    RValue_toInt32(battleChoice(draw, 2))); break;
                case 4: action = GMLArray_get(battleRow(GET_GLOBAL(draw, actnamenoe), i),
                    RValue_toInt32(battleChoice(draw, 2))); break;
                default: break;
            }
            if (action.type == RVALUE_STRING && action.string != nullptr &&
                (strcmp(action.string, "S-Action") == 0 || strcmp(action.string, "R-Action") == 0 ||
                 strcmp(action.string, "N-Action") == 0)) action = plain;
            renderer->drawColor = RValue_toColour(GMLArray_get(battleArray(GET_SELF(draw, hpcolorsoft)), character - 1));
            battleDrawWrapped(draw, action, nameX + nameMax + 60, textY, 514 - (80 + nameMax + 60));
            RValue_free(&plain);
        }

        GMLReal mercyAmount = RValue_toReal(amount);
        if (mercyAmount >= 100) mercyAmount = 100;
        SET_REAL(draw, mercyamt, mercyAmount);
        if (hideMercy) continue;

        GMLReal fraction = RValue_toReal(amount) / RValue_toReal(max);
        GMLReal mercyPercent = GMLReal_ceil(fraction * 100);
        if (mercyPercent > 100) mercyPercent = 100;
        bool cannot = battleEqual(draw, GMLArray_get(cannotSpare, i), 1);
        renderer->drawColor = (uint32_t)Color_lerp(BATTLE_ORANGE, 0x0000FF, 0.5f);
        battleRectangle(draw, draw->x + 520, barY, draw->x + 600, barY + 15);
        renderer->drawColor = BATTLE_YELLOW;
        if (mercyAmount > 0 && !cannot) {
            battleRectangle(draw, draw->x + 520, barY, draw->x + 520 + mercyPercent * (GMLReal)0.8, barY + 15);
        }
        renderer->drawColor = BATTLE_WHITE;
        battleDrawLocalized(draw, "MERCY", "obj_battlecontroller_slash_Draw_0_gml_208_0",
            draw->x + 524, draw->y + 364, draw->japanese ? 0.5 : 1.0, 0.5);
        renderer->drawColor = BATTLE_MAROON;
        if (!cannot) battleDrawPercent(draw, mercyPercent, draw->x + 524, barY);
        else {
            renderer->vtable->drawLine(renderer, (float)(draw->x + 519), (float)barY,
                (float)(draw->x + 600), (float)(barY + 15), 2, BATTLE_MAROON, renderer->drawAlpha);
            renderer->vtable->drawLine(renderer, (float)(draw->x + 519), (float)(barY + 15),
                (float)(draw->x + 600), (float)barY, 2, BATTLE_MAROON, renderer->drawAlpha);
        }
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
    bool legacy = battleEqual(draw, GMLArray_get(battleArray(GET_GLOBAL(draw, flag)), 34), 1);
    int32_t who = legacy
        ? RValue_toInt32(GMLArray_get(battleArray(GET_GLOBAL(draw, partyChar)), draw->turn)) : draw->turn;
    SET_REAL(draw, thischar, who);
    int32_t choice = RValue_toInt32(battleChoice(draw, 2));
    int32_t page = choice > 5;
    choice -= page * 6;
    SET_REAL(draw, spellcoord, choice);
    SET_REAL(draw, page, page);
    battleGridCursor(draw, choice, 230);

    RValue costs = legacy ? GET_GLOBAL(draw, spellcost) : GET_GLOBAL(draw, battlespellcost);
    RValue spells = legacy ? GET_GLOBAL(draw, spell) : GET_GLOBAL(draw, battlespell);
    RValue names = legacy ? GET_GLOBAL(draw, spellnameb) : GET_GLOBAL(draw, battlespellname);
    RValue descriptions = legacy ? GET_GLOBAL(draw, spelldescb) : GET_GLOBAL(draw, battlespelldesc);
    GMLArray* costRow = battleRow(costs, who);
    GMLArray* spellRow = battleRow(spells, who);
    GMLArray* nameRow = battleRow(names, who);
    GMLArray* specialRow = legacy ? nullptr : battleRow(GET_GLOBAL(draw, battlespellspecial), who);
    RValue tension = GET_GLOBAL(draw, tension);
    uint32_t specialColor = BATTLE_WHITE;
    if (!legacy) {
        int32_t character = RValue_toInt32(GMLArray_get(battleArray(GET_GLOBAL(draw, partyChar)), who));
        specialColor = RValue_toColour(GMLArray_get(battleArray(GET_SELF(draw, hpcolorsoft)), character - 1));
    }
    Renderer* renderer = draw->renderer;

    for (int32_t i = 0; i < 3; ++i) {
        SET_REAL(draw, i, i);
        GMLReal y = (draw->y + 375) + i * 30;
        for (int32_t col = 0; col < 2; ++col) {
            int32_t slot = page * 6 + i * 2 + col;
            renderer->drawColor = BATTLE_WHITE;
            if (!legacy && !battleLess(draw, GMLArray_get(specialRow, slot), RValue_makeReal(1))) {
                renderer->drawColor = specialColor;
            }
            if (battleLess(draw, tension, GMLArray_get(costRow, slot))) {
                renderer->drawColor = BATTLE_GRAY;
            } else if ((!legacy || col == 0) && (battleEqual(draw, GMLArray_get(spellRow, slot), 3) ||
                        battleEqual(draw, GMLArray_get(spellRow, slot), 8)) && battlePacifyGlow(draw)) {
                renderer->drawColor = cache.tiredColor;
            }
            battleDrawLabel(draw, GMLArray_get(nameRow, slot), draw->x + (col == 0 ? 30 : 260), y);
        }
    }
    SET_REAL(draw, i, 3);

    int32_t slot = page * 6 + choice;
    renderer->drawColor = BATTLE_GRAY;
    battleDrawLabel(draw, GMLArray_get(battleRow(descriptions, who), slot),
        draw->x + draw->spellOffset, draw->y + 375);
    battleDrawCost(draw, GMLArray_get(costRow, slot), draw->x + draw->spellOffset, legacy);
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
        renderer->drawColor = BATTLE_WHITE;
        for (int32_t col = 0; col < 2; ++col) {
            RValue value = GMLArray_get(battleRow(names, slot + col), draw->turn);
            PreprocessedText label = battleText(draw, value);
            GMLReal width = battleTextWidth(draw, label.text);
            GMLReal scale = width > 200 ? (GMLReal)200 / width : 1.0;
            battleDrawText(draw, label.text, draw->x + (col == 0 ? 30 : 260), y, scale);
            PreprocessedText_free(label);
        }
    }
    SET_REAL(draw, i, 3);

    if (page == 1 || !battleEqual(draw, GMLArray_get(battleArray(GET_GLOBAL(draw, item)), 6), 0)) {
        GMLReal wave = GMLReal_sin(RValue_toReal(GET_SELF(draw, s_siner)) / (GMLReal)10) * (GMLReal)2;
        if (page == 0) Renderer_drawSprite(renderer, cache.moreArrow, 0,
            (float)(draw->x + 470), (float)((draw->y + 445) + wave));
        else Renderer_drawSpriteExt(renderer, cache.moreArrow, 0,
            (float)(draw->x + 470), (float)((draw->y + 395) - wave), 1, -1, 0, BATTLE_WHITE, 1);
    }
    renderer->drawColor = BATTLE_GRAY;
    GMLArray* descriptions = battleRow(GET_SELF(draw, tempitemdescb), page * 6 + choice);
    battleDrawLabel(draw, GMLArray_get(descriptions, draw->turn), draw->x + draw->spellOffset, draw->y + 375);
}

static void battleDrawActs(const BattleDraw* draw) {
    int32_t choice = RValue_toInt32(battleChoice(draw, 9));
    SET_REAL(draw, actcoord, choice);
    SET_REAL(draw, page, 0);

    int32_t character = RValue_toInt32(GMLArray_get(battleArray(GET_GLOBAL(draw, partyChar)), draw->turn));
    int32_t enemy = RValue_toInt32(GET_SELF(draw, thisenemy));
    int32_t ids[4][5] = {
        {cache.g_canact, cache.actcost, cache.g_actsimul, cache.g_actname, cache.g_actdesc},
        {cache.canactsus, cache.actcostsus, cache.actsimulsus, cache.actnamesus, cache.actdescsus},
        {cache.canactral, cache.actcostral, cache.actsimulral, cache.actnameral, cache.actdescral},
        {cache.canactnoe, cache.actcostnoe, cache.actsimulnoe, cache.actnamenoe, cache.actdescnoe},
    };
    const int32_t destination[5] = {cache.canact, cache.acttpcost, cache.actsimul, cache.actname, cache.actdesc};
    GMLArray* source[5] = {nullptr};
    if (character >= 1 && character <= 4) {
        for (int32_t j = 0; j < 5; ++j)
            source[j] = battleRow(Instance_getSelfVar(draw->globals, ids[character - 1][j]), enemy);
    }
    for (int32_t j = 0; j < 5; ++j) {
        RValue* destinationArray = IntRValueHashMap_getOrInsertUndefined(&draw->self->selfVars, destination[j]);
        draw->ctx->currentArrayOwner = (void*)(intptr_t)(BATTLE_ACT_OWNER + j);
        for (int32_t i = 0; i < 6; ++i) {
            RValue value = (j == 0 && source[j] == nullptr) ? RValue_makeReal(0) : GMLArray_get(source[j], i);
            VM_setArrayElement(draw->ctx, destinationArray, i, value);
        }
    }

    battleGridCursor(draw, choice, 230);
    Renderer* renderer = draw->renderer;
    GMLArray* actors = battleRow(GET_GLOBAL(draw, actactor), RValue_toInt32(battleChoice(draw, 11)));
    GMLArray* costs = battleArray(GET_SELF(draw, acttpcost));
    GMLArray* names = battleArray(GET_SELF(draw, actname));
    GMLArray* descriptions = battleArray(GET_SELF(draw, actdesc));
    GMLArray* haveChar = battleArray(GET_SELF(draw, havechar));
    GMLArray* hp = battleArray(GET_GLOBAL(draw, hp));
    RValue tension = GET_GLOBAL(draw, tension);

    for (int32_t i = 0; i < 6; ++i) {
        SET_REAL(draw, i, i);
        RValue actor = character == 1 ? GMLArray_get(actors, i) : RValue_makeReal(0);
        bool both = battleEqual(draw, actor, 4);
        bool susie = both || battleEqual(draw, actor, 2);
        bool ralsei = both || battleEqual(draw, actor, 3);
        bool noelle = battleEqual(draw, actor, 5);
        bool noSusie = susie && (battleEqual(draw, GMLArray_get(haveChar, 1), 0) ||
            !battleLess(draw, RValue_makeReal(0), GMLArray_get(hp, 2)));
        bool noRalsei = ralsei && (battleEqual(draw, GMLArray_get(haveChar, 2), 0) ||
            !battleLess(draw, RValue_makeReal(0), GMLArray_get(hp, 3)));
        bool noNoelle = noelle && (battleEqual(draw, GMLArray_get(haveChar, 3), 0) ||
            !battleLess(draw, RValue_makeReal(0), GMLArray_get(hp, 4)));
        bool cant = noSusie || noRalsei || noNoelle || battleLess(draw, tension, GMLArray_get(costs, i));
        GMLReal charOffset = both ? 60 : susie || ralsei || noelle ? 30 : 0;
        int32_t xOffset = (i & 1) ? 230 : 0;
        int32_t yOffset = (i / 2) * 30;
        uint32_t susBlend = noSusie ? BATTLE_GRAY : BATTLE_WHITE;
        uint32_t ralBlend = noRalsei ? BATTLE_GRAY : BATTLE_WHITE;
        uint32_t noeBlend = noNoelle ? BATTLE_GRAY : BATTLE_WHITE;
        SET_REAL(draw, cant, cant);
        Instance_setSelfVar(draw->self, cache.chartime, actor);
        SET_REAL(draw, charoffset, charOffset);
        SET_REAL(draw, xoffset, xOffset);
        SET_REAL(draw, yoffset, yOffset);
        SET_REAL(draw, drawsus, susie);
        SET_REAL(draw, drawral, ralsei);
        SET_REAL(draw, drawnoe, noelle);
        SET_REAL(draw, susblend, susBlend);
        SET_REAL(draw, ralblend, ralBlend);
        SET_REAL(draw, noeblend, noeBlend);

        renderer->drawColor = cant ? BATTLE_GRAY : BATTLE_WHITE;
        GMLReal y = (draw->y + 375) + yOffset;
        if (susie) Renderer_drawSpriteExt(renderer, cache.headSusie, 0,
            (float)((draw->x + 30) + xOffset), (float)y, 1, 1, 0, susBlend, 1);
        if (ralsei) Renderer_drawSpriteExt(renderer, cache.headRalsei, 0,
            (float)((draw->x + (both ? 60 : 30)) + xOffset), (float)y, 1, 1, 0, susBlend, 1);
        if (noelle) Renderer_drawSpriteExt(renderer, cache.headNoelle, 0,
            (float)((draw->x + 30) + xOffset), (float)y, 1, 1, 0, noeBlend, 1);

        if (draw->spamton != nullptr) {
            GMLReal progress = RValue_toReal(Instance_getSelfVar(draw->spamton, cache.savemeactcon));
            int32_t sprite = progress > 4 ? cache.headNoelle : progress > 2 ? cache.headSusie : cache.headRalsei;
            if (progress > 0) Renderer_drawSpriteExt(renderer, sprite, 0, (float)(draw->x + 30),
                (float)(draw->y + 380), 1, 1, 0, noeBlend, 1);
        }

        PreprocessedText name = battleText(draw, GMLArray_get(names, i));
        GMLReal width = battleTextWidth(draw, name.text);
        if (width < 1) width = 1;
        GMLReal scale = (206 - charOffset) / width;
        if (scale > 1) scale = 1;
        if (scale < (GMLReal)0.5) scale = (GMLReal)0.5;
        battleDrawText(draw, name.text, draw->x + 30 + charOffset + xOffset, y, scale);
        PreprocessedText_free(name);
    }
    SET_REAL(draw, i, 6);

    renderer->drawColor = BATTLE_GRAY;
    battleDrawLabel(draw, GMLArray_get(descriptions, choice), draw->x + 500, draw->y + 375);
    if (battleLess(draw, RValue_makeReal(0), GET_GLOBAL(draw, tensionselect))) {
        battleDrawCost(draw, GMLArray_get(costs, choice), draw->x + 500, true);
    }
    battleFlashTarget(draw, GMLArray_get(battleArray(GET_GLOBAL(draw, monsterinstance)),
        RValue_toInt32(battleChoice(draw, 11))));
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

static void battleDrawActionHints(const BattleDraw* draw) {
    if (!battleEqual(draw, GMLArray_get(battleArray(GET_GLOBAL(draw, flag)), 34), 0) ||
        !battleEqual(draw, GET_GLOBAL(draw, myfight), 0)) return;
    int32_t menu = RValue_toInt32(GET_GLOBAL(draw, bmenuno));
    if (menu != 11 && menu != 13 && menu != 2) return;

    RValue key = RValue_makeReal(65);
    if (!RValue_toBool(cache.keyboardCheck(draw->ctx, &key, 1))) return;

    GMLArray* party = battleArray(GET_GLOBAL(draw, partyChar));
    bool present[3] = {false};
    for (int32_t i = 0; i < 3; ++i) {
        int32_t character = RValue_toInt32(GMLArray_get(party, i));
        if (character >= 2 && character <= 4) present[character - 2] = true;
    }
    const int32_t enabledIds[3] = {cache.canactsus, cache.canactral, cache.canactnoe};
    const int32_t namesIds[3] = {cache.actnamesus, cache.actnameral, cache.actnamenoe};
    const char* const prefixes[3] = {"S-Action", "R-Action", "N-Action"};
    GMLArray* monsters = battleArray(GET_GLOBAL(draw, monster));
    GMLArray* instances = battleArray(GET_GLOBAL(draw, monsterinstance));
    GMLArray* monsterXs = battleArray(GET_GLOBAL(draw, monsterx));
    GMLArray* monsterYs = battleArray(GET_GLOBAL(draw, monstery));
    GMLArray* colors = battleArray(GET_SELF(draw, hpcolorsoft));
    Renderer* renderer = draw->renderer;
    int32_t oldFont = renderer->drawFont;

    for (int32_t i = 0; i < 3; ++i) {
        SET_REAL(draw, i, i);
        if (!RValue_toBool(GMLArray_get(monsters, i))) continue;
        GMLReal offsetX = 0, offsetY = 0, y = 0, maxWidth = 0;
        RValue target = GMLArray_get(instances, i);
        if (target.type != RVALUE_UNDEFINED && RValue_toInt32(target) >= INSTANCE_ID_BASE) {
            int32_t id = RValue_toInt32(target);
            Instance* enemy = hmget(draw->ctx->runner->instancesById, id);
            if (enemy != nullptr && enemy->active && !enemy->destroyed) {
                offsetX = RValue_toReal(Instance_getSelfVar(enemy, cache.sactionboxx));
                offsetY = RValue_toReal(Instance_getSelfVar(enemy, cache.sactionboxy));
            }
        }
        RValue font = RValue_makeString("main");
        RValue result = NativeScript_call(draw->ctx, &cache.drawFont, "gml_Script_scr_84_set_draw_font", &font, 1);
        RValue_free(&result);

        GMLReal x = RValue_toReal(GMLArray_get(monsterXs, i));
        GMLReal top = RValue_toReal(GMLArray_get(monsterYs, i));
        for (int32_t character = 0; character < 3; ++character) {
            if (!present[character]) continue;
            renderer->drawColor = RValue_toColour(GMLArray_get(colors, character + 1));
            GMLArray* enabled = battleRow(Instance_getSelfVar(draw->globals, enabledIds[character]), i);
            GMLArray* names = battleRow(Instance_getSelfVar(draw->globals, namesIds[character]), i);
            for (int32_t j = 0; j < 3; ++j) {
                if (!battleEqual(draw, GMLArray_get(enabled, j), 1)) continue;
                RValue value = GMLArray_get(names, j);
                PreprocessedText name = battleText(draw, value);
                GMLReal width = battleTextWidth(draw, name.text) - 45;
                if (width > maxWidth) maxWidth = width;
                bool defaultName = value.type == RVALUE_STRING && value.string != nullptr &&
                    strcmp(value.string, prefixes[character]) == 0;
                if (defaultName && character == 0) {
                    battleDrawText(draw, name.text, x - 200 + offsetX, top + y + offsetY, 1);
                } else {
                    char label[256];
                    snprintf(label, sizeof(label), "%s - %s", prefixes[character],
                        defaultName ? "Standard" : name.text);
                    battleDrawText(draw, label, x - 200 + offsetX, top + y + offsetY, 1);
                }
                PreprocessedText_free(name);
                y += 18;
            }
        }
        if (y > 0) {
            renderer->drawColor = 0xFF00FF;
            renderer->vtable->drawRectangle(renderer, (float)(x - 210 + offsetX), (float)(top + offsetY),
                (float)(x - 80 + maxWidth + offsetX), (float)(top + y + offsetY),
                renderer->drawColor, renderer->drawAlpha, true);
        }
    }
    renderer->drawFont = oldFont;
}

static void battleDrawQueen(const BattleDraw* draw) {
    if (!battleEqual(draw, GET_GLOBAL(draw, chapter), 2) ||
        !battleEqual(draw, GET_GLOBAL(draw, myfight), 0)) return;
    int32_t menu = RValue_toInt32(GET_GLOBAL(draw, bmenuno));
    if (menu != 1 && menu != 3 && menu != 11 && menu != 12 && menu != 13) return;

    Instance* queen = battleObject(draw, cache.queenObject);
    if (queen == nullptr) return;
    Renderer* renderer = draw->renderer;
    GMLReal value;
    if (battleObject(draw, cache.berdlyObject) != nullptr) {
        value = RValue_toReal(Instance_getSelfVar(queen, cache.bardlymercy));
        renderer->drawColor = BATTLE_GRAY;
        battleDrawLocalized(draw, "Berdly", "obj_battlecontroller_slash_Draw_0_gml_653_0",
            draw->x + 80, draw->y + 407, 1, 1);
    } else {
        GMLReal current = RValue_toReal(Instance_getSelfVar(queen, cache.shieldacthp));
        GMLReal maximum = RValue_toReal(Instance_getSelfVar(queen, cache.shieldactmaxhp));
        value = (current / maximum) * 10;
        renderer->drawColor = BATTLE_GRAY;
        battleDrawLocalized(draw, "Shield", "obj_queen_enemy_slash_Draw_0_gml_67_0",
            draw->x + 80, draw->y + 407, 1, 1);
    }
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    renderer->drawColor = BATTLE_WHITE;
    renderer->vtable->drawRectangle(renderer, (float)(draw->x + 520), (float)(draw->y + 418),
        (float)(draw->x + 600), (float)(draw->y + 433), BATTLE_GRAY, renderer->drawAlpha, false);
    renderer->vtable->drawRectangle(renderer, (float)(draw->x + 520), (float)(draw->y + 418),
        (float)(draw->x + 520 + value * (GMLReal)0.8), (float)(draw->y + 433), BATTLE_YELLOW,
        renderer->drawAlpha, false);

    if (!draw->japanese) {
        bool protected = RValue_toReal(Instance_getSelfVar(queen, cache.shieldhp)) > 1;
        GMLReal x = draw->x + 210 + (draw->turn > 0 && menu == 13 ? 100 : 0);
        battleDrawLocalized(draw, protected ? "(Protected)" : "(Defenseless)",
            protected ? "obj_queen_enemy_slash_Draw_0_gml_78_0" : "obj_queen_enemy_slash_Draw_0_gml_82_0",
            x, draw->y + 376, 1, 1);
    }
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
    battleRectangle(draw, left, 481 + draw->y, right, (top - 4) + draw->y);
    renderer->drawColor = RValue_toColour(GET_SELF(draw, bcolor));
    battleRectangle(draw, left, (top - 3) + draw->y, right, (top - 2) + draw->y);
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
    if (battleEqual(&draw, GET_GLOBAL(&draw, chapter), 2) &&
        battleObject(&draw, cache.gigaQueenObject) != nullptr) {
        int32_t state = RValue_toInt32(GET_SELF(&draw, gigaqueencon));
        GMLReal timer = RValue_toReal(GET_SELF(&draw, gigaqueentimer));
        GMLReal movement = RValue_toReal(GET_SELF(&draw, gigaqueeny));
        if (state == 1 || state == 2 || state == 4 || state == 5) {
            timer += 1;
            SET_REAL(&draw, gigaqueentimer, timer);
            GMLReal start = state == 1 || state == 5 ? 0 : state == 2 ? 100 : 200;
            GMLReal end = state == 1 ? 100 : state == 5 ? 200 : 0;
            movement = start + (end - start) * (timer / 10);
            SET_REAL(&draw, gigaqueeny, movement);
            if (timer == 10) SET_REAL(&draw, gigaqueencon, 0);
        } else if (state == 3) {
            movement = 200;
            SET_REAL(&draw, gigaqueeny, movement);
        }
        draw.y += movement;
    }
    draw.spamton = battleEqual(&draw, GET_GLOBAL(&draw, chapter), 2)
        ? battleObject(&draw, cache.spamtonObject) : nullptr;
    battleDrawPanel(&draw);

    if (battleEqual(&draw, GET_GLOBAL(&draw, myfight), 0)) {
        draw.turn = RValue_toInt32(GET_GLOBAL(&draw, charturn));
        RValue menu = GET_GLOBAL(&draw, bmenuno);
        if (battleEqual(&draw, menu, 1) || battleEqual(&draw, menu, 3) ||
            battleEqual(&draw, menu, 11) || battleEqual(&draw, menu, 12) || battleEqual(&draw, menu, 13)) {
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
    battleDrawActionHints(&draw);
    battleDrawQueen(&draw);
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

void NativeChapter2BattlecontrollerDraw_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 2") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    cache.charbox = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.drawFont = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.localization = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.stringWidth = VM_findBuiltin(ctx, "string_width");
    cache.keyboardCheck = VM_findBuiltin(ctx, "keyboard_check");
    if (cache.stringWidth == nullptr || cache.keyboardCheck == nullptr ||
        !NativeScript_canCall(ctx, &cache.charbox, "gml_Script_scr_charbox") ||
        !NativeScript_canCall(ctx, &cache.drawFont, "gml_Script_scr_84_set_draw_font") ||
        !NativeScript_canCall(ctx, &cache.localization, "gml_Script_stringsetloc")) return;
    if (!VM_registerNativeScript(ctx, "gml_Object_obj_battlecontroller_Draw_0", battlecontrollerDraw)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
#define RESOLVE_GLOBAL(name) cache.name = battleGlobalVarID(ctx, #name);
    BATTLE_SELF_VARS(RESOLVE_SELF)
    BATTLE_GLOBAL_VARS(RESOLVE_GLOBAL)
#undef RESOLVE_GLOBAL
#undef RESOLVE_SELF

    cache.g_canact = battleGlobalVarID(ctx, "canact");
    cache.g_actsimul = battleGlobalVarID(ctx, "actsimul");
    cache.g_actname = battleGlobalVarID(ctx, "actname");
    cache.g_actdesc = battleGlobalVarID(ctx, "actdesc");
    cache.partyChar = battleGlobalVarID(ctx, "char");
    cache.heart = battleSpriteID(ctx->dataWin, "spr_heart");
    cache.tiredMark = battleSpriteID(ctx->dataWin, "spr_tiredmark");
    cache.spareStar = battleSpriteID(ctx->dataWin, "spr_sparestar");
    cache.moreArrow = battleSpriteID(ctx->dataWin, "spr_morearrow");
    cache.headSusie = battleSpriteID(ctx->dataWin, "spr_headsusie");
    cache.headRalsei = battleSpriteID(ctx->dataWin, "spr_headralsei");
    cache.headNoelle = battleSpriteID(ctx->dataWin, "spr_headnoelle");
    cache.gigaQueenObject = -1;
    cache.queenObject = -1;
    cache.berdlyObject = -1;
    cache.spamtonObject = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (!object->present) continue;
        const char* name = object->name;
        if (strcmp(name, "obj_gigaqueen_enemy") == 0) cache.gigaQueenObject = (int32_t)i;
        else if (strcmp(name, "obj_queen_enemy") == 0) cache.queenObject = (int32_t)i;
        else if (strcmp(name, "obj_berdlyplug_enemy") == 0) cache.berdlyObject = (int32_t)i;
        else if (strcmp(name, "obj_spamton_neo_enemy") == 0) cache.spamtonObject = (int32_t)i;
    }
    cache.savemeactcon = NativeScript_getSelfVarID(ctx, "savemeactcon");
    cache.shieldhp = NativeScript_getSelfVarID(ctx, "shieldhp");
    cache.shieldacthp = NativeScript_getSelfVarID(ctx, "shieldacthp");
    cache.shieldactmaxhp = NativeScript_getSelfVarID(ctx, "shieldactmaxhp");
    cache.bardlymercy = NativeScript_getSelfVarID(ctx, "bardlymercy");
    cache.sactionboxx = NativeScript_getSelfVarID(ctx, "sactionboxx");
    cache.sactionboxy = NativeScript_getSelfVarID(ctx, "sactionboxy");
    cache.tiredColor = (uint32_t)Color_lerp(0xFFFF00, 0xFF0000, 0.3f);
}
