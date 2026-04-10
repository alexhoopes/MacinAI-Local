/*------------------------------------------------------------------------------
    ActionDispatcher.h - Local AI Command Dispatching

    Parses command tokens from the local model's output and dispatches
    them to SystemActions. In MacinAI Local, commands come from the model's
    constrained decoding (e.g., [CMD:LAUNCH_APP] photoshop) rather than
    from a server ACTION= protocol.

    Supported Commands:
        [CMD:NONE]         - No action needed
        [CMD:LAUNCH_APP]   - Launch application (arg = app name)
        [CMD:OPEN_CP]      - Open control panel (arg = CP name)
        [CMD:QUERY_SYS]    - Query system info (arg = query type)
        [CMD:REFRESH]      - Refresh system info
        [CMD:SHOW_ALERT]   - Show alert dialog (arg = message)
        [CMD:SHUTDOWN]     - Shutdown (with confirmation)
        [CMD:RESTART]      - Restart (with confirmation)
        [CMD:APPLESCRIPT]  - Execute AppleScript via OSA (token 8205)

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

#ifndef ACTIONDISPATCHER_H
#define ACTIONDISPATCHER_H

#include <Types.h>

/*------------------------------------------------------------------------------
    Result Codes
------------------------------------------------------------------------------*/
#define kActionResultSuccess        0
#define kActionResultInvalidFormat  -1
#define kActionResultUnknownCommand -2
#define kActionResultExecutionError -3
#define kActionResultUserCanceled   -4
#define kActionResultNoAction       -5

/* Initialize action dispatcher */
OSErr ActionDispatcher_Initialize(void);

/* Process a command token + argument from model output */
/* cmdToken: command token ID (kTokenCmdNone through kTokenCmdRestart) */
/* argument: text argument following the command token (may be nil) */
/* resultMsg: buffer for result message */
/* maxLen: size of resultMsg buffer */
short ActionDispatcher_ProcessToken(long cmdToken, const char *argument,
                                    char *resultMsg, short maxLen);

/* Legacy: Process ACTION= string (for compatibility if needed) */
short ActionDispatcher_ProcessCommand(const char *commandStr,
                                      char *resultMsg, short maxLen);

#endif /* ACTIONDISPATCHER_H */
