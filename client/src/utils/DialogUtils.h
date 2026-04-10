/*------------------------------------------------------------------------------
    DialogUtils.h - Reusable dialog components

    Provides common dialogs used throughout MacinAI Local.

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

#ifndef DIALOGUTILS_H
#define DIALOGUTILS_H

#include <Types.h>
#include <Windows.h>

/* Prompt for user input */
Boolean PromptForInput(const char* promptText, char* valueOut);

/* Legacy wrapper */
Boolean PromptForConversationTitle(char* titleOut);

/* Show save prompt dialog */
/* Returns: 1=Save, 2=Don't Save, 3=Cancel */
short ShowSavePrompt(WindowPtr parentWindow);

/* Show simple message dialog */
void ShowMessageDialog(const char* message);

/* Show confirmation dialog (OK/Cancel) */
Boolean ShowConfirmDialog(const char* message);

/* Show MacinAI alert using ALRT 129 (OK/Cancel buttons) */
short ShowMacinAIAlert(const char* title, const char* message,
                       const char* okButton, const char* cancelButton);

/* Show MacinAI alert using specific ALRT resource ID */
short ShowMacinAIAlertWithID(short alertID, const char* title, const char* message);

/* Show MacinAI-branded confirmation with extra button */
short ShowMacinAIAlert3(const char* title, const char* message,
                        const char* okButton, const char* otherButton,
                        const char* cancelButton);

#endif /* DIALOGUTILS_H */
