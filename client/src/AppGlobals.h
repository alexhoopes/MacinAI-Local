/*------------------------------------------------------------------------------
    AppGlobals.h - Global state and constants for MacinAI Local

    This header defines shared constants, types, and global variables
    for the self-contained MacinAI Local application. No network, no relay.
    The AI model runs natively on the Mac via the inference engine.

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

#ifndef APPGLOBALS_H
#define APPGLOBALS_H

#include <Types.h>
#include <Files.h>
#include <Quickdraw.h>
#include "AppVersion.h"

/*------------------------------------------------------------------------------
    Application States
------------------------------------------------------------------------------*/
typedef enum {
    kAppStateSplash,        /* Splash screen + model loading */
    kAppStateMain,          /* Main application (chat interface) */
    kAppStateQuitting       /* Application shutting down */
} AppState;

/*------------------------------------------------------------------------------
    Hardware Detection (from Gestalt at startup)
------------------------------------------------------------------------------*/
typedef struct {
    short   cpuType;            /* gestaltProcessorType result */
    short   fpuType;            /* gestaltFPUType result (0 = no FPU) */
    Boolean hasFPU;             /* Convenience flag: fpuType != 0 */
    long    physicalRAM;        /* Total physical RAM in bytes */
    long    availableRAM;       /* Largest contiguous free block */
    char    machineName[64];    /* Human-readable machine name */
    char    systemVersion[32];  /* e.g., "7.5.3" */
} HardwareInfo;

/*------------------------------------------------------------------------------
    Local Model State
------------------------------------------------------------------------------*/
typedef struct {
    /* Model file info */
    FSSpec  modelFileSpec;      /* FSSpec for the model weight file */
    FSSpec  vocabFileSpec;      /* FSSpec for the vocabulary file */
    Boolean modelFileValid;     /* Model file found and readable */
    Boolean vocabFileValid;     /* Vocab file found and readable */

    /* Model configuration (read from weight file header) */
    short   numLayers;          /* Number of transformer layers */
    short   hiddenDim;          /* Hidden dimension (e.g., 512) */
    short   numHeads;           /* Number of attention heads */
    short   ffnDim;             /* FFN intermediate dimension */
    long    vocabSize;          /* Vocabulary size (e.g., 8192) */
    short   maxSeqLen;          /* Maximum sequence length */

    /* Quantization and memory */
    short   quantType;          /* 0=float32, 1=int8, 2=int4 */
    short   layersInRAM;        /* How many layers fit in memory */
    short   layersOnDisk;       /* Layers that must be paged from disk */
    long    modelSizeBytes;     /* Total model file size */
    long    arenaSize;          /* Arena allocation size */

    /* Runtime state */
    Boolean modelLoaded;        /* Model weights loaded and ready */
    Boolean isGenerating;       /* Currently generating a response */

    /* Token counters (per conversation) */
    long    totalTokensIn;      /* Total input tokens processed */
    long    totalTokensOut;     /* Total output tokens generated */
} LocalModelState;

/*------------------------------------------------------------------------------
    Conversation Choice State
    When the model returns multiple search results, the user picks one.
    This is handled with pure C string matching, no model invocation.
------------------------------------------------------------------------------*/
#define kMaxSearchResults   20
#define kMaxResultNameLen   64
#define kMaxResultPathLen   256

typedef enum {
    kPendingNone = 0,
    kPendingLaunchApp,
    kPendingOpenCP,
    kPendingOpenDocument
} PendingActionType;

typedef struct {
    char    name[kMaxResultNameLen];
    char    path[kMaxResultPathLen];
    FSSpec  fileSpec;
} SearchResult;

typedef struct {
    Boolean             awaitingChoice;     /* User needs to pick from list */
    PendingActionType   pendingAction;      /* What to do with the choice */
    SearchResult        results[kMaxSearchResults];
    short               resultCount;
} ChoiceState;

/*------------------------------------------------------------------------------
    Speech Settings
------------------------------------------------------------------------------*/
typedef struct {
    Boolean     speechEnabled;      /* Master toggle (default: false) */
    Boolean     autoSpeak;          /* Auto-speak AI responses (default: false) */
    short       voiceIndex;         /* Selected voice (0 = system default) */
    char        voiceName[64];      /* Voice name for matching after restart */
    short       ratePercent;        /* Rate 0-100 (50 = normal) */
    Boolean     feedbackSounds;     /* UI feedback sounds enabled */
    Boolean     streamSpeech;       /* Speak word-by-word during generation (default: false) */
} SpeechSettings;

/*------------------------------------------------------------------------------
    Persistent Settings (saved to MacinAI Local Settings file)
------------------------------------------------------------------------------*/
#define kSettingsVersion    2

typedef struct {
    long            version;            /* Settings file version */
    SpeechSettings  speech;             /* Speech settings */
    Boolean         beepsEnabled;       /* Application beeps enabled */
    char            modelFileName[64];  /* Model weight filename (in app folder) */
    char            vocabFileName[64];  /* Vocab filename (in app folder) */
    Boolean         debugLogging;       /* Runtime debug logging enabled */
} PersistentSettings;

/*------------------------------------------------------------------------------
    Conversation Storage (local .oas files)
------------------------------------------------------------------------------*/
#define kMaxConversations       24
#define kMaxConversationTitle   64

typedef struct {
    char    filename[64];
    char    title[kMaxConversationTitle];
    char    savedTime[64];
    char    lastEditedTime[64];
} ConversationInfo;

typedef struct {
    short               count;
    ConversationInfo    conversations[kMaxConversations];
} ConversationList;

/*------------------------------------------------------------------------------
    Global Application Data
------------------------------------------------------------------------------*/
typedef struct {
    QDGlobals           qd;
    WindowPtr           currentWindow;
    AppState            appState;
    Boolean             done;

    /* Settings */
    PersistentSettings  settings;

    /* Hardware */
    HardwareInfo        hardware;

    /* Model */
    LocalModelState     model;

    /* Conversation */
    ChoiceState         choiceState;

    /* Status bar text */
    char                statusText[128];
} AppGlobals;

/*------------------------------------------------------------------------------
    Resource IDs
------------------------------------------------------------------------------*/
#define rMenuBar            128
#define rAboutAlert         128

/* Menu IDs */
#define mApple              128
#define iAbout              1

#define mFile               129
#define iNew                1
#define iClose              2
#define iOpen               4
#define iSave               6
#define iSaveAs             7
#define iQuit               9

#define mEdit               130
#define iUndo               1
#define iCut                3
#define iCopy               4
#define iPaste              5
#define iClear              6
#define iSelectAll          8
#define iSettings           10

/* Dialog IDs */
#define rSplashDialog       200

/* Alert IDs */
#define kShutdownAlertID    300
#define kRestartAlertID     301

/*------------------------------------------------------------------------------
    Constants
------------------------------------------------------------------------------*/
#define kWindowMargin           20
#define kButtonHeight           30
#define kTextFieldHeight        20

/* Chat Window UI Constants */
#define kStatusBarHeight        20
#define kMaxInputTextLength     10000
#define kMaxOutputTextLength    100000
#define kTextEditDestHeight     32000

/* Timing */
#define kShortDelayTicks        8

/* Error Codes */
#ifndef paramErr
#define paramErr                -50
#endif

/*------------------------------------------------------------------------------
    External Global (defined in MacinAI.c)
------------------------------------------------------------------------------*/
extern AppGlobals gApp;

/*------------------------------------------------------------------------------
    AppBeep - Conditional beep that respects beepsEnabled setting
------------------------------------------------------------------------------*/
void AppBeep(void);

#endif /* APPGLOBALS_H */
