/*------------------------------------------------------------------------------
    SettingsDialog.c - Settings Panel

    Two-tab settings dialog:
    1. Speech - Voice selection, rate, auto-speak toggle
    2. About  - Hardware info, model status, version

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

#include "SettingsDialog.h"
#include "AppGlobals.h"
#include "AppVersion.h"
#include "DrawingHelpers.h"
#include "SpeechManager.h"
#include "SystemDiscovery.h"
#include "InferenceEngine.h"
#include "SafeString.h"
#include "DebugLog.h"

#include <Quickdraw.h>
#include <Fonts.h>
#include <Events.h>
#include <Windows.h>
#include <Controls.h>
#include <Menus.h>
#include <string.h>
#include <stdio.h>

#pragma segment Settings

/*------------------------------------------------------------------------------
    Constants
------------------------------------------------------------------------------*/
#define kSettingsWidth      360
#define kSettingsHeight     300
#define kTabHeight          24
#define kNumTabs            3

/*------------------------------------------------------------------------------
    Tab identifiers
------------------------------------------------------------------------------*/
typedef enum {
    kTabSpeech = 0,
    kTabModel  = 1,
    kTabAbout  = 2
} SettingsTab;

/*------------------------------------------------------------------------------
    Module Globals
------------------------------------------------------------------------------*/
static WindowPtr sSettingsWindow = nil;
static SettingsTab sCurrentTab = kTabSpeech;
static Rect sTabRects[kNumTabs];
static Rect sContentRect;
static Rect sOKRect;
static Rect sCancelRect;

/* Checkbox hit rects (Speech tab) */
static Rect sCheckEnableSpeech = {0, 0, 0, 0};
static Rect sCheckAutoSpeak = {0, 0, 0, 0};
static Rect sCheckFeedback = {0, 0, 0, 0};
static Rect sCheckStreamSpeech = {0, 0, 0, 0};

/* Model tab hit rect */
static Rect sLoadModelBtnRect = {0, 0, 0, 0};
static Rect sCheckDebugLog = {0, 0, 0, 0};

/* Popup hit rects (Speech tab) */
static Rect sVoicePopupRect = {0, 0, 0, 0};
static Rect sRatePopupRect = {0, 0, 0, 0};

/* Popup menus */
static MenuHandle sVoiceMenu = nil;
static MenuHandle sRateMenu = nil;

/* Rate labels and percent values */
static const unsigned char *sRateLabels[7] = {
    "\pSlowest", "\pSlower", "\pSlow", "\pNormal",
    "\pFast", "\pFaster", "\pFastest"
};
static short sRatePercents[7] = {10, 25, 35, 50, 65, 80, 95};

extern AppGlobals gApp;
extern void HandleOpenModel(void);

/*------------------------------------------------------------------------------
    Forward declarations
------------------------------------------------------------------------------*/
static void DrawSettingsContent(void);
static void DrawTabBar(void);
static void DrawSpeechTab(void);
static void DrawModelTab(void);
static void DrawAboutTab(void);
static void HandleTabClick(Point localPt);
static void HandleSpeechClick(Point localPt);
static void HandleModelClick(Point localPt);
static void BuildPopupMenus(void);
static void CleanupPopupMenus(void);
static short RatePercentToIndex(short percent);
static void DrawPopup(Rect *r, ConstStr255Param label);

/*------------------------------------------------------------------------------
    RatePercentToIndex - Map rate percent to menu index (1-based)
------------------------------------------------------------------------------*/
static short RatePercentToIndex(short percent)
{
    short i;
    short bestIdx = 4; /* default Normal */
    short bestDist = 32767;
    for (i = 0; i < 7; i++)
    {
        short dist = percent - sRatePercents[i];
        if (dist < 0) dist = -dist;
        if (dist < bestDist)
        {
            bestDist = dist;
            bestIdx = i + 1;
        }
    }
    return bestIdx;
}

/*------------------------------------------------------------------------------
    DrawPopup - Draw a popup-style rectangle with text and down arrow
------------------------------------------------------------------------------*/
static void DrawPopup(Rect *r, ConstStr255Param label)
{
    EraseRect(r);
    FrameRect(r);
    if (label[0] > 0)
    {
        MoveTo(r->left + 5, r->top + 14);
        DrawString(label);
    }
    /* Down arrow */
    MoveTo(r->right - 14, r->top + 7);
    LineTo(r->right - 9, r->top + 13);
    LineTo(r->right - 4, r->top + 7);
}

/*------------------------------------------------------------------------------
    BuildPopupMenus - Create voice and rate popup menus
------------------------------------------------------------------------------*/
static void BuildPopupMenus(void)
{
    short voiceCount;
    short i;
    Str255 voiceName;

    /* Voice menu */
    sVoiceMenu = NewMenu(201, "\pVoices");
    if (sVoiceMenu != nil)
    {
        voiceCount = Speech_GetVoiceCount();
        for (i = 1; i <= voiceCount; i++)
        {
            Speech_GetVoiceName(i, voiceName);
            if (voiceName[0] > 0)
            {
                AppendMenu(sVoiceMenu, "\px");
                SetMenuItemText(sVoiceMenu, i, voiceName);
            }
        }
        InsertMenu(sVoiceMenu, -1);
    }

    /* Rate menu */
    sRateMenu = NewMenu(202, "\pRate");
    if (sRateMenu != nil)
    {
        for (i = 0; i < 7; i++)
            AppendMenu(sRateMenu, sRateLabels[i]);
        InsertMenu(sRateMenu, -1);
    }
}

/*------------------------------------------------------------------------------
    CleanupPopupMenus
------------------------------------------------------------------------------*/
static void CleanupPopupMenus(void)
{
    if (sVoiceMenu != nil)
    {
        DeleteMenu(201);
        DisposeMenu(sVoiceMenu);
        sVoiceMenu = nil;
    }
    if (sRateMenu != nil)
    {
        DeleteMenu(202);
        DisposeMenu(sRateMenu);
        sRateMenu = nil;
    }
}

/*------------------------------------------------------------------------------
    SettingsDialog_Show
------------------------------------------------------------------------------*/
void SettingsDialog_Show(void)
{
    Rect windowRect;
    EventRecord event;
    Boolean done = false;
    Point localPt;
    short screenWidth, screenHeight;

    screenWidth = gApp.qd.screenBits.bounds.right;
    screenHeight = gApp.qd.screenBits.bounds.bottom;

    SetRect(&windowRect,
            (screenWidth - kSettingsWidth) / 2,
            (screenHeight - kSettingsHeight) / 2,
            (screenWidth + kSettingsWidth) / 2,
            (screenHeight + kSettingsHeight) / 2);

    sSettingsWindow = NewCWindow(nil, &windowRect,
                                "\pSettings",
                                true, documentProc,
                                (WindowPtr)-1, true, 0);
    if (sSettingsWindow == nil)
        return;

    SetPort(sSettingsWindow);

    /* Calculate tab rects */
    {
        short tabWidth;
        tabWidth = kSettingsWidth / kNumTabs;
        SetRect(&sTabRects[0], 0, 0, tabWidth, kTabHeight);
        SetRect(&sTabRects[1], tabWidth, 0, tabWidth * 2, kTabHeight);
        SetRect(&sTabRects[2], tabWidth * 2, 0, kSettingsWidth, kTabHeight);
    }

    /* Content area */
    SetRect(&sContentRect, 0, kTabHeight, kSettingsWidth, kSettingsHeight - 40);

    /* Buttons */
    SetRect(&sOKRect, kSettingsWidth - 80, kSettingsHeight - 35, kSettingsWidth - 10, kSettingsHeight - 10);
    SetRect(&sCancelRect, kSettingsWidth - 165, kSettingsHeight - 35, kSettingsWidth - 95, kSettingsHeight - 10);

    sCurrentTab = kTabSpeech;

    /* Build popup menus for voice and rate */
    BuildPopupMenus();

    DrawSettingsContent();

    /* Event loop */
    while (!done)
    {
        if (GetNextEvent(everyEvent, &event))
        {
            switch (event.what)
            {
                case mouseDown:
                {
                    WindowPtr clickWindow;
                    short part;
                    part = FindWindow(event.where, &clickWindow);

                    if (clickWindow == sSettingsWindow)
                    {
                        switch (part)
                        {
                            case inDrag:
                                DragWindow(sSettingsWindow, event.where, &gApp.qd.screenBits.bounds);
                                break;

                            case inGoAway:
                                if (TrackGoAway(sSettingsWindow, event.where))
                                    done = true;
                                break;

                            case inContent:
                                SetPort(sSettingsWindow);
                                localPt = event.where;
                                GlobalToLocal(&localPt);

                                /* Tab clicks */
                                HandleTabClick(localPt);

                                /* Speech tab controls */
                                if (sCurrentTab == kTabSpeech)
                                    HandleSpeechClick(localPt);

                                /* Model tab controls */
                                if (sCurrentTab == kTabModel)
                                    HandleModelClick(localPt);

                                /* OK button */
                                if (PtInRect(localPt, &sOKRect))
                                {
                                    DrawButton(&sOKRect, "\pOK", true);
                                    while (StillDown()) { /* wait */ }
                                    done = true;
                                    Speech_ApplySettings();
                                }

                                /* Cancel button */
                                if (PtInRect(localPt, &sCancelRect))
                                {
                                    DrawButton(&sCancelRect, "\pCancel", true);
                                    while (StillDown()) { /* wait */ }
                                    done = true;
                                }
                                break;
                        }
                    }
                    break;
                }

                case keyDown:
                {
                    char key;
                    key = event.message & charCodeMask;
                    if (key == '\r' || key == '\n')
                    {
                        done = true;
                        Speech_ApplySettings();
                    }
                    else if (key == 0x1B)  /* Escape */
                    {
                        done = true;
                    }
                    break;
                }

                case updateEvt:
                    if ((WindowPtr)event.message == sSettingsWindow)
                    {
                        BeginUpdate(sSettingsWindow);
                        DrawSettingsContent();
                        EndUpdate(sSettingsWindow);
                    }
                    break;
            }
        }
        SystemTask();
    }

    CleanupPopupMenus();
    DisposeWindow(sSettingsWindow);
    sSettingsWindow = nil;
}

/*------------------------------------------------------------------------------
    DrawSettingsContent
------------------------------------------------------------------------------*/
static void DrawSettingsContent(void)
{
    if (sSettingsWindow == nil)
        return;

    SetPort(sSettingsWindow);
    EraseRect(&sSettingsWindow->portRect);

    DrawTabBar();

    switch (sCurrentTab)
    {
        case kTabSpeech:
            DrawSpeechTab();
            break;
        case kTabModel:
            DrawModelTab();
            break;
        case kTabAbout:
            DrawAboutTab();
            break;
    }

    DrawButton(&sOKRect, "\pOK", false);
    DrawButton(&sCancelRect, "\pCancel", false);
}

/*------------------------------------------------------------------------------
    DrawTabBar
------------------------------------------------------------------------------*/
static void DrawTabBar(void)
{
    short i;
    short textWidth;
    ConstStr255Param tabNames[] = { "\pSpeech", "\pModel", "\pAbout" };

    for (i = 0; i < kNumTabs; i++)
    {
        if (i == (short)sCurrentTab)
        {
            /* Selected tab - white background, no bottom line */
            EraseRect(&sTabRects[i]);
            FrameRect(&sTabRects[i]);
            /* Erase bottom line to connect with content area */
            MoveTo(sTabRects[i].left + 1, sTabRects[i].bottom - 1);
            PenMode(patBic);
            LineTo(sTabRects[i].right - 2, sTabRects[i].bottom - 1);
            PenMode(patCopy);
        }
        else
        {
            /* Unselected tab - white background with bottom line */
            EraseRect(&sTabRects[i]);
            FrameRect(&sTabRects[i]);
        }

        /* Draw tab label */
        TextFont(3);
        TextSize(10);
        {
            short face;
            face = (i == (short)sCurrentTab) ? bold : normal;
            TextFace(face);
        }
        textWidth = StringWidth(tabNames[i]);
        MoveTo(sTabRects[i].left + (sTabRects[i].right - sTabRects[i].left - textWidth) / 2,
               sTabRects[i].top + 16);
        DrawString(tabNames[i]);
    }
    TextFace(normal);
}

/*------------------------------------------------------------------------------
    DrawSpeechTab
------------------------------------------------------------------------------*/
static void DrawSpeechTab(void)
{
    short y;

    y = sContentRect.top + 20;

    SetPort(sSettingsWindow);
    TextFont(3);
    TextSize(10);
    TextFace(normal);

    /* Enable Speech checkbox */
    SetRect(&sCheckEnableSpeech, 10, y - 12, 260, y + 4);
    MoveTo(20, y);
    if (gApp.settings.speech.speechEnabled)
        DrawString("\p[x] Enable Speech");
    else
        DrawString("\p[ ] Enable Speech");
    y += 22;

    /* Auto-speak checkbox */
    SetRect(&sCheckAutoSpeak, 10, y - 12, 260, y + 4);
    MoveTo(20, y);
    if (gApp.settings.speech.autoSpeak)
        DrawString("\p[x] Auto-speak responses");
    else
        DrawString("\p[ ] Auto-speak responses");
    y += 22;

    /* Feedback sounds checkbox */
    SetRect(&sCheckFeedback, 10, y - 12, 260, y + 4);
    MoveTo(20, y);
    if (gApp.settings.speech.feedbackSounds)
        DrawString("\p[x] Feedback sounds");
    else
        DrawString("\p[ ] Feedback sounds");
    y += 22;

    /* Stream speech checkbox */
    SetRect(&sCheckStreamSpeech, 10, y - 12, 320, y + 4);
    MoveTo(20, y);
    if (gApp.settings.speech.streamSpeech)
        DrawString("\p[x] Speak during generation");
    else
        DrawString("\p[ ] Speak during generation");
    y += 30;

    /* Voice popup */
    if (Speech_IsAvailable())
    {
        Str255 voiceName;
        short currentVoice;
        short rateIdx;

        MoveTo(20, y + 14);
        DrawString("\pVoice:");

        SetRect(&sVoicePopupRect, 80, y, 320, y + 20);
        currentVoice = Speech_GetCurrentVoiceIndex();
        if (currentVoice < 1)
            currentVoice = 1;
        Speech_GetVoiceName(currentVoice, voiceName);
        DrawPopup(&sVoicePopupRect, voiceName);
        y += 30;

        /* Rate popup */
        MoveTo(20, y + 14);
        DrawString("\pSpeed:");

        SetRect(&sRatePopupRect, 80, y, 200, y + 20);
        rateIdx = RatePercentToIndex(gApp.settings.speech.ratePercent);
        DrawPopup(&sRatePopupRect, sRateLabels[rateIdx - 1]);
    }
    else
    {
        MoveTo(20, y);
        DrawString("\pSpeech Manager not available on this system.");
        SetRect(&sVoicePopupRect, 0, 0, 0, 0);
        SetRect(&sRatePopupRect, 0, 0, 0, 0);
    }
}

/*------------------------------------------------------------------------------
    DrawModelTab - Model file management
------------------------------------------------------------------------------*/
static void DrawModelTab(void)
{
    short y;
    char infoLine[256];
    Str255 pInfo;
    short lineLen;

    y = sContentRect.top + 25;

    SetPort(sSettingsWindow);

    /* Section title */
    TextFont(3);
    TextSize(12);
    TextFace(bold);
    MoveTo(20, y);
    DrawString("\pModel File");
    y += 20;

    TextFace(normal);
    TextSize(10);

    /* Current model status */
    if (gApp.model.modelFileValid)
    {
        sprintf(infoLine, "Loaded: %s", gApp.settings.modelFileName);
        lineLen = strlen(infoLine);
        pInfo[0] = lineLen;
        BlockMoveData(infoLine, &pInfo[1], lineLen);
        MoveTo(20, y);
        DrawString(pInfo);
        y += 16;

        /* Arena info */
        sprintf(infoLine, "Arena: %ldKB allocated",
                gApp.model.arenaSize / 1024L);
        lineLen = strlen(infoLine);
        pInfo[0] = lineLen;
        BlockMoveData(infoLine, &pInfo[1], lineLen);
        MoveTo(20, y);
        DrawString(pInfo);
        y += 16;

        if (gApp.model.modelLoaded)
        {
            sprintf(infoLine, "Status: Ready (%d layers RAM, %d disk)",
                    gApp.model.layersInRAM, gApp.model.layersOnDisk);
        }
        else
        {
            sprintf(infoLine, "Status: File valid, engine not loaded");
        }
        lineLen = strlen(infoLine);
        pInfo[0] = lineLen;
        BlockMoveData(infoLine, &pInfo[1], lineLen);
        MoveTo(20, y);
        DrawString(pInfo);
        y += 24;
    }
    else
    {
        MoveTo(20, y);
        DrawString("\pNo model file loaded.");
        y += 16;
        MoveTo(20, y);
        DrawString("\pPlace macinai_model.bin in the app folder,");
        y += 14;
        MoveTo(20, y);
        DrawString("\por click Load Model below.");
        y += 24;
    }

    /* Load Model button */
    SetRect(&sLoadModelBtnRect, 20, y, 140, y + 22);
    DrawButton(&sLoadModelBtnRect, "\pLoad Model\xC9", false);  /* ellipsis */

    y += 32;

    /* Auto-detect note */
    TextSize(9);
    ForeColor(blueColor);
    MoveTo(20, y);
    DrawString("\pMacinAI auto-detects model files in the app folder.");
    ForeColor(blackColor);
    TextSize(10);

    y += 20;

    /* Debug logging checkbox */
    SetRect(&sCheckDebugLog, 10, y - 12, 260, y + 4);
    MoveTo(20, y);
    if (gApp.settings.debugLogging)
        DrawString("\p[x] Debug Logging");
    else
        DrawString("\p[ ] Debug Logging");
}

/*------------------------------------------------------------------------------
    HandleModelClick - Handle clicks on Model tab
------------------------------------------------------------------------------*/
static void HandleModelClick(Point localPt)
{
    if (PtInRect(localPt, &sLoadModelBtnRect))
    {
        DrawButton(&sLoadModelBtnRect, "\pLoad Model\xC9", true);
        while (StillDown()) { /* wait */ }
        HandleOpenModel();
        DrawSettingsContent();
        return;
    }

    /* Debug logging toggle */
    if (PtInRect(localPt, &sCheckDebugLog))
    {
        gApp.settings.debugLogging = !gApp.settings.debugLogging;
        DebugLog_SetEnabled(gApp.settings.debugLogging);
        DrawSettingsContent();
        return;
    }
}

/*------------------------------------------------------------------------------
    DrawAboutTab
------------------------------------------------------------------------------*/
static void DrawAboutTab(void)
{
    short y;
    char infoLine[256];
    Str255 pInfo;
    short lineLen;
    const char *versionStr;

    y = sContentRect.top + 20;

    SetPort(sSettingsWindow);

    /* Version */
    TextFont(3);
    TextSize(12);
    TextFace(bold);

    versionStr = GetFullVersionString();
    lineLen = strlen(versionStr);
    if (lineLen > 255) lineLen = 255;
    pInfo[0] = lineLen;
    BlockMoveData(versionStr, &pInfo[1], lineLen);
    MoveTo(20, y);
    DrawString(pInfo);
    y += 20;

    TextFace(normal);
    TextSize(10);

    /* Hardware info */
    sprintf(infoLine, "Machine: %s", gApp.hardware.machineName);
    lineLen = strlen(infoLine);
    pInfo[0] = lineLen;
    BlockMoveData(infoLine, &pInfo[1], lineLen);
    MoveTo(20, y);
    DrawString(pInfo);
    y += 16;

    if (gApp.hardware.cpuType >= 0x0100)
        sprintf(infoLine, "CPU: PowerPC, FPU: Built-in");
    else
        sprintf(infoLine, "CPU: %s, FPU: %s",
                gApp.hardware.cpuType >= 4 ? "68040" : "68030",
                gApp.hardware.hasFPU ? "Yes" : "No");
    lineLen = strlen(infoLine);
    pInfo[0] = lineLen;
    BlockMoveData(infoLine, &pInfo[1], lineLen);
    MoveTo(20, y);
    DrawString(pInfo);
    y += 16;

    sprintf(infoLine, "RAM: %ldMB total, %ldMB available",
            gApp.hardware.physicalRAM / (1024L * 1024L),
            gApp.hardware.availableRAM / (1024L * 1024L));
    lineLen = strlen(infoLine);
    pInfo[0] = lineLen;
    BlockMoveData(infoLine, &pInfo[1], lineLen);
    MoveTo(20, y);
    DrawString(pInfo);
    y += 16;

    sprintf(infoLine, "System: %s", gApp.hardware.systemVersion);
    lineLen = strlen(infoLine);
    pInfo[0] = lineLen;
    BlockMoveData(infoLine, &pInfo[1], lineLen);
    MoveTo(20, y);
    DrawString(pInfo);
    y += 20;

    /* Model status */
    {
        char modelStatus[128];
        Engine_GetStatusString(modelStatus, sizeof(modelStatus));
        lineLen = strlen(modelStatus);
        pInfo[0] = lineLen;
        BlockMoveData(modelStatus, &pInfo[1], lineLen);
        MoveTo(20, y);
        DrawString(pInfo);
    }
}

/*------------------------------------------------------------------------------
    HandleTabClick
------------------------------------------------------------------------------*/
static void HandleTabClick(Point localPt)
{
    short i;

    for (i = 0; i < kNumTabs; i++)
    {
        if (PtInRect(localPt, &sTabRects[i]))
        {
            if (i != (short)sCurrentTab)
            {
                sCurrentTab = (SettingsTab)i;
                DrawSettingsContent();
            }
            return;
        }
    }
}

/*------------------------------------------------------------------------------
    HandleSpeechClick - Toggle checkboxes and show popup menus
------------------------------------------------------------------------------*/
static void HandleSpeechClick(Point localPt)
{
    /* Checkbox toggles */
    if (PtInRect(localPt, &sCheckEnableSpeech))
    {
        gApp.settings.speech.speechEnabled = !gApp.settings.speech.speechEnabled;
        DrawSettingsContent();
        return;
    }
    if (PtInRect(localPt, &sCheckAutoSpeak))
    {
        gApp.settings.speech.autoSpeak = !gApp.settings.speech.autoSpeak;
        DrawSettingsContent();
        return;
    }
    if (PtInRect(localPt, &sCheckFeedback))
    {
        gApp.settings.speech.feedbackSounds = !gApp.settings.speech.feedbackSounds;
        DrawSettingsContent();
        return;
    }
    if (PtInRect(localPt, &sCheckStreamSpeech))
    {
        gApp.settings.speech.streamSpeech = !gApp.settings.speech.streamSpeech;
        DrawSettingsContent();
        return;
    }

    /* Voice popup */
    if (PtInRect(localPt, &sVoicePopupRect) && sVoiceMenu != nil)
    {
        Point menuPt;
        long menuResult;
        short currentVoice;

        currentVoice = Speech_GetCurrentVoiceIndex();
        if (currentVoice < 1) currentVoice = 1;

        menuPt.h = sVoicePopupRect.left;
        menuPt.v = sVoicePopupRect.top;
        LocalToGlobal(&menuPt);

        menuResult = PopUpMenuSelect(sVoiceMenu, menuPt.v, menuPt.h, currentVoice);
        if (HiWord(menuResult) == 201)
        {
            Speech_SetVoice(LoWord(menuResult));
            SetPort(sSettingsWindow);
            DrawSettingsContent();
        }
        return;
    }

    /* Rate popup */
    if (PtInRect(localPt, &sRatePopupRect) && sRateMenu != nil)
    {
        Point menuPt;
        long menuResult;
        short rateIdx;

        rateIdx = RatePercentToIndex(gApp.settings.speech.ratePercent);

        menuPt.h = sRatePopupRect.left;
        menuPt.v = sRatePopupRect.top;
        LocalToGlobal(&menuPt);

        menuResult = PopUpMenuSelect(sRateMenu, menuPt.v, menuPt.h, rateIdx);
        if (HiWord(menuResult) == 202)
        {
            short selectedIdx;
            selectedIdx = LoWord(menuResult);
            if (selectedIdx >= 1 && selectedIdx <= 7)
            {
                Speech_SetRatePercent(sRatePercents[selectedIdx - 1]);
            }
            SetPort(sSettingsWindow);
            DrawSettingsContent();
        }
        return;
    }
}
