#include "xDebugTweaks.h"
#include "xDebug.h"
#include "xString.h"
#include <string.h>

static const S32 TWEAK_MAX = 2048;

static tweak_entry gTweaks[TWEAK_MAX];
static S32 gTweakCount = 0;

S32 xDebugTweaks_Count()
{
    return gTweakCount;
}

tweak_entry* xDebugTweaks_Get(S32 i)
{
    if (i < 0 || i >= gTweakCount)
    {
        return NULL;
    }

    return &gTweaks[i];
}

tweak_entry* xDebugTweaks_Find(const char* name)
{
    if (name == NULL)
    {
        return NULL;
    }

    for (S32 i = 0; i < gTweakCount; ++i)
    {
        if (strcmp(gTweaks[i].name, name) == 0)
        {
            return &gTweaks[i];
        }
    }

    return NULL;
}

static tweak_entry* tweak_alloc(const char* name, U8 type)
{
    tweak_entry* e = xDebugTweaks_Find(name);

    if (e == NULL)
    {
        if (gTweakCount >= TWEAK_MAX)
        {
            return NULL;
        }

        e = &gTweaks[gTweakCount];
        gTweakCount++;
        memset(e, 0, sizeof(tweak_entry));
        strncpy(e->name, name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
    }

    e->type = type;
    return e;
}

void xDebugTweaks_Fire(tweak_entry* e)
{
    if (e == NULL || e->cb == NULL || e->cb->on_change == NULL)
    {
        return;
    }

    tweak_info info;
    memset(&info, 0, sizeof(info));
    info.name.text = e->name;
    info.name.size = strlen(e->name);
    info.value = e->value;
    info.cb = e->cb;
    info.context = e->context;
    info.type = e->type;

    switch (e->type)
    {
    case TWEAK_FLOAT:
        info.value_size = 4;
        info.float_context.value_def = e->fmin;
        info.float_context.value_min = e->fmin;
        info.float_context.value_max = e->fmax;
        break;
    case TWEAK_S16:
        info.value_size = 2;
        info.int_context.value_def = e->imin;
        info.int_context.value_min = e->imin;
        info.int_context.value_max = e->imax;
        break;
    case TWEAK_U8:
        info.value_size = 1;
        info.uint_context.value_def = (U32)e->imin;
        info.uint_context.value_min = (U32)e->imin;
        info.uint_context.value_max = (U32)e->imax;
        break;
    case TWEAK_U32:
        info.value_size = 4;
        info.uint_context.value_def = (U32)e->imin;
        info.uint_context.value_min = (U32)e->imin;
        info.uint_context.value_max = (U32)e->imax;
        break;
    case TWEAK_BUTTON:
        info.value_size = 0;
        break;
    case TWEAK_FLAG:
        info.value_size = 4;
        info.flag_context.mask = e->flag_mask;
        break;
    case TWEAK_SELECT:
        info.value_size = 4;
        info.select_context.labels = (char**)e->sel_labels;
        info.select_context.labels_size = e->sel_count;
        info.select_context.values = (void*)e->sel_values;
        break;
    default:
        break;
    }

    e->cb->on_change(info);
}

void xDebugAddTweak(const char* name, F32* value, F32 min, F32 max, const tweak_callback* cb,
                    void* context, U32 flags)
{
    tweak_entry* e = tweak_alloc(name, TWEAK_FLOAT);

    if (e == NULL)
    {
        return;
    }

    e->value = value;
    e->fmin = min;
    e->fmax = max;
    e->cb = (tweak_callback*)cb;
    e->context = context;
    e->flags = flags;
}

void xDebugAddTweak(const char* name, S16* value, S16 min, S16 max, const tweak_callback* cb,
                    void* context, U32 flags)
{
    tweak_entry* e = tweak_alloc(name, TWEAK_S16);

    if (e == NULL)
    {
        return;
    }

    e->value = value;
    e->imin = min;
    e->imax = max;
    e->cb = (tweak_callback*)cb;
    e->context = context;
    e->flags = flags;
}

void xDebugAddTweak(const char* name, U8* value, U8 min, U8 max, const tweak_callback* cb,
                    void* context, U32 flags)
{
    tweak_entry* e = tweak_alloc(name, TWEAK_U8);

    if (e == NULL)
    {
        return;
    }

    e->value = value;
    e->imin = min;
    e->imax = max;
    e->cb = (tweak_callback*)cb;
    e->context = context;
    e->flags = flags;
}

void xDebugAddTweak(const char* name, U32* value, U32 min, U32 max, const tweak_callback* cb,
                    void* context, U32 flags)
{
    tweak_entry* e = tweak_alloc(name, TWEAK_U32);

    if (e == NULL)
    {
        return;
    }

    e->value = value;
    e->imin = (S32)min;
    e->imax = (S32)max;
    e->cb = (tweak_callback*)cb;
    e->context = context;
    e->flags = flags;
}

void xDebugAddTweak(const char* name, const char* label, const tweak_callback* cb, void* context,
                    U32 flags)
{
    tweak_entry* e = tweak_alloc(name, TWEAK_BUTTON);

    if (e == NULL)
    {
        return;
    }

    e->value = (void*)label;
    e->cb = (tweak_callback*)cb;
    e->context = context;
    e->flags = flags;
}

void xDebugAddFlagTweak(const char* name, U32* flags, U32 mask, const tweak_callback* cb,
                        void* context, U32 flag)
{
    tweak_entry* e = tweak_alloc(name, TWEAK_FLAG);

    if (e == NULL)
    {
        return;
    }

    e->value = flags;
    e->flag_mask = mask;
    e->cb = (tweak_callback*)cb;
    e->context = context;
    e->flags = flag;
}

void xDebugAddSelectTweak(const char* name, U32* value, const char** labels, const U32* values,
                          U32 count, const tweak_callback* cb, void* context, U32 flags)
{
    tweak_entry* e = tweak_alloc(name, TWEAK_SELECT);

    if (e == NULL)
    {
        return;
    }

    e->value = value;
    e->sel_labels = labels;
    e->sel_values = values;
    e->sel_count = count;
    e->cb = (tweak_callback*)cb;
    e->context = context;
    e->flags = flags;
}

void xDebugRemoveTweak(const char* name)
{
    if (name == NULL)
    {
        return;
    }

    size_t len = strlen(name);
    S32 w = 0;

    for (S32 r = 0; r < gTweakCount; ++r)
    {
        if (strncmp(gTweaks[r].name, name, len) != 0)
        {
            if (w != r)
            {
                gTweaks[w] = gTweaks[r];
            }
            w++;
        }
    }

    gTweakCount = w;
}
