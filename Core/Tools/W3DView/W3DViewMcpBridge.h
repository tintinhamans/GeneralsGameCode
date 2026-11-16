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

#pragma once

#include <windows.h>

// Answers read-only model queries from the local W3DView MCP server (tools/w3dview_mcp).
namespace W3DViewMcpBridge
{

const ULONG_PTR REQUEST_MAGIC = 0x5733444D; // "W3DM"
const wchar_t WINDOW_PROPERTY[] = L"GeneralsW3DViewMcp";
const ULONG_PTR BRIDGE_VERSION = 1;

void Attach(HWND window);
void Detach(HWND window);
BOOL IsEnabled(HWND window);
BOOL IsRequest(const COPYDATASTRUCT *copy_data);

// The copied data is the wide path of a request file. The answer goes to that path plus ".response".
BOOL HandleRequest(const COPYDATASTRUCT *copy_data);

} // namespace W3DViewMcpBridge
