/*------------------------------------------------------------------------------
    DrawingHelpers.c - Common UI drawing utilities

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

#include "DrawingHelpers.h"
#include <Fonts.h>
#include <string.h>

#pragma segment ChatUI

/*------------------------------------------------------------------------------
    DrawButton - Draw a rounded rectangle button with centered text
------------------------------------------------------------------------------*/
void DrawButton(const Rect *rect, ConstStr255Param text, Boolean pressed)
{
    short textWidth, textX, textY;

    /* Erase button area first */
    EraseRect(rect);

    /* Draw button frame */
    PenSize(2, 2);
    FrameRoundRect(rect, 12, 12);
    PenNormal();
    PenSize(1, 1);

    /* Invert if pressed */
    if (pressed)
    {
        InvertRect(rect);
    }

    /* Draw centered text */
    TextSize(12);
    TextFace(normal);
    textWidth = StringWidth(text);
    textX = rect->left + ((rect->right - rect->left) - textWidth) / 2;
    textY = rect->top + ((rect->bottom - rect->top) + 12) / 2;
    MoveTo(textX, textY);
    DrawString(text);
}

/*------------------------------------------------------------------------------
    DrawGroupBox - Draw a group box (rounded rectangle frame) with title
------------------------------------------------------------------------------*/
void DrawGroupBox(const Rect *rect, ConstStr255Param title)
{
    Rect safeRect;
    GrafPtr port;

    GetPort(&port);
    safeRect = *rect;

    /* Clip rect to port bounds to prevent math domain errors */
    if (safeRect.left < port->portRect.left) safeRect.left = port->portRect.left;
    if (safeRect.top < port->portRect.top) safeRect.top = port->portRect.top;
    if (safeRect.right > port->portRect.right) safeRect.right = port->portRect.right;
    if (safeRect.bottom > port->portRect.bottom) safeRect.bottom = port->portRect.bottom;

    /* Ensure rect is valid */
    if (safeRect.left >= safeRect.right || safeRect.top >= safeRect.bottom)
        return;

    /* Draw title above the frame */
    TextFont(3);    /* Geneva */
    TextSize(10);
    TextFace(bold);

    {
        short titleWidth = StringWidth(title);
        short titleX = safeRect.left + 8;
        short titleY = safeRect.top + 3;
        Rect eraseRect;

        /* Draw frame first */
        PenSize(1, 1);
        FrameRoundRect(&safeRect, 8, 8);

        /* Erase area where title goes */
        SetRect(&eraseRect, titleX - 2, safeRect.top - 1, titleX + titleWidth + 2, safeRect.top + 6);
        EraseRect(&eraseRect);

        /* Draw title text */
        MoveTo(titleX, titleY);
        DrawString(title);
    }

    TextFace(normal);
    PenSize(1, 1);
}

/*------------------------------------------------------------------------------
    DrawTextField - Draw a text field frame and update its TextEdit contents
------------------------------------------------------------------------------*/
void DrawTextField(const Rect *rect, TEHandle textEdit)
{
    FrameRect(rect);

    if (textEdit != nil)
    {
        TEUpdate(rect, textEdit);
    }
}

/*------------------------------------------------------------------------------
    DrawCenteredHeader - Draw centered header text with underline
------------------------------------------------------------------------------*/
void DrawCenteredHeader(short windowWidth, ConstStr255Param title, short yPos)
{
    short headerWidth;

    PenNormal();
    TextFont(3);    /* Geneva */
    TextSize(14);
    TextFace(bold);
    headerWidth = StringWidth(title);
    MoveTo((windowWidth - headerWidth) / 2, yPos);
    DrawString(title);
    TextFace(normal);

    MoveTo(0, yPos + 12);
    LineTo(windowWidth, yPos + 12);
}
