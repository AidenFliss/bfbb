#ifndef ZNPCSPAWN_H
#define ZNPCSPAWN_H

#include <types.h>

void zNPCSpawnTick();
bool zNPCSpawnWanted();
void zNPCSpawnSceneInit();
void zNPCSpawnScenePreReset();
void zNPCSpawnScenePostReset();
bool zNPCSpawnDeleteOnReset();
void zNPCSpawnSetDeleteOnReset(bool enable);
F32 zNPCSpawnArenaRadius();
void zNPCSpawnSetArenaRadius(F32 radius);
F32 zNPCSpawnInterval();
void zNPCSpawnSetInterval(F32 interval);
bool zNPCSpawnEnabled();
void zNPCSpawnSetEnabled(bool enable);
bool zNPCSpawnStateAllows();

S32 zNPCSpawnAvailCount();
const char* zNPCSpawnAvailName(S32 i);
S32 zNPCSpawnAvailType(S32 i);
S32 zNPCSpawnSelectedAvail();
void zNPCSpawnSelectAvail(S32 i);

#endif
