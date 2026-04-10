/*------------------------------------------------------------------------------
    MacinAI_UI.r - User Interface Resources

    All UI resources for MacinAI Local:
    - Chat Window (WIND 200, CNTL 200-206, STR# 200)
    - Legacy Window Template (WIND 128, CNTL 128-140, DITL 128, STR# 128)
    - Alert Dialogs (ALRT/DITL 129-131, ALRT/DITL 200, ICON 128-129)

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
#include "SysTypes.r"

/*==============================================================================
    Main Chat Window Template
==============================================================================*/

/* Main window - uses custom controls, not DLOG */
resource 'WIND' (128, "Main Chat Window", purgeable) {
    {50, 50, 450, 650},     /* Bounds: top, left, bottom, right */
    documentProc,            /* Standard document window with grow box */
    visible,
    goAway,
    0x0,                     /* refCon */
    "MacinAI Local",         /* Title */
    centerMainScreen
};

/*==============================================================================
    Control Templates (cntl resources)

    These define buttons with icons. The Control Manager handles drawing.
==============================================================================*/

/* Toolbar Icons (32x32) - Icon buttons */
resource 'CNTL' (128, "Toolbar Left Icon", purgeable) {
    {8, 10, 40, 42},         /* Bounds: top, left, bottom, right (32x32) */
    141,                      /* Initial value (icon ID) */
    visible,
    0,                        /* Max */
    0,                        /* Min */
    pushButProc,              /* Standard push button */
    0,                        /* refCon */
    ""                        /* Title (empty for icon-only) */
};

resource 'CNTL' (129, "Toolbar Center Icon", purgeable) {
    {8, 284, 40, 316},       /* Centered (assuming 600px wide window) */
    128,                      /* MacinAI logo icon ID */
    visible,
    0,
    0,
    pushButProc,
    0,
    ""
};

resource 'CNTL' (130, "Toolbar Right Icon", purgeable) {
    {8, 558, 40, 590},       /* Right side */
    130,                      /* New chat icon ID */
    visible,
    0,
    0,
    pushButProc,
    0,
    ""
};

/* New Chat Button - Icon + Text */
resource 'CNTL' (131, "New Chat Button", purgeable) {
    {78, 20, 98, 140},       /* In sidebar */
    0,
    visible,
    1,
    0,
    pushButProc,
    0,
    "+ New Chat"
};

/* Send Button - Icon + Text */
resource 'CNTL' (132, "Send Button", purgeable) {
    {368, 520, 394, 590},    /* Bottom right */
    0,
    visible,
    1,
    0,
    pushButProc,
    0,
    "Send"
};

/*==============================================================================
    Dialog Item Lists (DITL)

    Defines layout of controls in the window. While we use a WIND not DLOG,
    we can still use DITL patterns for organization.
==============================================================================*/

resource 'DITL' (128, "Main Window Items", purgeable) {
    {
        /* Toolbar icons */
        {8, 10, 40, 42},
        Control {
            enabled,
            128                /* Toolbar left icon control */
        },

        {8, 284, 40, 316},
        Control {
            enabled,
            129                /* Toolbar center icon control */
        },

        {8, 558, 40, 590},
        Control {
            enabled,
            130                /* Toolbar right icon control */
        },

        /* Sidebar header */
        {58, 20, 74, 140},
        StaticText {
            disabled,
            "Conversations"
        },

        /* New Chat button */
        {78, 20, 98, 140},
        Control {
            enabled,
            131                /* New Chat button control */
        },

        /* Output area label */
        {58, 170, 74, 220},
        StaticText {
            disabled,
            "Ask:"
        },

        /* Input area label */
        {340, 170, 356, 280},
        StaticText {
            disabled,
            "Ask Anything:"
        },

        /* Send button */
        {368, 520, 394, 590},
        Control {
            enabled,
            132                /* Send button control */
        }
    }
};

/*==============================================================================
    Icon Suites for Buttons

    Associates icons with controls. The Control Manager will automatically
    display the icon in the button.
==============================================================================*/

/* cicn (color icon) resources - these are icon + mask for buttons */
/* Note: The actual icon data (ICN#, icl8, etc.) is already in MacinAI.rsrc */

/*==============================================================================
    Control Definitions for Icon Buttons

    CDEF (control definition) resources allow custom control behavior.
    However, for simplicity, we'll use standard controls with icon IDs.
==============================================================================*/

/* Alternative approach: Use icon controls (System 7.5+) */
resource 'CNTL' (140, "Icon Button Template", purgeable) {
    {0, 0, 32, 32},
    0,                        /* Value = icon ID to display */
    visible,
    0,                        /* Max */
    0,                        /* Min */
    pushButProc,
    0,
    ""
};

/*==============================================================================
    Static Text Resources

    For labels and non-editable text.
==============================================================================*/

resource 'STR#' (128, "UI Strings", purgeable) {
    {
        "Conversations",
        "Ask:",
        "Ask Anything:",
        "+ New Chat",
        "Send",
        "MacinAI Local"
    }
};

/*==============================================================================
    Alert Dialogs

    ALRT resources for user prompts and confirmations.
==============================================================================*/

/* Save Changes Alert - shown when starting new chat with unsaved changes */
resource 'ALRT' (200, "Save Changes Alert", purgeable) {
    {40, 40, 160, 380},    /* Bounds */
    200,                    /* DITL resource ID */
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};

resource 'DITL' (200, "Save Changes Items", purgeable) {
    {
        /* Item 1: Save button (default - returns 1) */
        {52, 210, 72, 280},
        Button {
            enabled,
            "Save"
        },

        /* Item 2: Cancel button (returns 2) */
        {52, 120, 72, 190},
        Button {
            enabled,
            "Cancel"
        },

        /* Item 3: Don't Save button (returns 3) */
        {52, 20, 72, 110},
        Button {
            enabled,
            "Don't Save"
        },

        /* Item 4: Static text - message */
        {10, 65, 42, 335},
        StaticText {
            disabled,
            "Save changes to the current conversation?"
        },

        /* Item 5: Icon (caution) */
        {10, 20, 42, 52},
        Icon {
            disabled,
            2  /* Caution icon */
        }
    }
};

/* MacinAI icon for alerts (32x32 black & white) */
/* Classic Mac computer icon - used by DITL 129 for Alert dialogs */
/* Note: DITL Icon items ONLY work with 'ICON' resources, not cicn/icl8/ICN# */
resource 'ICON' (128, "MacinAI Icon 128", purgeable) {
    /* MacinAI icon - extracted from ICN# 128 in MacinAI.rsrc */
    $"0000 0FFC 0000 1002 7FFF E001 8000 2001"
    $"8000 4001 9FFF CEE9 9000 4001 9000 4001"
    $"9000 4EE9 9084 4001 9084 4001 9084 4EB9"
    $"9000 4001 9000 4001 9201 4F79 91FE 4001"
    $"9082 4001 907D C001 9004 0001 9FFF 0002"
    $"8000 FFFC 8000 0400 8000 0400 800F C400"
    $"8000 0400 9800 0400 8000 0400 8000 0400"
    $"7FFF F800 4000 0800 4000 0800 3FFF F000"
};

resource 'ICON' (129, "MacinAI Alert Icon", purgeable) {
    /* MacinAI icon - extracted from ICN# 128 in MacinAI.rsrc */
    $"0000 0FFC 0000 1002 7FFF E001 8000 2001"
    $"8000 4001 9FFF CEE9 9000 4001 9000 4001"
    $"9000 4EE9 9084 4001 9084 4001 9084 4EB9"
    $"9000 4001 9000 4001 9201 4F79 91FE 4001"
    $"9082 4001 907D C001 9004 0001 9FFF 0002"
    $"8000 FFFC 8000 0400 8000 0400 800F C400"
    $"8000 0400 9800 0400 8000 0400 8000 0400"
    $"7FFF F800 4000 0800 4000 0800 3FFF F000"
};

/* Generic 2-button Alert - used by ShowMacinAIAlert */
/* ParamText: ^0 = title, ^1 = message */
resource 'ALRT' (129, "MacinAI 2-Button Alert", purgeable) {
    {40, 40, 150, 400},    /* Bounds: 110h x 360w */
    129,                    /* DITL resource ID */
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};

resource 'DITL' (129, "MacinAI 2-Button Items", purgeable) {
    {
        /* Item 1: OK button (default - returns 1) */
        {75, 270, 95, 340},
        Button {
            enabled,
            "OK"
        },

        /* Item 2: Cancel button (returns 2) */
        {75, 180, 95, 250},
        Button {
            enabled,
            "Cancel"
        },

        /* Item 3: Title text (^0) - bold */
        {10, 65, 28, 345},
        StaticText {
            disabled,
            "^0"
        },

        /* Item 4: Message text (^1) */
        {32, 65, 65, 345},
        StaticText {
            disabled,
            "^1"
        },

        /* Item 5: MacinAI Icon (cicn 145) */
        {10, 20, 42, 52},
        Icon {
            disabled,
            145  /* MacinAI cicn icon */
        }
    }
};

/* Download/Locate Alert - for StuffIt not found prompt */
resource 'ALRT' (130, "Download or Locate Alert", purgeable) {
    {40, 40, 150, 400},    /* Bounds: 110h x 360w */
    130,                    /* DITL resource ID */
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};

resource 'DITL' (130, "Download/Locate Items", purgeable) {
    {
        /* Item 1: Download button (returns 1) */
        {75, 180, 95, 260},
        Button {
            enabled,
            "Download"
        },

        /* Item 2: Locate button (returns 2) */
        {75, 270, 95, 340},
        Button {
            enabled,
            "Locate"
        },

        /* Item 3: Title text (^0) */
        {10, 65, 28, 345},
        StaticText {
            disabled,
            "^0"
        },

        /* Item 4: Message text (^1) */
        {32, 65, 65, 345},
        StaticText {
            disabled,
            "^1"
        },

        /* Item 5: MacinAI Icon */
        {10, 20, 42, 52},
        Icon {
            disabled,
            145
        }
    }
};

/* Extract/Later Alert - for archive extraction prompt */
resource 'ALRT' (131, "Extract Alert", purgeable) {
    {40, 40, 150, 400},
    131,
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};

resource 'DITL' (131, "Extract/Later Items", purgeable) {
    {
        /* Item 1: Extract button (returns 1) */
        {75, 270, 95, 340},
        Button {
            enabled,
            "Extract"
        },

        /* Item 2: Later button (returns 2) */
        {75, 190, 95, 260},
        Button {
            enabled,
            "Later"
        },

        /* Item 3: Title text (^0) */
        {10, 65, 28, 345},
        StaticText {
            disabled,
            "^0"
        },

        /* Item 4: Message text (^1) */
        {32, 65, 65, 345},
        StaticText {
            disabled,
            "^1"
        },

        /* Item 5: MacinAI Icon */
        {10, 20, 42, 52},
        Icon {
            disabled,
            145
        }
    }
};

/*==============================================================================
    Size Resource (SIZE)

    Tells Finder about memory requirements.
    Commented out for now - can be added via CodeWarrior project settings.
==============================================================================*/

/* Removed - causing Rez compilation issues with flag definitions */
/* Set memory size in CodeWarrior: Edit -> Target Settings -> PPC/68K Target */

/*==============================================================================
    Chat Window (ID range 200-206)

    The main chat window used by ChatWindow.c.
    Window with zoom/grow box, toolbar icons, sidebar, scrollbars, send button.
==============================================================================*/

resource 'WIND' (200, "Chat Window", purgeable) {
    {60, 100, 480, 700},     /* Bounds: 420h x 600w - fits 640x480 displays */
    8,                        /* zoomDocProc (document with grow + zoom box) */
    visible,
    goAway,
    0x0,
    "MacinAI Local",
    centerMainScreen
};

/* Toolbar Icon Controls (32x32) */
resource 'CNTL' (200, "Toolbar Left", purgeable) {
    {8, 10, 40, 42},
    141,                      /* Icon ID: list/menu */
    visible, 0, 0,
    pushButProc, 0, ""
};

resource 'CNTL' (201, "Toolbar Center", purgeable) {
    {8, 324, 40, 356},       /* Centered in 600px window */
    128,                      /* Icon ID: MacinAI logo */
    visible, 0, 0,
    pushButProc, 0, ""
};

resource 'CNTL' (202, "Toolbar Right", purgeable) {
    {8, 558, 40, 590},
    139,                      /* Icon ID: new chat */
    visible, 0, 0,
    pushButProc, 0, ""
};

/* Sidebar */
resource 'CNTL' (203, "New Chat Button", purgeable) {
    {78, 10, 98, 140},
    0, visible, 1, 0,
    pushButProc, 0, "+ New Chat"
};

/* Output Scrollbar */
resource 'CNTL' (205, "Output Scrollbar", purgeable) {
    {74, 572, 334, 588},
    0, visible, 0, 0,
    scrollBarProc, 0, ""
};

/* Input Scrollbar */
resource 'CNTL' (206, "Input Scrollbar", purgeable) {
    {357, 453, 403, 469},
    0, visible, 0, 0,
    scrollBarProc, 0, ""
};

/* Send Button */
resource 'CNTL' (204, "Send Button", purgeable) {
    {368, 500, 393, 580},
    0, visible, 1, 0,
    pushButProc, 0, "Send"
};

/* Chat Window Strings */
resource 'STR#' (200, "Chat Window Strings", purgeable) {
    {
        "Conversations",
        "Ask:",
        "Ask Anything:",
        "+ New Chat",
        "Send",
        "MacinAI Local"
    }
};
