/*------------------------------------------------------------------------------
    DialogUtils.c - Reusable dialog components
    Provides common dialogs used throughout the application.
    For CodeWarrior Pro 5 on System 7.5.3

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
#include "DialogUtils.h"
#include "AppGlobals.h"
#include "DrawingHelpers.h"
#include <Types.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Events.h>
#include <Windows.h>
#include <Dialogs.h>
#include <Controls.h>
#include <Icons.h>
#include <TextEdit.h>
#include <string.h>
#include <stdio.h>
/*------------------------------------------------------------------------------
    Forward declarations for all functions (required for older C compilers)
------------------------------------------------------------------------------*/
Boolean PromptForConversationTitle(char* titleOut);
short ShowSavePrompt(WindowPtr parentWindow);
Boolean PromptForInput(const char* promptText, char* valueOut);
void ShowMessageDialog(const char* message);
Boolean ShowConfirmDialog(const char* message);
short ShowMacinAIAlertWithID(short alertID, const char* title, const char* message);
short ShowMacinAIAlert(const char* title, const char* message,
                       const char* okButton, const char* cancelButton);
short ShowMacinAIAlert3(const char* title, const char* message,
                        const char* okButton, const char* otherButton,
                        const char* cancelButton);
static pascal Boolean AlertModalFilter(DialogPtr dialog, EventRecord *event, short *itemHit);
/*------------------------------------------------------------------------------
    AlertModalFilter - Modal filter for Alert dialogs
    Processes events during modal Alert() calls to prevent UI freezing.
    Calls SystemTask() and handles update events for background windows.
------------------------------------------------------------------------------*/
static pascal Boolean AlertModalFilter(DialogPtr dialog, EventRecord *event, short *itemHit)
{
    #pragma unused(dialog, itemHit)
    /* Call SystemTask to keep system responsive */
    SystemTask();
    /* Handle update events for background windows */
    if (event->what == updateEvt)
    {
        WindowPtr updateWindow = (WindowPtr)event->message;
        if (updateWindow != (WindowPtr)dialog)
        {
            /* Let background window handle its own update */
            BeginUpdate(updateWindow);
            EndUpdate(updateWindow);
        }
    }
    return false;  /* Let dialog handle the event normally */
}
#pragma segment ChatDialog
/* Prompt user for conversation title using TextEdit in a simple dialog */
Boolean PromptForConversationTitle(char* titleOut)
{
    WindowPtr   dialogWindow;
    Rect        windowRect, textRect, okRect, cancelRect;
    TEHandle    titleTE;
    Rect        destRect, viewRect;
    EventRecord event;
    Boolean     done = false;
    Boolean     result = false;
    Point       localPt;
    short       screenWidth, screenHeight;
    long        textLen;
    Handle      textHandle;
    extern AppGlobals gApp;
    /* Calculate centered dialog position */
    screenWidth = gApp.qd.screenBits.bounds.right - gApp.qd.screenBits.bounds.left;
    screenHeight = gApp.qd.screenBits.bounds.bottom - gApp.qd.screenBits.bounds.top;
    SetRect(&windowRect,
            (screenWidth - 320) / 2,
            (screenHeight - 140) / 2,
            (screenWidth + 320) / 2,
            (screenHeight + 140) / 2);
    /* Create dialog window */
    dialogWindow = NewWindow(nil, &windowRect,
                            "\pName Conversation",
                            true, documentProc,
                            (WindowPtr)-1, true, 0);
    if (dialogWindow == nil)
        return false;
    SetPort(dialogWindow);
    /* Set up text field */
    SetRect(&textRect, 20, 50, 300, 70);
    destRect = textRect;
    viewRect = textRect;
    InsetRect(&viewRect, 2, 2);
    titleTE = TENew(&destRect, &viewRect);
    if (titleTE != nil)
    {
        TEActivate(titleTE);
        /* Pre-fill with "New Conversation" */
        TESetText("New Conversation", 16, titleTE);
        TESetSelect(0, 32767, titleTE);  /* Select all */
    }
    /* Set up buttons */
    SetRect(&okRect, 220, 90, 300, 120);
    SetRect(&cancelRect, 130, 90, 210, 120);
    /* Draw initial dialog */
    EraseRect(&dialogWindow->portRect);
    TextFont(3);
    TextSize(12);
    TextFace(normal);
    MoveTo(20, 30);
    DrawString("\pEnter conversation name:");
    FrameRect(&textRect);
    if (titleTE != nil)
        TEUpdate(&textRect, titleTE);
    DrawButton(&cancelRect, "\pCancel", false);
    DrawButton(&okRect, "\pOK", false);
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
                        short part = FindWindow(event.where, &clickWindow);
                        if (clickWindow == dialogWindow)
                        {
                            switch (part)
                            {
                                case inDrag:
                                    DragWindow(dialogWindow, event.where, &gApp.qd.screenBits.bounds);
                                    break;
                                case inGoAway:
                                    if (TrackGoAway(dialogWindow, event.where))
                                    {
                                        result = false;
                                        done = true;
                                    }
                                    break;
                                case inContent:
                                    localPt = event.where;
                                    GlobalToLocal(&localPt);
                                    if (PtInRect(localPt, &okRect))
                                    {
                                        /* OK button */
                                        InvertRect(&okRect);
                                        while (StillDown()) { /* Wait */ }
                                        InvertRect(&okRect);
                                        result = true;
                                        done = true;
                                    }
                                    else if (PtInRect(localPt, &cancelRect))
                                    {
                                        /* Cancel button */
                                        InvertRect(&cancelRect);
                                        while (StillDown()) { /* Wait */ }
                                        InvertRect(&cancelRect);
                                        result = false;
                                        done = true;
                                    }
                                    else if (PtInRect(localPt, &textRect) && titleTE != nil)
                                    {
                                        TEClick(localPt, false, titleTE);
                                    }
                                    break;
                            }
                        }
                    }
                    break;
                case keyDown:
                case autoKey:
                    {
                        char key = event.message & charCodeMask;
                        if (key == '\r' || key == '\n')
                        {
                            result = true;
                            done = true;
                        }
                        else if (key == 0x1B)  /* Escape */
                        {
                            result = false;
                            done = true;
                        }
                        else if (titleTE != nil)
                        {
                            TEKey(key, titleTE);
                            EraseRect(&textRect);
                            FrameRect(&textRect);
                            TEUpdate(&textRect, titleTE);
                        }
                    }
                    break;
                case updateEvt:
                    if ((WindowPtr)event.message == dialogWindow)
                    {
                        BeginUpdate(dialogWindow);
                        EraseRect(&dialogWindow->portRect);
                        TextFont(3);
                        TextSize(12);
                        TextFace(normal);
                        MoveTo(20, 30);
                        DrawString("\pEnter conversation name:");
                        FrameRect(&textRect);
                        if (titleTE != nil)
                            TEUpdate(&textRect, titleTE);
                        DrawButton(&cancelRect, "\pCancel", false);
                        DrawButton(&okRect, "\pOK", false);
                        EndUpdate(dialogWindow);
                    }
                    break;
            }
        }
        /* Yield to system - MUST be outside if block to prevent freeze */
        SystemTask();
    }
    /* Get the text if OK was pressed */
    if (result && titleTE != nil)
    {
        textHandle = (*titleTE)->hText;
        textLen = (*titleTE)->teLength;
        /* Validate handle before dereferencing */
        if (textHandle != nil)
        {
            HLock(textHandle);
            if (textLen > 0 && textLen < kMaxConversationTitle - 1)
            {
                BlockMove(*textHandle, titleOut, textLen);
                titleOut[textLen] = '\0';
            }
            else if (textLen == 0)
            {
                /* If empty, use default */
                BlockMove("New Conversation", titleOut, 16);
                titleOut[16] = '\0';
            }
            else
            {
                /* Truncate if too long */
                BlockMove(*textHandle, titleOut, kMaxConversationTitle - 1);
                titleOut[kMaxConversationTitle - 1] = '\0';
            }
            HUnlock(textHandle);
        }
        else
        {
            /* Handle is nil - use default */
            BlockMove("New Conversation", titleOut, 16);
            titleOut[16] = '\0';
        }
    }
    /* Clean up */
    if (titleTE != nil)
        TEDispose(titleTE);
    DisposeWindow(dialogWindow);
    return result;
}
/* Show save prompt dialog */
short ShowSavePrompt(WindowPtr parentWindow)
{
    WindowPtr   dialogWindow;
    Rect        windowRect, iconRect, saveRect, dontSaveRect, cancelRect;
    ControlHandle saveButton, dontSaveButton, cancelButton;
    EventRecord event, updateEvent;
    Boolean     done = false;
    short       result = 3;  /* Default to cancel */
    Point       localPt, finalPt;
    Rect        mainWindowRect, btnRect;
    short       textWidth1, textWidth2;
    ControlHandle clickedControl;
    short       part;
    Boolean     pressedSave = false;
    Boolean     pressedDontSave = false;
    Boolean     pressedCancel = false;
    /* Position relative to parent window */
    if (parentWindow != nil)
    {
        GrafPtr savePort;
        /* SYSTEM 7.5.3 FIX: Must set port to parentWindow BEFORE calling LocalToGlobal
           LocalToGlobal converts from CURRENT port's local coords to global coords.
           On System 7.5.3, calling LocalToGlobal with coords from a different window
           while wrong port is active causes "math domain error" crash. */
        GetPort(&savePort);
        SetPort(parentWindow);
        mainWindowRect = parentWindow->portRect;
        LocalToGlobal((Point*)&mainWindowRect.top);
        LocalToGlobal((Point*)&mainWindowRect.bottom);
        SetPort(savePort);  /* Restore port */
        /* Center on parent window */
        SetRect(&windowRect,
                mainWindowRect.left + (mainWindowRect.right - mainWindowRect.left - 280) / 2,
                mainWindowRect.top + (mainWindowRect.bottom - mainWindowRect.top - 100) / 2,
                mainWindowRect.left + (mainWindowRect.right - mainWindowRect.left - 280) / 2 + 280,
                mainWindowRect.top + (mainWindowRect.bottom - mainWindowRect.top - 100) / 2 + 100);
    }
    else
    {
        /* Fallback to screen center */
        SetRect(&windowRect, 100, 100, 380, 200);
    }
    /* Create compact color dialog window */
    dialogWindow = NewCWindow(nil, &windowRect,
                            "\p",  /* No title - cleaner look */
                            true, dBoxProc,
                            (WindowPtr)-1, true, 0);
    if (dialogWindow == nil)
        return 3;  /* Cancel */
    SetPort(dialogWindow);
    /* Icon area (folder graphic) - 32x32 for pixel-perfect rendering, left-aligned with Cancel button */
    SetRect(&iconRect, 20, 15, 52, 47);
    /* Create Control Manager buttons matching New Chat button style */
    /* Smaller buttons to match compact dialog - Cancel (left), Don't Save (middle), Save (right) */
    SetRect(&cancelRect, 20, 70, 85, 90);         /* 65px wide, 20px tall */
    SetRect(&dontSaveRect, 95, 70, 175, 90);      /* 80px wide, 20px tall */
    SetRect(&saveRect, 185, 70, 240, 90);         /* 55px wide, 20px tall */
    cancelButton = NewControl(dialogWindow, &cancelRect,
                             "\pCancel", true, 0, 0, 1, pushButProc, 0);
    dontSaveButton = NewControl(dialogWindow, &dontSaveRect,
                               "\pDon't Save", true, 0, 0, 1, pushButProc, 0);
    saveButton = NewControl(dialogWindow, &saveRect,
                           "\pSave", true, 0, 0, 1, pushButProc, 0);
    /* Draw initial dialog */
    EraseRect(&dialogWindow->portRect);
    /* Draw folder icon 130 using icon suite - use large data only for crisp rendering */
    {
        Handle iconSuite;
        OSErr err;
        extern void DebugLog(const char *message);
        DebugLog("ShowSavePrompt: Loading icon suite 130 (large data only)");
        iconSuite = nil;
        /* Use svLarge1Bit | svLarge4Bit | svLarge8Bit for just the large icons */
        err = GetIconSuite(&iconSuite, 130, svLarge1Bit | svLarge4Bit | svLarge8Bit);
        if (err == noErr && iconSuite != nil)
        {
            DebugLog("Icon suite 130 loaded, plotting pixel-perfect at 32x32");
            /* Use atNone for pixel-perfect 32x32 rendering with no scaling */
            PlotIconSuite(&iconRect, atNone, ttNone, iconSuite);
            DisposeIconSuite(iconSuite, true);
        }
        else
        {
            DebugLog("Icon suite 130 not found");
            /* Draw placeholder */
            PenNormal();
            FrameRect(&iconRect);
        }
    }
    /* Draw message text - 12pt */
    TextFont(3);
    TextSize(12);
    TextFace(bold);
    textWidth1 = StringWidth("\pSave conversation before");
    textWidth2 = StringWidth("\pstarting a new one?");
    /* Center text in area from x=55 to x=280 (225px width) */
    MoveTo(55 + (225 - textWidth1) / 2, 30);
    DrawString("\pSave conversation before");
    MoveTo(55 + (225 - textWidth2) / 2, 45);
    DrawString("\pstarting a new one?");
    TextFace(normal);
    /* Control Manager draws buttons automatically */
    DrawControls(dialogWindow);
    /* Event loop */
    while (!done)
    {
        if (GetNextEvent(everyEvent, &event))
        {
            switch (event.what)
            {
                case mouseDown:
                    SetPort(dialogWindow);
                    localPt = event.where;
                    GlobalToLocal(&localPt);
                    /* Use FindControl to detect button clicks */
                    if (FindControl(localPt, dialogWindow, &clickedControl) != 0)
                    {
                        /* Visual feedback using the same pattern as Send/New Chat buttons */
                        btnRect = (**clickedControl).contrlRect;
                        /* Set pressed state */
                        if (clickedControl == saveButton)
                            pressedSave = true;
                        else if (clickedControl == dontSaveButton)
                            pressedDontSave = true;
                        else if (clickedControl == cancelButton)
                            pressedCancel = true;
                        /* Invalidate button area to trigger redraw with feedback */
                        InvalRect(&btnRect);
                        /* INFINITE LOOP FIX: Process update events with iteration limit */
                        {
                            int updateCount = 0;
                            /* Use GetNextEvent (not EventAvail) to actually REMOVE events from queue */
                            while (GetNextEvent(updateMask, &updateEvent) && updateCount < 10) {
                                updateCount++;
                                if ((WindowPtr)updateEvent.message == dialogWindow) {
                                switch (updateEvent.what) {
                                    case updateEvt:
                                        if ((WindowPtr)updateEvent.message == dialogWindow)
                                        {
                                            BeginUpdate(dialogWindow);
                                            SetPort(dialogWindow);
                                            EraseRect(&dialogWindow->portRect);
                                            /* Redraw folder icon */
                                            {
                                                Handle iconSuite = nil;
                                                OSErr err = GetIconSuite(&iconSuite, 130, svLarge1Bit | svLarge4Bit | svLarge8Bit);
                                                if (err == noErr && iconSuite != nil)
                                                {
                                                    PlotIconSuite(&iconRect, atNone, ttNone, iconSuite);
                                                    DisposeIconSuite(iconSuite, true);
                                                }
                                            }
                                            /* Redraw text */
                                            TextFont(3);
                                            TextSize(12);
                                            TextFace(bold);
                                            textWidth1 = StringWidth("\pSave conversation before");
                                            textWidth2 = StringWidth("\pstarting a new one?");
                                            MoveTo(55 + (225 - textWidth1) / 2, 30);
                                            DrawString("\pSave conversation before");
                                            MoveTo(55 + (225 - textWidth2) / 2, 45);
                                            DrawString("\pstarting a new one?");
                                            TextFace(normal);
                                            /* Control Manager redraws buttons */
                                            DrawControls(dialogWindow);
                                            /* Draw visual feedback for pressed buttons */
                                            if (pressedSave)
                                            {
                                                PenNormal();
                                                PenSize(3, 3);
                                                FrameRect(&saveRect);
                                                PenSize(1, 1);
                                            }
                                            if (pressedDontSave)
                                            {
                                                PenNormal();
                                                PenSize(3, 3);
                                                FrameRect(&dontSaveRect);
                                                PenSize(1, 1);
                                            }
                                            if (pressedCancel)
                                            {
                                                PenNormal();
                                                PenSize(3, 3);
                                                FrameRect(&cancelRect);
                                                PenSize(1, 1);
                                            }
                                            EndUpdate(dialogWindow);
                                        }
                                        break;
                                }
                            } else {
                                break;  /* Not our window */
                            }
                        }
                        }  /* End of update count block */
                        /* Wait for mouse release */
                        while (StillDown()) { /* wait */ }
                        /* Clear pressed state */
                        if (clickedControl == saveButton)
                            pressedSave = false;
                        else if (clickedControl == dontSaveButton)
                            pressedDontSave = false;
                        else if (clickedControl == cancelButton)
                            pressedCancel = false;
                        /* Invalidate again to remove feedback */
                        InvalRect(&btnRect);
                        /* INFINITE LOOP FIX: Process update events with iteration limit */
                        {
                            int updateCount = 0;
                            /* Use GetNextEvent (not EventAvail) to actually REMOVE events from queue */
                            while (GetNextEvent(updateMask, &updateEvent) && updateCount < 10) {
                                updateCount++;
                                if ((WindowPtr)updateEvent.message == dialogWindow) {
                                    switch (updateEvent.what) {
                                    case updateEvt:
                                        if ((WindowPtr)updateEvent.message == dialogWindow)
                                        {
                                            BeginUpdate(dialogWindow);
                                            SetPort(dialogWindow);
                                            EraseRect(&dialogWindow->portRect);
                                            /* Redraw folder icon */
                                            {
                                                Handle iconSuite = nil;
                                                OSErr err = GetIconSuite(&iconSuite, 130, svLarge1Bit | svLarge4Bit | svLarge8Bit);
                                                if (err == noErr && iconSuite != nil)
                                                {
                                                    PlotIconSuite(&iconRect, atNone, ttNone, iconSuite);
                                                    DisposeIconSuite(iconSuite, true);
                                                }
                                            }
                                            /* Redraw text */
                                            TextFont(3);
                                            TextSize(12);
                                            TextFace(bold);
                                            textWidth1 = StringWidth("\pSave conversation before");
                                            textWidth2 = StringWidth("\pstarting a new one?");
                                            MoveTo(55 + (225 - textWidth1) / 2, 30);
                                            DrawString("\pSave conversation before");
                                            MoveTo(55 + (225 - textWidth2) / 2, 45);
                                            DrawString("\pstarting a new one?");
                                            TextFace(normal);
                                            /* Control Manager redraws buttons */
                                            DrawControls(dialogWindow);
                                            EndUpdate(dialogWindow);
                                        }
                                        break;
                                }
                            } else {
                                break;  /* Not our window */
                            }
                        }
                        }  /* End of update count block */
                        /* Check if released inside */
                        GetMouse(&finalPt);
                        part = PtInRect(finalPt, &btnRect) ? 1 : 0;
                        if (part != 0)
                        {
                            /* Determine which button was clicked */
                            if (clickedControl == saveButton)
                            {
                                result = 1;  /* Save */
                                done = true;
                            }
                            else if (clickedControl == dontSaveButton)
                            {
                                result = 2;  /* Don't Save */
                                done = true;
                            }
                            else if (clickedControl == cancelButton)
                            {
                                result = 3;  /* Cancel */
                                done = true;
                            }
                        }
                    }
                    break;
                case updateEvt:
                    if ((WindowPtr)event.message == dialogWindow)
                    {
                        BeginUpdate(dialogWindow);
                        SetPort(dialogWindow);
                        EraseRect(&dialogWindow->portRect);
                        /* Redraw folder icon 130 using icon suite (large data only) */
                        {
                            Handle iconSuite = nil;
                            OSErr err = GetIconSuite(&iconSuite, 130, svLarge1Bit | svLarge4Bit | svLarge8Bit);
                            if (err == noErr && iconSuite != nil)
                            {
                                PlotIconSuite(&iconRect, atNone, ttNone, iconSuite);
                                DisposeIconSuite(iconSuite, true);
                            }
                        }
                        /* Redraw text - 12pt */
                        TextFont(3);
                        TextSize(12);
                        TextFace(bold);
                        textWidth1 = StringWidth("\pSave conversation before");
                        textWidth2 = StringWidth("\pstarting a new one?");
                        /* Center text in area from x=55 to x=280 (225px width) */
                        MoveTo(55 + (225 - textWidth1) / 2, 30);
                        DrawString("\pSave conversation before");
                        MoveTo(55 + (225 - textWidth2) / 2, 45);
                        DrawString("\pstarting a new one?");
                        TextFace(normal);
                        /* Control Manager redraws buttons automatically */
                        DrawControls(dialogWindow);
                        /* Draw visual feedback for pressed buttons */
                        if (pressedSave)
                        {
                            PenNormal();
                            PenSize(3, 3);
                            FrameRect(&saveRect);
                            PenSize(1, 1);
                        }
                        if (pressedDontSave)
                        {
                            PenNormal();
                            PenSize(3, 3);
                            FrameRect(&dontSaveRect);
                            PenSize(1, 1);
                        }
                        if (pressedCancel)
                        {
                            PenNormal();
                            PenSize(3, 3);
                            FrameRect(&cancelRect);
                            PenSize(1, 1);
                        }
                        EndUpdate(dialogWindow);
                    }
                    break;
            }
        }
        /* Yield to system - MUST be outside if block to prevent freeze */
        SystemTask();
    }
    /* Clean up */
    DisposeWindow(dialogWindow);
    return result;
}
/* Generic prompt for input */
Boolean PromptForInput(const char* promptText, char* valueOut)
{
    #pragma unused(promptText)
    /* For now, just call PromptForConversationTitle */
    /* Can be extended later to use promptText */
    return PromptForConversationTitle(valueOut);
}
#pragma segment ConvMgr
/* Show simple message dialog */
void ShowMessageDialog(const char* message)
{
    WindowPtr dialogWindow;
    Rect windowRect, okRect;
    EventRecord event;
    Boolean done = false;
    Point localPt;
    short screenWidth, screenHeight;
    extern AppGlobals gApp;
    /* Calculate centered position */
    screenWidth = gApp.qd.screenBits.bounds.right - gApp.qd.screenBits.bounds.left;
    screenHeight = gApp.qd.screenBits.bounds.bottom - gApp.qd.screenBits.bounds.top;
    SetRect(&windowRect,
            (screenWidth - 280) / 2,
            (screenHeight - 120) / 2,
            (screenWidth + 280) / 2,
            (screenHeight + 120) / 2);
    /* Create dialog */
    dialogWindow = NewWindow(nil, &windowRect, "\pMacAI Local", true, documentProc,
                            (WindowPtr)-1, true, 0);
    if (dialogWindow == nil)
        return;
    SetPort(dialogWindow);
    /* Set up OK button */
    SetRect(&okRect, 110, 75, 170, 100);
    /* Draw dialog */
    EraseRect(&dialogWindow->portRect);
    TextFont(3);
    TextSize(12);
    TextFace(normal);
    MoveTo(20, 35);
    DrawString((ConstStr255Param)c2pstr((char*)message));
    PenSize(2, 2);
    FrameRoundRect(&okRect, 8, 8);
    PenSize(1, 1);
    MoveTo(okRect.left + 22, okRect.top + 17);
    DrawString("\pOK");
    /* Event loop */
    while (!done)
    {
        if (GetNextEvent(everyEvent, &event))
        {
            switch (event.what)
            {
                case mouseDown:
                    SetPort(dialogWindow);
                    localPt = event.where;
                    GlobalToLocal(&localPt);
                    if (PtInRect(localPt, &okRect))
                    {
                        InvertRect(&okRect);
                        while (StillDown()) { }
                        done = true;
                    }
                    break;
                case keyDown:
                    if ((event.message & charCodeMask) == '\r')
                        done = true;
                    break;
            }
        }
        /* Yield to system - MUST be outside if block to prevent freeze */
        SystemTask();
    }
    DisposeWindow(dialogWindow);
}
/* Show confirmation dialog */
Boolean ShowConfirmDialog(const char* message)
{
    WindowPtr dialogWindow;
    Rect windowRect, okRect, cancelRect;
    EventRecord event;
    Boolean done = false;
    Boolean result = false;
    Point localPt;
    short screenWidth, screenHeight;
    extern AppGlobals gApp;
    /* Calculate centered position */
    screenWidth = gApp.qd.screenBits.bounds.right - gApp.qd.screenBits.bounds.left;
    screenHeight = gApp.qd.screenBits.bounds.bottom - gApp.qd.screenBits.bounds.top;
    SetRect(&windowRect,
            (screenWidth - 320) / 2,
            (screenHeight - 130) / 2,
            (screenWidth + 320) / 2,
            (screenHeight + 130) / 2);
    /* Create dialog */
    dialogWindow = NewWindow(nil, &windowRect, "\pConfirm", true, documentProc,
                            (WindowPtr)-1, true, 0);
    if (dialogWindow == nil)
        return false;
    SetPort(dialogWindow);
    /* Set up buttons */
    SetRect(&okRect, 180, 80, 300, 110);
    SetRect(&cancelRect, 20, 80, 140, 110);
    /* Draw dialog */
    EraseRect(&dialogWindow->portRect);
    TextFont(3);
    TextSize(12);
    TextFace(normal);
    MoveTo(20, 35);
    DrawString((ConstStr255Param)c2pstr((char*)message));
    PenSize(2, 2);
    FrameRoundRect(&okRect, 8, 8);
    FrameRoundRect(&cancelRect, 8, 8);
    PenSize(1, 1);
    TextFace(bold);
    MoveTo(okRect.left + 30, okRect.top + 20);
    DrawString("\pOverwrite");
    MoveTo(cancelRect.left + 35, cancelRect.top + 20);
    DrawString("\pCancel");
    /* Event loop */
    while (!done)
    {
        if (GetNextEvent(everyEvent, &event))
        {
            switch (event.what)
            {
                case mouseDown:
                    SetPort(dialogWindow);
                    localPt = event.where;
                    GlobalToLocal(&localPt);
                    if (PtInRect(localPt, &okRect))
                    {
                        InvertRect(&okRect);
                        while (StillDown()) { }
                        result = true;
                        done = true;
                    }
                    else if (PtInRect(localPt, &cancelRect))
                    {
                        InvertRect(&cancelRect);
                        while (StillDown()) { }
                        result = false;
                        done = true;
                    }
                    break;
                case keyDown:
                    {
                        char key = event.message & charCodeMask;
                        if (key == '\r')
                        {
                            result = true;
                            done = true;
                        }
                        else if (key == 0x1B)  /* Escape */
                        {
                            result = false;
                            done = true;
                        }
                    }
                    break;
            }
        }
        /* Yield to system - MUST be outside if block to prevent freeze */
        SystemTask();
    }
    DisposeWindow(dialogWindow);
    return result;
}
/*------------------------------------------------------------------------------
    ShowMacinAIAlertWithID - Show system Alert using specific ALRT resource
    Uses ParamText to set ^0=title and ^1=message in the DITL.
    Button labels are defined in the DITL resource (not runtime customizable).
    Returns: Item number clicked (1=first button, 2=second button)
------------------------------------------------------------------------------*/
short ShowMacinAIAlertWithID(short alertID, const char* title, const char* message)
{
    Str255 pTitle, pMessage;
    short titleLen, msgLen;
    short result;
    ModalFilterUPP filterUPP;
    /* Convert C strings to Pascal strings */
    titleLen = strlen(title);
    if (titleLen > 255) titleLen = 255;
    pTitle[0] = titleLen;
    BlockMoveData(title, &pTitle[1], titleLen);
    msgLen = strlen(message);
    if (msgLen > 255) msgLen = 255;
    pMessage[0] = msgLen;
    BlockMoveData(message, &pMessage[1], msgLen);
    /* Set dialog text - ^0 = title, ^1 = message */
    ParamText(pTitle, pMessage, "\p", "\p");
    /* Create modal filter UPP to process events during alert */
    #if TARGET_API_MAC_CARBON
        filterUPP = NewModalFilterUPP(AlertModalFilter);
    #else
        filterUPP = NewModalFilterProc(AlertModalFilter);
    #endif
    /* Show system alert with modal filter to prevent UI freeze */
    result = Alert(alertID, filterUPP);
    /* Clean up */
    #if TARGET_API_MAC_CARBON
        DisposeModalFilterUPP(filterUPP);
    #else
        DisposeRoutineDescriptor(filterUPP);
    #endif
    return result;
}
/*------------------------------------------------------------------------------
    ShowMacinAIAlert - MacinAI-branded dialog with OK/Cancel buttons
    Uses ALRT 129 which has hardcoded "OK" and "Cancel" button labels.
    Button text params are IGNORED - use ShowMacinAIAlertWithID for custom buttons.
    Returns: 1=OK button, 2=Cancel button
------------------------------------------------------------------------------*/
short ShowMacinAIAlert(const char* title, const char* message,
                       const char* okButton, const char* cancelButton)
{
#pragma unused(okButton, cancelButton)
    /* Use ALRT 129 which has "OK" / "Cancel" buttons */
    return ShowMacinAIAlertWithID(129, title, message);
}
/*------------------------------------------------------------------------------
    ShowMacinAIAlert3 - MacinAI-branded dialog with 3 buttons
    Uses Control Manager buttons for consistent UI.
    Returns: 1=OK, 2=Other, 3=Cancel
------------------------------------------------------------------------------*/
short ShowMacinAIAlert3(const char* title, const char* message,
                        const char* okButton, const char* otherButton,
                        const char* cancelButton)
{
    WindowPtr dialogWindow;
    Rect windowRect, iconRect, okRect, otherRect, cancelRect;
    ControlHandle okControl, otherControl, cancelControl;
    EventRecord event;
    Boolean done;
    short result;
    Point localPt;
    short screenWidth, screenHeight;
    Handle iconSuite;
    OSErr err;
    Str255 pTitle, pMessage, pOK, pOther, pCancel;
    short titleLen, msgLen, okLen, otherLen, cancelLen;
    ControlHandle clickedControl;
    extern AppGlobals gApp;
    done = false;
    result = 3;  /* Default to Cancel */
    iconSuite = nil;
    /* Convert C strings to Pascal strings */
    titleLen = strlen(title);
    if (titleLen > 255) titleLen = 255;
    pTitle[0] = titleLen;
    BlockMoveData(title, &pTitle[1], titleLen);
    msgLen = strlen(message);
    if (msgLen > 255) msgLen = 255;
    pMessage[0] = msgLen;
    BlockMoveData(message, &pMessage[1], msgLen);
    okLen = strlen(okButton);
    if (okLen > 255) okLen = 255;
    pOK[0] = okLen;
    BlockMoveData(okButton, &pOK[1], okLen);
    otherLen = strlen(otherButton);
    if (otherLen > 255) otherLen = 255;
    pOther[0] = otherLen;
    BlockMoveData(otherButton, &pOther[1], otherLen);
    cancelLen = strlen(cancelButton);
    if (cancelLen > 255) cancelLen = 255;
    pCancel[0] = cancelLen;
    BlockMoveData(cancelButton, &pCancel[1], cancelLen);
    /* Calculate centered position - compact 320x100 dialog */
    screenWidth = gApp.qd.screenBits.bounds.right - gApp.qd.screenBits.bounds.left;
    screenHeight = gApp.qd.screenBits.bounds.bottom - gApp.qd.screenBits.bounds.top;
    SetRect(&windowRect,
            (screenWidth - 320) / 2,
            (screenHeight - 100) / 2,
            (screenWidth + 320) / 2,
            (screenHeight + 100) / 2);
    /* Create dialog window (dBoxProc = modal, no title bar) */
    dialogWindow = NewCWindow(nil, &windowRect, "\p",
                             true, dBoxProc,
                             (WindowPtr)-1, false, 0);
    if (dialogWindow == nil)
        return 3;
    SetPort(dialogWindow);
    /* Icon position (32x32 on left) */
    SetRect(&iconRect, 20, 15, 52, 47);
    /* Load MacinAI icon suite (ID 128 = app icon) */
    err = GetIconSuite(&iconSuite, 128, svLarge1Bit | svLarge4Bit | svLarge8Bit);
    /* Create Control Manager buttons - Cancel left, Other middle, OK right */
    SetRect(&cancelRect, 20, 70, 100, 90);
    SetRect(&otherRect, 115, 70, 205, 90);
    SetRect(&okRect, 220, 70, 300, 90);
    cancelControl = NewControl(dialogWindow, &cancelRect, pCancel, true, 0, 0, 1, pushButProc, 0);
    otherControl = NewControl(dialogWindow, &otherRect, pOther, true, 0, 0, 1, pushButProc, 0);
    okControl = NewControl(dialogWindow, &okRect, pOK, true, 0, 0, 1, pushButProc, 0);
    /* Draw initial dialog */
    EraseRect(&dialogWindow->portRect);
    /* Draw icon */
    if (err == noErr && iconSuite != nil) {
        PlotIconSuite(&iconRect, atNone, ttNone, iconSuite);
    }
    /* Draw title (bold) */
    TextFont(3);  /* Geneva */
    TextSize(12);
    TextFace(bold);
    MoveTo(60, 28);
    DrawString(pTitle);
    /* Draw message (normal) */
    TextFace(normal);
    MoveTo(60, 45);
    DrawString(pMessage);
    /* Draw controls */
    DrawControls(dialogWindow);
    /* Event loop */
    while (!done)
    {
        if (GetNextEvent(everyEvent, &event))
        {
            switch (event.what)
            {
                case mouseDown:
                    SetPort(dialogWindow);
                    localPt = event.where;
                    GlobalToLocal(&localPt);
                    /* Use FindControl for proper button handling */
                    if (FindControl(localPt, dialogWindow, &clickedControl) != 0)
                    {
                        if (TrackControl(clickedControl, localPt, nil) != 0)
                        {
                            if (clickedControl == okControl)
                            {
                                result = 1;  /* OK */
                                done = true;
                            }
                            else if (clickedControl == otherControl)
                            {
                                result = 2;  /* Other */
                                done = true;
                            }
                            else if (clickedControl == cancelControl)
                            {
                                result = 3;  /* Cancel */
                                done = true;
                            }
                        }
                    }
                    break;
                case keyDown:
                    {
                        char key = event.message & charCodeMask;
                        if (key == '\r' || key == '\r')
                        {
                            result = 1;  /* OK */
                            done = true;
                        }
                        else if (key == 0x1B)  /* Escape */
                        {
                            result = 3;  /* Cancel */
                            done = true;
                        }
                    }
                    break;
                case updateEvt:
                    {
                        /* MUST call BeginUpdate/EndUpdate for ALL windows */
                        WindowPtr updateWindow = (WindowPtr)event.message;
                        BeginUpdate(updateWindow);
                        if (updateWindow == dialogWindow)
                        {
                            SetPort(dialogWindow);
                            EraseRect(&dialogWindow->portRect);
                            /* Redraw icon */
                            if (iconSuite != nil) {
                                PlotIconSuite(&iconRect, atNone, ttNone, iconSuite);
                            }
                            /* Redraw text */
                            TextFont(3);
                            TextSize(12);
                            TextFace(bold);
                            MoveTo(60, 28);
                            DrawString(pTitle);
                            TextFace(normal);
                            MoveTo(60, 45);
                            DrawString(pMessage);
                            /* Redraw controls */
                            DrawControls(dialogWindow);
                        }
                        EndUpdate(updateWindow);
                    }
                    break;
            }
        }
        /* Yield to system - MUST be outside if block to prevent freeze */
        SystemTask();
    }
    /* Clean up */
    if (iconSuite != nil) {
        DisposeIconSuite(iconSuite, true);
    }
    DisposeWindow(dialogWindow);
    return result;
}
