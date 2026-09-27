#ifndef ICHEATS_H
#define ICHEATS_H

#include <types.h>

// PC-only: the `[cheats]` section of config.ini. Each one is off by default.
//
// iCheatsUpdate runs once a frame from the game loop and writes the player's
// state, so an unlock or a full inventory holds through a save load, a new
// game and whatever the level takes away. The power-ups, shinies and spatulas
// are saved like any other progress, so turning the cheat off keeps them.
//
// all_warps sets zGlobalSettings::CheatAlwaysPortal, the developers' own
// switch. zUI's pause-menu taxi already honours it; retail's SB.INI loader
// clears it. It is not saved.
//
// unlimited_health clears zGlobalSettings::TakeDamage, another developers'
// switch: zEntPlayer_Damage still knocks the player back but leaves Health
// alone. Goo and falling to death zero Health directly and still kill.
void iCheatSetAllWarps(S32 on);
void iCheatSetBubbleBowl(S32 on);
void iCheatSetCruiseBubble(S32 on);
void iCheatSetUnlimitedShinies(S32 on);
void iCheatSetUnlimitedSpatulas(S32 on);
void iCheatSetUnlimitedHealth(S32 on);

void iCheatsUpdate();

#endif
