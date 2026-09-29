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

// FILE: ReplayMenuActions.cpp ///////////////////////////////////////////////
// See ReplayMenuActions.h. Bodies moved out of ReplayMenu.cpp's callbacks; no behavior change.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/ReplayMenuActions.h"

#include "Common/GameEngine.h"
#include "Common/Recorder.h"
#include "GameClient/GameText.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Shell.h"
#include "GameClient/GUI/GUICallbacks/Menus/ReplayMenuData.h"

#include <windows.h>
#include <shlobj.h>

namespace ReplayMenuActions
{

static ReplayMenuData &data()
{
	return ReplayMenuData::instance();
}

static void noFileSelected()
{
	MessageBoxOk(TheGameText->fetch("GUI:NoFileSelected"),TheGameText->fetch("GUI:PleaseSelectAFile"), nullptr);
}

static void deleteReplayFlag()
{
	data().m_deletePending = TRUE;
}

static void copyReplayFlag()
{
	data().m_copyPending = TRUE;
}

static void handleReplayLoadFailure()
{
	UnicodeString title = TheGameText->FETCH_OR_SUBSTITUTE("GUI:ReplayLoadFailedTitle", L"REPLAY CANNOT BE LOADED");
	UnicodeString body = TheGameText->FETCH_OR_SUBSTITUTE("GUI:ReplayLoadFailed", L"The replay file could not be opened or is invalid.");

	MessageBoxOk(title, body, nullptr);

	data().refresh();
}

static void showReplayMapNotFound()
{
	UnicodeString title = TheGameText->FETCH_OR_SUBSTITUTE("GUI:ReplayMapNotFoundTitle", L"MAP NOT FOUND");
	UnicodeString body = TheGameText->FETCH_OR_SUBSTITUTE("GUI:ReplayMapNotFound", L"This replay cannot be loaded because the map was not found on this device.");

	MessageBoxOk(title, body, nullptr);
}

// Starts the playback, closing the menu, or brings up the failure box.
static void startPlayback(const AsciiString &filename)
{
	if(TheRecorder->playbackFile(filename))
	{
		if(data().m_closeScreen)
			data().m_closeScreen();
	}
	else
	{
		handleReplayLoadFailure();
	}
}

static void reallyLoadReplay()
{
	const ReplayRow *row = data().selectedRow();
	if(row == nullptr)
	{
		noFileSelected();
		return;
	}

	const AsciiString asciiFilename = row->m_fileName;

	// TheSuperHackers @bugfix bobtista 25/07/2026 Re-validate the replay before starting playback.
	// The user can delete the file while the version mismatch prompt is open, in which case the
	// listbox entry is stale. Prompts the same message box as loadReplay and refreshes the list.
	RecorderClass::ReplayHeader header;
	ReplayGameInfo info;
	const MapMetaData *mapData;

	if(!ReplayList::readMapInfo(asciiFilename, header, info, mapData))
	{
		handleReplayLoadFailure();
		return;
	}

	if(mapData == nullptr)
	{
		showReplayMapNotFound();
		return;
	}

	startPlayback(asciiFilename);
}

static void loadReplay(const AsciiString &asciiFilename)
{
	RecorderClass::ReplayHeader header;
	ReplayGameInfo info;
	const MapMetaData *mapData;

	if(!ReplayList::readMapInfo(asciiFilename, header, info, mapData))
	{
		// TheSuperHackers @bugfix Prompts a message box when the replay was deleted by the user while the Replay Menu was opened.

		handleReplayLoadFailure();
	}
	else if(mapData == nullptr)
	{
		// TheSuperHackers @bugfix Prompts a message box when the map used by the replay was not found.

		showReplayMapNotFound();
	}
	else if(!TheRecorder->replayMatchesGameVersion(header))
	{
		// Pressing OK loads the replay.

		MessageBoxOkCancel(TheGameText->fetch("GUI:OlderReplayVersionTitle"), TheGameText->fetch("GUI:OlderReplayVersion"), reallyLoadReplay, nullptr);
	}
	else
	{
		// TheSuperHackers @bugfix bobtista 25/07/2026 Keep the Replay Menu open when the playback
		// could not be started, for example when the replay was deleted after it was validated above.
		startPlayback(asciiFilename);
	}
}

static void deleteReplay()
{
	data().m_deletePending = FALSE;
	const ReplayRow *row = data().selectedRow();
	if(row == nullptr)
	{
		noFileSelected();
		return;
	}
	AsciiString filename, translate;
	filename = TheRecorder->getReplayDir();
	translate = row->m_fileName;
	filename.concat(translate);
	if(DeleteFile(filename.str()) == 0)
	{
		char buffer[1024];
		FormatMessage ( FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, buffer, sizeof(buffer), nullptr);
		UnicodeString errorStr;
		translate.set(buffer);
		errorStr.translate(translate);
		MessageBoxOk(TheGameText->fetch("GUI:Error"),errorStr, nullptr);
	}
	data().refresh();
}

static void copyReplay()
{
	data().m_copyPending = FALSE;
	const ReplayRow *row = data().selectedRow();
	if(row == nullptr)
	{
		noFileSelected();
		return;
	}
	AsciiString filename, translate;
	filename = TheRecorder->getReplayDir();
	translate = row->m_fileName;
	filename.concat(translate);

	char path[1024];
	LPITEMIDLIST pidl;
	SHGetSpecialFolderLocation(nullptr, CSIDL_DESKTOPDIRECTORY, &pidl);
	SHGetPathFromIDList(pidl,path);
	AsciiString newFilename;
	newFilename.set(path);
	newFilename.concat("\\");
	newFilename.concat(translate);
	if(CopyFile(filename.str(),newFilename.str(), FALSE) == 0)
	{
		wchar_t buffer[1024];
		FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, buffer, ARRAY_SIZE(buffer), nullptr);
		UnicodeString errorStr;
		errorStr.set(buffer);
		errorStr.trim();
		MessageBoxOk(TheGameText->fetch("GUI:Error"),errorStr, nullptr);
	}
}

void open( void (*closeScreen)() )
{
	TheShell->showShellMap(TRUE);

	data().m_closeScreen = closeScreen;
	data().open();
}

void select( Int row )
{
	data().select( row );
}

void activate( Int row )
{
	select( row );

	const ReplayRow *selected = data().selectedRow();
	if( selected )
		loadReplay( selected->m_fileName );
}

void load()
{
	const ReplayRow *row = data().selectedRow();
	if( row == nullptr )
	{
		noFileSelected();
		return;
	}

	loadReplay( row->m_fileName );
}

void remove()
{
	if( data().selectedRow() == nullptr )
	{
		noFileSelected();
		return;
	}
	MessageBoxYesNo(TheGameText->fetch("GUI:DeleteFile"), TheGameText->fetch("GUI:AreYouSureDelete"), deleteReplayFlag, nullptr);
}

void copy()
{
	if( data().selectedRow() == nullptr )
	{
		noFileSelected();
		return;
	}
	MessageBoxYesNo(TheGameText->fetch("GUI:CopyReplay"), TheGameText->fetch("GUI:AreYouSureCopy"), copyReplayFlag, nullptr);
}

void back()
{
	// thou art directed to return to thy known solar system immediately!
	TheShell->pop();
}

void update()
{
	// We'll only be successful if we've requested to
	if( data().m_copyPending )
		copyReplay();
	if( data().m_deletePending )
		deleteReplay();
}

}
