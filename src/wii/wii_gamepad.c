#include "wii_gamepad.h"

#include "../runner_keyboard.h"

#include <gccore.h>
#include <math.h>
#include <ogc/pad.h>
#include <stdio.h>
#include <string.h>
#include <wiiuse/wpad.h>

#define WII_WPAD_CHANNEL_COUNT 4u
#define WII_GAMECUBE_CHANNEL_COUNT 4u
#define WII_CONTROLLER_CANDIDATE_COUNT (WII_WPAD_CHANNEL_COUNT + WII_GAMECUBE_CHANNEL_COUNT)
#define WII_STICK_DEADZONE 0.20f
#define WII_DEG_TO_RAD 0.01745329251994329577f

typedef struct {
    uint32_t wpadButton;
    uint32_t mappingIndex;
} WiiWpadKeyMap;

typedef struct {
    uint16_t padButton;
    uint32_t mappingIndex;
} WiiPadKeyMap;

typedef struct {
    WiiControllerKind kind;
    uint32_t channel;
    uint32_t held;
    expansion_t expansion;
    PADStatus padStatus;
    bool hasHeldInput;
    char description[64];
    char guid[64];
} WiiControllerCandidate;

static char gPrimaryGamepadGuid[64];
static bool gGameCubeStartHeld[WII_GAMECUBE_CHANNEL_COUNT];

static const WiiWpadKeyMap WPAD_KEYS[] = {
    {WPAD_BUTTON_LEFT, WII_WIIMOTE_DPAD_LEFT},
    {WPAD_BUTTON_DOWN, WII_WIIMOTE_DPAD_DOWN},
    {WPAD_BUTTON_UP, WII_WIIMOTE_DPAD_UP},
    {WPAD_BUTTON_RIGHT, WII_WIIMOTE_DPAD_RIGHT},
    {WPAD_BUTTON_A, WII_WIIMOTE_A},
    {WPAD_BUTTON_PLUS, WII_WIIMOTE_PLUS},
    {WPAD_BUTTON_MINUS, WII_WIIMOTE_MINUS},
    {WPAD_BUTTON_1, WII_WIIMOTE_ONE},
    {WPAD_BUTTON_2, WII_WIIMOTE_TWO},
    {WPAD_BUTTON_B, WII_WIIMOTE_B},
};

static const WiiWpadKeyMap WPAD_CLASSIC_KEYS[] = {
    {WPAD_CLASSIC_BUTTON_LEFT, WII_CLASSIC_DPAD_LEFT},
    {WPAD_CLASSIC_BUTTON_DOWN, WII_CLASSIC_DPAD_DOWN},
    {WPAD_CLASSIC_BUTTON_UP, WII_CLASSIC_DPAD_UP},
    {WPAD_CLASSIC_BUTTON_RIGHT, WII_CLASSIC_DPAD_RIGHT},
    {WPAD_CLASSIC_BUTTON_A, WII_CLASSIC_A},
    {WPAD_CLASSIC_BUTTON_B, WII_CLASSIC_B},
    {WPAD_CLASSIC_BUTTON_X, WII_CLASSIC_X},
    {WPAD_CLASSIC_BUTTON_Y, WII_CLASSIC_Y},
    {WPAD_CLASSIC_BUTTON_MINUS, WII_CLASSIC_MINUS},
    {WPAD_CLASSIC_BUTTON_PLUS, WII_CLASSIC_PLUS},
    {WPAD_CLASSIC_BUTTON_FULL_L, WII_CLASSIC_L},
    {WPAD_CLASSIC_BUTTON_FULL_R, WII_CLASSIC_R},
    {WPAD_CLASSIC_BUTTON_ZL, WII_CLASSIC_ZL},
    {WPAD_CLASSIC_BUTTON_ZR, WII_CLASSIC_ZR},
};

static const WiiPadKeyMap PAD_KEYS[] = {
    {PAD_BUTTON_LEFT, WII_GAMECUBE_DPAD_LEFT},
    {PAD_BUTTON_DOWN, WII_GAMECUBE_DPAD_DOWN},
    {PAD_BUTTON_RIGHT, WII_GAMECUBE_DPAD_RIGHT},
    {PAD_BUTTON_UP, WII_GAMECUBE_DPAD_UP},
    {PAD_BUTTON_A, WII_GAMECUBE_A},
    {PAD_BUTTON_B, WII_GAMECUBE_B},
    {PAD_BUTTON_X, WII_GAMECUBE_X},
    {PAD_BUTTON_Y, WII_GAMECUBE_Y},
    {PAD_TRIGGER_L, WII_GAMECUBE_LEFT_TRIGGER},
    {PAD_TRIGGER_R, WII_GAMECUBE_RIGHT_TRIGGER},
    {PAD_TRIGGER_Z, WII_GAMECUBE_Z},
};

void WiiPadCfg_init(WiiGamepadConfig* config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    for (uint32_t i = 0u; i < WII_INPUT_CONTROLLER_COUNT; ++i) {
        config->controllerKind[i] = i < 2u ? WII_CONTROLLER_WIIMOTE : WII_CONTROLLER_GAMECUBE;
        config->controller[i].wiimoteSideways = true;

        config->controller[i].wiimote[WII_WIIMOTE_DPAD_LEFT] = VK_LEFT;
        config->controller[i].wiimote[WII_WIIMOTE_DPAD_DOWN] = VK_DOWN;
        config->controller[i].wiimote[WII_WIIMOTE_DPAD_UP] = VK_UP;
        config->controller[i].wiimote[WII_WIIMOTE_DPAD_RIGHT] = VK_RIGHT;
        config->controller[i].wiimote[WII_WIIMOTE_A] = -1;
        config->controller[i].wiimote[WII_WIIMOTE_PLUS] = 'C';
        config->controller[i].wiimote[WII_WIIMOTE_MINUS] = 'C';
        config->controller[i].wiimote[WII_WIIMOTE_ONE] = 'X';
        config->controller[i].wiimote[WII_WIIMOTE_TWO] = 'Z';
        config->controller[i].wiimote[WII_WIIMOTE_B] = -1;

        config->controller[i].classic[WII_CLASSIC_DPAD_LEFT] = VK_LEFT;
        config->controller[i].classic[WII_CLASSIC_DPAD_DOWN] = VK_DOWN;
        config->controller[i].classic[WII_CLASSIC_DPAD_UP] = VK_UP;
        config->controller[i].classic[WII_CLASSIC_DPAD_RIGHT] = VK_RIGHT;
        config->controller[i].classic[WII_CLASSIC_LEFT_STICK_LEFT] = VK_LEFT;
        config->controller[i].classic[WII_CLASSIC_LEFT_STICK_DOWN] = VK_DOWN;
        config->controller[i].classic[WII_CLASSIC_LEFT_STICK_UP] = VK_UP;
        config->controller[i].classic[WII_CLASSIC_LEFT_STICK_RIGHT] = VK_RIGHT;
        config->controller[i].classic[WII_CLASSIC_RIGHT_STICK_LEFT] = -1;
        config->controller[i].classic[WII_CLASSIC_RIGHT_STICK_DOWN] = -1;
        config->controller[i].classic[WII_CLASSIC_RIGHT_STICK_UP] = -1;
        config->controller[i].classic[WII_CLASSIC_RIGHT_STICK_RIGHT] = -1;
        config->controller[i].classic[WII_CLASSIC_A] = 'Z';
        config->controller[i].classic[WII_CLASSIC_B] = 'X';
        config->controller[i].classic[WII_CLASSIC_X] = 'C';
        config->controller[i].classic[WII_CLASSIC_Y] = 'C';
        config->controller[i].classic[WII_CLASSIC_MINUS] = VK_BACKSPACE;
        config->controller[i].classic[WII_CLASSIC_PLUS] = VK_ENTER;
        config->controller[i].classic[WII_CLASSIC_L] = VK_PAGEDOWN;
        config->controller[i].classic[WII_CLASSIC_R] = VK_PAGEUP;
        config->controller[i].classic[WII_CLASSIC_ZL] = VK_SHIFT;
        config->controller[i].classic[WII_CLASSIC_ZR] = -1;

        config->controller[i].gamecube[WII_GAMECUBE_DPAD_LEFT] = VK_LEFT;
        config->controller[i].gamecube[WII_GAMECUBE_DPAD_DOWN] = VK_DOWN;
        config->controller[i].gamecube[WII_GAMECUBE_DPAD_RIGHT] = VK_RIGHT;
        config->controller[i].gamecube[WII_GAMECUBE_DPAD_UP] = VK_UP;
        config->controller[i].gamecube[WII_GAMECUBE_LEFT_STICK_LEFT] = VK_LEFT;
        config->controller[i].gamecube[WII_GAMECUBE_LEFT_STICK_DOWN] = VK_DOWN;
        config->controller[i].gamecube[WII_GAMECUBE_LEFT_STICK_UP] = VK_UP;
        config->controller[i].gamecube[WII_GAMECUBE_LEFT_STICK_RIGHT] = VK_RIGHT;
        config->controller[i].gamecube[WII_GAMECUBE_CSTICK_LEFT] = -1;
        config->controller[i].gamecube[WII_GAMECUBE_CSTICK_DOWN] = -1;
        config->controller[i].gamecube[WII_GAMECUBE_CSTICK_UP] = -1;
        config->controller[i].gamecube[WII_GAMECUBE_CSTICK_RIGHT] = -1;
        config->controller[i].gamecube[WII_GAMECUBE_A] = 'Z';
        config->controller[i].gamecube[WII_GAMECUBE_B] = 'X';
        config->controller[i].gamecube[WII_GAMECUBE_X] = 'C';
        config->controller[i].gamecube[WII_GAMECUBE_Y] = 'C';
        config->controller[i].gamecube[WII_GAMECUBE_LEFT_TRIGGER] = VK_PAGEDOWN;
        config->controller[i].gamecube[WII_GAMECUBE_RIGHT_TRIGGER] = VK_PAGEUP;
        config->controller[i].gamecube[WII_GAMECUBE_Z] = VK_SHIFT;
    }
}

static float clampUnit(float value) {
    if (value < -1.0f) return -1.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float applyInputDeadzone(float value) {
    if (value > -WII_STICK_DEADZONE && value < WII_STICK_DEADZONE) return 0.0f;
    return clampUnit(value);
}

static float normalizePadAxis(s8 value) {
    float axis = (float)value / 127.0f;
    return applyInputDeadzone(axis);
}

static float normalizePadTrigger(u8 value) {
    float trigger = (float)value / 255.0f;
    if (trigger < 0.0f) return 0.0f;
    if (trigger > 1.0f) return 1.0f;
    return trigger;
}

static void joystickToAxes(const joystick_t* joystick, float* outX, float* outY) {
    if (outX != NULL) *outX = 0.0f;
    if (outY != NULL) *outY = 0.0f;
    if (joystick == NULL || joystick->mag <= 0.0f) return;

    float magnitude = clampUnit(joystick->mag);
    float radians = joystick->ang * WII_DEG_TO_RAD;
    if (outX != NULL) *outX = applyInputDeadzone(sinf(radians) * magnitude);
    if (outY != NULL) *outY = applyInputDeadzone(-cosf(radians) * magnitude);
}

static bool axisHasInput(float value) {
    return value <= -0.5f || value >= 0.5f;
}

static bool classicHasHeldInput(uint32_t held, const classic_ctrl_t* classic) {
    if (held != 0u) return true;
    if (classic == NULL) return false;

    float axisLH = 0.0f;
    float axisLV = 0.0f;
    float axisRH = 0.0f;
    float axisRV = 0.0f;
    joystickToAxes(&classic->ljs, &axisLH, &axisLV);
    joystickToAxes(&classic->rjs, &axisRH, &axisRV);
    return axisHasInput(axisLH) || axisHasInput(axisLV) || axisHasInput(axisRH) ||
        axisHasInput(axisRV) || classic->l_shoulder >= 0.5f || classic->r_shoulder >= 0.5f;
}

static bool gameCubeHasHeldInput(const PADStatus* status) {
    if (status == NULL || status->err != PAD_ERR_NONE) return false;
    return status->button != 0u || axisHasInput(normalizePadAxis(status->stickX)) ||
        axisHasInput(normalizePadAxis(status->stickY)) ||
        axisHasInput(normalizePadAxis(status->substickX)) ||
        axisHasInput(normalizePadAxis(status->substickY)) ||
        normalizePadTrigger(status->triggerL) >= 0.5f ||
        normalizePadTrigger(status->triggerR) >= 0.5f;
}

static void syncGamepadButton(GamepadSlot* slot, int buttonIndex, bool down, float value) {
    if (slot == NULL || buttonIndex < 0 || buttonIndex >= GP_BUTTON_COUNT) return;
    bool previous = slot->buttonDown[buttonIndex];
    slot->buttonDownPrev[buttonIndex] = previous;
    slot->buttonDown[buttonIndex] = down;
    slot->buttonPressed[buttonIndex] = down && !previous;
    slot->buttonReleased[buttonIndex] = !down && previous;
    slot->buttonValue[buttonIndex] = down ? value : 0.0f;
}

static void disconnectGamepadSlot(GamepadSlot* slot) {
    if (slot == NULL) return;
    slot->connected = false;
    memset(slot->buttonDownPrev, 0, sizeof(slot->buttonDownPrev));
    memset(slot->buttonDown, 0, sizeof(slot->buttonDown));
    memset(slot->buttonPressed, 0, sizeof(slot->buttonPressed));
    memset(slot->buttonReleased, 0, sizeof(slot->buttonReleased));
    memset(slot->buttonValue, 0, sizeof(slot->buttonValue));
    memset(slot->axisValue, 0, sizeof(slot->axisValue));
}

static GamepadSlot* assignGamepadSlot(RunnerGamepadState* gamepads, uint32_t* nextSlotIndex,
    int jid, const char* description, const char* guid) {
    if (gamepads == NULL || nextSlotIndex == NULL || *nextSlotIndex >= MAX_GAMEPADS) return NULL;
    GamepadSlot* slot = &gamepads->slots[*nextSlotIndex];
    (*nextSlotIndex)++;

    bool descriptionChanged = strcmp(slot->description, description) != 0;
    bool guidChanged = strcmp(slot->guid, guid) != 0;
    if (descriptionChanged || guidChanged) {
        if ((descriptionChanged && slot->description[0] != '\0') ||
            (guidChanged && slot->guid[0] != '\0'))
            disconnectGamepadSlot(slot);
        snprintf(slot->description, sizeof(slot->description), "%s", description);
        snprintf(slot->guid, sizeof(slot->guid), "%s", guid);
    }

    slot->connected = true;
    slot->jid = jid;
    gamepads->connectedCount++;
    return slot;
}

static void syncClassicController(GamepadSlot* slot, uint32_t held, const classic_ctrl_t* classic) {
    if (slot == NULL || classic == NULL) return;

    float axisLH = 0.0f;
    float axisLV = 0.0f;
    float axisRH = 0.0f;
    float axisRV = 0.0f;
    joystickToAxes(&classic->ljs, &axisLH, &axisLV);
    joystickToAxes(&classic->rjs, &axisRH, &axisRV);
    slot->axisValue[0] = axisLH;
    slot->axisValue[1] = axisLV;
    slot->axisValue[2] = axisRH;
    slot->axisValue[3] = axisRV;

    float leftShoulder = clampUnit(classic->l_shoulder);
    float rightShoulder = clampUnit(classic->r_shoulder);
    bool leftShoulderDown =
        (held & WPAD_CLASSIC_BUTTON_FULL_L) != 0u || leftShoulder >= slot->triggerThreshold;
    bool rightShoulderDown =
        (held & WPAD_CLASSIC_BUTTON_FULL_R) != 0u || rightShoulder >= slot->triggerThreshold;

    syncGamepadButton(slot, 0, (held & WPAD_CLASSIC_BUTTON_A) != 0u, 1.0f);
    syncGamepadButton(slot, 1, (held & WPAD_CLASSIC_BUTTON_B) != 0u, 1.0f);
    syncGamepadButton(slot, 2, (held & WPAD_CLASSIC_BUTTON_X) != 0u, 1.0f);
    syncGamepadButton(slot, 3, (held & WPAD_CLASSIC_BUTTON_Y) != 0u, 1.0f);
    syncGamepadButton(
        slot, 4, leftShoulderDown, leftShoulderDown && leftShoulder > 0.0f ? leftShoulder : 1.0f);
    syncGamepadButton(slot, 5, rightShoulderDown,
        rightShoulderDown && rightShoulder > 0.0f ? rightShoulder : 1.0f);
    syncGamepadButton(slot, 6, (held & WPAD_CLASSIC_BUTTON_ZL) != 0u, 1.0f);
    syncGamepadButton(slot, 7, (held & WPAD_CLASSIC_BUTTON_ZR) != 0u, 1.0f);
    syncGamepadButton(slot, 8, (held & WPAD_CLASSIC_BUTTON_MINUS) != 0u, 1.0f);
    syncGamepadButton(slot, 9, (held & WPAD_CLASSIC_BUTTON_PLUS) != 0u, 1.0f);
    syncGamepadButton(slot, 10, false, 0.0f);
    syncGamepadButton(slot, 11, false, 0.0f);
    syncGamepadButton(slot, 12, (held & WPAD_CLASSIC_BUTTON_UP) != 0u || axisLV < -0.5f, 1.0f);
    syncGamepadButton(slot, 13, (held & WPAD_CLASSIC_BUTTON_DOWN) != 0u || axisLV > 0.5f, 1.0f);
    syncGamepadButton(slot, 14, (held & WPAD_CLASSIC_BUTTON_LEFT) != 0u || axisLH < -0.5f, 1.0f);
    syncGamepadButton(slot, 15, (held & WPAD_CLASSIC_BUTTON_RIGHT) != 0u || axisLH > 0.5f, 1.0f);
    syncGamepadButton(slot, 16, (held & WPAD_CLASSIC_BUTTON_HOME) != 0u, 1.0f);
}

static void syncWiimoteController(GamepadSlot* slot, uint32_t held) {
    if (slot == NULL) return;

    memset(slot->axisValue, 0, sizeof(slot->axisValue));
    syncGamepadButton(slot, 0, (held & WPAD_BUTTON_2) != 0u, 1.0f);
    syncGamepadButton(slot, 1, (held & WPAD_BUTTON_1) != 0u, 1.0f);
    syncGamepadButton(slot, 2, (held & WPAD_BUTTON_A) != 0u, 1.0f);
    syncGamepadButton(slot, 3, (held & WPAD_BUTTON_B) != 0u, 1.0f);
    syncGamepadButton(slot, 4, false, 0.0f);
    syncGamepadButton(slot, 5, false, 0.0f);
    syncGamepadButton(slot, 6, false, 0.0f);
    syncGamepadButton(slot, 7, false, 0.0f);
    syncGamepadButton(slot, 8, (held & WPAD_BUTTON_MINUS) != 0u, 1.0f);
    syncGamepadButton(slot, 9, (held & WPAD_BUTTON_PLUS) != 0u, 1.0f);
    syncGamepadButton(slot, 10, false, 0.0f);
    syncGamepadButton(slot, 11, false, 0.0f);
    syncGamepadButton(slot, 12, (held & WPAD_BUTTON_RIGHT) != 0u, 1.0f);
    syncGamepadButton(slot, 13, (held & WPAD_BUTTON_LEFT) != 0u, 1.0f);
    syncGamepadButton(slot, 14, (held & WPAD_BUTTON_UP) != 0u, 1.0f);
    syncGamepadButton(slot, 15, (held & WPAD_BUTTON_DOWN) != 0u, 1.0f);
    syncGamepadButton(slot, 16, (held & WPAD_BUTTON_HOME) != 0u, 1.0f);
}

static void syncGameCubeController(GamepadSlot* slot, const PADStatus* status) {
    if (slot == NULL || status == NULL) return;

    uint16_t held = status->button;
    float leftTrigger = normalizePadTrigger(status->triggerL);
    float rightTrigger = normalizePadTrigger(status->triggerR);
    bool leftTriggerDown = (held & PAD_TRIGGER_L) != 0u || leftTrigger >= slot->triggerThreshold;
    bool rightTriggerDown = (held & PAD_TRIGGER_R) != 0u || rightTrigger >= slot->triggerThreshold;

    slot->axisValue[0] = normalizePadAxis(status->stickX);
    slot->axisValue[1] = -normalizePadAxis(status->stickY);
    slot->axisValue[2] = normalizePadAxis(status->substickX);
    slot->axisValue[3] = -normalizePadAxis(status->substickY);

    syncGamepadButton(slot, 0, (held & PAD_BUTTON_A) != 0u, 1.0f);
    syncGamepadButton(slot, 1, (held & PAD_BUTTON_B) != 0u, 1.0f);
    syncGamepadButton(slot, 2, (held & PAD_BUTTON_X) != 0u, 1.0f);
    syncGamepadButton(slot, 3, (held & PAD_BUTTON_Y) != 0u, 1.0f);
    syncGamepadButton(
        slot, 4, leftTriggerDown, leftTriggerDown && leftTrigger > 0.0f ? leftTrigger : 1.0f);
    syncGamepadButton(
        slot, 5, rightTriggerDown, rightTriggerDown && rightTrigger > 0.0f ? rightTrigger : 1.0f);
    syncGamepadButton(slot, 6, false, 0.0f);
    syncGamepadButton(slot, 7, (held & PAD_TRIGGER_Z) != 0u, 1.0f);
    syncGamepadButton(slot, 8, false, 0.0f);
    syncGamepadButton(slot, 9, (held & PAD_BUTTON_START) != 0u, 1.0f);
    syncGamepadButton(slot, 10, false, 0.0f);
    syncGamepadButton(slot, 11, false, 0.0f);
    syncGamepadButton(slot, 12, (held & PAD_BUTTON_UP) != 0u || slot->axisValue[1] < -0.5f, 1.0f);
    syncGamepadButton(slot, 13, (held & PAD_BUTTON_DOWN) != 0u || slot->axisValue[1] > 0.5f, 1.0f);
    syncGamepadButton(slot, 14, (held & PAD_BUTTON_LEFT) != 0u || slot->axisValue[0] < -0.5f, 1.0f);
    syncGamepadButton(slot, 15, (held & PAD_BUTTON_RIGHT) != 0u || slot->axisValue[0] > 0.5f, 1.0f);
    syncGamepadButton(slot, 16, false, 0.0f);
}

static int findCandidateByGuid(
    const WiiControllerCandidate* candidates, uint32_t count, const char* guid) {
    if (candidates == NULL || guid == NULL || guid[0] == '\0') return -1;
    for (uint32_t i = 0u; i < count; ++i) {
        if (strcmp(candidates[i].guid, guid) == 0) return (int)i;
    }
    return -1;
}

static void choosePrimaryCandidate(const WiiControllerCandidate* candidates, uint32_t count) {
    int currentPrimary = findCandidateByGuid(candidates, count, gPrimaryGamepadGuid);
    if (currentPrimary >= 0 && candidates[currentPrimary].hasHeldInput) return;

    for (uint32_t i = 0u; i < count; ++i) {
        if (!candidates[i].hasHeldInput) continue;
        snprintf(gPrimaryGamepadGuid, sizeof(gPrimaryGamepadGuid), "%s", candidates[i].guid);
        return;
    }

    if (currentPrimary >= 0) return;
    if (count > 0u) {
        snprintf(gPrimaryGamepadGuid, sizeof(gPrimaryGamepadGuid), "%s", candidates[0].guid);
    } else {
        gPrimaryGamepadGuid[0] = '\0';
    }
}

static void syncControllerCandidate(RunnerGamepadState* gamepads, uint32_t* nextSlotIndex,
    const WiiControllerCandidate* candidate) {
    if (gamepads == NULL || nextSlotIndex == NULL || candidate == NULL) return;

    GamepadSlot* slot = assignGamepadSlot(
        gamepads, nextSlotIndex, (int)candidate->channel, candidate->description, candidate->guid);
    if (slot == NULL) return;

    switch (candidate->kind) {
    case WII_CONTROLLER_CLASSIC:
        syncClassicController(slot, candidate->held, &candidate->expansion.classic);
        break;
    case WII_CONTROLLER_GAMECUBE:
        syncGameCubeController(slot, &candidate->padStatus);
        break;
    case WII_CONTROLLER_WIIMOTE:
    default:
        syncWiimoteController(slot, candidate->held);
        break;
    }
}

static void syncControllerCandidates(
    RunnerGamepadState* gamepads, WiiControllerCandidate* candidates, uint32_t count) {
    if (gamepads == NULL) return;

    choosePrimaryCandidate(candidates, count);

    bool assigned[WII_CONTROLLER_CANDIDATE_COUNT] = {false};
    uint32_t nextSlotIndex = 0u;
    int primary = findCandidateByGuid(candidates, count, gPrimaryGamepadGuid);
    if (primary >= 0) {
        syncControllerCandidate(gamepads, &nextSlotIndex, &candidates[primary]);
        assigned[primary] = true;
    }

    for (uint32_t pass = 0u; pass < 2u; ++pass) {
        bool wantHeldInput = pass == 0u;
        for (uint32_t i = 0u; i < count && nextSlotIndex < MAX_GAMEPADS; ++i) {
            if (assigned[i] || candidates[i].hasHeldInput != wantHeldInput) continue;
            syncControllerCandidate(gamepads, &nextSlotIndex, &candidates[i]);
            assigned[i] = true;
        }
    }

    for (uint32_t slotIndex = nextSlotIndex; slotIndex < MAX_GAMEPADS; ++slotIndex) {
        disconnectGamepadSlot(&gamepads->slots[slotIndex]);
    }
}

static void applyMappedKey(bool* keyboardDesired, int32_t key, bool down) {
    if (keyboardDesired == NULL || key < 0 || key >= GML_KEY_COUNT) return;
    keyboardDesired[key] = keyboardDesired[key] || down;
}

static const WiiControllerMappingSettings* getControllerMappings(
    const WiiGamepadConfig* config, uint32_t channel) {
    if (config == NULL || channel >= WII_INPUT_CONTROLLER_COUNT) return NULL;
    return &config->controller[channel];
}

static uint32_t getWiimoteMappingIndex(
    const WiiControllerMappingSettings* mappings, const WiiWpadKeyMap* map) {
    if (mappings == NULL || map == NULL || !mappings->wiimoteSideways)
        return map != NULL ? map->mappingIndex : 0u;
    switch (map->wpadButton) {
    case WPAD_BUTTON_LEFT:
        return WII_WIIMOTE_DPAD_DOWN;
    case WPAD_BUTTON_RIGHT:
        return WII_WIIMOTE_DPAD_UP;
    case WPAD_BUTTON_UP:
        return WII_WIIMOTE_DPAD_LEFT;
    case WPAD_BUTTON_DOWN:
        return WII_WIIMOTE_DPAD_RIGHT;
    default:
        return map->mappingIndex;
    }
}

static void accumulateWiimoteKeys(
    bool* keyboardDesired, const WiiControllerMappingSettings* mappings, uint32_t held) {
    if (keyboardDesired == NULL || mappings == NULL) return;

    for (uint32_t i = 0u; i < sizeof(WPAD_KEYS) / sizeof(WPAD_KEYS[0]); ++i) {
        applyMappedKey(keyboardDesired,
            mappings->wiimote[getWiimoteMappingIndex(mappings, &WPAD_KEYS[i])],
            (held & WPAD_KEYS[i].wpadButton) != 0u);
    }
}

static void accumulateClassicKeys(bool* keyboardDesired,
    const WiiControllerMappingSettings* mappings, uint32_t held, const classic_ctrl_t* classic) {
    if (keyboardDesired == NULL || mappings == NULL) return;

    for (uint32_t i = 0u; i < sizeof(WPAD_CLASSIC_KEYS) / sizeof(WPAD_CLASSIC_KEYS[0]); ++i) {
        applyMappedKey(keyboardDesired, mappings->classic[WPAD_CLASSIC_KEYS[i].mappingIndex],
            (held & WPAD_CLASSIC_KEYS[i].wpadButton) != 0u);
    }

    if (classic == NULL) return;
    float axisLH = 0.0f;
    float axisLV = 0.0f;
    float axisRH = 0.0f;
    float axisRV = 0.0f;
    joystickToAxes(&classic->ljs, &axisLH, &axisLV);
    joystickToAxes(&classic->rjs, &axisRH, &axisRV);
    applyMappedKey(keyboardDesired, mappings->classic[WII_CLASSIC_LEFT_STICK_LEFT], axisLH < -0.5f);
    applyMappedKey(keyboardDesired, mappings->classic[WII_CLASSIC_LEFT_STICK_RIGHT], axisLH > 0.5f);
    applyMappedKey(keyboardDesired, mappings->classic[WII_CLASSIC_LEFT_STICK_UP], axisLV < -0.5f);
    applyMappedKey(keyboardDesired, mappings->classic[WII_CLASSIC_LEFT_STICK_DOWN], axisLV > 0.5f);
    applyMappedKey(
        keyboardDesired, mappings->classic[WII_CLASSIC_RIGHT_STICK_LEFT], axisRH < -0.5f);
    applyMappedKey(
        keyboardDesired, mappings->classic[WII_CLASSIC_RIGHT_STICK_RIGHT], axisRH > 0.5f);
    applyMappedKey(keyboardDesired, mappings->classic[WII_CLASSIC_RIGHT_STICK_UP], axisRV < -0.5f);
    applyMappedKey(keyboardDesired, mappings->classic[WII_CLASSIC_RIGHT_STICK_DOWN], axisRV > 0.5f);
    applyMappedKey(keyboardDesired, mappings->classic[WII_CLASSIC_L], classic->l_shoulder >= 0.5f);
    applyMappedKey(keyboardDesired, mappings->classic[WII_CLASSIC_R], classic->r_shoulder >= 0.5f);
}

static void accumulateGameCubeKeys(bool* keyboardDesired,
    const WiiControllerMappingSettings* mappings, uint16_t held, float axisX, float axisY,
    float cstickX, float cstickY, float triggerL, float triggerR) {
    if (keyboardDesired == NULL || mappings == NULL) return;

    for (uint32_t i = 0u; i < sizeof(PAD_KEYS) / sizeof(PAD_KEYS[0]); ++i) {
        applyMappedKey(keyboardDesired, mappings->gamecube[PAD_KEYS[i].mappingIndex],
            (held & PAD_KEYS[i].padButton) != 0u);
    }

    applyMappedKey(
        keyboardDesired, mappings->gamecube[WII_GAMECUBE_LEFT_STICK_LEFT], axisX < -0.5f);
    applyMappedKey(
        keyboardDesired, mappings->gamecube[WII_GAMECUBE_LEFT_STICK_RIGHT], axisX > 0.5f);
    applyMappedKey(keyboardDesired, mappings->gamecube[WII_GAMECUBE_LEFT_STICK_UP], axisY < -0.5f);
    applyMappedKey(keyboardDesired, mappings->gamecube[WII_GAMECUBE_LEFT_STICK_DOWN], axisY > 0.5f);
    applyMappedKey(keyboardDesired, mappings->gamecube[WII_GAMECUBE_CSTICK_LEFT], cstickX < -0.5f);
    applyMappedKey(keyboardDesired, mappings->gamecube[WII_GAMECUBE_CSTICK_RIGHT], cstickX > 0.5f);
    applyMappedKey(keyboardDesired, mappings->gamecube[WII_GAMECUBE_CSTICK_UP], cstickY < -0.5f);
    applyMappedKey(keyboardDesired, mappings->gamecube[WII_GAMECUBE_CSTICK_DOWN], cstickY > 0.5f);
    applyMappedKey(
        keyboardDesired, mappings->gamecube[WII_GAMECUBE_LEFT_TRIGGER], triggerL >= 0.5f);
    applyMappedKey(
        keyboardDesired, mappings->gamecube[WII_GAMECUBE_RIGHT_TRIGGER], triggerR >= 0.5f);
}

void WiiPad_poll(RunnerGamepadState* gamepads, bool* keyboardDesired, bool* outHomePressed,
    const WiiGamepadConfig* config) {
    if (outHomePressed != NULL) *outHomePressed = false;
    if (gamepads != NULL) RunnerGamepad_beginFrame(gamepads);
    const bool exposeControllersToRunner = gamepads != NULL;
    WiiGamepadConfig defaultConfig;
    if (config == NULL) {
        WiiPadCfg_init(&defaultConfig);
        config = &defaultConfig;
    }
    WiiControllerCandidate candidates[WII_CONTROLLER_CANDIDATE_COUNT];
    uint32_t candidateCount = 0u;

    WPAD_ScanPads();

    PADStatus padStatus[WII_GAMECUBE_CHANNEL_COUNT];
    memset(padStatus, 0, sizeof(padStatus));
    PAD_Read(padStatus);
    PAD_Clamp(padStatus);

    for (uint32_t channel = 0u; channel < WII_WPAD_CHANNEL_COUNT; ++channel) {
        uint32_t type = WPAD_EXP_NONE;
        bool connected = WPAD_Probe((s32)channel, &type) == WPAD_ERR_NONE;
        if (!connected) continue;

        uint32_t held = WPAD_ButtonsHeld((s32)channel);
        uint32_t down = WPAD_ButtonsDown((s32)channel);
        expansion_t expansion;
        memset(&expansion, 0, sizeof(expansion));
        WPAD_Expansion((s32)channel, &expansion);
        bool isClassic = type == WPAD_EXP_CLASSIC || expansion.type == WPAD_EXP_CLASSIC;

        if ((down & WPAD_BUTTON_HOME) != 0u || (down & WPAD_CLASSIC_BUTTON_HOME) != 0u) {
            if (outHomePressed != NULL) *outHomePressed = true;
        }

        if (gamepads != NULL && candidateCount < WII_CONTROLLER_CANDIDATE_COUNT) {
            WiiControllerCandidate* candidate = &candidates[candidateCount++];
            memset(candidate, 0, sizeof(*candidate));
            candidate->kind = isClassic ? WII_CONTROLLER_CLASSIC : WII_CONTROLLER_WIIMOTE;
            candidate->channel = channel;
            candidate->held = held;
            candidate->expansion = expansion;
            if (isClassic) {
                candidate->hasHeldInput = classicHasHeldInput(held, &expansion.classic);
                snprintf(candidate->description, sizeof(candidate->description),
                    "Wii Classic Controller");
                snprintf(candidate->guid, sizeof(candidate->guid), "wii-classic-%lu",
                    (unsigned long)channel);
            } else {
                candidate->hasHeldInput = held != 0u;
                snprintf(candidate->description, sizeof(candidate->description), "Wii Remote");
                snprintf(candidate->guid, sizeof(candidate->guid), "wii-remote-%lu",
                    (unsigned long)channel);
            }
        }

        if (keyboardDesired != NULL && !exposeControllersToRunner) {
            const WiiControllerMappingSettings* mappings = getControllerMappings(config, channel);
            if (isClassic) {
                accumulateClassicKeys(keyboardDesired, mappings, held, &expansion.classic);
            } else {
                accumulateWiimoteKeys(keyboardDesired, mappings, held);
            }
        }
    }

    for (uint32_t channel = 0u; channel < WII_GAMECUBE_CHANNEL_COUNT; ++channel) {
        if (padStatus[channel].err != PAD_ERR_NONE) {
            continue;
        }

        uint16_t padHeld = padStatus[channel].button;
        bool startHeld = (padHeld & PAD_BUTTON_START) != 0u;
        bool startDown = startHeld && !gGameCubeStartHeld[channel];
        gGameCubeStartHeld[channel] = startHeld;
        if (startDown) {
            if (outHomePressed != NULL) *outHomePressed = true;
        }

        if (gamepads != NULL && candidateCount < WII_CONTROLLER_CANDIDATE_COUNT) {
            WiiControllerCandidate* candidate = &candidates[candidateCount++];
            memset(candidate, 0, sizeof(*candidate));
            candidate->kind = WII_CONTROLLER_GAMECUBE;
            candidate->channel = channel;
            candidate->padStatus = padStatus[channel];
            candidate->hasHeldInput = gameCubeHasHeldInput(&padStatus[channel]);
            snprintf(candidate->description, sizeof(candidate->description), "GameCube Controller");
            snprintf(candidate->guid, sizeof(candidate->guid), "wii-gamecube-%lu",
                (unsigned long)channel);
        }

        if (!exposeControllersToRunner) {
            const WiiControllerMappingSettings* mappings = getControllerMappings(config, channel);
            float axisX = normalizePadAxis(padStatus[channel].stickX);
            float axisY = -normalizePadAxis(padStatus[channel].stickY);
            float cstickX = normalizePadAxis(padStatus[channel].substickX);
            float cstickY = -normalizePadAxis(padStatus[channel].substickY);
            float triggerL = normalizePadTrigger(padStatus[channel].triggerL);
            float triggerR = normalizePadTrigger(padStatus[channel].triggerR);
            accumulateGameCubeKeys(keyboardDesired, mappings, padStatus[channel].button, axisX,
                axisY, cstickX, cstickY, triggerL, triggerR);
        }
    }

    if (gamepads != NULL) {
        syncControllerCandidates(gamepads, candidates, candidateCount);
    }
}
