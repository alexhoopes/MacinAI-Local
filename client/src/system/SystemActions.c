/*------------------------------------------------------------------------------
    SystemActions.c - AI-Controlled System Actions

    Implements system-level actions. Stripped from MacinAI relay version:
    - Removed RefreshSysInfo (no server to upload to)
    - Kept shutdown, restart, launch app, open CP, query system

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

#include "SystemActions.h"
#include "AppGlobals.h"
#include "SafeString.h"
#include "CatalogResolver.h"

#include <Types.h>
#include <Memory.h>
#include <Dialogs.h>
#include <MacWindows.h>
#include <AppleEvents.h>
#include <AERegistry.h>
#include <AEObjects.h>
#include <Gestalt.h>
#include <Shutdown.h>
#include <Power.h>
#include <Processes.h>
#include <Files.h>
#include <Folders.h>
#include <Aliases.h>
#include <string.h>
#include <stdio.h>

#pragma segment System

/*------------------------------------------------------------------------------
    Forward declarations
------------------------------------------------------------------------------*/
static short ShowConfirmationDialog(ConstStr255Param message, ConstStr255Param buttonLabel);

/*------------------------------------------------------------------------------
    Module Globals
------------------------------------------------------------------------------*/
static Boolean gSystemActionsInitialized = false;

/*------------------------------------------------------------------------------
    ShowConfirmationDialog
------------------------------------------------------------------------------*/
static short ShowConfirmationDialog(ConstStr255Param message, ConstStr255Param buttonLabel)
{
    short itemHit;
    long gestaltResponse;
    OSErr err;

    err = Gestalt(gestaltAppearanceAttr, &gestaltResponse);

    if (err == noErr && (gestaltResponse & (1 << gestaltAppearanceExists))) {
        AlertStdAlertParamRec params;

        params.movable = false;
        params.helpButton = false;
        params.filterProc = nil;
        params.defaultText = (StringPtr)buttonLabel;
        params.cancelText = (StringPtr)"\pCancel";
        params.otherText = nil;
        params.defaultButton = kAlertStdAlertOKButton;
        params.cancelButton = kAlertStdAlertCancelButton;
        params.position = kWindowDefaultPosition;

        StandardAlert(kAlertCautionAlert, (StringPtr)message, (StringPtr)"\p", &params, &itemHit);
        return itemHit;
    } else {
        Str255 alertText;

        BlockMove(message, alertText, message[0] + 1);
        ParamText(alertText, "\p", "\p", "\p");
        itemHit = CautionAlert(128, nil);
        return itemHit;
    }
}

/*------------------------------------------------------------------------------
    SystemActions_Initialize
------------------------------------------------------------------------------*/
OSErr SystemActions_Initialize(void)
{
    if (gSystemActionsInitialized)
        return noErr;

    gSystemActionsInitialized = true;
    return noErr;
}

/*------------------------------------------------------------------------------
    SystemActions_Shutdown
------------------------------------------------------------------------------*/
OSErr SystemActions_Shutdown(Boolean showDialog)
{
    OSErr err;
    short itemHit;
    AppleEvent shutdownEvent, reply;
    AEAddressDesc finderAddress;
    OSType finderSignature = 'MACS';

    if (showDialog) {
        itemHit = ShowConfirmationDialog("\pShut down your Macintosh now?", "\pShut Down");
        if (itemHit == 2)
            return userCanceledErr;
    }

    /* Send Apple Event to Finder */
    err = AECreateDesc(typeApplSignature, &finderSignature,
                       sizeof(OSType), &finderAddress);
    if (err == noErr) {
        err = AECreateAppleEvent(kAEFinderEvents, kAEShutDown,
                                &finderAddress, kAutoGenerateReturnID,
                                kAnyTransactionID, &shutdownEvent);
        AEDisposeDesc(&finderAddress);

        if (err == noErr) {
            err = AESend(&shutdownEvent, &reply,
                        kAENoReply | kAECanInteract,
                        kAENormalPriority, kAEDefaultTimeout,
                        nil, nil);
            AEDisposeDesc(&shutdownEvent);
        }
    }

    if (err != noErr) {
        /* Fallback to ShutDwnPower trap */
        ShutDwnPower();
    }

    return noErr;
}

/*------------------------------------------------------------------------------
    SystemActions_Restart
------------------------------------------------------------------------------*/
OSErr SystemActions_Restart(Boolean showDialog)
{
    OSErr err;
    short itemHit;
    AppleEvent restartEvent, reply;
    AEAddressDesc finderAddress;
    OSType finderSignature = 'MACS';

    if (showDialog) {
        itemHit = ShowConfirmationDialog("\pRestart your Macintosh now?", "\pRestart");
        if (itemHit == 2)
            return userCanceledErr;
    }

    err = AECreateDesc(typeApplSignature, &finderSignature,
                       sizeof(OSType), &finderAddress);
    if (err == noErr) {
        err = AECreateAppleEvent(kAEFinderEvents, kAERestart,
                                &finderAddress, kAutoGenerateReturnID,
                                kAnyTransactionID, &restartEvent);
        AEDisposeDesc(&finderAddress);

        if (err == noErr) {
            err = AESend(&restartEvent, &reply,
                        kAENoReply | kAECanInteract,
                        kAENormalPriority, kAEDefaultTimeout,
                        nil, nil);
            AEDisposeDesc(&restartEvent);
        }
    }

    if (err != noErr) {
        ShutDwnStart();
    }

    return noErr;
}

/*------------------------------------------------------------------------------
    SystemActions_LaunchApplication
------------------------------------------------------------------------------*/
OSErr SystemActions_LaunchApplication(const char *appName)
{
    ResolveResult results[kMaxSearchResults];
    short numFound;
    Boolean isPath;
    short ci;

    if (appName == nil || appName[0] == '\0')
        return paramErr;

    /* If argument contains ':' it is an HFS path (from choice selection) */
    isPath = false;
    for (ci = 0; appName[ci] != '\0'; ci++)
    {
        if (appName[ci] == ':')
        {
            isPath = true;
            break;
        }
    }
    if (isPath)
    {
        char errorMsg[256];
        return SystemActions_LaunchAppByPath(appName, errorMsg);
    }

    numFound = CatalogResolver_FindApp(appName, results, kMaxSearchResults);

    if (numFound == 0)
        return fnfErr;

    /* Single high-confidence result: launch directly */
    if (numFound == 1 || results[0].confidence >= 80)
        return CatalogResolver_LaunchApp(&results[0]);

    /* Multiple results: populate choice state for user selection */
    {
        short i;
        short count;

        count = numFound;
        if (count > kMaxSearchResults)
            count = kMaxSearchResults;

        for (i = 0; i < count; i++)
        {
            SafeStringCopy(gApp.choiceState.results[i].name,
                          results[i].name, kMaxResultNameLen);
            SafeStringCopy(gApp.choiceState.results[i].path,
                          results[i].path, kMaxResultPathLen);
            gApp.choiceState.results[i].fileSpec = results[i].spec;
        }

        gApp.choiceState.resultCount = count;
        gApp.choiceState.pendingAction = kPendingLaunchApp;
        gApp.choiceState.awaitingChoice = true;
    }

    /* Return special code - caller should display the list */
    return 1;  /* Positive = choice pending */
}

/*------------------------------------------------------------------------------
    SystemActions_OpenControlPanel
------------------------------------------------------------------------------*/
OSErr SystemActions_OpenControlPanel(const char *cpName)
{
    FSSpec cpSpec;
    OSErr err;

    if (cpName == nil || cpName[0] == '\0')
        return paramErr;

    err = CatalogResolver_FindControlPanel(cpName, &cpSpec);
    if (err != noErr)
        return err;

    return CatalogResolver_OpenControlPanel(&cpSpec);
}

/*------------------------------------------------------------------------------
    SystemActions_PathToFSSpec
------------------------------------------------------------------------------*/
OSErr SystemActions_PathToFSSpec(const char *hfsPath, FSSpec *spec)
{
    Str255 pascalPath;
    short pathLen;

    if (hfsPath == nil || spec == nil)
        return paramErr;

    pathLen = strlen(hfsPath);
    if (pathLen > 255) pathLen = 255;

    pascalPath[0] = pathLen;
    BlockMoveData(hfsPath, &pascalPath[1], pathLen);

    return FSMakeFSSpec(0, 0, pascalPath, spec);
}

/*------------------------------------------------------------------------------
    SystemActions_LaunchAppByPath
------------------------------------------------------------------------------*/
OSErr SystemActions_LaunchAppByPath(const char *hfsPath, char *errorMsg)
{
    FSSpec appSpec;
    LaunchParamBlockRec launchPB;
    OSErr err;

    err = SystemActions_PathToFSSpec(hfsPath, &appSpec);
    if (err != noErr) {
        sprintf(errorMsg, "ERROR: File not found at path '%s' (error %d)", hfsPath, err);
        return err;
    }

    launchPB.launchBlockID = extendedBlock;
    launchPB.launchEPBLength = extendedBlockLen;
    launchPB.launchFileFlags = 0;
    launchPB.launchControlFlags = launchContinue;
    launchPB.launchAppSpec = &appSpec;
    launchPB.launchAppParameters = nil;

    err = LaunchApplication(&launchPB);
    if (err != noErr) {
        sprintf(errorMsg, "ERROR: Launch failed (error %d)", err);
        return err;
    }

    sprintf(errorMsg, "SUCCESS");
    return noErr;
}

/*------------------------------------------------------------------------------
    SystemActions_QuerySystem
------------------------------------------------------------------------------*/
OSErr SystemActions_QuerySystem(const char *queryType, char *result, short maxLen)
{
    if (strcmp(queryType, "MEMORY") == 0) {
        long freeMem;
        Size growBytes;

        freeMem = TempMaxMem(&growBytes);
        sprintf(result, "Available memory: %ldKB (%ldMB)",
                freeMem / 1024L, freeMem / (1024L * 1024L));
        return noErr;
    }
    else if (strcmp(queryType, "VOLUMES") == 0) {
        HParamBlockRec pb;
        short volIndex = 1;
        Str255 volName;
        short nameLen;

        result[0] = '\0';

        while (true) {
            pb.volumeParam.ioCompletion = nil;
            pb.volumeParam.ioNamePtr = volName;
            pb.volumeParam.ioVRefNum = 0;
            pb.volumeParam.ioVolIndex = volIndex;

            if (PBHGetVInfoSync(&pb) != noErr)
                break;

            if (result[0] != '\0')
                SafeStringCat(result, ", ", maxLen);

            nameLen = volName[0];
            volName[nameLen + 1] = '\0';
            SafeStringCat(result, (char*)&volName[1], maxLen);

            volIndex++;
        }
        return noErr;
    }

    sprintf(result, "Unknown query type: %s", queryType);
    return paramErr;
}

/*------------------------------------------------------------------------------
    SystemActions_ShowAlert
------------------------------------------------------------------------------*/
void SystemActions_ShowAlert(const char *message)
{
    Str255 pStr;
    short len;

    len = strlen(message);
    if (len > 255) len = 255;
    pStr[0] = len;
    BlockMoveData(message, &pStr[1], len);

    ParamText(pStr, "\p", "\p", "\p");
    Alert(128, nil);
}
