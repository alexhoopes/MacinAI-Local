/*------------------------------------------------------------------------------
    CatalogResolver.h - Filesystem Action Resolution

    Resolves application names and control panel names to FSSpecs using
    PBCatSearchSync. This replaces the network-based tool calling system
    from MacinAI (relay) with direct filesystem catalog searching.

    The model outputs command tokens like [CMD:LAUNCH_APP] photoshop.
    CatalogResolver finds the actual application on disk using:
    1. Category knowledge table (known app/CP names and paths)
    2. PBCatSearchSync (fast volume-level file search)
    3. FindFolder (for system folders like Control Panels)
    4. Fuzzy name matching (partial, case-insensitive)

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

#ifndef CATALOGRESOLVER_H
#define CATALOGRESOLVER_H

#include <Types.h>
#include <Files.h>
#include "AppGlobals.h"

/*------------------------------------------------------------------------------
    Constants
------------------------------------------------------------------------------*/
#define kMaxResolveResults  20

/*------------------------------------------------------------------------------
    Resolve Result
------------------------------------------------------------------------------*/
typedef struct {
    char    name[64];
    char    path[256];
    FSSpec  spec;
    short   confidence;     /* 0-100, higher is better match */
} ResolveResult;

/*------------------------------------------------------------------------------
    Public API
------------------------------------------------------------------------------*/

/* Initialize catalog resolver */
OSErr CatalogResolver_Initialize(void);

/* Resolve application name to FSSpec(s) */
/* query: user-provided name (e.g., "photoshop", "simpletext") */
/* results: output array */
/* maxResults: size of output array */
/* Returns: number of results found */
short CatalogResolver_FindApp(const char *query,
                              ResolveResult *results, short maxResults);

/* Resolve control panel name to FSSpec */
/* query: CP name (e.g., "tcp/ip", "memory") */
/* Returns: noErr if found, fnfErr if not */
OSErr CatalogResolver_FindControlPanel(const char *query, FSSpec *result);

/* Launch application from ResolveResult */
OSErr CatalogResolver_LaunchApp(const ResolveResult *result);

/* Open control panel */
OSErr CatalogResolver_OpenControlPanel(const FSSpec *cpSpec);

/* Cleanup */
void CatalogResolver_Cleanup(void);

#endif /* CATALOGRESOLVER_H */
