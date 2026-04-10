/*------------------------------------------------------------------------------
    MessageHandler.c - Local AI Message Processing

    Processes user messages through the QueryRouter pipeline:
    1. Get user input from TextEdit
    2. Route through QueryRouter (Canned -> Lookup -> Model)
    3. Display response in chat window
    4. Dispatch command actions if present

    QueryRouter handles SFT formatting, tokenization, inference,
    command extraction, and post-processing internally.

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

#include "MessageHandler.h"
#include "AppGlobals.h"
#include "QueryRouter.h"
#include "InferenceEngine.h"
#include "Tokenizer.h"
#include "ActionDispatcher.h"
#include "ConversationManager.h"
#include "SpeechManager.h"
#include "ChatWindow.h"
#include "SafeString.h"
#include "DebugLog.h"

#include <TextEdit.h>
#include <Memory.h>
#include <string.h>
#include <stdio.h>

#pragma segment ChatUI

/*------------------------------------------------------------------------------
    External references
------------------------------------------------------------------------------*/
extern AppGlobals gApp;
extern TEHandle gInputTE;
extern TEHandle gOutputTE;
extern void (*gEngineTokenCallback)(long, long);
extern void (*gEnginePrefillCallback)(long, long);

/*------------------------------------------------------------------------------
    Constants
------------------------------------------------------------------------------*/
#define kMaxInputLen        2048

/*------------------------------------------------------------------------------
    Forward declarations
------------------------------------------------------------------------------*/
static void AppendToOutput(const char *text, long len);
static void StreamTokenCallback(long tokenID, long numGenerated);
static void PrefillProgressCallback(long current, long total);

/*------------------------------------------------------------------------------
    AppendToOutput - Append text to the output TextEdit
------------------------------------------------------------------------------*/
static void AppendToOutput(const char *text, long len)
{
    if (gOutputTE == nil || text == nil || len <= 0)
        return;

    TESetSelect(32767, 32767, gOutputTE);
    TEInsert(text, len, gOutputTE);
    ChatWindow_UpdateScrollbar();
}

/*------------------------------------------------------------------------------
    PrefillProgressCallback - Update status bar during prefill phase
------------------------------------------------------------------------------*/
static void PrefillProgressCallback(long current, long total)
{
    char statusMsg[64];

    sprintf(statusMsg, "Processing input (%ld/%ld)...", current, total);
    SafeStringCopy(gApp.statusText, statusMsg, sizeof(gApp.statusText));
    ChatWindow_UpdateStatus();
}

/*------------------------------------------------------------------------------
    StreamTokenCallback - Display each token as it's generated

    Called by Engine_Generate after each output token. Decodes the token
    to text and appends it to the chat output in real-time.
    Special tokens ([SEP], [EOS], [CMD:xxx]) are not displayed.
------------------------------------------------------------------------------*/
static Boolean sStreamingStarted = false;
static Boolean sSawSep = false;
static Boolean sSuppressAppleScript = false;
static long sSuppressedTokenCount = 0;

/* Streaming speech: buffer tokens until word boundary, then speak.
   Controlled by gApp.settings.speech.feedbackSounds (repurposed as "speak during generation").
   autoSpeak = speak after generation (smooth, full response).
   feedbackSounds = speak during generation (word-by-word, real-time). */
static char sSpeechBuf[256];
static short sSpeechBufLen = 0;

static void StreamTokenCallback(long tokenID, long numGenerated)
{
    char tokenText[128];
    char statusMsg[64];
    long decoded;

    /* Show prefix before first response token */
    if (!sStreamingStarted)
    {
        char prefix[] = "MacinAI: ";
        AppendToOutput(prefix, strlen(prefix));
        sStreamingStarted = true;
    }

    /* If suppressing command argument output, skip all tokens until EOS */
    if (sSuppressAppleScript)
    {
        if (tokenID == 2)  /* kTokenEOS */
            sSuppressAppleScript = false;
        else
        {
            /* Count suppressed tokens and update status + output */
            sSuppressedTokenCount++;
            {
                char statusMsg[64];
                sprintf(statusMsg, "Generating AppleScript... (%ld tokens)",
                        (long)sSuppressedTokenCount);
                SafeStringCopy(gApp.statusText, statusMsg,
                              sizeof(gApp.statusText));
                ChatWindow_UpdateStatus();

            }
        }
        return;
    }

    /* Skip special tokens (don't display control tokens) */
    if (tokenID <= 4 || Tokenizer_IsCommandToken(tokenID))
    {
        /* Detect CMD:APPLESCRIPT - suppress subsequent script tokens */
        if (tokenID == kTokenCmdAppleScript)
        {
            char *asMsg = "\rGenerating AppleScript...";
            AppendToOutput(asMsg, strlen(asMsg));
            SafeStringCopy(gApp.statusText, "Generating AppleScript...",
                          sizeof(gApp.statusText));
            ChatWindow_UpdateStatus();
            sSuppressAppleScript = true;
            return;
        }

        /* Update status for other command tokens */
        if (Tokenizer_IsCommandToken(tokenID))
        {
            SafeStringCopy(gApp.statusText, "Processing command...",
                          sizeof(gApp.statusText));
            ChatWindow_UpdateStatus();
        }
        return;
    }

    /* Decode token to text */
    decoded = Tokenizer_Decode(&tokenID, 1, tokenText, sizeof(tokenText));
    if (decoded > 0 && tokenText[0] != '\0')
    {
        /* Convert Unix newlines to Mac newlines for display */
        long ci;
        for (ci = 0; ci < decoded; ci++) {
            if (tokenText[ci] == '\n')
                tokenText[ci] = '\r';
        }
        AppendToOutput(tokenText, strlen(tokenText));

        /* Streaming speech: speak word-by-word during generation.
           Controlled by feedbackSounds setting ("Speak during generation").
           autoSpeak = speak after generation (smooth full response). */
        if (gApp.settings.speech.streamSpeech && Speech_IsAvailable())
        {
            /* If new token starts with space, speak buffered word first */
            if (sSpeechBufLen > 0 && decoded > 0)
            {
                char firstChar;
                firstChar = tokenText[0];
                if (firstChar == ' ' || firstChar == '\r'
                    || sSpeechBufLen >= 200)
                {
                    while (Speech_IsBusy())
                        SystemTask();
                    Speech_SpeakText(sSpeechBuf, sSpeechBufLen);
                    sSpeechBufLen = 0;
                }
            }

            /* Accumulate token text into buffer */
            for (ci = 0; ci < decoded && sSpeechBufLen < 250; ci++)
            {
                sSpeechBuf[sSpeechBufLen++] = tokenText[ci];
            }
            sSpeechBuf[sSpeechBufLen] = '\0';

            /* Speak immediately on sentence-ending punctuation */
            if (sSpeechBufLen > 0)
            {
                char lastChar;
                lastChar = sSpeechBuf[sSpeechBufLen - 1];
                if (lastChar == '.' || lastChar == '!'
                    || lastChar == '?' || lastChar == '\r')
                {
                    while (Speech_IsBusy())
                        SystemTask();
                    Speech_SpeakText(sSpeechBuf, sSpeechBufLen);
                    sSpeechBufLen = 0;
                }
            }
        }
    }

    /* Update status with token count */
    sprintf(statusMsg, "Generating... (%ld tokens)", numGenerated);
    SafeStringCopy(gApp.statusText, statusMsg, sizeof(gApp.statusText));
    ChatWindow_UpdateStatus();
}

/*------------------------------------------------------------------------------
    Message_Send
------------------------------------------------------------------------------*/
void Message_Send(void)
{
    Handle inputHandle;
    char inputText[kMaxInputLen];
    long inputLen;
    long cmdToken;
    char cmdArgument[1024];
    char actionResult[512];

    DebugLog_Write("Message_Send: begin");

    /* Initialize buffers */
    inputText[0] = '\0';
    cmdArgument[0] = '\0';
    cmdToken = -1;

    if (gInputTE == nil)
        return;

    /* Get input text */
    HLock((Handle)gInputTE);
    inputHandle = (*gInputTE)->hText;
    inputLen = (*gInputTE)->teLength;
    HUnlock((Handle)gInputTE);

    if (inputLen <= 0 || inputLen >= kMaxInputLen)
        return;

    HLock(inputHandle);
    BlockMoveData(*inputHandle, inputText, inputLen);
    inputText[inputLen] = '\0';
    HUnlock(inputHandle);

    /* Clear input field */
    TESetSelect(0, 32767, gInputTE);
    TEDelete(gInputTE);

    /* Display user message */
    {
        char userLine[kMaxInputLen + 32];
        sprintf(userLine, "You: %s\r\r", inputText);
        AppendToOutput(userLine, strlen(userLine));
    }

    /* Check if router + engine + tokenizer are ready */
    if (!QueryRouter_IsReady())
    {
        char *notReady = "MacinAI: No model loaded. Use File > Open to load a model.\r\r";
        AppendToOutput(notReady, strlen(notReady));
        if (gApp.settings.speech.autoSpeak)
            Speech_SpeakText(notReady, strlen(notReady));
        return;
    }

    /* Handle pending choice selection (numbered list from CatalogResolver) */
    if (gApp.choiceState.awaitingChoice)
    {
        short choice;
        choice = 0;
        if (inputLen == 1 && inputText[0] >= '1' && inputText[0] <= '9')
        {
            choice = inputText[0] - '0';
        }
        else if (inputLen == 2 && inputText[0] >= '1' && inputText[0] <= '2'
                && inputText[1] >= '0' && inputText[1] <= '9')
        {
            choice = (inputText[0] - '0') * 10 + (inputText[1] - '0');
        }

        if (choice >= 1 && choice <= gApp.choiceState.resultCount)
        {
            char msg[256];
            sprintf(msg, "MacinAI: Opening %s...\r\r",
                    gApp.choiceState.results[choice - 1].name);
            AppendToOutput(msg, strlen(msg));
            if (gApp.settings.speech.autoSpeak)
                Speech_SpeakText(msg, strlen(msg));

            ActionDispatcher_ProcessToken(
                (short)gApp.choiceState.pendingAction + kTokenCmdNone,
                gApp.choiceState.results[choice - 1].path,
                actionResult, sizeof(actionResult));

            gApp.choiceState.awaitingChoice = false;
            gApp.choiceState.resultCount = 0;
        }
        else
        {
            /* Not a valid choice - clear state and process normally */
            gApp.choiceState.awaitingChoice = false;
            gApp.choiceState.resultCount = 0;
        }

        ConvMgr_SetUnsavedChanges(true);
        return;
    }

    /* Route query through QueryRouter (Canned -> Lookup -> Model) */
    {
        QueryResult qResult;
        OSErr routeErr;

        SafeStringCopy(gApp.statusText, "Thinking...", sizeof(gApp.statusText));
        ChatWindow_UpdateStatus();
        gApp.model.isGenerating = true;

        /* Set up streaming callbacks for model route */
        sStreamingStarted = false;
        sSawSep = false;
        sSuppressAppleScript = false;
        sSuppressedTokenCount = 0;
        sSpeechBufLen = 0;
        gEngineTokenCallback = StreamTokenCallback;
        gEnginePrefillCallback = PrefillProgressCallback;

        routeErr = QueryRouter_ProcessQuery(inputText, &qResult);

        gEngineTokenCallback = nil;
        gEnginePrefillCallback = nil;
        gApp.model.isGenerating = false;

        /* Flush any remaining speech buffer from streaming */
        if (sSpeechBufLen > 0 && gApp.settings.speech.feedbackSounds
            && Speech_IsAvailable())
        {
            while (Speech_IsBusy())
                SystemTask();
            Speech_SpeakText(sSpeechBuf, sSpeechBufLen);
            sSpeechBufLen = 0;
        }

        DebugLog_WriteNum("Message_Send: routeUsed =", (long)qResult.routeUsed);
        DebugLog_WriteNum("Message_Send: tokensGenerated =", qResult.tokensGenerated);
        DebugLog_WriteNum("Message_Send: cmdToken =", qResult.commandToken);

        if (routeErr != noErr)
        {
            char *routeErrMsg = "MacinAI: Sorry, I couldn't process your message.\r\r";
            AppendToOutput(routeErrMsg, strlen(routeErrMsg));
            if (gApp.settings.speech.autoSpeak)
                Speech_SpeakText(routeErrMsg, strlen(routeErrMsg));
            SafeStringCopy(gApp.statusText, "Ready", sizeof(gApp.statusText));
            ChatWindow_UpdateStatus();
            return;
        }

        /* Track token counts */
        if (qResult.tokensGenerated > 0)
            gApp.model.totalTokensOut += qResult.tokensGenerated;

        /* Display response */
        if (qResult.response[0] != '\0')
        {
            long respLen;
            char suffix[] = "\r\r";

            respLen = strlen(qResult.response);

            /* For non-model routes, text wasn't streamed -- display it now */
            if (qResult.routeUsed != kRouteToModel)
            {
                char prefix[] = "MacinAI: ";
                AppendToOutput(prefix, strlen(prefix));
                AppendToOutput(qResult.response, respLen);
            }

            AppendToOutput(suffix, strlen(suffix));

            /* Speak full response after generation (smooth, not choppy).
               Skip if "speak during generation" already handled it. */
            if (gApp.settings.speech.autoSpeak
                && !gApp.settings.speech.streamSpeech)
                Speech_SpeakText(qResult.response, respLen);
        }

        /* Copy results for action dispatch */
        cmdToken = (long)qResult.commandToken;
        SafeStringCopy(cmdArgument, qResult.commandArg, sizeof(cmdArgument));
    }

    /* Execute command action if present (not CMD:NONE) */
    if (cmdToken > kTokenCmdNone)
    {
        short actionRet;
        actionRet = ActionDispatcher_ProcessToken(cmdToken, cmdArgument,
                                                  actionResult, sizeof(actionResult));

        /* Show action result */
        if (actionRet == kActionResultSuccess)
        {
            char resultLine[512];
            sprintf(resultLine, "[%s]\r\r", actionResult);
            AppendToOutput(resultLine, strlen(resultLine));
        }
        else if (actionRet < 0 && actionRet != kActionResultNoAction)
        {
            char errLine[512];
            sprintf(errLine, "[Error: %s]\r\r", actionResult);
            AppendToOutput(errLine, strlen(errLine));
        }
    }

    /* Play beep on response */
    AppBeep();

    /* Update state */
    ConvMgr_SetUnsavedChanges(true);
    SafeStringCopy(gApp.statusText, "Ready", sizeof(gApp.statusText));
    ChatWindow_UpdateStatus();
}
