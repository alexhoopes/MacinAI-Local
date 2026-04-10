/*----------------------------------------------------------------------
    QueryRouter.h - Top-Level Query Routing for MacinAI Local

    Coordinates the complete query-to-response pipeline:
    1. InferenceGuard pre-processing (refusal / lookup / model routing)
    2. MacSpecsTable lookup for hardware questions (100% accurate)
    3. Tokenizer + Engine_Generate for neural model inference
    4. InferenceGuard post-processing (repetition, cleanup)
    5. Command token extraction for CatalogResolver actions

    This is the single entry point the client app calls when the
    user sends a message. All routing decisions are transparent.

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

#ifndef QUERYROUTER_H
#define QUERYROUTER_H

#include <Types.h>
#include <Files.h>
#include "InferenceGuard.h"
#include "Tokenizer.h"

/*----------------------------------------------------------------------
    Constants
----------------------------------------------------------------------*/

#define kMaxResponseLen      2048    /* Max response text length */
#define kMaxCommandArgLen    1024    /* Max command argument length (AppleScript can be 500+ bytes) */
#define kMaxPromptTokens     512     /* Max tokens in prompt */
#define kMaxOutputTokens     256     /* Max tokens model can generate */

/*----------------------------------------------------------------------
    QueryResult - Complete result of processing a user query
----------------------------------------------------------------------*/
typedef struct {
    /* Response text */
    char            response[kMaxResponseLen];
    short           responseLen;

    /* Command extraction (from model output) */
    long            commandToken;       /* kTokenCmdNone..kTokenCmdRestart */
    char            commandArg[kMaxCommandArgLen]; /* e.g., app name */

    /* Routing info (for status display / debugging) */
    InferenceRoute  routeUsed;          /* Which tier handled this */
    long            tokensGenerated;    /* 0 for non-model routes */
} QueryResult;

/*----------------------------------------------------------------------
    Public API
----------------------------------------------------------------------*/

/* Initialize the router and all sub-modules.
   modelFile: FSSpec for the .bin model weights
   vocabFile: FSSpec for vocab.txt tokenizer data
   Returns noErr on success. */
OSErr QueryRouter_Initialize(const FSSpec *modelFile,
                             const FSSpec *vocabFile);

/* Process a user query end-to-end.
   query: user's message (C string)
   result: filled with response text, command, and routing info
   Returns noErr on success. */
OSErr QueryRouter_ProcessQuery(const char *query, QueryResult *result);

/* Process query with conversation context.
   history: array of prior [BOS]...[EOS] token sequences
   historyLen: number of tokens in history
   query: current user message
   result: filled with response
   Returns noErr on success. */
OSErr QueryRouter_ProcessQueryWithHistory(const long *history,
                                          long historyLen,
                                          const char *query,
                                          QueryResult *result);

/* Stop any in-progress generation */
void QueryRouter_StopGeneration(void);

/* Check if router is fully initialized and ready */
Boolean QueryRouter_IsReady(void);

/* Get status string for display (shows route, model info) */
void QueryRouter_GetStatusString(char *buffer, short maxLen);

/* Clean up router and all sub-modules */
void QueryRouter_Cleanup(void);

#endif /* QUERYROUTER_H */
