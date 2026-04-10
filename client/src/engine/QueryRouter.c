/*----------------------------------------------------------------------
    QueryRouter.c - Top-Level Query Routing for MacinAI Local

    Coordinates InferenceGuard -> MacSpecsTable -> Engine -> PostProcess
    pipeline. Single entry point for all user queries.

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
----------------------------------------------------------------------*/

#pragma segment Engine

#include "QueryRouter.h"
#include "AppGlobals.h"
#include "InferenceGuard.h"
#include "InferenceEngine.h"
#include "MacSpecsTable.h"
#include "Tokenizer.h"
#include "DebugLog.h"
#include <string.h>
#include <stdio.h>

/*----------------------------------------------------------------------
    Forward declarations
----------------------------------------------------------------------*/
static void InitResult(QueryResult *result);
static OSErr HandleCannedRoute(const PreProcessResult *preResult,
                               QueryResult *result);
static OSErr HandleLookupRoute(const PreProcessResult *preResult,
                               QueryResult *result);
static OSErr HandleModelRoute(const char *query,
                              const long *history, long historyLen,
                              QueryResult *result);
static long BuildPromptTokens(const char *query,
                              const long *history, long historyLen,
                              long *tokens, long maxTokens);
static long BuildPromptTokensChatML(const char *query,
                                    long *tokens, long maxTokens);
static long BuildPromptTokensZephyr(const char *query,
                                    long *tokens, long maxTokens);
static long BuildPromptTokensRaw(const char *query,
                                  long *tokens, long maxTokens);
static void ExtractCommandFromTokens(const long *tokens, long numTokens,
                                     QueryResult *result);
static void DecodeResponseText(const long *tokens, long numTokens,
                               char *output, short maxLen,
                               short *outLen);

/*----------------------------------------------------------------------
    Module state
----------------------------------------------------------------------*/
static Boolean sInitialized = false;

/*----------------------------------------------------------------------
    QueryRouter_Initialize - Set up all sub-modules
----------------------------------------------------------------------*/
OSErr QueryRouter_Initialize(const FSSpec *modelFile,
                             const FSSpec *vocabFile)
{
    OSErr err;

    if (sInitialized) {
        return noErr;
    }

    /* Skip sub-modules that are already initialized (e.g. by SplashScreen) */
    if (!Tokenizer_IsReady()) {
        err = Tokenizer_Initialize(vocabFile);
        if (err != noErr) {
            return err;
        }
    }

    if (!Engine_IsReady()) {
        err = Engine_InitializeWithProgress();
        if (err != noErr) {
            Tokenizer_Cleanup();
            return err;
        }

        err = Engine_LoadModel(modelFile);
        if (err != noErr) {
            Engine_Cleanup();
            Tokenizer_Cleanup();
            return err;
        }
    }

    /* MacSpecsTable and InferenceGuard are stateless - no init needed */

    sInitialized = true;
    return noErr;
}

/*----------------------------------------------------------------------
    QueryRouter_ProcessQuery - Main entry point for user queries
----------------------------------------------------------------------*/
OSErr QueryRouter_ProcessQuery(const char *query, QueryResult *result)
{
    return QueryRouter_ProcessQueryWithHistory(nil, 0, query, result);
}

/*----------------------------------------------------------------------
    QueryRouter_ProcessQueryWithHistory - Process with conversation context
----------------------------------------------------------------------*/
OSErr QueryRouter_ProcessQueryWithHistory(const long *history,
                                          long historyLen,
                                          const char *query,
                                          QueryResult *result)
{
    PreProcessResult preResult;

    InitResult(result);

    if (query == nil || query[0] == '\0') {
        strcpy(result->response, "I didn't catch that. Could you try again?");
        result->responseLen = (short)strlen(result->response);
        result->commandToken = kTokenCmdNone;
        return noErr;
    }

    /* External models: skip MacinAI-specific routing, go straight to model */
    if (Tokenizer_IsExternalModel()) {
        return HandleModelRoute(query, history, historyLen, result);
    }

    /* Step 1: Pre-process -- determine routing tier (MacinAI models only) */
    preResult = InferenceGuard_PreProcess(query);

    /* Step 2: Route to appropriate handler */
    switch (preResult.route) {
        case kRouteToCanned:
            return HandleCannedRoute(&preResult, result);

        case kRouteToLookup:
            return HandleLookupRoute(&preResult, result);

        case kRouteToModel:
            return HandleModelRoute(query, history, historyLen, result);
    }

    /* Should never reach here */
    return noErr;
}

/*----------------------------------------------------------------------
    QueryRouter_StopGeneration - Abort in-progress model inference
----------------------------------------------------------------------*/
void QueryRouter_StopGeneration(void)
{
    Engine_StopGeneration();
}

/*----------------------------------------------------------------------
    QueryRouter_IsReady - Check if all sub-modules are initialized
----------------------------------------------------------------------*/
Boolean QueryRouter_IsReady(void)
{
    if (!sInitialized)
        return false;
    if (!Tokenizer_IsReady())
        return false;
    if (!Engine_IsReady())
        return false;
    return true;
}

/*----------------------------------------------------------------------
    QueryRouter_GetStatusString - Status for display
----------------------------------------------------------------------*/
void QueryRouter_GetStatusString(char *buffer, short maxLen)
{
    short used;
    short specsCount;

    if (buffer == nil || maxLen < 2) {
        return;
    }

    buffer[0] = '\0';

    if (!sInitialized) {
        strncpy(buffer, "Router: not initialized", maxLen - 1);
        buffer[maxLen - 1] = '\0';
        return;
    }

    /* Engine status (shows Q8/f32, layers, tier) */
    Engine_GetStatusString(buffer, maxLen);
    used = (short)strlen(buffer);

    /* Append specs table info */
    specsCount = MacSpecs_GetCount();
    if (used + 30 < maxLen) {
        sprintf(buffer + used, " | %d Mac specs", specsCount);
    }
}

/*----------------------------------------------------------------------
    QueryRouter_Cleanup - Shut down all sub-modules
----------------------------------------------------------------------*/
void QueryRouter_Cleanup(void)
{
    Engine_Cleanup();
    Tokenizer_Cleanup();
    sInitialized = false;
}

/*======================================================================
    Internal: Route Handlers
======================================================================*/

/*----------------------------------------------------------------------
    InitResult - Zero out a QueryResult
----------------------------------------------------------------------*/
static void InitResult(QueryResult *result)
{
    if (result == nil) {
        return;
    }
    result->response[0] = '\0';
    result->responseLen = 0;
    result->commandToken = kTokenCmdNone;
    result->commandArg[0] = '\0';
    result->routeUsed = kRouteToModel;
    result->tokensGenerated = 0;
}

/*----------------------------------------------------------------------
    HandleCannedRoute - Return pre-written refusal/boundary response
----------------------------------------------------------------------*/
static OSErr HandleCannedRoute(const PreProcessResult *preResult,
                               QueryResult *result)
{
    short len;

    len = (short)strlen(preResult->cannedResponse);
    if (len >= kMaxResponseLen) {
        len = kMaxResponseLen - 1;
    }

    strncpy(result->response, preResult->cannedResponse, len);
    result->response[len] = '\0';
    result->responseLen = len;
    result->commandToken = kTokenCmdNone;
    result->routeUsed = kRouteToCanned;
    result->tokensGenerated = 0;

    return noErr;
}

/*----------------------------------------------------------------------
    HandleLookupRoute - Format hardware specs from lookup table
----------------------------------------------------------------------*/
static OSErr HandleLookupRoute(const PreProcessResult *preResult,
                               QueryResult *result)
{
    short written;

    if (preResult->spec == nil) {
        /* Shouldn't happen, but handle gracefully */
        strcpy(result->response,
               "I found a Mac model reference but couldn't "
               "look up the details. Could you try rephrasing?");
        result->responseLen = (short)strlen(result->response);
        result->commandToken = kTokenCmdNone;
        result->routeUsed = kRouteToLookup;
        return noErr;
    }

    written = MacSpecs_FormatAnswer(preResult->spec,
                                    result->response,
                                    kMaxResponseLen);
    result->responseLen = written;
    result->commandToken = kTokenCmdNone;
    result->routeUsed = kRouteToLookup;
    result->tokensGenerated = 0;

    return noErr;
}

/*----------------------------------------------------------------------
    HandleModelRoute - Full neural model inference pipeline

    For custom MacinAI models:
      1. Build prompt: [BOS] System:... [SEP] User:... [SEP] Assistant:
      2. Generate, extract command token, decode text before [SEP]

    For external models (ChatML):
      1. Build prompt: <|im_start|>user\n...<|im_end|>\n<|im_start|>assistant\n
      2. Generate, decode all text until EOS, no command extraction
----------------------------------------------------------------------*/
static OSErr HandleModelRoute(const char *query,
                              const long *history, long historyLen,
                              QueryResult *result)
{
    long promptTokens[kMaxPromptTokens];
    long outputTokens[kMaxOutputTokens];
    long promptLen;
    long numGenerated;
    short rawLen;
    Boolean isExternal;

    result->routeUsed = kRouteToModel;
    isExternal = Tokenizer_IsExternalModel();

    /* 1. Build prompt token sequence */
    if (Tokenizer_GetChatTemplate() == kChatTemplateRaw) {
        promptLen = BuildPromptTokensRaw(query,
                                          promptTokens, kMaxPromptTokens);
    } else if (Tokenizer_GetChatTemplate() == kChatTemplateZephyr) {
        promptLen = BuildPromptTokensZephyr(query,
                                            promptTokens, kMaxPromptTokens);
    } else if (isExternal) {
        promptLen = BuildPromptTokensChatML(query,
                                            promptTokens, kMaxPromptTokens);
    } else {
        promptLen = BuildPromptTokens(query, history, historyLen,
                                      promptTokens, kMaxPromptTokens);
    }
    if (promptLen <= 0) {
        strcpy(result->response,
               "I had trouble understanding that query. "
               "Could you try a shorter question?");
        result->responseLen = (short)strlen(result->response);
        result->commandToken = kTokenCmdNone;
        return noErr;
    }

    /* 2. Generate output tokens */
    numGenerated = Engine_Generate(promptTokens, promptLen,
                                   outputTokens, kMaxOutputTokens);
    if (numGenerated <= 0) {
        strcpy(result->response,
               "I wasn't able to generate a response. "
               "The model may need more memory.");
        result->responseLen = (short)strlen(result->response);
        result->commandToken = kTokenCmdNone;
        return noErr;
    }

    result->tokensGenerated = numGenerated;

    /* 3. Extract command token (custom models only) */
    if (!isExternal) {
        ExtractCommandFromTokens(outputTokens, numGenerated, result);
    }

    /* 4. Decode response text */
    DecodeResponseText(outputTokens, numGenerated,
                       result->response, kMaxResponseLen,
                       &rawLen);

    /* 5. Post-process: repetition detection, cleanup */
    result->responseLen = InferenceGuard_PostProcess(result->response,
                                                     rawLen);

    /* 6. Convert Unix newlines to Mac newlines (\n -> \r) as final step */
    {
        long j;
        for (j = 0; j < result->responseLen; j++) {
            if (result->response[j] == '\n')
                result->response[j] = '\r';
        }
    }

    return noErr;
}

/*======================================================================
    Internal: Token Handling
======================================================================*/

/*----------------------------------------------------------------------
    BuildPromptTokens - Construct the input token sequence

    Format (matches SFT training):
      [BOS] System: X.X, CPU: XXXX, RAM: XXMB [SEP] User: <query> [SEP] Assistant:

    With history:
      <history tokens> System: ... [SEP] User: <query> [SEP] Assistant:

    History already contains [BOS] and prior turns.
----------------------------------------------------------------------*/
static long BuildPromptTokens(const char *query,
                              const long *history, long historyLen,
                              long *tokens, long maxTokens)
{
    long pos;
    long sysCtxTokens[64];
    long numSysCtxTokens;
    long queryTokens[kMaxPromptTokens];
    long numQueryTokens;
    long suffixTokens[kMaxPromptTokens];
    long numSuffixTokens;
    long i;

    pos = 0;
    numSysCtxTokens = 0;

    /* Build system context string matching training format:
       " System: 7.5.3, CPU: 68040, RAM: 32MB" */
    {
        char sysCtx[128];
        const char *cpuName;
        long ramMB;

        /* Map Gestalt CPU type to training-compatible name */
        if (gApp.hardware.cpuType >= 0x010C)
            cpuName = "G4";
        else if (gApp.hardware.cpuType >= 0x010A)
            cpuName = "G3";
        else if (gApp.hardware.cpuType >= 0x0104)
            cpuName = "604e";
        else if (gApp.hardware.cpuType >= 0x0103)
            cpuName = "603e";
        else if (gApp.hardware.cpuType >= 0x0100)
            cpuName = "PowerPC";
        else if (gApp.hardware.cpuType >= 4)
            cpuName = "68040";
        else if (gApp.hardware.cpuType >= 3)
            cpuName = "68030";
        else
            cpuName = "68020";

        ramMB = gApp.hardware.physicalRAM / (1024L * 1024L);

        sprintf(sysCtx, " System: %s, CPU: %s, RAM: %ldMB ",
                gApp.hardware.systemVersion, cpuName, ramMB);

        numSysCtxTokens = Tokenizer_Encode(sysCtx, sysCtxTokens, 64);
        if (numSysCtxTokens < 0)
            numSysCtxTokens = 0;
    }

    /* Encode the full user turn: " User: {query} " */
    {
        char formatted[2048];
        sprintf(formatted, " User: %s ", query);
        numQueryTokens = Tokenizer_Encode(formatted, queryTokens, kMaxPromptTokens);
        if (numQueryTokens <= 0) {
            return -1;
        }
    }

    /* Encode " Assistant:" suffix (placed after second [SEP]) */
    numSuffixTokens = Tokenizer_Encode(" Assistant:", suffixTokens, 8);
    if (numSuffixTokens < 0) {
        numSuffixTokens = 0;
    }

    /* Calculate total length:
       [BOS] + sysCtx + [SEP] + query + [SEP] + suffix */
    {
        long totalNeeded;
        totalNeeded = 1; /* [BOS] */
        if (history != nil && historyLen > 0) {
            totalNeeded += historyLen;
        }
        totalNeeded += numSysCtxTokens;
        totalNeeded += 1; /* first [SEP] after system context */
        totalNeeded += numQueryTokens;
        totalNeeded += 1; /* second [SEP] after user query */
        totalNeeded += numSuffixTokens;

        /* If too long, truncate history first */
        if (totalNeeded > maxTokens) {
            long needed;
            needed = 1 + numSysCtxTokens + 1 + numQueryTokens +
                     1 + numSuffixTokens;
            if (needed > maxTokens) {
                return -1;
            }
            if (history != nil && historyLen > 0) {
                long maxHist;
                maxHist = maxTokens - needed;
                if (historyLen > maxHist) {
                    history = history + (historyLen - maxHist);
                    historyLen = maxHist;
                }
            }
        }
    }

    /* Build the sequence */
    if (history != nil && historyLen > 0) {
        /* History already starts with [BOS] */
        for (i = 0; i < historyLen && pos < maxTokens; i++) {
            tokens[pos++] = history[i];
        }
    } else {
        /* Start fresh with [BOS] */
        tokens[pos++] = kTokenBOS;
    }

    /* System context tokens: " System: X.X, CPU: XXXX, RAM: XXMB" */
    for (i = 0; i < numSysCtxTokens && pos < maxTokens; i++) {
        tokens[pos++] = sysCtxTokens[i];
    }

    /* First [SEP] (between system context and user query) */
    if (pos < maxTokens) {
        tokens[pos++] = kTokenSEP;
    }

    /* " User: {query} " tokens */
    for (i = 0; i < numQueryTokens && pos < maxTokens; i++) {
        tokens[pos++] = queryTokens[i];
    }

    /* Second [SEP] (between user query and assistant) */
    if (pos < maxTokens) {
        tokens[pos++] = kTokenSEP;
    }

    /* " Assistant:" suffix (model continues from here) */
    for (i = 0; i < numSuffixTokens && pos < maxTokens; i++) {
        tokens[pos++] = suffixTokens[i];
    }

    /* Debug: dump input token IDs for comparison with Python tokenizer */
    {
        char dbg[64];
        sprintf(dbg, "BuildPromptTokens: total=%ld sysCtx=%ld query=%ld suffix=%ld",
                pos, numSysCtxTokens, numQueryTokens, numSuffixTokens);
        DebugLog_Write(dbg);
        for (i = 0; i < pos && i < 50; i++) {
            DebugLog_WriteNum2("  tok[i]=", i, tokens[i]);
        }
    }

    return pos;
}

/*----------------------------------------------------------------------
    BuildPromptTokensChatML - Build ChatML format prompt for external models

    ChatML format:
      <|im_start|>system\nYou are a helpful assistant.\n<|im_end|>\n
      <|im_start|>user\n{query}\n<|im_end|>\n
      <|im_start|>assistant\n

    Token IDs: <|im_start|>=1, <|im_end|>=2 (same as BOS/EOS)
----------------------------------------------------------------------*/
static long BuildPromptTokensChatML(const char *query,
                                    long *tokens, long maxTokens)
{
    long pos;
    long tmpTokens[kMaxPromptTokens];
    long numTmp;
    long i;
    char formatted[2048];
    long imStart;
    long imEnd;

    pos = 0;
    imStart = Tokenizer_GetImStartToken();
    imEnd = Tokenizer_GetImEndToken();

    /* <|im_start|>system\n...<|im_end|>\n */
    if (pos < maxTokens)
        tokens[pos++] = imStart;

    numTmp = Tokenizer_Encode("system\nYou are a helpful assistant.",
                              tmpTokens, kMaxPromptTokens);
    if (numTmp > 0) {
        for (i = 0; i < numTmp && pos < maxTokens; i++)
            tokens[pos++] = tmpTokens[i];
    }

    if (pos < maxTokens)
        tokens[pos++] = imEnd;

    numTmp = Tokenizer_Encode("\n", tmpTokens, 8);
    if (numTmp > 0) {
        for (i = 0; i < numTmp && pos < maxTokens; i++)
            tokens[pos++] = tmpTokens[i];
    }

    /* <|im_start|>user\n{query}<|im_end|>\n */
    if (pos < maxTokens)
        tokens[pos++] = imStart;

    sprintf(formatted, "user\n%s", query);
    numTmp = Tokenizer_Encode(formatted, tmpTokens, kMaxPromptTokens);
    if (numTmp > 0) {
        for (i = 0; i < numTmp && pos < maxTokens; i++)
            tokens[pos++] = tmpTokens[i];
    }

    if (pos < maxTokens)
        tokens[pos++] = imEnd;

    numTmp = Tokenizer_Encode("\n", tmpTokens, 8);
    if (numTmp > 0) {
        for (i = 0; i < numTmp && pos < maxTokens; i++)
            tokens[pos++] = tmpTokens[i];
    }

    /* <|im_start|>assistant\n */
    if (pos < maxTokens)
        tokens[pos++] = imStart;

    numTmp = Tokenizer_Encode("assistant\n", tmpTokens, 16);
    if (numTmp > 0) {
        for (i = 0; i < numTmp && pos < maxTokens; i++)
            tokens[pos++] = tmpTokens[i];
    }

    {
        char dbg[64];
        sprintf(dbg, "BuildPromptTokensChatML: total=%ld", pos);
        DebugLog_Write(dbg);
        for (i = 0; i < pos && i < 50; i++) {
            DebugLog_WriteNum2("  tok[i]=", i, tokens[i]);
        }
    }

    return pos;
}

/*----------------------------------------------------------------------
    BuildPromptTokensZephyr - Zephyr/TinyLlama/Mistral chat format

    Format: <|system|>\n{sysmsg}</s>\n<|user|>\n{query}</s>\n<|assistant|>\n

    All role tags are BPE-encoded text. </s> (token 2) separates turns.
----------------------------------------------------------------------*/
static long BuildPromptTokensZephyr(const char *query,
                                    long *tokens, long maxTokens)
{
    long pos;
    long tmpTokens[kMaxPromptTokens];
    long numTmp;
    long i;

    pos = 0;

    /* <|system|>\nYou are a helpful assistant.</s> + \n<|user|>\n{query}</s> + \n<|assistant|>\n
       Merge \n after </s> into the next text chunk to avoid SentencePiece
       adding a leading metaspace to standalone \n */
    /* Tokenizer_Encode adds leading ▁ (prepend_scheme="first") */
    numTmp = Tokenizer_Encode("<|system|>\nYou are a helpful assistant.",
                              tmpTokens, kMaxPromptTokens);
    if (numTmp > 0) {
        for (i = 0; i < numTmp && pos < maxTokens; i++)
            tokens[pos++] = tmpTokens[i];
    }
    if (pos < maxTokens)
        tokens[pos++] = 2;  /* </s> */

    /* Continuation chunks use EncodeNoPrefix to skip leading metaspace */
    {
        char userChunk[2048];
        long queryLen;

        /* Strip trailing whitespace from query */
        queryLen = (long)strlen(query);
        while (queryLen > 0 && (query[queryLen - 1] == ' ' ||
               query[queryLen - 1] == '\t'))
            queryLen--;

        sprintf(userChunk, "\n<|user|>\n%.*s", (int)queryLen, query);
        numTmp = Tokenizer_EncodeNoPrefix(userChunk, tmpTokens, kMaxPromptTokens);
        if (numTmp > 0) {
            for (i = 0; i < numTmp && pos < maxTokens; i++)
                tokens[pos++] = tmpTokens[i];
        }
    }
    if (pos < maxTokens)
        tokens[pos++] = 2;  /* </s> */

    numTmp = Tokenizer_EncodeNoPrefix("\n<|assistant|>\n", tmpTokens, 16);
    if (numTmp > 0) {
        for (i = 0; i < numTmp && pos < maxTokens; i++)
            tokens[pos++] = tmpTokens[i];
    }

    {
        char dbg[64];
        sprintf(dbg, "BuildPromptTokensZephyr: total=%ld", pos);
        DebugLog_Write(dbg);
        for (i = 0; i < pos && i < 50; i++) {
            DebugLog_WriteNum2("  tok[i]=", i, tokens[i]);
        }
    }

    return pos;
}

/*----------------------------------------------------------------------
    BuildPromptTokensRaw - Raw text input for GPT-2 style text completion

    No template wrapping at all. Just encode the user's text directly.
    For GPT-2: user types "The meaning of life is" and the model
    continues the text. EOS token (50256 = <|endoftext|>) stops generation.
----------------------------------------------------------------------*/
static long BuildPromptTokensRaw(const char *query,
                                  long *tokens, long maxTokens)
{
    long numTokens;
    long i;

    numTokens = Tokenizer_Encode(query, tokens, maxTokens);
    if (numTokens <= 0) {
        return -1;
    }

    {
        char dbg[64];
        sprintf(dbg, "BuildPromptTokensRaw: total=%ld", numTokens);
        DebugLog_Write(dbg);
        for (i = 0; i < numTokens && i < 50; i++) {
            DebugLog_WriteNum2("  tok[i]=", i, tokens[i]);
        }
    }

    return numTokens;
}

/*----------------------------------------------------------------------
    ExtractCommandFromTokens - Find command token in output

    Model output format after [SEP]:
      <response text> [SEP] [CMD:xxx] <argument> [EOS]

    We scan for the first command token (IDs 5-12) and extract
    any text after it as the command argument.
----------------------------------------------------------------------*/
static void ExtractCommandFromTokens(const long *tokens, long numTokens,
                                     QueryResult *result)
{
    long i;
    long cmdPos;
    long argStart;
    char argBuffer[kMaxCommandArgLen];
    long argLen;

    result->commandToken = kTokenCmdNone;
    result->commandArg[0] = '\0';

    /* Find the first command token */
    cmdPos = -1;
    for (i = 0; i < numTokens; i++) {
        if (Tokenizer_IsCommandToken(tokens[i])) {
            result->commandToken = tokens[i];
            cmdPos = i;
            break;
        }
    }

    if (cmdPos < 0) {
        /* No command token found -- default to CMD:NONE */
        return;
    }

    /* Extract argument: tokens after command token, before [EOS] */
    argStart = cmdPos + 1;
    if (argStart < numTokens && tokens[argStart] != kTokenEOS) {
        long argTokens[256];
        long numArgTokens;
        long j;

        numArgTokens = 0;
        for (j = argStart; j < numTokens && numArgTokens < 256; j++) {
            if (tokens[j] == kTokenEOS ||
                tokens[j] == kTokenBOS ||
                tokens[j] == kTokenSEP) {
                break;
            }
            if (Tokenizer_IsCommandToken(tokens[j])) {
                break;
            }
            argTokens[numArgTokens++] = tokens[j];
        }

        if (numArgTokens > 0) {
            argLen = Tokenizer_Decode(argTokens, numArgTokens,
                                      argBuffer, kMaxCommandArgLen);
            if (argLen > 0) {
                /* Strip leading/trailing whitespace */
                {
                    long start;
                    long end;
                    long copyLen;
                    start = 0;
                    end = argLen - 1;
                    while (start < argLen &&
                           (argBuffer[start] == ' ' ||
                            argBuffer[start] == '\t')) {
                        start++;
                    }
                    while (end > start &&
                           (argBuffer[end] == ' ' ||
                            argBuffer[end] == '\t' ||
                            argBuffer[end] == '\n')) {
                        end--;
                    }
                    copyLen = end - start + 1;
                    if (copyLen > 0 && copyLen < kMaxCommandArgLen) {
                        strncpy(result->commandArg,
                                argBuffer + start, copyLen);
                        result->commandArg[copyLen] = '\0';
                    }
                }
            }
        }
    }
}

/*----------------------------------------------------------------------
    DecodeResponseText - Decode output tokens to response text

    Stops at [SEP], [EOS], or a command token -- whichever comes first.
    The text portion is everything the model generated before the
    structural tokens.
----------------------------------------------------------------------*/
static void DecodeResponseText(const long *tokens, long numTokens,
                               char *output, short maxLen,
                               short *outLen)
{
    long responseTokens[kMaxOutputTokens];
    long numRespTokens;
    long decoded;
    long i;
    Boolean isExternal;

    numRespTokens = 0;
    *outLen = 0;
    output[0] = '\0';
    isExternal = Tokenizer_IsExternalModel();

    /* Collect tokens until we hit a boundary token */
    for (i = 0; i < numTokens && numRespTokens < kMaxOutputTokens; i++) {
        /* Always stop at EOS/PAD and model-specific im_end */
        if (tokens[i] == kTokenEOS || tokens[i] == kTokenPAD) {
            break;
        }
        if (isExternal && tokens[i] == Tokenizer_GetImEndToken()) {
            break;
        }
        if (!isExternal && tokens[i] == kTokenBOS) {
            break;
        }
        /* For custom models: also stop at SEP and command tokens */
        if (!isExternal) {
            if (tokens[i] == kTokenSEP) {
                break;
            }
            if (Tokenizer_IsCommandToken(tokens[i])) {
                break;
            }
        }
        responseTokens[numRespTokens++] = tokens[i];
    }

    if (numRespTokens == 0) {
        return;
    }

    /* Decode tokens to text */
    decoded = Tokenizer_Decode(responseTokens, numRespTokens,
                               output, maxLen);
    if (decoded < 0) {
        decoded = 0;
    }
    if (decoded >= maxLen) {
        decoded = maxLen - 1;
    }
    output[decoded] = '\0';

    /* Convert Unix newlines to Mac newlines (\n -> \r) */
    {
        long j;
        for (j = 0; j < decoded; j++) {
            if (output[j] == '\n')
                output[j] = '\r';
        }
    }

    /* Strip leading whitespace (model often generates leading space) */
    {
        long start;
        start = 0;
        while (start < decoded &&
               (output[start] == ' ' || output[start] == '\r')) {
            start++;
        }
        if (start > 0 && start < decoded) {
            long remaining;
            long j;
            remaining = decoded - start;
            for (j = 0; j < remaining; j++) {
                output[j] = output[start + j];
            }
            output[remaining] = '\0';
            decoded = remaining;
        }
    }

    *outLen = (short)decoded;
}
