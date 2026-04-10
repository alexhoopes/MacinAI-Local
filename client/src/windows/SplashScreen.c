/*------------------------------------------------------------------------------
    SplashScreen.c - Splash Screen with Model Loading

    Shows welcome screen while detecting hardware and loading the model.
    Progress bar shows model loading status.

    Replaces MacinAI relay version:
    - Removed network connection test
    - Removed server version check
    - Added hardware detection display
    - Added model loading progress bar

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

#include "SplashScreen.h"
#include "AppVersion.h"
#include "DrawingHelpers.h"
#include "SystemDiscovery.h"
#include "InferenceEngine.h"
#include "Tokenizer.h"
#include "SpeechManager.h"
#include "SafeString.h"
#include "DebugLog.h"

#include <Quickdraw.h>
#include <Fonts.h>
#include <Events.h>
#include <Windows.h>
#include <Icons.h>
#include <string.h>
#include <Files.h>
#include <stdio.h>

#pragma segment Splash

/*------------------------------------------------------------------------------
    Module Globals
------------------------------------------------------------------------------*/
static WindowPtr sSplashWindow = nil;
static Rect sStartRect = {0, 0, 0, 0};
static Boolean sModelLoadStarted = false;
static char sStatusMessage[128] = "";
static short sCurrentProgress = 0;

extern AppGlobals gApp;

/* SplashLog - routes to unified DebugLog */
static void SplashLog(const char *msg)
{
    DebugLog_Write(msg);
}
extern void AutoDetectModel(void);




/*------------------------------------------------------------------------------
    Forward declarations
------------------------------------------------------------------------------*/
static void DrawSplashContent(void);
static void UpdateStatusMessage(const char *msg);
static void DrawProgressBar(long percent);

/*------------------------------------------------------------------------------
    Engine_ProgressUpdate - Called by engine during init
    This is the extern function declared in InferenceEngine.h
------------------------------------------------------------------------------*/
void Engine_ProgressUpdate(long percent, char *message)
{
    EventRecord dummyEvent;
    char logMsg[128];

    if (sSplashWindow == nil)
        return;

    sprintf(logMsg, "ProgressUpdate: %ld%% - %s", percent, message != nil ? message : "(nil)");
    SplashLog(logMsg);

    DrawProgressBar(percent);
    if (message != nil)
        UpdateStatusMessage(message);

    /* Flush screen so user sees the update */
    GetNextEvent(0, &dummyEvent);
    SystemTask();
}

/*------------------------------------------------------------------------------
    SplashScreen_Show
------------------------------------------------------------------------------*/
void SplashScreen_Show(void)
{
    Rect windowRect;
    short screenWidth, screenHeight;

    /* Show watch cursor immediately so user knows app is working */
    SetCursor(*GetCursor(watchCursor));

    /* Calculate centered position */
    screenWidth = gApp.qd.screenBits.bounds.right;
    screenHeight = gApp.qd.screenBits.bounds.bottom;

    SetRect(&windowRect,
            (screenWidth - 400) / 2,
            (screenHeight - 280) / 2,
            (screenWidth + 400) / 2,
            (screenHeight + 280) / 2);

    /* Create splash window */
    sSplashWindow = NewCWindow(nil, &windowRect, "\p",
                              true, dBoxProc,
                              (WindowPtr)-1, false, 0);

    if (sSplashWindow == nil)
    {
        TransitionToState(kAppStateMain);
        return;
    }

    SetPort(sSplashWindow);
    gApp.currentWindow = sSplashWindow;

    /* Set up Start button */
    SetRect(&sStartRect, 150, 230, 250, 260);

    sModelLoadStarted = false;

    SplashLog("SplashScreen_Show: detecting hardware...");
    /* Detect hardware FIRST so splash shows real values */
    SystemDiscovery_DetectHardware();
    SplashLog("SplashScreen_Show: hardware detected");

    SafeStringCopy(sStatusMessage, "Initializing...", sizeof(sStatusMessage));

    /* Draw initial content (hardware info now populated) */
    DrawSplashContent();

    /* Force screen update so splash is visible before heavy init */
    {
        EventRecord flushEvent;
        GetNextEvent(updateMask, &flushEvent);
        SystemTask();
        GetNextEvent(0, &flushEvent);
        SystemTask();
    }

    /* Update status with hardware info */
    {
        char hwMsg[128];
        sprintf(hwMsg, "%s | %ldMB RAM | %s",
                gApp.hardware.machineName,
                gApp.hardware.physicalRAM / (1024L * 1024L),
                gApp.hardware.hasFPU ? "FPU detected" : "No FPU (SANE)");
        UpdateStatusMessage(hwMsg);
    }

    /* Initialize engine with real-time progress */
    {
        OSErr err;
        EventRecord dummyEvent;
        char arenaMsg[128];

        SplashLog("SplashScreen_Show: starting engine init");
        UpdateStatusMessage("Initializing inference engine...");
        DrawProgressBar(5);
        GetNextEvent(0, &dummyEvent);
        SystemTask();

        /* Engine init with progress callback (arena alloc + zeroing = 5-85%) */
        SplashLog("SplashScreen_Show: calling Engine_InitializeWithProgress");
        Engine_SetProgressCallback();
        err = Engine_InitializeWithProgress();
        SplashLog("SplashScreen_Show: engine init returned");

        if (err != noErr)
        {
            DrawProgressBar(30);
            UpdateStatusMessage("Engine init failed - check memory");
        }
        else
        {
            /* Initialize tokenizer */
            UpdateStatusMessage("Loading tokenizer...");
            DrawProgressBar(88);
            GetNextEvent(0, &dummyEvent);
            SystemTask();

            /* Initialize speech */
            UpdateStatusMessage("Checking Speech Manager...");
            DrawProgressBar(90);
            Speech_Initialize();
            Speech_ApplySettings();
            GetNextEvent(0, &dummyEvent);
            SystemTask();

            /* Auto-detect and load model from app folder */
            /* AutoDetectModel calls Engine_LoadModel which updates progress 5-90% */
            AutoDetectModel();

            if (gApp.model.modelFileValid)
            {
                DrawProgressBar(100);
                sprintf(arenaMsg, "Model loaded. Ready.");
            }
            else
            {
                DrawProgressBar(100);
                sprintf(arenaMsg, "No model found. Use File > Open.");
            }
            UpdateStatusMessage(arenaMsg);
        }
    }

    SplashLog("SplashScreen_Show: complete");
    /* Log continues in DebugLog */

    /* Restore arrow cursor */
    InitCursor();
}

/*------------------------------------------------------------------------------
    DrawSplashContent
------------------------------------------------------------------------------*/
static void DrawSplashContent(void)
{
    Rect iconRect;
    Handle iconSuite;
    OSErr err;
    short textWidth;
    const char *versionStr;

    if (sSplashWindow == nil)
        return;

    SetPort(sSplashWindow);
    EraseRect(&sSplashWindow->portRect);

    /* Draw MacinAI icon (ID 128) centered at top */
    SetRect(&iconRect, 184, 20, 216, 52);
    iconSuite = nil;
    err = GetIconSuite(&iconSuite, 128, svLarge1Bit | svLarge4Bit | svLarge8Bit);
    if (err == noErr && iconSuite != nil)
    {
        PlotIconSuite(&iconRect, atNone, ttNone, iconSuite);
        DisposeIconSuite(iconSuite, true);
    }

    /* Title */
    TextFont(3);  /* Geneva */
    TextSize(18);
    TextFace(bold);
    textWidth = StringWidth("\pMacinAI Local");
    MoveTo((400 - textWidth) / 2, 80);
    DrawString("\pMacinAI Local");

    /* Subtitle */
    TextSize(10);
    TextFace(normal);
    textWidth = StringWidth("\pAI on your Mac. No internet required.");
    MoveTo((400 - textWidth) / 2, 98);
    DrawString("\pAI on your Mac. No internet required.");

    /* Version */
    versionStr = GetFullVersionString();
    {
        Str255 pVersion;
        short vLen = strlen(versionStr);
        if (vLen > 255) vLen = 255;
        pVersion[0] = vLen;
        BlockMoveData(versionStr, &pVersion[1], vLen);

        TextSize(9);
        TextFace(italic);
        textWidth = StringWidth(pVersion);
        MoveTo((400 - textWidth) / 2, 115);
        DrawString(pVersion);
        TextFace(normal);
    }

    /* Separator line */
    MoveTo(40, 125);
    LineTo(360, 125);

    /* Hardware info area - two lines */
    TextFont(4);  /* Monaco */
    TextSize(9);
    {
        char line1[128];
        char line2[128];
        Str255 pLine;
        short lineLen;

        /* Line 1: Machine + CPU + FPU */
        sprintf(line1, "%s | %s - %s",
                gApp.hardware.machineName,
                gApp.hardware.cpuType >= 0x0100 ? "PowerPC" : gApp.hardware.cpuType >= 4 ? "68040" : "68030",
                gApp.hardware.hasFPU ? "FPU" : "No FPU");
        lineLen = strlen(line1);
        if (lineLen > 255) lineLen = 255;
        pLine[0] = lineLen;
        BlockMoveData(line1, &pLine[1], lineLen);
        MoveTo(40, 140);
        DrawString(pLine);

        /* Line 2: RAM + System version */
        sprintf(line2, "%ldMB RAM (%ldMB free) | System %s",
                gApp.hardware.physicalRAM / (1024L * 1024L),
                gApp.hardware.availableRAM / (1024L * 1024L),
                gApp.hardware.systemVersion);
        lineLen = strlen(line2);
        if (lineLen > 255) lineLen = 255;
        pLine[0] = lineLen;
        BlockMoveData(line2, &pLine[1], lineLen);
        MoveTo(40, 154);
        DrawString(pLine);
    }

    /* Status message */
    TextFont(3);
    TextSize(10);
    {
        Str255 pStatus;
        short sLen = strlen(sStatusMessage);
        if (sLen > 255) sLen = 255;
        pStatus[0] = sLen;
        BlockMoveData(sStatusMessage, &pStatus[1], sLen);
        textWidth = StringWidth(pStatus);
        MoveTo((400 - textWidth) / 2, 200);
        DrawString(pStatus);
    }

    /* Progress bar (preserves current fill level) */
    DrawProgressBar(sCurrentProgress);

    /* Start button - grayed out until loading complete */
    DrawButton(&sStartRect, "\pStart", false);
    if (sCurrentProgress < 100)
    {
        PenPat(&gApp.qd.gray);
        PenMode(patBic);
        PaintRect(&sStartRect);
        PenMode(patCopy);
        PenPat(&gApp.qd.black);
    }
}

/*------------------------------------------------------------------------------
    UpdateStatusMessage
------------------------------------------------------------------------------*/
static void UpdateStatusMessage(const char *msg)
{
    Rect statusRect;

    SafeStringCopy(sStatusMessage, msg, sizeof(sStatusMessage));

    if (sSplashWindow == nil)
        return;

    SetPort(sSplashWindow);

    /* Erase and redraw status area */
    SetRect(&statusRect, 40, 185, 360, 220);
    EraseRect(&statusRect);

    TextFont(3);
    TextSize(10);
    TextFace(normal);
    {
        Str255 pStatus;
        short sLen = strlen(sStatusMessage);
        short textWidth;
        if (sLen > 255) sLen = 255;
        pStatus[0] = sLen;
        BlockMoveData(sStatusMessage, &pStatus[1], sLen);
        textWidth = StringWidth(pStatus);
        MoveTo((400 - textWidth) / 2, 200);
        DrawString(pStatus);
    }
}

/*------------------------------------------------------------------------------
    DrawProgressBar
------------------------------------------------------------------------------*/
static void DrawProgressBar(long percent)
{
    Rect barRect, fillRect;

    if (sSplashWindow == nil)
        return;

    sCurrentProgress = percent;
    SetPort(sSplashWindow);

    SetRect(&barRect, 80, 165, 320, 178);
    FrameRect(&barRect);

    if (percent > 0)
    {
        short fillWidth;
        if (percent > 100) percent = 100;
        fillWidth = ((barRect.right - barRect.left - 2) * percent) / 100;
        SetRect(&fillRect, barRect.left + 1, barRect.top + 1,
                barRect.left + 1 + fillWidth, barRect.bottom - 1);
        PaintRect(&fillRect);
    }
}

/*------------------------------------------------------------------------------
    SplashScreen_HandleEvent
------------------------------------------------------------------------------*/
Boolean SplashScreen_HandleEvent(EventRecord *event)
{
    Point localPt;

    if (sSplashWindow == nil)
        return false;

    switch (event->what)
    {
        case mouseDown:
        {
            WindowPtr clickWindow;
            short part = FindWindow(event->where, &clickWindow);

            if (clickWindow == sSplashWindow && part == inContent)
            {
                SetPort(sSplashWindow);
                localPt = event->where;
                GlobalToLocal(&localPt);

                if (PtInRect(localPt, &sStartRect) && sCurrentProgress >= 100)
                {
                    /* Start button clicked (only when loading complete) */
                    DrawButton(&sStartRect, "\pStart", true);
                    while (StillDown()) { /* wait */ }
                    DrawButton(&sStartRect, "\pStart", false);

                    /* Transition to main chat */
                    TransitionToState(kAppStateMain);
                    return true;
                }
            }
            break;
        }

        case keyDown:
        case autoKey:
        {
            char key = event->message & charCodeMask;
            if (key == '\r' || key == '\n')
            {
                TransitionToState(kAppStateMain);
                return true;
            }
            break;
        }

        case updateEvt:
            if ((WindowPtr)event->message == sSplashWindow)
            {
                BeginUpdate(sSplashWindow);
                DrawSplashContent();
                EndUpdate(sSplashWindow);
                return true;
            }
            break;
    }

    return false;
}

/*------------------------------------------------------------------------------
    SplashScreen_Draw
------------------------------------------------------------------------------*/
void SplashScreen_Draw(WindowPtr window)
{
    if (window == sSplashWindow)
        DrawSplashContent();
}

/*------------------------------------------------------------------------------
    SplashScreen_Close
------------------------------------------------------------------------------*/
void SplashScreen_Close(void)
{
    if (sSplashWindow != nil)
    {
        DisposeWindow(sSplashWindow);
        sSplashWindow = nil;
    }
}
