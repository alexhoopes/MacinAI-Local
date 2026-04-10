/*------------------------------------------------------------------------------
    SettingsDialog.h - Settings Panel

    Simplified settings for MacinAI Local:
    - Speech settings (voice, rate, auto-speak)
    - Model settings (model file, vocab file)
    - System info display

    Removed from MacinAI relay version:
    - Server/connection settings
    - AI system catalog settings
    - Client identity settings
    - Debug mode settings

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

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <Types.h>

/* Show settings dialog (modal) */
void SettingsDialog_Show(void);

#endif /* SETTINGSDIALOG_H */
