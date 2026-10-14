#ifndef ZDBGRENDER_H
#define ZDBGRENDER_H

#include <types.h>

#include "xVec3.h"
#include "xCollide.h"

struct zDbgPreds
{
    enum
    {
        MAXPREDS = 256
    };

    xVec3 pos[MAXPREDS];
    S32 cnt;
};

struct zDbgAtks
{
    enum
    {
        MAXATK = 32
    };

    xVec3 pos[MAXATK];
    F32 r[MAXATK];
    S32 cnt;
};

struct zDbgRenderState
{
    bool on;
    bool predict;
    bool robotkill;
    bool nav;
    bool playerarrow;
    bool boss;
    bool projectile;
    bool boulder;
    bool attack;
    bool platpath;
};

struct zDbgBossInfo
{
    S32 type;
    xVec3 pos;
    S32 goalID;
    S32 flg_vuln;
    F32 tmr_invuln;
};

struct zDbgHazInfo
{
    S32 type;
    xVec3 pos;
    xParabola parab;
    xVec3 tgt;
    F32 tcur;
    U8 hasParab;
    U8 hasTgt;
};

extern zDbgRenderState gDbgRender;
extern zDbgPreds gDbgPreds;
extern zDbgAtks gDbgAtks;

void zDbgRenderFrame();
void zDbgPredictRecord(const xVec3* pos);
void zDbgPredsClear();
void zDbgAttackRecord(const xVec3* pos, F32 r);
void zDbgAtksClear();

S32 zDbgCollectBosses(zDbgBossInfo* out, S32 max);
S32 zNPCHazard_CollectDebug(zDbgHazInfo* out, S32 max);
const char* zDbgTypeName(S32 type);
void zDbgGoalFourCC(S32 goalID, char out[5]);

void zDbgSphere(const xVec3* c, F32 r, U8 cr, U8 cg, U8 cb, U8 al);
void zDbgLine(const xVec3* a, const xVec3* b, F32 thick, U8 cr, U8 cg, U8 cb, U8 al);
void zDbgArrow(const xVec3* base, const xVec3* tip, F32 thick, U8 cr, U8 cg, U8 cb, U8 al);

#endif
