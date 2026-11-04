#include "3ds_gamepad.h"

#include <stdio.h>

static float n3ds_normalizeCircleAxis(int axis) {
    float value = (float)axis / 156.0f;
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    return value;
}

static void n3ds_syncGamepadButton(GamepadSlot* slot, int buttonIndex, bool held, float value) {
    if (slot == NULL || buttonIndex < 0 || buttonIndex >= GP_BUTTON_COUNT) return;

    bool previous = slot->buttonDown[buttonIndex];
    slot->buttonDownPrev[buttonIndex] = previous;
    slot->buttonDown[buttonIndex] = held;
    slot->buttonPressed[buttonIndex] = held && !previous;
    slot->buttonReleased[buttonIndex] = !held && previous;
    slot->buttonValue[buttonIndex] = held ? value : 0.0f;
}

void n3ds_padSyncPrimary(RunnerGamepadState* gamepads, u32 held, circlePosition circle) {
    if (gamepads == NULL) return;

    GamepadSlot* slot = &gamepads->slots[0];
    slot->connected = true;
    slot->jid = 0;
    if (slot->description[0] == '\0')
        snprintf(slot->description, sizeof(slot->description), "Nintendo 3DS");
    if (slot->guid[0] == '\0') snprintf(slot->guid, sizeof(slot->guid), "3ds");
    gamepads->connectedCount = 1;

    float axisLH = n3ds_normalizeCircleAxis(circle.dx);
    float axisLV = -n3ds_normalizeCircleAxis(circle.dy);
    slot->axisValue[0] = axisLH;
    slot->axisValue[1] = axisLV;
    slot->axisValue[2] = 0.0f;
    slot->axisValue[3] = 0.0f;

    n3ds_syncGamepadButton(slot, 0, (held & KEY_A) != 0, 1.0f);
    n3ds_syncGamepadButton(slot, 1, (held & KEY_B) != 0, 1.0f);
    n3ds_syncGamepadButton(slot, 2, (held & KEY_X) != 0, 1.0f);
    n3ds_syncGamepadButton(slot, 3, (held & KEY_Y) != 0, 1.0f);
    n3ds_syncGamepadButton(slot, 4, (held & KEY_L) != 0, 1.0f);
    n3ds_syncGamepadButton(slot, 5, (held & KEY_R) != 0, 1.0f);
    n3ds_syncGamepadButton(slot, 6, false, 0.0f);
    n3ds_syncGamepadButton(slot, 7, false, 0.0f);
    n3ds_syncGamepadButton(slot, 8, (held & KEY_SELECT) != 0, 1.0f);
    n3ds_syncGamepadButton(slot, 9, (held & KEY_START) != 0, 1.0f);
    n3ds_syncGamepadButton(slot, 10, false, 0.0f);
    n3ds_syncGamepadButton(slot, 11, false, 0.0f);
    n3ds_syncGamepadButton(slot, 12, ((held & KEY_UP) != 0) || axisLV < -0.5f, 1.0f);
    n3ds_syncGamepadButton(slot, 13, ((held & KEY_DOWN) != 0) || axisLV > 0.5f, 1.0f);
    n3ds_syncGamepadButton(slot, 14, ((held & KEY_LEFT) != 0) || axisLH < -0.5f, 1.0f);
    n3ds_syncGamepadButton(slot, 15, ((held & KEY_RIGHT) != 0) || axisLH > 0.5f, 1.0f);
    n3ds_syncGamepadButton(slot, 16, false, 0.0f);

    for (int i = 1; i < MAX_GAMEPADS; ++i) {
        gamepads->slots[i].connected = false;
    }
}
