/*------------------------------------------------------------------------------
    ActionDispatcher.c - Local AI Command Dispatching

    Dispatches command tokens from local model output to system actions.
    Includes AppleScript execution via OSA for CMD:APPLESCRIPT.

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

#include "ActionDispatcher.h"
#include "AppleScriptExec.h"
#include "SystemActions.h"
#include "AppGlobals.h"
#include "SpeechManager.h"
#include "Tokenizer.h"
#include "SafeString.h"

#include <Types.h>
#include <string.h>
#include <stdio.h>

#pragma segment System

/*------------------------------------------------------------------------------
    Forward declarations
------------------------------------------------------------------------------*/
static Boolean ParseCommandType(const char *commandStr, char *commandType,
                                const char **paramsStart);

/*------------------------------------------------------------------------------
    Module Globals
------------------------------------------------------------------------------*/
static Boolean gDispatcherInitialized = false;

/*------------------------------------------------------------------------------
    ActionDispatcher_Initialize
------------------------------------------------------------------------------*/
OSErr ActionDispatcher_Initialize(void)
{
    OSErr err;

    if (gDispatcherInitialized)
        return noErr;

    err = SystemActions_Initialize();
    if (err != noErr)
        return err;

    /* Initialize AppleScript (non-fatal if unavailable) */
    AppleScript_Initialize();

    gDispatcherInitialized = true;
    return noErr;
}

/*------------------------------------------------------------------------------
    ActionDispatcher_ProcessToken - Process command token from model
------------------------------------------------------------------------------*/
short ActionDispatcher_ProcessToken(long cmdToken, const char *argument,
                                    char *resultMsg, short maxLen)
{
    OSErr err;

    if (resultMsg == nil || maxLen < 64)
    {
        return kActionResultInvalidFormat;
    }

    switch (cmdToken)
    {
        case kTokenCmdNone:
            sprintf(resultMsg, "No action needed");
            return kActionResultNoAction;

        case kTokenCmdLaunchApp:
            if (argument == nil || argument[0] == '\0') {
                sprintf(resultMsg, "ERROR: No application name specified");
                return kActionResultInvalidFormat;
            }
            err = SystemActions_LaunchApplication(argument);
            if (err == noErr) {
                sprintf(resultMsg, "Launched %s", argument);
                return kActionResultSuccess;
            } else if (err == 1) {
                /* Multiple results - build numbered list */
                {
                    short i;
                    char line[128];
                    SafeStringCopy(resultMsg, "Multiple matches found:\r", maxLen);
                    for (i = 0; i < gApp.choiceState.resultCount; i++)
                    {
                        sprintf(line, "  %d. %s\r", i + 1,
                                gApp.choiceState.results[i].name);
                        SafeStringCat(resultMsg, line, maxLen);
                    }
                    SafeStringCat(resultMsg, "Type a number to choose.", maxLen);
                }
                return kActionResultSuccess;
            } else if (err == fnfErr) {
                sprintf(resultMsg, "Application '%s' not found", argument);
                return kActionResultExecutionError;
            } else {
                sprintf(resultMsg, "Launch failed (error %d)", err);
                return kActionResultExecutionError;
            }

        case kTokenCmdOpenCP:
            if (argument == nil || argument[0] == '\0') {
                sprintf(resultMsg, "ERROR: No control panel name specified");
                return kActionResultInvalidFormat;
            }
            err = SystemActions_OpenControlPanel(argument);
            if (err == noErr) {
                sprintf(resultMsg, "SUCCESS: Control panel opened");
                return kActionResultSuccess;
            } else if (err == fnfErr) {
                sprintf(resultMsg, "ERROR: Control panel '%s' not found", argument);
                return kActionResultExecutionError;
            } else {
                sprintf(resultMsg, "ERROR: Failed to open control panel (code %d)", err);
                return kActionResultExecutionError;
            }

        case kTokenCmdQuerySys:
            if (argument == nil || argument[0] == '\0') {
                sprintf(resultMsg, "ERROR: No query type specified");
                return kActionResultInvalidFormat;
            }
            err = SystemActions_QuerySystem(argument, resultMsg, maxLen);
            if (err != noErr) {
                sprintf(resultMsg, "ERROR: Query failed (code %d)", err);
                return kActionResultExecutionError;
            }
            return kActionResultSuccess;

        case kTokenCmdRefresh:
            /* Local model doesn't need server refresh - just query memory */
            err = SystemActions_QuerySystem("MEMORY", resultMsg, maxLen);
            return (err == noErr) ? kActionResultSuccess : kActionResultExecutionError;

        case kTokenCmdShowAlert:
            if (argument != nil && argument[0] != '\0') {
                SystemActions_ShowAlert(argument);
                sprintf(resultMsg, "Alert shown");
            } else {
                sprintf(resultMsg, "ERROR: No alert message");
                return kActionResultInvalidFormat;
            }
            return kActionResultSuccess;

        case kTokenCmdShutdown:
            err = SystemActions_Shutdown(false);  /* No dialog - user already typed intent */
            if (err == noErr) {
                sprintf(resultMsg, "SUCCESS: Shutdown initiated");
                Speech_FeedbackInterrupt("Shutting your Macintosh down");
            } else if (err == userCanceledErr) {
                sprintf(resultMsg, "CANCELED: User canceled shutdown");
                return kActionResultUserCanceled;
            } else {
                sprintf(resultMsg, "ERROR: Shutdown failed (code %d)", err);
                return kActionResultExecutionError;
            }
            return kActionResultSuccess;

        case kTokenCmdRestart:
            err = SystemActions_Restart(false);  /* No dialog - user already typed intent */
            if (err == noErr) {
                sprintf(resultMsg, "SUCCESS: Restart initiated");
                Speech_FeedbackInterrupt("Restarting your Macintosh");
            } else if (err == userCanceledErr) {
                sprintf(resultMsg, "CANCELED: User canceled restart");
                return kActionResultUserCanceled;
            } else {
                sprintf(resultMsg, "ERROR: Restart failed (code %d)", err);
                return kActionResultExecutionError;
            }
            return kActionResultSuccess;

        case kTokenCmdAppleScript:
            #if ENABLE_DEBUG_LOGGING
            {
                extern void DebugLog_Write(const char *message);
                extern void DebugLog_WriteNum(const char *label, long value);
                char logBuf[256];
                long argLogLen;
                DebugLog_Write("=== ActionDispatcher: CMD:APPLESCRIPT ===");
                if (argument != nil) {
                    argLogLen = strlen(argument);
                    DebugLog_WriteNum("ActionDisp: argument length =", argLogLen);
                    /* Log first 200 chars of the script argument */
                    {
                        long logCopy;
                        long ci;
                        logCopy = argLogLen;
                        if (logCopy > 200) logCopy = 200;
                        sprintf(logBuf, "ActionDisp: script (first %ld chars):", logCopy);
                        DebugLog_Write(logBuf);
                        {
                            char argChunk[201];
                            BlockMoveData(argument, argChunk, logCopy);
                            for (ci = 0; ci < logCopy; ci++) {
                                if (argChunk[ci] == '\n') argChunk[ci] = '|';
                                if (argChunk[ci] == '\r') argChunk[ci] = '|';
                            }
                            argChunk[logCopy] = '\0';
                            DebugLog_Write(argChunk);
                        }
                    }
                } else {
                    DebugLog_Write("ActionDisp: argument is nil!");
                }
            }
            #endif
            if (argument == nil || argument[0] == '\0') {
                sprintf(resultMsg, "ERROR: No AppleScript provided");
                return kActionResultInvalidFormat;
            }
            if (!AppleScript_IsAvailable()) {
                sprintf(resultMsg, "AppleScript not available on this system");
                return kActionResultExecutionError;
            }
            #if ENABLE_DEBUG_LOGGING
            {
                extern void DebugLog_Write(const char *message);
                DebugLog_Write("ActionDisp: calling AppleScript_ExecuteWithConfirm...");
            }
            #endif
            err = AppleScript_ExecuteWithConfirm(argument, resultMsg, maxLen);
            #if ENABLE_DEBUG_LOGGING
            {
                extern void DebugLog_Write(const char *message);
                extern void DebugLog_WriteNum(const char *label, long value);
                char logBuf[256];
                DebugLog_WriteNum("ActionDisp: ExecuteWithConfirm returned err =", (long)err);
                sprintf(logBuf, "ActionDisp: resultMsg = %.200s", resultMsg);
                DebugLog_Write(logBuf);
            }
            #endif
            if (err == userCanceledErr) {
                return kActionResultUserCanceled;
            }
            return (err == noErr) ? kActionResultSuccess : kActionResultExecutionError;

        default:
            sprintf(resultMsg, "ERROR: Unknown command token %d", cmdToken);
            return kActionResultUnknownCommand;
    }
}

/*------------------------------------------------------------------------------
    ParseCommandType - Extract command type from ACTION= string
------------------------------------------------------------------------------*/
static Boolean ParseCommandType(const char *commandStr, char *commandType,
                                const char **paramsStart)
{
    const char *p;
    int i;

    if (strncmp(commandStr, "ACTION=", 7) != 0)
        return false;

    p = commandStr + 7;
    i = 0;
    while (*p != '\0' && *p != ':' && i < 63) {
        commandType[i] = *p;
        i++;
        p++;
    }
    commandType[i] = '\0';

    if (*p == ':')
        *paramsStart = p + 1;
    else
        *paramsStart = p;

    return true;
}

/*------------------------------------------------------------------------------
    ActionDispatcher_ProcessCommand - Legacy ACTION= string processing
------------------------------------------------------------------------------*/
short ActionDispatcher_ProcessCommand(const char *commandStr,
                                      char *resultMsg, short maxLen)
{
    char commandType[64];
    const char *paramsStart;
    OSErr err;

    if (commandStr == nil || resultMsg == nil || maxLen < 64) {
        sprintf(resultMsg, "ERROR: Invalid parameters");
        return kActionResultInvalidFormat;
    }

    if (!ParseCommandType(commandStr, commandType, &paramsStart)) {
        sprintf(resultMsg, "ERROR: Invalid command format");
        return kActionResultInvalidFormat;
    }

    if (strcmp(commandType, "LAUNCH_APP") == 0) {
        if (*paramsStart == '\0') {
            sprintf(resultMsg, "ERROR: LAUNCH_APP requires path");
            return kActionResultInvalidFormat;
        }
        err = SystemActions_LaunchAppByPath(paramsStart, resultMsg);
        return (err == noErr) ? kActionResultSuccess : kActionResultExecutionError;
    }
    else if (strcmp(commandType, "OPEN_CP") == 0) {
        if (*paramsStart == '\0') {
            sprintf(resultMsg, "ERROR: OPEN_CP requires name");
            return kActionResultInvalidFormat;
        }
        err = SystemActions_OpenControlPanel(paramsStart);
        if (err == noErr)
            sprintf(resultMsg, "SUCCESS: Control panel opened");
        else
            sprintf(resultMsg, "ERROR: Control panel not found");
        return (err == noErr) ? kActionResultSuccess : kActionResultExecutionError;
    }
    else if (strcmp(commandType, "QUERY_SYSTEM") == 0) {
        err = SystemActions_QuerySystem(paramsStart, resultMsg, maxLen);
        return (err == noErr) ? kActionResultSuccess : kActionResultExecutionError;
    }
    else if (strcmp(commandType, "SHUTDOWN") == 0) {
        return ActionDispatcher_ProcessToken(kTokenCmdShutdown, nil, resultMsg, maxLen);
    }
    else if (strcmp(commandType, "RESTART") == 0) {
        return ActionDispatcher_ProcessToken(kTokenCmdRestart, nil, resultMsg, maxLen);
    }
    else {
        sprintf(resultMsg, "ERROR: Unknown command '%s'", commandType);
        return kActionResultUnknownCommand;
    }
}
