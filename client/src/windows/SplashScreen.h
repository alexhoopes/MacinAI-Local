/*------------------------------------------------------------------------------
    SplashScreen.h - Splash Screen with Model Loading

    Shows welcome screen while the inference engine loads the model.
    Replaces MacinAI's network connection splash with model loading progress.

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

#ifndef SPLASHSCREEN_H
#define SPLASHSCREEN_H

#include <Types.h>
#include <Windows.h>
#include "AppGlobals.h"

/* State transition function from MacinAI.c */
extern void TransitionToState(AppState newState);

/* Initialize and show the splash screen */
void SplashScreen_Show(void);

/* Handle events for the splash screen */
Boolean SplashScreen_HandleEvent(EventRecord *event);

/* Draw the splash screen content */
void SplashScreen_Draw(WindowPtr window);

/* Clean up splash screen resources */
void SplashScreen_Close(void);

#endif /* SPLASHSCREEN_H */
