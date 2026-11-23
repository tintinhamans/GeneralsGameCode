/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

// FILE: OnlineProviderType.h /////////////////////////////////////////////////
// GenMP online provider abstraction.
// Description: Identifies which online-service provider the client talks to.
//              GenOnline builds also register GenOnline and select it by default.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"

// Append new providers before ONLINE_PROVIDER_COUNT to keep configured values stable.
enum OnlineProviderType CPP_11(: Int)
{
	ONLINE_PROVIDER_GAMESPY = 0,
	ONLINE_PROVIDER_GENONLINE = 1,

	ONLINE_PROVIDER_COUNT
};
