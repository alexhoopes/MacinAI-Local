/*------------------------------------------------------------------------------
    DrawingHelpers.h - Common UI drawing utilities

    Provides reusable drawing functions to reduce code duplication across
    the MacinAI Local application.

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

#ifndef DRAWINGHELPERS_H
#define DRAWINGHELPERS_H

#include <Types.h>
#include <Quickdraw.h>
#include <TextEdit.h>

/*------------------------------------------------------------------------------
    Button Drawing
------------------------------------------------------------------------------*/
/* Draw a rounded rectangle button with centered text */
void DrawButton(const Rect *rect, ConstStr255Param text, Boolean pressed);

/*------------------------------------------------------------------------------
    Group Box Drawing
------------------------------------------------------------------------------*/
/* Draw a group box (rounded rectangle frame) with title */
void DrawGroupBox(const Rect *rect, ConstStr255Param title);

/*------------------------------------------------------------------------------
    Text Field Drawing
------------------------------------------------------------------------------*/
/* Draw a text field frame and update its TextEdit contents */
void DrawTextField(const Rect *rect, TEHandle textEdit);

/*------------------------------------------------------------------------------
    Header Drawing
------------------------------------------------------------------------------*/
/* Draw centered header text with underline */
void DrawCenteredHeader(short windowWidth, ConstStr255Param title, short yPos);

#endif /* DRAWINGHELPERS_H */
