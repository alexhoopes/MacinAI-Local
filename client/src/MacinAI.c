/*------------------------------------------------------------------------------
    MacinAI.c - Main Application for MacinAI Local

    Self-contained AI assistant for vintage Macintosh.
    No network, no relay, the model runs natively on the Mac.

    Screens:
    1. Splash - Welcome + model loading
    2. Main   - Chat interface with local inference

    Built with CodeWarrior Pro 5 on System 7.5.3+

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

#include "AppGlobals.h"
#include "SafeString.h"
#include "SplashScreen.h"
#include "ChatWindow.h"
#include "SettingsDialog.h"
#include "DrawingHelpers.h"
#include "AppVersion.h"
#include "ConversationManager.h"
#include "SystemDiscovery.h"
#include "ActionDispatcher.h"
#include "DialogUtils.h"
#include "InferenceEngine.h"
#include "QueryRouter.h"
#include "Tokenizer.h"
#include "SpeechManager.h"
#include "DebugLog.h"
#include "DemoMode.h"

#include <Types.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Events.h>
#include <Menus.h>
#include <Windows.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Controls.h>
#include <ToolUtils.h>
#include <Processes.h>
#include <Sound.h>
#include <StandardFile.h>
#include <Files.h>
#include <Folders.h>
#include <AppleEvents.h>
#include <string.h>
#include <stdio.h>

#pragma segment Main

/*------------------------------------------------------------------------------
    Global Application Data
------------------------------------------------------------------------------*/
AppGlobals gApp;

/*------------------------------------------------------------------------------
    Function Prototypes
------------------------------------------------------------------------------*/
void Initialize(void);
void SetupMenus(void);
void InstallAppleEventHandlers(void);
pascal OSErr HandleQuitAppleEvent(const AppleEvent *event, AppleEvent *reply, long refcon);
void EventLoop(void);
void DoEvent(EventRecord *event);
void HandleMouseDown(EventRecord *event);
void HandleKeyDown(EventRecord *event);
void HandleMenuChoice(long menuChoice);
void HandleAppleMenu(short item);
void HandleFileMenu(short item);
void HandleEditMenu(short item);
void TransitionToState(AppState newState);
void Cleanup(void);
void LoadSettings(void);
void SaveSettings(void);
void HandleOpenModel(void);
void AutoDetectModel(void);
static Boolean ValidateModelFile(const FSSpec *fileSpec);

/*------------------------------------------------------------------------------
    AppBeep - Conditional beep
------------------------------------------------------------------------------*/
void AppBeep(void)
{
    if (gApp.settings.beepsEnabled)
        SysBeep(5);
}

/*------------------------------------------------------------------------------
    main
------------------------------------------------------------------------------*/
void main(void)
{
    Initialize();
    EventLoop();
    Cleanup();
}

/*------------------------------------------------------------------------------
    Initialize
------------------------------------------------------------------------------*/
void Initialize(void)
{
    /* Initialize Mac Toolbox */
    InitGraf(&gApp.qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(nil);
    InitCursor();

    /* Clear application state */
    gApp.currentWindow = nil;
    gApp.appState = kAppStateSplash;
    gApp.done = false;
    gApp.statusText[0] = '\0';

    /* Initialize choice state */
    gApp.choiceState.awaitingChoice = false;
    gApp.choiceState.pendingAction = kPendingNone;
    gApp.choiceState.resultCount = 0;

    /* Load settings */
    LoadSettings();

    /* Initialize debug logging - always on during startup for diagnostics */
    DebugLog_Init();
    DebugLog_SetEnabled(true);

    /* Set up menus */
    SetupMenus();

    /* Install Apple Event handlers */
    InstallAppleEventHandlers();

    /* Initialize conversation manager */
    ConvMgr_Init();

    /* Initialize action dispatcher */
    ActionDispatcher_Initialize();

    /* Show splash screen (engine init happens there with progress bar) */
    TransitionToState(kAppStateSplash);
}

/*------------------------------------------------------------------------------
    SetupMenus
------------------------------------------------------------------------------*/
void SetupMenus(void)
{
    MenuHandle appleMenu, fileMenu, editMenu;

    /* Apple menu */
    appleMenu = NewMenu(mApple, "\p\024");
    AppendMenu(appleMenu, "\pAbout MacinAI Local...;(-");
    AppendResMenu(appleMenu, 'DRVR');
    InsertMenu(appleMenu, 0);

    /* File menu */
    fileMenu = NewMenu(mFile, "\pFile");
    AppendMenu(fileMenu, "\pNew/N;Close/W;(-;Open.../O;(-;Save/S;Save As...;(-;Quit/Q");
    InsertMenu(fileMenu, 0);

    /* Edit menu */
    editMenu = NewMenu(mEdit, "\pEdit");
    AppendMenu(editMenu, "\pUndo/Z;(-;Cut/X;Copy/C;Paste/V;Clear;(-;Select All/A;(-;Settings...");
    InsertMenu(editMenu, 0);

    DrawMenuBar();
}

/*------------------------------------------------------------------------------
    Apple Event Handlers
------------------------------------------------------------------------------*/
pascal OSErr HandleQuitAppleEvent(const AppleEvent *event, AppleEvent *reply, long refcon)
{
    #pragma unused(event, reply, refcon)
    gApp.done = true;
    return noErr;
}

void InstallAppleEventHandlers(void)
{
    AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
                          NewAEEventHandlerProc(HandleQuitAppleEvent),
                          0, false);
}

/*------------------------------------------------------------------------------
    EventLoop
------------------------------------------------------------------------------*/
void EventLoop(void)
{
    EventRecord event;

    while (!gApp.done)
    {
        if (GetNextEvent(everyEvent, &event))
        {
            DoEvent(&event);
        }

        /* CRITICAL: SystemTask outside if block */
        SystemTask();

        /* Idle processing */
        if (gApp.appState == kAppStateMain)
        {
            ChatScreen_HandleIdle();
        }
    }
}

/*------------------------------------------------------------------------------
    DoEvent
------------------------------------------------------------------------------*/
void DoEvent(EventRecord *event)
{
    Boolean handled = false;

    /* Route to current screen first */
    switch (gApp.appState)
    {
        case kAppStateSplash:
            handled = SplashScreen_HandleEvent(event);
            break;
        case kAppStateMain:
            handled = ChatScreen_HandleEvent(event);
            break;
        case kAppStateQuitting:
            break;
    }

    if (handled)
        return;

    /* Global event handling */
    switch (event->what)
    {
        case mouseDown:
            HandleMouseDown(event);
            break;

        case keyDown:
        case autoKey:
            HandleKeyDown(event);
            break;

        case updateEvt:
        {
            WindowPtr updateWindow = (WindowPtr)event->message;
            BeginUpdate(updateWindow);
            EndUpdate(updateWindow);
            break;
        }

        case kHighLevelEvent:
            AEProcessAppleEvent(event);
            break;
    }
}

/*------------------------------------------------------------------------------
    HandleMouseDown
------------------------------------------------------------------------------*/
void HandleMouseDown(EventRecord *event)
{
    WindowPtr window;
    short part;

    part = FindWindow(event->where, &window);

    switch (part)
    {
        case inMenuBar:
            HandleMenuChoice(MenuSelect(event->where));
            break;

        case inSysWindow:
            SystemClick(event, window);
            break;
    }
}

/*------------------------------------------------------------------------------
    HandleKeyDown
------------------------------------------------------------------------------*/
void HandleKeyDown(EventRecord *event)
{
    char key;
    long menuResult;

    key = event->message & charCodeMask;

    if (event->modifiers & cmdKey)
    {
        menuResult = MenuKey(key);
        if (HiWord(menuResult) != 0)
        {
            HandleMenuChoice(menuResult);
        }
        else if ((key == 't' || key == 'T') &&
                 gApp.appState == kAppStateMain)
        {
            /* Cmd+T: Toggle demo mode */
            if (DemoMode_IsRunning())
                DemoMode_Stop();
            else
                DemoMode_Start();
        }
    }
}

/*------------------------------------------------------------------------------
    HandleMenuChoice
------------------------------------------------------------------------------*/
void HandleMenuChoice(long menuChoice)
{
    short menuID, item;

    menuID = HiWord(menuChoice);
    item = LoWord(menuChoice);

    switch (menuID)
    {
        case mApple:
            HandleAppleMenu(item);
            break;
        case mFile:
            HandleFileMenu(item);
            break;
        case mEdit:
            HandleEditMenu(item);
            break;
    }

    HiliteMenu(0);
}

/*------------------------------------------------------------------------------
    HandleAppleMenu
------------------------------------------------------------------------------*/
void HandleAppleMenu(short item)
{
    if (item == iAbout)
    {
        /* Show About dialog */
        char aboutMsg[256];
        const char *ver = GetFullVersionString();
        sprintf(aboutMsg, "%s\nAI on your Mac. No internet required.", ver);
        ShowMessageDialog(aboutMsg);
    }
    else
    {
        /* Desk Accessory */
        Str255 daName;
        GetMenuItemText(GetMenuHandle(mApple), item, daName);
        OpenDeskAcc(daName);
    }
}

/*------------------------------------------------------------------------------
    HandleFileMenu
------------------------------------------------------------------------------*/
void HandleFileMenu(short item)
{
    switch (item)
    {
        case iNew:
            if (gApp.appState == kAppStateMain)
            {
                ConvMgr_CreateNew(ChatWindow_GetOutputTE());
                ChatScreen_UpdateTitle();
            }
            break;

        case iClose:
            gApp.done = true;
            break;

        case iSave:
            if (gApp.appState == kAppStateMain)
                ChatScreen_HandleSave();
            break;

        case iQuit:
            /* Check for unsaved changes */
            if (ConvMgr_HasUnsavedChanges())
            {
                short result = ShowSavePrompt(ChatScreen_GetWindow());
                if (result == 1)  /* Save */
                    ChatScreen_HandleSave();
                else if (result == 3)  /* Cancel */
                    return;
            }
            gApp.done = true;
            break;
    }
}

/*------------------------------------------------------------------------------
    HandleEditMenu
------------------------------------------------------------------------------*/
void HandleEditMenu(short item)
{
    /* Try system edit first (for DAs) */
    if (SystemEdit(item - 1))
        return;

    if (gApp.appState != kAppStateMain)
        return;

    switch (item)
    {
        case iCut:
            ChatWindow_HandleCut();
            break;
        case iCopy:
            ChatWindow_HandleCopy();
            break;
        case iPaste:
            ChatWindow_HandlePaste();
            break;
        case iClear:
            ChatWindow_HandleClear();
            break;
        case iSelectAll:
            ChatWindow_HandleSelectAll();
            break;
        case iSettings:
            SettingsDialog_Show();
            break;
    }
}

/*------------------------------------------------------------------------------
    TransitionToState
------------------------------------------------------------------------------*/
void TransitionToState(AppState newState)
{
    /* Cleanup current state */
    switch (gApp.appState)
    {
        case kAppStateSplash:
            SplashScreen_Close();
            /* After splash, respect user's debug logging setting */
            if (!gApp.settings.debugLogging)
                DebugLog_SetEnabled(false);
            break;
        case kAppStateMain:
            ChatScreen_Close();
            break;
        case kAppStateQuitting:
            break;
    }

    /* Enter new state */
    gApp.appState = newState;
    switch (newState)
    {
        case kAppStateSplash:
            SplashScreen_Show();
            break;
        case kAppStateMain:
            ChatScreen_Show();
            break;
        case kAppStateQuitting:
            gApp.done = true;
            break;
    }
}

/*------------------------------------------------------------------------------
    AutoDetectModel - Look for model file in app's folder on startup
    Checks for the default model filename in the same directory as the app.
------------------------------------------------------------------------------*/
void AutoDetectModel(void)
{
    ProcessSerialNumber psn;
    ProcessInfoRec info;
    FSSpec appSpec;
    FSSpec modelSpec;
    OSErr err;
    Str255 modelName;
    short nameLen;
    CInfoPBRec pb;
    short idx;
    Boolean found;
    long modelsDirID;
    short searchVRefNum;
    long searchDirID;

    DebugLog_Write("AutoDetectModel: starting");

    /* Get the application's FSSpec via Process Manager */
    psn.highLongOfPSN = 0;
    psn.lowLongOfPSN = kCurrentProcess;
    info.processInfoLength = sizeof(ProcessInfoRec);
    info.processName = nil;
    info.processAppSpec = &appSpec;

    err = GetProcessInformation(&psn, &info);
    if (err != noErr)
    {
        DebugLog_WriteNum("AutoDetectModel: GetProcessInformation failed", (long)err);
        return;
    }

    DebugLog_Write("AutoDetectModel: got app location");

    /* Ensure "Models" subfolder exists next to the app.
       If it doesn't exist, create it so the user knows where to put models. */
    modelsDirID = 0;
    {
        FSSpec modelsFolder;
        CInfoPBRec folderPB;

        err = FSMakeFSSpec(appSpec.vRefNum, appSpec.parID,
                           "\pModels", &modelsFolder);
        if (err == noErr)
        {
            /* Check it's actually a folder */
            memset(&folderPB, 0, sizeof(CInfoPBRec));
            folderPB.dirInfo.ioNamePtr = modelsFolder.name;
            folderPB.dirInfo.ioVRefNum = modelsFolder.vRefNum;
            folderPB.dirInfo.ioDrDirID = modelsFolder.parID;
            if (PBGetCatInfoSync(&folderPB) == noErr &&
                (folderPB.dirInfo.ioFlAttrib & 0x10))
            {
                modelsDirID = folderPB.dirInfo.ioDrDirID;
                DebugLog_Write("AutoDetectModel: Models folder found");
            }
        }
        else
        {
            /* Create the Models folder */
            err = DirCreate(appSpec.vRefNum, appSpec.parID,
                            "\pModels", &modelsDirID);
            if (err == noErr)
                DebugLog_Write("AutoDetectModel: created Models folder");
            else
                DebugLog_WriteNum("AutoDetectModel: DirCreate Models failed =", (long)err);
        }
    }

    /* Search priority: Models subfolder first, then app root */
    /* Try Models folder first if it exists */
    if (modelsDirID != 0)
    {
        searchVRefNum = appSpec.vRefNum;
        searchDirID = modelsDirID;
        DebugLog_Write("AutoDetectModel: searching Models folder first");
    }
    else
    {
        searchVRefNum = appSpec.vRefNum;
        searchDirID = appSpec.parID;
        DebugLog_Write("AutoDetectModel: searching app folder");
    }

    /* Try 1: Look for default filename in search dir */
    nameLen = strlen(gApp.settings.modelFileName);
    if (nameLen > 255) nameLen = 255;
    modelName[0] = nameLen;
    BlockMoveData(gApp.settings.modelFileName, &modelName[1], nameLen);

    err = FSMakeFSSpec(searchVRefNum, searchDirID, modelName, &modelSpec);
    if (err == noErr)
    {
        DebugLog_Write("AutoDetectModel: found file by name");
        if (ValidateModelFile(&modelSpec))
        {
            DebugLog_Write("AutoDetectModel: magic valid, loading");
            gApp.model.modelFileSpec = modelSpec;
            gApp.model.modelFileValid = true;
            {
                OSErr loadErr;
                loadErr = Engine_LoadModel(&modelSpec);
                if (loadErr == noErr)
                {
                    OSErr tokErr;
                    Engine_ProgressUpdate(92, "Loading tokenizer...");
                    tokErr = Tokenizer_Initialize(&modelSpec);
                    DebugLog_WriteNum("AutoDetectModel: Tokenizer_Initialize returned", (long)tokErr);
                    if (tokErr == noErr)
                    {
                        Engine_ProgressUpdate(98, "Tokenizer ready.");
                        /* Finalize QueryRouter (sub-modules already init'd, sets sInitialized) */
                        QueryRouter_Initialize(&modelSpec, &modelSpec);
                    }
                    else
                        Engine_ProgressUpdate(98, "Tokenizer failed!");
                }
                else
                {
                    DebugLog_WriteNum("AutoDetectModel: Engine_LoadModel FAILED =", (long)loadErr);
                    Engine_ProgressUpdate(50, "Model load failed!");
                }
            }
            return;
        }
        else
        {
            DebugLog_Write("AutoDetectModel: file exists but magic invalid");
        }
    }
    else
    {
        DebugLog_Write("AutoDetectModel: default filename not found, scanning...");
    }

    /* Try 2: Scan search directory for any file with type 'MCAI' */
    found = false;
    for (idx = 1; idx < 200 && !found; idx++)
    {
        memset(&pb, 0, sizeof(CInfoPBRec));
        modelName[0] = 0;
        pb.hFileInfo.ioNamePtr = modelName;
        pb.hFileInfo.ioVRefNum = searchVRefNum;
        pb.hFileInfo.ioDirID = searchDirID;
        pb.hFileInfo.ioFDirIndex = idx;

        err = PBGetCatInfoSync(&pb);
        if (err != noErr)
            break;  /* No more files */

        /* Skip directories */
        if (pb.hFileInfo.ioFlAttrib & 0x10)
            continue;

        /* Check file type */
        if (pb.hFileInfo.ioFlFndrInfo.fdType == 'MCAI')
        {
            DebugLog_Write("AutoDetectModel: found MCAI file by type");
            err = FSMakeFSSpec(searchVRefNum, searchDirID, modelName, &modelSpec);
            if (err == noErr && ValidateModelFile(&modelSpec))
            {
                /* Update settings with found filename */
                nameLen = modelName[0];
                if (nameLen > 63) nameLen = 63;
                BlockMoveData(&modelName[1], gApp.settings.modelFileName, nameLen);
                gApp.settings.modelFileName[nameLen] = '\0';

                gApp.model.modelFileSpec = modelSpec;
                gApp.model.modelFileValid = true;
                {
                    OSErr loadErr;
                    loadErr = Engine_LoadModel(&modelSpec);
                    if (loadErr == noErr)
                    {
                        OSErr tokErr;
                        Engine_ProgressUpdate(92, "Loading tokenizer...");
                        tokErr = Tokenizer_Initialize(&modelSpec);
                        DebugLog_WriteNum("AutoDetectModel: Tokenizer_Initialize (scan) returned", (long)tokErr);
                        if (tokErr == noErr)
                        {
                            Engine_ProgressUpdate(98, "Tokenizer ready.");
                            QueryRouter_Initialize(&modelSpec, &modelSpec);
                        }
                        else
                            Engine_ProgressUpdate(98, "Tokenizer failed!");
                    }
                    else
                    {
                        DebugLog_WriteNum("AutoDetectModel: Engine_LoadModel (scan) FAILED =", (long)loadErr);
                        Engine_ProgressUpdate(50, "Model load failed!");
                    }
                }
                found = true;
            }
        }
    }

    /* Try 3: If we searched Models folder and found nothing, try app root */
    if (!found && modelsDirID != 0 && searchDirID == modelsDirID)
    {
        DebugLog_Write("AutoDetectModel: not in Models folder, trying app root...");
        searchVRefNum = appSpec.vRefNum;
        searchDirID = appSpec.parID;

        /* Scan app root */
        for (idx = 1; idx < 200 && !found; idx++)
        {
            memset(&pb, 0, sizeof(CInfoPBRec));
            modelName[0] = 0;
            pb.hFileInfo.ioNamePtr = modelName;
            pb.hFileInfo.ioVRefNum = searchVRefNum;
            pb.hFileInfo.ioDirID = searchDirID;
            pb.hFileInfo.ioFDirIndex = idx;

            err = PBGetCatInfoSync(&pb);
            if (err != noErr)
                break;

            if (pb.hFileInfo.ioFlAttrib & 0x10)
                continue;

            if (pb.hFileInfo.ioFlFndrInfo.fdType == 'MCAI')
            {
                DebugLog_Write("AutoDetectModel: found MCAI in app root");
                err = FSMakeFSSpec(searchVRefNum, searchDirID, modelName, &modelSpec);
                if (err == noErr && ValidateModelFile(&modelSpec))
                {
                    nameLen = modelName[0];
                    if (nameLen > 63) nameLen = 63;
                    BlockMoveData(&modelName[1], gApp.settings.modelFileName, nameLen);
                    gApp.settings.modelFileName[nameLen] = '\0';

                    gApp.model.modelFileSpec = modelSpec;
                    gApp.model.modelFileValid = true;
                    {
                        OSErr loadErr;
                        loadErr = Engine_LoadModel(&modelSpec);
                        if (loadErr == noErr)
                        {
                            OSErr tokErr;
                            Engine_ProgressUpdate(92, "Loading tokenizer...");
                            tokErr = Tokenizer_Initialize(&modelSpec);
                            if (tokErr == noErr)
                            {
                                Engine_ProgressUpdate(98, "Tokenizer ready.");
                                QueryRouter_Initialize(&modelSpec, &modelSpec);
                            }
                        }
                    }
                    found = true;
                }
            }
        }
    }

    if (!found)
        DebugLog_Write("AutoDetectModel: no model found in Models folder or app root");
}

/*------------------------------------------------------------------------------
    ValidateModelFile - Check if file has MacinAI magic header
    Returns true if the file starts with 'MCAI' magic bytes.
------------------------------------------------------------------------------*/
static Boolean ValidateModelFile(const FSSpec *fileSpec)
{
    short refNum;
    long count;
    long magic;
    OSErr err;

    err = FSpOpenDF(fileSpec, fsRdPerm, &refNum);
    if (err != noErr)
        return false;

    count = sizeof(long);
    err = FSRead(refNum, &count, &magic);
    FSClose(refNum);

    if (err != noErr || count != sizeof(long))
        return false;

    /* Check for 'MCAI' magic (0x4D434149) */
    return (magic == 'MCAI');
}

/*------------------------------------------------------------------------------
    ModelFileFilter - StandardGetFile filter proc
    Only shows files with type 'MCAI' (MacinAI Model) or validates
    generic files by checking the magic header.
------------------------------------------------------------------------------*/
static pascal Boolean ModelFileFilter(CInfoPBPtr pb)
{
    /* Return true to HIDE the file, false to SHOW it */
    /* Show directories always */
    if (pb->hFileInfo.ioFlAttrib & 0x10)
        return false;

    /* Show files with our creator code */
    if (pb->hFileInfo.ioFlFndrInfo.fdCreator == 'OAS ')
        return false;

    /* Show .bin files */
    if (pb->hFileInfo.ioFlFndrInfo.fdType == 'MCAI')
        return false;

    /* Hide everything else */
    return true;
}

/*------------------------------------------------------------------------------
    HandleOpenModel - Open a MacinAI model file
------------------------------------------------------------------------------*/
void HandleOpenModel(void)
{
    SFReply reply;
    SFTypeList types;
    Point where;
    FSSpec fileSpec;
    char statusMsg[128];
    OSErr err;

    /* Center the dialog on screen */
    where.h = (gApp.qd.screenBits.bounds.right - 344) / 2;
    where.v = (gApp.qd.screenBits.bounds.bottom - 200) / 3;

    /* Show classic Get File dialog (smaller than StandardGetFile) */
    types[0] = 'MCAI';
    SFGetFile(where, "\pSelect a MacinAI model:", nil, 1, types, nil, &reply);

    if (!reply.good)
        return;

    /* Convert to FSSpec */
    err = FSMakeFSSpec(reply.vRefNum, 0, reply.fName, &fileSpec);
    if (err != noErr)
        return;

    /* Validate the magic header */
    if (!ValidateModelFile(&fileSpec))
    {
        ParamText("\pThis is not a valid MacinAI model file.", "\p", "\p", "\p");
        Alert(128, nil);
        return;
    }

    /* Store the model file spec */
    gApp.model.modelFileSpec = fileSpec;
    gApp.model.modelFileValid = true;

    /* Extract filename for settings */
    {
        short nameLen;
        nameLen = fileSpec.name[0];
        if (nameLen > 63) nameLen = 63;
        BlockMoveData(&fileSpec.name[1], gApp.settings.modelFileName, nameLen);
        gApp.settings.modelFileName[nameLen] = '\0';
    }

    /* Show progress window during model loading */
    {
        WindowPtr progressWin;
        Rect progressRect;
        Rect barRect;
        short screenW;
        short screenH;
        short winW;
        short winH;

        winW = 300;
        winH = 80;
        screenW = gApp.qd.screenBits.bounds.right;
        screenH = gApp.qd.screenBits.bounds.bottom;
        SetRect(&progressRect,
                (screenW - winW) / 2, (screenH - winH) / 2,
                (screenW + winW) / 2, (screenH + winH) / 2);
        progressWin = NewCWindow(nil, &progressRect, "\pLoading Model",
                                  true, dBoxProc, (WindowPtr)-1, false, 0);

        if (progressWin != nil)
        {
            SetPort(progressWin);
            TextFont(1);
            TextSize(10);
            MoveTo(20, 25);
            DrawString("\pLoading model, please wait...");

            /* Draw empty progress bar */
            SetRect(&barRect, 20, 40, winW - 20, 58);
            FrameRect(&barRect);
        }

        /* Tear down previous model completely before loading new one */
        QueryRouter_Cleanup();
        Engine_Cleanup();

        /* Re-initialize engine */
        if (Engine_InitializeWithProgress() != noErr)
        {
            if (progressWin != nil) DisposeWindow(progressWin);
            ParamText("\pFailed to re-initialize engine.", "\p", "\p", "\p");
            Alert(128, nil);
            return;
        }

        /* Update progress bar to 30% */
        if (progressWin != nil)
        {
            Rect fillRect;
            SetPort(progressWin);
            SetRect(&fillRect, 21, 41, 21 + (winW - 42) * 30 / 100, 57);
            PaintRect(&fillRect);
            SystemTask();
        }

        /* Load model weights */
        if (Engine_LoadModel(&fileSpec) == noErr)
        {
            /* Update progress to 80% */
            if (progressWin != nil)
            {
                Rect fillRect;
                SetPort(progressWin);
                SetRect(&fillRect, 21, 41, 21 + (winW - 42) * 80 / 100, 57);
                PaintRect(&fillRect);
                SystemTask();
            }

            /* Load tokenizer + init router */
            if (Tokenizer_Initialize(&fileSpec) == noErr)
                QueryRouter_Initialize(&fileSpec, &fileSpec);
        }

        /* Dismiss progress window and force redraw of windows behind it */
        if (progressWin != nil)
        {
            DisposeWindow(progressWin);
            /* Process pending update events to clear ghosting */
            {
                EventRecord updEvt;
                short updateCount;
                updateCount = 0;
                while (GetNextEvent(updateMask, &updEvt) && updateCount < 5)
                {
                    updateCount++;
                    BeginUpdate((WindowPtr)updEvt.message);
                    EndUpdate((WindowPtr)updEvt.message);
                }
            }
        }
    }

    /* Update status bar */
    sprintf(statusMsg, "Model: %s", gApp.settings.modelFileName);
    SafeStringCopy(gApp.statusText, statusMsg, sizeof(gApp.statusText));
    ChatWindow_UpdateStatus();

    /* Update chat output with new model name */
    if (gApp.model.modelLoaded && gOutputTE != nil)
    {
        char loadMsg[128];
        char modelName[64];
        short nameLen;

        SafeStringCopy(modelName, gApp.settings.modelFileName, 64);
        nameLen = (short)strlen(modelName);
        /* Strip .bin extension */
        if (nameLen > 4 && modelName[nameLen-4] == '.')
            modelName[nameLen-4] = '\0';

        sprintf(loadMsg, "\rMacinAI: Model switched to %s.\r\r", modelName);
        TESetSelect(32767, 32767, gOutputTE);
        TEInsert(loadMsg, strlen(loadMsg), gOutputTE);
        ChatWindow_UpdateScrollbar();
    }

    /* Save settings so model path persists */
    SaveSettings();
}

/*------------------------------------------------------------------------------
    LoadSettings
------------------------------------------------------------------------------*/
void LoadSettings(void)
{
    FSSpec settingsSpec;
    short refNum;
    long count;
    OSErr err;
    short vRefNum;
    long dirID;

    /* Default settings */
    memset(&gApp.settings, 0, sizeof(PersistentSettings));
    gApp.settings.version = kSettingsVersion;
    gApp.settings.beepsEnabled = true;
    gApp.settings.speech.speechEnabled = false;
    gApp.settings.speech.autoSpeak = false;
    gApp.settings.speech.voiceIndex = 0;
    gApp.settings.speech.ratePercent = 50;
    gApp.settings.speech.feedbackSounds = false;
    SafeStringCopy(gApp.settings.modelFileName, "macinai_model.bin", 64);
    SafeStringCopy(gApp.settings.vocabFileName, "vocab.txt", 64);

    /* Try to load from Preferences */
    err = FindFolder(kOnSystemDisk, kPreferencesFolderType, false, &vRefNum, &dirID);
    if (err != noErr)
        return;

    err = FSMakeFSSpec(vRefNum, dirID, "\pMacinAI Local Settings", &settingsSpec);
    if (err != noErr)
        return;

    err = FSpOpenDF(&settingsSpec, fsRdPerm, &refNum);
    if (err != noErr)
        return;

    count = sizeof(PersistentSettings);
    err = FSRead(refNum, &count, &gApp.settings);
    FSClose(refNum);

    /* Validate version */
    if (gApp.settings.version != kSettingsVersion)
    {
        /* Reset to defaults on version mismatch */
        memset(&gApp.settings, 0, sizeof(PersistentSettings));
        gApp.settings.version = kSettingsVersion;
        gApp.settings.beepsEnabled = true;
        SafeStringCopy(gApp.settings.modelFileName, "macinai_model.bin", 64);
        SafeStringCopy(gApp.settings.vocabFileName, "vocab.txt", 64);
    }
}

/*------------------------------------------------------------------------------
    SaveSettings
------------------------------------------------------------------------------*/
void SaveSettings(void)
{
    FSSpec settingsSpec;
    short refNum;
    long count;
    OSErr err;
    short vRefNum;
    long dirID;

    err = FindFolder(kOnSystemDisk, kPreferencesFolderType, true, &vRefNum, &dirID);
    if (err != noErr)
        return;

    err = FSMakeFSSpec(vRefNum, dirID, "\pMacinAI Local Settings", &settingsSpec);
    if (err == fnfErr)
        err = FSpCreate(&settingsSpec, 'OAS ', 'pref', smSystemScript);
    if (err != noErr && err != dupFNErr)
        return;

    err = FSpOpenDF(&settingsSpec, fsWrPerm, &refNum);
    if (err != noErr)
        return;

    count = sizeof(PersistentSettings);
    FSWrite(refNum, &count, &gApp.settings);
    SetEOF(refNum, count);
    FSClose(refNum);
}

/*------------------------------------------------------------------------------
    Cleanup
------------------------------------------------------------------------------*/
void Cleanup(void)
{
    /* Save settings */

    /* Close debug log */
    DebugLog_Close();
    SaveSettings();

    /* Save conversations */
    ConvMgr_Save();

    /* Cleanup engine */
    Engine_Cleanup();

    /* Cleanup speech */
    Speech_Cleanup();

    /* Close windows */
    ChatScreen_Close();
    SplashScreen_Close();
}
