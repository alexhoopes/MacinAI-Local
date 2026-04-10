/*------------------------------------------------------------------------------
    SystemActions.h - AI-Controlled System Actions

    Implements system-level actions triggered by the local AI model.
    Same functionality as MacinAI (relay) but without network dependency.

    For CodeWarrior Pro 5 on System 7.5.3+

    Written by Alex Hoopes
    Copyright (c) 2026 OldAppleStuff / Alex Hoopes

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <https://www.gnu.org/licenses/>.
------------------------------------------------------------------------------*/

#ifndef SYSTEMACTIONS_H
#define SYSTEMACTIONS_H

#include <Types.h>

/* Initialize system actions module */
OSErr SystemActions_Initialize(void);

/* Power management */
OSErr SystemActions_Shutdown(Boolean showDialog);
OSErr SystemActions_Restart(Boolean showDialog);

/* Application management */
OSErr SystemActions_LaunchApplication(const char *appName);

/* Control panel management */
OSErr SystemActions_OpenControlPanel(const char *cpName);

/* Path-based actions */
OSErr SystemActions_PathToFSSpec(const char *hfsPath, FSSpec *spec);
OSErr SystemActions_LaunchAppByPath(const char *hfsPath, char *errorMsg);

/* System queries */
OSErr SystemActions_QuerySystem(const char *queryType, char *result, short maxLen);

/* Show alert */
void SystemActions_ShowAlert(const char *message);

#endif /* SYSTEMACTIONS_H */
