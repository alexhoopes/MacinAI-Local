/*------------------------------------------------------------------------------
    SafeString.h

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
    SafeString.h - Safe string operations for C89

    Provides bounds-checked alternatives to unsafe C string functions.
    Compatible with CodeWarrior Pro 5 on System 7.5.3+
==============================================================================*/

#ifndef SAFESTRING_H
#define SAFESTRING_H

#include <Types.h>

void SafeStringCopy(char* dest, const char* src, short destSize);
void SafeStringCat(char* dest, const char* src, short destSize);
short SafeStringPrintf(char* dest, short destSize, const char* format, ...);
Boolean SafeStringCatPrintf(char* dest, short destSize, const char* format, ...);

#endif /* SAFESTRING_H */
