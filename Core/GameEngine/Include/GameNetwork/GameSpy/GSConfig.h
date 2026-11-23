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

// FILE: GSConfig.h ///////////////////////////////////////////////////////////
// TheSuperHackers @refactor GenMP Phase 1: This forwarding header keeps the old include path of IOnlineConfig.h valid.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/Online/IOnlineConfig.h"

// TheSuperHackers @refactor GenMP Phase 1: The alias keeps code that names GameSpyConfigInterface compiling.
typedef IOnlineConfig GameSpyConfigInterface;
