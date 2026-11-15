#ifndef _CINNAMON_ASGORE_BATTLE_H_
#define _CINNAMON_ASGORE_BATTLE_H_

#include "native_runtime.h"

void NativeAsgoreBattle_reset(void);
bool NativeAsgoreBattle_isActive(VMContext* ctx);
// Requires a successful isActive check for this context.
bool NativeAsgoreBattle_collidesWithHeart(VMContext* ctx, struct Instance* self);

#endif
