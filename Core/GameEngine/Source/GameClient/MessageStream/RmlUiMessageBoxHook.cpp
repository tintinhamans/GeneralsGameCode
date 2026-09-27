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

#include "PreRTS.h"
#include "GameClient/RmlUiMessageBoxHook.h"

namespace
{
	RmlUiMessageBoxFunc s_handler = nullptr;
	RmlUiMessageBoxCloseFunc s_closeHandler = nullptr;
}

void RmlUiMessageBoxHook::setHandler(RmlUiMessageBoxFunc handler)
{
	s_handler = handler;
}

void RmlUiMessageBoxHook::setCloseHandler(RmlUiMessageBoxCloseFunc closeHandler)
{
	s_closeHandler = closeHandler;
}

bool RmlUiMessageBoxHook::isAvailable()
{
	return s_handler != nullptr;
}

void RmlUiMessageBoxHook::show(UnsignedShort buttonFlags, const UnicodeString &titleString, const UnicodeString &bodyString,
	GameWinMsgBoxFunc yesCallback, GameWinMsgBoxFunc noCallback,
	GameWinMsgBoxFunc okCallback, GameWinMsgBoxFunc cancelCallback,
	Bool useLogo)
{
	if (s_handler)
		s_handler(buttonFlags, titleString, bodyString, yesCallback, noCallback, okCallback, cancelCallback, useLogo);
}

void RmlUiMessageBoxHook::closeCurrent()
{
	if (s_closeHandler)
		s_closeHandler();
}
