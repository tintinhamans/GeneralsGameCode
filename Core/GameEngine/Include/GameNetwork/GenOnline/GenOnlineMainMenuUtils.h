/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: GenOnlineMainMenuUtils.h /////////////////////////////////////////////
// Description: The Online path of the main menu for the GenOnline provider:
//              the update check, the patch download and the sign-in.
///////////////////////////////////////////////////////////////////////////////

#pragma once

void GenOnlineHTTPThinkWrapper();
void GenOnlineStopAsyncDNSCheck();
void GenOnlineStartPatchCheck();
void GenOnlineCancelPatchCheckCallback();
// TheSuperHackers @feature arcticdolphin 09/11/2026 Ends an Online attempt: closes the sign-in dialog, tears the online services down and shows the main menu again.
void GenOnlineCancelPatchCheckCallbackAndReopenDropdown();
void GenOnlineStartDownloadingPatches();
void GenOnlineCheckOverallStats();
void GenOnlineCheckNumPlayersOnline();
