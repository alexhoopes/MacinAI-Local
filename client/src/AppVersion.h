/*------------------------------------------------------------------------------
    AppVersion.h - Version Configuration for MacinAI Local

    Defines version numbers, build type flags, and conditional compilation
    macros for MacinAI Local (self-contained, no network).

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

#ifndef APPVERSION_H
#define APPVERSION_H

/*------------------------------------------------------------------------------
    Version Numbers
------------------------------------------------------------------------------*/
#define VERSION_MAJOR    0
#define VERSION_MINOR    1
#define VERSION_PATCH    0

/*------------------------------------------------------------------------------
    Version Type
    Only ONE of these should be set to 1.
------------------------------------------------------------------------------*/
#define VERSION_TYPE_PRODUCTION    1
#define VERSION_TYPE_BETA          0
#define VERSION_TYPE_DEVELOPMENT   0
#define VERSION_TYPE_PRE_RELEASE   0

#define CURRENT_VERSION_TYPE VERSION_TYPE_PRODUCTION

/*------------------------------------------------------------------------------
    Derived Version Flags
------------------------------------------------------------------------------*/
#define IS_PROD  (CURRENT_VERSION_TYPE == VERSION_TYPE_PRODUCTION)
#define IS_BETA  (CURRENT_VERSION_TYPE == VERSION_TYPE_BETA)
#define IS_DEV   (CURRENT_VERSION_TYPE == VERSION_TYPE_DEVELOPMENT)

/*------------------------------------------------------------------------------
    Feature Flags
------------------------------------------------------------------------------*/
#define ENABLE_DEBUG_LOGGING  1  /* Always compiled in, toggled at runtime via Settings */

/*------------------------------------------------------------------------------
    Version String Functions
------------------------------------------------------------------------------*/
const char* GetVersionString(void);
const char* GetFullVersionString(void);
const char* GetVersionTypeString(void);

#endif /* APPVERSION_H */
