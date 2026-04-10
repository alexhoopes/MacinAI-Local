/*----------------------------------------------------------------------
    DemoMode.h - Automated Test/Demo Sequence

    Types 10 curated questions character-by-character into the chat
    input and sends them through Message_Send, logging all results
    to the debug log for verification.

    Trigger: Cmd+T from chat screen (model must be loaded).
    Tests tool model v2: lookup, canned, actions, AppleScript,
    how-to procedures, and deflection to MacinAI Online.

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
----------------------------------------------------------------------*/

#ifndef DEMOMODE_H
#define DEMOMODE_H

#include <Types.h>

/* Start the demo sequence (requires model loaded + chat screen open) */
void DemoMode_Start(void);

/* Advance the demo state machine (call from idle handler) */
void DemoMode_Step(void);

/* Stop/cancel the demo */
void DemoMode_Stop(void);

/* Check if demo is currently running */
Boolean DemoMode_IsRunning(void);

#endif /* DEMOMODE_H */
