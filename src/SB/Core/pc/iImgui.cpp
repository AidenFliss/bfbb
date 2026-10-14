// Dear ImGui, behind an i interface, for the port only. No gc counterpart
// exists and nothing outside src/SB/Core/pc includes this header, so the
// console build cannot reach it even by accident.
//
// The backend is librw's skeleton imgui_impl_rw, which is a RENDERER backend
// only: no clock, no input. The frame delta is a parameter on NewFrame and is
// supplied here from iTime. The frame contract is ImGui's own: NewFrame once,
// RenderDrawLists once, with the camera open -- the backend draws through
// RenderWare, so both calls belong between RwCameraBeginUpdate and
// RwCameraEndUpdate.
//
// ImGui_ImplRW_NewFrame calls ImGui::NewFrame itself, so this file must not.
// ImGui_ImplRW_Init creates the context, so this file must not. DisplaySize is
// the virtual screen (iScreen), the space the backend's Im2D vertices land in,
// and the mouse is mapped into it by inverting librw's present blit.

#include "iImgui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <types.h>
#include "iCheats.h"
#include "iFile.h"
#include "iScreen.h"
#include "iTime.h"
#include "iWindow.h"
#include "rw/backend.h"
#include "xEnt.h"
#include "xEvent.h"
#include "xDebugTweaks.h"
#include "xMathInlines.h"
#include "xMemMgr.h"
#include "zBase.h"
#include "zDbgRender.h"
#include "zEntPickup.h"
#include "zEntPlayer.h"
#include "zEntPlayerOOBState.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zNPCSpawn.h"
#include "zPortal.h"
#include "zSaveLoad.h"
#include "zScene.h"
#include "zMain.h"
#include "xIni.h"

#include "imgui.h"
#include "imgui_impl_rw.h"

#include <SDL3/SDL.h>

// The soak test's scene-switch portal, defined in zGame.cpp. Reused rather
// than duplicated: the scene-switch and menu paths both read whichever portal
// pendingPortal names, and these two already outlive every frame.
extern xPortalAsset dummyPortalAsset;
extern _zPortal dummyPortal;

// Give helpers with no header declaration -- defined in zEntPlayer.cpp and
// only ever called from within it in retail.
extern void zEntPlayer_GiveShinyObject(S32 quantity);
extern void zEntPlayer_GiveLevelPickupCurrentLevel(S32 quantity);

// zGame.cpp one-shot reload flags, external linkage, no header.
extern S32 g_hiphopForcePortal;
extern S32 g_hiphopReloadHIP;

static bool sImguiReady = false;
static bool sShowDemo = false;
static bool sShowWarp = false;
static bool sShowGlobals = false;
static bool sShowEntities = false;
static bool sShowSpawn = false;
static bool sShowPerf = false;
static bool sShowMem = false;
static bool sShowTweaks = false;
static bool sShowIni = false;

#define IIMGUI_INI_MAX 512
static char sIniKeys[IIMGUI_INI_MAX][128];
static char sIniVals[IIMGUI_INI_MAX][160];
static char sIniDefaultKeys[IIMGUI_INI_MAX][128];
static char sIniDefaults[IIMGUI_INI_MAX][160];
static int sIniCount;
static int sIniDefaultCount;
static bool sIniHaveDefaults;

static int iImguiStricmp(const char* a, const char* b)
{
    while (*a && *b)
    {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);

        if (ca != cb)
        {
            return ca - cb;
        }

        ++a;
        ++b;
    }

    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static char* iImguiTrim(char* s)
{
    while (*s == ' ' || *s == '\t')
    {
        ++s;
    }

    char* e = s + strlen(s);

    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
    {
        --e;
    }

    *e = '\0';
    return s;
}

void iImguiINICaptureDefaults(xIniFile* ini)
{
    if (ini == NULL)
    {
        return;
    }

    sIniDefaultCount = 0;

    for (S32 i = 0; i < ini->NumValues && sIniDefaultCount < IIMGUI_INI_MAX; ++i)
    {
        strncpy(sIniDefaultKeys[sIniDefaultCount], ini->Values[i].tok,
                sizeof(sIniDefaultKeys[0]) - 1);
        sIniDefaultKeys[sIniDefaultCount][sizeof(sIniDefaultKeys[0]) - 1] = '\0';
        strncpy(sIniDefaults[sIniDefaultCount], ini->Values[i].val, sizeof(sIniDefaults[0]) - 1);
        sIniDefaults[sIniDefaultCount][sizeof(sIniDefaults[0]) - 1] = '\0';
        ++sIniDefaultCount;
    }

    sIniHaveDefaults = sIniDefaultCount > 0;
}

// Resolve a writable path to SB.INI, fixing the filename case so a write on a
// case-sensitive filesystem overwrites the real on-disk file rather than
// creating a second one.
static void iImguiINIDiskPath(char* out, size_t n)
{
    char full[512];
    iFileFullPath("SB.INI", full);

    FILE* f = fopen(full, "rb");

    if (f)
    {
        fclose(f);
        snprintf(out, n, "%s", full);
        return;
    }

    char lower[512];
    snprintf(lower, sizeof(lower), "%s", full);

    for (int i = (int)strlen(lower) - 1; i >= 0 && lower[i] != '/' && lower[i] != '\\'; --i)
    {
        lower[i] = (char)tolower((unsigned char)lower[i]);
    }

    f = fopen(lower, "rb");

    if (f)
    {
        fclose(f);
        snprintf(out, n, "%s", lower);
        return;
    }

    snprintf(out, n, "%s", full);
}

static bool iImguiINILoadFromDisk()
{
    char path[512];
    iImguiINIDiskPath(path, sizeof(path));

    FILE* f = fopen(path, "rb");

    if (f == NULL)
    {
        return false;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 1 << 20)
    {
        fclose(f);
        return false;
    }

    char* buf = (char*)malloc((size_t)sz + 1);

    if (buf == NULL)
    {
        fclose(f);
        return false;
    }

    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);

    sIniCount = 0;
    char* line = buf;

    while (line && *line && sIniCount < IIMGUI_INI_MAX)
    {
        char* nl = strchr(line, '\n');

        if (nl)
        {
            *nl = '\0';
        }

        char* t = iImguiTrim(line);

        if (*t != '#' && *t != '[' && *t != '\0')
        {
            char* eq = strchr(t, '=');

            if (eq)
            {
                *eq = '\0';
                char* key = iImguiTrim(t);
                char* val = iImguiTrim(eq + 1);

                if (*key != '\0')
                {
                    strncpy(sIniKeys[sIniCount], key, sizeof(sIniKeys[0]) - 1);
                    sIniKeys[sIniCount][sizeof(sIniKeys[0]) - 1] = '\0';
                    strncpy(sIniVals[sIniCount], val, sizeof(sIniVals[0]) - 1);
                    sIniVals[sIniCount][sizeof(sIniVals[0]) - 1] = '\0';
                    ++sIniCount;
                }
            }
        }

        line = nl ? nl + 1 : NULL;
    }

    free(buf);
    return sIniCount > 0;
}

static void iImguiINIApplyDefaults()
{
    if (!sIniHaveDefaults)
    {
        return;
    }

    for (int i = 0; i < sIniCount; ++i)
    {
        for (int d = 0; d < sIniDefaultCount; ++d)
        {
            if (iImguiStricmp(sIniKeys[i], sIniDefaultKeys[d]) == 0)
            {
                strncpy(sIniVals[i], sIniDefaults[d], sizeof(sIniVals[0]) - 1);
                sIniVals[i][sizeof(sIniVals[0]) - 1] = '\0';
                break;
            }
        }
    }
}

static void iImguiINIWriteAndReload()
{
    char path[512];
    iImguiINIDiskPath(path, sizeof(path));

    U32 size = 0;
    U32* pbuf = iFileLoad("SB.INI", NULL, &size);

    if (pbuf == NULL)
    {
        return;
    }

    char* text = (char*)malloc((size_t)size + 1);

    if (text == NULL)
    {
        return;
    }

    memcpy(text, pbuf, size);
    text[size] = '\0';

    FILE* out = fopen(path, "wb");

    if (out == NULL)
    {
        free(text);
        return;
    }

    char* line = text;

    while (line && *line)
    {
        char* nl = strchr(line, '\n');

        if (nl)
        {
            *nl = '\0';
        }

        char* t = iImguiTrim(line);

        if (*t != '#' && *t != '[' && *t != '\0')
        {
            char* eq = strchr(t, '=');

            if (eq)
            {
                *eq = '\0';
                char* key = iImguiTrim(t);
                bool matched = false;

                for (int i = 0; i < sIniCount; ++i)
                {
                    if (iImguiStricmp(key, sIniKeys[i]) == 0)
                    {
                        fprintf(out, "%s = %s\n", sIniKeys[i], sIniVals[i]);
                        matched = true;
                        break;
                    }
                }

                if (!matched)
                {
                    fprintf(out, "%s = %s\n", key, iImguiTrim(eq + 1));
                }
            }
            else
            {
                fprintf(out, "%s\n", line);
            }
        }
        else
        {
            fprintf(out, "%s\n", line);
        }

        line = nl ? nl + 1 : NULL;
    }

    fclose(out);
    free(text);
    zMainReloadINI();
}

static char sModeBuf[32] = "game";
static char sEntitySearch[64] = "";
static bool sShowBootPickups = false;

static char sMemAddrBuf[32] = "0x";
static unsigned long long sMemAddr;
static bool sMemInited;

static iTime sLastFrame = 0;
static bool sHaveLastFrame = false;
static iTime sPrevPerfFrame = 0;

static const char* sLastWarpName = NULL;

static void iImguiFeedMouse()
{
    float mx = 0.0f;
    float my = 0.0f;
    Uint32 buttons = SDL_GetMouseState(&mx, &my);

    S32 winw = 0;
    S32 winh = 0;

    if (iBackendIsGL3())
    {
        SDL_Window* win = (SDL_Window*)iWindowNativeHandle();
        if (win != NULL)
        {
            SDL_GetWindowSize(win, &winw, &winh);
        }
    }

    if (winw <= 0 || winh <= 0)
    {
        iWindowGetSize(&winw, &winh);
    }

    if (winw <= 0 || winh <= 0)
    {
        return;
    }

    F32 vw = iScreenWidthF();
    F32 vh = iScreenHeightF();

    F32 dw = (F32)winw;
    F32 dh = dw * vh / vw;
    if (dh > (F32)winh)
    {
        dh = (F32)winh;
        dw = dh * vw / vh;
    }
    F32 dx = ((F32)winw - dw) * 0.5f;
    F32 dy = ((F32)winh - dh) * 0.5f;

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent((mx - dx) * vw / dw, (my - dy) * vh / dh);
    io.AddMouseButtonEvent(0, (buttons & SDL_BUTTON_LMASK) != 0);
    io.AddMouseButtonEvent(1, (buttons & SDL_BUTTON_RMASK) != 0);
    io.AddMouseButtonEvent(2, (buttons & SDL_BUTTON_MMASK) != 0);

    SDL_Event ev;
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_EVENT_MOUSE_WHEEL,
                          SDL_EVENT_MOUSE_WHEEL) > 0)
    {
        io.AddMouseWheelEvent(0.0f, ev.wheel.y);
    }
}

static const char* iImguiGameModeName(S32 mode)
{
    static const char* names[] = { "boot",        "intro",   "title", "start",
                                   "load",        "options", "save",  "pause",
                                   "stall",       "worldmap", "monstergallery",
                                   "conceptartgallery", "game" };
    if (mode >= 0 && mode < (S32)(sizeof(names) / sizeof(names[0])))
    {
        return names[mode];
    }
    return "?";
}

static S32 iImguiParseGameMode(const char* s)
{
    for (S32 i = 0; i < (S32)eGameMode_Count; ++i)
    {
        const char* a = s;
        const char* b = iImguiGameModeName(i);
        while (*a != '\0' && *b != '\0' &&
               tolower((unsigned char)*a) == tolower((unsigned char)*b))
        {
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0')
        {
            return i;
        }
    }

    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (end != s && *end == '\0' && v >= 0 && v < (long)eGameMode_Count)
    {
        return (S32)v;
    }
    return -1;
}

// en_ZBASETYPE in order; retail ships no name table, so the debug layer owns
// one (same shape as iImguiGameModeName above).
static const char* iImguiBaseTypeName(U32 t)
{
    static const char* names[eBaseTypeCount] = {
        "unknown",        "trigger",           "villain",
        "player",         "pickup",            "env",
        "platform",       "camera",            "door",
        "savepoint",      "item",              "static",
        "dynamic",        "movepoint",         "timer",
        "bubble",         "portal",            "group",
        "pendulum",       "sfx",               "ffx",
        "vfx",            "counter",           "hangable",
        "button",         "projectile",        "surface",
        "destructobj",    "gust",              "volume",
        "dispatcher",     "cond",              "ui",
        "uifont",         "projectiletype",    "lobmaster",
        "fog",            "light",             "particleemitter",
        "particlesystem", "cutscenemgr",       "egenerator",
        "script",         "npc",               "hud",
        "npcprops",       "particleemitterprops", "boulder",
        "cruisebubble",   "teleportbox",       "busstop",
        "textbox",        "talkbox",           "taskbox",
        "bouldergenerator", "npcsettings",     "discofloor",
        "taxi",           "hud_model",         "hud_font_meter",
        "hud_unit_meter", "bungeehook",        "camerafly",
        "trackphysics",   "zipline",           "arena",
        "duplicator",     "laserbeam",         "turret",
        "camerataweak",   "slideprops",        "hud_text"
    };
    if (t < eBaseTypeCount)
    {
        return names[t];
    }
    return "?";
}

// The base types whose objects are xEnt at offset 0 with a live model, i.e.
// the ones HIDE and TELE can act on. Everything else in the scene list is a
// script, dispatcher, UI element or similar and has no position to show.
// xEntGetPos is not null-safe below the model: menu-scene entities carry a
// model instance whose Frame was never assigned, and the inline dereferences
// it. Every viewer readout goes through here instead.
static const xVec3* iImguiEntPos(const xEnt* e)
{
    if (e == NULL || e->model == NULL)
    {
        return NULL;
    }

    xMat4x3* fr = xModelGetFrame(e->model);
    if (fr == NULL)
    {
        return NULL;
    }

    return &fr->pos;
}

// The base types whose scene objects are genuinely xEnt at offset 0. The
// authority is zScene.cpp's init tables, not intuition: the
// zSceneInitFunc_DefaultEnt rows and the dynamic_types is_ent flag. Surface
// is a bare xSurface, Env and Gust are xBase, and BusStop/Taxi/BungeeHook/
// BoulderGenerator are flagged is_ent false -- casting those to xEnt reads
// unrelated pointers as the model and crashes (the menu scene is where
// Surface shows up). Everything else is still listed and searchable; it just
// has no pos/HIDE/TELE.
static bool iImguiBaseHasModel(U32 t)
{
    switch (t)
    {
    case eBaseTypeTrigger:
    case eBaseTypePickup:
    case eBaseTypeStatic:
    case eBaseTypeTrackPhysics:
    case eBaseTypePlatform:
    case eBaseTypePendulum:
    case eBaseTypeHangable:
    case eBaseTypeDestructObj:
    case eBaseTypeBoulder:
    case eBaseTypeButton:
    case eBaseTypeEGenerator:
    case eBaseTypePlayer:
    case eBaseTypeTeleportBox:
    case eBaseTypeNPC:
        return true;
    default:
        return false;
    }
}

// Rebuilt with the entity rows: only types present in the current scene get
// a dropdown entry. "All" is always entry 0.
static const char* sTypeFilterNames[eBaseTypeCount + 1] = { "All" };
static S32 sTypeFilterTypes[eBaseTypeCount + 1] = { -1 };
static int sTypeFilterCount = 1;
static int sTypeFilter = 0;

// Warp targets: every on-disc level pair, ordered by the pause-menu task
// groups (mnu4's PAUSE TASK TXT 010X-150X). The 140X Patrick and 150X
// Mr. Krabs groups have no level files -- hub start positions, not scenes.
// PG12 has no task group; it sits at the end. Unused scenes that ship a
// file (B301, DB05) stay in. Names are the per-file names from the Heavy
// Iron wiki; tweak them here freely.
static const struct
{
    const char* code;
    const char* name;
} sSceneList[] = {
    { "HB00", "Prologue Cutscene" },
    { "HB01", "Bikini Bottom" },
    { "HB02", "SpongeBob's Pineapple" },
    { "HB03", "Squidward's Tiki" },
    { "HB04", "Patrick's Rock" },
    { "HB05", "Sandy's Tree-dome" },
    { "HB06", "Shady Shoals Rest Home" },
    { "HB07", "Krusty Krab" },
    { "HB08", "Chum Bucket" },
    { "HB09", "Police Station" },
    { "HB10", "Theater" },
    { "JF01", "Jellyfish Rock" },
    { "JF02", "Jellyfish Caves" },
    { "JF03", "Jellyfish Lake" },
    { "JF04", "Spork Mountain (King Jellyfish)" },
    { "BB01", "Downtown Streets" },
    { "BB02", "Downtown Rooftops" },
    { "BB03", "Lighthouse" },
    { "BB04", "Sea Needle" },
    { "GL01", "Goo Lagoon Beach" },
    { "GL02", "Goo Lagoon Sea Caves" },
    { "GL03", "Goo Lagoon Pier" },
    { "B101", "Poseidome" },
    { "RB01", "Downtown Rock Bottom" },
    { "RB02", "Rock Bottom Museum" },
    { "RB03", "Trench of Advanced Darkness" },
    { "BC01", "Lobby" },
    { "BC02", "Main Chamber" },
    { "BC03", "Security Tunnel" },
    { "BC04", "Rolling Ball Area" },
    { "BC05", "Villain Containment Area (Prawn)" },
    { "SM01", "Ski Lodge" },
    { "SM02", "Guppy Mound" },
    { "SM03", "Flounder Hill" },
    { "SM04", "Sand Mountain" },
    { "B201", "Industrial Park" },
    { "KF01", "Kelp Forest" },
    { "KF02", "Kelp Swamp" },
    { "KF04", "Kelp Caves" },
    { "KF05", "Kelp Vines" },
    { "GY01", "Graveyard Lake" },
    { "GY02", "Graveyard of Ships" },
    { "GY03", "Dutchman's Ship" },
    { "GY04", "Flying Dutchman Battle" },
    { "DB01", "SpongeBob's Dream" },
    { "DB02", "Sandy's Dream" },
    { "DB03", "Squidward's Dream" },
    { "DB04", "Mr. Krabs' Dream" },
    { "DB05", "Patrick's Dream (Unused)" },
    { "DB06", "Patrick's Mind" },
    { "B301", "MuscleBob (Unused)" },
    { "B302", "Kah - Rah - Tae!" },
    { "B303", "The Small Shall Rule... or Not" },
    { "PG12", "SpongeBall Arena" },
};

// Subtype #defines local to zEntPickup.cpp; the viewer keeps the few it
// reads. Names must stay in sync with that file.
#define IIMGUI_PICKUP_TYPE_SHINY 0
#define IIMGUI_PICKUP_TYPE_1 1
#define IIMGUI_PICKUP_SPATULA 0
#define IIMGUI_PICKUP_UNDERWEAR 1
#define IIMGUI_PICKUP_GOLDEN_UNDERWEAR 2
#define IIMGUI_PICKUP_SUNDAE 5
#define IIMGUI_PICKUP_SOCK 6
#define IIMGUI_PICKUP_SPONGEBALL 7

// The boot.HIP reward placeholders: isRewardPickup at zEntPickup.cpp:242
// matches these names by hash; the viewer has the name string in hand and
// checks the prefix instead. Directory names arrive uppercase.
static bool iImguiIsBootReward(const char* name)
{
    static const char kPrefix[] = "boot_reward_pickup";
    for (size_t i = 0; i < sizeof(kPrefix) - 1; ++i)
    {
        if (tolower((unsigned char)name[i]) != kPrefix[i])
        {
            return false;
        }
    }
    return name[sizeof(kPrefix) - 1] != '\0';
}

static bool iImguiSearchHit(const char* hay, const char* needle)
{
    if (needle[0] == '\0')
    {
        return true;
    }

    for (const char* h = hay; *h != '\0'; ++h)
    {
        const char* a = h;
        const char* b = needle;
        while (*a != '\0' && *b != '\0' &&
               tolower((unsigned char)*a) == tolower((unsigned char)*b))
        {
            ++a;
            ++b;
        }
        if (*b == '\0')
        {
            return true;
        }
    }
    return false;
}

struct iImguiEntRow
{
    xBase* b;
    const char* name;
};

static iImguiEntRow* sEntRows = NULL;
static U32 sEntRowCount = 0;
static zScene* sEntRowsScene = NULL;
static U32 sEntRowsNumBase = 0;

static int iImguiEntRowCmp(const void* a, const void* b)
{
    const iImguiEntRow* ra = (const iImguiEntRow*)a;
    const iImguiEntRow* rb = (const iImguiEntRow*)b;
    int c = strcmp(ra->name, rb->name);
    if (c != 0)
    {
        return c;
    }
    return ra->b->id < rb->b->id ? -1 : 1;
}

// Rebuilt whenever scene->base[] changes: the array is sized at load and
// grows again for every runtime spawn, so the row list follows it.
static void iImguiBuildEntRows()
{
    zScene* sc = globals.sceneCur;
    U32 nb = sc != NULL ? sc->num_base : 0;

    if (sc == sEntRowsScene && nb == sEntRowsNumBase)
    {
        return;
    }

    free(sEntRows);
    sEntRows = NULL;
    sEntRowCount = 0;
    sEntRowsScene = sc;
    sEntRowsNumBase = nb;

    sTypeFilterNames[0] = "All";
    sTypeFilterTypes[0] = -1;
    sTypeFilterCount = 1;

    if (sc == NULL || sc->base == NULL || nb == 0)
    {
        return;
    }

    sEntRows = (iImguiEntRow*)malloc(nb * sizeof(iImguiEntRow));
    if (sEntRows == NULL)
    {
        return;
    }

    U32 n = 0;
    for (U32 i = 0; i < nb; ++i)
    {
        if (sc->base[i] == NULL)
        {
            continue;
        }
        sEntRows[n].b = sc->base[i];
        sEntRows[n].name = zSceneGetName(sc->base[i]);
        ++n;
    }
    sEntRowCount = n;
    qsort(sEntRows, sEntRowCount, sizeof(iImguiEntRow), iImguiEntRowCmp);

    bool present[eBaseTypeCount];
    memset(present, 0, sizeof(present));

    for (U32 i = 0; i < n; ++i)
    {
        U32 t = sEntRows[i].b->baseType;
        if (t < eBaseTypeCount)
        {
            present[t] = true;
        }
    }

    for (S32 t = 0; t < eBaseTypeCount; ++t)
    {
        if (present[t])
        {
            sTypeFilterNames[sTypeFilterCount] = iImguiBaseTypeName((U32)t);
            sTypeFilterTypes[sTypeFilterCount] = t;
            ++sTypeFilterCount;
        }
    }
    if (sTypeFilter >= sTypeFilterCount)
    {
        sTypeFilter = 0;
    }
}

// The scene-load marker placement (zScene.cpp:1412-1448) is the precedent:
// both matrices must move or physics and rendering disagree, and velocity
// zeroed the way the player-reset event does.
static void iImguiTeleportPlayerTo(const xEnt* tgt)
{
    const xVec3* t = iImguiEntPos(tgt);
    xVec3* p = &globals.player.ent.frame->mat.pos;

    if (t == NULL)
    {
        return;
    }

    p->x = t->x;
    p->y = t->y + 1.0f;
    p->z = t->z;
    xVec3Copy((xVec3*)&globals.player.ent.model->Mat->pos, p);

    globals.player.ent.frame->vel.x = 0.0f;
    globals.player.ent.frame->vel.y = 0.0f;
    globals.player.ent.frame->vel.z = 0.0f;

    oob_player_teleported = true;
}

// Collect is the retail entry point (zEntPickup_DoPickup:788) with the model
// snapped onto the player so the normal in-flight finalize (collectPickup)
// lands on the next update instead of flying across the level.
static void iImguiCollectPickup(zEntPickup* pk)
{
    if (pk->p == NULL)
    {
        return;
    }

    zEntPickup_DoPickup(pk);

    if (pk->model != NULL && globals.player.ent.model != NULL)
    {
        pk->model->Mat->pos.x = globals.player.ent.model->Mat->pos.x;
        pk->model->Mat->pos.y = globals.player.ent.model->Mat->pos.y;
        pk->model->Mat->pos.z = globals.player.ent.model->Mat->pos.z;
    }
}

// Restore is the entity reset plus the inventory reverse of
// zEntPickup_GivePickup (:812). zEntEvent(eEventReset) runs the retail reset
// -- state back to idle, matrices back to the asset, un-hidden -- because
// zEntPickup_Reset does not touch inventory.
static void iImguiRestorePickup(zEntPickup* pk)
{
    zEntEvent(pk, eEventReset);

    if (pk->p == NULL)
    {
        return;
    }

    if (pk->p->pickupType == IIMGUI_PICKUP_TYPE_SHINY)
    {
        S32 v = 0;
        switch (pk->p->pickupIndex)
        {
        case 0: v = globals.player.g.ShinyValuePurple; break;
        case 1: v = globals.player.g.ShinyValueBlue; break;
        case 2: v = globals.player.g.ShinyValueGreen; break;
        case 3: v = globals.player.g.ShinyValueYellow; break;
        case 4: v = globals.player.g.ShinyValueRed; break;
        }
        zEntPlayer_GiveShinyObject(-v);
    }
    else if (pk->p->pickupType == IIMGUI_PICKUP_TYPE_1)
    {
        switch (pk->p->pickupIndex)
        {
        case IIMGUI_PICKUP_SPATULA:
            if (globals.player.Inv_Spatula > 0)
            {
                globals.player.Inv_Spatula--;
            }
            break;
        case IIMGUI_PICKUP_UNDERWEAR:
            if (globals.player.Health > 0)
            {
                globals.player.Health--;
            }
            break;
        case IIMGUI_PICKUP_GOLDEN_UNDERWEAR:
            if (globals.player.MaxHealth > 0)
            {
                globals.player.MaxHealth--;
            }
            if (globals.player.Health > globals.player.MaxHealth)
            {
                globals.player.Health = globals.player.MaxHealth;
            }
            break;
        case IIMGUI_PICKUP_SUNDAE:
            globals.player.SundaeTimer = 0.0f;
            globals.player.SpeedMult = 1.0f;
            break;
        case IIMGUI_PICKUP_SOCK:
            zEntPlayer_GivePatsSocksCurrentLevel(-1);
            break;
        case IIMGUI_PICKUP_SPONGEBALL:
            zEntPlayer_setBoulderMode(0);
            break;
        default:
            zEntPlayer_GiveLevelPickupCurrentLevel(-1);
            break;
        }
    }
}

static void iImguiWarpTo(const char* name)
{
    if (globals.sceneCur == NULL)
    {
        return;
    }

    // The soak test's scene switch, recipe and all -- except the sceneID
    // endianness, which the soak code gets wrong on a little-endian host: the
    // consumers read passet->sceneID as raw bytes. The in-game branch
    // (zGameUpdateMode) assembles nextSceneID as b0<<24|b1<<16|b2<<8|b3 from
    // the field's memory bytes, and the title path (zMenuLoop) reverses them;
    // both come out right only when the memory bytes are the name in order,
    // which is the big-endian byte layout -- so the U32 value stored here is
    // the tag byteswapped. That matches what real portal assets carry, which
    // is why retail portals work through the same code.
    gGameState = eGameState_SceneSwitch;
    dummyPortalAsset.assetCameraID = 0;
    dummyPortalAsset.assetMarkerID = 0;
    dummyPortalAsset.ang = 0.0f;
    dummyPortalAsset.sceneID = ((U32)name[3] << 24) | ((U32)name[2] << 16) |
                               ((U32)name[1] << 8) | (U32)name[0];
    dummyPortal.passet = &dummyPortalAsset;
    globals.sceneCur->pendingPortal = &dummyPortal;
    sLastWarpName = name;
}

const char* iImguiLastWarpName()
{
    return sLastWarpName;
}

static void iImguiReloadLevel()
{
    if (globals.sceneCur == NULL)
    {
        return;
    }

    U32 id = globals.sceneCur->sceneID;
    char name[5];
    name[0] = (char)((id >> 24) & 0xff);
    name[1] = (char)((id >> 16) & 0xff);
    name[2] = (char)((id >> 8) & 0xff);
    name[3] = (char)(id & 0xff);
    name[4] = '\0';

    g_hiphopForcePortal = 1;
    g_hiphopReloadHIP = 1;

    gGameState = eGameState_SceneSwitch;
    dummyPortalAsset.assetCameraID = 0;
    dummyPortalAsset.assetMarkerID = 0;
    dummyPortalAsset.ang = 0.0f;
    dummyPortalAsset.sceneID = ((U32)name[3] << 24) | ((U32)name[2] << 16) |
                               ((U32)name[1] << 8) | (U32)name[0];
    dummyPortal.passet = &dummyPortalAsset;
    globals.sceneCur->pendingPortal = &dummyPortal;
}

static void iImguiKillPlayer()
{
    globals.player.DamageTimer = 0.0f;
    globals.player.Health = 0;
}

bool iImguiInGame()
{
    return gGameMode == eGameMode_Game && gGameState == eGameState_Play;
}

static const char* sDebugUIIniPath = "debugui.ini";

static void iImguiLoadWindowState()
{
    FILE* f = fopen(sDebugUIIniPath, "r");

    if (f == NULL)
    {
        return;
    }

    char line[128];

    while (fgets(line, sizeof(line), f) != NULL)
    {
        char* eq = strchr(line, '=');

        if (eq == NULL)
        {
            continue;
        }

        *eq = '\0';
        const char* key = line;
        int val = atoi(eq + 1);
        bool on = val != 0;

        if (strcmp(key, "demo") == 0)
        {
            sShowDemo = on;
        }
        else if (strcmp(key, "warp") == 0)
        {
            sShowWarp = on;
        }
        else if (strcmp(key, "globals") == 0)
        {
            sShowGlobals = on;
        }
        else if (strcmp(key, "entities") == 0)
        {
            sShowEntities = on;
        }
        else if (strcmp(key, "spawn") == 0)
        {
            sShowSpawn = on;
        }
        else if (strcmp(key, "perf") == 0)
        {
            sShowPerf = on;
        }
        else if (strcmp(key, "mem") == 0)
        {
            sShowMem = on;
        }
        else if (strcmp(key, "ini") == 0)
        {
            sShowIni = on;
        }
    }

    fclose(f);
}

static void iImguiSaveWindowState()
{
    FILE* f = fopen(sDebugUIIniPath, "w");

    if (f == NULL)
    {
        return;
    }

    fprintf(f, "demo=%d\n", sShowDemo ? 1 : 0);
    fprintf(f, "warp=%d\n", sShowWarp ? 1 : 0);
    fprintf(f, "globals=%d\n", sShowGlobals ? 1 : 0);
    fprintf(f, "entities=%d\n", sShowEntities ? 1 : 0);
    fprintf(f, "spawn=%d\n", sShowSpawn ? 1 : 0);
    fprintf(f, "perf=%d\n", sShowPerf ? 1 : 0);
    fprintf(f, "mem=%d\n", sShowMem ? 1 : 0);
    fprintf(f, "ini=%d\n", sShowIni ? 1 : 0);

    fclose(f);
}

void iImguiInit()
{
    if (sImguiReady)
    {
        return;
    }

    // After RwEngineStart, not merely RwEngineOpen: the font atlas is
    // uploaded through the driver, and there is no device until start.
    IMGUI_CHECKVERSION();
    ImGui_ImplRW_Init();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "imgui.ini"; // beside the executable, next to config.ini

    ImGui::StyleColorsDark();

    iImguiLoadWindowState();

    sImguiReady = true;
}

static void iImguiFeedKeyboard()
{
    static bool sPrev[SDL_SCANCODE_COUNT];
    static bool sInited;

    int numkeys = 0;
    const bool* keys = SDL_GetKeyboardState(&numkeys);

    if (keys == NULL)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    if (!sInited)
    {
        memset(sPrev, 0, sizeof(sPrev));
        sInited = true;
    }

    int n = numkeys < SDL_SCANCODE_COUNT ? numkeys : SDL_SCANCODE_COUNT;

    for (int sc = 0; sc < n; sc++)
    {
        bool down = keys[sc];

        if (down == sPrev[sc])
        {
            continue;
        }

        sPrev[sc] = down;

        ImGuiKey key = ImGuiKey_None;

        if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        {
            key = (ImGuiKey)(ImGuiKey_A + (sc - SDL_SCANCODE_A));
        }
        else if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
        {
            key = (ImGuiKey)(ImGuiKey_1 + (sc - SDL_SCANCODE_1));
        }
        else if (sc == SDL_SCANCODE_0)
        {
            key = ImGuiKey_0;
        }
        else if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
        {
            key = (ImGuiKey)(ImGuiKey_F1 + (sc - SDL_SCANCODE_F1));
        }
        else
        {
            switch (sc)
            {
            case SDL_SCANCODE_SPACE: key = ImGuiKey_Space; break;
            case SDL_SCANCODE_APOSTROPHE: key = ImGuiKey_Apostrophe; break;
            case SDL_SCANCODE_COMMA: key = ImGuiKey_Comma; break;
            case SDL_SCANCODE_MINUS: key = ImGuiKey_Minus; break;
            case SDL_SCANCODE_PERIOD: key = ImGuiKey_Period; break;
            case SDL_SCANCODE_SLASH: key = ImGuiKey_Slash; break;
            case SDL_SCANCODE_SEMICOLON: key = ImGuiKey_Semicolon; break;
            case SDL_SCANCODE_EQUALS: key = ImGuiKey_Equal; break;
            case SDL_SCANCODE_LEFTBRACKET: key = ImGuiKey_LeftBracket; break;
            case SDL_SCANCODE_BACKSLASH: key = ImGuiKey_Backslash; break;
            case SDL_SCANCODE_RIGHTBRACKET: key = ImGuiKey_RightBracket; break;
            case SDL_SCANCODE_GRAVE: key = ImGuiKey_GraveAccent; break;
            case SDL_SCANCODE_CAPSLOCK: key = ImGuiKey_CapsLock; break;
            case SDL_SCANCODE_SCROLLLOCK: key = ImGuiKey_ScrollLock; break;
            case SDL_SCANCODE_NUMLOCKCLEAR: key = ImGuiKey_NumLock; break;
            case SDL_SCANCODE_PRINTSCREEN: key = ImGuiKey_PrintScreen; break;
            case SDL_SCANCODE_PAUSE: key = ImGuiKey_Pause; break;
            case SDL_SCANCODE_LCTRL: key = ImGuiKey_LeftCtrl; break;
            case SDL_SCANCODE_RCTRL: key = ImGuiKey_RightCtrl; break;
            case SDL_SCANCODE_LSHIFT: key = ImGuiKey_LeftShift; break;
            case SDL_SCANCODE_RSHIFT: key = ImGuiKey_RightShift; break;
            case SDL_SCANCODE_LALT: key = ImGuiKey_LeftAlt; break;
            case SDL_SCANCODE_RALT: key = ImGuiKey_RightAlt; break;
            case SDL_SCANCODE_LGUI: key = ImGuiKey_LeftSuper; break;
            case SDL_SCANCODE_RGUI: key = ImGuiKey_RightSuper; break;
            case SDL_SCANCODE_APPLICATION: key = ImGuiKey_Menu; break;
            case SDL_SCANCODE_TAB: key = ImGuiKey_Tab; break;
            case SDL_SCANCODE_RETURN: key = ImGuiKey_Enter; break;
            case SDL_SCANCODE_KP_ENTER: key = ImGuiKey_KeypadEnter; break;
            case SDL_SCANCODE_BACKSPACE: key = ImGuiKey_Backspace; break;
            case SDL_SCANCODE_INSERT: key = ImGuiKey_Insert; break;
            case SDL_SCANCODE_DELETE: key = ImGuiKey_Delete; break;
            case SDL_SCANCODE_HOME: key = ImGuiKey_Home; break;
            case SDL_SCANCODE_END: key = ImGuiKey_End; break;
            case SDL_SCANCODE_PAGEUP: key = ImGuiKey_PageUp; break;
            case SDL_SCANCODE_PAGEDOWN: key = ImGuiKey_PageDown; break;
            case SDL_SCANCODE_LEFT: key = ImGuiKey_LeftArrow; break;
            case SDL_SCANCODE_RIGHT: key = ImGuiKey_RightArrow; break;
            case SDL_SCANCODE_UP: key = ImGuiKey_UpArrow; break;
            case SDL_SCANCODE_DOWN: key = ImGuiKey_DownArrow; break;
            case SDL_SCANCODE_KP_DIVIDE: key = ImGuiKey_KeypadDivide; break;
            case SDL_SCANCODE_KP_MULTIPLY: key = ImGuiKey_KeypadMultiply; break;
            case SDL_SCANCODE_KP_MINUS: key = ImGuiKey_KeypadSubtract; break;
            case SDL_SCANCODE_KP_PLUS: key = ImGuiKey_KeypadAdd; break;
            case SDL_SCANCODE_KP_PERIOD: key = ImGuiKey_KeypadDecimal; break;
            case SDL_SCANCODE_KP_0: key = ImGuiKey_Keypad0; break;
            case SDL_SCANCODE_KP_1: key = ImGuiKey_Keypad1; break;
            case SDL_SCANCODE_KP_2: key = ImGuiKey_Keypad2; break;
            case SDL_SCANCODE_KP_3: key = ImGuiKey_Keypad3; break;
            case SDL_SCANCODE_KP_4: key = ImGuiKey_Keypad4; break;
            case SDL_SCANCODE_KP_5: key = ImGuiKey_Keypad5; break;
            case SDL_SCANCODE_KP_6: key = ImGuiKey_Keypad6; break;
            case SDL_SCANCODE_KP_7: key = ImGuiKey_Keypad7; break;
            case SDL_SCANCODE_KP_8: key = ImGuiKey_Keypad8; break;
            case SDL_SCANCODE_KP_9: key = ImGuiKey_Keypad9; break;
            default: break;
            }
        }

        if (key != ImGuiKey_None)
        {
            io.AddKeyEvent(key, down);
        }
    }

    SDL_Keymod mod = SDL_GetModState();

    io.AddKeyEvent(ImGuiKey_ModCtrl, (mod & SDL_KMOD_CTRL) != 0);
    io.AddKeyEvent(ImGuiKey_ModShift, (mod & SDL_KMOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiKey_ModAlt, (mod & SDL_KMOD_ALT) != 0);
    io.AddKeyEvent(ImGuiKey_ModSuper, (mod & SDL_KMOD_GUI) != 0);

    SDL_Event ev;

    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_EVENT_TEXT_INPUT,
                          SDL_EVENT_TEXT_INPUT) > 0)
    {
        if (ev.text.text != NULL)
        {
            io.AddInputCharactersUTF8(ev.text.text);
        }
    }

    // SDL only posts TEXT_INPUT while text input is active on the window, and
    // the port never starts it elsewhere. Turn it on only while a field has
    // keyboard focus (WantTextInput, set at the end of the previous frame), so
    // characters arrive and the on-screen keyboard stays away otherwise.
    static bool sTextActive;

    if (io.WantTextInput && !sTextActive)
    {
        iWindowStartTextInput();
        sTextActive = true;
    }
    else if (!io.WantTextInput && sTextActive)
    {
        iWindowStopTextInput();
        sTextActive = false;
    }
}

#define IMGUI_TWEAK_SORT_MAX 2048

static tweak_entry* sTweaksSorted[IMGUI_TWEAK_SORT_MAX];
static S32 sTweaksSortedCount;

static int iImguiTweakCmp(const void* a, const void* b)
{
    const tweak_entry* ea = *(tweak_entry* const*)a;
    const tweak_entry* eb = *(tweak_entry* const*)b;
    return strcmp(ea->name, eb->name);
}

static S32 iImguiTweakSegCount(const tweak_entry* e)
{
    S32 n = 1;

    for (const char* s = e->name; *s; ++s)
    {
        if (*s == '|')
        {
            n++;
        }
    }

    return n;
}

static void iImguiTweakSeg(const tweak_entry* e, S32 depth, char* out, S32 outMax)
{
    const char* s = e->name;
    S32 d = 0;

    while (*s && d < depth)
    {
        if (*s == '|')
        {
            d++;
        }
        s++;
    }

    S32 i = 0;

    while (*s && *s != '|' && i < outMax - 1)
    {
        unsigned char c = (unsigned char)*s;
        out[i] = (c >= 32 && c < 127) ? (char)c : ' ';
        i++;
        s++;
    }

    out[i] = '\0';
}

static void iImguiDrawTweakLeaf(tweak_entry* e, const char* label)
{
    ImGui::PushID(e->name);

    switch (e->type)
    {
    case TWEAK_FLOAT:
    {
        F32 v = e->value ? *(F32*)e->value : 0.0f;

        if (ImGui::SliderFloat(label, &v, e->fmin, e->fmax, "%.4g"))
        {
            if (e->value)
            {
                *(F32*)e->value = v;
            }
            xDebugTweaks_Fire(e);
        }
        break;
    }
    case TWEAK_S16:
    {
        S32 v = e->value ? (S32)*(S16*)e->value : 0;

        if (ImGui::SliderInt(label, &v, e->imin, e->imax))
        {
            if (e->value)
            {
                *(S16*)e->value = (S16)v;
            }
            xDebugTweaks_Fire(e);
        }
        break;
    }
    case TWEAK_U8:
    {
        S32 v = e->value ? (S32)*(U8*)e->value : 0;

        if (ImGui::SliderInt(label, &v, e->imin, e->imax))
        {
            if (e->value)
            {
                *(U8*)e->value = (U8)v;
            }
            xDebugTweaks_Fire(e);
        }
        break;
    }
    case TWEAK_U32:
    {
        S32 v = e->value ? (S32)*(U32*)e->value : 0;

        if (ImGui::SliderInt(label, &v, e->imin, e->imax))
        {
            if (e->value)
            {
                *(U32*)e->value = (U32)v;
            }
            xDebugTweaks_Fire(e);
        }
        break;
    }
    case TWEAK_BUTTON:
    {
        const char* btn = e->value ? (const char*)e->value : label;

        if (ImGui::Button(btn))
        {
            xDebugTweaks_Fire(e);
        }
        break;
    }
    case TWEAK_FLAG:
    {
        U32 flags = e->value ? *(U32*)e->value : 0;
        bool on = (flags & e->flag_mask) != 0;

        if (ImGui::Checkbox(label, &on))
        {
            if (e->value)
            {
                if (on)
                {
                    *(U32*)e->value |= e->flag_mask;
                }
                else
                {
                    *(U32*)e->value &= ~e->flag_mask;
                }
            }
            xDebugTweaks_Fire(e);
        }
        break;
    }
    case TWEAK_SELECT:
    {
        if (e->sel_labels == NULL || e->sel_count == 0)
        {
            ImGui::Text("%s", label);
            break;
        }

        U32 cur = e->value ? *(U32*)e->value : 0;
        S32 idx = 0;

        if (e->sel_values != NULL)
        {
            for (U32 i = 0; i < e->sel_count; ++i)
            {
                if (e->sel_values[i] == cur)
                {
                    idx = (S32)i;
                    break;
                }
            }
        }
        else
        {
            idx = (S32)cur;
        }

        if (idx < 0 || idx >= (S32)e->sel_count)
        {
            idx = 0;
        }

        if (ImGui::BeginCombo(label, e->sel_labels[idx]))
        {
            for (U32 i = 0; i < e->sel_count; ++i)
            {
                bool sel = ((S32)i == idx);

                if (ImGui::Selectable(e->sel_labels[i], sel))
                {
                    idx = (S32)i;

                    if (e->value)
                    {
                        *(U32*)e->value = e->sel_values ? e->sel_values[i] : i;
                    }
                    xDebugTweaks_Fire(e);
                }
            }
            ImGui::EndCombo();
        }
        break;
    }
    default:
        ImGui::Text("%s", label);
        break;
    }

    ImGui::PopID();
}

static void iImguiDrawTweakLevel(S32 start, S32 end, S32 depth)
{
    S32 i = start;

    while (i < end)
    {
        if (iImguiTweakSegCount(sTweaksSorted[i]) <= depth)
        {
            i++;
            continue;
        }

        char seg[192];
        iImguiTweakSeg(sTweaksSorted[i], depth, seg, sizeof(seg));

        S32 j = i + 1;

        while (j < end)
        {
            if (iImguiTweakSegCount(sTweaksSorted[j]) <= depth)
            {
                j++;
                continue;
            }

            char seg2[192];
            iImguiTweakSeg(sTweaksSorted[j], depth, seg2, sizeof(seg2));

            if (strcmp(seg, seg2) != 0)
            {
                break;
            }
            j++;
        }

        bool hasDeeper = false;

        for (S32 k = i; k < j; ++k)
        {
            if (iImguiTweakSegCount(sTweaksSorted[k]) > depth + 1)
            {
                hasDeeper = true;
                break;
            }
        }

        if (!hasDeeper)
        {
            iImguiDrawTweakLeaf(sTweaksSorted[i], seg);
        }
        else
        {
            ImGui::PushID(i);

            if (ImGui::TreeNode(seg))
            {
                for (S32 k = i; k < j; ++k)
                {
                    if (iImguiTweakSegCount(sTweaksSorted[k]) == depth + 1)
                    {
                        char leaf[192];
                        iImguiTweakSeg(sTweaksSorted[k], depth, leaf, sizeof(leaf));
                        iImguiDrawTweakLeaf(sTweaksSorted[k], leaf);
                    }
                }

                iImguiDrawTweakLevel(i, j, depth + 1);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        i = j;
    }
}

void iImguiNewFrame()
{
    if (!sImguiReady)
    {
        return;
    }

    // The backend has no clock -- timeDelta is the frame time in seconds and
    // it is ours to supply. The first frame has nothing to subtract and takes
    // a nominal 60 Hz step. Clamped both ways: a zero delta (two calls in one
    // tick, i.e. a broken frame contract) and a multi-second one (breakpoint,
    // level load) both misbehave downstream.
    iTime now = iTimeGet();
    F32 delta = sHaveLastFrame ? iTimeDiffSec(now - sLastFrame) : (1.0f / 60.0f);
    sLastFrame = now;
    sHaveLastFrame = true;

    if (delta < 1.0e-5f)
    {
        delta = 1.0e-5f;
    }
    else if (delta > 0.1f)
    {
        delta = 0.1f;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(iScreenWidthF(), iScreenHeightF());
    iImguiFeedMouse();
    iImguiFeedKeyboard();

    ImGui_ImplRW_NewFrame(delta);

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Debug"))
        {
            ImGui::MenuItem("Level Warp", NULL, &sShowWarp);
            ImGui::MenuItem("Toggle Globals", NULL, &sShowGlobals);
            ImGui::MenuItem("Scene Entity Viewer", NULL, &sShowEntities);
            ImGui::MenuItem("Dynamic Spawning", NULL, &sShowSpawn);
            ImGui::MenuItem("Performance", NULL, &sShowPerf);
            ImGui::MenuItem("Memory Viewer", NULL, &sShowMem);
            ImGui::MenuItem("Debug Tweaks", NULL, &sShowTweaks);
            ImGui::MenuItem("SB.INI Editor", NULL, &sShowIni);
            ImGui::MenuItem("ImGui Demo", NULL, &sShowDemo);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (sShowDemo)
    {
        // The demo is the smoke test: every widget in the library.
        ImGui::ShowDemoWindow(&sShowDemo);
    }

    if (sShowWarp)
    {
        if (ImGui::Begin("Level Warp", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            static const int sSceneCount =
                (int)(sizeof(sSceneList) / sizeof(sSceneList[0]));
            static int sWarpScene = 1;

            char preview[80];
            snprintf(preview, sizeof(preview), "%s - %s",
                     sSceneList[sWarpScene].code, sSceneList[sWarpScene].name);
            if (ImGui::BeginCombo("Scene", preview))
            {
                for (int i = 0; i < sSceneCount; ++i)
                {
                    char label[80];
                    snprintf(label, sizeof(label), "%s - %s",
                             sSceneList[i].code, sSceneList[i].name);
                    if (ImGui::Selectable(label, i == sWarpScene))
                    {
                        sWarpScene = i;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Warp"))
            {
                iImguiWarpTo(sSceneList[sWarpScene].code);
            }
        }
        ImGui::End();
    }

    if (sShowGlobals)
    {
        if (ImGui::Begin("Toggle Globals", &sShowGlobals,
                         ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
            {
            ImGui::SetTooltip(
                "Cheat Mode: fly. Spin rises, bounce sinks, hold a\n"
                "shoulder for the 3x boost. Zero damage, no OOB\n"
                "catch. Death and scene loads clear it.\n"
                "Force Cheat Mode: re-applies it every frame so\n"
                "those resets cannot take it away.\n"
                "Noclip: no collision at all, walls and floor\n"
                "included. Pickups still collect. Needs Cheat Mode\n"
                "or Force on.\n"
                "Disable Hans: trigger OOBs cannot grab. Sand still\n"
                "kills -- the half-hand-disable glitch.\n"
                "Full Hans Disable: the save-warp glitch. Surface\n"
                "Hans and sand stop too. The save icon may stay up;\n"
                "grabbing a spatula clears it.\n"
                "Delete spawns on reset: runtime-spawned NPCs are\n"
                "removed on death/scene reset. Off keeps them and\n"
                "re-anchors their arena to the player instead.");
            }

            // The player position, as integers -- the debug tools this port
            // ships alongside all print and set whole units. The pos helper
            // is the null guard: the player entity has no model or frame on
            // the title screen.
            const xVec3* ppos = iImguiEntPos(&globals.player.ent);
            if (ppos != NULL)
            {
                ImGui::Text("Player: %d %d %d", (int)ppos->x, (int)ppos->y,
                            (int)ppos->z);
            }
            else
            {
                ImGui::Text("Player: n/a");
            }

            ImGui::Separator();

            ImGui::Text("mode: %s", iImguiGameModeName(gGameMode));
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputText("set mode", sModeBuf, sizeof(sModeBuf));
            ImGui::SameLine();
            if (ImGui::Button("Set"))
            {
                S32 mode = iImguiParseGameMode(sModeBuf);
                if (mode >= 0)
                {
                    iCheatRequestGameMode(mode);
                }
            }

            ImGui::Separator();

            // Both checkboxes read the live state every frame rather than
            // keeping their own copy: zEntPlayerReset clears cheat_mode on
            // death and scene loads, and a mirrored bool would go stale and
            // force the user to uncheck/recheck. Fly semantics live in
            // iCheats.h. Noclip sits between the rows, indented past the
            // first checkbox, so it reads as belonging to both.
            bool fly = globals.player.cheat_mode != 0;
            if (ImGui::Checkbox("Cheat Mode", &fly))
            {
                iCheatSetFlyMode(fly);
            }
            ImGui::Indent(ImGui::GetFrameHeight() +
                          ImGui::GetStyle().ItemInnerSpacing.x);
            bool canNoclip = fly || iCheatIsForceFlyMode();
            if (!canNoclip)
            {
                ImGui::BeginDisabled();
            }
            bool noclip = iCheatIsNoclip() != 0;
            if (ImGui::Checkbox("Noclip", &noclip))
            {
                iCheatSetNoclip(noclip);
            }
            if (!canNoclip)
            {
                ImGui::EndDisabled();
            }
            ImGui::Unindent();
            bool force = iCheatIsForceFlyMode() != 0;
            if (ImGui::Checkbox("Force Cheat Mode", &force))
            {
                iCheatSetForceFlyMode(force);
            }

            ImGui::Separator();

            // Disable Hans is the hand-disable glitch in one call: bit 0x4
            // of the OOB state's shared flags means "hand on screen", the
            // grab entry (force_start) refuses while it is set, and only a
            // completed grab->drop clears it -- so a level transition during
            // a grab leaves it stuck and the hand can never grab again. The
            // sand death plane is a separate event handler and still works,
            // which is the half-hand-disable the glitch is known for.
            bool hand = oob_state::hand_disabled();
            if (ImGui::Checkbox("Disable Hans", &hand))
            {
                oob_state::set_hand_disabled(hand);
            }

            // Full Hans Disable is what a warp during the spatula autosave
            // leaves behind: zSaveLoadPreAutoSave's flag (the core the
            // speedrun glitch relies on -- retail checks it at
            // oob_state::update and the surface-Hans/sand path dies with the
            // state machine) plus the bit-0x4 trigger block, because retail's
            // force_start does NOT check the save flag and an asset OOB
            // trigger would otherwise start a grab that can never advance.
            // The stack is exactly what the community taxi-then-save-warp
            // sequence produces. zEntPlayerReset never clears the save flag,
            // which is why the glitch -- and the save icon -- persist.
            bool full = zSaveLoadGetPreAutoSave() != 0;
            if (ImGui::Checkbox("Full Hans Disable", &full))
            {
                zSaveLoadPreAutoSave(full);
                oob_state::set_hand_disabled(full);
            }

            ImGui::Separator();
            ImGui::Text("Powerups");

            bool bubbleBowl = globals.player.g.PowerUp[0] != 0;
            if (ImGui::Checkbox("Bubble Bowl Powerup", &bubbleBowl))
            {
                globals.player.g.PowerUp[0] = bubbleBowl ? 1 : 0;
            }

            bool cruiseBubble = globals.player.g.PowerUp[1] != 0;
            if (ImGui::Checkbox("Cruise Bubble Powerup", &cruiseBubble))
            {
                globals.player.g.PowerUp[1] = cruiseBubble ? 1 : 0;
            }

            if (ImGui::Button("Trigger Spongeball"))
            {
                zEntPlayer_setBoulderMode(1);
            }

            ImGui::Separator();
            ImGui::Text("Character");

            if (ImGui::Button("Swap Character"))
            {
                // Cycle like the bus stop: SpongeBob -> Patrick -> Sandy ->
                // SpongeBob, but only step onto a character whose model is
                // actually loaded. The model-apply switch in zEntPlayer also
                // guards on the model, so a missing asset would otherwise
                // silently keep the old model with the new id.
                static const _CurrentPlayer kOrder[] = { eCurrentPlayerSpongeBob,
                                                         eCurrentPlayerPatrick,
                                                         eCurrentPlayerSandy };

                                        for (int step = 1; step <= 3; ++step)
                                        {
                                            int idx = ((int)gCurrentPlayer + step) % 3;
                                            _CurrentPlayer cand = kOrder[idx];

                                            xModelInstance* mdl = NULL;
                                            switch (cand)
                                            {
                                            case eCurrentPlayerSpongeBob:
                                                mdl = globals.player.model_spongebob;
                                                break;
                                            case eCurrentPlayerPatrick:
                                                mdl = globals.player.model_patrick;
                                                break;
                                            case eCurrentPlayerSandy:
                                                mdl = globals.player.model_sandy;
                                                break;
                                            default:
                                                break;
                                            }

                                            if (mdl != NULL)
                                            {
                                                gCurrentPlayer = cand;
                                                break;
                                            }
                                        }
            }

            static const char* kPlayerNames[] = { "SpongeBob", "Patrick", "Sandy" };
            const char* curName = "?";
            if (gCurrentPlayer >= eCurrentPlayerSpongeBob && gCurrentPlayer < eCurrentPlayerCount)
            {
                curName = kPlayerNames[gCurrentPlayer];
            }
            ImGui::Text("current: %s", curName);

            ImGui::Separator();
            ImGui::Text("Movement");

            xEntFrame* frame = globals.player.ent.frame;

            if (frame != NULL)
            {
                const xVec3& vel = frame->vel;
                F32 speed = xsqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);

                ImGui::Text("vel: %.2f %.2f %.2f  (|%.2f|)", vel.x, vel.y, vel.z, speed);
            }

            ImGui::Text("bubble bowl: %s  initVel: %.2f  min: %.2f",
                        globals.player.IsBubbleBowling ? "YES" : "no",
                        globals.player.bbowlInitVel,
                        globals.player.g.BubbleBowlMinSpeed);

            static float sSetSpeed = 0.0f;
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputFloat("set speed", &sSetSpeed, 0.5f, 2.0f, "%.2f");
            ImGui::SameLine();
            if (ImGui::Button("apply") && frame != NULL)
            {
                xVec3& vel = frame->vel;
                F32 len = xsqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);

                if (len > 0.0001f)
                {
                    F32 s = sSetSpeed / len;
                    vel.x *= s;
                    vel.y *= s;
                    vel.z *= s;
                }
                else
                {
                    RwMatrix* mat = globals.player.ent.model->Mat;
                    vel.x = mat->at.x * sSetSpeed;
                    vel.y = mat->at.y * sSetSpeed;
                    vel.z = mat->at.z * sSetSpeed;
                }
            }

            ImGui::Separator();
            ImGui::Text("Debug Tools");

            if (ImGui::Button("Reload Level"))
            {
                iImguiReloadLevel();
            }

            ImGui::SameLine();
            if (ImGui::Button("Kill Player"))
            {
                iImguiKillPlayer();
            }

            ImGui::SameLine();
            if (ImGui::Button("Sundae Powerup"))
            {
                globals.player.SundaeTimer = globals.player.g.SundaeTime;
                globals.player.SpeedMult = globals.player.g.SundaeMult;
            }

            {
                bool td = globals.player.g.TakeDamage != 0;

                if (ImGui::Checkbox("Take Damage", &td))
                {
                    globals.player.g.TakeDamage = td ? 1 : 0;
                }
            }

            ImGui::SetNextItemWidth(120.0f);

            if (ImGui::InputFloat("gravity", &globals.player.g.Gravity, 1.0f, 5.0f, "%.2f"))
            {
                globals.player.Jump_CurrGravity = globals.player.g.Gravity;
                CalcJumpImpulse(&globals.player.sb.Jump, &globals.player.sb);
                CalcJumpImpulse(&globals.player.sb.Double, &globals.player.sb);
                CalcJumpImpulse(&globals.player.sb.Bounce, &globals.player.sb);
                CalcJumpImpulse(&globals.player.sb.Spring, &globals.player.sb);
                CalcJumpImpulse(&globals.player.sb.Wall, &globals.player.sb);
                CalcJumpImpulse(&globals.player.patrick.Jump, &globals.player.patrick);
                CalcJumpImpulse(&globals.player.patrick.Double, &globals.player.patrick);
                CalcJumpImpulse(&globals.player.patrick.Bounce, &globals.player.patrick);
                CalcJumpImpulse(&globals.player.patrick.Spring, &globals.player.patrick);
                CalcJumpImpulse(&globals.player.patrick.Wall, &globals.player.patrick);
                CalcJumpImpulse(&globals.player.sandy.Jump, &globals.player.sandy);
                CalcJumpImpulse(&globals.player.sandy.Double, &globals.player.sandy);
                CalcJumpImpulse(&globals.player.sandy.Bounce, &globals.player.sandy);
                CalcJumpImpulse(&globals.player.sandy.Spring, &globals.player.sandy);
                CalcJumpImpulse(&globals.player.sandy.Wall, &globals.player.sandy);
            }

            ImGui::Text("walk / run speed");
            ImGui::SetNextItemWidth(90.0f);
            ImGui::InputFloat("SB##w", &globals.player.sb.MoveSpeed[1], 0.1f, 0.5f, "%.2f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::InputFloat("##sbr", &globals.player.sb.MoveSpeed[2], 0.1f, 0.5f, "%.2f");
            ImGui::SetNextItemWidth(90.0f);
            ImGui::InputFloat("Patrick##w", &globals.player.patrick.MoveSpeed[1], 0.1f, 0.5f, "%.2f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::InputFloat("##patr", &globals.player.patrick.MoveSpeed[2], 0.1f, 0.5f, "%.2f");
            ImGui::SetNextItemWidth(90.0f);
            ImGui::InputFloat("Sandy##w", &globals.player.sandy.MoveSpeed[1], 0.1f, 0.5f, "%.2f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::InputFloat("##sandr", &globals.player.sandy.MoveSpeed[2], 0.1f, 0.5f, "%.2f");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputFloat("bowl min speed", &globals.player.g.BubbleBowlMinSpeed, 0.1f, 0.5f,
                              "%.2f");

            ImGui::Separator();
            ImGui::Text("Debug Rendering");

            ImGui::Checkbox("##dbg_on", &gDbgRender.on);
            ImGui::SameLine();
            ImGui::Text("master");

            if (gDbgRender.on)
            {
                if (ImGui::BeginChild("dbg_render_subs", ImVec2(300.0f, 170.0f), true))
                {
                    ImGui::Checkbox("AI prediction", &gDbgRender.predict);
                    ImGui::Checkbox("robot kill hitbox", &gDbgRender.robotkill);
                    ImGui::Checkbox("nav path / chase", &gDbgRender.nav);
                    ImGui::Checkbox("player arrows", &gDbgRender.playerarrow);
                    ImGui::Checkbox("boss marker", &gDbgRender.boss);
                    ImGui::Checkbox("projectile path", &gDbgRender.projectile);
                    ImGui::Checkbox("boulder path", &gDbgRender.boulder);
                    ImGui::Checkbox("attack viz", &gDbgRender.attack);
                    ImGui::Checkbox("platform path", &gDbgRender.platpath);
                }
                ImGui::EndChild();

                if (gDbgRender.boss)
                {
                    static zDbgBossInfo sBosses[16];
                    S32 bossCnt = zDbgCollectBosses(sBosses, 16);

                    if (bossCnt > 0)
                    {
                        ImGui::Separator();
                        ImGui::Text("Bosses (%d)", bossCnt);

                        for (S32 b = 0; b < bossCnt; ++b)
                        {
                            char goal[5];
                            zDbgGoalFourCC(sBosses[b].goalID, goal);

                            ImGui::Text(
                                "%s  pos(%.1f %.1f %.1f)\n  goal %s  vuln 0x%08X  invuln %.2f",
                                zDbgTypeName(sBosses[b].type), sBosses[b].pos.x,
                                sBosses[b].pos.y, sBosses[b].pos.z, goal,
                                (unsigned)sBosses[b].flg_vuln, sBosses[b].tmr_invuln);
                        }
                    }
                }
            }
        }
        ImGui::End();
    }

    if (sShowIni)
    {
        if (ImGui::Begin("SB.INI Editor", &sShowIni))
        {
            if (sIniCount == 0)
            {
                if (ImGui::Button("Load from disk"))
                {
                    iImguiINILoadFromDisk();
                }

                ImGui::TextDisabled("Press Load to read SB.INI into the editor.");
            }
            else
            {
                if (ImGui::Button("Apply && Reload"))
                {
                    iImguiINIWriteAndReload();
                }

                ImGui::SameLine();
                if (ImGui::Button("Restore Defaults"))
                {
                    iImguiINIApplyDefaults();
                }

                ImGui::SameLine();
                if (ImGui::Button("Reload from disk"))
                {
                    iImguiINILoadFromDisk();
                }

                if (!sIniHaveDefaults)
                {
                    ImGui::TextDisabled("No boot defaults captured.");
                }

                ImGui::TextDisabled("Apply writes SB.INI (comments kept) and reloads live.");
                ImGui::Separator();

                if (ImGui::BeginChild("ini_vals", ImVec2(0, 0), true))
                {
                    for (int i = 0; i < sIniCount; ++i)
                    {
                        ImGui::PushID(i);
                        ImGui::SetNextItemWidth(200.0f);
                        ImGui::InputText("##k", sIniKeys[i], sizeof(sIniKeys[0]),
                                         ImGuiInputTextFlags_ReadOnly);
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(-1.0f);
                        ImGui::InputText("##v", sIniVals[i], sizeof(sIniVals[0]));
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    if (sShowTweaks)
    {
        if (ImGui::Begin("Debug Tweaks", &sShowTweaks))
        {
            S32 cnt = xDebugTweaks_Count();
            ImGui::Text("tweaks: %d", cnt);
            ImGui::Separator();

            if (cnt > 0)
            {
                sTweaksSortedCount = cnt;

                if (sTweaksSortedCount > IMGUI_TWEAK_SORT_MAX)
                {
                    sTweaksSortedCount = IMGUI_TWEAK_SORT_MAX;
                }

                for (S32 i = 0; i < sTweaksSortedCount; ++i)
                {
                    sTweaksSorted[i] = xDebugTweaks_Get(i);
                }

                qsort(sTweaksSorted, sTweaksSortedCount, sizeof(tweak_entry*), iImguiTweakCmp);

                if (sTweaksSortedCount > 0)
                {
                    iImguiDrawTweakLevel(0, sTweaksSortedCount, 0);
                }
            }
            else
            {
                ImGui::TextUnformatted("(none registered yet -- load a scene)");
            }
        }
        ImGui::End();
    }

    if (sShowEntities)
    {
        iImguiBuildEntRows();

        if (ImGui::Begin("Scene Entities", &sShowEntities))
        {
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputText("search", sEntitySearch, sizeof(sEntitySearch));
            ImGui::SameLine();
            ImGui::Checkbox("BOOT pickups", &sShowBootPickups);
            ImGui::SameLine();
            bool nocoll = iCheatIsPickupCollectDisabled() != 0;
            if (ImGui::Checkbox("no-collect", &nocoll))
            {
                iCheatSetPickupCollectDisabled(nocoll);
            }
            ImGui::SetNextItemWidth(160.0f);
            ImGui::Combo("type", &sTypeFilter, sTypeFilterNames,
                         sTypeFilterCount);
            ImGui::TextDisabled("Collect=now, HIDE, TELE, Restore=un-give");

            ImVec2 listSize = ImGui::GetContentRegionAvail();
            if (listSize.y < 120.0f)
            {
                listSize.y = 240.0f;
            }

            if (ImGui::BeginChild("list", listSize))
            {
                if (ImGui::BeginTable("ents", 6,
                                      ImGuiTableFlags_SizingStretchProp |
                                          ImGuiTableFlags_Resizable |
                                          ImGuiTableFlags_Borders))
                {
                    ImGui::TableSetupColumn("name",
                                            ImGuiTableColumnFlags_WidthStretch,
                                            3.0f);
                    ImGui::TableSetupColumn("type",
                                            ImGuiTableColumnFlags_WidthStretch,
                                            1.5f);
                    ImGui::TableSetupColumn("state",
                                            ImGuiTableColumnFlags_WidthStretch,
                                            1.5f);
                    ImGui::TableSetupColumn("pos",
                                            ImGuiTableColumnFlags_WidthStretch,
                                            2.0f);
                    ImGui::TableSetupColumn("pick",
                                            ImGuiTableColumnFlags_WidthStretch,
                                            1.0f);
                    ImGui::TableSetupColumn("ent",
                                            ImGuiTableColumnFlags_WidthStretch,
                                            1.0f);
                    ImGui::TableHeadersRow();

                    U32 drawn = 0;

                    for (U32 i = 0; i < sEntRowCount; ++i)
                    {
                        xBase* b = sEntRows[i].b;
                        const char* nm = sEntRows[i].name;

                        if (sTypeFilter > 0 &&
                            (S32)b->baseType != sTypeFilterTypes[sTypeFilter])
                        {
                            continue;
                        }
                        if (iImguiIsBootReward(nm) && !sShowBootPickups)
                        {
                            continue;
                        }
                        if (!iImguiSearchHit(nm, sEntitySearch) &&
                            !iImguiSearchHit(iImguiBaseTypeName(b->baseType),
                                             sEntitySearch))
                        {
                            continue;
                        }

                        ++drawn;
                        ImGui::TableNextRow();
                        ImGui::PushID((int)i);

                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(nm);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(
                            iImguiBaseTypeName(b->baseType));

                        bool hasmodel = iImguiBaseHasModel(b->baseType);
                        zEntPickup* pk = b->baseType == eBaseTypePickup
                                             ? (zEntPickup*)b
                                             : NULL;

                        ImGui::TableSetColumnIndex(2);
                        if (pk != NULL)
                        {
                            // Collected = the final 0x8 bit collectPickup
                            // sets; in-flight = 0x4. Anything else is
                            // touch-collectible or script-held.
                            if (pk->state & 0x8)
                            {
                                ImGui::TextColored(ImColor(80, 220, 80),
                                                   "collected");
                            }
                            else if (pk->state & 0x4)
                            {
                                ImGui::TextColored(ImColor(240, 210, 50),
                                                   "flying");
                            }
                            else
                            {
                                ImGui::TextColored(ImColor(240, 210, 50),
                                                   "ready");
                            }
                        }

                        ImGui::TableSetColumnIndex(3);
                        if (hasmodel)
                        {
                            const xVec3* p = iImguiEntPos((xEnt*)b);
                            if (p != NULL)
                            {
                                ImGui::Text("%d %d %d", (int)p->x, (int)p->y,
                                            (int)p->z);
                            }
                        }

                        ImGui::TableSetColumnIndex(4);
                        if (pk != NULL)
                        {
                            if (pk->state & 0x8)
                            {
                                if (ImGui::SmallButton("Restore"))
                                {
                                    iImguiRestorePickup(pk);
                                }
                            }
                            else if (ImGui::SmallButton("Collect"))
                            {
                                iImguiCollectPickup(pk);
                            }
                        }

                        ImGui::TableSetColumnIndex(5);
                        if (hasmodel)
                        {
                            xEnt* e = (xEnt*)b;
                            const char* hide = (e->flags & 0x1) ? "HIDE"
                                                                : "SHOW";
                            if (ImGui::SmallButton(hide))
                            {
                                if (e->flags & 0x1)
                                {
                                    xEntHide(e);
                                }
                                else
                                {
                                    xEntShow(e);
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("TELE"))
                            {
                                if (globals.player.ent.frame != NULL &&
                                    globals.player.ent.model != NULL)
                                {
                                    iImguiTeleportPlayerTo(e);
                                }
                            }
                        }

                        ImGui::PopID();
                    }

                    if (drawn == 0)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        if (sEntRowCount == 0)
                        {
                            ImGui::TextDisabled(
                                "No Assets Present In Archive");
                        }
                        else
                        {
                            ImGui::TextDisabled("No matching assets");
                        }
                    }

                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    if (sShowSpawn)
    {
        if (ImGui::Begin("Dynamic Spawning", &sShowSpawn,
                         ImGuiWindowFlags_AlwaysAutoResize))
        {
            bool stateOk = zNPCSpawnStateAllows();
            if (!stateOk)
            {
                ImGui::BeginDisabled();
            }

            bool enabled = zNPCSpawnEnabled();
            if (ImGui::Checkbox("Enable spawning", &enabled))
            {
                zNPCSpawnSetEnabled(enabled);
            }

            bool delspawn = zNPCSpawnDeleteOnReset();
            if (ImGui::Checkbox("Delete spawns on reset", &delspawn))
            {
                zNPCSpawnSetDeleteOnReset(delspawn);
            }

            static const char* s_spawnNames[64];
            S32 availCnt = zNPCSpawnAvailCount();

            for (S32 i = 0; i < availCnt; i++)
            {
                s_spawnNames[i] = zNPCSpawnAvailName(i);
            }

            if (availCnt > 0)
            {
                S32 sel = zNPCSpawnSelectedAvail();

                if (sel < 0 || sel >= availCnt)
                {
                    sel = 0;
                }

                if (ImGui::Combo("Enemy", &sel, s_spawnNames, availCnt))
                {
                    zNPCSpawnSelectAvail(sel);
                }
            }
            else
            {
                ImGui::TextDisabled("No robots in this level.");
            }

            F32 radius = zNPCSpawnArenaRadius();
            if (ImGui::SliderFloat("MVPT radius", &radius, 1.0f, 100.0f))
            {
                zNPCSpawnSetArenaRadius(radius);
            }

            F32 interval = zNPCSpawnInterval();
            if (ImGui::SliderFloat("Spawn interval", &interval, 0.05f, 10.0f))
            {
                zNPCSpawnSetInterval(interval);
            }

            if (!stateOk)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Only available in-game or paused.");
            }
        }
        ImGui::End();
    }

    if (sShowPerf)
    {
        if (ImGui::Begin("Performance", &sShowPerf))
        {
            static F32 sFrameMs = 0.0f;
            static F32 sAvgMs = 0.0f;
            static F32 sMinMs = 1.0e9f;
            static F32 sMaxMs = 0.0f;
            static U32 sFrames = 0;
            static F32 sAccum = 0.0f;

            if (sHaveLastFrame)
            {
                sFrameMs = iTimeDiffSec(now - sPrevPerfFrame) * 1000.0f;
            }
            sPrevPerfFrame = now;

            sAccum += sFrameMs;
            sFrames++;
            if (sFrameMs < sMinMs)
            {
                sMinMs = sFrameMs;
            }
            if (sFrameMs > sMaxMs)
            {
                sMaxMs = sFrameMs;
            }

            if (sFrames >= 120)
            {
                sAvgMs = sAccum / (F32)sFrames;
                sAccum = 0.0f;
                sFrames = 0;
                sMinMs = 1.0e9f;
                sMaxMs = 0.0f;
            }

            F32 fps = sAvgMs > 0.0f ? 1000.0f / sAvgMs : 0.0f;
            ImGui::Text("FPS %.1f", (double)fps);
            ImGui::Text("Frame %.2f ms  (avg %.2f)", (double)sFrameMs, (double)sAvgMs);
            ImGui::Text("Min %.2f ms  Max %.2f ms", (double)sMinMs, (double)sMaxMs);

            F64 presentMs = (F64)iWindowPerfPresentNS() / 1.0e6;
            F64 paceMs = (F64)iWindowPerfPaceNS() / 1.0e6;
            F64 busyMs = (F64)sFrameMs - presentMs - paceMs;
            if (busyMs < 0.0)
            {
                busyMs = 0.0;
            }

            ImGui::Separator();
            ImGui::Text("CPU busy %.2f ms", busyMs);
            ImGui::Text("Present %.2f ms  (GPU+vsync)", presentMs);
            ImGui::Text("Pacer sleep %.2f ms", paceMs);

            ImGui::Separator();
            ImGui::Text("Arena heaps (xMem)");

            static const char* sHeapName[3] = { "0", "1", "2" };
            for (U32 h = 0; h < 3; h++)
            {
                U32 used = 0, wasted = 0, size = 0;
                xMemGetHeapStats(h, &used, &wasted, &size);
                F32 pct = size > 0 ? (F32)used / (F32)size * 100.0f : 0.0f;
                ImGui::Text("Heap %s  %6.1f / %6.1f MB  (%.1f%%)  wasted %.1f MB",
                            sHeapName[h], (double)used / 1048576.0,
                            (double)size / 1048576.0, (double)pct,
                            (double)wasted / 1048576.0);
            }

            U32 ovCount = 0, ovBytes = 0, ovFreed = 0;
            xMemGetOverflowStats(&ovCount, &ovBytes, &ovFreed);
            ImGui::Separator();
            ImGui::Text("Heap fallback (malloc)");
            ImGui::Text("Live  %u blocks  %.1f MB", ovCount, (double)ovBytes / 1048576.0);
            ImGui::Text("Freed %.1f MB", (double)ovFreed / 1048576.0);
        }
        ImGui::End();
    }

    if (sShowMem)
    {
        if (!sMemInited)
        {
            sMemInited = true;
            sMemAddr = (unsigned long long)(uintptr_t)&globals;
            snprintf(sMemAddrBuf, sizeof(sMemAddrBuf), "0x%llx", sMemAddr);
        }

        if (ImGui::Begin("Memory Viewer", &sShowMem))
        {
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Read-only hex dump (HxD style).\n"
                    "Left column: offset from the base.\n"
                    "Middle: 16 bytes in hex, split 8 + 8.\n"
                    "Right: printable ASCII ('.' for non-printables).\n"
                    "Type a hex address and press Go to jump.\n"
                    "No editing -- view only.");
            }

            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("goto", sMemAddrBuf, sizeof(sMemAddrBuf),
                                 ImGuiInputTextFlags_CharsHexadecimal |
                                     ImGuiInputTextFlags_EnterReturnsTrue))
            {
                sMemAddr = strtoull(sMemAddrBuf, NULL, 16);
            }
            ImGui::SameLine();
            if (ImGui::Button("Go"))
            {
                sMemAddr = strtoull(sMemAddrBuf, NULL, 16);
            }
            ImGui::SameLine();
            ImGui::Text("base 0x%llx", sMemAddr);

            const int kRows = 4096;
            const U8* base = (const U8*)sMemAddr;
            const F32 rowH = ImGui::GetTextLineHeightWithSpacing();

            if (ImGui::BeginChild("hexdump", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                                  ImGuiWindowFlags_HorizontalScrollbar))
            {
                ImGuiListClipper clip;
                clip.Begin(kRows, rowH);

                while (clip.Step())
                {
                    for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r)
                    {
                        const U8* row = base + (unsigned long long)r * 16ULL;
                        char line[160];
                        int n = snprintf(line, sizeof(line), "%08x  ", r * 16);

                        for (int b = 0; b < 8; ++b)
                        {
                            n += snprintf(line + n, sizeof(line) - n, "%02x ", row[b]);
                        }

                        n += snprintf(line + n, sizeof(line) - n, " ");

                        for (int b = 8; b < 16; ++b)
                        {
                            n += snprintf(line + n, sizeof(line) - n, "%02x ", row[b]);
                        }

                        n += snprintf(line + n, sizeof(line) - n, " |");

                        for (int b = 0; b < 16; ++b)
                        {
                            U8 c = row[b];
                            line[n++] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
                        }

                        line[n++] = '|';
                        line[n] = '\0';

                        ImGui::TextUnformatted(line);
                    }
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    static bool sPrevDemo, sPrevWarp, sPrevGlobals, sPrevEntities, sPrevSpawn, sPrevPerf, sPrevMem,
        sPrevIni;
    static bool sPrevInited;

    if (!sPrevInited)
    {
        sPrevDemo = sShowDemo;
        sPrevWarp = sShowWarp;
        sPrevGlobals = sShowGlobals;
        sPrevEntities = sShowEntities;
        sPrevSpawn = sShowSpawn;
        sPrevPerf = sShowPerf;
        sPrevMem = sShowMem;
        sPrevIni = sShowIni;
        sPrevInited = true;
    }
    else if (sShowDemo != sPrevDemo || sShowWarp != sPrevWarp ||
             sShowGlobals != sPrevGlobals || sShowEntities != sPrevEntities ||
             sShowSpawn != sPrevSpawn || sShowPerf != sPrevPerf ||
             sShowMem != sPrevMem || sShowIni != sPrevIni)
    {
        sPrevDemo = sShowDemo;
        sPrevWarp = sShowWarp;
        sPrevGlobals = sShowGlobals;
        sPrevEntities = sShowEntities;
        sPrevSpawn = sShowSpawn;
        sPrevPerf = sShowPerf;
        sPrevMem = sShowMem;
        sPrevIni = sShowIni;
        iImguiSaveWindowState();
    }
}

void iImguiRender()
{
    if (!sImguiReady)
    {
        return;
    }

    ImGui::Render();
    ImGui_ImplRW_RenderDrawLists(ImGui::GetDrawData());
}

bool iImguiWantCapture()
{
    if (!sImguiReady)
    {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard || io.WantCaptureMouse;
}

void iImguiShutdown()
{
    if (!sImguiReady)
    {
        return;
    }

    ImGui_ImplRW_Shutdown();
    ImGui::DestroyContext();

    sImguiReady = false;
}
