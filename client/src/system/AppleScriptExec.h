/*------------------------------------------------------------------------------
    AppleScriptExec.h - AppleScript Execution Module

    Provides AppleScript execution for MacinAI+ users via the Open Scripting
    Architecture (OSA). Scripts are shown to the user for confirmation before
    execution.

    Requires System 7.5.3+ with AppleScript extension installed.

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

#ifndef APPLESCRIPTEXEC_H
#define APPLESCRIPTEXEC_H

#include <Types.h>

/* Initialize AppleScript execution (check for OSA availability) */
OSErr AppleScript_Initialize(void);

/* Check if AppleScript is available on this system */
Boolean AppleScript_IsAvailable(void);

/* Execute AppleScript with user confirmation dialog.
   script: C string containing AppleScript source
   resultMsg: Buffer for result/error message (min 512 bytes)
   maxLen: Size of resultMsg buffer
   Returns: noErr on success, userCanceledErr if user declines, other OSErr on failure */
OSErr AppleScript_ExecuteWithConfirm(const char *script, char *resultMsg, short maxLen);

/* Cleanup AppleScript resources */
void AppleScript_Cleanup(void);

#endif /* APPLESCRIPTEXEC_H */
