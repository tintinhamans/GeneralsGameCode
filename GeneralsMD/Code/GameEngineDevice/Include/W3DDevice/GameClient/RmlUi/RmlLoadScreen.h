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

// FILE: RmlLoadScreen.h //////////////////////////////////////////////////////
// RmlUi view of the network load screens (Menus/MapTransferScreen.wnd,
// Menus/MultiplayerLoadScreen.wnd, Menus/GameSpyLoadScreen.wnd). The LoadScreen
// classes keep all the logic and write a LoadScreenData; this only mirrors that
// into a data model whenever its version changes. One instance per document,
// created by winCreateFromScript() through RmlUiScreenRegistry and closed when
// the LoadScreen destroys its placeholder window. RmlUiManager::update() calls
// tick(): load screens block the main loop, so nothing else would refresh them.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; }

//-------------------------------------------------------------------------------------------------
class RmlLoadScreen
{
public:
	enum Kind { KIND_MAP_TRANSFER, KIND_MULTIPLAYER, KIND_ONLINE, KIND_COUNT };

	static RmlLoadScreen &instance(Kind kind);
	static void tick(); ///< refreshes every open instance whose LoadScreenData changed

	void open();
	void close();
	bool isVisible() const;

	RmlLoadScreen(const char *document, const char *modelName) : m_documentPath(document), m_modelName(modelName) {}

private:
	void load(Rml::Context *context);
	void refresh();

	struct RowModel
	{
		Rml::String name;
		Rml::String side;
		Rml::String team;
		Rml::String winLoss;
		Rml::String disconnects;
		Rml::String status;
		Rml::String rankImage;
		Rml::String medalImage;
		Rml::String colorHex = "#FFFFFF";
		Rml::String progressStyle = "0%"; ///< "42%", bound via data-style-width
		bool used = false; ///< the array is always MAX_SLOTS long; unused rows are hidden
		bool showProgress = true;
		bool showStats = true;
		bool hasRank = false;
		bool hasMedal = false;
	};

	struct MarkerModel
	{
		Rml::String xStyle = "0%"; ///< "12.500%", bound via data-style-left
		Rml::String yStyle = "0%";
		Rml::String label;
		Rml::String colorHex = "#FFFFFF";
		bool used = false;
		bool hasLabel = false;
	};

	const char *m_documentPath;
	const char *m_modelName;
	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	unsigned int m_seenVersion = 0;

	Rml::Vector<RowModel> m_rows;
	Rml::Vector<MarkerModel> m_markers;
	Rml::String m_localName;
	Rml::String m_localFeatures;
	Rml::String m_localPortrait;
	Rml::String m_mapName;
	Rml::String m_currentFile;
	Rml::String m_timeout;
	bool m_hasPortrait = false;
	bool m_hasMap = false;
};

// Registry entry points (see RmlUiManager::init()).
void OpenRmlMapTransferScreen();
void CloseRmlMapTransferScreen();
void OpenRmlMultiplayerLoadScreen();
void CloseRmlMultiplayerLoadScreen();
void OpenRmlOnlineLoadScreen();
void CloseRmlOnlineLoadScreen();
