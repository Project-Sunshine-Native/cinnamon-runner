#ifndef _BS_3DS_GAMEPAD_H_
#define _BS_3DS_GAMEPAD_H_

#include "../runner_gamepad.h"

#include <3ds.h>

void n3ds_padSyncPrimary(RunnerGamepadState* gamepads, u32 held, circlePosition circle);

#endif
