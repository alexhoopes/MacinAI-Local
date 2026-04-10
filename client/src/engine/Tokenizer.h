/*------------------------------------------------------------------------------
    Tokenizer.h - BPE Tokenizer for MacinAI Local

    Byte Pair Encoding tokenizer that runs natively on the Mac.
    Loads vocabulary and merge rules from a text file (vocab.txt).

    Vocabulary: 8,192 BPE tokens + 14 special tokens = 8,206 total

    Special tokens (IDs 0-12, plus 8205):
        0  [PAD]            Padding
        1  [BOS]            Beginning of sequence
        2  [EOS]            End of sequence
        3  [UNK]            Unknown token
        4  [SEP]            Separator
        5  [CMD:NONE]       No action
        6  [CMD:LAUNCH_APP] Launch application
        7  [CMD:OPEN_CP]    Open control panel
        8  [CMD:QUERY_SYS]  Query system info
        9  [CMD:REFRESH]    Refresh
       10  [CMD:SHOW_ALERT] Show alert
       11  [CMD:SHUTDOWN]   Shutdown Mac
       12  [CMD:RESTART]    Restart Mac
     8205  [CMD:APPLESCRIPT] Execute AppleScript (added by expand_embedding)

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

#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <Types.h>
#include <Files.h>

/*------------------------------------------------------------------------------
    Constants
------------------------------------------------------------------------------*/
#define kVocabSize          8206
#define kNumSpecialTokens   14
#define kMaxTokenLen        64
#define kMaxMergeRules      8192

/* Special token IDs */
#define kTokenPAD           0
#define kTokenBOS           1
#define kTokenEOS           2
#define kTokenUNK           3
#define kTokenSEP           4
#define kTokenCmdNone       5
#define kTokenCmdLaunchApp  6
#define kTokenCmdOpenCP     7
#define kTokenCmdQuerySys   8
#define kTokenCmdRefresh    9
#define kTokenCmdShowAlert  10
#define kTokenCmdShutdown   11
#define kTokenCmdRestart    12
#define kTokenCmdAppleScript 8205   /* Added by expand_embedding.py (end of vocab) */

/*------------------------------------------------------------------------------
    Public API
------------------------------------------------------------------------------*/

/* Initialize tokenizer from vocab.txt file */
OSErr Tokenizer_Initialize(const FSSpec *vocabFile);

/* Encode text to token IDs */
/* text: input C string */
/* tokens: output buffer for token IDs */
/* maxTokens: size of output buffer */
/* Returns: number of tokens produced, or negative error */
long Tokenizer_Encode(const char *text, long *tokens, long maxTokens);

/* Decode token IDs to text */
/* tokens: input token IDs */
/* numTokens: number of tokens */
/* text: output buffer */
/* maxLen: size of output buffer */
/* Returns: number of chars written */
long Tokenizer_Decode(const long *tokens, long numTokens, char *text, long maxLen);

/* Get token string for a single token ID */
const char* Tokenizer_GetTokenString(long tokenID);

/* Check if a token is a command token (always false for external models) */
Boolean Tokenizer_IsCommandToken(long tokenID);

/* Set/query external model mode (disables command token detection) */
void Tokenizer_SetExternalModel(Boolean isExternal);
Boolean Tokenizer_IsExternalModel(void);

/* Get ChatML special token IDs (from model header) */
long Tokenizer_GetImStartToken(void);
long Tokenizer_GetImEndToken(void);

/* Get chat template type (0=custom, 1=ChatML, 2=raw, 3=Zephyr) */
long Tokenizer_GetChatTemplate(void);

/* Encode without SentencePiece leading metaspace (for continuation chunks) */
long Tokenizer_EncodeNoPrefix(const char *text, long *tokens, long maxTokens);

/* Check if tokenizer is loaded */
Boolean Tokenizer_IsReady(void);

/* Clean up tokenizer */
void Tokenizer_Cleanup(void);

#endif /* TOKENIZER_H */
