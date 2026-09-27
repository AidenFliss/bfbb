#ifndef IQUITDESKTOP_H
#define IQUITDESKTOP_H

#include <types.h>

// The pause menu's Quit to Desktop entry and its confirmation. The widgets are
// the menu data's (mnu4). Yes switches the game to Save mode and stays
// selected, the way the Settings entry does, and iSGSaveLoop sees that and
// closes the window.
#define IQUIT_DESKTOP_ENTRY "PAUSE OPTION QUIT DESKTOP UIF"
#define IQUIT_DESKTOP_GROUP "PC QUIT DESKTOP GROUP"
#define IQUIT_DESKTOP_PROMPT "PC QUIT DESKTOP PROMPT UIF"
#define IQUIT_DESKTOP_YES "PC QUIT DESKTOP YES UIF"
#define IQUIT_DESKTOP_NO "PC QUIT DESKTOP NO UIF"

// Whether Yes was just chosen. Asked by the save loop.
S32 iQuitDesktopRequested();

#endif
