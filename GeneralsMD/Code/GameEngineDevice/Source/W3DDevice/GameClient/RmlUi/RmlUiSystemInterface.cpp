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

#include "W3DDevice/GameClient/RmlUi/RmlUiSystemInterface.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiIme.h"

RmlUiSystemInterface::RmlUiSystemInterface()
{
	m_startTickMs = ::GetTickCount();
}

double RmlUiSystemInterface::GetElapsedTime()
{
	return (double)(::GetTickCount() - m_startTickMs) / 1000.0;
}

bool RmlUiSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String &message)
{
	const char *prefix = "INFO";
	switch (type)
	{
		case Rml::Log::LT_ERROR: prefix = "ERROR"; break;
		case Rml::Log::LT_ASSERT: prefix = "ASSERT"; break;
		case Rml::Log::LT_WARNING: prefix = "WARNING"; break;
		case Rml::Log::LT_DEBUG: prefix = "DEBUG"; break;
		default: break;
	}

	char buffer[1024];
	_snprintf(buffer, sizeof(buffer), "[RmlUi %s] %s\n", prefix, message.c_str());
	buffer[sizeof(buffer) - 1] = '\0';
	::OutputDebugStringA(buffer);

	return true; // continue execution (don't break into debugger)
}

void RmlUiSystemInterface::ActivateKeyboard(Rml::Vector2f caret_position, float line_height)
{
	if (m_ime)
		m_ime->setCaret(caret_position, line_height);
}
