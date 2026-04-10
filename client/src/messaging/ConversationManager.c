/*------------------------------------------------------------------------------
    ConversationManager.c - Conversation file I/O and management

    Simplified from MacinAI relay version:
    - Removed server conversation IDs
    - Removed AI context/memory (model handles its own context)
    - Kept local .oas file persistence
    - Kept debug logging

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

#include "ConversationManager.h"
#include "SafeString.h"
#include "DialogUtils.h"

#include <Files.h>
#include <Folders.h>
#include <Memory.h>
#include <TextEdit.h>
#include <string.h>
#include <stdio.h>

#pragma segment ConvMgr

/*------------------------------------------------------------------------------
    Forward declarations
------------------------------------------------------------------------------*/
static OSErr GetConversationsFolderSpec(FSSpec *folderSpec);
static void GenerateFilename(const char *title, char *filename);

/*------------------------------------------------------------------------------
    Module Globals
------------------------------------------------------------------------------*/
static ConversationList gConvList;
static char gCurrentFilename[64];
static short gScrollOffset = 0;
static Boolean gHasUnsavedChanges = false;
static Boolean gInitialized = false;

extern AppGlobals gApp;

/* Debug log file */
#if ENABLE_DEBUG_LOGGING
static short gDebugRefNum = 0;
static Boolean gDebugFileOpen = false;
#endif

/*------------------------------------------------------------------------------
    Debug Logging
------------------------------------------------------------------------------*/
#if ENABLE_DEBUG_LOGGING
void DebugLog(const char* message)
{
    FSSpec logSpec;
    OSErr err;
    long count;
    char logLine[512];
    short len;

    if (!gDebugFileOpen)
    {
        err = FSMakeFSSpec(0, 0, "\pMacinAI Local Debug.log", &logSpec);
        if (err == fnfErr)
        {
            err = FSpCreate(&logSpec, 'ttxt', 'TEXT', smSystemScript);
        }
        if (err == noErr || err == dupFNErr)
        {
            err = FSpOpenDF(&logSpec, fsWrPerm, &gDebugRefNum);
            if (err == noErr)
            {
                gDebugFileOpen = true;
                SetFPos(gDebugRefNum, fsFromLEOF, 0);
            }
        }
    }

    if (gDebugFileOpen)
    {
        sprintf(logLine, "%s\r", message);
        len = strlen(logLine);
        count = len;
        FSWrite(gDebugRefNum, &count, logLine);
    }
}
#endif

/*------------------------------------------------------------------------------
    GetConversationsFolderSpec - Get/create conversations folder
------------------------------------------------------------------------------*/
static OSErr GetConversationsFolderSpec(FSSpec *folderSpec)
{
    short vRefNum;
    long dirID;
    long newDirID;
    OSErr err;

    /* Find Preferences folder */
    err = FindFolder(kOnSystemDisk, kPreferencesFolderType, true, &vRefNum, &dirID);
    if (err != noErr)
        return err;

    /* Create MacinAI Local subfolder */
    err = DirCreate(vRefNum, dirID, "\pMacinAI Local", &newDirID);
    if (err == dupFNErr)
    {
        /* Already exists - get its dirID */
        CInfoPBRec cpb;
        Str255 folderName;

        SafeStringCopy((char*)&folderName[1], "MacinAI Local", 254);
        folderName[0] = strlen("MacinAI Local");

        cpb.dirInfo.ioCompletion = nil;
        cpb.dirInfo.ioNamePtr = folderName;
        cpb.dirInfo.ioVRefNum = vRefNum;
        cpb.dirInfo.ioDrDirID = dirID;
        cpb.dirInfo.ioFDirIndex = 0;

        err = PBGetCatInfoSync(&cpb);
        if (err == noErr)
            newDirID = cpb.dirInfo.ioDrDirID;
        else
            return err;
    }
    else if (err != noErr)
    {
        return err;
    }

    return FSMakeFSSpec(vRefNum, newDirID, "\p", folderSpec);
}

/*------------------------------------------------------------------------------
    GenerateFilename
------------------------------------------------------------------------------*/
static void GenerateFilename(const char *title, char *filename)
{
    short i;
    short len;

    len = strlen(title);
    if (len > 28) len = 28;

    for (i = 0; i < len; i++)
    {
        char c = title[i];
        if (c == ':' || c == '/' || c == '\\' || c < 32)
            filename[i] = '_';
        else
            filename[i] = c;
    }

    sprintf(filename + len, ".oas");
}

/*------------------------------------------------------------------------------
    ConvMgr_Init
------------------------------------------------------------------------------*/
void ConvMgr_Init(void)
{
    if (gInitialized)
        return;

    memset(&gConvList, 0, sizeof(ConversationList));
    gCurrentFilename[0] = '\0';
    gScrollOffset = 0;
    gHasUnsavedChanges = false;
    gInitialized = true;
}

/*------------------------------------------------------------------------------
    ConvMgr_Load
------------------------------------------------------------------------------*/
void ConvMgr_Load(void)
{
    /* STUB - Load conversation index from preferences folder */
    /* Phase 3 implementation */
}

/*------------------------------------------------------------------------------
    ConvMgr_Save
------------------------------------------------------------------------------*/
void ConvMgr_Save(void)
{
    /* STUB - Save conversation index to preferences folder */
}

/*------------------------------------------------------------------------------
    ConvMgr_SaveCurrent
------------------------------------------------------------------------------*/
void ConvMgr_SaveCurrent(TEHandle outputTE)
{
    if (gCurrentFilename[0] == '\0')
    {
        char title[kMaxConversationTitle];

        if (!PromptForConversationTitle(title))
            return;

        GenerateFilename(title, gCurrentFilename);
    }

    ConvMgr_SaveContent(gCurrentFilename, outputTE);
    gHasUnsavedChanges = false;
}

/*------------------------------------------------------------------------------
    ConvMgr_CreateNew
------------------------------------------------------------------------------*/
void ConvMgr_CreateNew(TEHandle outputTE)
{
    /* Save current if unsaved */
    if (gHasUnsavedChanges && outputTE != nil)
    {
        short result;
        result = ShowSavePrompt(nil);
        if (result == 1)
            ConvMgr_SaveCurrent(outputTE);
        else if (result == 3)
            return;  /* Cancel */
    }

    /* Reset state */
    gCurrentFilename[0] = '\0';
    gScrollOffset = 0;
    gHasUnsavedChanges = false;

    /* Reset token counters */
    gApp.model.totalTokensIn = 0;
    gApp.model.totalTokensOut = 0;

    /* Clear output TextEdit */
    if (outputTE != nil)
    {
        TESetSelect(0, 32767, outputTE);
        TEDelete(outputTE);
    }
}

/*------------------------------------------------------------------------------
    ConvMgr_GetCurrentTitle
------------------------------------------------------------------------------*/
void ConvMgr_GetCurrentTitle(char* titleOut)
{
    if (gCurrentFilename[0] != '\0')
    {
        /* Extract title from filename (remove .oas extension) */
        short len = strlen(gCurrentFilename);
        if (len > 4)
            len -= 4;
        if (len >= kMaxConversationTitle)
            len = kMaxConversationTitle - 1;
        BlockMoveData(gCurrentFilename, titleOut, len);
        titleOut[len] = '\0';
    }
    else
    {
        SafeStringCopy(titleOut, "New Conversation", kMaxConversationTitle);
    }
}

/*------------------------------------------------------------------------------
    ConvMgr_SaveContent
------------------------------------------------------------------------------*/
void ConvMgr_SaveContent(const char* filename, TEHandle outputTE)
{
    FSSpec folderSpec, fileSpec;
    OSErr err;
    short refNum;
    long count;
    Handle textHandle;
    long textLen;

    if (filename == nil || outputTE == nil)
        return;

    err = GetConversationsFolderSpec(&folderSpec);
    if (err != noErr)
        return;

    {
        Str255 pFilename;
        short nameLen;

        nameLen = strlen(filename);
        if (nameLen > 255) nameLen = 255;
        pFilename[0] = nameLen;
        BlockMoveData(filename, &pFilename[1], nameLen);

        err = FSMakeFSSpec(folderSpec.vRefNum, folderSpec.parID, pFilename, &fileSpec);
        if (err == fnfErr)
            err = FSpCreate(&fileSpec, 'OAS ', 'TEXT', smSystemScript);
        if (err != noErr && err != dupFNErr)
            return;
    }

    err = FSpOpenDF(&fileSpec, fsWrPerm, &refNum);
    if (err != noErr)
        return;

    /* Write conversation header (20 bytes) */
    {
        long headerFields[5];
        long headerCount;

        headerFields[0] = 0x4F415354L;  /* Magic: OAST */
        headerFields[1] = 1;            /* Version */
        headerFields[2] = gApp.model.totalTokensIn;
        headerFields[3] = gApp.model.totalTokensOut;
        headerFields[4] = 0;            /* Reserved */
        headerCount = 20;
        FSWrite(refNum, &headerCount, headerFields);
    }

    /* Write TextEdit content */
    HLock((Handle)outputTE);
    textHandle = (*outputTE)->hText;
    textLen = (*outputTE)->teLength;

    if (textHandle != nil && textLen > 0)
    {
        HLock(textHandle);
        count = textLen;
        FSWrite(refNum, &count, *textHandle);
        HUnlock(textHandle);
    }
    HUnlock((Handle)outputTE);

    SetEOF(refNum, textLen + 20);  /* 20-byte header + text */
    FSClose(refNum);
}

/*------------------------------------------------------------------------------
    ConvMgr_LoadContent
------------------------------------------------------------------------------*/
void ConvMgr_LoadContent(const char* filename, TEHandle outputTE)
{
    FSSpec folderSpec, fileSpec;
    OSErr err;
    short refNum;
    long fileSize;
    Handle buffer;
    long count;

    if (filename == nil || outputTE == nil)
        return;

    err = GetConversationsFolderSpec(&folderSpec);
    if (err != noErr)
        return;

    {
        Str255 pFilename;
        short nameLen;

        nameLen = strlen(filename);
        if (nameLen > 255) nameLen = 255;
        pFilename[0] = nameLen;
        BlockMoveData(filename, &pFilename[1], nameLen);

        err = FSMakeFSSpec(folderSpec.vRefNum, folderSpec.parID, pFilename, &fileSpec);
        if (err != noErr)
            return;
    }

    err = FSpOpenDF(&fileSpec, fsRdPerm, &refNum);
    if (err != noErr)
        return;

    GetEOF(refNum, &fileSize);
    if (fileSize <= 0 || fileSize > kMaxOutputTextLength)
    {
        FSClose(refNum);
        return;
    }

    /* Check for conversation header (20-byte OAST magic + metadata) */
    {
        long hdrBuf[5];
        long hdrCount;

        hdrCount = 20;
        err = FSRead(refNum, &hdrCount, hdrBuf);
        if (err == noErr && hdrCount == 20 && hdrBuf[0] == 0x4F415354L)
        {
            /* Valid OAST header - restore token counts */
            gApp.model.totalTokensIn = hdrBuf[2];
            gApp.model.totalTokensOut = hdrBuf[3];
            fileSize -= 20;
        }
        else
        {
            /* No header (legacy file) - rewind */
            SetFPos(refNum, fsFromStart, 0);
            gApp.model.totalTokensIn = 0;
            gApp.model.totalTokensOut = 0;
        }
    }

    buffer = NewHandle(fileSize);
    if (buffer == nil)
    {
        FSClose(refNum);
        return;
    }

    HLock(buffer);
    count = fileSize;
    err = FSRead(refNum, &count, *buffer);
    HUnlock(buffer);
    FSClose(refNum);

    if (err == noErr)
    {
        TESetSelect(0, 32767, outputTE);
        TEDelete(outputTE);
        HLock(buffer);
        TESetText(*buffer, count, outputTE);
        HUnlock(buffer);
    }

    DisposeHandle(buffer);

    SafeStringCopy(gCurrentFilename, filename, sizeof(gCurrentFilename));
    gHasUnsavedChanges = false;
}

/*------------------------------------------------------------------------------
    Accessors
------------------------------------------------------------------------------*/
ConversationList* ConvMgr_GetList(void)
{
    return &gConvList;
}

const char* ConvMgr_GetCurrentFilename(void)
{
    return gCurrentFilename;
}

void ConvMgr_SetCurrentFilename(const char* filename)
{
    SafeStringCopy(gCurrentFilename, filename, sizeof(gCurrentFilename));
}

short ConvMgr_GetScrollOffset(void)
{
    return gScrollOffset;
}

void ConvMgr_SetScrollOffset(short offset)
{
    gScrollOffset = offset;
}

Boolean ConvMgr_HasUnsavedChanges(void)
{
    return gHasUnsavedChanges;
}

void ConvMgr_SetUnsavedChanges(Boolean hasChanges)
{
    gHasUnsavedChanges = hasChanges;
}
