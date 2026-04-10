/*----------------------------------------------------------------------
    MacSpecsTable.h - Hardware Specifications Lookup Table

    Provides a static table of Macintosh hardware specifications for
    all models from 1984-2001. Used by the inference engine to answer
    hardware spec questions with 100% accuracy, bypassing the neural
    model for factual recall.

    Data sourced from EveryMac.com specifications.

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

#ifndef MACSPECSTABLE_H
#define MACSPECSTABLE_H

#include <Types.h>

/* Maximum string lengths for spec fields */
#define kSpecNameLen        48
#define kSpecProcessorLen   40
#define kSpecFormLen        20
#define kSpecDisplayLen     52
#define kSpecStorageLen     80
#define kSpecBusLen         20
#define kSpecPortsLen       80
#define kSpecFPULen         32
#define kSpecNotesLen       160
#define kSpecReleaseLen     24
#define kSpecPriceLen       12

/* Maximum number of alias entries */
#define kMaxAliases         128

/* A single Macintosh model's specifications */
typedef struct {
    char name[kSpecNameLen];
    char processor[kSpecProcessorLen];
    short clockMHz;
    long ramMinKB;
    long ramMaxKB;
    char release[kSpecReleaseLen];
    char price[kSpecPriceLen];
    char form[kSpecFormLen];
    char display[kSpecDisplayLen];
    char storage[kSpecStorageLen];
    char bus[kSpecBusLen];
    short expansionSlots;
    char ports[kSpecPortsLen];
    char fpu[kSpecFPULen];
    char notes[kSpecNotesLen];
} MacSpec;

/* An alias mapping (e.g. "mac plus" -> index into specs table) */
typedef struct {
    char alias[kSpecNameLen];
    short specIndex;
} MacSpecAlias;

/*----------------------------------------------------------------------
    MacSpecs_Lookup - Find a Mac model in the specs table

    Searches the query string for known Mac model names.
    Returns a pointer to the MacSpec if found, nil otherwise.
    Uses longest-match substring search with alias support.
----------------------------------------------------------------------*/
const MacSpec* MacSpecs_Lookup(const char *query);

/*----------------------------------------------------------------------
    MacSpecs_FormatAnswer - Format a specs answer as natural text

    Writes a human-readable answer about the specified model into
    the output buffer. Returns the number of characters written.
    The answer includes processor, RAM, release date, display,
    storage, expansion, ports, and notes.
----------------------------------------------------------------------*/
short MacSpecs_FormatAnswer(const MacSpec *spec, char *output,
                            short maxLen);

/*----------------------------------------------------------------------
    MacSpecs_GetCount - Return the number of models in the table
----------------------------------------------------------------------*/
short MacSpecs_GetCount(void);

#endif /* MACSPECSTABLE_H */
