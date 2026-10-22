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

// FILE: GenOnlineNetworkOverlay.h ////////////////////////////////////////////
// Description: The GenOnline network overlay with the connection rating of
//              every player in the match.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"

class DisplayString;

// Draws the overlay in the given mode with the font of the given string. Mode 1 is the player view and mode 2 is the dev view.
void GenOnlineDrawNetworkOverlay(Int mode, DisplayString *fontSource, Int gameTimeDropColor);

// Frees the display strings the overlay keeps between frames.
void GenOnlineReleaseNetworkOverlay();
