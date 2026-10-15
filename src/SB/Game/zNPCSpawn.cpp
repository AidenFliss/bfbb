#include <types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "zNPCSpawn.h"

#include "xEnt.h"
#include "xMath3.h"
#include "xMemMgr.h"
#include "xScene.h"
#include "xString.h"
#include "xstransvc.h"
#include "xBehaviour.h"
#include "xGrid.h"
#include "xModel.h"
#include "xShadow.h"
#include "xSnd.h"
#include "xordarray.h"
#include "zGlobals.h"
#include "zGameState.h"
#include "zNPCMgr.h"
#include "zNPCTypeCommon.h"
#include "zNPCTypeRobot.h"
#include "zNPCSupport.h"
#include "zNPCHazard.h"
#include "zNPCMessenger.h"
#include "zMovePoint.h"
#include "zScene.h"

#include "iImgui.h"

struct zNPCSpawnPlan
{
    const char* modelName;
    S32 spawned;
};

static zNPCSpawnPlan s_plan = { "robot_0a_fodder_bind", 0 };
static F32 s_cooldown;
static F32 s_delay;
static F32 s_interval = 1.5f;
static F32 s_arenaRadius = 10.0f;
static xEnt* s_prev;
static S32 s_srcIndex = -1;
static U32 s_vilAssetSize;
static bool s_exhausted;
static U32 s_mvptSerial = 0xF00E0000;
static bool s_deleteOnReset = true;
static bool s_enabled = false;

struct zNPCSpawnAvail
{
    S32 vilIndex;
    U32 vilSize;
    S32 type;
    const char* name;
};

static zNPCSpawnAvail s_avail[64];
static S32 s_availCount;
static bool s_availBuilt;
static S32 s_selected = -1;

static xEnt** s_spawned;
static U32 s_spawnedCount;
static U32 s_spawnedAllocd;

static void zNPCSpawnTrack(xEnt* npc);
static const xVec3* zNPCSpawnPlayerPos();
static bool zNPCSpawnSceneReady();
static bool zNPCSpawnFindSource();
static zMovePoint* zNPCSpawnAnchor(xEnt* npc, const xVec3* pos);
static void zNPCSpawnUnlink(xEnt* npc);
static void zNPCSpawnLog(const char* fmt, ...);

static U32 s_stressTarget;
static U32 s_stressKillPct;
static U32 s_stressBatch = 20;
static bool s_stressInit;

static void zNPCSpawnStressSetup()
{
    if (s_stressInit)
    {
        return;
    }

    s_stressInit = true;

    const char* kenv = getenv("BFBB_STRESS_KILLPCT");

    if (kenv != NULL && kenv[0] != '\0')
    {
        long k = strtol(kenv, NULL, 10);

        if (k < 0)
        {
            k = 0;
        }

        if (k > 100)
        {
            k = 100;
        }

        s_stressKillPct = (U32)k;

        if (s_stressKillPct > 0)
        {
            zNPCSpawnLog("STRESS killpct armed pct=%u", s_stressKillPct);
        }
    }

    const char* env = getenv("BFBB_STRESS_SPAWN");

    if (env == NULL || env[0] == '\0')
    {
        return;
    }

    long v = strtol(env, NULL, 10);
    if (v <= 0)
    {
        v = 5000;
    }

    s_stressTarget = (U32)v;
    s_enabled = true;
    s_interval = 0.0f;
    s_delay = 0.0f;
    s_cooldown = 0.0f;
    s_deleteOnReset = true;

    zNPCSpawnLog("STRESS armed target=%u batch=%u", s_stressTarget, s_stressBatch);
}

static void zNPCSpawnStressKillOldest()
{
    if (s_spawnedCount == 0)
    {
        return;
    }

    xEnt* e = s_spawned[0];

    for (U32 i = 1; i < s_spawnedCount; i++)
    {
        s_spawned[i - 1] = s_spawned[i];
    }

    s_spawnedCount--;

    if (e != NULL)
    {
        zNPCSpawnUnlink(e);
    }
}

// Emulates a real hit so the mass-kill goes through the game's own death path
// (Damage -> goal transition -> afterlife) instead of the hand-rolled teardown.
// DMGTYP_INSTAKILL forces tmr_invuln to -1 first, so it always lands; who == NULL
// skips the player-immunity gate at the top of zNPCCommon::Damage.
static void zNPCSpawnStressHit(zNPCCommon* npc)
{
    if (npc == NULL)
    {
        return;
    }

    npc->Damage(DMGTYP_INSTAKILL, NULL, NULL);
}

static void zNPCSpawnStressKillPercent()
{
    if (s_stressKillPct == 0)
    {
        return;
    }

    st_XORDEREDARRAY* list = zNPCMgr_GetNPCList();

    if (list == NULL || list->cnt < 1)
    {
        return;
    }

    U32 hits = 0;

    for (S32 i = list->cnt - 1; i >= 0; i--)
    {
        if (xurand() * 100.0f >= (F32)s_stressKillPct)
        {
            continue;
        }

        zNPCSpawnStressHit((zNPCCommon*)list->list[i]);
        hits++;
    }

    if (hits > 0)
    {
        zNPCSpawnLog("STRESS killpct pct=%u hit=%u alive=%d", s_stressKillPct, hits, list->cnt);
    }
}

static void zNPCSpawnStressTick()
{
    zNPCSpawnStressSetup();

    if (s_stressTarget == 0)
    {
        return;
    }

    if (!zNPCSpawnSceneReady())
    {
        return;
    }

    if (!zNPCSpawnFindSource())
    {
        s_exhausted = true;
        zNPCSpawnLog("STRESS no cloneable VIL source");
        return;
    }

    const xVec3* p = zNPCSpawnPlayerPos();

    if (p == NULL)
    {
        return;
    }

    if (s_spawnedCount >= s_stressTarget)
    {
        U32 kill = s_spawnedCount - s_stressTarget + s_stressBatch;

        if (kill > s_stressBatch)
        {
            kill = s_stressBatch;
        }

        for (U32 k = 0; k < kill; k++)
        {
            zNPCSpawnStressKillOldest();
        }
    }

    U32 want = s_stressTarget - s_spawnedCount;

    if (want > s_stressBatch)
    {
        want = s_stressBatch;
    }

    for (U32 n = 0; n < want; n++)
    {
        xVec3 pos;
        pos.x = p->x + (F32)((S32)(n % 8) - 4) * 2.5f;
        pos.y = p->y + 5.0f;
        pos.z = p->z + (F32)((S32)((n / 8) % 8) - 4) * 2.5f;

        xEnt* npc = (xEnt*)zSceneSpawn('VIL ', s_srcIndex, &pos);

        if (npc == NULL)
        {
            zNPCSpawnLog("STRESS spawn failed at n=%u alive=%u", s_plan.spawned, s_spawnedCount);
            break;
        }

        zNPCSpawnTrack(npc);
        zNPCSpawnAnchor(npc, p);
        s_plan.spawned++;
    }

    if ((s_plan.spawned % 200) == 0 && s_plan.spawned > 0)
    {
        zNPCSpawnLog("STRESS progress spawned=%u alive=%u", s_plan.spawned, s_spawnedCount);
    }
}

bool zNPCSpawnDeleteOnReset()
{
    return s_deleteOnReset;
}

void zNPCSpawnSetDeleteOnReset(bool enable)
{
    s_deleteOnReset = enable;
}

F32 zNPCSpawnArenaRadius()
{
    return s_arenaRadius;
}

void zNPCSpawnSetArenaRadius(F32 radius)
{
    s_arenaRadius = radius;
}

F32 zNPCSpawnInterval()
{
    return s_interval;
}

void zNPCSpawnSetInterval(F32 interval)
{
    s_interval = interval;
}

bool zNPCSpawnEnabled()
{
    zNPCSpawnStressSetup();

    if (s_stressTarget > 0 || s_stressKillPct > 0)
    {
        return true;
    }

    return s_enabled;
}

void zNPCSpawnSetEnabled(bool enable)
{
    s_enabled = enable;
}

bool zNPCSpawnStateAllows()
{
    return gGameMode == eGameMode_Game || gGameMode == eGameMode_Pause;
}

static void zNPCSpawnTrack(xEnt* npc)
{
    if (s_spawnedCount == s_spawnedAllocd)
    {
        U32 nalloc = s_spawnedAllocd * 2;

        if (nalloc == 0)
        {
            nalloc = 64;
        }

        xEnt** grown = (xEnt**)realloc(s_spawned, nalloc * sizeof(xEnt*));

        if (grown == NULL)
        {
            s_exhausted = true;
            return;
        }

        s_spawned = grown;
        s_spawnedAllocd = nalloc;
    }

    s_spawned[s_spawnedCount++] = npc;
}

// model->Mat->pos is the authoritative player position (camera, collision and
// every zEntPlayer consumer read it). frame->mat.pos is the simulation copy and
// reads (0,0,0) until the first update, so it is the wrong source here.
static const xVec3* zNPCSpawnPlayerPos()
{
    xModelInstance* m = globals.player.ent.model;
    return m != NULL ? (const xVec3*)&m->Mat->pos : NULL;
}

// The player is placed at the scene spawn point only once the scene has run its
// setup placement (zScene.cpp seeds frame->mat and model->Mat there). Until then
// the model sits at the origin, which would make a player-relative spawn land on
// (0,0,0). Treat "any axis non-zero" as "placed somewhere real".
static bool zNPCSpawnSceneReady()
{
    const xVec3* p = zNPCSpawnPlayerPos();
    return globals.sceneCur != NULL && p != NULL &&
           (p->x != 0.0f || p->y != 0.0f || p->z != 0.0f);
}

// xprintf is a no-op in this tree, so the runtime-asset test logs through printf
// the way the rest of the PC layer does (iSystem.cpp) -- gated on BFBB_DEBUG so
// a headless run can confirm a spawn happened. Port-only; there is no gc caller
// to mirror.
static void zNPCSpawnLog(const char* fmt, ...)
{
    static bool checked;
    static bool on;

    if (!checked)
    {
        checked = true;
        on = getenv("BFBB_DEBUG") != NULL;
    }

    if (!on)
    {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static S32 zNPCSpawnGoalOf(xEnt* e)
{
    xPsyche* psy = ((zNPCCommon*)e)->psy_instinct;
    if (psy == NULL)
    {
        return -1;
    }
    xGoal* g = psy->GetCurGoal();
    return g != NULL ? g->GetID() : -1;
}

static void zNPCSpawnDump(const char* tag, xEnt* e)
{
    if (e == NULL)
    {
        zNPCSpawnLog("%s: <null>", tag);
        return;
    }

    xModelInstance* m = e->model;

    zNPCSpawnLog("%s id=0x%08x entflg=0x%x base=0x%x culled=%d upd=%p rnd=%p frame=%p lk=%p "
                 "model=%p data=%p bucket=%p b0=%p pipe=0x%x mflg=0x%x alpha=%.2f anim=%p "
                 "surf=%p pool=%p scale=(%.2f,%.2f,%.2f) pos=(%.2f,%.2f,%.2f) fpos=(%.2f,%.2f,%.2f) goal=%d "
                 "cfg=%p navdst=%p vtbl=%p",
                 tag, e->id, (U32)e->flags, (U32)e->baseFlags, (U32)e->isCulled,
                 (const void*)e->update, (const void*)e->render, (const void*)e->frame,
                 (const void*)e->lightKit, m, m ? (const void*)m->Data : NULL,
                 m ? (const void*)m->Bucket : NULL,
                 (m && m->Bucket) ? (const void*)m->Bucket[0]->Data : NULL,
                 m ? m->PipeFlags : 0, m ? (U32)m->Flags : 0, m ? m->Alpha : 0.0f,
                 m ? (const void*)m->Anim : NULL, m ? (const void*)m->Surf : NULL,
                 m ? (const void*)m->Pool : NULL, m ? m->Scale.x : 0.0f, m ? m->Scale.y : 0.0f,
                 m ? m->Scale.z : 0.0f, m ? m->Mat->pos.x : 0.0f, m ? m->Mat->pos.y : 0.0f,
                 m ? m->Mat->pos.z : 0.0f, e->frame ? e->frame->mat.pos.x : 0.0f,
                 e->frame ? e->frame->mat.pos.y : 0.0f, e->frame ? e->frame->mat.pos.z : 0.0f,
                 zNPCSpawnGoalOf(e), (const void*)((zNPCCommon*)e)->cfg_npc,
                 (const void*)((zNPCCommon*)e)->nav_dest, *(const void**)e);
}

static void zNPCSpawnArenaLog(const char* tag, xEnt* e)
{
    if (e == NULL)
    {
        zNPCSpawnLog("%s arena: <null>", tag);
        return;
    }

    zNPCRobot* rn = (zNPCRobot*)e;
    NPCArena* ar = &rn->arena;
    const xVec3* ppos = xEntGetPos(&globals.player.ent);

    zNPCSpawnLog("%s arena ready=%d flg=%d rad=%.2f pos=(%.2f,%.2f,%.2f) inNPC=%d inPly=%d "
                 "need=%d det=%d wonder=0x%x lamp=%d hp=%d "
                 "navcur=%p navdst=%p navar=%p refdst=%p refcur=%p",
                 tag, (S32)ar->IsReady(), (S32)ar->flg_arena, ar->rad_arena, ar->pos_arena.x,
                 ar->pos_arena.y, ar->pos_arena.z, (S32)ar->IncludesNPC(rn, 0.75f, NULL),
                 (S32)ar->IncludesPlayer(0.5f, NULL), (S32)ar->NeedToCycle(rn),
                 (S32)rn->npcset.allowDetect, (U32)rn->SomethingWonderful(),
                 (S32)NPCC_LampStatus(), globals.player.Health,
                 (const void*)rn->nav_curr, (const void*)rn->nav_dest,
                 (const void*)ar->nav_arena, (const void*)ar->nav_refer_dest,
                 (const void*)ar->nav_refer_curr);
    zNPCSpawnLog("%s arena player=(%.2f,%.2f,%.2f) npc=(%.2f,%.2f,%.2f)", tag, ppos ? ppos->x : 0.0f,
                 ppos ? ppos->y : 0.0f, ppos ? ppos->z : 0.0f, rn->model->Mat->pos.x,
                 rn->model->Mat->pos.y, rn->model->Mat->pos.z);
}

static xEnt* zNPCSpawnFindSceneNPC()
{
    zScene* s = globals.sceneCur;

    if (s == NULL)
    {
        return NULL;
    }

    for (U32 i = 0; i < s->num_base; i++)
    {
        if (s->base[i] && s->base[i]->baseType == eBaseTypeNPC && s->base[i]->id < 0xF00D0000)
        {
            return (xEnt*)s->base[i];
        }
    }

    return NULL;
}

static bool zNPCSpawnFindSource()
{
    zScene* s = globals.sceneCur;

    if (s_srcIndex >= 0)
    {
        return true;
    }

    if (s == NULL)
    {
        return false;
    }

    U32 nameHash = xStrHash(s_plan.modelName);
    S32 vilCount = xSTAssetCountByType('VIL ');

    for (S32 i = 0; i < vilCount; i++)
    {
        U32 sz = 0;
        xEntAsset* ent = (xEntAsset*)xSTFindAssetByType('VIL ', i, &sz);

        if (ent == NULL || ent->modelInfoID == 0)
        {
            continue;
        }

        U32 infoSz;
        void* info = xSTFindAsset(ent->modelInfoID, &infoSz);

        if (info == NULL || xSTGetAssetType(ent->modelInfoID) != 'MINF')
        {
            continue;
        }

        xEntNPCAsset* npc = (xEntNPCAsset*)(ent + 1);

        if (npc->npcModel == (S32)nameHash)
        {
            s_srcIndex = i;
            s_vilAssetSize = sz;
            return true;
        }
    }

    return false;
}

static const char* zNPCSpawnRobotName(S32 type)
{
    switch (type)
    {
    case NPC_TYPE_ROBOT:
        return "Robot";
    case NPC_TYPE_HAMMER:
        return "Hammer";
    case NPC_TYPE_HAMSPIN:
        return "Ham-Spin";
    case NPC_TYPE_TARTAR:
        return "Tar-Tar";
    case NPC_TYPE_GLOVE:
        return "Glove";
    case NPC_TYPE_MONSOON:
        return "Monsoon";
    case NPC_TYPE_SLEEPY:
        return "Sleepy";
    case NPC_TYPE_ARFDOG:
        return "Arf Dog";
    case NPC_TYPE_ARFARF:
        return "Arf-Arf";
    case NPC_TYPE_CHUCK:
        return "Chuck";
    case NPC_TYPE_TUBELET:
        return "Tubelet";
    case NPC_TYPE_TUBESLAVE:
        return "Tube Slave";
    case NPC_TYPE_SLICK:
        return "Slick";
    case NPC_TYPE_FODDER:
        return "Fodder";
    case NPC_TYPE_FODBOMB:
        return "Fod Bomb";
    case NPC_TYPE_FODBZZT:
        return "Fod Bzzt";
    case NPC_TYPE_CHOMPER:
        return "Chomper";
    case NPC_TYPE_CRITTER:
        return "Critter";
    default:
        return NULL;
    }
}

static bool zNPCSpawnIsRobot(S32 type)
{
    return zNPCSpawnRobotName(type) != NULL;
}

static void zNPCSpawnBuildAvail()
{
    s_availCount = 0;
    s_availBuilt = true;

    zNPCMgr* mgr = zNPCMgrSelf();
    S32 vilCount = xSTAssetCountByType('VIL ');

    for (S32 i = 0; i < vilCount && s_availCount < 64; i++)
    {
        U32 sz = 0;
        xEntAsset* ent = (xEntAsset*)xSTFindAssetByType('VIL ', i, &sz);

        if (ent == NULL || ent->modelInfoID == 0)
        {
            continue;
        }

        U32 infoSz;
        xModelAssetInfo* info = (xModelAssetInfo*)xSTFindAsset(ent->modelInfoID, &infoSz);

        if (info == NULL || xSTGetAssetType(ent->modelInfoID) != 'MINF')
        {
            continue;
        }

        xEntNPCAsset* npc = (xEntNPCAsset*)(ent + 1);
        S32 type = (S32)mgr->NPCTypeForModel(info->BrainID, (U32)npc->npcModel);

        if (!zNPCSpawnIsRobot(type))
        {
            continue;
        }

        bool dup = false;

        for (S32 j = 0; j < s_availCount; j++)
        {
            if (s_avail[j].type == type)
            {
                dup = true;
                break;
            }
        }

        if (dup)
        {
            continue;
        }

        s_avail[s_availCount].vilIndex = i;
        s_avail[s_availCount].vilSize = sz;
        s_avail[s_availCount].type = type;
        s_avail[s_availCount].name = zNPCSpawnRobotName(type);
        s_availCount++;
    }
}

S32 zNPCSpawnAvailCount()
{
    if (!s_availBuilt)
    {
        zNPCSpawnBuildAvail();
    }

    return s_availCount;
}

const char* zNPCSpawnAvailName(S32 i)
{
    if (i < 0 || i >= s_availCount)
    {
        return NULL;
    }

    return s_avail[i].name;
}

S32 zNPCSpawnAvailType(S32 i)
{
    if (i < 0 || i >= s_availCount)
    {
        return 0;
    }

    return s_avail[i].type;
}

S32 zNPCSpawnSelectedAvail()
{
    return s_selected;
}

void zNPCSpawnSelectAvail(S32 i)
{
    if (i < 0 || i >= s_availCount)
    {
        return;
    }

    s_selected = i;
    s_srcIndex = s_avail[i].vilIndex;
    s_vilAssetSize = s_avail[i].vilSize;
    s_exhausted = false;
    s_plan.spawned = 0;
}

bool zNPCSpawnWanted()
{
    if (s_exhausted || s_cooldown > 0.0f)
    {
        return false;
    }

    if (!zNPCSpawnFindSource())
    {
        return false;
    }

    return true;
}

static zMovePoint* zNPCSpawnMakeMovePoint(const xVec3* pos)
{
    xMovePointAsset* asset = (xMovePointAsset*)xMemAllocSize(sizeof(xMovePointAsset));
    zMovePoint* mvpt;

    if (asset == NULL)
    {
        return NULL;
    }

    memset(asset, 0, sizeof(xMovePointAsset));
    asset->id = s_mvptSerial++;
    asset->baseType = eBaseTypeMovePoint;
    asset->baseFlags = 0x1;
    asset->pos = *pos;
    asset->on = 1;
    asset->arenaRadius = s_arenaRadius;

    mvpt = (zMovePoint*)xMemAllocSize(sizeof(zMovePoint));

    if (mvpt == NULL)
    {
        return NULL;
    }

    memset(mvpt, 0, sizeof(zMovePoint));
    zMovePointInit(mvpt, asset);

    return mvpt;
}

static zMovePoint* zNPCSpawnAnchor(xEnt* npc, const xVec3* pos)
{
    zMovePoint* mvpt;

    if (npc == NULL || npc->baseType != eBaseTypeNPC)
    {
        return NULL;
    }

    mvpt = zNPCSpawnMakeMovePoint(pos);

    if (mvpt == NULL)
    {
        return NULL;
    }

    ((zNPCRobot*)npc)->MvptReset(mvpt);
    ((zNPCRobot*)npc)->arena.SetHome((zNPCCommon*)npc, mvpt);

    return mvpt;
}

static void zNPCSpawnUnlink(xEnt* npc)
{
    zScene* s = globals.sceneCur;
    st_XORDEREDARRAY* npclist = zNPCMgr_GetNPCList();
    U32 i;

    xGridRemove(&npc->gridb);

    xSndParentDied((UPtr)npc);

    zNPCHazard_ClearOwner((zNPCCommon*)npc);

    if (npclist != NULL)
    {
        for (S32 k = 0; k < npclist->cnt; k++)
        {
            zNPCCommon* other = (zNPCCommon*)npclist->list[k];

            if (other != NULL && other != (zNPCCommon*)npc)
            {
                other->NPCClearTargetEnt(npc);
            }
        }
    }

    if (s != NULL)
    {
        for (i = 0; i < s->num_base; i++)
        {
            if (s->base[i] == (xBase*)npc)
            {
                s->num_base--;

                for (; i < s->num_base; i++)
                {
                    s->base[i] = s->base[i + 1];
                }

                break;
            }
        }

        for (i = 0; i < s->num_update_base; i++)
        {
            if (s->update_base[i] == (xBase*)npc)
            {
                s->num_update_base--;

                for (; i < s->num_update_base; i++)
                {
                    s->update_base[i] = s->update_base[i + 1];
                }

                break;
            }
        }

        for (i = 0; i < s->num_npcs; i++)
        {
            if (s->npcs[i] == npc)
            {
                s->num_npcs--;

                for (; i < s->num_npcs; i++)
                {
                    s->npcs[i] = s->npcs[i + 1];
                }

                break;
            }
        }

        for (i = 0; i < s->num_act_ents; i++)
        {
            if (s->act_ents[i] == npc)
            {
                s->num_act_ents--;

                for (; i < s->num_act_ents; i++)
                {
                    s->act_ents[i] = s->act_ents[i + 1];
                }

                break;
            }
        }
    }

    if (npclist != NULL)
    {
        void* npckey = (void*)(zNPCCommon*)npc;

        for (S32 i = 0; i < npclist->cnt; i++)
        {
            if (npclist->list[i] == npckey)
            {
                XOrdRemove(npclist, npckey, i);
                break;
            }
        }
    }

    zSceneForgetObject(npc->id, npc);

    xShadowManager_Remove(npc);

    if (npc->model != NULL)
    {
        xModelInstanceFree(npc->model);
        npc->model = NULL;
    }

    ((zNPCCommon*)npc)->Destroy();

    if (zNPCMgrSelf() != NULL && zNPCMgrSelf()->npcFactory != NULL)
    {
        zNPCMgrSelf()->npcFactory->DestroyItem((xFactoryInst*)(zNPCCommon*)npc);
    }
}

void zNPCSpawnSceneInit()
{
    free(s_spawned);
    s_spawned = NULL;
    s_spawnedCount = 0;
    s_spawnedAllocd = 0;
    s_plan.spawned = 0;
    s_cooldown = 0.0f;
    s_delay = 0.0f;
    s_prev = NULL;
    s_srcIndex = -1;
    s_vilAssetSize = 0;
    s_exhausted = false;
    s_availCount = 0;
    s_availBuilt = false;
    s_selected = -1;
}

void zNPCSpawnScenePreReset()
{
    if (!s_deleteOnReset)
    {
        return;
    }

    for (U32 i = 0; i < s_spawnedCount; i++)
    {
        zNPCSpawnUnlink(s_spawned[i]);
    }

    zNPCSpawnSceneInit();
}

void zNPCSpawnScenePostReset()
{
    const xVec3* p;

    if (s_deleteOnReset)
    {
        return;
    }

    p = zNPCSpawnPlayerPos();

    if (p == NULL)
    {
        return;
    }

    for (U32 i = 0; i < s_spawnedCount; i++)
    {
        zNPCSpawnAnchor(s_spawned[i], p);
    }

    s_plan.spawned = 0;
    s_delay = 0.0f;
    s_prev = NULL;
}

void zNPCSpawnTick()
{
    zNPCSpawnStressSetup();

    zNPCSpawnStressKillPercent();

    if (s_stressTarget > 0)
    {
        zNPCSpawnStressTick();
        return;
    }

    if (s_cooldown > 0.0f)
    {
        s_cooldown -= gSceneUpdateTime;
    }

    if (!iImguiInGame() || !zNPCSpawnSceneReady())
    {
        const xVec3* p = zNPCSpawnPlayerPos();
        static F32 s_probe;
        s_probe += gSceneUpdateTime;
        if (s_probe >= 1.0f)
        {
            s_probe = 0.0f;
            zNPCSpawnLog("probe mode=%d state=%d model=%p pos=(%.2f, %.2f, %.2f)", (int)gGameMode,
                         (int)gGameState, (const void*)p, p ? p->x : 0.0f, p ? p->y : 0.0f,
                         p ? p->z : 0.0f);
        }
        s_delay = 0.0f;
        s_plan.spawned = 0;
        s_prev = NULL;
        s_srcIndex = -1;
        s_vilAssetSize = 0;
        s_exhausted = false;
        return;
    }

    {
        static bool dbgSet;
        if (!dbgSet)
        {
            extern RpAtomic* gDbgRenderAtomic;
            xEnt* scenenpc = zNPCSpawnFindSceneNPC();
            if (scenenpc != NULL && scenenpc->model != NULL)
            {
                gDbgRenderAtomic = scenenpc->model->Data;
                dbgSet = true;
            }
        }
    }

    if (!zNPCSpawnWanted())
    {
        static F32 s_track;
        s_track += gSceneUpdateTime;
        if (s_track >= 1.0f && s_prev != NULL && s_prev->model != NULL)
        {
            s_track = 0.0f;
            zNPCSpawnLog("track id=0x%08x pos=(%.2f, %.2f, %.2f) fpos=(%.2f, %.2f, %.2f) "
                         "goal=%d culled=%d",
                         s_prev->id, s_prev->model->Mat->pos.x, s_prev->model->Mat->pos.y,
                         s_prev->model->Mat->pos.z, s_prev->frame->mat.pos.x,
                         s_prev->frame->mat.pos.y, s_prev->frame->mat.pos.z,
                         zNPCSpawnGoalOf(s_prev), (U32)s_prev->isCulled);
            zNPCSpawnArenaLog("CLONE", s_prev);
            xEnt* sc = zNPCSpawnFindSceneNPC();
            if (sc != NULL && sc->model != NULL)
            {
                zNPCSpawnLog("SCENE id=0x%08x pos=(%.2f, %.2f, %.2f) goal=%d", sc->id,
                             sc->model->Mat->pos.x, sc->model->Mat->pos.y, sc->model->Mat->pos.z,
                             zNPCSpawnGoalOf(sc));
                zNPCSpawnArenaLog("SCENE", sc);
            }
        }
        return;
    }

    s_delay += gSceneUpdateTime;

    if (s_delay < 5.0f)
    {
        return;
    }

    S32 index;

    if (!zNPCSpawnFindSource())
    {
        zNPCSpawnLog("no cloneable %s VIL in this scene", s_plan.modelName);
        s_exhausted = true;
        return;
    }

    index = s_srcIndex;

    const xVec3* p = zNPCSpawnPlayerPos();
    xVec3 pos;
    pos.x = p->x;
    pos.y = p->y + 5.0f;
    pos.z = p->z;

    xEnt* npc = (xEnt*)zSceneSpawn('VIL ', index, &pos);

    if (npc == NULL)
    {
        zNPCSpawnLog("zSceneSpawn(%s) failed at n=%d", s_plan.modelName, s_plan.spawned);
        s_exhausted = true;
        return;
    }

    zNPCSpawnTrack(npc);

    zMovePoint* mvpt = zNPCSpawnAnchor(npc, p);

    s_plan.spawned++;
    s_cooldown = s_interval;

    if (s_prev != NULL && s_prev->model != NULL)
    {
        zNPCSpawnLog("prev id=0x%08x pos=(%.1f, %.1f, %.1f) fpos=(%.1f, %.1f, %.1f) goal=%d",
                     s_prev->id, s_prev->model->Mat->pos.x, s_prev->model->Mat->pos.y,
                     s_prev->model->Mat->pos.z, s_prev->frame->mat.pos.x,
                     s_prev->frame->mat.pos.y, s_prev->frame->mat.pos.z, zNPCSpawnGoalOf(s_prev));
    }

    s_prev = npc;

    if (s_plan.spawned == 1)
    {
        extern RpAtomic* gDbgRenderAtomic;
        gDbgRenderAtomic = npc->model != NULL ? npc->model->Data : NULL;
        zNPCSpawnDump("CLONE", npc);
        zNPCSpawnDump("SCENE", zNPCSpawnFindSceneNPC());
        zNPCSpawnLog("scene num_base=%u num_update_base=%u", globals.sceneCur->num_base,
                     globals.sceneCur->num_update_base);
    }

    const xVec3* np = npc->model != NULL ? (const xVec3*)&npc->model->Mat->pos : NULL;
    zNPCSpawnLog("%s id=0x%08x pos=(%.1f, %.1f, %.1f) n=%d mvpt=%p nav=(%p,%p) "
                 "mvpos=(%.1f,%.1f,%.1f) mvrad=%.1f",
                 s_plan.modelName, npc->id, np ? np->x : 0.0f, np ? np->y : 0.0f,
                 np ? np->z : 0.0f, s_plan.spawned,
                 (const void*)mvpt,
                 (const void*)((zNPCRobot*)npc)->nav_curr,
                 (const void*)((zNPCRobot*)npc)->nav_dest,
                 mvpt ? mvpt->asset->pos.x : 0.0f, mvpt ? mvpt->asset->pos.y : 0.0f,
                 mvpt ? mvpt->asset->pos.z : 0.0f, mvpt ? mvpt->asset->arenaRadius : 0.0f);
}
