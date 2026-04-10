/*------------------------------------------------------------------------------
    AppleScriptExec.c - AppleScript Execution Module

    Uses the Open Scripting Architecture (OSA) to compile and execute
    AppleScript code. Shows a confirmation dialog before execution for safety.

    OSA is available on System 7.5.3+ when the AppleScript extension is
    installed (comes with System 7.5.3).

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

#pragma segment AppleScript

#include "AppleScriptExec.h"
#include "AppGlobals.h"
#include "DebugLog.h"

#include <Components.h>
#include <OSA.h>
#include <AppleScript.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations */
static Boolean ShowScriptConfirmDialog(const char *script);
static void CopyResultFromDesc(AEDesc *desc, char *resultMsg, short maxLen);

/* Module state */
static ComponentInstance sScriptingComponent = nil;
static Boolean sInitialized = false;
static Boolean sAvailable = false;

/*------------------------------------------------------------------------------
    AppleScript_Initialize - Open the scripting component
------------------------------------------------------------------------------*/
OSErr AppleScript_Initialize(void)
{
    if (sInitialized)
        return noErr;

    sInitialized = true;
    sAvailable = false;

    /* Try to open the AppleScript scripting component */
    sScriptingComponent = OpenDefaultComponent(kOSAComponentType,
                                                kAppleScriptSubtype);

    if (sScriptingComponent != nil)
    {
        sAvailable = true;

        #if ENABLE_DEBUG_LOGGING
        DebugLog_Write("AppleScript_Initialize: OSA component opened successfully");
        #endif

        return noErr;
    }
    else
    {
        #if ENABLE_DEBUG_LOGGING
        DebugLog_Write("AppleScript_Initialize: AppleScript not available");
        #endif

        return -1;
    }
}

/*------------------------------------------------------------------------------
    AppleScript_IsAvailable - Check if AppleScript is available
------------------------------------------------------------------------------*/
Boolean AppleScript_IsAvailable(void)
{
    if (!sInitialized)
        AppleScript_Initialize();

    return sAvailable;
}

/*------------------------------------------------------------------------------
    AppleScript_ExecuteWithConfirm - Execute script after user confirmation
------------------------------------------------------------------------------*/
OSErr AppleScript_ExecuteWithConfirm(const char *script, char *resultMsg, short maxLen)
{
    AEDesc scriptDesc;
    AEDesc resultDesc;
    OSAID scriptID;
    OSAID resultID;
    OSErr err;
    long scriptLen;
    char cleanScript[512];
    long cleanLen;

    /* Initialize descriptors */
    scriptDesc.descriptorType = typeNull;
    scriptDesc.dataHandle = nil;
    resultDesc.descriptorType = typeNull;
    resultDesc.dataHandle = nil;
    scriptID = kOSANullScript;
    resultID = kOSANullScript;

    /* Validate inputs */
    if (script == nil || script[0] == '\0')
    {
        strncpy(resultMsg, "No script provided", maxLen - 1);
        resultMsg[maxLen - 1] = '\0';
        return paramErr;
    }

    if (!sAvailable || sScriptingComponent == nil)
    {
        strncpy(resultMsg, "AppleScript not available on this system", maxLen - 1);
        resultMsg[maxLen - 1] = '\0';
        return -1;
    }

    /* Strip token leaking: truncate at first non-ASCII byte.
       LLM models sometimes emit multilingual garbage tokens after valid
       AppleScript. AppleScript is pure ASCII, so any byte > 127 means
       the valid script has ended. Also strip trailing whitespace. */
    {
        long ci;

        cleanLen = strlen(script);
        if (cleanLen >= (long)sizeof(cleanScript))
            cleanLen = (long)sizeof(cleanScript) - 1;

        BlockMoveData(script, cleanScript, cleanLen);
        cleanScript[cleanLen] = '\0';

        /* Truncate at first non-ASCII byte */
        for (ci = 0; ci < cleanLen; ci++)
        {
            if ((unsigned char)cleanScript[ci] > 127)
            {
                cleanScript[ci] = '\0';
                cleanLen = ci;

                #if ENABLE_DEBUG_LOGGING
                {
                    extern void DebugLog_Write(const char *message);
                    extern void DebugLog_WriteNum(const char *label, long value);
                    DebugLog_WriteNum("AS_Exec: STRIPPED non-ASCII at pos =", ci);
                    DebugLog_WriteNum("AS_Exec: clean script len =", cleanLen);
                }
                #endif

                break;
            }
        }

        /* Strip trailing whitespace/newlines */
        while (cleanLen > 0 &&
               (cleanScript[cleanLen - 1] == ' ' ||
                cleanScript[cleanLen - 1] == '\t' ||
                cleanScript[cleanLen - 1] == '\n' ||
                cleanScript[cleanLen - 1] == '\r'))
        {
            cleanLen--;
            cleanScript[cleanLen] = '\0';
        }

        /* Use cleaned script from here on */
        script = cleanScript;
    }

    /* Post-process: fix common LLM script generation errors.
       The model sometimes truncates early (missing end tell) or
       inserts stray commas. These are fixable syntax errors. */
    {
        long tellCount;
        long endTellCount;
        long ci;
        char *cp;

        /* 1. Remove stray commas after quotes: set cell "A4", to -> set cell "A4" to */
        cp = cleanScript;
        while (*cp)
        {
            if (*cp == '"' && *(cp + 1) == ',')
            {
                /* Shift everything after the comma left by 1 */
                {
                    char *src;
                    char *dst;
                    dst = cp + 1;
                    src = cp + 2;
                    while (*src)
                    {
                        *dst = *src;
                        dst++;
                        src++;
                    }
                    *dst = '\0';
                    cleanLen--;
                }
            }
            cp++;
        }

        /* 3. Balance tell/end tell blocks */
        tellCount = 0;
        endTellCount = 0;
        cp = cleanScript;
        while (*cp)
        {
            /* Count "tell " at start of line or after newline */
            if ((cp == cleanScript || *(cp - 1) == '\n') &&
                cp[0] == 't' && cp[1] == 'e' && cp[2] == 'l' && cp[3] == 'l' && cp[4] == ' ')
            {
                tellCount++;
            }
            /* Count "end tell" */
            if ((cp == cleanScript || *(cp - 1) == '\n') &&
                cp[0] == 'e' && cp[1] == 'n' && cp[2] == 'd' && cp[3] == ' ' &&
                cp[4] == 't' && cp[5] == 'e' && cp[6] == 'l' && cp[7] == 'l')
            {
                endTellCount++;
            }
            cp++;
        }

        /* Append missing end tell blocks */
        while (endTellCount < tellCount && cleanLen < (long)sizeof(cleanScript) - 12)
        {
            cleanScript[cleanLen++] = '\n';
            cleanScript[cleanLen++] = 'e';
            cleanScript[cleanLen++] = 'n';
            cleanScript[cleanLen++] = 'd';
            cleanScript[cleanLen++] = ' ';
            cleanScript[cleanLen++] = 't';
            cleanScript[cleanLen++] = 'e';
            cleanScript[cleanLen++] = 'l';
            cleanScript[cleanLen++] = 'l';
            cleanScript[cleanLen] = '\0';
            endTellCount++;

            #if ENABLE_DEBUG_LOGGING
            {
                extern void DebugLog_Write(const char *message);
                DebugLog_Write("AS_PostProcess: appended missing end tell");
            }
            #endif
        }

        script = cleanScript;
    }

    #if ENABLE_DEBUG_LOGGING
    {
        extern void DebugLog_Write(const char *message);
        extern void DebugLog_WriteNum(const char *label, long value);
        long logLen;
        DebugLog_Write("=== AppleScript_ExecuteWithConfirm BEGIN ===");
        DebugLog_WriteNum("AS_Exec: script length =", (long)strlen(script));

        /* Log entire script with newlines replaced by | for readability */
        logLen = strlen(script);
        DebugLog_Write("AS_Exec: --- FULL SCRIPT START ---");
        {
            /* Log in 120-char chunks, line by line */
            long pos;
            long lineStart;
            short lineNum;
            pos = 0;
            lineNum = 1;
            lineStart = 0;
            while (pos <= logLen)
            {
                if (pos == logLen || script[pos] == '\n' || script[pos] == '\r')
                {
                    long lineLen;
                    lineLen = pos - lineStart;
                    if (lineLen > 200) lineLen = 200;
                    if (lineLen > 0) {
                        char lineBuf[256];
                        sprintf(lineBuf, "AS_Exec: L%d: ", lineNum);
                        {
                            long hdrLen;
                            hdrLen = strlen(lineBuf);
                            BlockMoveData(script + lineStart, lineBuf + hdrLen, lineLen);
                            lineBuf[hdrLen + lineLen] = '\0';
                        }
                        DebugLog_Write(lineBuf);
                    }
                    lineNum++;
                    lineStart = pos + 1;
                }
                pos++;
            }
        }
        DebugLog_Write("AS_Exec: --- FULL SCRIPT END ---");

        /* Log hex dump of first 100 bytes to catch encoding issues */
        DebugLog_Write("AS_Exec: --- HEX DUMP (first 100 bytes) ---");
        {
            char hexBuf[400];
            long hexLen;
            hexLen = strlen(script);
            if (hexLen > 100) hexLen = 100;
            {
                long hi;
                char *hp;
                hp = hexBuf;
                for (hi = 0; hi < hexLen; hi++)
                {
                    unsigned char ch;
                    ch = (unsigned char)script[hi];
                    sprintf(hp, "%02X ", ch);
                    hp += 3;
                    if ((hi + 1) % 32 == 0) {
                        *hp = '\0';
                        DebugLog_Write(hexBuf);
                        hp = hexBuf;
                    }
                }
                *hp = '\0';
                if (hp != hexBuf) DebugLog_Write(hexBuf);
            }
        }
        DebugLog_Write("AS_Exec: --- HEX DUMP END ---");

        /* Check for any non-ASCII bytes (possible token leaking) */
        {
            long ci;
            long nonAsciiCount;
            long firstNonAscii;
            nonAsciiCount = 0;
            firstNonAscii = -1;
            for (ci = 0; ci < logLen; ci++) {
                if ((unsigned char)script[ci] > 127) {
                    nonAsciiCount++;
                    if (firstNonAscii < 0) firstNonAscii = ci;
                }
            }
            DebugLog_WriteNum("AS_Exec: non-ASCII byte count =", nonAsciiCount);
            if (firstNonAscii >= 0)
                DebugLog_WriteNum("AS_Exec: first non-ASCII at pos =", firstNonAscii);
        }
    }
    #endif

    /* Show confirmation dialog */
    if (!ShowScriptConfirmDialog(script))
    {
        #if ENABLE_DEBUG_LOGGING
        {
            extern void DebugLog_Write(const char *message);
            DebugLog_Write("AS_Exec: user CANCELLED script execution");
        }
        #endif
        strncpy(resultMsg, "User cancelled script execution", maxLen - 1);
        resultMsg[maxLen - 1] = '\0';
        return userCanceledErr;
    }

    #if ENABLE_DEBUG_LOGGING
    {
        extern void DebugLog_Write(const char *message);
        DebugLog_Write("AS_Exec: user CONFIRMED, converting newlines...");
    }
    #endif

    /* Convert \n to \r for Classic Mac AppleScript (1.1-1.5.5 requires \r) */
    scriptLen = strlen(script);
    {
        char convertBuf[512];
        long ci;
        long copyLen;

        copyLen = scriptLen;
        if (copyLen >= (long)sizeof(convertBuf))
            copyLen = (long)sizeof(convertBuf) - 1;

        BlockMoveData(script, convertBuf, copyLen);
        for (ci = 0; ci < copyLen; ci++)
        {
            if (convertBuf[ci] == '\n')
                convertBuf[ci] = '\r';
        }
        convertBuf[copyLen] = '\0';

        #if ENABLE_DEBUG_LOGGING
        {
            extern void DebugLog_Write(const char *message);
            extern void DebugLog_WriteNum(const char *label, long value);
            DebugLog_WriteNum("AS_Exec: converted script len =", copyLen);
            DebugLog_Write("AS_Exec: calling AECreateDesc...");
        }
        #endif

        err = AECreateDesc(typeChar, convertBuf, copyLen, &scriptDesc);
    }
    if (err != noErr)
    {
        #if ENABLE_DEBUG_LOGGING
        {
            extern void DebugLog_Write(const char *message);
            extern void DebugLog_WriteNum(const char *label, long value);
            DebugLog_WriteNum("AS_Exec: AECreateDesc FAILED err =", (long)err);
        }
        #endif
        sprintf(resultMsg, "Failed to create script descriptor (error %d)", err);
        return err;
    }

    #if ENABLE_DEBUG_LOGGING
    {
        extern void DebugLog_Write(const char *message);
        DebugLog_Write("AS_Exec: AECreateDesc OK, calling OSACompile...");
    }
    #endif

    /* Compile the script */
    err = OSACompile(sScriptingComponent, &scriptDesc, kOSAModeNull, &scriptID);
    AEDisposeDesc(&scriptDesc);

    if (err != noErr)
    {
        /* Try to get the compilation error message */
        AEDesc errDesc;
        errDesc.descriptorType = typeNull;
        errDesc.dataHandle = nil;

        #if ENABLE_DEBUG_LOGGING
        {
            extern void DebugLog_Write(const char *message);
            extern void DebugLog_WriteNum(const char *label, long value);
            DebugLog_WriteNum("AS_Exec: OSACompile FAILED err =", (long)err);
        }
        #endif

        if (OSAScriptError(sScriptingComponent, kOSAErrorMessage,
                          typeChar, &errDesc) == noErr)
        {
            CopyResultFromDesc(&errDesc, resultMsg, maxLen);
            AEDisposeDesc(&errDesc);

            #if ENABLE_DEBUG_LOGGING
            {
                extern void DebugLog_Write(const char *message);
                char logBuf[256];
                sprintf(logBuf, "AS_Exec: compile error msg = %s", resultMsg);
                DebugLog_Write(logBuf);
            }
            #endif
        }
        else
        {
            sprintf(resultMsg, "Script compilation failed (error %d)", err);
        }

        return err;
    }

    #if ENABLE_DEBUG_LOGGING
    {
        extern void DebugLog_Write(const char *message);
        DebugLog_Write("AS_Exec: OSACompile OK, calling OSAExecute...");
    }
    #endif

    /* Execute the compiled script */
    err = OSAExecute(sScriptingComponent, scriptID, kOSANullScript,
                     kOSAModeNull, &resultID);

    if (err != noErr)
    {
        /* Try to get the execution error message */
        AEDesc errDesc;
        errDesc.descriptorType = typeNull;
        errDesc.dataHandle = nil;

        #if ENABLE_DEBUG_LOGGING
        {
            extern void DebugLog_Write(const char *message);
            extern void DebugLog_WriteNum(const char *label, long value);
            DebugLog_WriteNum("AS_Exec: OSAExecute FAILED err =", (long)err);
        }
        #endif

        if (OSAScriptError(sScriptingComponent, kOSAErrorMessage,
                          typeChar, &errDesc) == noErr)
        {
            CopyResultFromDesc(&errDesc, resultMsg, maxLen);
            AEDisposeDesc(&errDesc);

            #if ENABLE_DEBUG_LOGGING
            {
                extern void DebugLog_Write(const char *message);
                char logBuf[256];
                sprintf(logBuf, "AS_Exec: execution error msg = %.200s", resultMsg);
                DebugLog_Write(logBuf);
            }
            #endif
        }
        else
        {
            sprintf(resultMsg, "Script execution failed (error %d)", err);
        }

        OSADispose(sScriptingComponent, scriptID);
        return err;
    }

    #if ENABLE_DEBUG_LOGGING
    {
        extern void DebugLog_Write(const char *message);
        DebugLog_Write("AS_Exec: OSAExecute OK! Getting result...");
    }
    #endif

    /* Get result as text */
    if (resultID != kOSANullScript)
    {
        err = OSADisplay(sScriptingComponent, resultID, typeChar,
                        kOSAModeNull, &resultDesc);

        if (err == noErr)
        {
            CopyResultFromDesc(&resultDesc, resultMsg, maxLen);
            AEDisposeDesc(&resultDesc);

            #if ENABLE_DEBUG_LOGGING
            {
                extern void DebugLog_Write(const char *message);
                char logBuf[256];
                sprintf(logBuf, "AS_Exec: result = %.200s", resultMsg);
                DebugLog_Write(logBuf);
            }
            #endif
        }
        else
        {
            strncpy(resultMsg, "Script completed (no displayable result)", maxLen - 1);
            resultMsg[maxLen - 1] = '\0';
        }

        OSADispose(sScriptingComponent, resultID);
    }
    else
    {
        strncpy(resultMsg, "Script completed successfully", maxLen - 1);
        resultMsg[maxLen - 1] = '\0';
    }

    /* Cleanup */
    OSADispose(sScriptingComponent, scriptID);

    #if ENABLE_DEBUG_LOGGING
    {
        extern void DebugLog_Write(const char *message);
        DebugLog_Write("=== AppleScript_ExecuteWithConfirm END (success) ===");
    }
    #endif

    return noErr;
}

/*------------------------------------------------------------------------------
    AppleScript_Cleanup - Close the scripting component
------------------------------------------------------------------------------*/
void AppleScript_Cleanup(void)
{
    if (sScriptingComponent != nil)
    {
        CloseComponent(sScriptingComponent);
        sScriptingComponent = nil;
    }
    sAvailable = false;
    sInitialized = false;
}

/*------------------------------------------------------------------------------
    ShowScriptConfirmDialog - Show confirmation dialog with script text
    Returns true if user clicks "Run", false if "Cancel"
------------------------------------------------------------------------------*/
static Boolean ShowScriptConfirmDialog(const char *script)
{
    WindowPtr dialog;
    Rect dialogRect;
    Rect scriptRect;
    Rect buttonRect;
    EventRecord event;
    Point localPt;
    ControlHandle cancelButton;
    ControlHandle runButton;
    ControlHandle clickedControl;
    TEHandle scriptTE;
    Boolean done;
    Boolean result;
    char key;
    long scriptLen;
    long displayLen;
    short screenW;
    short screenH;

    done = false;
    result = false;

    /* Create dialog centered on screen */
    screenW = gApp.qd.screenBits.bounds.right - gApp.qd.screenBits.bounds.left;
    screenH = gApp.qd.screenBits.bounds.bottom - gApp.qd.screenBits.bounds.top;
    SetRect(&dialogRect,
            (screenW - 380) / 2, (screenH - 240) / 2,
            (screenW + 380) / 2, (screenH + 240) / 2);

    dialog = NewCWindow(nil, &dialogRect, "\pAppleScript Confirmation", true,
                       dBoxProc, (WindowPtr)-1L, true, 0);
    if (dialog == nil)
        return false;

    SetPort(dialog);

    /* Create buttons */
    SetRect(&buttonRect, 100, 200, 180, 220);
    cancelButton = NewControl(dialog, &buttonRect, "\pCancel", true,
                             0, 0, 1, pushButProc, 0);

    SetRect(&buttonRect, 220, 200, 300, 220);
    runButton = NewControl(dialog, &buttonRect, "\pRun", true,
                          0, 0, 1, pushButProc, 0);

    /* Create TextEdit for script display (read-only) */
    SetRect(&scriptRect, 20, 50, 360, 190);
    FrameRect(&scriptRect);
    InsetRect(&scriptRect, 2, 2);
    scriptTE = TENew(&scriptRect, &scriptRect);

    if (scriptTE != nil)
    {
        /* Show script text (truncate if too long) */
        scriptLen = strlen(script);
        displayLen = scriptLen;
        if (displayLen > 2000)
            displayLen = 2000;

        /* Convert \n to \r for Mac TextEdit (model generates Unix line endings) */
        {
            char displayBuf[2048];
            long di;
            BlockMoveData(script, displayBuf, displayLen);
            for (di = 0; di < displayLen; di++)
            {
                if (displayBuf[di] == '\n')
                    displayBuf[di] = '\r';
            }
            TESetText(displayBuf, displayLen, scriptTE);
        }
        TEDeactivate(scriptTE);  /* Read-only appearance */
    }

    /* Draw header text */
    TextFont(systemFont);
    TextSize(12);
    TextFace(bold);
    MoveTo(20, 20);
    DrawString("\pMacinAI wants to run this script:");
    TextFace(normal);

    TextFont(3);  /* Geneva */
    TextSize(10);
    MoveTo(20, 35);
    DrawString("\pReview the script below and click Run to allow.");

    /* Draw script content */
    if (scriptTE != nil)
        TEUpdate(&scriptRect, scriptTE);

    DrawControls(dialog);

    /* Event loop */
    while (!done)
    {
        if (GetNextEvent(everyEvent, &event))
        {
            switch (event.what)
            {
                case mouseDown:
                    SetPort(dialog);
                    localPt = event.where;
                    GlobalToLocal(&localPt);

                    if (FindControl(localPt, dialog, &clickedControl) != 0)
                    {
                        if (TrackControl(clickedControl, localPt, nil) != 0)
                        {
                            if (clickedControl == cancelButton)
                            {
                                result = false;
                                done = true;
                            }
                            else if (clickedControl == runButton)
                            {
                                result = true;
                                done = true;
                            }
                        }
                    }
                    break;

                case keyDown:
                case autoKey:
                    key = event.message & charCodeMask;
                    if (key == '\r' || key == 3)  /* Return/Enter = Run */
                    {
                        result = true;
                        done = true;
                    }
                    else if (key == 27)  /* Escape = Cancel */
                    {
                        result = false;
                        done = true;
                    }
                    break;

                case updateEvt:
                    if ((WindowPtr)event.message == dialog)
                    {
                        BeginUpdate(dialog);
                        SetPort(dialog);
                        EraseRect(&dialog->portRect);

                        TextFont(systemFont);
                        TextSize(12);
                        TextFace(bold);
                        MoveTo(20, 20);
                        DrawString("\pMacinAI wants to run this script:");
                        TextFace(normal);

                        TextFont(3);
                        TextSize(10);
                        MoveTo(20, 35);
                        DrawString("\pReview the script below and click Run to allow.");

                        InsetRect(&scriptRect, -2, -2);
                        FrameRect(&scriptRect);
                        InsetRect(&scriptRect, 2, 2);

                        if (scriptTE != nil)
                            TEUpdate(&scriptRect, scriptTE);

                        DrawControls(dialog);
                        EndUpdate(dialog);
                    }
                    break;
            }
        }
        SystemTask();
    }

    /* Cleanup */
    if (scriptTE != nil)
        TEDispose(scriptTE);

    DisposeWindow(dialog);
    return result;
}

/*------------------------------------------------------------------------------
    CopyResultFromDesc - Extract text from AEDesc into buffer
------------------------------------------------------------------------------*/
static void CopyResultFromDesc(AEDesc *desc, char *resultMsg, short maxLen)
{
    long dataSize;
    long copyLen;

    if (desc == nil || desc->dataHandle == nil)
    {
        resultMsg[0] = '\0';
        return;
    }

    dataSize = GetHandleSize(desc->dataHandle);
    copyLen = dataSize;
    if (copyLen >= maxLen)
        copyLen = maxLen - 1;

    HLock(desc->dataHandle);
    BlockMoveData(*(desc->dataHandle), resultMsg, copyLen);
    HUnlock(desc->dataHandle);

    resultMsg[copyLen] = '\0';
}
