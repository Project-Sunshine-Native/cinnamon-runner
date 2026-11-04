#ifndef _CINNAMON_NATIVE_SCRIPTS_H_
#define _CINNAMON_NATIVE_SCRIPTS_H_

typedef struct VMContext VMContext;

// Registers the native implementations shipped with the runner. Missing CODE entries are ignored,
// which keeps this safe for games that don't have native script compilations
void NativeScripts_registerAll(VMContext* ctx);

#endif /* _CINNAMON_NATIVE_SCRIPTS_H_ */
