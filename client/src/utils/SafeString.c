/*------------------------------------------------------------------------------
    SafeString.c

    Part of MacinAI Local.

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

/*==============================================================================
    SafeString.c - Safe string operations implementation

    Implements bounds-checked string functions for System 7.5.3+ compatibility.
    Uses Mac OS Toolbox memory functions for performance.
==============================================================================*/

#include "SafeString.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <Memory.h>

void SafeStringCopy(char* dest, const char* src, short destSize)
{
    short len;

    if (dest == NULL || src == NULL || destSize <= 0)
        return;

    len = strlen(src);
    if (len >= destSize)
        len = destSize - 1;

    if (len > 0)
        BlockMoveData(src, dest, len);

    dest[len] = '\0';
}

void SafeStringCat(char* dest, const char* src, short destSize)
{
    short destLen, srcLen, spaceLeft;

    if (dest == NULL || src == NULL || destSize <= 0)
        return;

    destLen = strlen(dest);
    srcLen = strlen(src);
    spaceLeft = destSize - destLen - 1;

    if (spaceLeft <= 0)
        return;

    if (srcLen > spaceLeft)
        srcLen = spaceLeft;

    if (srcLen > 0)
        BlockMoveData(src, dest + destLen, srcLen);

    dest[destLen + srcLen] = '\0';
}

short SafeStringPrintf(char* dest, short destSize, const char* format, ...)
{
    va_list args;
    char tempBuf[1024];
    short len;
    Boolean overflow;

    if (dest == NULL || format == NULL || destSize <= 0)
        return -1;

    va_start(args, format);
    vsprintf(tempBuf, format, args);
    va_end(args);

    len = strlen(tempBuf);
    overflow = false;

    if (len >= destSize) {
        len = destSize - 1;
        overflow = true;
    }

    if (len > 0)
        BlockMoveData(tempBuf, dest, len);

    dest[len] = '\0';

    return overflow ? -1 : len;
}

Boolean SafeStringCatPrintf(char* dest, short destSize, const char* format, ...)
{
    va_list args;
    char tempBuf[512];
    short destLen, tempLen, spaceLeft;
    Boolean overflow;

    if (dest == NULL || format == NULL || destSize <= 0)
        return false;

    destLen = strlen(dest);
    spaceLeft = destSize - destLen - 1;

    if (spaceLeft <= 0)
        return false;

    va_start(args, format);
    vsprintf(tempBuf, format, args);
    va_end(args);

    tempLen = strlen(tempBuf);
    overflow = false;

    if (tempLen > spaceLeft) {
        tempLen = spaceLeft;
        overflow = true;
    }

    if (tempLen > 0)
        BlockMoveData(tempBuf, dest + destLen, tempLen);

    dest[destLen + tempLen] = '\0';

    return !overflow;
}
