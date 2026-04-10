/*------------------------------------------------------------------------------
    MacinAI_AppIcon.r - Application Icon Bundle Resources

    Defines BNDL and FREF resources to associate the application with
    icon family 128 from MacinAI.rsrc

    Compile with Rez and merge into application resource fork.

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

#include "Types.r"

/* Application signature - must match CODE resource signature */
/* OAS = OldAppleStuff (designer) - note: must be exactly 4 chars */
#define kAppSignature 'OAS '

/*
 * Bundle resource - associates application signature with icon
 */
resource 'BNDL' (128) {
    kAppSignature,  /* Application signature */
    0,              /* Resource ID for local numbering */
    {
        /* Icon family */
        'ICN#', {
            0, 128  /* Local ID 0 maps to ICN# 128 */
        },
        /* File reference */
        'FREF', {
            0, 128  /* Local ID 0 maps to FREF 128 */
        }
    }
};

/*
 * File reference - defines the file type and icon association
 */
resource 'FREF' (128) {
    'APPL',  /* File type for application */
    0,       /* Icon local ID (maps to ICN# 128 via BNDL) */
    ""       /* File name (empty = use application name) */
};

/*
 * Version resource - displayed in Finder "Get Info"
 */
/*
 * Version numbers - MUST match AppVersion.h
 * When changing version, update both files:
 *   - src/AppVersion.h (VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH)
 *   - This file (vers resources below)
 * Pre-release builds: include '-pre' suffix (e.g., "1.0.0-pre0.1")
 * Production builds: no suffix (e.g., "1.0.0")
 */
resource 'vers' (1) {
    0x01,       /* Major version: 1 */
    0x00,       /* Minor.Patch in BCD: 0.0 = 0x00 */
    beta,       /* Release stage: beta (pre-release stage) */
    0x01,       /* Non-release version: 1 (pre0.1) */
    verUS,      /* Region: US */
    "1.0.0-pre0.1", /* Short version string */
    "1.0.0-pre0.1, \$A9 2025-2026 OldAppleStuff"
};

resource 'vers' (2) {
    0x01,       /* Major version: 1 */
    0x00,       /* Minor.Patch in BCD: 0.0 = 0x00 */
    beta,       /* Release stage: beta (pre-release stage) */
    0x01,       /* Non-release version: 1 (pre0.1) */
    verUS,      /* Region: US */
    "1.0.0-pre0.1", /* Short version string */
    "MacinAI 1.0.0 Pre-Release 0.1"
};
