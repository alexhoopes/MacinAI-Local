/*----------------------------------------------------------------------
    Installer.r - Resources for MacinAI Local Installer
    CodeWarrior Pro 5 Rez format

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

#include "Types.r"
#include "SysTypes.r"

/* ---- DITL 128: Message (single OK button) ---- */
resource 'DITL' (128, "Message Items", purgeable) {
    {
        /* Item 1: OK button */
        {115, 290, 135, 370},
        Button {
            enabled,
            "OK"
        },

        /* Item 2: Line 1 text (^0) */
        {10, 75, 30, 370},
        StaticText {
            disabled,
            "^0"
        },

        /* Item 3: Line 2 text (^1) */
        {35, 75, 105, 370},
        StaticText {
            disabled,
            "^1"
        }
    }
};

/* ---- ALRT 128: Message alert ---- */
resource 'ALRT' (128, "Message Alert", purgeable) {
    {60, 60, 210, 440},
    128,
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};

/* ---- DITL 129: Yes/No (Install + Skip buttons) ---- */
resource 'DITL' (129, "YesNo Items", purgeable) {
    {
        /* Item 1: Install/OK button */
        {120, 310, 140, 390},
        Button {
            enabled,
            "Install"
        },

        /* Item 2: Skip/Quit button */
        {120, 210, 140, 300},
        Button {
            enabled,
            "Skip"
        },

        /* Item 3: Line 1 text (^0) */
        {10, 75, 30, 390},
        StaticText {
            disabled,
            "^0"
        },

        /* Item 4: Line 2 text (^1) */
        {35, 75, 110, 390},
        StaticText {
            disabled,
            "^1"
        }
    }
};

/* ---- ALRT 129: Yes/No alert ---- */
resource 'ALRT' (129, "YesNo Alert", purgeable) {
    {60, 60, 220, 460},
    129,
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};

/* ---- SIZE resource ---- */
resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    multiFinderAware,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    not32BitCompatible,
    reserved,
    reserved,
    reserved,
    reserved,
    reserved,
    reserved,
    reserved,
    393216,
    262144
};
