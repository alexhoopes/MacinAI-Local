/*------------------------------------------------------------------------------
    DebugLog.c - Runtime-Toggleable Debug Logging

    Writes to "MacinAI Local Debug.log" next to the application.
    Each entry is timestamped with TickCount for performance analysis.

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

#include "DebugLog.h"
#include "SafeString.h"
#include <Files.h>
#include <Events.h>
#include <string.h>
#include <stdio.h>

#pragma segment Engine

/*------------------------------------------------------------------------------
    Module Globals
------------------------------------------------------------------------------*/
static Boolean gLogEnabled = false;
static Boolean gLogInitialized = false;
static short gLogRefNum = 0;
static Boolean gLogFileOpen = false;

/*------------------------------------------------------------------------------
    OpenLogFile - Open or create the log file
------------------------------------------------------------------------------*/
static OSErr OpenLogFile(void)
{
    FSSpec logSpec;
    OSErr err;

    if (gLogFileOpen)
        return noErr;

    err = FSMakeFSSpec(0, 0, "\pMacinAI Local Debug.log", &logSpec);
    if (err == fnfErr)
    {
        err = FSpCreate(&logSpec, 'ttxt', 'TEXT', smSystemScript);
        if (err != noErr)
            return err;
    }
    else if (err != noErr)
    {
        return err;
    }

    err = FSpOpenDF(&logSpec, fsWrPerm, &gLogRefNum);
    if (err != noErr)
        return err;

    SetFPos(gLogRefNum, fsFromLEOF, 0);
    gLogFileOpen = true;

    return noErr;
}

/*------------------------------------------------------------------------------
    DebugLog_Init
------------------------------------------------------------------------------*/
void DebugLog_Init(void)
{
    gLogInitialized = true;
}

/*------------------------------------------------------------------------------
    DebugLog_SetEnabled
------------------------------------------------------------------------------*/
void DebugLog_SetEnabled(Boolean enabled)
{
    gLogEnabled = enabled;

    if (enabled)
    {
        OSErr err;
        err = OpenLogFile();
        if (err == noErr)
        {
            char header[128];
            long count;

            sprintf(header, "\r--- MacinAI Local Log Session (ticks=%ld) ---\r",
                    TickCount());
            count = strlen(header);
            FSWrite(gLogRefNum, &count, header);
        }
    }
}

/*------------------------------------------------------------------------------
    DebugLog_IsEnabled
------------------------------------------------------------------------------*/
Boolean DebugLog_IsEnabled(void)
{
    return gLogEnabled;
}

/*------------------------------------------------------------------------------
    DebugLog_Write
------------------------------------------------------------------------------*/
void DebugLog_Write(const char *message)
{
    char line[512];
    long count;
    long ticks;

    if (!gLogEnabled || !gLogInitialized)
        return;

    if (!gLogFileOpen)
    {
        if (OpenLogFile() != noErr)
            return;
    }

    ticks = TickCount();
    sprintf(line, "[%ld] %s\r", ticks, message);
    count = strlen(line);
    FSWrite(gLogRefNum, &count, line);
}

/*------------------------------------------------------------------------------
    DebugLog_WriteNum
------------------------------------------------------------------------------*/
void DebugLog_WriteNum(const char *message, long value)
{
    char line[512];
    long count;
    long ticks;

    if (!gLogEnabled || !gLogInitialized)
        return;

    if (!gLogFileOpen)
    {
        if (OpenLogFile() != noErr)
            return;
    }

    ticks = TickCount();
    sprintf(line, "[%ld] %s %ld\r", ticks, message, value);
    count = strlen(line);
    FSWrite(gLogRefNum, &count, line);
}

/*------------------------------------------------------------------------------
    DebugLog_WriteNum2
------------------------------------------------------------------------------*/
void DebugLog_WriteNum2(const char *message, long val1, long val2)
{
    char line[512];
    long count;
    long ticks;

    if (!gLogEnabled || !gLogInitialized)
        return;

    if (!gLogFileOpen)
    {
        if (OpenLogFile() != noErr)
            return;
    }

    ticks = TickCount();
    sprintf(line, "[%ld] %s %ld, %ld\r", ticks, message, val1, val2);
    count = strlen(line);
    FSWrite(gLogRefNum, &count, line);
}

/*------------------------------------------------------------------------------
    DebugLog_Close
------------------------------------------------------------------------------*/
void DebugLog_Close(void)
{
    if (gLogFileOpen && gLogRefNum != 0)
    {
        char footer[64];
        long count;

        sprintf(footer, "--- Log closed (ticks=%ld) ---\r\r", TickCount());
        count = strlen(footer);
        FSWrite(gLogRefNum, &count, footer);

        FSClose(gLogRefNum);
        gLogRefNum = 0;
        gLogFileOpen = false;
    }
}


/*----------------------------------------------------------------------
    DebugLog_Flush - Force flush log to disk
----------------------------------------------------------------------*/
void DebugLog_Flush(void)
{
    /* Simple flush: write a zero-length block to force sync */
    if (gLogRefNum != 0)
    {
        long zero;
        zero = 0;
        FSWrite(gLogRefNum, &zero, nil);
    }
}
