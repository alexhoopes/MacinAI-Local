/*----------------------------------------------------------------------
    Installer.h - MacinAI Local 2-Disc CD Installer

    Custom UI installer for Classic Mac OS.
    No system alerts, all custom windows with controls.

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

#ifndef INSTALLER_H
#define INSTALLER_H

#include <Types.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Controls.h>
#include <Windows.h>
#include <Events.h>
#include <Files.h>
#include <Folders.h>
#include <Gestalt.h>
#include <Processes.h>
#include <Aliases.h>
#include <TextUtils.h>
#include <Icons.h>
#include <Fonts.h>
#include <string.h>
#include <stdio.h>

/* Control part codes (if not defined by headers) */
#ifndef inButton
#define inButton 10
#endif
#ifndef inCheckBox
#define inCheckBox 11
#endif

/* Alias constants (if not defined by headers) */
#ifndef rAliasType
#define rAliasType 'alis'
#endif
#ifndef kIsAlias
#define kIsAlias 0x8000
#endif

/* Copy buffer, 32KB chunks */
#define kCopyBufferSize     32768L

/* Disc volume names */
#define kDisc1VolName       "\pMacinAI Disc 1"
#define kDisc2VolName       "\pMacinAI Disc 2"

/* CD subfolder names */
#define kDataFolder         "\pData"
#define kApp68KFolder       "\pApp_68K"
#define kAppPPCFolder       "\pApp_PPC"
#define kModelsFolder       "\pModels"

/* Install destination */
#define kInstallFolderName  "\pMacinAI-Local"
#define kAppFileName        "\pMacinAI Local"
#define kInstallerCopyName  "\pMacinAI Installer"
#define kAppsFolderName     "\pApplications"
#define kApps9FolderName    "\pApplications (Mac OS 9)"

/* Model file names */
#define kModel1Name         "\pMacinAI Tool 94M v7 Q8g32.bin"
#define kModel2Name         "\pGPT-2 124M.bin"
#define kModel3Name         "\pSmolLM 360M Instruct.bin"
#define kModel4Name         "\pQwen2.5 0.5B Instruct.bin"

/* Model sizes in MB (for display) */
#define kModel1SizeMB       102
#define kModel2SizeMB       137
#define kModel3SizeMB       389
#define kModel4SizeMB       533

/* File type/creator */
#define kAppType            'APPL'
#define kAppCreator         'MaAI'
#define kModelType          'BINA'
#define kModelCreator       'MaAI'

/* UI dimensions */
#define kWelcomeWidth       360
#define kWelcomeHeight      280
#define kSelectWidth        380
#define kSelectHeight       300
#define kProgressWidth      400
#define kProgressHeight     100

/* Model selection */
typedef struct {
    Boolean macinaiTool;    /* MacinAI Tool 94M (Disc 1) */
    Boolean gpt2;           /* GPT-2 124M (Disc 1) */
    Boolean smolLM;         /* SmolLM 360M (Disc 1) */
    Boolean qwen;           /* Qwen 0.5B (Disc 2) */
} ModelChoices;

#endif /* INSTALLER_H */
