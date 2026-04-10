/*------------------------------------------------------------------------------
    ConversationManager.h - Conversation file I/O and management

    Handles conversation persistence using local .oas files.
    No server-side storage, everything is on disk.

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

#ifndef CONVERSATIONMANAGER_H
#define CONVERSATIONMANAGER_H

#include "AppGlobals.h"
#include <Types.h>
#include <TextEdit.h>

/* Initialize conversation system */
void ConvMgr_Init(void);

/* Load conversations from disk */
void ConvMgr_Load(void);

/* Save conversations index to disk */
void ConvMgr_Save(void);

/* Save current conversation with prompts */
void ConvMgr_SaveCurrent(TEHandle outputTE);

/* Create new conversation */
void ConvMgr_CreateNew(TEHandle outputTE);

/* Get current conversation title */
void ConvMgr_GetCurrentTitle(char* titleOut);

/* Save conversation content to file */
void ConvMgr_SaveContent(const char* filename, TEHandle outputTE);

/* Load conversation content from file */
void ConvMgr_LoadContent(const char* filename, TEHandle outputTE);

/* Accessors */
ConversationList* ConvMgr_GetList(void);
const char* ConvMgr_GetCurrentFilename(void);
void ConvMgr_SetCurrentFilename(const char* filename);
short ConvMgr_GetScrollOffset(void);
void ConvMgr_SetScrollOffset(short offset);
Boolean ConvMgr_HasUnsavedChanges(void);
void ConvMgr_SetUnsavedChanges(Boolean hasChanges);

/* Debug Logging */
#if ENABLE_DEBUG_LOGGING
void DebugLog(const char* message);
#endif

#endif /* CONVERSATIONMANAGER_H */
