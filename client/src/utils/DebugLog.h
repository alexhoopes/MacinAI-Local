/*------------------------------------------------------------------------------
    DebugLog.h - Runtime-Toggleable Debug Logging

    Writes timestamped log entries to "MacinAI Local Debug.log" in the
    same folder as the application. Can be enabled/disabled at runtime
    via the Settings dialog without recompiling.

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

#ifndef DEBUGLOG_H
#define DEBUGLOG_H

#include <Types.h>

/* Initialize the logging system */
void DebugLog_Init(void);

/* Enable or disable logging at runtime */
void DebugLog_SetEnabled(Boolean enabled);

/* Check if logging is currently enabled */
Boolean DebugLog_IsEnabled(void);

/* Write a log entry (no-op if disabled) */
void DebugLog_Write(const char *message);

/* Write a formatted log entry with one long value */
void DebugLog_WriteNum(const char *message, long value);

/* Write a formatted log entry with two values */
void DebugLog_WriteNum2(const char *message, long val1, long val2);

/* Flush and close the log file */
void DebugLog_Close(void);

void DebugLog_Flush(void);

#endif /* DEBUGLOG_H */
