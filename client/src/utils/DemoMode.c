/*----------------------------------------------------------------------
    DemoMode.c - Automated Test/Demo Sequence

    Types 10 curated questions character-by-character into the chat
    input, sends them through Message_Send, and logs all results
    to the debug log.

    Test coverage for tool model v2:
    - Lookup tier: hardware spec (MacSpecsTable)
    - Canned tier: refusal pattern
    - Model actions: LAUNCH_APP, OPEN_CP, RESTART
    - Model AppleScript: file/app/system commands
    - Model how-to: keyboard procedure (CMD:NONE)
    - Model deflection: factual -> MacinAI Online

    State machine runs cooperatively from the idle handler.
    Message_Send blocks during inference but keeps UI alive
    via the streaming token callback.

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

#pragma segment DemoMode

#include "DemoMode.h"
#include "AppGlobals.h"
#include "ChatWindow.h"
#include "MessageHandler.h"
#include "InferenceGuard.h"
#include "InferenceEngine.h"
#include "Tokenizer.h"
#include "DebugLog.h"
#include "SafeString.h"

#include <TextEdit.h>
#include <Events.h>
#include <Files.h>
#include <Memory.h>
#include <string.h>
#include <stdio.h>

/*----------------------------------------------------------------------
    Forward declarations
----------------------------------------------------------------------*/
static void LogDemoHeader(void);
static void LogDemoFooter(void);
static void LogQuestionResult(void);
static void ExtractLastResponse(char *buf, short maxLen);
static void FlushLog(void);
static const char* RouteToString(InferenceRoute route);
static Boolean CheckForEscapeCancel(void);

/*----------------------------------------------------------------------
    Constants
----------------------------------------------------------------------*/
#define kDemoQuestionCount  10
#define kDemoTypeDelay      3       /* ticks between chars (~50ms) */
#define kDemoWaitDelay      90      /* ticks after response (~1.5s) */
#define kDemoPreStartDelay  60      /* ticks before first question (~1s) */

/*----------------------------------------------------------------------
    Demo State Machine
----------------------------------------------------------------------*/
typedef enum {
    kDemoIdle,
    kDemoPreStart,
    kDemoTyping,
    kDemoSending,
    kDemoWaiting,
    kDemoLogging,
    kDemoNextQuestion,
    kDemoFinished
} DemoState;

/*----------------------------------------------------------------------
    Test Questions -- Tool Model v2

    Covers all 3 routing tiers + tool model capabilities:
    - Q1:     Lookup (hardware spec via MacSpecsTable)
    - Q2:     Canned (refusal pattern)
    - Q3-Q5:  Model action commands (OPEN_CP, LAUNCH_APP, RESTART)
    - Q6-Q8:  Model AppleScript generation (file/app/system)
    - Q9:     Model how-to procedure (CMD:NONE)
    - Q10:    Model deflection to MacinAI Online (CMD:NONE)
----------------------------------------------------------------------*/
static const char *sDemoQuestions[kDemoQuestionCount] = {
    "What processor does the Macintosh SE/30 have?",
    "Can you help me hack into a server?",
    "Open the Memory control panel",
    "Launch SimpleText",
    "Empty the trash",
    "Make a new folder called Projects",
    "Close all Finder windows",
    "How do I rebuild the desktop?",
    "Tell me the history of HyperCard",
    "Restart this Mac"
};

static const char *sDemoExpectedRoute[kDemoQuestionCount] = {
    "LOOKUP", "CANNED", "MODEL", "MODEL", "MODEL",
    "MODEL",  "MODEL",  "MODEL", "MODEL", "MODEL"
};

static const char *sDemoExpectedCmd[kDemoQuestionCount] = {
    "--",           /* LOOKUP - no CMD (MacSpecsTable) */
    "--",           /* CANNED - no CMD (refusal) */
    "OPEN_CP",      /* Memory control panel */
    "LAUNCH_APP",   /* SimpleText */
    "APPLESCRIPT",  /* empty trash (tell Finder) */
    "APPLESCRIPT",  /* new folder (tell Finder) */
    "APPLESCRIPT",  /* close windows (tell Finder) */
    "NONE",         /* how-to: keyboard procedure */
    "NONE",         /* deflection: MacinAI Online */
    "RESTART"       /* restart Mac -- LAST to avoid interrupting test */
};

/*----------------------------------------------------------------------
    Module State
----------------------------------------------------------------------*/
static Boolean   sDemoRunning = false;
static DemoState sDemoState = kDemoIdle;
static short     sQuestionIndex = 0;
static short     sCharIndex = 0;
static long      sNextTickTarget = 0;
static long      sOutputLenBefore = 0;
static char      sActualRoute[16];
static short     sRouteCounts[3];       /* [0]=canned, [1]=lookup, [2]=model */
static long      sDemoStartTick = 0;

/* Externals */
extern AppGlobals gApp;
extern TEHandle gInputTE;
extern TEHandle gOutputTE;

/*----------------------------------------------------------------------
    DemoMode_Start
----------------------------------------------------------------------*/
void DemoMode_Start(void)
{
    char statusMsg[64];

    /* Guard checks */
    if (sDemoRunning)
        return;

    if (gApp.appState != kAppStateMain)
        return;

    if (!Engine_IsReady() || !Tokenizer_IsReady())
    {
        DebugLog_Write("DemoMode: cannot start - engine/tokenizer not ready");
        return;
    }

    /* Initialize state */
    sDemoRunning = true;
    sDemoState = kDemoPreStart;
    sQuestionIndex = 0;
    sCharIndex = 0;
    sRouteCounts[0] = 0;
    sRouteCounts[1] = 0;
    sRouteCounts[2] = 0;
    sActualRoute[0] = '\0';
    sDemoStartTick = TickCount();
    sNextTickTarget = sDemoStartTick + kDemoPreStartDelay;

    /* Force debug logging on for the demo */
    DebugLog_SetEnabled(true);

    /* Log header */
    LogDemoHeader();

    /* Show status */
    sprintf(statusMsg, "Demo: starting (%d questions)...", kDemoQuestionCount);
    SafeStringCopy(gApp.statusText, statusMsg, sizeof(gApp.statusText));
    ChatWindow_UpdateStatus();
}

/*----------------------------------------------------------------------
    DemoMode_Step - Called from ChatScreen_HandleIdle
----------------------------------------------------------------------*/
void DemoMode_Step(void)
{
    if (!sDemoRunning)
        return;

    /* Check for Escape key to cancel demo */
    if (CheckForEscapeCancel())
        return;

    switch (sDemoState)
    {
        case kDemoIdle:
            break;

        case kDemoPreStart:
        {
            /* Brief pause before first question */
            if (TickCount() < sNextTickTarget)
                break;
            sDemoState = kDemoTyping;
            sCharIndex = 0;
            sNextTickTarget = TickCount() + kDemoTypeDelay;
            break;
        }

        case kDemoTyping:
        {
            long now;
            char ch;
            GrafPtr savePort;
            char statusMsg[64];

            now = TickCount();
            if (now < sNextTickTarget)
                break;

            ch = sDemoQuestions[sQuestionIndex][sCharIndex];
            if (ch == '\0')
            {
                /* Done typing -- move to sending */
                sDemoState = kDemoSending;
                break;
            }

            /* Type one character into input TE */
            if (gInputTE != nil)
            {
                GetPort(&savePort);
                SetPort(ChatScreen_GetWindow());
                TEKey(ch, gInputTE);
                SetPort(savePort);
            }

            sCharIndex++;
            sNextTickTarget = now + kDemoTypeDelay;

            /* Update status */
            sprintf(statusMsg, "Demo Q%d/%d: typing...",
                    sQuestionIndex + 1, kDemoQuestionCount);
            SafeStringCopy(gApp.statusText, statusMsg, sizeof(gApp.statusText));
            ChatWindow_UpdateStatus();
            break;
        }

        case kDemoSending:
        {
            PreProcessResult preResult;
            char logLine[256];

            /* Determine what route InferenceGuard would choose */
            preResult = InferenceGuard_PreProcess(
                sDemoQuestions[sQuestionIndex]);
            SafeStringCopy(sActualRoute,
                          RouteToString(preResult.route), sizeof(sActualRoute));

            /* Log question */
            sprintf(logLine, "DEMO Q%d/%d [expected=%s actual=%s]: %s",
                    sQuestionIndex + 1, kDemoQuestionCount,
                    sDemoExpectedRoute[sQuestionIndex],
                    sActualRoute,
                    sDemoQuestions[sQuestionIndex]);
            DebugLog_Write(logLine);

            /* Record output length before send (for response extraction) */
            if (gOutputTE != nil)
                sOutputLenBefore = (*gOutputTE)->teLength;
            else
                sOutputLenBefore = 0;

            /* Send through the normal pipeline.
               This BLOCKS during inference but keeps UI alive
               via the streaming token callback + SystemTask. */
            Message_Send();

            /* Count route */
            switch (preResult.route)
            {
                case kRouteToCanned: sRouteCounts[0]++; break;
                case kRouteToLookup: sRouteCounts[1]++; break;
                case kRouteToModel:  sRouteCounts[2]++; break;
            }

            /* Set wait timer */
            sNextTickTarget = TickCount() + kDemoWaitDelay;
            sDemoState = kDemoWaiting;
            break;
        }

        case kDemoWaiting:
        {
            char statusMsg[64];

            if (TickCount() < sNextTickTarget)
                break;

            /* Update status */
            sprintf(statusMsg, "Demo Q%d/%d: complete",
                    sQuestionIndex + 1, kDemoQuestionCount);
            SafeStringCopy(gApp.statusText, statusMsg, sizeof(gApp.statusText));
            ChatWindow_UpdateStatus();

            sDemoState = kDemoLogging;
            break;
        }

        case kDemoLogging:
        {
            /* Log the response and flush to disk */
            LogQuestionResult();
            FlushLog();

            sDemoState = kDemoNextQuestion;
            break;
        }

        case kDemoNextQuestion:
        {
            sQuestionIndex++;
            sCharIndex = 0;

            if (sQuestionIndex < kDemoQuestionCount)
            {
                /* Brief pause before next question */
                sNextTickTarget = TickCount() + kDemoPreStartDelay;
                sDemoState = kDemoPreStart;
            }
            else
            {
                sDemoState = kDemoFinished;
            }
            break;
        }

        case kDemoFinished:
        {
            char summary[256];
            long elapsed;

            /* Log footer with summary */
            LogDemoFooter();
            FlushLog();

            /* Show completion in chat */
            elapsed = (TickCount() - sDemoStartTick) / 60;
            sprintf(summary,
                    "\rMacinAI: Demo complete: %d questions in %ld seconds "
                    "(%d lookup, %d canned, %d model). "
                    "Results logged to debug log.\r\r",
                    kDemoQuestionCount, elapsed,
                    sRouteCounts[1], sRouteCounts[0], sRouteCounts[2]);
            if (gOutputTE != nil)
            {
                GrafPtr savePort;
                GetPort(&savePort);
                SetPort(ChatScreen_GetWindow());
                TESetSelect(32767, 32767, gOutputTE);
                TEInsert(summary, strlen(summary), gOutputTE);
                ChatWindow_UpdateScrollbar();
                SetPort(savePort);
            }

            /* Reset state */
            sDemoRunning = false;
            sDemoState = kDemoIdle;

            SafeStringCopy(gApp.statusText, "Demo complete",
                          sizeof(gApp.statusText));
            ChatWindow_UpdateStatus();

            AppBeep();
            break;
        }
    }
}

/*----------------------------------------------------------------------
    DemoMode_Stop
----------------------------------------------------------------------*/
void DemoMode_Stop(void)
{
    char logLine[128];

    if (!sDemoRunning)
        return;

    sprintf(logLine, "DEMO ABORTED at Q%d/%d by user",
            sQuestionIndex + 1, kDemoQuestionCount);
    DebugLog_Write(logLine);
    FlushLog();

    sDemoRunning = false;
    sDemoState = kDemoIdle;

    SafeStringCopy(gApp.statusText, "Demo stopped",
                  sizeof(gApp.statusText));
    ChatWindow_UpdateStatus();
}

/*----------------------------------------------------------------------
    DemoMode_IsRunning
----------------------------------------------------------------------*/
Boolean DemoMode_IsRunning(void)
{
    return sDemoRunning;
}

/*======================================================================
    Internal helpers
======================================================================*/

/*----------------------------------------------------------------------
    CheckForEscapeCancel - Poll for Escape key to abort demo

    Uses EventAvail to peek without removing, then GetNextEvent
    to consume the keypress if it's Escape (charCode 27).
    Called every DemoMode_Step iteration -- lightweight check.
----------------------------------------------------------------------*/
static Boolean CheckForEscapeCancel(void)
{
    EventRecord event;

    if (EventAvail(keyDownMask, &event))
    {
        if ((event.message & charCodeMask) == 0x1B)  /* Escape = 0x1B */
        {
            /* Consume the event so it doesn't propagate */
            GetNextEvent(keyDownMask, &event);
            DemoMode_Stop();
            return true;
        }
    }
    return false;
}

/*----------------------------------------------------------------------
    LogDemoHeader
----------------------------------------------------------------------*/
static void LogDemoHeader(void)
{
    char line[256];
    char engineStatus[128];

    DebugLog_Write("========================================");
    DebugLog_Write("DEMO MODE START -- Tool Model v2 Suite");

    Engine_GetStatusString(engineStatus, sizeof(engineStatus));
    sprintf(line, "Engine: %s", engineStatus);
    DebugLog_Write(line);

    sprintf(line, "Questions: %d (1 lookup, 1 canned, 8 model)",
            kDemoQuestionCount);
    DebugLog_Write(line);
    DebugLog_Write("  Model tests: 3 action, 3 AppleScript, 1 how-to, 1 deflection");
    DebugLog_Write("========================================");
}

/*----------------------------------------------------------------------
    LogDemoFooter
----------------------------------------------------------------------*/
static void LogDemoFooter(void)
{
    char line[256];
    long elapsed;

    elapsed = (TickCount() - sDemoStartTick) / 60;

    DebugLog_Write("========================================");
    sprintf(line, "DEMO COMPLETE: %d/%d questions, %ld seconds total",
            kDemoQuestionCount, kDemoQuestionCount, elapsed);
    DebugLog_Write(line);

    sprintf(line, "Routes: %d lookup, %d canned, %d model",
            sRouteCounts[1], sRouteCounts[0], sRouteCounts[2]);
    DebugLog_Write(line);
    DebugLog_Write("========================================");
}

/*----------------------------------------------------------------------
    LogQuestionResult - Log the response from the last question
----------------------------------------------------------------------*/
static void LogQuestionResult(void)
{
    char response[512];
    char logLine[600];

    /* Extract the AI's response from gOutputTE */
    ExtractLastResponse(response, sizeof(response));

    /* Log expected CMD for model-tier questions */
    sprintf(logLine, "DEMO A%d/%d [expect_cmd=%s]: %.480s",
            sQuestionIndex + 1, kDemoQuestionCount,
            sDemoExpectedCmd[sQuestionIndex],
            response);
    DebugLog_Write(logLine);

    DebugLog_Write("---");
}

/*----------------------------------------------------------------------
    ExtractLastResponse - Get the AI's last response from gOutputTE

    After Message_Send, gOutputTE contains:
      ...previous content...
      You: <question>\r\r
      MacinAI: <response>\r\r
      [optional action result]\r\r

    We read from sOutputLenBefore to current end, then find
    the last "MacinAI: " prefix and extract from there.
----------------------------------------------------------------------*/
static void ExtractLastResponse(char *buf, short maxLen)
{
    Handle hText;
    long totalLen;
    long newStart;
    long newLen;
    long searchStart;
    long i;
    long responseStart;
    long copyLen;
    char *textPtr;

    buf[0] = '\0';

    if (gOutputTE == nil)
        return;

    HLock((Handle)gOutputTE);
    hText = (*gOutputTE)->hText;
    totalLen = (*gOutputTE)->teLength;
    HUnlock((Handle)gOutputTE);

    if (totalLen <= sOutputLenBefore)
        return;

    newStart = sOutputLenBefore;
    newLen = totalLen - newStart;

    HLock(hText);
    textPtr = *hText;

    /* Search for last "MacinAI: " in the new text */
    responseStart = -1;
    searchStart = newStart;
    for (i = totalLen - 10; i >= searchStart; i--)
    {
        if (textPtr[i] == 'M' &&
            i + 9 <= totalLen &&
            textPtr[i + 1] == 'a' &&
            textPtr[i + 2] == 'c' &&
            textPtr[i + 3] == 'i' &&
            textPtr[i + 4] == 'n' &&
            textPtr[i + 5] == 'A' &&
            textPtr[i + 6] == 'I' &&
            textPtr[i + 7] == ':' &&
            textPtr[i + 8] == ' ')
        {
            responseStart = i + 9;  /* Skip "MacinAI: " */
            break;
        }
    }

    if (responseStart < 0)
    {
        /* Fallback: just grab everything new */
        responseStart = newStart;
    }

    /* Copy response text, skipping trailing \r */
    copyLen = totalLen - responseStart;
    while (copyLen > 0 && (textPtr[responseStart + copyLen - 1] == '\r' ||
                            textPtr[responseStart + copyLen - 1] == '\n' ||
                            textPtr[responseStart + copyLen - 1] == ' '))
    {
        copyLen--;
    }

    if (copyLen >= maxLen)
        copyLen = maxLen - 1;
    if (copyLen > 0)
        BlockMoveData(textPtr + responseStart, buf, copyLen);
    buf[copyLen] = '\0';

    HUnlock(hText);
}

/*----------------------------------------------------------------------
    FlushLog - Force all pending writes to disk (crash safety)
----------------------------------------------------------------------*/
static void FlushLog(void)
{
    FlushVol(nil, 0);
}

/*----------------------------------------------------------------------
    RouteToString - Convert InferenceRoute enum to log string
----------------------------------------------------------------------*/
static const char* RouteToString(InferenceRoute route)
{
    switch (route)
    {
        case kRouteToCanned: return "CANNED";
        case kRouteToLookup: return "LOOKUP";
        case kRouteToModel:  return "MODEL";
    }
    return "UNKNOWN";
}
