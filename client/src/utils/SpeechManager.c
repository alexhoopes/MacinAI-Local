/*------------------------------------------------------------------------------
    SpeechManager.c - Speech synthesis wrapper module

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

#include "SpeechManager.h"
#include "AppGlobals.h"
#include "AppVersion.h"
#include <Speech.h>
#include <Gestalt.h>
#include <Memory.h>
#include <string.h>
#include <stdio.h>

#pragma segment SpeechMgr

/*------------------------------------------------------------------------------
    Forward declarations
------------------------------------------------------------------------------*/
static long Speech_PreprocessText(const char *src, long srcLen, char *dst, long dstSize);
static void Speech_FeedbackEx(const char *message, Boolean interrupt);

/*------------------------------------------------------------------------------
    Module Globals
------------------------------------------------------------------------------*/
static Boolean          gSpeechAvailable = false;
static Boolean          gSpeechInitialized = false;
static SpeechChannel    gSpeechChannel = nil;
static short            gVoiceCount = 0;
static short            gCurrentVoiceIndex = 0;

#define kSpeechBufferSize 8192
static char             gSpeechBuffer[kSpeechBufferSize];

#define kFeedbackQueueSize 8
#define kFeedbackMsgSize 128
static char             gFeedbackQueue[kFeedbackQueueSize][kFeedbackMsgSize];
static short            gFeedbackQueueHead = 0;
static short            gFeedbackQueueTail = 0;
static short            gFeedbackQueueCount = 0;

#define kMinRate        0x00320000L
#define kDefaultRate    0x00960000L
#define kMaxRate        0x01900000L

extern AppGlobals gApp;

/*------------------------------------------------------------------------------
    Speech_IsAvailable
------------------------------------------------------------------------------*/
Boolean Speech_IsAvailable(void)
{
    return gSpeechAvailable;
}

/*------------------------------------------------------------------------------
    Speech_Initialize
------------------------------------------------------------------------------*/
OSErr Speech_Initialize(void)
{
    long gestaltResult;
    OSErr err;
    VoiceSpec selectedVoice;
    short victoriaIndex = 0;
    short i;

    if (gSpeechInitialized)
        return noErr;

    err = Gestalt(gestaltSpeechAttr, &gestaltResult);
    if (err != noErr)
    {
        gSpeechAvailable = false;
        return err;
    }

    if ((gestaltResult & (1 << gestaltSpeechMgrPresent)) == 0)
    {
        gSpeechAvailable = false;
        return noSynthFound;
    }

    gSpeechAvailable = true;

    err = CountVoices(&gVoiceCount);
    if (err != noErr)
    {
        gSpeechAvailable = false;
        return err;
    }

    /* Search for Victoria voice as preferred default */
    for (i = 1; i <= gVoiceCount; i++)
    {
        VoiceSpec voiceSpec;
        VoiceDescription voiceDesc;

        err = GetIndVoice(i, &voiceSpec);
        if (err != noErr)
            continue;

        err = GetVoiceDescription(&voiceSpec, &voiceDesc, sizeof(VoiceDescription));
        if (err != noErr)
            continue;

        if (voiceDesc.name[0] == 8 &&
            voiceDesc.name[1] == 'V' && voiceDesc.name[2] == 'i' &&
            voiceDesc.name[3] == 'c' && voiceDesc.name[4] == 't' &&
            voiceDesc.name[5] == 'o' && voiceDesc.name[6] == 'r' &&
            voiceDesc.name[7] == 'i' && voiceDesc.name[8] == 'a')
        {
            victoriaIndex = i;
            break;
        }
    }

    /* Use saved voice from settings if valid, otherwise default to Victoria or first */
    if (gApp.settings.speech.voiceIndex >= 1 && gApp.settings.speech.voiceIndex <= gVoiceCount)
    {
        err = GetIndVoice(gApp.settings.speech.voiceIndex, &selectedVoice);
        gCurrentVoiceIndex = gApp.settings.speech.voiceIndex;
    }
    else if (victoriaIndex > 0)
    {
        err = GetIndVoice(victoriaIndex, &selectedVoice);
        gCurrentVoiceIndex = victoriaIndex;
        gApp.settings.speech.voiceIndex = victoriaIndex;
    }
    else
    {
        err = GetIndVoice(1, &selectedVoice);
        gCurrentVoiceIndex = 1;
        gApp.settings.speech.voiceIndex = 1;
    }

    if (err != noErr)
    {
        gSpeechAvailable = false;
        return err;
    }

    err = NewSpeechChannel(&selectedVoice, &gSpeechChannel);
    if (err != noErr)
    {
        gSpeechAvailable = false;
        return err;
    }

    gSpeechInitialized = true;
    return noErr;
}

/*------------------------------------------------------------------------------
    Speech_Cleanup
------------------------------------------------------------------------------*/
void Speech_Cleanup(void)
{
    if (gSpeechChannel != nil)
    {
        StopSpeech(gSpeechChannel);
        DisposeSpeechChannel(gSpeechChannel);
        gSpeechChannel = nil;
    }
    gSpeechInitialized = false;
}

/*------------------------------------------------------------------------------
    Speech_GetVoiceCount
------------------------------------------------------------------------------*/
short Speech_GetVoiceCount(void)
{
    return gVoiceCount;
}

/*------------------------------------------------------------------------------
    Speech_GetVoiceName
------------------------------------------------------------------------------*/
void Speech_GetVoiceName(short index, Str255 voiceName)
{
    VoiceSpec voiceSpec;
    VoiceDescription voiceDesc;
    OSErr err;

    voiceName[0] = 0;

    if (!gSpeechAvailable || index < 1 || index > gVoiceCount)
        return;

    err = GetIndVoice(index, &voiceSpec);
    if (err != noErr)
        return;

    err = GetVoiceDescription(&voiceSpec, &voiceDesc, sizeof(VoiceDescription));
    if (err != noErr)
        return;

    BlockMove(voiceDesc.name, voiceName, voiceDesc.name[0] + 1);
}

/*------------------------------------------------------------------------------
    Speech_GetCurrentVoiceIndex
------------------------------------------------------------------------------*/
short Speech_GetCurrentVoiceIndex(void)
{
    return gCurrentVoiceIndex;
}

/*------------------------------------------------------------------------------
    Speech_SetVoice
------------------------------------------------------------------------------*/
OSErr Speech_SetVoice(short index)
{
    VoiceSpec voiceSpec;
    OSErr err;

    if (!gSpeechAvailable || index < 1 || index > gVoiceCount)
        return paramErr;

    err = GetIndVoice(index, &voiceSpec);
    if (err != noErr)
        return err;

    if (gSpeechChannel != nil)
    {
        StopSpeech(gSpeechChannel);
        DisposeSpeechChannel(gSpeechChannel);
        gSpeechChannel = nil;
    }

    err = NewSpeechChannel(&voiceSpec, &gSpeechChannel);
    if (err != noErr)
        return err;

    gCurrentVoiceIndex = index;
    gApp.settings.speech.voiceIndex = index;
    Speech_SetRatePercent(gApp.settings.speech.ratePercent);

    return noErr;
}

/*------------------------------------------------------------------------------
    Speech_GetRatePercent
------------------------------------------------------------------------------*/
short Speech_GetRatePercent(void)
{
    Fixed rate;
    long range;
    short percent;

    if (!gSpeechAvailable || gSpeechChannel == nil)
        return 50;

    GetSpeechRate(gSpeechChannel, &rate);

    range = kMaxRate - kMinRate;
    percent = (short)(((rate - kMinRate) * 100) / range);

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    return percent;
}

/*------------------------------------------------------------------------------
    Speech_SetRatePercent
------------------------------------------------------------------------------*/
OSErr Speech_SetRatePercent(short percent)
{
    Fixed rate;
    long range;

    if (!gSpeechAvailable || gSpeechChannel == nil)
        return paramErr;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    range = kMaxRate - kMinRate;
    rate = kMinRate + ((range * percent) / 100);

    gApp.settings.speech.ratePercent = percent;
    return SetSpeechRate(gSpeechChannel, rate);
}

/*------------------------------------------------------------------------------
    Speech_PreprocessText - Pronunciation substitutions
------------------------------------------------------------------------------*/
static long Speech_PreprocessText(const char *src, long srcLen, char *dst, long dstSize)
{
    const char *srcEnd = src + srcLen;
    char *dstStart = dst;
    char *dstEnd = dst + dstSize - 1;

    while (src < srcEnd && dst < dstEnd)
    {
        /* "MacinAI: " -> pronunciation + pause */
        if (srcEnd - src >= 9 && strncmp(src, "MacinAI: ", 9) == 0)
        {
            if (dstEnd - dst >= 28)
            {
                BlockMove("Mack in Ay Eye: [[slnc 400]]", dst, 28);
                dst += 28;
                src += 9;
            }
            else break;
        }
        /* "MacinAI" -> pronunciation only */
        else if (srcEnd - src >= 7 && strncmp(src, "MacinAI", 7) == 0)
        {
            if (dstEnd - dst >= 14)
            {
                BlockMove("Mack in Ay Eye", dst, 14);
                dst += 14;
                src += 7;
            }
            else break;
        }
        /* "You: " -> add pause */
        else if (srcEnd - src >= 5 && strncmp(src, "You: ", 5) == 0)
        {
            if (dstEnd - dst >= 17)
            {
                BlockMove("You: [[slnc 300]]", dst, 17);
                dst += 17;
                src += 5;
            }
            else break;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    return dst - dstStart;
}

/*------------------------------------------------------------------------------
    Speech_SpeakText
------------------------------------------------------------------------------*/
OSErr Speech_SpeakText(const char *text, long textLen)
{
    long processedLen;

    if (!gSpeechAvailable || gSpeechChannel == nil)
        return paramErr;

    if (!gApp.settings.speech.speechEnabled)
        return noErr;

    StopSpeech(gSpeechChannel);

    if (textLen > kSpeechBufferSize - 100)
        textLen = kSpeechBufferSize - 100;

    processedLen = Speech_PreprocessText(text, textLen, gSpeechBuffer, kSpeechBufferSize);

    if (processedLen <= 0)
        return noErr;

    return SpeakText(gSpeechChannel, (Ptr)gSpeechBuffer, processedLen);
}

/*------------------------------------------------------------------------------
    Speech_IsBusy
------------------------------------------------------------------------------*/
Boolean Speech_IsBusy(void)
{
    if (!gSpeechAvailable)
        return false;

    return SpeechBusy();
}

/*------------------------------------------------------------------------------
    Speech_Stop
------------------------------------------------------------------------------*/
OSErr Speech_Stop(void)
{
    if (!gSpeechAvailable || gSpeechChannel == nil)
        return paramErr;

    return StopSpeech(gSpeechChannel);
}

/*------------------------------------------------------------------------------
    Speech_FeedbackEx
------------------------------------------------------------------------------*/
static void Speech_FeedbackEx(const char *message, Boolean interrupt)
{
    long msgLen;

    if (!gSpeechAvailable || !gSpeechInitialized)
        return;

    if (!gApp.settings.speech.speechEnabled)
        return;

    if (!gApp.settings.speech.feedbackSounds)
        return;

    if (interrupt)
    {
        StopSpeech(gSpeechChannel);
        SpeakText(gSpeechChannel, (Ptr)message, strlen(message));
        return;
    }

    if (!SpeechBusy())
    {
        SpeakText(gSpeechChannel, (Ptr)message, strlen(message));
        return;
    }

    if (gFeedbackQueueCount >= kFeedbackQueueSize)
        return;

    msgLen = strlen(message);
    if (msgLen >= kFeedbackMsgSize)
        msgLen = kFeedbackMsgSize - 1;

    BlockMove(message, gFeedbackQueue[gFeedbackQueueTail], msgLen);
    gFeedbackQueue[gFeedbackQueueTail][msgLen] = '\0';

    gFeedbackQueueTail = (gFeedbackQueueTail + 1) % kFeedbackQueueSize;
    gFeedbackQueueCount++;
}

/*------------------------------------------------------------------------------
    Speech_Feedback
------------------------------------------------------------------------------*/
void Speech_Feedback(const char *message)
{
    Speech_FeedbackEx(message, false);
}

/*------------------------------------------------------------------------------
    Speech_FeedbackInterrupt
------------------------------------------------------------------------------*/
void Speech_FeedbackInterrupt(const char *message)
{
    Speech_FeedbackEx(message, true);
}

/*------------------------------------------------------------------------------
    Speech_ProcessQueue
------------------------------------------------------------------------------*/
void Speech_ProcessQueue(void)
{
    char *msg;

    if (!gSpeechAvailable || !gSpeechInitialized)
        return;

    if (!gApp.settings.speech.speechEnabled)
        return;

    if (!gApp.settings.speech.feedbackSounds)
        return;

    if (SpeechBusy() || gFeedbackQueueCount == 0)
        return;

    msg = gFeedbackQueue[gFeedbackQueueHead];
    gFeedbackQueueHead = (gFeedbackQueueHead + 1) % kFeedbackQueueSize;
    gFeedbackQueueCount--;

    SpeakText(gSpeechChannel, (Ptr)msg, strlen(msg));
}

/*------------------------------------------------------------------------------
    Speech_ApplySettings
------------------------------------------------------------------------------*/
void Speech_ApplySettings(void)
{
    if (!gSpeechAvailable)
        return;

    if (gApp.settings.speech.voiceIndex > 0 &&
        gApp.settings.speech.voiceIndex <= gVoiceCount)
    {
        Speech_SetVoice(gApp.settings.speech.voiceIndex);
    }

    Speech_SetRatePercent(gApp.settings.speech.ratePercent);
}
