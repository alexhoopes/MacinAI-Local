/*------------------------------------------------------------------------------
    AppVersion.c - Version string implementation for MacinAI Local

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

#include "AppVersion.h"
#include <stdio.h>
#include <string.h>

#pragma segment Main

static char sVersionStr[32];
static char sFullVersionStr[64];

const char* GetVersionString(void)
{
    sprintf(sVersionStr, "%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
    return sVersionStr;
}

const char* GetFullVersionString(void)
{
    const char* typeStr;

    typeStr = GetVersionTypeString();
    if (IS_PROD)
        sprintf(sFullVersionStr, "MacinAI Local v%d.%d.%d",
                VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
    else
        sprintf(sFullVersionStr, "MacinAI Local v%d.%d.%d %s",
                VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, typeStr);
    return sFullVersionStr;
}

const char* GetVersionTypeString(void)
{
#if IS_PROD
    return "Production";
#elif IS_BETA
    return "Beta";
#elif IS_DEV
    return "Development";
#else
    return "Pre-Release";
#endif
}
