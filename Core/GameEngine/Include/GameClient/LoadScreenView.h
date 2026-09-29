/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

// FILE: LoadScreenView.h /////////////////////////////////////////////////////
// Legacy .wnd presentation of LoadScreenData. A LoadScreen owns one only when the registry
// does not route its screen to RmlUi; RmlLoadScreen is the other reader of the same data.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/GameWindow.h"
#include "GameClient/LoadScreenData.h"

class GameInfo;

class LoadScreenView
{
public:
	virtual ~LoadScreenView() {}

	virtual void init( GameWindow *root, GameInfo *game, const LoadScreenData &data ) = 0; ///< bind the gadgets and apply everything
	virtual void update( const LoadScreenData &data ) = 0; ///< apply what changes while loading
	virtual void reset() = 0; ///< the window is going away, drop the gadget pointers
};

// Title art, legal line and progress bar of the shell load.
class ShellLoadScreenView : public LoadScreenView
{
public:
	ShellLoadScreenView();

	virtual void init( GameWindow *root, GameInfo *game, const LoadScreenData &data ) override;
	virtual void update( const LoadScreenData &data ) override;
	virtual void reset() override;

private:
	GameWindow *m_progressBar;
};
