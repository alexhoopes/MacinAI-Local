/*----------------------------------------------------------------------
    InferenceGuard.c - Pre/Post Processing for Model Inference

    Routes queries optimally and cleans up model output.

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

#pragma segment InfGuard

#include "InferenceGuard.h"
#include <string.h>
#include <ctype.h>

/* Forward declarations */
static void StrToLower(const char *src, char *dst, short maxLen);
static Boolean ContainsWord(const char *text, const char *word);
static Boolean IsHardwareQuestion(const char *lowerQuery);
static Boolean IsRefusalPattern(const char *lowerQuery,
                                char *response, short maxResp);
static short DetectRepetition(const char *text, short len);

/*----------------------------------------------------------------------
    Keyword lists for classification
----------------------------------------------------------------------*/

/* Words that suggest a hardware spec question */
static const char *sHardwareKeywords[] = {
    "processor", "cpu", "ram", "memory", "simm", "dimm",
    "nubus", "pds", "pci", "slot", "expansion",
    "floppy", "drive", "scsi", "display", "monitor",
    "release", "released", "came out", "introduced",
    "clock speed", "mhz", "how fast", "how much ram",
    "specs", "specifications",
    nil
};

/* Refusal patterns and their responses */
typedef struct {
    const char *pattern;
    const char *response;
} RefusalEntry;

static const RefusalEntry sRefusals[] = {
    {
        "hack",
        "I specialize in classic Macintosh computers from 1984 "
        "through Mac OS 9. I can help with networking setup, "
        "troubleshooting, and hardware questions."
    },
    {
        "python script",
        "I specialize in classic Macintosh programming using the "
        "Toolbox API, Pascal, and C. I can help with Mac-specific "
        "development questions."
    },
    {
        "write me a script",
        "I specialize in classic Macintosh programming. I can help "
        "with Toolbox API, ResEdit, and Mac development questions."
    },
    {
        "windows 95",
        "I specialize in classic Macintosh computers. I can tell "
        "you about Mac OS versions from System 1 through Mac OS 9, "
        "or help compare Mac hardware."
    },
    {
        "windows 98",
        "I specialize in classic Macintosh computers from 1984 "
        "through 2001. How can I help with your Mac?"
    },
    {
        "linux",
        "I specialize in classic Mac OS, not Linux. However, some "
        "vintage Macs can run A/UX (Apple's Unix) or MkLinux. "
        "Would you like to know more about those?"
    },
    {
        "chatgpt",
        "No, I am MacinAI, an AI assistant created by Alex Hoopes "
        "for classic Macintosh computers running System 7 through "
        "Mac OS 9."
    },
    {
        "are you gpt",
        "No, I am MacinAI, created by Alex Hoopes. I specialize "
        "in vintage Macintosh hardware, software, and programming."
    },
    {
        "internet explorer",
        "Internet Explorer is not a Macintosh application. For web "
        "browsing on classic Mac OS, try Netscape Navigator, "
        "Cyberdog, or iCab."
    },
    {
        "open chrome",
        "Google Chrome is not available for classic Mac OS. For "
        "web browsing, try Netscape Navigator, Cyberdog, or iCab."
    },
    {nil, nil}
};

/*----------------------------------------------------------------------
    StrToLower - Convert string to lowercase (C89 compatible)
----------------------------------------------------------------------*/
static void StrToLower(const char *src, char *dst, short maxLen)
{
    short i;

    for (i = 0; i < maxLen - 1 && src[i] != '\0'; i++) {
        if (src[i] >= 'A' && src[i] <= 'Z') {
            dst[i] = src[i] + 32;
        } else {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

/*----------------------------------------------------------------------
    ContainsWord - Check if text contains a word/phrase
----------------------------------------------------------------------*/
static Boolean ContainsWord(const char *text, const char *word)
{
    if (text == nil || word == nil) {
        return false;
    }
    return (strstr(text, word) != nil);
}

/*----------------------------------------------------------------------
    IsHardwareQuestion - Does the query ask about hardware specs?
----------------------------------------------------------------------*/
static Boolean IsHardwareQuestion(const char *lowerQuery)
{
    short i;

    for (i = 0; sHardwareKeywords[i] != nil; i++) {
        if (ContainsWord(lowerQuery, sHardwareKeywords[i])) {
            return true;
        }
    }

    return false;
}

/*----------------------------------------------------------------------
    IsRefusalPattern - Does the query match a refusal pattern?
----------------------------------------------------------------------*/
static Boolean IsRefusalPattern(const char *lowerQuery,
                                char *response, short maxResp)
{
    short i;

    for (i = 0; sRefusals[i].pattern != nil; i++) {
        if (ContainsWord(lowerQuery, sRefusals[i].pattern)) {
            strncpy(response, sRefusals[i].response, maxResp - 1);
            response[maxResp - 1] = '\0';
            return true;
        }
    }

    return false;
}

/*----------------------------------------------------------------------
    InferenceGuard_PreProcess - Analyze query before inference
----------------------------------------------------------------------*/
PreProcessResult InferenceGuard_PreProcess(const char *query)
{
    PreProcessResult result;
    char lowerQuery[256];
    const MacSpec *spec;

    /* Initialize result */
    result.route = kRouteToModel;
    result.spec = nil;
    result.cannedResponse[0] = '\0';

    if (query == nil || query[0] == '\0') {
        return result;
    }

    /* Convert query to lowercase for matching */
    StrToLower(query, lowerQuery, 256);

    /* 1. Check refusal patterns first (highest priority) */
    if (IsRefusalPattern(lowerQuery, result.cannedResponse, 256)) {
        result.route = kRouteToCanned;
        return result;
    }

    /* 2. Check if a Mac model is mentioned AND it's a hardware question */
    spec = MacSpecs_Lookup(lowerQuery);
    if (spec != nil && IsHardwareQuestion(lowerQuery)) {
        result.route = kRouteToLookup;
        result.spec = spec;
        return result;
    }

    /* 3. If a Mac model is mentioned but it's not clearly a
       hardware question, still provide specs as context.
       The model handles "tell me about the Macintosh Plus" better
       when it has accurate specs to reference. */
    if (spec != nil) {
        /* Check for general "tell me about" / "what is the" patterns */
        if (ContainsWord(lowerQuery, "tell me about") ||
            ContainsWord(lowerQuery, "what is the") ||
            ContainsWord(lowerQuery, "what was the") ||
            ContainsWord(lowerQuery, "describe the")) {
            result.route = kRouteToLookup;
            result.spec = spec;
            return result;
        }
    }

    /* 4. Default: pass to model */
    return result;
}

/*----------------------------------------------------------------------
    DetectRepetition - Find where text starts repeating

    Looks for a phrase of 8+ chars that repeats within the text.
    Returns the position where repetition starts, or -1 if none.
----------------------------------------------------------------------*/
static short DetectRepetition(const char *text, short len)
{
    short windowSize;
    short i;
    short j;
    Boolean match;

    /* Try different window sizes from 40 down to 8 chars */
    for (windowSize = 40; windowSize >= 8; windowSize -= 4) {
        if (windowSize > len / 2) {
            continue;
        }

        /* Slide through the text looking for repeated windows */
        for (i = 0; i <= len - windowSize * 2; i++) {
            /* Check if text[i..i+window] repeats at text[i+window..] */
            match = true;
            for (j = 0; j < windowSize; j++) {
                if (text[i + j] != text[i + windowSize + j]) {
                    match = false;
                    break;
                }
            }

            if (match) {
                /* Found repetition starting at i + windowSize */
                return (short)(i + windowSize);
            }
        }
    }

    return -1;
}

/*----------------------------------------------------------------------
    InferenceGuard_PostProcess - Clean model output
----------------------------------------------------------------------*/
short InferenceGuard_PostProcess(char *output, short len)
{
    short repStart;
    short lastPeriod;
    short lastSpace;
    short i;

    if (output == nil || len <= 0) {
        return 0;
    }

    /* 1. Detect and truncate repetitive text */
    repStart = DetectRepetition(output, len);
    if (repStart > 0 && repStart < len) {
        len = repStart;
        output[len] = '\0';
    }

    /* 2. Find a clean sentence ending */
    lastPeriod = -1;
    lastSpace = -1;
    for (i = len - 1; i >= 0; i--) {
        if (output[i] == '.' && lastPeriod < 0) {
            lastPeriod = i;
        }
        if (output[i] == ' ' && lastSpace < 0) {
            lastSpace = i;
        }
        if (lastPeriod >= 0) {
            break;
        }
    }

    /* Truncate to last complete sentence if possible */
    if (lastPeriod > len / 3) {
        /* Keep through the period */
        len = lastPeriod + 1;
        output[len] = '\0';
    }

    /* 3. Strip trailing whitespace */
    while (len > 0 && (output[len - 1] == ' ' ||
                        output[len - 1] == '\n' ||
                        output[len - 1] == '\r' ||
                        output[len - 1] == '\t')) {
        len--;
    }
    output[len] = '\0';

    return len;
}
