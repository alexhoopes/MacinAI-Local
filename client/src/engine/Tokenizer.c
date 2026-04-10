/*------------------------------------------------------------------------------
    Tokenizer.c - BPE Tokenizer for MacinAI Local

    Byte Pair Encoding tokenizer that loads vocab and merge rules
    from the .bin model file's vocab section. Implements the same
    byte-level pre-tokenizer as HuggingFace's GPT-2 tokenizer.

    Encoding: text bytes -> unicode chars -> BPE merges -> token IDs
    Decoding: token IDs -> vocab strings -> unicode chars -> text bytes

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

#include "Tokenizer.h"
#include "InferenceEngine.h"
#include "SafeString.h"
#include <Memory.h>
#include <Files.h>
#include <string.h>
#include <stdio.h>
#include "DebugLog.h"

/* External prototype for arena allocation fallback (large vocabs) */
extern Ptr Engine_ArenaAlloc(long size);

#pragma segment Engine

/*------------------------------------------------------------------------------
    Forward declarations
------------------------------------------------------------------------------*/
static unsigned long HashString(const char *str, short len);
static void HashTableInsert(const char *str, short len, long tokenID);
static long HashTableLookup(const char *str, short len);
static char *StringPoolAlloc(short len);
static short UTF8CharLen(unsigned char byte);
static long DecodeUTF8Char(const unsigned char *src, short *bytesRead);
static long EncodeChunk(const char *text, long chunkLen,
                        long *tokens, long maxTokens);
static long EncodeChunkSentencePiece(const char *text, long chunkLen,
                                     long *tokens, long maxTokens);

/*------------------------------------------------------------------------------
    Constants
------------------------------------------------------------------------------*/
#define kDefaultHashSize    16384   /* Default, scaled up for large vocabs */
#define kDefaultPoolSize    262144L /* 256KB default, scaled for large vocabs */
#define kMaxPieces          1024    /* Max BPE pieces during encoding */
#define kMaxWorkBuf         4096    /* Working buffer for byte->unicode conversion */

/*------------------------------------------------------------------------------
    Module Globals
------------------------------------------------------------------------------*/
static Boolean gTokenizerReady = false;
static Boolean sIsExternalModel = false;
static long sImStartToken = 1;    /* <|im_start|> token ID, default matches SmolLM */
static long sImEndToken = 2;      /* <|im_end|> token ID, default matches SmolLM */
static short sPreTokenizerType = 0; /* 0=GPT-2, 1=Qwen, 2=SentencePiece */
static long sChatTemplate = 0;     /* 0=custom, 1=ChatML, 2=raw, 3=Zephyr */
static Boolean sSuppressPrefix = false;  /* Skip leading metaspace for continuation chunks */

/* Vocab: token ID -> string */
static char  **gVocabStrings = nil;     /* [vocabSize] pointers */
static short  *gVocabLens = nil;        /* [vocabSize] string lengths */
static long    gVocabSize = 0;

/* Merge rules: pairs of strings in priority order */
static char  **gMergeFirst = nil;       /* [numMerges] first token */
static char  **gMergeSecond = nil;      /* [numMerges] second token */
static short  *gMergeFirstLen = nil;
static short  *gMergeSecondLen = nil;
static long    gNumMerges = 0;

/* Hash table: string -> token ID (open addressing, linear probing) */
static long   *gHashIDs = nil;          /* Token ID at each slot, -1 = empty */
static long    gHashSize = kDefaultHashSize;  /* Scaled for vocab size */
static long    gHashMask = kDefaultHashSize - 1;

/* String pool: all vocab + merge strings stored contiguously */
static char   *gStringPool = nil;
static long    gStringPoolUsed = 0;
static long    gStringPoolSize = kDefaultPoolSize;

/*------------------------------------------------------------------------------
    AllocClear - Allocate zeroed memory with fallback chain
    Tries app heap, then system heap, then engine arena.
    Arena fallback is needed for large vocabs (151K+) where the arena
    has consumed most available heap memory. Arena-allocated memory
    is freed when Engine_Cleanup disposes the arena.
------------------------------------------------------------------------------*/
static Boolean sUsingArenaAlloc = false;  /* true if any alloc used arena */

static Ptr AllocClear(long size)
{
    Ptr p;
    long i;

    p = NewPtrClear(size);  /* App heap, pre-cleared */
    if (p != nil)
        return p;

    /* App heap full - try system heap */
    p = NewPtrSys(size);
    if (p != nil)
    {
        for (i = 0; i < size; i++)
            p[i] = 0;
        return p;
    }

    /* Both heaps full - allocate from engine arena's remaining space */
    DebugLog_WriteNum("AllocClear: heap failed, trying arena for size =", size);
    p = Engine_ArenaAlloc(size);
    if (p != nil)
    {
        DebugLog_Write("AllocClear: arena OK");
        for (i = 0; i < size; i++)
            p[i] = 0;
        sUsingArenaAlloc = true;
        return p;
    }

    DebugLog_WriteNum("AllocClear: ALL FAILED for size =", size);
    return nil;
}

/* BPE working buffers (reused each encode call) */
static short   gPieceStart[kMaxPieces]; /* Start offset in work buffer */
static short   gPieceLen[kMaxPieces];   /* Length of this piece */
static char    gWorkBuf[kMaxWorkBuf];   /* Byte->unicode converted input */

/*------------------------------------------------------------------------------
    Special token strings
------------------------------------------------------------------------------*/
static const char *kSpecialTokenStrings[] = {
    "[PAD]", "[BOS]", "[EOS]", "[UNK]", "[SEP]",
    "[CMD:NONE]", "[CMD:LAUNCH_APP]", "[CMD:OPEN_CP]",
    "[CMD:QUERY_SYS]", "[CMD:REFRESH]", "[CMD:SHOW_ALERT]",
    "[CMD:SHUTDOWN]", "[CMD:RESTART]"
};

/*------------------------------------------------------------------------------
    Byte-to-Unicode Mapping (GPT-2 ByteLevel pre-tokenizer)

    Each input byte maps to a specific Unicode codepoint.
    Stored as UTF-8 bytes: {length, byte1, byte2}.
    Printable ASCII (33-126) maps to itself (1 byte).
    Space (32) maps to U+0120 = 0xC4 0xA0.
    Other bytes map to U+0100+ range (2 bytes).
------------------------------------------------------------------------------*/
static const unsigned char gByteToUTF8[256][3] = {
    {2,0xC4,0x80},{2,0xC4,0x81},{2,0xC4,0x82},{2,0xC4,0x83},  /*   0-  3 */
    {2,0xC4,0x84},{2,0xC4,0x85},{2,0xC4,0x86},{2,0xC4,0x87},  /*   4-  7 */
    {2,0xC4,0x88},{2,0xC4,0x89},{2,0xC4,0x8A},{2,0xC4,0x8B},  /*   8- 11 */
    {2,0xC4,0x8C},{2,0xC4,0x8D},{2,0xC4,0x8E},{2,0xC4,0x8F},  /*  12- 15 */
    {2,0xC4,0x90},{2,0xC4,0x91},{2,0xC4,0x92},{2,0xC4,0x93},  /*  16- 19 */
    {2,0xC4,0x94},{2,0xC4,0x95},{2,0xC4,0x96},{2,0xC4,0x97},  /*  20- 23 */
    {2,0xC4,0x98},{2,0xC4,0x99},{2,0xC4,0x9A},{2,0xC4,0x9B},  /*  24- 27 */
    {2,0xC4,0x9C},{2,0xC4,0x9D},{2,0xC4,0x9E},{2,0xC4,0x9F},  /*  28- 31 */
    {2,0xC4,0xA0},                                             /*  32 (space) */
    {1,0x21,0x00},{1,0x22,0x00},{1,0x23,0x00},{1,0x24,0x00},  /*  33- 36 */
    {1,0x25,0x00},{1,0x26,0x00},{1,0x27,0x00},{1,0x28,0x00},  /*  37- 40 */
    {1,0x29,0x00},{1,0x2A,0x00},{1,0x2B,0x00},{1,0x2C,0x00},  /*  41- 44 */
    {1,0x2D,0x00},{1,0x2E,0x00},{1,0x2F,0x00},{1,0x30,0x00},  /*  45- 48 */
    {1,0x31,0x00},{1,0x32,0x00},{1,0x33,0x00},{1,0x34,0x00},  /*  49- 52 */
    {1,0x35,0x00},{1,0x36,0x00},{1,0x37,0x00},{1,0x38,0x00},  /*  53- 56 */
    {1,0x39,0x00},{1,0x3A,0x00},{1,0x3B,0x00},{1,0x3C,0x00},  /*  57- 60 */
    {1,0x3D,0x00},{1,0x3E,0x00},{1,0x3F,0x00},{1,0x40,0x00},  /*  61- 64 */
    {1,0x41,0x00},{1,0x42,0x00},{1,0x43,0x00},{1,0x44,0x00},  /*  65- 68 */
    {1,0x45,0x00},{1,0x46,0x00},{1,0x47,0x00},{1,0x48,0x00},  /*  69- 72 */
    {1,0x49,0x00},{1,0x4A,0x00},{1,0x4B,0x00},{1,0x4C,0x00},  /*  73- 76 */
    {1,0x4D,0x00},{1,0x4E,0x00},{1,0x4F,0x00},{1,0x50,0x00},  /*  77- 80 */
    {1,0x51,0x00},{1,0x52,0x00},{1,0x53,0x00},{1,0x54,0x00},  /*  81- 84 */
    {1,0x55,0x00},{1,0x56,0x00},{1,0x57,0x00},{1,0x58,0x00},  /*  85- 88 */
    {1,0x59,0x00},{1,0x5A,0x00},{1,0x5B,0x00},{1,0x5C,0x00},  /*  89- 92 */
    {1,0x5D,0x00},{1,0x5E,0x00},{1,0x5F,0x00},{1,0x60,0x00},  /*  93- 96 */
    {1,0x61,0x00},{1,0x62,0x00},{1,0x63,0x00},{1,0x64,0x00},  /*  97-100 */
    {1,0x65,0x00},{1,0x66,0x00},{1,0x67,0x00},{1,0x68,0x00},  /* 101-104 */
    {1,0x69,0x00},{1,0x6A,0x00},{1,0x6B,0x00},{1,0x6C,0x00},  /* 105-108 */
    {1,0x6D,0x00},{1,0x6E,0x00},{1,0x6F,0x00},{1,0x70,0x00},  /* 109-112 */
    {1,0x71,0x00},{1,0x72,0x00},{1,0x73,0x00},{1,0x74,0x00},  /* 113-116 */
    {1,0x75,0x00},{1,0x76,0x00},{1,0x77,0x00},{1,0x78,0x00},  /* 117-120 */
    {1,0x79,0x00},{1,0x7A,0x00},{1,0x7B,0x00},{1,0x7C,0x00},  /* 121-124 */
    {1,0x7D,0x00},{1,0x7E,0x00},                               /* 125-126 */
    {2,0xC4,0xA1},                                             /* 127 */
    {2,0xC4,0xA2},{2,0xC4,0xA3},{2,0xC4,0xA4},{2,0xC4,0xA5},  /* 128-131 */
    {2,0xC4,0xA6},{2,0xC4,0xA7},{2,0xC4,0xA8},{2,0xC4,0xA9},  /* 132-135 */
    {2,0xC4,0xAA},{2,0xC4,0xAB},{2,0xC4,0xAC},{2,0xC4,0xAD},  /* 136-139 */
    {2,0xC4,0xAE},{2,0xC4,0xAF},{2,0xC4,0xB0},{2,0xC4,0xB1},  /* 140-143 */
    {2,0xC4,0xB2},{2,0xC4,0xB3},{2,0xC4,0xB4},{2,0xC4,0xB5},  /* 144-147 */
    {2,0xC4,0xB6},{2,0xC4,0xB7},{2,0xC4,0xB8},{2,0xC4,0xB9},  /* 148-151 */
    {2,0xC4,0xBA},{2,0xC4,0xBB},{2,0xC4,0xBC},{2,0xC4,0xBD},  /* 152-155 */
    {2,0xC4,0xBE},{2,0xC4,0xBF},{2,0xC5,0x80},{2,0xC5,0x81},  /* 156-159 */
    {2,0xC5,0x82},                                             /* 160 */
    {2,0xC2,0xA1},{2,0xC2,0xA2},{2,0xC2,0xA3},{2,0xC2,0xA4},  /* 161-164 */
    {2,0xC2,0xA5},{2,0xC2,0xA6},{2,0xC2,0xA7},{2,0xC2,0xA8},  /* 165-168 */
    {2,0xC2,0xA9},{2,0xC2,0xAA},{2,0xC2,0xAB},{2,0xC2,0xAC},  /* 169-172 */
    {2,0xC5,0x83},                                             /* 173 */
    {2,0xC2,0xAE},{2,0xC2,0xAF},{2,0xC2,0xB0},{2,0xC2,0xB1},  /* 174-177 */
    {2,0xC2,0xB2},{2,0xC2,0xB3},{2,0xC2,0xB4},{2,0xC2,0xB5},  /* 178-181 */
    {2,0xC2,0xB6},{2,0xC2,0xB7},{2,0xC2,0xB8},{2,0xC2,0xB9},  /* 182-185 */
    {2,0xC2,0xBA},{2,0xC2,0xBB},{2,0xC2,0xBC},{2,0xC2,0xBD},  /* 186-189 */
    {2,0xC2,0xBE},{2,0xC2,0xBF},{2,0xC3,0x80},{2,0xC3,0x81},  /* 190-193 */
    {2,0xC3,0x82},{2,0xC3,0x83},{2,0xC3,0x84},{2,0xC3,0x85},  /* 194-197 */
    {2,0xC3,0x86},{2,0xC3,0x87},{2,0xC3,0x88},{2,0xC3,0x89},  /* 198-201 */
    {2,0xC3,0x8A},{2,0xC3,0x8B},{2,0xC3,0x8C},{2,0xC3,0x8D},  /* 202-205 */
    {2,0xC3,0x8E},{2,0xC3,0x8F},{2,0xC3,0x90},{2,0xC3,0x91},  /* 206-209 */
    {2,0xC3,0x92},{2,0xC3,0x93},{2,0xC3,0x94},{2,0xC3,0x95},  /* 210-213 */
    {2,0xC3,0x96},{2,0xC3,0x97},{2,0xC3,0x98},{2,0xC3,0x99},  /* 214-217 */
    {2,0xC3,0x9A},{2,0xC3,0x9B},{2,0xC3,0x9C},{2,0xC3,0x9D},  /* 218-221 */
    {2,0xC3,0x9E},{2,0xC3,0x9F},{2,0xC3,0xA0},{2,0xC3,0xA1},  /* 222-225 */
    {2,0xC3,0xA2},{2,0xC3,0xA3},{2,0xC3,0xA4},{2,0xC3,0xA5},  /* 226-229 */
    {2,0xC3,0xA6},{2,0xC3,0xA7},{2,0xC3,0xA8},{2,0xC3,0xA9},  /* 230-233 */
    {2,0xC3,0xAA},{2,0xC3,0xAB},{2,0xC3,0xAC},{2,0xC3,0xAD},  /* 234-237 */
    {2,0xC3,0xAE},{2,0xC3,0xAF},{2,0xC3,0xB0},{2,0xC3,0xB1},  /* 238-241 */
    {2,0xC3,0xB2},{2,0xC3,0xB3},{2,0xC3,0xB4},{2,0xC3,0xB5},  /* 242-245 */
    {2,0xC3,0xB6},{2,0xC3,0xB7},{2,0xC3,0xB8},{2,0xC3,0xB9},  /* 246-249 */
    {2,0xC3,0xBA},{2,0xC3,0xBB},{2,0xC3,0xBC},{2,0xC3,0xBD},  /* 250-253 */
    {2,0xC3,0xBE},{2,0xC3,0xBF}                                /* 254-255 */
};

/*------------------------------------------------------------------------------
    Unicode codepoint -> original byte (reverse mapping)
    Codepoints 0-323 cover all used values.
------------------------------------------------------------------------------*/
static const unsigned char gUnicodeToByte[324] = {
    /* U+0000 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* U+0010 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* U+0020 */ 0,0x21,0x22,0x23,0x24,0x25,0x26,0x27,
               0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,
    /* U+0030 */ 0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,
               0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
    /* U+0040 */ 0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,
               0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,
    /* U+0050 */ 0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,
               0x58,0x59,0x5A,0x5B,0x5C,0x5D,0x5E,0x5F,
    /* U+0060 */ 0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,
               0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,
    /* U+0070 */ 0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,
               0x78,0x79,0x7A,0x7B,0x7C,0x7D,0x7E,0,
    /* U+0080 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* U+0090 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* U+00A0 */ 0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,
               0xA8,0xA9,0xAA,0xAB,0xAC,0,0xAE,0xAF,
    /* U+00B0 */ 0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,
               0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
    /* U+00C0 */ 0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,
               0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,
    /* U+00D0 */ 0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,
               0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,
    /* U+00E0 */ 0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,
               0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,
    /* U+00F0 */ 0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,
               0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF,
    /* U+0100 */ 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
               0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    /* U+0110 */ 0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
               0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,
    /* U+0120 */ 0x20,0x7F,0x80,0x81,0x82,0x83,0x84,0x85,
               0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,
    /* U+0130 */ 0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,
               0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,
    /* U+0140 */ 0x9E,0x9F,0xA0,0xAD
};

/*------------------------------------------------------------------------------
    UTF-8 helpers
------------------------------------------------------------------------------*/
static short UTF8CharLen(unsigned char byte)
{
    if (byte < 0x80) return 1;
    if (byte < 0xC0) return 1;  /* continuation byte, treat as 1 */
    if (byte < 0xE0) return 2;
    if (byte < 0xF0) return 3;
    return 4;
}

static long DecodeUTF8Char(const unsigned char *src, short *bytesRead)
{
    long cp;
    if (src[0] < 0x80)
    {
        *bytesRead = 1;
        return src[0];
    }
    if (src[0] < 0xE0)
    {
        *bytesRead = 2;
        cp = ((long)(src[0] & 0x1F) << 6) | (src[1] & 0x3F);
        return cp;
    }
    if (src[0] < 0xF0)
    {
        *bytesRead = 3;
        cp = ((long)(src[0] & 0x0F) << 12) |
             ((long)(src[1] & 0x3F) << 6) |
             (src[2] & 0x3F);
        return cp;
    }
    *bytesRead = 1;
    return 0;  /* 4-byte chars not used */
}

/*------------------------------------------------------------------------------
    String Pool - Simple bump allocator for vocab/merge strings
------------------------------------------------------------------------------*/
static char *StringPoolAlloc(short len)
{
    char *result;
    if (gStringPoolUsed + len + 1 > gStringPoolSize)
        return nil;
    result = gStringPool + gStringPoolUsed;
    gStringPoolUsed += len + 1;  /* +1 for null terminator */
    return result;
}

/*------------------------------------------------------------------------------
    Hash Table - Open addressing with linear probing
    Maps vocab string -> token ID for BPE encode
------------------------------------------------------------------------------*/
static unsigned long HashString(const char *str, short len)
{
    unsigned long hash;
    short i;
    hash = 5381;
    for (i = 0; i < len; i++)
        hash = ((hash << 5) + hash) + (unsigned char)str[i];
    return hash;
}

static void HashTableInsert(const char *str, short len, long tokenID)
{
    unsigned long slot;
    slot = HashString(str, len) & gHashMask;
    while (gHashIDs[slot] != -1)
    {
        slot = (slot + 1) & gHashMask;
    }
    gHashIDs[slot] = tokenID;
}

static long HashTableLookup(const char *str, short len)
{
    unsigned long slot;
    long id;
    unsigned long startSlot;

    slot = HashString(str, len) & gHashMask;
    startSlot = slot;

    do {
        id = gHashIDs[slot];
        if (id == -1)
            return -1;  /* Not found */

        /* Compare with actual vocab string */
        if (gVocabLens[id] == len &&
            memcmp(gVocabStrings[id], str, len) == 0)
        {
            return id;
        }

        slot = (slot + 1) & gHashMask;
    } while (slot != startSlot);

    return -1;  /* Table full (shouldn't happen) */
}

/*------------------------------------------------------------------------------
    Tokenizer_Initialize - Load vocab + merges from .bin model file

    Opens the .bin file, reads the header to find vocabOffset,
    then reads all token strings and BPE merge rules.
    Builds hash table for string->ID lookup.
------------------------------------------------------------------------------*/
OSErr Tokenizer_Initialize(const FSSpec *modelFile)
{
    OSErr err;
    short refNum;
    long headerBytes;
    char headerBuf[128];
    long *headerLongs;
    long vocabOffset;
    long vocabSize;
    long numMerges;
    long readBytes;
    long i;
    unsigned char strLen;
    char *strPtr;
    long allocSize;

    refNum = 0;

    /* Open model file */
    DebugLog_Write("Tokenizer_Initialize: opening model file");
    err = FSpOpenDF(modelFile, fsRdPerm, &refNum);
    if (err != noErr)
    {
        DebugLog_WriteNum("Tokenizer_Initialize: FSpOpenDF failed", (long)err);
        return err;
    }

    /* Read header to get vocab section info */
    headerBytes = 128;
    err = FSRead(refNum, &headerBytes, headerBuf);
    if (err != noErr)
    {
        FSClose(refNum);
        return err;
    }

    headerLongs = (long *)headerBuf;

    /* Validate magic */
    if (headerLongs[0] != 0x4D434149L)  /* 'MCAI' */
    {
        FSClose(refNum);
        return -3;
    }

    vocabSize = headerLongs[8];     /* vocabSize field */
    vocabOffset = headerLongs[14];  /* vocabOffset field */
    numMerges = headerLongs[16];    /* numMerges field */

    DebugLog_WriteNum("Tokenizer: vocabSize =", vocabSize);
    DebugLog_WriteNum("Tokenizer: vocabOffset =", vocabOffset);
    DebugLog_WriteNum("Tokenizer: numMerges =", numMerges);

    gVocabSize = vocabSize;
    gNumMerges = numMerges;

    /* Read chatTemplate and special token IDs from header */
    {
        long chatTemplate;
        chatTemplate = headerLongs[19];
        sChatTemplate = chatTemplate;
        sIsExternalModel = (chatTemplate != 0);
        sImStartToken = headerLongs[20];
        sImEndToken = headerLongs[21];
        /* Default to 1/2 if not set (SmolLM compatibility) */
        if (sImStartToken == 0 && sIsExternalModel)
            sImStartToken = 1;
        if (sImEndToken == 0 && sIsExternalModel)
            sImEndToken = 2;
        DebugLog_WriteNum("Tokenizer: chatTemplate =", chatTemplate);
        sPreTokenizerType = (short)headerLongs[22];
        DebugLog_WriteNum("Tokenizer: isExternalModel =", (long)sIsExternalModel);
        DebugLog_WriteNum("Tokenizer: imStartToken =", sImStartToken);
        DebugLog_WriteNum("Tokenizer: imEndToken =", sImEndToken);
        DebugLog_WriteNum("Tokenizer: preTokenizerType =", (long)sPreTokenizerType);
    }

    /* Scale hash table for vocab size (next power of 2 above vocabSize * 2) */
    gHashSize = kDefaultHashSize;
    while (gHashSize < vocabSize * 2)
        gHashSize *= 2;
    gHashMask = gHashSize - 1;
    DebugLog_WriteNum("Tokenizer: hashSize =", gHashSize);

    /* Scale string pool: vocab section size + null terminators + padding.
       StringPoolAlloc adds +1 byte per string for null terminator.
       Vocab has vocabSize strings, merges have numMerges*2 strings. */
    {
        long weightsOffset;
        long nullTermBytes;
        weightsOffset = headerLongs[15];
        nullTermBytes = vocabSize + numMerges * 2L;
        gStringPoolSize = (weightsOffset - vocabOffset) + nullTermBytes + 4096L;
        if (gStringPoolSize < kDefaultPoolSize)
            gStringPoolSize = kDefaultPoolSize;
        DebugLog_WriteNum("Tokenizer: stringPoolSize =", gStringPoolSize);
    }

    /* Allocate arrays */
    allocSize = vocabSize * sizeof(char *);
    gVocabStrings = (char **)AllocClear(allocSize);
    if (gVocabStrings == nil) { FSClose(refNum); return memFullErr; }

    allocSize = vocabSize * sizeof(short);
    gVocabLens = (short *)AllocClear(allocSize);
    if (gVocabLens == nil) { FSClose(refNum); return memFullErr; }

    allocSize = numMerges * sizeof(char *);
    gMergeFirst = (char **)AllocClear(allocSize);
    gMergeSecond = (char **)AllocClear(allocSize);
    if (gMergeFirst == nil || gMergeSecond == nil)
        { FSClose(refNum); return memFullErr; }

    allocSize = numMerges * sizeof(short);
    gMergeFirstLen = (short *)AllocClear(allocSize);
    gMergeSecondLen = (short *)AllocClear(allocSize);
    if (gMergeFirstLen == nil || gMergeSecondLen == nil)
        { FSClose(refNum); return memFullErr; }

    allocSize = gHashSize * sizeof(long);
    gHashIDs = (long *)AllocClear(allocSize);
    if (gHashIDs == nil) { FSClose(refNum); return memFullErr; }
    for (i = 0; i < gHashSize; i++)
        gHashIDs[i] = -1;

    /* String pool */
    gStringPool = AllocClear(gStringPoolSize);
    if (gStringPool == nil) { FSClose(refNum); return memFullErr; }
    gStringPoolUsed = 0;

    /* Seek to vocab section */
    err = SetFPos(refNum, fsFromStart, vocabOffset);
    if (err != noErr) { FSClose(refNum); return err; }

    /* Read vocab strings */
    for (i = 0; i < vocabSize; i++)
    {
        /* Read 1-byte length */
        readBytes = 1;
        err = FSRead(refNum, &readBytes, &strLen);
        if (err != noErr) { FSClose(refNum); return err; }

        /* Allocate and read string */
        strPtr = StringPoolAlloc((short)strLen);
        if (strPtr == nil) { FSClose(refNum); return memFullErr; }

        if (strLen > 0)
        {
            readBytes = strLen;
            err = FSRead(refNum, &readBytes, strPtr);
            if (err != noErr) { FSClose(refNum); return err; }
        }
        strPtr[strLen] = '\0';

        gVocabStrings[i] = strPtr;
        gVocabLens[i] = (short)strLen;

        /* Add to hash table */
        HashTableInsert(strPtr, (short)strLen, i);
    }

    /* Read merge rules */
    for (i = 0; i < numMerges; i++)
    {
        /* First token of merge pair */
        readBytes = 1;
        err = FSRead(refNum, &readBytes, &strLen);
        if (err != noErr) { FSClose(refNum); return err; }

        strPtr = StringPoolAlloc((short)strLen);
        if (strPtr == nil) { FSClose(refNum); return memFullErr; }

        if (strLen > 0)
        {
            readBytes = strLen;
            err = FSRead(refNum, &readBytes, strPtr);
            if (err != noErr) { FSClose(refNum); return err; }
        }
        strPtr[strLen] = '\0';

        gMergeFirst[i] = strPtr;
        gMergeFirstLen[i] = (short)strLen;

        /* Second token of merge pair */
        readBytes = 1;
        err = FSRead(refNum, &readBytes, &strLen);
        if (err != noErr) { FSClose(refNum); return err; }

        strPtr = StringPoolAlloc((short)strLen);
        if (strPtr == nil) { FSClose(refNum); return memFullErr; }

        if (strLen > 0)
        {
            readBytes = strLen;
            err = FSRead(refNum, &readBytes, strPtr);
            if (err != noErr) { FSClose(refNum); return err; }
        }
        strPtr[strLen] = '\0';

        gMergeSecond[i] = strPtr;
        gMergeSecondLen[i] = (short)strLen;
    }

    FSClose(refNum);
    gTokenizerReady = true;

    DebugLog_WriteNum("Tokenizer: loaded OK, vocabSize =", gVocabSize);
    DebugLog_WriteNum("Tokenizer: merges loaded =", gNumMerges);
    return noErr;
}

/*------------------------------------------------------------------------------
    EncodeChunk - BPE-encode a single pre-tokenized chunk

    1. Convert each input byte to its UTF-8 BPE representation
    2. Initialize piece array (one piece per input byte)
    3. Apply BPE merges in priority order
    4. Look up each resulting piece in vocab hash table
------------------------------------------------------------------------------*/
static long EncodeChunk(const char *text, long chunkLen,
                        long *tokens, long maxTokens)
{
    long numPieces;
    long numTokens;
    long i;
    long m;
    short workLen;
    const unsigned char *src;

    if (chunkLen == 0)
        return 0;

    /* Step 1: Convert input bytes to BPE unicode representation */
    workLen = 0;
    src = (const unsigned char *)text;
    numPieces = 0;

    for (i = 0; i < chunkLen && numPieces < kMaxPieces; i++)
    {
        short charLen;
        charLen = gByteToUTF8[src[i]][0];

        if (workLen + charLen >= kMaxWorkBuf)
            break;

        /* Record piece start and length */
        gPieceStart[numPieces] = workLen;
        gPieceLen[numPieces] = charLen;

        /* Copy UTF-8 bytes to work buffer */
        gWorkBuf[workLen] = gByteToUTF8[src[i]][1];
        if (charLen > 1)
            gWorkBuf[workLen + 1] = gByteToUTF8[src[i]][2];
        workLen += charLen;
        numPieces++;
    }

    /* Step 2: Apply BPE merges in priority order */
    for (m = 0; m < gNumMerges; m++)
    {
        short firstLen;
        short secondLen;
        char *first;
        char *second;

        first = gMergeFirst[m];
        firstLen = gMergeFirstLen[m];
        second = gMergeSecond[m];
        secondLen = gMergeSecondLen[m];

        /* Scan for this merge pair in the piece list */
        for (i = 0; i < numPieces - 1; i++)
        {
            /* Skip deleted pieces */
            if (gPieceLen[i] == 0)
                continue;

            /* Find next active piece */
            {
                long j;
                j = i + 1;
                while (j < numPieces && gPieceLen[j] == 0)
                    j++;
                if (j >= numPieces)
                    break;

                /* Check if this pair matches the merge rule */
                if (gPieceLen[i] == firstLen &&
                    gPieceLen[j] == secondLen &&
                    memcmp(&gWorkBuf[gPieceStart[i]], first, firstLen) == 0 &&
                    memcmp(&gWorkBuf[gPieceStart[j]], second, secondLen) == 0)
                {
                    /* Merge: extend first piece to cover both */
                    memcpy(&gWorkBuf[gPieceStart[i] + gPieceLen[i]],
                           &gWorkBuf[gPieceStart[j]], secondLen);
                    gPieceLen[i] = firstLen + secondLen;
                    gPieceLen[j] = 0;  /* Mark second as deleted */

                    /* Don't advance i - check for more merges at same position */
                    i--;  /* Will be incremented by for loop */
                }
            }
        }
    }

    /* Step 3: Look up each remaining piece in vocab */
    numTokens = 0;
    for (i = 0; i < numPieces && numTokens < maxTokens; i++)
    {
        long tokenID;
        if (gPieceLen[i] == 0)
            continue;  /* Deleted by merge */

        tokenID = HashTableLookup(&gWorkBuf[gPieceStart[i]], gPieceLen[i]);
        if (tokenID == -1)
            tokenID = kTokenUNK;  /* Unknown token */

        tokens[numTokens] = tokenID;
        numTokens++;
    }

    return numTokens;
}

/*------------------------------------------------------------------------------
    EncodeChunkSentencePiece - BPE-encode a single chunk for SentencePiece

    Unlike EncodeChunk (ByteLevel), this works directly on UTF-8 text.
    Input already has spaces replaced with the metaspace character.
    1. Split input into individual UTF-8 characters (1-4 bytes each)
    2. Apply BPE merges in priority order (SAME algorithm)
    3. Look up each resulting piece in vocab hash table
------------------------------------------------------------------------------*/
static long EncodeChunkSentencePiece(const char *text, long chunkLen,
                                     long *tokens, long maxTokens)
{
    long numPieces;
    long numTokens;
    long i;
    long m;
    short workLen;
    const unsigned char *src;

    if (chunkLen == 0)
        return 0;

    /* Step 1: Split input into UTF-8 characters as initial pieces.
       Copy text into gWorkBuf and record piece boundaries. */
    workLen = 0;
    src = (const unsigned char *)text;
    numPieces = 0;
    i = 0;

    while (i < chunkLen && numPieces < kMaxPieces)
    {
        short charLen;
        charLen = UTF8CharLen(src[i]);

        /* Clamp to remaining input */
        if (i + charLen > chunkLen)
            charLen = (short)(chunkLen - i);

        if (workLen + charLen >= kMaxWorkBuf)
            break;

        /* Record piece start and length */
        gPieceStart[numPieces] = workLen;
        gPieceLen[numPieces] = charLen;

        /* Copy UTF-8 bytes to work buffer */
        memcpy(&gWorkBuf[workLen], &src[i], charLen);
        workLen += charLen;
        numPieces++;
        i += charLen;
    }

    /* Step 2: Apply BPE merges in priority order (same algorithm as ByteLevel) */
    for (m = 0; m < gNumMerges; m++)
    {
        short firstLen;
        short secondLen;
        char *first;
        char *second;

        first = gMergeFirst[m];
        firstLen = gMergeFirstLen[m];
        second = gMergeSecond[m];
        secondLen = gMergeSecondLen[m];

        /* Scan for this merge pair in the piece list */
        for (i = 0; i < numPieces - 1; i++)
        {
            /* Skip deleted pieces */
            if (gPieceLen[i] == 0)
                continue;

            /* Find next active piece */
            {
                long j;
                j = i + 1;
                while (j < numPieces && gPieceLen[j] == 0)
                    j++;
                if (j >= numPieces)
                    break;

                /* Check if this pair matches the merge rule */
                if (gPieceLen[i] == firstLen &&
                    gPieceLen[j] == secondLen &&
                    memcmp(&gWorkBuf[gPieceStart[i]], first, firstLen) == 0 &&
                    memcmp(&gWorkBuf[gPieceStart[j]], second, secondLen) == 0)
                {
                    /* Merge: extend first piece to cover both */
                    memcpy(&gWorkBuf[gPieceStart[i] + gPieceLen[i]],
                           &gWorkBuf[gPieceStart[j]], secondLen);
                    gPieceLen[i] = firstLen + secondLen;
                    gPieceLen[j] = 0;  /* Mark second as deleted */

                    /* Don't advance i - check for more merges at same position */
                    i--;  /* Will be incremented by for loop */
                }
            }
        }
    }

    /* Step 3: Look up each remaining piece in vocab.
       If not found, try SentencePiece byte fallback: each byte becomes <0xNN> */
    numTokens = 0;
    for (i = 0; i < numPieces && numTokens < maxTokens; i++)
    {
        long tokenID;
        if (gPieceLen[i] == 0)
            continue;  /* Deleted by merge */

        tokenID = HashTableLookup(&gWorkBuf[gPieceStart[i]], gPieceLen[i]);
        if (tokenID >= 0)
        {
            tokens[numTokens] = tokenID;
            numTokens++;
        }
        else
        {
            /* Byte fallback: encode each byte as <0xNN> token */
            short bi;
            for (bi = 0; bi < gPieceLen[i] && numTokens < maxTokens; bi++)
            {
                char fallback[7];
                unsigned char byte;
                long fbID;

                byte = (unsigned char)gWorkBuf[gPieceStart[i] + bi];
                fallback[0] = '<';
                fallback[1] = '0';
                fallback[2] = 'x';
                fallback[3] = "0123456789ABCDEF"[(byte >> 4) & 0x0F];
                fallback[4] = "0123456789ABCDEF"[byte & 0x0F];
                fallback[5] = '>';
                fbID = HashTableLookup(fallback, 6);
                if (fbID >= 0)
                    tokens[numTokens] = fbID;
                else
                    tokens[numTokens] = kTokenUNK;
                numTokens++;
            }
        }
    }

    return numTokens;
}

/*------------------------------------------------------------------------------
    Tokenizer_Encode - Convert text to BPE token IDs with pre-tokenization

    Supports three pre-tokenizer modes:
      0 = GPT-2 ByteLevel: byte-to-unicode mapping, split into word chunks
      1 = Qwen: single-digit split + ByteLevel
      2 = SentencePiece Metaspace: replace spaces with metaspace char,
          split at metaspace boundaries, BPE on raw UTF-8

    GPT-2/Qwen pre-tokenization rules:
      - Optional space + letters  -> one chunk (e.g., " System")
      - Optional space + digits   -> one chunk (e.g., " 1024")
      - Optional space + punct    -> one chunk (e.g., ":", ",")
      - Trailing whitespace       -> one chunk (e.g., " ")
------------------------------------------------------------------------------*/
long Tokenizer_Encode(const char *text, long *tokens, long maxTokens)
{
    long textLen;
    long totalTokens;
    long pos;
    const unsigned char *src;

    if (!gTokenizerReady)
        return 0;

    textLen = strlen(text);
    if (textLen == 0)
        return 0;

    src = (const unsigned char *)text;
    totalTokens = 0;
    pos = 0;

    /*------------------------------------------------------------------
        SentencePiece Metaspace path (type 2):
        1. Build a temp buffer with spaces replaced by metaspace (E2 96 81)
        2. Add leading metaspace (add_prefix_space behavior)
        3. Split at metaspace boundaries, BPE each chunk
    ------------------------------------------------------------------*/
    if (sPreTokenizerType == 2)
    {
        /* Build metaspace-substituted text in a local buffer.
           Each space (1 byte) becomes metaspace (3 bytes), so worst case
           is textLen * 3 + 3 (leading metaspace) + 1 (null). */
        char spBuf[kMaxWorkBuf];
        long spLen;
        long i;
        long chunkTokens;

        spLen = 0;

        /* Add leading metaspace only for first chunk (prepend_scheme="first").
           Continuation chunks (via Tokenizer_EncodeNoPrefix) skip this. */
        if (!sSuppressPrefix)
        {
            spBuf[spLen++] = (char)0xE2;
            spBuf[spLen++] = (char)0x96;
            spBuf[spLen++] = (char)0x81;
        }

        /* Copy text, replacing 0x20 with metaspace */
        for (i = 0; i < textLen; i++)
        {
            if (src[i] == 0x20)
            {
                if (spLen + 3 >= kMaxWorkBuf)
                    break;
                spBuf[spLen++] = (char)0xE2;
                spBuf[spLen++] = (char)0x96;
                spBuf[spLen++] = (char)0x81;
            }
            else
            {
                if (spLen + 1 >= kMaxWorkBuf)
                    break;
                spBuf[spLen++] = (char)src[i];
            }
        }

        /* BPE-encode the entire processed buffer.
           Metaspace characters in the vocab handle word boundaries
           naturally, no need to split at metaspace positions. */
        {
            chunkTokens = EncodeChunkSentencePiece(
                spBuf, spLen,
                &tokens[totalTokens], maxTokens - totalTokens);
            if (chunkTokens > 0)
                totalTokens += chunkTokens;
        }

        return totalTokens;
    }

    /*------------------------------------------------------------------
        GPT-2 / Qwen ByteLevel path (type 0 and 1)
    ------------------------------------------------------------------*/
    while (pos < textLen && totalTokens < maxTokens)
    {
        long chunkStart;
        long chunkLen;
        long chunkTokens;

        chunkStart = pos;

        if (sPreTokenizerType == 1 && src[pos] == 0x20 &&
            pos + 1 < textLen && src[pos + 1] >= '0' && src[pos + 1] <= '9')
        {
            /* Qwen: space before digit is its own chunk */
            pos++;
        }
        else if (sPreTokenizerType == 1 && src[pos] >= '0' && src[pos] <= '9')
        {
            /* Qwen: single digit per chunk */
            pos++;
        }
        else
        {
            /* GPT-2 style: leading space joins the following chunk */
            if (src[pos] == 0x20)
                pos++;

            if (pos < textLen && ((src[pos] >= 'A' && src[pos] <= 'Z') ||
                                  (src[pos] >= 'a' && src[pos] <= 'z')))
            {
                /* Space + letters */
                while (pos < textLen && ((src[pos] >= 'A' && src[pos] <= 'Z') ||
                                         (src[pos] >= 'a' && src[pos] <= 'z')))
                    pos++;
            }
            else if (pos < textLen && src[pos] >= '0' && src[pos] <= '9')
            {
                /* Space + digits (GPT-2: grouped) */
                while (pos < textLen && src[pos] >= '0' && src[pos] <= '9')
                    pos++;
            }
            else if (pos < textLen && src[pos] != 0x20)
            {
                /* Space + punctuation/symbol */
                while (pos < textLen && src[pos] != 0x20 &&
                       !((src[pos] >= 'A' && src[pos] <= 'Z') ||
                         (src[pos] >= 'a' && src[pos] <= 'z')) &&
                       !(src[pos] >= '0' && src[pos] <= '9'))
                    pos++;
            }
            /* else: just a trailing space */
        }

        chunkLen = pos - chunkStart;
        if (chunkLen == 0)
            break;  /* Safety: prevent infinite loop */

        /* BPE-encode this chunk independently */
        chunkTokens = EncodeChunk(&text[chunkStart], chunkLen,
                                  &tokens[totalTokens],
                                  maxTokens - totalTokens);
        if (chunkTokens > 0)
            totalTokens += chunkTokens;
    }

    return totalTokens;
}

/*------------------------------------------------------------------------------
    Tokenizer_Decode - Convert token IDs to text

    Two modes:
      ByteLevel (type 0/1): decode each UTF-8 char back to original byte
        using the unicode-to-byte reverse mapping table.
      SentencePiece (type 2): output vocab strings directly as UTF-8,
        converting metaspace (E2 96 81) back to space (0x20).
------------------------------------------------------------------------------*/
long Tokenizer_Decode(const long *tokens, long numTokens, char *text, long maxLen)
{
    long i;
    long outPos;
    const char *tokStr;
    short tokLen;
    const unsigned char *src;
    short pos;

    if (!gTokenizerReady)
        return 0;

    outPos = 0;

    for (i = 0; i < numTokens; i++)
    {
        long tokenID;
        tokenID = tokens[i];

        /* Skip special control tokens in output */
        if (tokenID == kTokenPAD || tokenID == kTokenBOS ||
            tokenID == kTokenEOS)
            continue;

        /* Skip command tokens (including AppleScript at end of vocab) */
        if (Tokenizer_IsCommandToken(tokenID))
            continue;

        /* Get token string */
        if (tokenID < 0 || tokenID >= gVocabSize)
            continue;

        tokStr = gVocabStrings[tokenID];
        tokLen = gVocabLens[tokenID];

        if (sPreTokenizerType == 2)
        {
            /* SentencePiece decode: handle metaspace, byte fallback, and raw UTF-8 */

            /* Check for byte fallback token: <0xNN> (6 chars) */
            if (tokLen == 6 && tokStr[0] == '<' && tokStr[1] == '0' &&
                tokStr[2] == 'x' && tokStr[5] == '>')
            {
                unsigned char hi;
                unsigned char lo;
                unsigned char byte;
                hi = (unsigned char)tokStr[3];
                lo = (unsigned char)tokStr[4];
                /* Parse hex digits */
                if (hi >= '0' && hi <= '9') hi = hi - '0';
                else if (hi >= 'A' && hi <= 'F') hi = hi - 'A' + 10;
                else hi = 0;
                if (lo >= '0' && lo <= '9') lo = lo - '0';
                else if (lo >= 'A' && lo <= 'F') lo = lo - 'A' + 10;
                else lo = 0;
                byte = (hi << 4) | lo;
                if (outPos < maxLen - 1)
                {
                    text[outPos] = (char)byte;
                    outPos++;
                }
            }
            else
            {
                /* Normal SentencePiece: output UTF-8, metaspace -> space */
                src = (const unsigned char *)tokStr;
                pos = 0;
                while (pos < tokLen && outPos < maxLen - 1)
                {
                    if (src[pos] == 0xE2 &&
                        pos + 2 < tokLen &&
                        src[pos + 1] == 0x96 &&
                        src[pos + 2] == 0x81)
                    {
                        text[outPos] = ' ';
                        outPos++;
                        pos += 3;
                    }
                    else
                    {
                        text[outPos] = (char)src[pos];
                        outPos++;
                        pos++;
                    }
                }
            }
        }
        else
        {
            /* ByteLevel decode: UTF-8 codepoint -> original byte via table */
            src = (const unsigned char *)tokStr;
            pos = 0;
            while (pos < tokLen && outPos < maxLen - 1)
            {
                short bytesRead;
                long codepoint;

                bytesRead = 0;
                codepoint = DecodeUTF8Char(&src[pos], &bytesRead);

                /* Map codepoint back to original byte */
                if (codepoint >= 0 && codepoint < 324)
                {
                    text[outPos] = (char)gUnicodeToByte[codepoint];
                }
                else
                {
                    text[outPos] = '?';  /* Unmapped codepoint */
                }
                outPos++;
                pos += bytesRead;
            }
        }
    }

    text[outPos] = '\0';
    return outPos;
}

/*------------------------------------------------------------------------------
    Tokenizer_GetTokenString
------------------------------------------------------------------------------*/
const char *Tokenizer_GetTokenString(long tokenID)
{
    if (tokenID >= 0 && tokenID < kNumSpecialTokens)
        return kSpecialTokenStrings[tokenID];

    if (gTokenizerReady && tokenID >= 0 && tokenID < gVocabSize)
        return gVocabStrings[tokenID];

    return "[?]";
}

/*------------------------------------------------------------------------------
    Tokenizer_IsCommandToken - Check if token is a command token.
    Always returns false for external models (their tokens 5-12 are regular BPE).
------------------------------------------------------------------------------*/
Boolean Tokenizer_IsCommandToken(long tokenID)
{
    if (sIsExternalModel)
        return false;
    if (tokenID >= kTokenCmdNone && tokenID <= kTokenCmdRestart)
        return true;
    if (tokenID == kTokenCmdAppleScript)
        return true;
    return false;
}

/*------------------------------------------------------------------------------
    Tokenizer_SetExternalModel / Tokenizer_IsExternalModel
------------------------------------------------------------------------------*/
void Tokenizer_SetExternalModel(Boolean isExternal)
{
    sIsExternalModel = isExternal;
}

Boolean Tokenizer_IsExternalModel(void)
{
    return sIsExternalModel;
}

long Tokenizer_GetImStartToken(void)
{
    return sImStartToken;
}

long Tokenizer_GetImEndToken(void)
{
    return sImEndToken;
}

long Tokenizer_GetChatTemplate(void)
{
    return sChatTemplate;
}

long Tokenizer_EncodeNoPrefix(const char *text, long *tokens, long maxTokens)
{
    long result;
    sSuppressPrefix = true;
    result = Tokenizer_Encode(text, tokens, maxTokens);
    sSuppressPrefix = false;
    return result;
}

/*------------------------------------------------------------------------------
    Tokenizer_IsReady
------------------------------------------------------------------------------*/
Boolean Tokenizer_IsReady(void)
{
    return gTokenizerReady;
}

/*------------------------------------------------------------------------------
    Tokenizer_Cleanup
------------------------------------------------------------------------------*/
void Tokenizer_Cleanup(void)
{
    /* If arena was used (large vocab), DON'T DisposePtr, arena owns the memory.
       Arena is freed by Engine_Cleanup which runs after Tokenizer_Cleanup. */
    if (!sUsingArenaAlloc)
    {
        /* Small vocab, all allocated from app/system heap */
        if (gVocabStrings != nil) DisposePtr((Ptr)gVocabStrings);
        if (gVocabLens != nil) DisposePtr((Ptr)gVocabLens);
        if (gMergeFirst != nil) DisposePtr((Ptr)gMergeFirst);
        if (gMergeSecond != nil) DisposePtr((Ptr)gMergeSecond);
        if (gMergeFirstLen != nil) DisposePtr((Ptr)gMergeFirstLen);
        if (gMergeSecondLen != nil) DisposePtr((Ptr)gMergeSecondLen);
        if (gHashIDs != nil) DisposePtr((Ptr)gHashIDs);
        if (gStringPool != nil) DisposePtr(gStringPool);
    }
    /* else: arena-allocated memory freed when Engine_Cleanup disposes arena */

    gVocabStrings = nil;
    gVocabLens = nil;
    gMergeFirst = nil;
    gMergeSecond = nil;
    gMergeFirstLen = nil;
    gMergeSecondLen = nil;
    gHashIDs = nil;
    gStringPool = nil;
    gStringPoolUsed = 0;
    gVocabSize = 0;
    gNumMerges = 0;
    gTokenizerReady = false;
    sIsExternalModel = false;
    sUsingArenaAlloc = false;
    sPreTokenizerType = 0;
}
