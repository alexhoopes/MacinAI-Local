/*------------------------------------------------------------------------------
    ChatWindow.h - Main Chat Window

    Chat interface for MacinAI Local. Same UI as MacinAI (relay) but with
    model status bar instead of connection status.

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

#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <Types.h>
#include <Windows.h>
#include <Events.h>

/* Initialize and show chat window */
void ChatScreen_Show(void);

/* Close chat window */
void ChatScreen_Close(void);

/* Handle events */
Boolean ChatScreen_HandleEvent(EventRecord *event);

/* Check if chat window is open */
Boolean ChatScreen_IsOpen(void);

/* Get window pointer */
WindowPtr ChatScreen_GetWindow(void);

/* Handle save from menu */
void ChatScreen_HandleSave(void);

/* Update window title with current conversation name */
void ChatScreen_UpdateTitle(void);

/* Handle idle time */
void ChatScreen_HandleIdle(void);

/* Edit menu handlers */
void ChatWindow_HandleCut(void);
void ChatWindow_HandleCopy(void);
void ChatWindow_HandlePaste(void);
void ChatWindow_HandleClear(void);
void ChatWindow_HandleSelectAll(void);

/* TextEdit accessor functions */
TEHandle ChatWindow_GetInputTE(void);
TEHandle ChatWindow_GetOutputTE(void);
Boolean ChatWindow_HasInputSelection(void);
Boolean ChatWindow_HasOutputSelection(void);
long ChatWindow_GetInputLength(void);
long ChatWindow_GetOutputLength(void);

/* Update output scrollbar after text changes */
void ChatWindow_UpdateScrollbar(void);

/* Redraw the status bar */
void ChatWindow_UpdateStatus(void);

/* TextEdit handles - exposed for Mac Toolbox API compatibility */
extern TEHandle gInputTE;
extern TEHandle gOutputTE;

#endif /* CHATWINDOW_H */
