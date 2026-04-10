/*------------------------------------------------------------------------------
    ChatWindow.c - Main Chat Window

    Provides the chat interface for MacinAI Local.
    This is a simplified version of the MacinAI relay ChatWindow:
    - Removed sidebar conversation list (single conversation mode for now)
    - Removed network status bar
    - Added model status bar (layers in RAM, FPU, arena size)
    - Kept full TextEdit input/output, toolbar, scrollbar

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

#include "ChatWindow.h"
#include "AppGlobals.h"
#include "SystemDiscovery.h"
#include "AppVersion.h"
#include "DrawingHelpers.h"
#include "ConversationManager.h"
#include "MessageHandler.h"
#include "DemoMode.h"
#include "InferenceEngine.h"
#include "SpeechManager.h"
#include "SafeString.h"
#include "SettingsDialog.h"

#include <Quickdraw.h>
#include <Fonts.h>
#include <Events.h>
#include <Windows.h>
#include <Controls.h>
#include <TextEdit.h>
#include <Scrap.h>
#include <string.h>
#include <stdio.h>

#pragma segment ChatUI

/*------------------------------------------------------------------------------
    Forward declarations
------------------------------------------------------------------------------*/
static void DrawChatContent(void);
static void DrawStatusBar(void);
static void DrawToolbar(void);
static void HandleContentClick(Point localPt, EventRecord *event);
static void ResizeChat(short width, short height);
static void UpdateScrollbar(void);
static void ScrollOutput(short delta);
static pascal void ScrollAction(ControlHandle control, short part);
static void DrawSpeakButton(void);

/*------------------------------------------------------------------------------
    Module Globals
------------------------------------------------------------------------------*/
static WindowPtr sChatWindow = nil;
static Boolean sChatOpen = false;

/* TextEdit handles (exposed via extern in header) */
TEHandle gInputTE = nil;
TEHandle gOutputTE = nil;

/* Scrollbar */
static ControlHandle sOutputScrollbar = nil;

/* Layout rectangles */
static Rect sOutputRect = {0, 0, 0, 0};
static Rect sInputRect = {0, 0, 0, 0};
static Rect sStatusRect = {0, 0, 0, 0};
static Rect sToolbarRect = {0, 0, 0, 0};
static Rect sSendRect = {0, 0, 0, 0};
static Rect sNewChatRect = {0, 0, 0, 0};
static Rect sSettingsIconRect = {0, 0, 0, 0};

/* Speak/Stop button */
static Boolean sSpeakPressed = false;
static Boolean sIsSpeaking = false;
static Rect sSpeakRect = {0, 0, 0, 0};

/* Cursor blink timing */
static long sLastBlink = 0;

extern AppGlobals gApp;

/*------------------------------------------------------------------------------
    Layout Constants
------------------------------------------------------------------------------*/
#define kToolbarHeight      32
#define kInputHeight        60
#define kSendButtonWidth    60
#define kSendButtonGap      6
#define kStatusBarHeight    20
#define kScrollbarWidth     16
#define kMargin             4

/*------------------------------------------------------------------------------
    ChatScreen_Show
------------------------------------------------------------------------------*/
void ChatScreen_Show(void)
{
    Rect windowRect, destRect, viewRect;
    short screenWidth, screenHeight;
    short winWidth, winHeight;

    if (sChatOpen)
        return;

    /* Calculate window position (centered, ~80% of screen) */
    screenWidth = gApp.qd.screenBits.bounds.right;
    screenHeight = gApp.qd.screenBits.bounds.bottom;

    winWidth = screenWidth - 40;
    winHeight = screenHeight - 60;
    if (winWidth > 512) winWidth = 512;
    if (winHeight > 380) winHeight = 380;

    SetRect(&windowRect,
            (screenWidth - winWidth) / 2,
            (screenHeight - winHeight) / 2 + 10,
            (screenWidth + winWidth) / 2,
            (screenHeight + winHeight) / 2 + 10);

    /* Create chat window */
    sChatWindow = NewCWindow(nil, &windowRect,
                            "\pMacinAI Local",
                            true, documentProc,
                            (WindowPtr)-1, true, 0);

    if (sChatWindow == nil)
        return;

    SetPort(sChatWindow);
    gApp.currentWindow = sChatWindow;

    /* Calculate layout rectangles */
    winWidth = sChatWindow->portRect.right - sChatWindow->portRect.left;
    winHeight = sChatWindow->portRect.bottom - sChatWindow->portRect.top;

    /* Status bar at bottom */
    SetRect(&sStatusRect, 0, winHeight - kStatusBarHeight, winWidth, winHeight);

    /* Toolbar at top */
    SetRect(&sToolbarRect, 0, 0, winWidth, kToolbarHeight);

    /* Input area above status bar */
    SetRect(&sInputRect, kMargin,
            winHeight - kStatusBarHeight - kInputHeight - kMargin,
            winWidth - kMargin - kSendButtonWidth - kSendButtonGap,
            winHeight - kStatusBarHeight - kMargin);

    /* Output area between toolbar and input */
    SetRect(&sOutputRect, kMargin,
            kToolbarHeight + kMargin,
            winWidth - kScrollbarWidth - kMargin,
            sInputRect.top - kMargin);

    /* Send button */
    SetRect(&sSendRect,
            winWidth - kMargin - kSendButtonWidth,
            sInputRect.top + (kInputHeight - 24) / 2,
            winWidth - kMargin,
            sInputRect.top + (kInputHeight - 24) / 2 + 24);

    /* New Chat button */
    SetRect(&sNewChatRect, kMargin + 2, 6, kMargin + 80, 26);

    /* Settings icon (top right) */
    SetRect(&sSettingsIconRect, winWidth - 42, 4, winWidth - 10, 36);

    /* Create output TextEdit */
    destRect = sOutputRect;
    InsetRect(&destRect, 2, 2);
    destRect.bottom = kTextEditDestHeight;  /* Tall dest rect for scrolling */
    viewRect = sOutputRect;
    InsetRect(&viewRect, 2, 2);

    gOutputTE = TENew(&destRect, &viewRect);
    if (gOutputTE != nil)
    {
        TEAutoView(true, gOutputTE);
        TEActivate(gOutputTE);
    }

    /* Create input TextEdit */
    destRect = sInputRect;
    InsetRect(&destRect, 2, 2);
    viewRect = sInputRect;
    InsetRect(&viewRect, 2, 2);

    gInputTE = TENew(&destRect, &viewRect);
    if (gInputTE != nil)
    {
        TEAutoView(true, gInputTE);
        TEActivate(gInputTE);
    }

    /* Create scrollbar */
    {
        Rect scrollRect;
        SetRect(&scrollRect,
                winWidth - kScrollbarWidth,
                kToolbarHeight,
                winWidth,
                sInputRect.top - kMargin + 1);
        sOutputScrollbar = NewControl(sChatWindow, &scrollRect,
                                     "\p", true, 0, 0, 0,
                                     scrollBarProc, 0);
    }

    sChatOpen = true;

    /* Welcome message */
    {
        char welcome[512];
        char hwSummary[128];

        SystemDiscovery_GetSummary(hwSummary, sizeof(hwSummary));
        if (gApp.model.modelFileValid)
        {
            /* Show model name (filename without .bin extension) */
            char modelName[64];
            short nameLen;
            SafeStringCopy(modelName, gApp.settings.modelFileName, 64);
            nameLen = (short)strlen(modelName);
            if (nameLen > 4 && modelName[nameLen-4] == '.' &&
                (modelName[nameLen-3] == 'b' || modelName[nameLen-3] == 'B') &&
                (modelName[nameLen-2] == 'i' || modelName[nameLen-2] == 'I') &&
                (modelName[nameLen-1] == 'n' || modelName[nameLen-1] == 'N'))
            {
                modelName[nameLen-4] = '\0';
            }
            sprintf(welcome, "MacinAI: Welcome to MacinAI Local!\r%s\rModel: %s\r\r",
                    hwSummary, modelName);
        }
        else
            sprintf(welcome, "MacinAI: Welcome to MacinAI Local!\r%s\rNo model found. Go to Settings > Model to load a MacinAI model.\r\r", hwSummary);

        if (gOutputTE != nil)
        {
            TESetSelect(0, 0, gOutputTE);
            TEInsert(welcome, strlen(welcome), gOutputTE);
        }
    }

    /* Draw initial content */
    DrawChatContent();

    /* Initial status */
    Engine_GetStatusString(gApp.statusText, sizeof(gApp.statusText));
}

/*------------------------------------------------------------------------------
    DrawChatContent
------------------------------------------------------------------------------*/
static void DrawChatContent(void)
{
    if (sChatWindow == nil)
        return;

    SetPort(sChatWindow);
    EraseRect(&sChatWindow->portRect);

    /* Draw toolbar */
    DrawToolbar();

    /* Draw output area frame */
    FrameRect(&sOutputRect);
    if (gOutputTE != nil)
        TEUpdate(&sOutputRect, gOutputTE);

    /* Draw input area frame */
    FrameRect(&sInputRect);
    if (gInputTE != nil)
        TEUpdate(&sInputRect, gInputTE);

    /* Draw Send button */
    DrawButton(&sSendRect, "\pSend", false);

    /* Draw scrollbar */
    if (sOutputScrollbar != nil)
        Draw1Control(sOutputScrollbar);

    /* Draw status bar */
    DrawStatusBar();
}

/*------------------------------------------------------------------------------
    DrawToolbar
------------------------------------------------------------------------------*/
static void DrawToolbar(void)
{
    if (sChatWindow == nil)
        return;

    SetPort(sChatWindow);

    /* Background */
    EraseRect(&sToolbarRect);

    /* New Chat button */
    DrawButton(&sNewChatRect, "\pNew Chat", false);

    /* Settings icon (top right) - three dots */
    {
        short winWidth;
        short cx, cy;

        winWidth = sChatWindow->portRect.right - sChatWindow->portRect.left;
        SetRect(&sSettingsIconRect, winWidth - kScrollbarWidth - 28, 6, winWidth - kScrollbarWidth - 4, 26);

        /* Draw button frame matching New Chat style */
        DrawButton(&sSettingsIconRect, "\p", false);

        /* Draw three horizontal lines (hamburger/list icon) */
        cx = (sSettingsIconRect.left + sSettingsIconRect.right) / 2;
        cy = (sSettingsIconRect.top + sSettingsIconRect.bottom) / 2;

        PenSize(1, 1);
        MoveTo(cx - 6, cy - 4); LineTo(cx + 6, cy - 4);
        MoveTo(cx - 6, cy);     LineTo(cx + 6, cy);
        MoveTo(cx - 6, cy + 4); LineTo(cx + 6, cy + 4);
    }

    /* Separator line */
    MoveTo(0, kToolbarHeight - 1);
    LineTo(sChatWindow->portRect.right, kToolbarHeight - 1);
}

/*------------------------------------------------------------------------------
    DrawSpeakButton - Speak/Stop in status bar (right side)
------------------------------------------------------------------------------*/
static void DrawSpeakButton(void)
{
    Str255 buttonLabel;
    short textW, btnW;

    if (!Speech_IsAvailable() || !gApp.settings.speech.speechEnabled)
        return;

    /* Button rect on right side of status bar (before grow box area) */
    SetRect(&sSpeakRect, sStatusRect.right - 70, sStatusRect.top + 2,
            sStatusRect.right - 18, sStatusRect.bottom - 2);

    /* Draw button background */
    if (sSpeakPressed)
    {
        ForeColor(blackColor);
        PaintRect(&sSpeakRect);
        ForeColor(whiteColor);
    }
    else
    {
        EraseRect(&sSpeakRect);
        ForeColor(blackColor);
        FrameRect(&sSpeakRect);
    }

    /* Set label based on speaking state */
    if (sIsSpeaking || Speech_IsBusy())
    {
        BlockMove("\pStop", buttonLabel, 5);
        sIsSpeaking = true;
    }
    else
    {
        BlockMove("\pSpeak", buttonLabel, 6);
        sIsSpeaking = false;
    }

    /* Draw label centered */
    TextFont(1);
    TextSize(9);
    TextFace(normal);
    textW = StringWidth(buttonLabel);
    btnW = sSpeakRect.right - sSpeakRect.left;
    MoveTo(sSpeakRect.left + (btnW - textW) / 2, sSpeakRect.bottom - 4);
    DrawString(buttonLabel);

    /* Restore color */
    ForeColor(blackColor);
}

/*------------------------------------------------------------------------------
    DrawStatusBar - Model status instead of connection status
------------------------------------------------------------------------------*/
static void DrawStatusBar(void)
{
    Str255 pStatus;
    short sLen;

    if (sChatWindow == nil)
        return;

    SetPort(sChatWindow);

    /* Draw status bar background */
    EraseRect(&sStatusRect);
    MoveTo(sStatusRect.left, sStatusRect.top);
    LineTo(sStatusRect.right, sStatusRect.top);

    /* Draw status text */
    TextFont(4);  /* Monaco */
    TextSize(9);
    TextFace(normal);

    /* Get status from engine */
    if (gApp.statusText[0] == '\0')
        Engine_GetStatusString(gApp.statusText, sizeof(gApp.statusText));

    sLen = strlen(gApp.statusText);
    if (sLen > 255) sLen = 255;
    pStatus[0] = sLen;
    BlockMoveData(gApp.statusText, &pStatus[1], sLen);

    MoveTo(sStatusRect.left + 6, sStatusRect.bottom - 5);
    DrawString(pStatus);

    /* Token counter (right-aligned, before speak button) */
    if (gApp.model.totalTokensIn > 0 || gApp.model.totalTokensOut > 0)
    {
        char tokenStr[64];
        Str255 pTokenStr;
        short tokenLen;
        short tokenWidth;
        short rightEdge;

        sprintf(tokenStr, "In:%ld Out:%ld",
                gApp.model.totalTokensIn, gApp.model.totalTokensOut);
        tokenLen = strlen(tokenStr);
        if (tokenLen > 255) tokenLen = 255;
        pTokenStr[0] = tokenLen;
        BlockMoveData(tokenStr, &pTokenStr[1], tokenLen);
        tokenWidth = StringWidth(pTokenStr);

        /* Position: right side, leaving room for speak button */
        rightEdge = sStatusRect.right - 76;
        if (Speech_IsAvailable() && gApp.settings.speech.speechEnabled)
            rightEdge = sSpeakRect.left - 8;
        MoveTo(rightEdge - tokenWidth, sStatusRect.bottom - 5);
        DrawString(pTokenStr);
    }

    /* Speak/Stop button on right */
    DrawSpeakButton();
}

/*------------------------------------------------------------------------------
    HandleContentClick
------------------------------------------------------------------------------*/
static void HandleContentClick(Point localPt, EventRecord *event)
{
    Boolean shiftDown;

    shiftDown = (event->modifiers & shiftKey) != 0;

    /* Settings icon (top right) */
    if (PtInRect(localPt, &sSettingsIconRect))
    {
        while (StillDown()) { /* wait */ }
        SettingsDialog_Show();
        DrawChatContent();
        return;
    }

    /* Speak/Stop button in status bar */
    if (PtInRect(localPt, &sSpeakRect) && Speech_IsAvailable()
        && gApp.settings.speech.speechEnabled)
    {
        Rect statusArea;
        statusArea = sStatusRect;

        /* Visual feedback */
        sSpeakPressed = true;
        DrawSpeakButton();
        while (StillDown()) { /* wait */ }
        sSpeakPressed = false;

        /* Check still in button */
        {
            Point checkPt;
            GetMouse(&checkPt);
            if (PtInRect(checkPt, &sSpeakRect))
            {
                if (sIsSpeaking || Speech_IsBusy())
                {
                    Speech_Stop();
                    sIsSpeaking = false;
                }
                else
                {
                    /* Speak output TE content */
                    if (gOutputTE != nil)
                    {
                        Handle hText;
                        long textLen;
                        HLock((Handle)gOutputTE);
                        hText = (*gOutputTE)->hText;
                        textLen = (*gOutputTE)->teLength;
                        HUnlock((Handle)gOutputTE);
                        if (textLen > 0)
                        {
                            HLock(hText);
                            Speech_SpeakText(*hText, textLen);
                            HUnlock(hText);
                            sIsSpeaking = true;
                        }
                    }
                }
            }
        }
        DrawSpeakButton();
        return;
    }

    /* Send button */
    if (PtInRect(localPt, &sSendRect))
    {
        DrawButton(&sSendRect, "\pSend", true);
        while (StillDown()) { /* wait */ }
        DrawButton(&sSendRect, "\pSend", false);
        Message_Send();
        return;
    }

    /* New Chat button */
    if (PtInRect(localPt, &sNewChatRect))
    {
        DrawButton(&sNewChatRect, "\pNew Chat", true);
        while (StillDown()) { /* wait */ }
        DrawButton(&sNewChatRect, "\pNew Chat", false);
        ConvMgr_CreateNew(gOutputTE);
        ChatScreen_UpdateTitle();
        return;
    }

    /* Input TextEdit */
    if (PtInRect(localPt, &sInputRect) && gInputTE != nil)
    {
        TEClick(localPt, shiftDown, gInputTE);
        return;
    }

    /* Output TextEdit */
    if (PtInRect(localPt, &sOutputRect) && gOutputTE != nil)
    {
        TEClick(localPt, shiftDown, gOutputTE);
        return;
    }

    /* Scrollbar */
    if (sOutputScrollbar != nil)
    {
        ControlHandle ctrl;
        short part;

        part = FindControl(localPt, sChatWindow, &ctrl);
        if (part != 0 && ctrl == sOutputScrollbar)
        {
            if (part == kControlIndicatorPart)
            {
                TrackControl(sOutputScrollbar, localPt, nil);
                ScrollOutput(GetControlValue(sOutputScrollbar));
            }
            else
            {
                TrackControl(sOutputScrollbar, localPt,
                            NewControlActionProc(ScrollAction));
            }
        }
    }
}

/*------------------------------------------------------------------------------
    ScrollOutput
------------------------------------------------------------------------------*/
static void ScrollOutput(short newValue)
{
    short lineHeight;
    short scrollPixels;
    short currentOffset;
    short delta;
    GrafPtr savePort;
    Rect viewRect;

    if (gOutputTE == nil || sChatWindow == nil)
        return;

    GetPort(&savePort);
    SetPort(sChatWindow);

    /* Calculate current scroll offset from destRect vs viewRect */
    lineHeight = (*gOutputTE)->lineHeight;
    if (lineHeight < 1) lineHeight = 12;

    /* scrollPixels = how far up to shift destRect.top relative to viewRect.top */
    scrollPixels = newValue * lineHeight;
    viewRect = (*gOutputTE)->viewRect;
    currentOffset = viewRect.top - (*gOutputTE)->destRect.top;
    delta = scrollPixels - currentOffset;

    if (delta != 0)
    {
        TEScroll(0, -delta, gOutputTE);
    }

    SetPort(savePort);
}

/*------------------------------------------------------------------------------
    ScrollAction - Scrollbar action proc
------------------------------------------------------------------------------*/
static pascal void ScrollAction(ControlHandle control, short part)
{
    short delta = 0;

    switch (part)
    {
        case kControlUpButtonPart:    delta = -12; break;
        case kControlDownButtonPart:  delta = 12;  break;
        case kControlPageUpPart:      delta = -(sOutputRect.bottom - sOutputRect.top - 24); break;
        case kControlPageDownPart:    delta = (sOutputRect.bottom - sOutputRect.top - 24); break;
    }

    if (delta != 0)
    {
        short newVal = GetControlValue(control) + delta;
        short maxVal = GetControlMaximum(control);

        if (newVal < 0) newVal = 0;
        if (newVal > maxVal) newVal = maxVal;
        SetControlValue(control, newVal);
        ScrollOutput(newVal);
    }
}

/*------------------------------------------------------------------------------
    UpdateScrollbar
------------------------------------------------------------------------------*/
static void UpdateScrollbar(void)
{
    short lineHeight;
    short totalLines;
    short visibleLines;
    short maxScroll;
    short viewHeight;

    if (gOutputTE == nil || sOutputScrollbar == nil)
        return;

    lineHeight = (*gOutputTE)->lineHeight;
    if (lineHeight < 1) lineHeight = 12;

    totalLines = (*gOutputTE)->nLines;
    viewHeight = sOutputRect.bottom - sOutputRect.top - 4;
    visibleLines = viewHeight / lineHeight;

    if (totalLines > visibleLines)
        maxScroll = totalLines - visibleLines;
    else
        maxScroll = 0;

    SetControlMaximum(sOutputScrollbar, maxScroll);

    /* Auto-scroll to bottom when new text is added */
    if (maxScroll > 0)
    {
        SetControlValue(sOutputScrollbar, maxScroll);
        ScrollOutput(maxScroll);
    }
}

/*------------------------------------------------------------------------------
    ChatScreen_HandleEvent
------------------------------------------------------------------------------*/
Boolean ChatScreen_HandleEvent(EventRecord *event)
{
    if (sChatWindow == nil || !sChatOpen)
        return false;

    switch (event->what)
    {
        case mouseDown:
        {
            WindowPtr clickWindow;
            short part = FindWindow(event->where, &clickWindow);

            if (clickWindow == sChatWindow)
            {
                switch (part)
                {
                    case inDrag:
                        DragWindow(sChatWindow, event->where, &gApp.qd.screenBits.bounds);
                        return true;

                    case inGoAway:
                        if (TrackGoAway(sChatWindow, event->where))
                        {
                            gApp.done = true;
                        }
                        return true;

                    case inGrow:
                    {
                        Rect limitRect;
                        long growResult;

                        SetRect(&limitRect, 300, 200, 32767, 32767);
                        growResult = GrowWindow(sChatWindow, event->where, &limitRect);
                        if (growResult != 0)
                        {
                            SizeWindow(sChatWindow, LoWord(growResult), HiWord(growResult), true);
                            ResizeChat(LoWord(growResult), HiWord(growResult));
                        }
                        return true;
                    }

                    case inContent:
                    {
                        Point localPt;
                        SetPort(sChatWindow);
                        localPt = event->where;
                        GlobalToLocal(&localPt);
                        HandleContentClick(localPt, event);
                        return true;
                    }
                }
            }
            break;
        }

        case keyDown:
        case autoKey:
        {
            char key = event->message & charCodeMask;

            /* Command key combinations handled by menu */
            if (event->modifiers & cmdKey)
                return false;

            /* Enter/Return sends message */
            if (key == '\r' || key == '\n')
            {
                if (event->modifiers & shiftKey)
                {
                    /* Shift+Return inserts newline */
                    if (gInputTE != nil)
                        TEKey(key, gInputTE);
                }
                else
                {
                    Message_Send();
                }
                return true;
            }

            /* Other keys go to input */
            if (gInputTE != nil)
            {
                TEKey(key, gInputTE);
                ConvMgr_SetUnsavedChanges(true);
            }
            return true;
        }

        case updateEvt:
            if ((WindowPtr)event->message == sChatWindow)
            {
                BeginUpdate(sChatWindow);
                DrawChatContent();
                EndUpdate(sChatWindow);
                return true;
            }
            break;

        case activateEvt:
            if ((WindowPtr)event->message == sChatWindow)
            {
                if (event->modifiers & activeFlag)
                {
                    if (gInputTE != nil) TEActivate(gInputTE);
                }
                else
                {
                    if (gInputTE != nil) TEDeactivate(gInputTE);
                }
                return true;
            }
            break;
    }

    return false;
}

/*------------------------------------------------------------------------------
    ResizeChat
------------------------------------------------------------------------------*/
static void ResizeChat(short width, short height)
{
    Rect destRect, viewRect;

    /* Recalculate layout */
    SetRect(&sStatusRect, 0, height - kStatusBarHeight, width, height);
    SetRect(&sToolbarRect, 0, 0, width, kToolbarHeight);
    SetRect(&sInputRect, kMargin,
            height - kStatusBarHeight - kInputHeight - kMargin,
            width - kMargin - kSendButtonWidth - kSendButtonGap,
            height - kStatusBarHeight - kMargin);
    SetRect(&sOutputRect, kMargin,
            kToolbarHeight + kMargin,
            width - kScrollbarWidth - kMargin,
            sInputRect.top - kMargin);
    SetRect(&sSendRect,
            width - kMargin - kSendButtonWidth,
            sInputRect.top + (kInputHeight - 24) / 2,
            width - kMargin,
            sInputRect.top + (kInputHeight - 24) / 2 + 24);

    /* Update TextEdit view rects */
    if (gOutputTE != nil)
    {
        viewRect = sOutputRect;
        InsetRect(&viewRect, 2, 2);
        (*gOutputTE)->viewRect = viewRect;
        destRect = viewRect;
        destRect.bottom = kTextEditDestHeight;
        (*gOutputTE)->destRect = destRect;
    }

    if (gInputTE != nil)
    {
        viewRect = sInputRect;
        InsetRect(&viewRect, 2, 2);
        (*gInputTE)->viewRect = viewRect;
        (*gInputTE)->destRect = viewRect;
    }

    /* Move scrollbar */
    if (sOutputScrollbar != nil)
    {
        MoveControl(sOutputScrollbar, width - kScrollbarWidth, kToolbarHeight);
        SizeControl(sOutputScrollbar, kScrollbarWidth,
                   sInputRect.top - kMargin - kToolbarHeight + 1);
    }

    /* Redraw */
    SetPort(sChatWindow);
    InvalRect(&sChatWindow->portRect);
}

/*------------------------------------------------------------------------------
    ChatScreen_Close
------------------------------------------------------------------------------*/
void ChatScreen_Close(void)
{
    if (gOutputTE != nil)
    {
        TEDispose(gOutputTE);
        gOutputTE = nil;
    }

    if (gInputTE != nil)
    {
        TEDispose(gInputTE);
        gInputTE = nil;
    }

    if (sChatWindow != nil)
    {
        DisposeWindow(sChatWindow);
        sChatWindow = nil;
    }

    sChatOpen = false;
}

/*------------------------------------------------------------------------------
    ChatScreen_IsOpen
------------------------------------------------------------------------------*/
Boolean ChatScreen_IsOpen(void)
{
    return sChatOpen;
}

/*------------------------------------------------------------------------------
    ChatScreen_GetWindow
------------------------------------------------------------------------------*/
WindowPtr ChatScreen_GetWindow(void)
{
    return sChatWindow;
}

/*------------------------------------------------------------------------------
    ChatScreen_HandleSave
------------------------------------------------------------------------------*/
void ChatScreen_HandleSave(void)
{
    ConvMgr_SaveCurrent(gOutputTE);
}

/*------------------------------------------------------------------------------
    ChatScreen_UpdateTitle
------------------------------------------------------------------------------*/
void ChatScreen_UpdateTitle(void)
{
    char title[kMaxConversationTitle + 16];
    char convTitle[kMaxConversationTitle];
    Str255 pTitle;
    short len;

    ConvMgr_GetCurrentTitle(convTitle);
    sprintf(title, "MacinAI Local - %s", convTitle);

    len = strlen(title);
    if (len > 255) len = 255;
    pTitle[0] = len;
    BlockMoveData(title, &pTitle[1], len);

    if (sChatWindow != nil)
        SetWTitle(sChatWindow, pTitle);
}

/*------------------------------------------------------------------------------
    ChatScreen_HandleIdle
------------------------------------------------------------------------------*/
void ChatScreen_HandleIdle(void)
{
    /* Blink cursor in input TE */
    if (gInputTE != nil)
        TEIdle(gInputTE);

    /* Update status bar periodically */
    {
        char newStatus[128];
        Engine_GetStatusString(newStatus, sizeof(newStatus));
        if (strcmp(newStatus, gApp.statusText) != 0)
        {
            SafeStringCopy(gApp.statusText, newStatus, sizeof(gApp.statusText));
            if (sChatWindow != nil)
            {
                SetPort(sChatWindow);
                DrawStatusBar();
            }
        }
    }

    /* Update speak button if speech finished */
    if (sIsSpeaking && !Speech_IsBusy())
    {
        sIsSpeaking = false;
        if (sChatWindow != nil)
        {
            SetPort(sChatWindow);
            DrawSpeakButton();
        }
    }

    /* Process speech queue */
    Speech_ProcessQueue();

    /* Advance demo mode state machine */
    if (DemoMode_IsRunning())
        DemoMode_Step();
}

/*------------------------------------------------------------------------------
    Edit Menu Handlers
------------------------------------------------------------------------------*/
void ChatWindow_HandleCut(void)
{
    if (gInputTE != nil && ChatWindow_HasInputSelection())
    {
        TECut(gInputTE);
        ZeroScrap();
        TEToScrap();
    }
}

void ChatWindow_HandleCopy(void)
{
    if (gOutputTE != nil && ChatWindow_HasOutputSelection())
    {
        TECopy(gOutputTE);
        ZeroScrap();
        TEToScrap();
    }
    else if (gInputTE != nil && ChatWindow_HasInputSelection())
    {
        TECopy(gInputTE);
        ZeroScrap();
        TEToScrap();
    }
}

void ChatWindow_HandlePaste(void)
{
    if (gInputTE != nil)
    {
        TEFromScrap();
        TEPaste(gInputTE);
    }
}

void ChatWindow_HandleClear(void)
{
    if (gInputTE != nil && ChatWindow_HasInputSelection())
    {
        TEDelete(gInputTE);
    }
}

void ChatWindow_HandleSelectAll(void)
{
    if (gInputTE != nil)
    {
        TESetSelect(0, 32767, gInputTE);
    }
}

/*------------------------------------------------------------------------------
    TextEdit Accessors
------------------------------------------------------------------------------*/
TEHandle ChatWindow_GetInputTE(void)
{
    return gInputTE;
}

TEHandle ChatWindow_GetOutputTE(void)
{
    return gOutputTE;
}

Boolean ChatWindow_HasInputSelection(void)
{
    if (gInputTE == nil) return false;
    return ((*gInputTE)->selStart != (*gInputTE)->selEnd);
}

Boolean ChatWindow_HasOutputSelection(void)
{
    if (gOutputTE == nil) return false;
    return ((*gOutputTE)->selStart != (*gOutputTE)->selEnd);
}

long ChatWindow_GetInputLength(void)
{
    if (gInputTE == nil) return 0;
    return (*gInputTE)->teLength;
}

long ChatWindow_GetOutputLength(void)
{
    if (gOutputTE == nil) return 0;
    return (*gOutputTE)->teLength;
}

/*------------------------------------------------------------------------------
    ChatWindow_UpdateScrollbar - Public wrapper for scrollbar update
------------------------------------------------------------------------------*/
void ChatWindow_UpdateScrollbar(void)
{
    UpdateScrollbar();
}

/*------------------------------------------------------------------------------
    ChatWindow_UpdateStatus - Redraw the status bar
------------------------------------------------------------------------------*/
void ChatWindow_UpdateStatus(void)
{
    DrawStatusBar();
}
