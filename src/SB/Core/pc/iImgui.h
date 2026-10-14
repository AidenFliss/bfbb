#ifndef IIMGUI_H
#define IIMGUI_H

// Dear ImGui behind the platform interface, port-only. There is no
// gc/iImgui.cpp to mirror and nothing outside src/SB/Core/pc includes this
// header -- same family as iSnapshot.h, and for the same reason: the DOL
// cannot see it by construction. See iImgui.cpp for the frame contract.

void iImguiInit();     // once, after RwEngineStart -- the font atlas needs a device
void iImguiNewFrame(); // exactly once per frame, before any ImGui:: call
void iImguiRender();   // exactly once per frame, after the game has drawn
void iImguiShutdown(); // once, before the engine is closed

// True while ImGui is holding mouse or keyboard. The game only consumes pad
// input (iPadHost), so nothing needs to ask yet -- this exists for the day
// something does.
bool iImguiWantCapture();

// Name of the last level warped to via the ImGui Level Warp menu, or NULL if
// none this run. The runtime-asset test (zNPCSpawn) uses it to reset its state
// when a new level loads. Port-only; same reasoning as the rest of this file.
const char* iImguiLastWarpName();

// True while the game is actually in a gameplay scene (eGameMode_Game and
// eGameState_Play). The runtime-asset test gates its spawn tick on this so it
// does not run on the title screen or during a scene switch.
bool iImguiInGame();

struct xIniFile;

// Snapshot the boot-time SB.INI key/value pairs so the SB.INI Editor can
// restore defaults. Called once from zMainReadINI before the file is destroyed.
void iImguiINICaptureDefaults(xIniFile* ini);

#endif
