/*------------------------------------------------------------------------------
    SystemDiscovery.h - Hardware Detection for MacinAI Local

    Detects CPU type, FPU presence, RAM, system version, and machine name
    using Gestalt at startup. This info determines the inference engine's
    memory allocation strategy and math code path.

    Stripped from MacinAI relay version:
    - Removed catalog scanning (replaced by CatalogResolver)
    - Removed upload to server (no network)
    - Removed system info formatting for AI context
    - Kept hardware detection for engine configuration

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

#ifndef SYSTEMDISCOVERY_H
#define SYSTEMDISCOVERY_H

#include <Types.h>
#include "AppGlobals.h"

/* Detect hardware and populate gApp.hardware */
OSErr SystemDiscovery_DetectHardware(void);

/* Get human-readable hardware summary */
void SystemDiscovery_GetSummary(char *buffer, short maxLen);

/* Get system version string (e.g., "7.5.3") */
void SystemDiscovery_GetSystemVersion(char *buffer, short maxLen);

/* Get machine name string */
void SystemDiscovery_GetMachineName(char *buffer, short maxLen);

#endif /* SYSTEMDISCOVERY_H */
