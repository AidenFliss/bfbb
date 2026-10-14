#ifndef XDEBUGTWEAKS_H
#define XDEBUGTWEAKS_H

#include "types.h"

struct tweak_callback;
struct tweak_info;

enum eTweakType
{
    TWEAK_FLOAT = 1,
    TWEAK_S16,
    TWEAK_U8,
    TWEAK_U32,
    TWEAK_BUTTON,
    TWEAK_FLAG,
    TWEAK_SELECT
};

struct tweak_entry
{
    char name[192];
    U8 type;
    void* value;
    F32 fmin;
    F32 fmax;
    S32 imin;
    S32 imax;
    tweak_callback* cb;
    void* context;
    U32 flags;
    const char** sel_labels;
    const U32* sel_values;
    U32 sel_count;
    U32 flag_mask;
};

S32 xDebugTweaks_Count();
tweak_entry* xDebugTweaks_Get(S32 i);
void xDebugTweaks_Fire(tweak_entry* e);
tweak_entry* xDebugTweaks_Find(const char* name);

#endif
