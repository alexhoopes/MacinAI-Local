/*----------------------------------------------------------------------
    Installer.c - MacinAI Local 2-Disc CD Installer

    All custom UI, no system alerts. Features:
    - Splash screen with MacinAI icon
    - Model selection screen with checkboxes and sizes
    - Accurate file-level progress bar (bytes written / file size)
    - Disc swap screen with OK/Skip
    - Gestalt 68K/PPC detection
    - Desktop alias creation

    Compiler: CodeWarrior Pro 5 (C89/ANSI C)
    Target: System 7.5.3+ (68K)

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

#include "Installer.h"

#pragma segment Installer

/* ---- Forward Declarations ---- */

static void InitToolbox(void);
static Boolean IsPowerPC(void);

static void ShowSplashScreen(void);
static Boolean ShowWelcomeScreen(void);
static Boolean ShowModelSelect(ModelChoices *choices);
static void ShowProgressScreen(void);
static void UpdateProgress(ConstStr255Param fileName, long fileBytes,
                            long fileTotalBytes, long overallBytes,
                            long overallTotal);
static void CloseProgressScreen(void);
static Boolean ShowDiscSwapScreen(void);
static void ShowCompleteScreen(void);
static void ShowErrorScreen(ConstStr255Param message);

static OSErr GetCDRoot(short *vRefNum, long *dirID);
static OSErr FindSubfolder(short vRefNum, long parentDirID,
                            ConstStr255Param folderName, long *childDirID);
static OSErr CreateInstallFolder(short *dstVRefNum, long *dstDirID);
static OSErr CopyOneFile(short srcVRefNum, long srcDirID,
                          short dstVRefNum, long dstDirID,
                          ConstStr255Param fileName,
                          OSType fileType, OSType fileCreator,
                          long *overallCopied, long overallTotal);
static long GetFileSize(short vRefNum, long dirID,
                         ConstStr255Param fileName);
static OSErr EjectOnly(short vRefNum);
static OSErr EjectVolume(short vRefNum);
static Boolean WaitForVolume(ConstStr255Param volName, short *vRefNum);
static OSErr CopyInstallerToHD(short dstVRefNum, FSSpec *outSpec);
static OSErr MakeDesktopAlias(short appVRefNum, long appDirID,
                               ConstStr255Param appName);
static void CenterRect(Rect *r, short width, short height);
static void DrawCenteredString(short windowWidth, short y,
                                ConstStr255Param str);

/* ---- Globals ---- */

static WindowPtr gProgressWind = nil;
static long gModelsDirID = 0;
static short gLogRefNum = 0;

/*----------------------------------------------------------------------
    InstLog - Write a line to installer.log in the install folder.
    Falls back to the app's own directory if install folder not created yet.
----------------------------------------------------------------------*/
static void InstLog(const char *msg)
{
    long count;
    char line[512];
    long ticks;

    if (gLogRefNum == 0)
    {
        FSSpec logSpec;
        OSErr err;
        short vRefNum;
        long dirID;

        /* Write log to Desktop (not the CD, can't write to CD,
           and CD gets ejected during disc swap) */
        err = FindFolder(kOnSystemDisk, kDesktopFolderType,
                         false, &vRefNum, &dirID);
        if (err != noErr)
        {
            gLogRefNum = 0;
            return;
        }

        err = FSMakeFSSpec(vRefNum, dirID, "\pMacinAI Installer.log", &logSpec);
        if (err == noErr)
            FSpDelete(&logSpec);
        FSpCreate(&logSpec, 'ttxt', 'TEXT', smSystemScript);
        err = FSpOpenDF(&logSpec, fsWrPerm, &gLogRefNum);
        if (err != noErr)
        {
            gLogRefNum = 0;
            return;
        }
    }

    ticks = TickCount();
    sprintf(line, "[%ld] %s\r", ticks, msg);
    count = strlen(line);
    FSWrite(gLogRefNum, &count, line);
}

static void InstLogNum(const char *label, long value)
{
    char msg[256];
    sprintf(msg, "%s%ld", label, value);
    InstLog(msg);
}

static void InstLogStr(const char *label, ConstStr255Param pStr)
{
    char msg[256];
    char cStr[64];
    short len;
    len = pStr[0];
    if (len > 60) len = 60;
    BlockMoveData(pStr + 1, cStr, len);
    cStr[len] = '\0';
    sprintf(msg, "%s%s", label, cStr);
    InstLog(msg);
}

/*----------------------------------------------------------------------
    InitToolbox
----------------------------------------------------------------------*/
static void InitToolbox(void)
{
    MaxApplZone();
    MoreMasters();
    MoreMasters();

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(nil);
    InitCursor();
    FlushEvents(everyEvent, 0);

    /* Lock all CODE resources in RAM so the installer doesn't
       need to read from the CD after ejecting Disc 1.
       The installer is small (~50KB) and fits easily in memory. */
    {
        short i;
        Handle codeRes;
        for (i = 0; i < 128; i++)
        {
            codeRes = GetResource('CODE', i);
            if (codeRes != nil)
            {
                HLock(codeRes);
                HNoPurge(codeRes);
                DetachResource(codeRes);
            }
        }
    }
}

/*----------------------------------------------------------------------
    IsPowerPC
----------------------------------------------------------------------*/
static Boolean IsPowerPC(void)
{
    long response;
    response = 0;
    if (Gestalt(gestaltSysArchitecture, &response) == noErr)
        if (response == gestaltPowerPC)
            return true;
    return false;
}

/*----------------------------------------------------------------------
    CenterRect
----------------------------------------------------------------------*/
static void CenterRect(Rect *r, short width, short height)
{
    short sw, sh;
    sw = qd.screenBits.bounds.right;
    sh = qd.screenBits.bounds.bottom;
    r->left = (sw - width) / 2;
    r->top = (sh - height) / 2;
    r->right = r->left + width;
    r->bottom = r->top + height;
}

/*----------------------------------------------------------------------
    DrawCenteredString
----------------------------------------------------------------------*/
static void DrawCenteredString(short windowWidth, short y,
                                ConstStr255Param str)
{
    short w;
    w = StringWidth(str);
    MoveTo((windowWidth - w) / 2, y);
    DrawString(str);
}

/*----------------------------------------------------------------------
    ShowSplashScreen
----------------------------------------------------------------------*/
static void ShowSplashScreen(void)
{
    WindowPtr wind;
    Rect windRect;
    Rect iconRect;
    Handle iconSuite;
    OSErr err;

    iconSuite = nil;
    InstLog("--- MacinAI Installer Log ---");
    CenterRect(&windRect, 300, 200);
    wind = NewCWindow(nil, &windRect, "\p", true, dBoxProc,
                       (WindowPtr)-1, false, 0);
    if (wind == nil) return;
    SetPort(wind);

    SetRect(&iconRect, 134, 20, 166, 52);
    err = GetIconSuite(&iconSuite, 128, svLarge1Bit | svLarge4Bit | svLarge8Bit);
    if (err == noErr && iconSuite != nil)
    {
        PlotIconSuite(&iconRect, atNone, ttNone, iconSuite);
        DisposeIconSuite(iconSuite, true);
    }

    TextFont(3); TextSize(18); TextFace(bold);
    DrawCenteredString(300, 82, "\pMacinAI Local");
    TextSize(10); TextFace(0);
    DrawCenteredString(300, 100, "\pInstaller");
    TextSize(9);
    DrawCenteredString(300, 120, "\pVersion 1.0");

    DrawCenteredString(300, 148, "\pCreated by: Alex Hoopes");
    DrawCenteredString(300, 163, "\p\xa9 2026 OldAppleStuff");
    DrawCenteredString(300, 183, "\pClick to continue...");

    /* Wait for click (not timer) */
    {
        EventRecord splashEvent;
        Boolean splashDone;
        splashDone = false;
        while (!splashDone)
        {
            if (GetNextEvent(mDownMask | keyDownMask, &splashEvent))
                splashDone = true;
            SystemTask();
        }
    }

    DisposeWindow(wind);
}

/*----------------------------------------------------------------------
    ShowWelcomeScreen - Returns true if Install clicked
----------------------------------------------------------------------*/
static Boolean ShowWelcomeScreen(void)
{
    WindowPtr wind;
    Rect windRect;
    ControlHandle installBtn, quitBtn;
    Rect btnRect;
    Boolean result, done, isPPC;
    EventRecord event;
    Point clickPt;
    ControlHandle whichCtl;
    short part;
    Handle iconSuite;
    Rect iconRect;
    OSErr err;

    result = false;
    done = false;
    isPPC = IsPowerPC();
    iconSuite = nil;

    CenterRect(&windRect, kWelcomeWidth, kWelcomeHeight);
    wind = NewCWindow(nil, &windRect, "\p", true, dBoxProc,
                       (WindowPtr)-1, false, 0);
    if (wind == nil) return false;
    SetPort(wind);

    SetRect(&btnRect, kWelcomeWidth - 110, kWelcomeHeight - 40,
            kWelcomeWidth - 20, kWelcomeHeight - 18);
    installBtn = NewControl(wind, &btnRect, "\pInstall", true,
                             0, 0, 1, pushButProc, 1);
    SetRect(&btnRect, kWelcomeWidth - 200, kWelcomeHeight - 40,
            kWelcomeWidth - 120, kWelcomeHeight - 18);
    quitBtn = NewControl(wind, &btnRect, "\pQuit", true,
                          0, 0, 1, pushButProc, 2);

    SetRect(&iconRect, (kWelcomeWidth - 32) / 2, 20,
            (kWelcomeWidth + 32) / 2, 52);
    err = GetIconSuite(&iconSuite, 128, svLarge1Bit | svLarge4Bit | svLarge8Bit);
    if (err == noErr && iconSuite != nil)
    {
        PlotIconSuite(&iconRect, atNone, ttNone, iconSuite);
        DisposeIconSuite(iconSuite, true);
    }

    TextFont(3); TextSize(18); TextFace(bold);
    DrawCenteredString(kWelcomeWidth, 80, "\pMacinAI Local Installer");
    TextSize(11); TextFace(0);
    DrawCenteredString(kWelcomeWidth, 110,
        isPPC ? "\pDetected: PowerPC architecture"
              : "\pDetected: 68K architecture");
    TextSize(10);
    MoveTo(30, 140);
    DrawString("\pThis will install MacinAI Local and up to");
    MoveTo(30, 155);
    DrawString("\p4 AI models to your Applications folder.");
    MoveTo(30, 180);
    DrawString("\pDisc 1: MacinAI Tool + GPT-2 + SmolLM");
    MoveTo(30, 195);
    DrawString("\pDisc 2: Qwen 0.5B (optional)");
    MoveTo(30, 220);
    DrawString("\pTotal: up to 1.16 GB of AI models.");


    DrawControls(wind);

    while (!done)
    {
        if (GetNextEvent(everyEvent, &event))
        {
            switch (event.what)
            {
                case mouseDown:
                    clickPt = event.where;
                    GlobalToLocal(&clickPt);
                    whichCtl = nil;
                    part = FindControl(clickPt, wind, &whichCtl);
                    if (part == inButton && whichCtl != nil)
                    {
                        if (TrackControl(whichCtl, clickPt, nil) == inButton)
                        {
                            if (whichCtl == installBtn)
                            { result = true; done = true; }
                            else if (whichCtl == quitBtn)
                            { result = false; done = true; }
                        }
                    }
                    break;
                case updateEvt:
                    if ((WindowPtr)event.message == wind)
                    { BeginUpdate(wind); DrawControls(wind); EndUpdate(wind); }
                    break;
                case keyDown:
                    if ((event.message & charCodeMask) == 0x0D ||
                        (event.message & charCodeMask) == 0x03)
                    { result = true; done = true; }
                    if ((event.message & charCodeMask) == 0x1B)
                    { result = false; done = true; }
                    break;
            }
        }
        SystemTask();
    }
    DisposeWindow(wind);
    InstLog(result ? "Welcome: user clicked Install" : "Welcome: user clicked Quit");
    return result;
}

/*----------------------------------------------------------------------
    ShowModelSelect - Checkboxes for each model
----------------------------------------------------------------------*/
static Boolean ShowModelSelect(ModelChoices *choices)
{
    WindowPtr wind;
    Rect windRect;
    ControlHandle chk1, chk2, chk3, chk4;
    ControlHandle installBtn, cancelBtn;
    Rect chkRect, btnRect;
    Boolean done, result;
    EventRecord event;
    Point clickPt;
    ControlHandle whichCtl;
    short part;

    done = false;
    result = false;
    choices->macinaiTool = true;
    choices->gpt2 = true;
    choices->smolLM = true;
    choices->qwen = true;

    CenterRect(&windRect, kSelectWidth, kSelectHeight);
    wind = NewCWindow(nil, &windRect, "\p", true, dBoxProc,
                       (WindowPtr)-1, false, 0);
    if (wind == nil) return false;
    SetPort(wind);

    TextFont(3); TextSize(14); TextFace(bold);
    DrawCenteredString(kSelectWidth, 25, "\pSelect Models to Install");
    TextSize(10); TextFace(0);
    DrawCenteredString(kSelectWidth, 42,
        "\pUncheck any models you don\xd5t want.");

    SetRect(&chkRect, 30, 60, kSelectWidth - 30, 78);
    chk1 = NewControl(wind, &chkRect,
        "\pMacinAI Tool 94M v7 Q8 (102 MB) \xd0 Disc 1",
        true, 1, 0, 1, checkBoxProc, 1);

    OffsetRect(&chkRect, 0, 28);
    chk2 = NewControl(wind, &chkRect,
        "\pGPT-2 124M (137 MB) \xd0 Disc 1",
        true, 1, 0, 1, checkBoxProc, 2);

    OffsetRect(&chkRect, 0, 28);
    chk3 = NewControl(wind, &chkRect,
        "\pSmolLM 360M Instruct (389 MB) \xd0 Disc 1",
        true, 1, 0, 1, checkBoxProc, 3);

    MoveTo(30, chkRect.bottom + 10);
    LineTo(kSelectWidth - 30, chkRect.bottom + 10);

    OffsetRect(&chkRect, 0, 38);
    chk4 = NewControl(wind, &chkRect,
        "\pQwen2.5 0.5B Instruct (533 MB) \xd0 Disc 2",
        true, 1, 0, 1, checkBoxProc, 4);

    TextSize(9);
    MoveTo(30, chkRect.bottom + 20);
    DrawString("\pTotal: ~1,163 MB (app + selected models)");

    SetRect(&btnRect, kSelectWidth - 110, kSelectHeight - 40,
            kSelectWidth - 20, kSelectHeight - 18);
    installBtn = NewControl(wind, &btnRect, "\pInstall", true,
                             0, 0, 1, pushButProc, 10);
    SetRect(&btnRect, kSelectWidth - 200, kSelectHeight - 40,
            kSelectWidth - 120, kSelectHeight - 18);
    cancelBtn = NewControl(wind, &btnRect, "\pCancel", true,
                            0, 0, 1, pushButProc, 11);
    DrawControls(wind);

    while (!done)
    {
        if (GetNextEvent(everyEvent, &event))
        {
            switch (event.what)
            {
                case mouseDown:
                    clickPt = event.where;
                    GlobalToLocal(&clickPt);
                    whichCtl = nil;
                    part = FindControl(clickPt, wind, &whichCtl);
                    if (part != 0 && whichCtl != nil)
                    {
                        if (TrackControl(whichCtl, clickPt, nil) != 0)
                        {
                            if (whichCtl == chk1 || whichCtl == chk2 ||
                                whichCtl == chk3 || whichCtl == chk4)
                                SetControlValue(whichCtl,
                                    1 - GetControlValue(whichCtl));
                            else if (whichCtl == installBtn)
                            { result = true; done = true; }
                            else if (whichCtl == cancelBtn)
                            { result = false; done = true; }
                        }
                    }
                    break;
                case updateEvt:
                    if ((WindowPtr)event.message == wind)
                    { BeginUpdate(wind); DrawControls(wind); EndUpdate(wind); }
                    break;
                case keyDown:
                    if ((event.message & charCodeMask) == 0x0D ||
                        (event.message & charCodeMask) == 0x03)
                    { result = true; done = true; }
                    if ((event.message & charCodeMask) == 0x1B)
                    { result = false; done = true; }
                    break;
            }
        }
        SystemTask();
    }

    choices->macinaiTool = (GetControlValue(chk1) == 1);
    choices->gpt2 = (GetControlValue(chk2) == 1);
    choices->smolLM = (GetControlValue(chk3) == 1);
    choices->qwen = (GetControlValue(chk4) == 1);

    InstLog(result ? "ModelSelect: user clicked Install" : "ModelSelect: user cancelled");
    InstLogNum("  MacinAI Tool: ", (long)choices->macinaiTool);
    InstLogNum("  GPT-2: ", (long)choices->gpt2);
    InstLogNum("  SmolLM: ", (long)choices->smolLM);
    InstLogNum("  Qwen: ", (long)choices->qwen);

    DisposeWindow(wind);
    return result;
}

/*----------------------------------------------------------------------
    Progress Screen
----------------------------------------------------------------------*/
static void ShowProgressScreen(void)
{
    Rect windRect;
    CenterRect(&windRect, kProgressWidth, kProgressHeight);
    gProgressWind = NewCWindow(nil, &windRect,
                                "\pInstalling MacinAI Local...",
                                true, dBoxProc,
                                (WindowPtr)-1, false, 0);
}

static void UpdateProgress(ConstStr255Param fileName, long fileBytes,
                            long fileTotalBytes, long overallBytes,
                            long overallTotal)
{
    Rect barRect, fillRect;
    long pct, filePct;
    char msg[128];
    Str255 pMsg;
    short len;
    short fileMB, fileTotalMB;
    GrafPtr savePort;
    char nameStr[64];
    short nameLen;

    if (gProgressWind == nil) return;
    GetPort(&savePort);
    SetPort(gProgressWind);
    EraseRect(&gProgressWind->portRect);

    nameLen = fileName[0];
    if (nameLen > 50) nameLen = 50;
    BlockMoveData(fileName + 1, nameStr, nameLen);
    nameStr[nameLen] = '\0';

    fileMB = (short)(fileBytes / (1024L * 1024L));
    fileTotalMB = (short)(fileTotalBytes / (1024L * 1024L));
    sprintf(msg, "Copying: %s (%d of %d MB)", nameStr, fileMB, fileTotalMB);

    TextFont(3); TextSize(10); TextFace(0);
    len = strlen(msg);
    pMsg[0] = (unsigned char)len;
    BlockMoveData(msg, pMsg + 1, len);
    MoveTo(20, 20);
    DrawString(pMsg);

    /* Overall percentage */
    pct = 0;
    if (overallTotal > 0)
        pct = overallBytes / (overallTotal / 100L);
    if (pct > 100) pct = 100;

    sprintf(msg, "Overall: %ld%%", pct);
    len = strlen(msg);
    pMsg[0] = (unsigned char)len;
    BlockMoveData(msg, pMsg + 1, len);
    TextSize(9);
    MoveTo(kProgressWidth - 80, 20);
    DrawString(pMsg);

    /* Overall bar */
    SetRect(&barRect, 20, 35, kProgressWidth - 20, 53);
    FrameRect(&barRect);
    fillRect = barRect;
    InsetRect(&fillRect, 1, 1);
    {
        long fillW;
        fillW = ((long)(fillRect.right - fillRect.left) * pct) / 100L;
        fillRect.right = fillRect.left + (short)fillW;
    }
    if (fillRect.right > fillRect.left)
        PaintRect(&fillRect);

    /* Per-file bar */
    filePct = 0;
    if (fileTotalBytes > 0)
        filePct = fileBytes / (fileTotalBytes / 100L);

    SetRect(&barRect, 20, 60, kProgressWidth - 20, 72);
    FrameRect(&barRect);
    fillRect = barRect;
    InsetRect(&fillRect, 1, 1);
    {
        long fillW;
        fillW = ((long)(fillRect.right - fillRect.left) * filePct) / 100L;
        fillRect.right = fillRect.left + (short)fillW;
    }
    if (fillRect.right > fillRect.left)
    {
        ForeColor(cyanColor);
        PaintRect(&fillRect);
        ForeColor(blackColor);
    }

    sprintf(msg, "%ld%%", filePct);
    len = strlen(msg);
    pMsg[0] = (unsigned char)len;
    BlockMoveData(msg, pMsg + 1, len);
    MoveTo(kProgressWidth - 50, 82);
    DrawString(pMsg);

    SetPort(savePort);
}

static void CloseProgressScreen(void)
{
    if (gProgressWind != nil)
    {
        DisposeWindow(gProgressWind);
        gProgressWind = nil;
    }
}

/*----------------------------------------------------------------------
    ShowDiscSwapScreen - Returns true for OK, false for Skip
----------------------------------------------------------------------*/
static Boolean ShowDiscSwapScreen(void)
{
    WindowPtr wind;
    Rect windRect;
    ControlHandle okBtn, skipBtn;
    Rect btnRect;
    Boolean done, result;
    EventRecord event;
    Point clickPt;
    ControlHandle whichCtl;

    done = false;
    result = false;

    CenterRect(&windRect, 340, 160);
    wind = NewCWindow(nil, &windRect, "\p", true, dBoxProc,
                       (WindowPtr)-1, false, 0);
    if (wind == nil) return false;
    SetPort(wind);

    TextFont(3); TextSize(14); TextFace(bold);
    DrawCenteredString(340, 30, "\pInsert Disc 2");
    TextSize(10); TextFace(0);
    MoveTo(30, 55);
    DrawString("\pClick OK, then eject Disc 1 and insert");
    MoveTo(30, 70);
    DrawString("\pMacinAI Disc 2 to install the");
    MoveTo(30, 85);
    DrawString("\pQwen 0.5B model (533 MB).");
    MoveTo(30, 110);
    DrawString("\pThe disc will eject automatically.");

    SetRect(&btnRect, 230, 125, 310, 147);
    okBtn = NewControl(wind, &btnRect, "\pOK", true,
                        0, 0, 1, pushButProc, 1);
    SetRect(&btnRect, 140, 125, 220, 147);
    skipBtn = NewControl(wind, &btnRect, "\pSkip", true,
                          0, 0, 1, pushButProc, 2);
    DrawControls(wind);

    while (!done)
    {
        if (GetNextEvent(everyEvent, &event))
        {
            if (event.what == mouseDown)
            {
                clickPt = event.where;
                GlobalToLocal(&clickPt);
                whichCtl = nil;
                if (FindControl(clickPt, wind, &whichCtl) == inButton)
                {
                    if (TrackControl(whichCtl, clickPt, nil) == inButton)
                    {
                        if (whichCtl == okBtn)
                        { result = true; done = true; }
                        else if (whichCtl == skipBtn)
                        { result = false; done = true; }
                    }
                }
            }
            if (event.what == updateEvt)
            { BeginUpdate(wind); DrawControls(wind); EndUpdate(wind); }
        }
        SystemTask();
    }
    DisposeWindow(wind);
    return result;
}

/*----------------------------------------------------------------------
    ShowCompleteScreen / ShowErrorScreen
----------------------------------------------------------------------*/
static void ShowCompleteScreen(void)
{
    WindowPtr wind;
    Rect windRect;
    ControlHandle okBtn;
    Rect btnRect;
    Boolean done;
    EventRecord event;
    Point clickPt;
    ControlHandle whichCtl;

    done = false;
    CenterRect(&windRect, 340, 150);
    wind = NewCWindow(nil, &windRect, "\p", true, dBoxProc,
                       (WindowPtr)-1, false, 0);
    if (wind == nil) return;
    SetPort(wind);

    TextFont(3); TextSize(14); TextFace(bold);
    DrawCenteredString(340, 30, "\pInstallation Complete!");
    TextSize(10); TextFace(0);
    MoveTo(30, 55);
    DrawString("\pMacinAI Local has been installed.");
    MoveTo(30, 70);
    DrawString("\pDouble-click the alias on your");
    MoveTo(30, 85);
    DrawString("\pDesktop to get started.");

    SetRect(&btnRect, 240, 110, 310, 132);
    okBtn = NewControl(wind, &btnRect, "\pOK", true,
                        0, 0, 1, pushButProc, 1);
    DrawControls(wind);

    while (!done)
    {
        if (GetNextEvent(everyEvent, &event))
        {
            if (event.what == mouseDown)
            {
                clickPt = event.where;
                GlobalToLocal(&clickPt);
                whichCtl = nil;
                if (FindControl(clickPt, wind, &whichCtl) == inButton)
                    if (TrackControl(whichCtl, clickPt, nil) == inButton)
                        done = true;
            }
            if (event.what == keyDown) done = true;
            if (event.what == updateEvt)
            { BeginUpdate(wind); DrawControls(wind); EndUpdate(wind); }
        }
        SystemTask();
    }
    DisposeWindow(wind);
}

static void ShowErrorScreen(ConstStr255Param message)
{
    WindowPtr wind;
    Rect windRect;
    ControlHandle okBtn;
    Rect btnRect;
    Boolean done;
    EventRecord event;
    Point clickPt;
    ControlHandle whichCtl;

    done = false;

    InstLogStr("ERROR: ", message);

    CenterRect(&windRect, 340, 120);
    wind = NewCWindow(nil, &windRect, "\p", true, dBoxProc,
                       (WindowPtr)-1, false, 0);
    if (wind == nil) return;
    SetPort(wind);

    TextFont(3); TextSize(12); TextFace(bold);
    DrawCenteredString(340, 25, "\pInstallation Error");
    TextSize(10); TextFace(0);
    MoveTo(30, 50);
    DrawString(message);

    SetRect(&btnRect, 240, 80, 310, 102);
    okBtn = NewControl(wind, &btnRect, "\pOK", true,
                        0, 0, 1, pushButProc, 1);
    DrawControls(wind);

    while (!done)
    {
        if (GetNextEvent(everyEvent, &event))
        {
            if (event.what == mouseDown)
            {
                clickPt = event.where;
                GlobalToLocal(&clickPt);
                whichCtl = nil;
                if (FindControl(clickPt, wind, &whichCtl) == inButton)
                    if (TrackControl(whichCtl, clickPt, nil) == inButton)
                        done = true;
            }
            if (event.what == keyDown) done = true;
            if (event.what == updateEvt)
            { BeginUpdate(wind); DrawControls(wind); EndUpdate(wind); }
        }
        SystemTask();
    }
    DisposeWindow(wind);
}

/*----------------------------------------------------------------------
    File Operations
----------------------------------------------------------------------*/
static OSErr GetCDRoot(short *vRefNum, long *dirID)
{
    ProcessSerialNumber psn;
    ProcessInfoRec info;
    FSSpec appSpec;
    OSErr err;

    memset(&info, 0, sizeof(ProcessInfoRec));
    memset(&appSpec, 0, sizeof(FSSpec));
    info.processInfoLength = sizeof(ProcessInfoRec);
    info.processAppSpec = &appSpec;
    psn.highLongOfPSN = 0;
    psn.lowLongOfPSN = kCurrentProcess;

    err = GetProcessInformation(&psn, &info);
    if (err != noErr) return err;

    *vRefNum = appSpec.vRefNum;
    *dirID = appSpec.parID;
    return noErr;
}

static OSErr FindSubfolder(short vRefNum, long parentDirID,
                            ConstStr255Param folderName, long *childDirID)
{
    CInfoPBRec pb;
    Str255 name;
    OSErr err;

    BlockMoveData(folderName, name, folderName[0] + 1);
    memset(&pb, 0, sizeof(CInfoPBRec));
    pb.dirInfo.ioNamePtr = name;
    pb.dirInfo.ioVRefNum = vRefNum;
    pb.dirInfo.ioDrDirID = parentDirID;

    err = PBGetCatInfoSync(&pb);
    if (err != noErr) return err;
    if ((pb.dirInfo.ioFlAttrib & 0x10) == 0) return dirNFErr;

    *childDirID = pb.dirInfo.ioDrDirID;
    return noErr;
}

static OSErr CreateInstallFolder(short *dstVRefNum, long *dstDirID)
{
    OSErr err;
    short vRefNum;
    long parentDirID, newDirID, appsDirID;

    vRefNum = 0;
    parentDirID = 0;
    newDirID = 0;
    appsDirID = 0;

    /* Try to find Applications folder on the startup disk.
       Check "Applications (Mac OS 9)" first, then "Applications".
       If neither exists, fall back to the root of the startup disk. */
    vRefNum = 0;
    FindFolder(kOnSystemDisk, kSystemFolderType,
               false, &vRefNum, &parentDirID);
    /* vRefNum is now the startup disk */

    err = FindSubfolder(vRefNum, 2, kApps9FolderName, &appsDirID);
    if (err != noErr)
        err = FindSubfolder(vRefNum, 2, kAppsFolderName, &appsDirID);

    if (err == noErr)
    {
        parentDirID = appsDirID;
        InstLog("Installing to Applications folder");
    }
    else
    {
        /* No Applications folder, install to root of startup disk */
        parentDirID = 2;
        InstLog("No Applications folder found, installing to disk root");
    }

    err = DirCreate(vRefNum, parentDirID, kInstallFolderName, &newDirID);
    if (err == dupFNErr)
    {
        err = FindSubfolder(vRefNum, parentDirID,
                             kInstallFolderName, &newDirID);
        if (err != noErr) return err;
    }
    else if (err != noErr)
        return err;

    *dstVRefNum = vRefNum;
    *dstDirID = newDirID;
    return noErr;
}

static long GetFileSize(short vRefNum, long dirID,
                         ConstStr255Param fileName)
{
    CInfoPBRec pb;
    Str255 name;

    BlockMoveData(fileName, name, fileName[0] + 1);
    memset(&pb, 0, sizeof(CInfoPBRec));
    pb.hFileInfo.ioNamePtr = name;
    pb.hFileInfo.ioVRefNum = vRefNum;
    pb.hFileInfo.ioDirID = dirID;

    if (PBGetCatInfoSync(&pb) != noErr) return 0;
    return pb.hFileInfo.ioFlLgLen;
}

static OSErr CopyOneFile(short srcVRefNum, long srcDirID,
                          short dstVRefNum, long dstDirID,
                          ConstStr255Param fileName,
                          OSType fileType, OSType fileCreator,
                          long *overallCopied, long overallTotal)
{
    OSErr err;
    FSSpec srcSpec, dstSpec;
    short srcRef, dstRef;
    long fileSize, remaining, chunk;
    Ptr buffer;
    FInfo fInfo;
    long fileCopied;
    long chunkCount;

    srcRef = 0;
    dstRef = 0;
    buffer = nil;
    fileCopied = 0;
    chunkCount = 0;

    err = FSMakeFSSpec(srcVRefNum, srcDirID, fileName, &srcSpec);
    if (err != noErr)
    {
        InstLogStr("CopyOneFile: FSMakeFSSpec FAILED for ", fileName);
        InstLogNum("  err = ", (long)err);
        return err;
    }

    fileSize = GetFileSize(srcVRefNum, srcDirID, fileName);
    InstLogStr("CopyOneFile: ", fileName);
    InstLogNum("  size = ", fileSize);
    UpdateProgress(fileName, 0, fileSize, *overallCopied, overallTotal);

    err = FSMakeFSSpec(dstVRefNum, dstDirID, fileName, &dstSpec);
    if (err == noErr) FSpDelete(&dstSpec);

    err = FSpCreate(&dstSpec, fileCreator, fileType, smSystemScript);
    if (err != noErr && err != dupFNErr) return err;

    buffer = NewPtr(kCopyBufferSize);
    if (buffer == nil) return memFullErr;

    err = FSpOpenDF(&srcSpec, fsRdPerm, &srcRef);
    if (err != noErr) { DisposePtr(buffer); return err; }

    err = FSpOpenDF(&dstSpec, fsWrPerm, &dstRef);
    if (err != noErr) { FSClose(srcRef); DisposePtr(buffer); return err; }

    remaining = fileSize;
    while (remaining > 0)
    {
        chunk = remaining;
        if (chunk > kCopyBufferSize) chunk = kCopyBufferSize;

        err = FSRead(srcRef, &chunk, buffer);
        if (err != noErr && err != eofErr) break;

        err = FSWrite(dstRef, &chunk, buffer);
        if (err != noErr) break;

        remaining -= chunk;
        fileCopied += chunk;
        *overallCopied += chunk;
        chunkCount++;

        if ((chunkCount & 3) == 0)
        {
            UpdateProgress(fileName, fileCopied, fileSize,
                            *overallCopied, overallTotal);
            SystemTask();
        }
    }

    FSClose(srcRef);
    FSClose(dstRef);
    DisposePtr(buffer);

    if (err == eofErr) err = noErr;

    if (err == noErr)
    {
        if (FSpGetFInfo(&dstSpec, &fInfo) == noErr)
        {
            fInfo.fdType = fileType;
            fInfo.fdCreator = fileCreator;
            FSpSetFInfo(&dstSpec, &fInfo);
        }
    }

    /* Copy resource fork if it exists (critical for app binaries).
       Applications store code, icons, menus, etc. in the resource fork.
       Model .bin files have no resource fork, so this safely skips them. */
    if (err == noErr)
    {
        short srcRF, dstRF;
        long rfSize;

        srcRF = 0;
        dstRF = 0;
        rfSize = 0;

        /* Check resource fork size */
        {
            CInfoPBRec rfPB;
            Str255 rfName;
            BlockMoveData(fileName, rfName, fileName[0] + 1);
            memset(&rfPB, 0, sizeof(CInfoPBRec));
            rfPB.hFileInfo.ioNamePtr = rfName;
            rfPB.hFileInfo.ioVRefNum = srcVRefNum;
            rfPB.hFileInfo.ioDirID = srcDirID;
            if (PBGetCatInfoSync(&rfPB) == noErr)
                rfSize = rfPB.hFileInfo.ioFlRLgLen;
        }

        if (rfSize > 0)
        {
            InstLogNum("  resource fork size = ", rfSize);

            /* Open source resource fork */
            if (FSpOpenRF(&srcSpec, fsRdPerm, &srcRF) == noErr)
            {
                /* Create dest resource fork and open it */
                FSpCreateResFile(&dstSpec, fileCreator, fileType,
                                  smSystemScript);
                if (FSpOpenRF(&dstSpec, fsWrPerm, &dstRF) == noErr)
                {
                    long rfRemaining;
                    long rfChunk;
                    Ptr rfBuf;

                    rfBuf = NewPtr(kCopyBufferSize);
                    if (rfBuf != nil)
                    {
                        rfRemaining = rfSize;
                        while (rfRemaining > 0)
                        {
                            rfChunk = rfRemaining;
                            if (rfChunk > kCopyBufferSize)
                                rfChunk = kCopyBufferSize;

                            if (FSRead(srcRF, &rfChunk, rfBuf) != noErr
                                && rfChunk == 0)
                                break;
                            if (FSWrite(dstRF, &rfChunk, rfBuf) != noErr)
                                break;

                            rfRemaining -= rfChunk;
                        }
                        DisposePtr(rfBuf);
                    }
                    FSClose(dstRF);
                }
                FSClose(srcRF);
            }
        }

        /* Copy full Finder info from source (preserves bundle bit,
           custom icon flag, etc.) */
        {
            FInfo srcFInfo;
            if (FSpGetFInfo(&srcSpec, &srcFInfo) == noErr)
            {
                FSpSetFInfo(&dstSpec, &srcFInfo);
            }
        }
    }

    UpdateProgress(fileName, fileSize, fileSize,
                    *overallCopied, overallTotal);
    InstLogStr("CopyOneFile: done ", fileName);
    InstLogNum("  err = ", (long)err);
    return err;
}

/* EjectOnly - physically eject without unmounting.
   Used for Disc 1 (installer's home volume). Unmounting the home
   volume causes the Finder to report "application unexpectedly quit"
   even though everything is in RAM and working. Eject-only keeps
   the volume offline without triggering the Process Manager crash. */
static OSErr EjectOnly(short vRefNum)
{
    OSErr err;
    InstLogNum("EjectOnly: vRefNum = ", (long)vRefNum);
    err = Eject(nil, vRefNum);
    InstLogNum("  Eject err = ", (long)err);
    return err;
}

/* EjectVolume - eject AND unmount. Used for Disc 2 and other
   volumes that the app wasn't launched from. */
static OSErr EjectVolume(short vRefNum)
{
    ParamBlockRec pb;
    OSErr err;

    InstLogNum("EjectVolume: vRefNum = ", (long)vRefNum);

    err = Eject(nil, vRefNum);
    InstLogNum("  Eject err = ", (long)err);

    memset(&pb, 0, sizeof(ParamBlockRec));
    pb.volumeParam.ioVRefNum = vRefNum;
    err = PBUnmountVol((ParmBlkPtr)&pb);
    InstLogNum("  PBUnmountVol err = ", (long)err);

    return err;
}

/*----------------------------------------------------------------------
    CopyInstallerToHD - Copy ourselves to root of startup disk.
    Used for self-relocation so we can freely eject the CD.
----------------------------------------------------------------------*/
static OSErr CopyInstallerToHD(short dstVRefNum, FSSpec *outSpec)
{
    ProcessSerialNumber psn;
    ProcessInfoRec pInfo;
    FSSpec srcSpec, dstSpec;
    OSErr err;
    FInfo srcFInfo;
    short srcRef, dstRef;
    long fileSize, remaining, chunk;
    Ptr buffer;

    srcRef = 0;
    dstRef = 0;
    buffer = nil;
    memset(&srcSpec, 0, sizeof(FSSpec));
    memset(&dstSpec, 0, sizeof(FSSpec));

    /* Get our own FSSpec */
    psn.highLongOfPSN = 0;
    psn.lowLongOfPSN = kCurrentProcess;
    memset(&pInfo, 0, sizeof(ProcessInfoRec));
    pInfo.processInfoLength = sizeof(ProcessInfoRec);
    pInfo.processAppSpec = &srcSpec;
    err = GetProcessInformation(&psn, &pInfo);
    if (err != noErr) return err;

    err = FSpGetFInfo(&srcSpec, &srcFInfo);
    if (err != noErr) return err;

    /* Delete existing copy if present */
    err = FSMakeFSSpec(dstVRefNum, 2, kInstallerCopyName, &dstSpec);
    if (err == noErr) FSpDelete(&dstSpec);

    err = FSpCreate(&dstSpec, srcFInfo.fdCreator, srcFInfo.fdType,
                     smSystemScript);
    if (err != noErr) return err;

    buffer = NewPtr(kCopyBufferSize);
    if (buffer == nil) { FSpDelete(&dstSpec); return memFullErr; }

    /* Copy data fork */
    err = FSpOpenDF(&srcSpec, fsRdPerm, &srcRef);
    if (err != noErr)
    { DisposePtr(buffer); FSpDelete(&dstSpec); return err; }

    err = FSpOpenDF(&dstSpec, fsWrPerm, &dstRef);
    if (err != noErr)
    { FSClose(srcRef); DisposePtr(buffer); FSpDelete(&dstSpec); return err; }

    GetEOF(srcRef, &fileSize);
    remaining = fileSize;
    while (remaining > 0)
    {
        chunk = remaining;
        if (chunk > kCopyBufferSize) chunk = kCopyBufferSize;
        err = FSRead(srcRef, &chunk, buffer);
        if (err != noErr && err != eofErr) break;
        err = FSWrite(dstRef, &chunk, buffer);
        if (err != noErr) break;
        remaining -= chunk;
    }
    FSClose(srcRef);
    FSClose(dstRef);
    if (err == eofErr) err = noErr;

    /* Copy resource fork */
    if (err == noErr)
    {
        CInfoPBRec rfPB;
        Str255 rfName;
        long rfSize;
        short srcRF, dstRF;

        rfSize = 0;
        srcRF = 0;
        dstRF = 0;
        BlockMoveData(srcSpec.name, rfName, srcSpec.name[0] + 1);
        memset(&rfPB, 0, sizeof(CInfoPBRec));
        rfPB.hFileInfo.ioNamePtr = rfName;
        rfPB.hFileInfo.ioVRefNum = srcSpec.vRefNum;
        rfPB.hFileInfo.ioDirID = srcSpec.parID;
        if (PBGetCatInfoSync(&rfPB) == noErr)
            rfSize = rfPB.hFileInfo.ioFlRLgLen;

        if (rfSize > 0)
        {
            if (FSpOpenRF(&srcSpec, fsRdPerm, &srcRF) == noErr)
            {
                FSpCreateResFile(&dstSpec, srcFInfo.fdCreator,
                                  srcFInfo.fdType, smSystemScript);
                if (FSpOpenRF(&dstSpec, fsWrPerm, &dstRF) == noErr)
                {
                    long rfRemaining, rfChunk;

                    rfRemaining = rfSize;
                    while (rfRemaining > 0)
                    {
                        rfChunk = rfRemaining;
                        if (rfChunk > kCopyBufferSize)
                            rfChunk = kCopyBufferSize;
                        if (FSRead(srcRF, &rfChunk, buffer) != noErr
                            && rfChunk == 0)
                            break;
                        if (FSWrite(dstRF, &rfChunk, buffer) != noErr)
                            break;
                        rfRemaining -= rfChunk;
                    }
                    FSClose(dstRF);
                }
                FSClose(srcRF);
            }
        }
    }

    DisposePtr(buffer);

    if (err == noErr)
        FSpSetFInfo(&dstSpec, &srcFInfo);

    *outSpec = dstSpec;
    return err;
}

static Boolean WaitForVolume(ConstStr255Param volName, short *vRefNum)
{
    HParamBlockRec pb;
    Str255 name;
    EventRecord event;
    short index;
    long waitCount;
    Boolean found;

    found = false;
    waitCount = 0;

    while (!found && waitCount < 600)
    {
        index = 1;
        while (true)
        {
            memset(&pb, 0, sizeof(HParamBlockRec));
            name[0] = 0;
            pb.volumeParam.ioNamePtr = name;
            pb.volumeParam.ioVolIndex = index;
            if (PBHGetVInfoSync(&pb) != noErr) break;

            if (EqualString(name, volName, false, true))
            {
                *vRefNum = pb.volumeParam.ioVRefNum;
                found = true;
                InstLogStr("WaitForVolume: found ", volName);
                InstLogNum("  vRefNum = ", (long)*vRefNum);
                break;
            }
            index++;
        }

        if (!found)
        {
            if (GetNextEvent(everyEvent, &event))
            {
                if (event.what == updateEvt)
                { BeginUpdate((WindowPtr)event.message); EndUpdate((WindowPtr)event.message); }
                if (event.what == keyDown &&
                    (event.message & charCodeMask) == 0x1B)
                    return false;
            }
            SystemTask();
            {
                long t;
                t = TickCount();
                while (TickCount() - t < 60) SystemTask();
            }
            waitCount++;
        }
    }
    return found;
}

static OSErr MakeDesktopAlias(short appVRefNum, long appDirID,
                               ConstStr255Param appName)
{
    OSErr err;
    FSSpec appSpec, aliasSpec;
    AliasHandle alias;
    short desktopVRefNum;
    long desktopDirID;
    short aliasRef;
    short saveResFile;
    FInfo fInfo;

    alias = nil;
    aliasRef = 0;

    /* Find the installed app */
    err = FSMakeFSSpec(appVRefNum, appDirID, appName, &appSpec);
    if (err != noErr)
    {
        InstLogNum("Alias: FSMakeFSSpec app failed, err = ", (long)err);
        return err;
    }
    InstLog("Alias: found app OK");

    /* Create alias record pointing to the app */
    err = NewAlias(nil, &appSpec, &alias);
    if (err != noErr)
    {
        InstLogNum("Alias: NewAlias failed, err = ", (long)err);
        return err;
    }
    InstLogNum("Alias: NewAlias OK, handle size = ",
               GetHandleSize((Handle)alias));

    /* Find Desktop folder */
    err = FindFolder(kOnSystemDisk, kDesktopFolderType,
                     false, &desktopVRefNum, &desktopDirID);
    if (err != noErr)
    {
        InstLogNum("Alias: FindFolder Desktop failed, err = ", (long)err);
        DisposeHandle((Handle)alias);
        return err;
    }

    /* Delete existing alias if present */
    err = FSMakeFSSpec(desktopVRefNum, desktopDirID, appName, &aliasSpec);
    if (err == noErr)
    {
        InstLog("Alias: deleting existing file on Desktop");
        FSpDelete(&aliasSpec);
    }

    /* Create the alias file */
    err = FSpCreate(&aliasSpec, kAppCreator, kAppType, smSystemScript);
    if (err != noErr)
    {
        InstLogNum("Alias: FSpCreate failed, err = ", (long)err);
        DisposeHandle((Handle)alias);
        return err;
    }
    InstLog("Alias: file created on Desktop");

    /* Save current resource file, create resource fork on alias */
    saveResFile = CurResFile();

    FSpCreateResFile(&aliasSpec, kAppCreator, kAppType, smSystemScript);
    err = ResError();
    if (err != noErr && err != dupFNErr)
    {
        InstLogNum("Alias: FSpCreateResFile failed, err = ", (long)err);
        DisposeHandle((Handle)alias);
        return err;
    }

    aliasRef = FSpOpenResFile(&aliasSpec, fsWrPerm);
    if (aliasRef == -1)
    {
        err = ResError();
        InstLogNum("Alias: FSpOpenResFile failed, err = ", (long)err);
        DisposeHandle((Handle)alias);
        return err;
    }
    InstLogNum("Alias: resource file opened, ref = ", (long)aliasRef);

    /* Make the alias resource file current before AddResource */
    UseResFile(aliasRef);

    AddResource((Handle)alias, rAliasType, 0, appName);
    err = ResError();
    if (err != noErr)
    {
        InstLogNum("Alias: AddResource failed, err = ", (long)err);
        CloseResFile(aliasRef);
        UseResFile(saveResFile);
        return err;
    }

    WriteResource((Handle)alias);
    err = ResError();
    InstLogNum("Alias: WriteResource err = ", (long)err);

    CloseResFile(aliasRef);
    UseResFile(saveResFile);

    /* Copy the TARGET app's Finder info to the alias file.
       This inherits the bundle bit, custom icon flag, type, and
       creator, so the alias displays the same icon as the app.
       Then add kIsAlias so the Finder treats it as an alias. */
    err = FSpGetFInfo(&appSpec, &fInfo);
    if (err == noErr)
    {
        fInfo.fdFlags |= kIsAlias;
        err = FSpSetFInfo(&aliasSpec, &fInfo);
        InstLogNum("Alias: SetFInfo flags err = ", (long)err);
        InstLogNum("Alias: fdFlags = ", (long)fInfo.fdFlags);
    }

    InstLog("Alias: creation complete");
    return noErr;
}

/*----------------------------------------------------------------------
    main
----------------------------------------------------------------------*/
void main(void)
{
    OSErr err;
    Boolean isPPC;
    Boolean isRelocated;
    ModelChoices choices;
    short cdVRefNum, dstVRefNum;
    long cdRootID, cdDataID, cdAppDirID, cdModelsDirID;
    long dstDirID;
    short disc2VRefNum;
    long disc2RootID, disc2ModelsID;
    long overallCopied, overallTotal;
    Boolean disc1Ejected;
    FSSpec selfSpec;

    cdVRefNum = 0;
    dstVRefNum = 0;
    cdRootID = 0;
    cdDataID = 0;
    cdAppDirID = 0;
    cdModelsDirID = 0;
    dstDirID = 0;
    disc2VRefNum = 0;
    disc2RootID = 0;
    disc2ModelsID = 0;
    overallCopied = 0;
    overallTotal = 0;
    disc1Ejected = false;
    isRelocated = false;
    memset(&selfSpec, 0, sizeof(FSSpec));
    memset(&choices, 0, sizeof(ModelChoices));

    InitToolbox();

    /* ---- Self-relocation ----
       If running from a non-startup volume (CD), copy ourselves to
       the root of the startup disk and relaunch. The HD copy can
       freely eject/unmount CDs without triggering "quit unexpectedly"
       or "please insert disc" dialogs from the Finder. */
    {
        short myVRefNum, startupVRefNum;
        long myDirID, startupDirID;

        myVRefNum = 0;
        startupVRefNum = 0;
        myDirID = 0;
        startupDirID = 0;

        err = GetCDRoot(&myVRefNum, &myDirID);
        if (err == noErr)
        {
            FindFolder(kOnSystemDisk, kSystemFolderType,
                       false, &startupVRefNum, &startupDirID);

            if (myVRefNum != startupVRefNum)
            {
                /* Running from CD, copy self to HD root and relaunch.
                   Use launchContinue so the copy gets its own fresh
                   process entry (with HD as launch volume). Then we
                   exit cleanly via ExitToShell. */
                FSSpec copySpec;
                LaunchParamBlockRec lpb;

                memset(&copySpec, 0, sizeof(FSSpec));
                memset(&lpb, 0, sizeof(LaunchParamBlockRec));

                err = CopyInstallerToHD(startupVRefNum, &copySpec);
                if (err == noErr)
                {
                    lpb.launchBlockID = extendedBlock;
                    lpb.launchEPBLength = extendedBlockLen;
                    lpb.launchFileFlags = 0;
                    lpb.launchControlFlags =
                        launchContinue | launchNoFileFlags;
                    lpb.launchAppSpec = &copySpec;
                    err = LaunchApplication(&lpb);
                    if (err == noErr)
                    {
                        ExitToShell();
                        return;
                    }
                }
                /* Copy or launch failed, cannot continue from CD */
                ShowErrorScreen("\pFailed to prepare installer.");
                ExitToShell();
                return;
            }
            else
            {
                /* Running from startup disk, we are the relocated copy */
                isRelocated = true;
                {
                    ProcessSerialNumber psn;
                    ProcessInfoRec pInfo;

                    psn.highLongOfPSN = 0;
                    psn.lowLongOfPSN = kCurrentProcess;
                    memset(&pInfo, 0, sizeof(ProcessInfoRec));
                    pInfo.processInfoLength = sizeof(ProcessInfoRec);
                    pInfo.processAppSpec = &selfSpec;
                    GetProcessInformation(&psn, &pInfo);
                }
                InstLog("Running as relocated copy from HD");
                InstLogNum("  selfSpec.vRefNum = ", (long)selfSpec.vRefNum);
                InstLogNum("  selfSpec.parID = ", selfSpec.parID);
            }
        }
    }

    ShowSplashScreen();

    InstLogNum("Architecture: isPPC = ", (long)IsPowerPC());

    if (!ShowWelcomeScreen())
    {
        if (isRelocated) FSpDelete(&selfSpec);
        ExitToShell();
        return;
    }

    if (!ShowModelSelect(&choices))
    {
        if (isRelocated) FSpDelete(&selfSpec);
        ExitToShell();
        return;
    }

    /* ---- Find CD ---- */
    if (isRelocated)
    {
        /* Find Disc 1 by volume name */
        HParamBlockRec vpb;
        Str255 vname;
        short vindex;
        Boolean disc1Found;

        disc1Found = false;
        vindex = 1;
        while (true)
        {
            memset(&vpb, 0, sizeof(HParamBlockRec));
            vname[0] = 0;
            vpb.volumeParam.ioNamePtr = vname;
            vpb.volumeParam.ioVolIndex = vindex;
            if (PBHGetVInfoSync(&vpb) != noErr) break;
            if (EqualString(vname, kDisc1VolName, false, true))
            {
                cdVRefNum = vpb.volumeParam.ioVRefNum;
                disc1Found = true;
                InstLogNum("Found Disc 1, vRefNum = ", (long)cdVRefNum);
                break;
            }
            vindex++;
        }

        if (!disc1Found)
        {
            ShowErrorScreen("\pMacinAI Disc 1 not found.");
            FSpDelete(&selfSpec);
            ExitToShell();
            return;
        }
        cdRootID = 2;  /* Root directory of CD */
    }
    else
    {
        /* Running from CD directly (fallback) */
        err = GetCDRoot(&cdVRefNum, &cdRootID);
        if (err != noErr)
        { ShowErrorScreen("\pCould not find installer disc."); ExitToShell(); return; }
    }

    err = FindSubfolder(cdVRefNum, cdRootID, kDataFolder, &cdDataID);
    if (err != noErr)
    { ShowErrorScreen("\pData folder not found on disc."); ExitToShell(); return; }

    isPPC = IsPowerPC();
    err = FindSubfolder(cdVRefNum, cdDataID,
                         isPPC ? kAppPPCFolder : kApp68KFolder, &cdAppDirID);
    if (err != noErr)
    { ShowErrorScreen("\pApp folder not found on disc."); ExitToShell(); return; }

    err = FindSubfolder(cdVRefNum, cdDataID, kModelsFolder, &cdModelsDirID);
    if (err != noErr)
    { ShowErrorScreen("\pModels folder not found on disc."); ExitToShell(); return; }

    err = CreateInstallFolder(&dstVRefNum, &dstDirID);
    if (err != noErr)
    { ShowErrorScreen("\pCould not create folder on Desktop."); ExitToShell(); return; }

    /* Create Models subfolder inside install folder */
    {
        long modelsDirID;
        modelsDirID = 0;
        err = DirCreate(dstVRefNum, dstDirID, "\pModels", &modelsDirID);
        if (err == dupFNErr)
        {
            /* Already exists */
            FSSpec mf;
            CInfoPBRec mpb;
            err = FSMakeFSSpec(dstVRefNum, dstDirID, "\pModels", &mf);
            if (err == noErr)
            {
                memset(&mpb, 0, sizeof(CInfoPBRec));
                mpb.dirInfo.ioNamePtr = mf.name;
                mpb.dirInfo.ioVRefNum = mf.vRefNum;
                mpb.dirInfo.ioDrDirID = mf.parID;
                if (PBGetCatInfoSync(&mpb) == noErr)
                    modelsDirID = mpb.dirInfo.ioDrDirID;
            }
            err = noErr;
        }
        if (modelsDirID != 0)
        {
            gModelsDirID = modelsDirID;
            InstLog("Created Models subfolder in install folder");
        }
        else
        {
            /* Fallback: install models to root of install folder */
            gModelsDirID = dstDirID;
            InstLog("Models subfolder failed, using install root");
        }
    }

    /* Calculate total bytes */
    overallTotal = GetFileSize(cdVRefNum, cdAppDirID, kAppFileName);
    if (choices.macinaiTool)
        overallTotal += GetFileSize(cdVRefNum, cdModelsDirID, kModel1Name);
    if (choices.gpt2)
        overallTotal += GetFileSize(cdVRefNum, cdModelsDirID, kModel2Name);
    if (choices.smolLM)
        overallTotal += GetFileSize(cdVRefNum, cdModelsDirID, kModel3Name);
    if (choices.qwen)
        overallTotal += (long)kModel4SizeMB * 1024L * 1024L;

    InstLogNum("Total bytes to copy: ", overallTotal);

    /* Copy! */
    ShowProgressScreen();

    err = CopyOneFile(cdVRefNum, cdAppDirID, dstVRefNum, dstDirID,
                       kAppFileName, kAppType, kAppCreator,
                       &overallCopied, overallTotal);
    if (err != noErr)
    { CloseProgressScreen(); ShowErrorScreen("\pFailed to copy application."); ExitToShell(); return; }

    if (choices.macinaiTool)
    {
        err = CopyOneFile(cdVRefNum, cdModelsDirID, dstVRefNum, gModelsDirID,
                           kModel1Name, kModelType, kModelCreator,
                           &overallCopied, overallTotal);
        if (err != noErr)
        { CloseProgressScreen(); ShowErrorScreen("\pFailed to copy MacinAI Tool."); ExitToShell(); return; }
    }

    if (choices.gpt2)
    {
        err = CopyOneFile(cdVRefNum, cdModelsDirID, dstVRefNum, gModelsDirID,
                           kModel2Name, kModelType, kModelCreator,
                           &overallCopied, overallTotal);
        if (err != noErr)
        { CloseProgressScreen(); ShowErrorScreen("\pFailed to copy GPT-2."); ExitToShell(); return; }
    }

    if (choices.smolLM)
    {
        err = CopyOneFile(cdVRefNum, cdModelsDirID, dstVRefNum, gModelsDirID,
                           kModel3Name, kModelType, kModelCreator,
                           &overallCopied, overallTotal);
        if (err != noErr)
        { CloseProgressScreen(); ShowErrorScreen("\pFailed to copy SmolLM."); ExitToShell(); return; }
    }

    /* Disc 2 */
    if (choices.qwen)
    {
        CloseProgressScreen();

        InstLog("Disc swap: asking user for Disc 2");
        if (ShowDiscSwapScreen())
        {
            /* Eject and unmount Disc 1 AFTER user clicks OK.
               CODE resources are detached and locked in RAM (InitToolbox).
               Close resource file first so volume has no open files.
               Both Eject and PBUnmountVol return 0 (confirmed by log).
               Only eject if this looks like a CD (not the startup disk). */
            {
                short startupVRefNum;
                long startupDirID;
                startupVRefNum = 0;
                FindFolder(kOnSystemDisk, kSystemFolderType,
                           false, &startupVRefNum, &startupDirID);
                if (cdVRefNum != startupVRefNum)
                {
                    InstLog("Ejecting and unmounting Disc 1...");
                    {
                        short appResFile;
                        appResFile = CurResFile();
                        if (appResFile != 0)
                            CloseResFile(appResFile);
                    }
                    EjectVolume(cdVRefNum);
                    disc1Ejected = true;
                }
                else
                {
                    InstLog("Skipped eject (testing from startup disk)");
                }
            }

            InstLog("Disc swap: waiting for volume...");
            ShowProgressScreen();

            /* Show "Waiting for Disc 2" while polling */
            if (gProgressWind != nil)
            {
                GrafPtr savePort;
                GetPort(&savePort);
                SetPort(gProgressWind);
                EraseRect(&gProgressWind->portRect);
                TextFont(3); TextSize(12); TextFace(0);
                DrawCenteredString(kProgressWidth, 30,
                    "\pWaiting for MacinAI Disc 2...");
                TextSize(10);
                DrawCenteredString(kProgressWidth, 55,
                    "\pPlease insert the disc now.");
                SetPort(savePort);
            }

            if (WaitForVolume(kDisc2VolName, &disc2VRefNum))
            {
                disc2RootID = 2;
                err = FindSubfolder(disc2VRefNum, disc2RootID,
                                     kModelsFolder, &disc2ModelsID);
                if (err != noErr)
                {
                    long disc2DataID;
                    disc2DataID = 0;
                    err = FindSubfolder(disc2VRefNum, disc2RootID,
                                         kDataFolder, &disc2DataID);
                    if (err == noErr)
                        err = FindSubfolder(disc2VRefNum, disc2DataID,
                                             kModelsFolder, &disc2ModelsID);
                }

                if (err == noErr)
                {
                    long qwenSize;
                    qwenSize = GetFileSize(disc2VRefNum, disc2ModelsID,
                                            kModel4Name);
                    overallTotal = overallCopied + qwenSize;

                    err = CopyOneFile(disc2VRefNum, disc2ModelsID,
                                       dstVRefNum, gModelsDirID,
                                       kModel4Name, kModelType, kModelCreator,
                                       &overallCopied, overallTotal);
                }
                EjectVolume(disc2VRefNum);
            }
            CloseProgressScreen();
        }
    }
    else
    {
        CloseProgressScreen();
    }

    /* Create a Desktop alias pointing to the installed app */
    {
        OSErr aliasErr;
        aliasErr = MakeDesktopAlias(dstVRefNum, dstDirID, kAppFileName);
        if (aliasErr == noErr)
            InstLog("Desktop alias created");
        else
            InstLogNum("Desktop alias failed, err = ", (long)aliasErr);
    }

    InstLog("Installation complete!");
    InstLogNum("Total bytes copied: ", overallCopied);

    ShowCompleteScreen();

    /* Self-delete the relocated installer copy from the HD root.
       CODE resources are detached and in RAM, resource file was closed
       during disc swap. No file forks are open, so FSpDelete works. */
    if (isRelocated && selfSpec.vRefNum != 0)
    {
        OSErr delErr;

        /* Close resource file if still open (skipped if no disc swap) */
        {
            short appResFile;
            appResFile = CurResFile();
            if (appResFile > 0)
                CloseResFile(appResFile);
        }
        delErr = FSpDelete(&selfSpec);
        InstLogNum("Self-delete err = ", (long)delErr);
    }

    /* Close log before exit */
    if (gLogRefNum != 0)
    {
        FSClose(gLogRefNum);
        gLogRefNum = 0;
    }

    ExitToShell();
}
