/*------------------------------------------------------------------------------
    SpeechManager.h - Speech synthesis wrapper module

    Provides text-to-speech using Mac OS Speech Manager.
    Gracefully handles systems where Speech Manager unavailable.

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

#ifndef SPEECHMANAGER_H
#define SPEECHMANAGER_H

#include <Types.h>

/*------------------------------------------------------------------------------
    Availability & Lifecycle
------------------------------------------------------------------------------*/
Boolean Speech_IsAvailable(void);
OSErr Speech_Initialize(void);
void Speech_Cleanup(void);

/*------------------------------------------------------------------------------
    Voice Management
------------------------------------------------------------------------------*/
short Speech_GetVoiceCount(void);
void Speech_GetVoiceName(short index, Str255 voiceName);
short Speech_GetCurrentVoiceIndex(void);
OSErr Speech_SetVoice(short index);

/*------------------------------------------------------------------------------
    Rate Control
------------------------------------------------------------------------------*/
short Speech_GetRatePercent(void);
OSErr Speech_SetRatePercent(short percent);

/*------------------------------------------------------------------------------
    Speaking Functions
------------------------------------------------------------------------------*/
OSErr Speech_SpeakText(const char *text, long textLen);
Boolean Speech_IsBusy(void);
OSErr Speech_Stop(void);
void Speech_Feedback(const char *message);
void Speech_FeedbackInterrupt(const char *message);
void Speech_ProcessQueue(void);

/*------------------------------------------------------------------------------
    Settings
------------------------------------------------------------------------------*/
void Speech_ApplySettings(void);

#endif /* SPEECHMANAGER_H */
