#include "zDbgRender.h"

#include <math.h>
#include <string.h>

#include <types.h>

#include "xEnt.h"
#include "xMath3.h"
#include "xMovePoint.h"
#include "xBehaviour.h"
#include "xordarray.h"
#include "xVec3.h"
#include "xCollide.h"
#include "xEntBoulder.h"

#include "zEntPlayer.h"
#include "zGlobals.h"
#include "zScene.h"
#include "zBase.h"
#include "zPlatform.h"
#include "zMovePoint.h"
#include "zNPCMgr.h"
#include "zNPCTypeCommon.h"
#include "zNPCTypeRobot.h"
#include "zNPCTypes.h"

zDbgRenderState gDbgRender = { false, true, true, true, true, true, false, false, false, false };
zDbgPreds gDbgPreds;
zDbgAtks gDbgAtks;

void zDbgPredsClear()
{
    gDbgPreds.cnt = 0;
}

void zDbgPredictRecord(const xVec3* pos)
{
    if (!gDbgRender.on || !gDbgRender.predict || pos == NULL)
    {
        return;
    }

    if (gDbgPreds.cnt >= zDbgPreds::MAXPREDS)
    {
        return;
    }

    gDbgPreds.pos[gDbgPreds.cnt] = *pos;
    gDbgPreds.cnt++;
}

void zDbgAtksClear()
{
    gDbgAtks.cnt = 0;
}

void zDbgAttackRecord(const xVec3* pos, F32 r)
{
    if (!gDbgRender.on || !gDbgRender.attack || pos == NULL || r <= 0.0f)
    {
        return;
    }

    if (gDbgAtks.cnt >= zDbgAtks::MAXATK)
    {
        return;
    }

    gDbgAtks.pos[gDbgAtks.cnt] = *pos;
    gDbgAtks.r[gDbgAtks.cnt] = r;
    gDbgAtks.cnt++;
}

static void dbgSetStates(U8 alpha)
{
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);

    if (alpha < 255)
    {
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    }
}

void zDbgSphere(const xVec3* c, F32 r, U8 cr, U8 cg, U8 cb, U8 al)
{
    enum
    {
        RINGS = 6,
        SEGS = 10
    };

    static RwIm3DVertex vert[(RINGS + 1) * (SEGS + 1)];
    static RwImVertexIndex idx[RINGS * SEGS * 6];

    S32 vi = 0;
    S32 ii = 0;

    for (S32 ring = 0; ring <= RINGS; ++ring)
    {
        F32 phi = 3.14159265f * (F32)ring / (F32)RINGS;
        F32 y = cosf(phi);
        F32 rad = sinf(phi);

        for (S32 seg = 0; seg <= SEGS; ++seg)
        {
            F32 theta = 6.2831853f * (F32)seg / (F32)SEGS;
            F32 x = rad * cosf(theta);
            F32 z = rad * sinf(theta);

            RwIm3DVertexSetPos(&vert[vi], c->x + x * r, c->y + y * r, c->z + z * r);
            RwIm3DVertexSetNormal(&vert[vi], x, y, z);
            RwIm3DVertexSetRGBA(&vert[vi], cr, cg, cb, al);
            vi++;
        }
    }

    for (S32 ring = 0; ring < RINGS; ++ring)
    {
        for (S32 seg = 0; seg < SEGS; ++seg)
        {
            RwImVertexIndex i0 = (RwImVertexIndex)(ring * (SEGS + 1) + seg);
            RwImVertexIndex i1 = (RwImVertexIndex)(i0 + 1);
            RwImVertexIndex i2 = (RwImVertexIndex)(i0 + SEGS + 1);
            RwImVertexIndex i3 = (RwImVertexIndex)(i2 + 1);

            idx[ii++] = i0;
            idx[ii++] = i2;
            idx[ii++] = i1;
            idx[ii++] = i1;
            idx[ii++] = i2;
            idx[ii++] = i3;
        }
    }

    dbgSetStates(al);

    if (RwIm3DTransform(vert, (RwUInt32)vi, NULL,
                        rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
    {
        RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, idx, ii);
        RwIm3DEnd();
    }
}

void zDbgLine(const xVec3* a, const xVec3* b, F32 thick, U8 cr, U8 cg, U8 cb, U8 al)
{
    xVec3 dir = *b - *a;
    F32 len = dir.length();

    if (len < 0.0001f)
    {
        return;
    }

    dir /= len;

    xVec3 up = { 0.0f, 1.0f, 0.0f };

    if (fabsf(dir.dot(up)) > 0.99f)
    {
        up = xVec3::create(1.0f, 0.0f, 0.0f);
    }

    xVec3 side = dir.cross(up).normal();
    xVec3 side2 = dir.cross(side).normal();

    side *= thick;
    side2 *= thick;

    static RwIm3DVertex vert[8];
    static RwImVertexIndex idx[36] = { 0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6,
                                             0, 4, 5, 0, 5, 1, 1, 5, 6, 1, 6, 2,
                                             2, 6, 7, 2, 7, 3, 3, 7, 4, 3, 4, 0 };

    xVec3 c[8];
    c[0] = *a + side + side2;
    c[1] = *a - side + side2;
    c[2] = *a - side - side2;
    c[3] = *a + side - side2;
    c[4] = *b + side + side2;
    c[5] = *b - side + side2;
    c[6] = *b - side - side2;
    c[7] = *b + side - side2;

    for (S32 i = 0; i < 8; ++i)
    {
        RwIm3DVertexSetPos(&vert[i], c[i].x, c[i].y, c[i].z);
        RwIm3DVertexSetRGBA(&vert[i], cr, cg, cb, al);
    }

    dbgSetStates(al);

    if (RwIm3DTransform(vert, 8, NULL, rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
    {
        RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, idx, 36);
        RwIm3DEnd();
    }
}

void zDbgArrow(const xVec3* base, const xVec3* tip, F32 thick, U8 cr, U8 cg, U8 cb, U8 al)
{
    xVec3 dir = *tip - *base;
    F32 len = dir.length();

    if (len < 0.0001f)
    {
        return;
    }

    dir /= len;

    thick *= 0.5f;

    F32 head = len * 0.3f;

    if (head > thick * 4.0f)
    {
        head = thick * 4.0f;
    }

    xVec3 shaftEnd = *tip - dir * head;

    zDbgLine(base, &shaftEnd, thick, cr, cg, cb, al);

    xVec3 up = { 0.0f, 1.0f, 0.0f };

    if (fabsf(dir.dot(up)) > 0.99f)
    {
        up = xVec3::create(1.0f, 0.0f, 0.0f);
    }

    xVec3 side = dir.cross(up).normal() * (head * 0.5f);
    xVec3 side2 = dir.cross(side).normal() * (head * 0.5f);

    static RwIm3DVertex vert[6];

    xVec3 b0 = shaftEnd + side;
    xVec3 b1 = shaftEnd - side;
    xVec3 b2 = shaftEnd - side2;
    xVec3 b3 = shaftEnd + side2;

    RwIm3DVertexSetPos(&vert[0], b0.x, b0.y, b0.z);
    RwIm3DVertexSetRGBA(&vert[0], cr, cg, cb, al);
    RwIm3DVertexSetPos(&vert[1], b1.x, b1.y, b1.z);
    RwIm3DVertexSetRGBA(&vert[1], cr, cg, cb, al);
    RwIm3DVertexSetPos(&vert[2], b2.x, b2.y, b2.z);
    RwIm3DVertexSetRGBA(&vert[2], cr, cg, cb, al);
    RwIm3DVertexSetPos(&vert[3], b3.x, b3.y, b3.z);
    RwIm3DVertexSetRGBA(&vert[3], cr, cg, cb, al);
    RwIm3DVertexSetPos(&vert[4], tip->x, tip->y, tip->z);
    RwIm3DVertexSetRGBA(&vert[4], cr, cg, cb, al);

    dbgSetStates(al);

    if (RwIm3DTransform(vert, 5, NULL, rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
    {
        static RwImVertexIndex idx[18] = { 0, 1, 4, 1, 2, 4, 2, 3, 4,
                                                 3, 0, 4, 0, 3, 1, 1, 3, 2 };
        RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, idx, 18);
        RwIm3DEnd();
    }
}

static bool dbgIsHostile(S32 type)
{
    U32 fam = (U32)type & 0xFFFFFF00U;

    switch (fam)
    {
    case ('NTR0' & 0xFFFFFF00U):
    case ('NTB0' & 0xFFFFFF00U):
    case ('NTH0' & 0xFFFFFF00U):
    case ('NBG0' & 0xFFFFFF00U):
        return true;
    default:
        break;
    }

    return type == NPC_TYPE_ROBOT || type == NPC_TYPE_BOSS || type == NPC_TYPE_BADGUY;
}

static bool dbgIsRobot(S32 type)
{
    return type == NPC_TYPE_ROBOT || ((U32)type & 0xFFFFFF00U) == ('NTR0' & 0xFFFFFF00U);
}

static bool dbgIsBoss(S32 type)
{
    U32 fam = (U32)type & 0xFFFFFF00U;

    if (fam == ('NTB0' & 0xFFFFFF00U) || fam == ('NTH0' & 0xFFFFFF00U))
    {
        return true;
    }

    return type == NPC_TYPE_BOSS;
}

const char* zDbgTypeName(S32 type)
{
    switch (type)
    {
    case NPC_TYPE_ROBOT:
        return "robot";
    case NPC_TYPE_BOSS:
        return "boss";
    case NPC_TYPE_BADGUY:
        return "badguy";
    case NPC_TYPE_BOSSSANDY:
        return "boss.sandy";
    case NPC_TYPE_BOSSPAT:
        return "boss.patrick";
    case NPC_TYPE_BOSS_SB1:
        return "boss.sb1";
    case NPC_TYPE_BOSSBOBBY:
        return "boss.bobby";
    case NPC_TYPE_BOSSPLANKTON:
        return "boss.plankton";
    case NPC_TYPE_KINGJELLY:
        return "kingjelly";
    case NPC_TYPE_DUTCHMAN:
        return "dutchman";
    case NPC_TYPE_PRAWN:
        return "prawn";
    case NPC_TYPE_FODDER:
        return "fodder";
    case NPC_TYPE_FODBOMB:
        return "fodbomb";
    case NPC_TYPE_FODBZZT:
        return "fodbzzt";
    case NPC_TYPE_CHOMPER:
        return "chomper";
    case NPC_TYPE_CRITTER:
        return "critter";
    default:
        return "npc";
    }
}

void zDbgGoalFourCC(S32 goalID, char out[5])
{
    out[0] = (char)((goalID >> 24) & 0xFF);
    out[1] = (char)((goalID >> 16) & 0xFF);
    out[2] = (char)((goalID >> 8) & 0xFF);
    out[3] = (char)(goalID & 0xFF);
    out[4] = '\0';

    for (S32 i = 0; i < 4; ++i)
    {
        if (out[i] < 32 || out[i] > 126)
        {
            out[i] = '.';
        }
    }
}

static void dbgDrawCircle(const xVec3* center, F32 radius, U8 cr, U8 cg, U8 cb, U8 al)
{
    const S32 SEGS = 24;
    xVec3 prev;
    prev.x = center->x + radius;
    prev.y = center->y;
    prev.z = center->z;

    for (S32 i = 1; i <= SEGS; ++i)
    {
        F32 ang = 6.2831853f * (F32)i / (F32)SEGS;
        xVec3 cur;
        cur.x = center->x + cosf(ang) * radius;
        cur.y = center->y;
        cur.z = center->z + sinf(ang) * radius;
        zDbgLine(&prev, &cur, 0.04f, cr, cg, cb, al);
        prev = cur;
    }
}

static void dbgDrawChase(zNPCCommon* npc)
{
    const xVec3& pos = *(const xVec3*)&npc->model->Mat->pos;

    if (npc->frame != NULL)
    {
        const xVec3& vel = npc->frame->vel;

        if (vel.length2() > 0.01f)
        {
            const S32 STEPS = 12;
            F32 dt = 1.0f / 12.0f;
            xVec3 prev = pos;

            for (S32 i = 1; i <= STEPS; ++i)
            {
                F32 t = (F32)i * dt;
                xVec3 p;
                p.x = pos.x + vel.x * t;
                p.y = pos.y + vel.y * t;
                p.z = pos.z + vel.z * t;
                zDbgLine(&prev, &p, 0.03f, 0, 255, 0, 170);
                prev = p;
            }
        }
    }

    if (dbgIsRobot(npc->SelfType()))
    {
        zNPCRobot* rob = (zNPCRobot*)npc;
        const xVec3* apos = rob->arena.Pos();

        if (apos != NULL)
        {
            dbgDrawCircle(apos, rob->arena.rad_arena, 0, 200, 200, 110);
        }
    }
}

static void dbgDrawNPCs()
{
    st_XORDEREDARRAY* list = zNPCMgr_GetNPCList();

    if (list == NULL)
    {
        return;
    }

    for (S32 i = 0; i < list->cnt; ++i)
    {
        zNPCCommon* npc = (zNPCCommon*)list->list[i];

        if (npc == NULL || npc->model == NULL)
        {
            continue;
        }

        const xVec3& pos = *(const xVec3*)&npc->model->Mat->pos;
        S32 type = npc->SelfType();

        if (gDbgRender.robotkill && dbgIsRobot(type))
        {
            const xSphere& sph = npc->bound.sph;

            if (npc->bound.type == XBOUND_TYPE_SPHERE)
            {
                zDbgSphere(&sph.center, sph.r, 0, 255, 255, 128);
            }
            else
            {
                zDbgSphere(&pos, 1.0f, 0, 255, 255, 128);
            }
        }

        if (gDbgRender.nav && dbgIsHostile(type))
        {
            dbgDrawChase(npc);
        }

        if (gDbgRender.boss && dbgIsBoss(type))
        {
            zDbgSphere(&pos, 1.6f, 255, 160, 0, 90);
        }
    }
}

static void dbgDrawParabola(const xParabola* p, F32 t0, F32 t1, U8 cr, U8 cg, U8 cb, U8 al)
{
    const S32 SEGS = 16;
    xVec3 prev;
    xParabolaEvalPos(p, &prev, t0);

    for (S32 i = 1; i <= SEGS; ++i)
    {
        F32 t = t0 + (t1 - t0) * (F32)i / (F32)SEGS;
        xVec3 cur;
        xParabolaEvalPos(p, &cur, t);
        zDbgLine(&prev, &cur, 0.05f, cr, cg, cb, al);
        prev = cur;
    }
}

static void dbgDrawProjectiles()
{
    zDbgHazInfo infos[64];
    S32 cnt = zNPCHazard_CollectDebug(infos, 64);

    for (S32 i = 0; i < cnt; ++i)
    {
        zDbgHazInfo* info = &infos[i];

        if (info->hasParab)
        {
            F32 t0 = info->tcur;
            F32 t1 = info->parab.maxTime;

            if (t0 < 0.0f)
            {
                t0 = 0.0f;
            }

            if (t1 > t0)
            {
                dbgDrawParabola(&info->parab, t0, t1, 255, 180, 0, 200);
            }
        }

        zDbgSphere(&info->pos, 0.2f, 255, 255, 255, 255);

        if (info->hasTgt)
        {
            zDbgSphere(&info->tgt, 0.35f, 255, 0, 255, 160);
            zDbgLine(&info->pos, &info->tgt, 0.03f, 255, 0, 255, 120);
        }
    }
}

static void dbgDrawBoulders()
{
    zScene* s = globals.sceneCur;

    if (s == NULL || s->baseList[eBaseTypeBoulder] == NULL)
    {
        return;
    }

    xEntBoulder* it = (xEntBoulder*)s->baseList[eBaseTypeBoulder];
    xEntBoulder* end = it + s->baseCount[eBaseTypeBoulder];

    while (it != end)
    {
        if (it->model != NULL && it->basset != NULL)
        {
            xVec3 pos = *(xVec3*)&it->model->Mat->pos;
            xVec3 vel = it->vel;
            F32 speed = vel.length();

            if (speed > 1.0f)
            {
                xVec3 tip = pos + vel * 0.15f;
                zDbgArrow(&pos, &tip, 0.05f, 255, 255, 0, 200);

                if (vel.y > 0.5f)
                {
                    F32 g = it->basset->gravity;
                    const S32 STEPS = 40;
                    F32 dt = 1.0f / 30.0f;
                    xVec3 prev = pos;

                    for (S32 i = 1; i <= STEPS; ++i)
                    {
                        F32 t = (F32)i * dt;
                        xVec3 p;
                        p.x = pos.x + vel.x * t;
                        p.z = pos.z + vel.z * t;
                        p.y = pos.y + vel.y * t - 0.5f * g * t * t;

                        if (p.y <= pos.y && i > 1)
                        {
                            zDbgLine(&prev, &p, 0.05f, 255, 100, 0, 200);
                            zDbgSphere(&p, 0.3f, 255, 60, 0, 180);
                            break;
                        }

                        zDbgLine(&prev, &p, 0.05f, 255, 100, 0, 200);
                        prev = p;
                    }
                }
            }
        }

        ++it;
    }
}

static void dbgDrawAttack()
{
    for (S32 i = 0; i < gDbgAtks.cnt; ++i)
    {
        zDbgSphere(&gDbgAtks.pos[i], gDbgAtks.r[i], 0, 255, 255, 110);
    }
}

static void dbgDrawSpongeBall()
{
    if (!gSpongeBall)
    {
        return;
    }

    xEntBoulder* bv = zEntPlayer_GetBoulderVehicle();

    if (bv == NULL || bv->model == NULL)
    {
        return;
    }

    const xVec3& pos = *(const xVec3*)&bv->bound.sph.center;
    zDbgSphere(&pos, bv->bound.sph.r, 0, 180, 255, 70);

    xVec3 tip;
    tip = pos + bv->vel * 0.15f;
    zDbgArrow(&pos, &tip, 0.06f, 255, 255, 255, 230);

    if (bv->angVel > 0.001f)
    {
        xVec3 axis = bv->rotVec;
        F32 alen = axis.length();

        if (alen > 0.001f)
        {
            axis /= alen;
            tip = pos + axis * (bv->bound.sph.r + 0.75f);
            zDbgArrow(&pos, &tip, 0.05f, 255, 120, 0, 220);
        }
    }

    if (bv->force.length2() > 0.0001f)
    {
        tip = pos + bv->force * 0.05f;
        zDbgArrow(&pos, &tip, 0.05f, 120, 255, 120, 220);
    }

    if (bv->lastRolling > 0.0f)
    {
        zDbgSphere(&pos, 0.35f, 255, 255, 0, 200);
    }
}

static void dbgDrawPlatPaths()
{
    zScene* s = globals.sceneCur;

    if (s == NULL || s->baseList[eBaseTypePlatform] == NULL)
    {
        return;
    }

    zPlatform* it = (zPlatform*)s->baseList[eBaseTypePlatform];
    zPlatform* end = it + s->baseCount[eBaseTypePlatform];

    while (it != end)
    {
        xEntMotion* mot = &it->motion;

        switch (mot->type)
        {
        case XENTMOTION_TYPE_ER:
            zDbgLine(&mot->er.a, &mot->er.b, 0.05f, 0, 200, 255, 160);
            break;
        case XENTMOTION_TYPE_MECH:
            zDbgLine(&mot->mech.apos, &mot->mech.bpos, 0.05f, 0, 200, 255, 160);
            break;
        case XENTMOTION_TYPE_ORB:
        {
            const S32 SEGS = 24;
            xVec3 c = mot->orb.orig;
            F32 ra = mot->orb.a;
            F32 rb = mot->orb.b;
            xVec3 prev;
            prev.x = c.x + ra;
            prev.y = c.y;
            prev.z = c.z;

            for (S32 i = 1; i <= SEGS; ++i)
            {
                F32 ang = 6.2831853f * (F32)i / (F32)SEGS;
                xVec3 cur;
                cur.x = c.x + cosf(ang) * ra;
                cur.y = c.y;
                cur.z = c.z + sinf(ang) * rb;
                zDbgLine(&prev, &cur, 0.05f, 0, 200, 255, 160);
                prev = cur;
            }
            break;
        }
        case XENTMOTION_TYPE_MP:
            if (mot->mp.src != NULL && mot->mp.dest != NULL && mot->mp.src->pos != NULL &&
                mot->mp.dest->pos != NULL)
            {
                zDbgLine(mot->mp.src->pos, mot->mp.dest->pos, 0.05f, 0, 200, 255, 160);
            }
            break;
        default:
            break;
        }

        ++it;
    }
}

S32 zDbgCollectBosses(zDbgBossInfo* out, S32 max)
{
    st_XORDEREDARRAY* list = zNPCMgr_GetNPCList();

    if (out == NULL || max <= 0 || list == NULL)
    {
        return 0;
    }

    S32 cnt = 0;

    for (S32 i = 0; i < list->cnt && cnt < max; ++i)
    {
        zNPCCommon* npc = (zNPCCommon*)list->list[i];

        if (npc == NULL || npc->model == NULL || !dbgIsBoss(npc->SelfType()))
        {
            continue;
        }

        zDbgBossInfo* info = &out[cnt];
        info->type = npc->SelfType();
        info->pos = *(const xVec3*)&npc->model->Mat->pos;
        info->goalID = 0;
        info->flg_vuln = npc->flg_vuln;
        info->tmr_invuln = npc->tmr_invuln;

        if (npc->psy_instinct != NULL)
        {
            xGoal* goal = npc->psy_instinct->GetCurGoal();

            if (goal != NULL)
            {
                info->goalID = goal->GetID();
            }
        }

        cnt++;
    }

    return cnt;
}

void zDbgRenderFrame()
{
    if (!gDbgRender.on)
    {
        zDbgPredsClear();
        zDbgAtksClear();
        return;
    }

    RxRenderStateVector rs;
    RxRenderStateVectorLoadDriverState(&rs);

    RwCullMode cull;
    RwRenderStateGet(rwRENDERSTATECULLMODE, &cull);

    if (gDbgRender.predict)
    {
        for (S32 i = 0; i < gDbgPreds.cnt; ++i)
        {
            zDbgSphere(&gDbgPreds.pos[i], 0.18f, 0, 255, 0, 255);
        }
    }

    dbgDrawNPCs();

    if (gDbgRender.projectile)
    {
        dbgDrawProjectiles();
    }

    if (gDbgRender.boulder)
    {
        dbgDrawBoulders();
        dbgDrawSpongeBall();
    }

    if (gDbgRender.attack)
    {
        dbgDrawAttack();
    }

    if (gDbgRender.platpath)
    {
        dbgDrawPlatPaths();
    }

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, rs.TextureRaster);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)(UPtr)((rs.Flags >> 1) & 1));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)(UPtr)((rs.Flags >> 2) & 1));
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)(UPtr)((rs.Flags >> 3) & 1));
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rs.SrcBlend);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rs.DestBlend);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)cull);

    zDbgPredsClear();
    zDbgAtksClear();
}
