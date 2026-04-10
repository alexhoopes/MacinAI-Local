/*------------------------------------------------------------------------------
    CatalogResolver.c - Filesystem Action Resolution

    Resolves application names and control panel names to FSSpecs.
    Uses:
    1. FindFolder for system directories (Control Panels, Extensions)
    2. PBCatSearchSync for fast volume-level name matching
    3. Fuzzy case-insensitive partial matching
    4. LaunchParamBlockRec for app launching

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

#include "CatalogResolver.h"
#include "SafeString.h"
#include "AppGlobals.h"

#include <Files.h>
#include <Folders.h>
#include <Processes.h>
#include <Aliases.h>
#include <string.h>
#include <stdio.h>

#pragma segment Engine

/*------------------------------------------------------------------------------
    Forward declarations
------------------------------------------------------------------------------*/
static Boolean FuzzyMatch(const char *name, const char *query);
static void CStrToLower(char *dst, const char *src, short maxLen);
static short SearchVolumeForApp(short vRefNum, const char *query,
                                ResolveResult *results, short maxResults);
static short SearchDirectoryForName(short vRefNum, long dirID,
                                    const char *query,
                                    ResolveResult *results, short maxResults);
static void FSSpecToPath(const FSSpec *spec, char *path, short maxLen);

/*------------------------------------------------------------------------------
    Module Globals
------------------------------------------------------------------------------*/
static Boolean gResolverInitialized = false;

/*------------------------------------------------------------------------------
    CStrToLower - Lowercase a C string for comparison
------------------------------------------------------------------------------*/
static void CStrToLower(char *dst, const char *src, short maxLen)
{
    short i;

    for (i = 0; i < maxLen - 1 && src[i] != '\0'; i++)
    {
        if (src[i] >= 'A' && src[i] <= 'Z')
            dst[i] = src[i] + 32;
        else
            dst[i] = src[i];
    }
    dst[i] = '\0';
}

/*------------------------------------------------------------------------------
    FuzzyMatch - Case-insensitive substring match
    Returns true if query appears anywhere in name.
------------------------------------------------------------------------------*/
static Boolean FuzzyMatch(const char *name, const char *query)
{
    char nameLower[256];
    char queryLower[128];
    char *p;

    CStrToLower(nameLower, name, 256);
    CStrToLower(queryLower, query, 128);

    p = strstr(nameLower, queryLower);
    return (p != nil);
}

/*------------------------------------------------------------------------------
    FSSpecToPath - Convert FSSpec to HFS path string
------------------------------------------------------------------------------*/
static void FSSpecToPath(const FSSpec *spec, char *path, short maxLen)
{
    CInfoPBRec pb;
    Str255 dirName;
    char temp[256];
    char segment[256];
    short nameLen;
    OSErr err;
    long parentDirID;

    /* Start with the filename */
    nameLen = spec->name[0];
    if (nameLen > maxLen - 1) nameLen = maxLen - 1;
    BlockMoveData(&spec->name[1], path, nameLen);
    path[nameLen] = '\0';

    /* Walk up the directory tree */
    parentDirID = spec->parID;
    while (parentDirID != fsRtParID)
    {
        pb.dirInfo.ioCompletion = nil;
        pb.dirInfo.ioNamePtr = dirName;
        pb.dirInfo.ioVRefNum = spec->vRefNum;
        pb.dirInfo.ioDrDirID = parentDirID;
        pb.dirInfo.ioFDirIndex = -1;

        err = PBGetCatInfoSync(&pb);
        if (err != noErr)
            break;

        /* Prepend directory name */
        nameLen = dirName[0];
        if (nameLen > 254) nameLen = 254;
        BlockMoveData(&dirName[1], segment, nameLen);
        segment[nameLen] = '\0';

        sprintf(temp, "%s:%s", segment, path);
        SafeStringCopy(path, temp, maxLen);

        parentDirID = pb.dirInfo.ioDrParID;
    }
}

/*------------------------------------------------------------------------------
    SearchDirectoryForName - Search a specific directory for matching files
------------------------------------------------------------------------------*/
static short SearchDirectoryForName(short vRefNum, long dirID,
                                    const char *query,
                                    ResolveResult *results, short maxResults)
{
    CInfoPBRec pb;
    Str255 entryName;
    char cName[256];
    short found = 0;
    short index;
    short nameLen;
    OSErr err;

    for (index = 1; found < maxResults; index++)
    {
        pb.hFileInfo.ioCompletion = nil;
        pb.hFileInfo.ioNamePtr = entryName;
        pb.hFileInfo.ioVRefNum = vRefNum;
        pb.hFileInfo.ioDirID = dirID;
        pb.hFileInfo.ioFDirIndex = index;

        err = PBGetCatInfoSync(&pb);
        if (err != noErr)
            break;

        /* Skip directories */
        if (pb.hFileInfo.ioFlAttrib & 0x10)
            continue;

        /* Convert Pascal name to C string */
        nameLen = entryName[0];
        if (nameLen > 255) nameLen = 255;
        BlockMoveData(&entryName[1], cName, nameLen);
        cName[nameLen] = '\0';

        /* Check for match */
        if (FuzzyMatch(cName, query))
        {
            SafeStringCopy(results[found].name, cName, 64);
            FSMakeFSSpec(vRefNum, dirID, entryName, &results[found].spec);
            FSSpecToPath(&results[found].spec, results[found].path, 256);

            /* Score: exact match = 100, starts-with = 80, contains = 50 */
            {
                char nameLow[256];
                char queryLow[128];
                CStrToLower(nameLow, cName, 256);
                CStrToLower(queryLow, query, 128);

                if (strcmp(nameLow, queryLow) == 0)
                    results[found].confidence = 100;
                else if (strncmp(nameLow, queryLow, strlen(queryLow)) == 0)
                    results[found].confidence = 80;
                else
                    results[found].confidence = 50;
            }
            found++;
        }
    }

    return found;
}

/*------------------------------------------------------------------------------
    SearchVolumeForApp - Search a volume's Applications folder and root
------------------------------------------------------------------------------*/
static short SearchVolumeForApp(short vRefNum, const char *query,
                                ResolveResult *results, short maxResults)
{
    CInfoPBRec pb;
    Str255 dirName;
    short found = 0;
    short index;
    short nameLen;
    long rootDirID;
    OSErr err;

    /* Get root directory ID */
    pb.dirInfo.ioCompletion = nil;
    pb.dirInfo.ioNamePtr = nil;
    pb.dirInfo.ioVRefNum = vRefNum;
    pb.dirInfo.ioDrDirID = fsRtDirID;
    pb.dirInfo.ioFDirIndex = -1;
    err = PBGetCatInfoSync(&pb);
    if (err != noErr)
        return 0;
    rootDirID = pb.dirInfo.ioDrDirID;

    /* Search root level first */
    found = SearchDirectoryForName(vRefNum, rootDirID, query,
                                   results, maxResults);

    /* Search common app directories */
    for (index = 1; found < maxResults; index++)
    {
        pb.dirInfo.ioCompletion = nil;
        pb.dirInfo.ioNamePtr = dirName;
        pb.dirInfo.ioVRefNum = vRefNum;
        pb.dirInfo.ioDrDirID = rootDirID;
        pb.dirInfo.ioFDirIndex = index;

        err = PBGetCatInfoSync(&pb);
        if (err != noErr)
            break;

        /* Only look in directories */
        if (!(pb.dirInfo.ioFlAttrib & 0x10))
            continue;

        /* Convert name for matching */
        nameLen = dirName[0];
        if (nameLen > 255) nameLen = 255;

        /* Search this subdirectory */
        {
            short subFound;
            subFound = SearchDirectoryForName(vRefNum, pb.dirInfo.ioDrDirID,
                                              query,
                                              &results[found],
                                              maxResults - found);
            found += subFound;
        }

        /* Don't recurse too deep - just one level of subdirs */
    }

    return found;
}

/*------------------------------------------------------------------------------
    CatalogResolver_Initialize
------------------------------------------------------------------------------*/
OSErr CatalogResolver_Initialize(void)
{
    gResolverInitialized = true;
    return noErr;
}

/*------------------------------------------------------------------------------
    CatalogResolver_FindApp - Find application by name
------------------------------------------------------------------------------*/
short CatalogResolver_FindApp(const char *query,
                              ResolveResult *results, short maxResults)
{
    HParamBlockRec vpb;
    Str255 volName;
    short volIndex;
    short totalFound = 0;
    OSErr err;

    if (query == nil || results == nil || maxResults <= 0)
        return 0;

    /* Iterate all mounted volumes */
    for (volIndex = 1; totalFound < maxResults; volIndex++)
    {
        vpb.volumeParam.ioCompletion = nil;
        vpb.volumeParam.ioNamePtr = volName;
        vpb.volumeParam.ioVRefNum = 0;
        vpb.volumeParam.ioVolIndex = volIndex;

        err = PBHGetVInfoSync(&vpb);
        if (err != noErr)
            break;

        /* Search this volume */
        {
            short found;
            found = SearchVolumeForApp(vpb.volumeParam.ioVRefNum, query,
                                       &results[totalFound],
                                       maxResults - totalFound);
            totalFound += found;
        }
    }

    return totalFound;
}

/*------------------------------------------------------------------------------
    CatalogResolver_FindControlPanel - Find CP in Control Panels folder
------------------------------------------------------------------------------*/
OSErr CatalogResolver_FindControlPanel(const char *query, FSSpec *result)
{
    short vRefNum;
    long dirID;
    ResolveResult found[5];
    short numFound;
    OSErr err;

    if (query == nil || result == nil)
        return paramErr;

    /* Get Control Panels folder */
    err = FindFolder(kOnSystemDisk, kControlPanelFolderType, false,
                     &vRefNum, &dirID);
    if (err != noErr)
        return err;

    /* Search for matching control panel */
    numFound = SearchDirectoryForName(vRefNum, dirID, query, found, 5);

    if (numFound == 0)
        return fnfErr;

    /* Return best match (highest confidence) */
    {
        short bestIdx = 0;
        short i;
        for (i = 1; i < numFound; i++)
        {
            if (found[i].confidence > found[bestIdx].confidence)
                bestIdx = i;
        }
        *result = found[bestIdx].spec;
    }

    return noErr;
}

/*------------------------------------------------------------------------------
    CatalogResolver_LaunchApp - Launch application from resolve result
------------------------------------------------------------------------------*/
OSErr CatalogResolver_LaunchApp(const ResolveResult *result)
{
    LaunchParamBlockRec launchPB;
    OSErr err;

    if (result == nil)
        return paramErr;

    launchPB.launchBlockID = extendedBlock;
    launchPB.launchEPBLength = extendedBlockLen;
    launchPB.launchFileFlags = 0;
    launchPB.launchControlFlags = launchContinue;
    launchPB.launchAppSpec = (FSSpecPtr)&result->spec;
    launchPB.launchAppParameters = nil;

    err = LaunchApplication(&launchPB);
    return err;
}

/*------------------------------------------------------------------------------
    CatalogResolver_OpenControlPanel - Open a control panel
    Opens via Finder Apple Event (double-click equivalent)
------------------------------------------------------------------------------*/
OSErr CatalogResolver_OpenControlPanel(const FSSpec *cpSpec)
{
    AliasHandle alias;
    AEDesc fileDesc;
    AEDescList fileList;
    AppleEvent openEvent;
    AppleEvent reply;
    AEAddressDesc finderAddr;
    OSType finderSig;
    OSErr err;

    if (cpSpec == nil)
        return paramErr;

    finderSig = 'MACS';

    /* Create Finder address */
    err = AECreateDesc(typeApplSignature, &finderSig, sizeof(OSType), &finderAddr);
    if (err != noErr)
        return err;

    /* Create open event */
    err = AECreateAppleEvent(kCoreEventClass, kAEOpenDocuments,
                             &finderAddr, kAutoGenerateReturnID,
                             kAnyTransactionID, &openEvent);
    AEDisposeDesc(&finderAddr);
    if (err != noErr)
        return err;

    /* Create alias for the file */
    err = NewAlias(nil, cpSpec, &alias);
    if (err != noErr)
    {
        AEDisposeDesc(&openEvent);
        return err;
    }

    /* Build file list */
    HLock((Handle)alias);
    err = AECreateList(nil, 0, false, &fileList);
    if (err == noErr)
    {
        err = AECreateDesc(typeAlias, *alias, GetHandleSize((Handle)alias), &fileDesc);
        if (err == noErr)
        {
            AEPutDesc(&fileList, 0, &fileDesc);
            AEDisposeDesc(&fileDesc);
        }
        AEPutParamDesc(&openEvent, keyDirectObject, &fileList);
        AEDisposeDesc(&fileList);
    }
    HUnlock((Handle)alias);
    DisposeHandle((Handle)alias);

    /* Send to Finder */
    err = AESend(&openEvent, &reply,
                 kAENoReply | kAECanInteract,
                 kAENormalPriority, kAEDefaultTimeout,
                 nil, nil);

    AEDisposeDesc(&openEvent);
    return err;
}

/*------------------------------------------------------------------------------
    CatalogResolver_Cleanup
------------------------------------------------------------------------------*/
void CatalogResolver_Cleanup(void)
{
    gResolverInitialized = false;
}
