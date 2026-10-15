// The config-file cheats. What each one does is in iCheats.h.

#include "iCheats.h"

#include "xPad.h"
#include "zEntPlayer.h"
#include "zGameExtras.h"
#include "zGlobals.h"
#include "zGameState.h"

namespace
{
    // The number of golden spatulas in the game.
    const U32 kSpatulaMax = 100;

    S32 sAllWarps = FALSE;
    S32 sBubbleBowl = FALSE;
    S32 sCruiseBubble = FALSE;
    S32 sUnlimitedShinies = FALSE;
    S32 sUnlimitedSpatulas = FALSE;
    S32 sUnlimitedHealth = FALSE;
    S32 sForceFlyMode = FALSE;
    S32 sNoclip = FALSE;
    S32 sNoclipSaved = FALSE;
    U8 sSavedCollType = 0;
    U8 sSavedChkBy = 0;
    S32 sGameModeRequest = -1;
    S32 sPickupCollectDisabled = FALSE;
    S32 sGameFlags = 0;
    S32 sCruiseBoostQueued = FALSE;
}

void iCheatSetGameFlag(iCheatGameFlag flag, S32 on)
{
    if (on)
    {
        sGameFlags |= flag;
    }
    else
    {
        sGameFlags &= ~flag;
    }
}

void iCheatSetAllWarps(S32 on)
{
    sAllWarps = on;
}

void iCheatSetBubbleBowl(S32 on)
{
    sBubbleBowl = on;
}

void iCheatSetCruiseBubble(S32 on)
{
    sCruiseBubble = on;
}

void iCheatSetUnlimitedShinies(S32 on)
{
    sUnlimitedShinies = on;
}

void iCheatSetUnlimitedSpatulas(S32 on)
{
    sUnlimitedSpatulas = on;
}

void iCheatSetUnlimitedHealth(S32 on)
{
    sUnlimitedHealth = on;
}

void iCheatSetFlyMode(S32 on)
{
    globals.player.cheat_mode = on ? TRUE : FALSE;
}

void iCheatSetForceFlyMode(S32 on)
{
    sForceFlyMode = on;
}

S32 iCheatIsForceFlyMode()
{
    return sForceFlyMode;
}

void iCheatSetNoclip(S32 on)
{
    sNoclip = on;
}

S32 iCheatIsNoclip()
{
    return sNoclip;
}

void iCheatRequestGameMode(S32 mode)
{
    if (mode >= 0 && mode < (S32)eGameMode_Count)
    {
        sGameModeRequest = mode;
    }
}

void iCheatRequestCruiseBoost()
{
    sCruiseBoostQueued = TRUE;
}

void iCheatSetPickupCollectDisabled(S32 on)
{
    sPickupCollectDisabled = on;
}

S32 iCheatIsPickupCollectDisabled()
{
    return sPickupCollectDisabled;
}

void iCheatsUpdate()
{
    if (sCruiseBoostQueued)
    {
        if (globals.pad0 != NULL)
        {
            globals.pad0->pressed |= XPAD_MOVE(XPAD_BUTTON_O) | XPAD_BUTTON_L1;
        }
        sCruiseBoostQueued = FALSE;
    }
    if (sAllWarps)
    {
        globals.player.g.CheatAlwaysPortal = TRUE;
    }
    if (sBubbleBowl)
    {
        globals.player.g.PowerUp[0] = TRUE;
    }
    if (sCruiseBubble)
    {
        globals.player.g.PowerUp[1] = TRUE;
    }
    if (sUnlimitedShinies)
    {
        globals.player.Inv_Shiny = SHINY_MAX;
    }
    if (sUnlimitedSpatulas)
    {
        globals.player.Inv_Spatula = kSpatulaMax;
    }
    if (sUnlimitedHealth)
    {
        globals.player.g.TakeDamage = FALSE;
    }
    if (sForceFlyMode)
    {
        globals.player.cheat_mode = TRUE;
    }
    if (!globals.player.cheat_mode)
    {
        sNoclip = FALSE;
    }
    if (sNoclip && globals.player.cheat_mode)
    {
        if (!sNoclipSaved)
        {
            sSavedCollType = globals.player.ent.collType;
            sSavedChkBy = globals.player.ent.chkby;
            sNoclipSaved = TRUE;
        }
        globals.player.ent.collType = XENT_COLLTYPE_NONE;
        globals.player.ent.chkby = 0;
    }
    else if (sNoclipSaved)
    {
        globals.player.ent.collType = sSavedCollType;
        globals.player.ent.chkby = sSavedChkBy;
        sNoclipSaved = FALSE;
    }
    if (sGameModeRequest >= 0)
    {
        zGameModeSwitch((eGameMode)sGameModeRequest);
        sGameModeRequest = -1;
    }
    {
        const S32 mask = ICHEAT_EXPERT | ICHEAT_BIG_PLANK | ICHEAT_MEDICS | ICHEAT_SMALL_VILLAGERS |
                         ICHEAT_SMALL_COSTARS | ICHEAT_RICH_VILLAGERS | ICHEAT_PANHANDLE |
                         ICHEAT_DOG_TRICKS | ICHEAT_SHRAPBOB | ICHEAT_NO_PANTS |
                         ICHEAT_CRUISE_CONTROL;
        const S32 cur = zGameExtras_CheatFlags();
        const S32 diff = cur ^ ((cur & ~mask) | sGameFlags);
        if (diff != 0)
        {
            GEC_CheatFlagToggle(diff);
        }
    }
}
