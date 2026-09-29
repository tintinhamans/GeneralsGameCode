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

// FILE: PopupReplayActions.cpp //////////////////////////////////////////////
// See PopupReplayActions.h. Bodies moved out of PopupReplay.cpp's callbacks; no behavior change.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include "GameClient/GUI/GUICallbacks/Menus/PopupReplayActions.h"

#include "Common/LocalFileSystem.h"
#include "Common/Recorder.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MessageBox.h"
#include "GameClient/GUI/GUICallbacks/Menus/PopupReplayData.h"

#include <windows.h>

namespace PopupReplayActions
{

static PopupReplayData &data()
{
	return PopupReplayData::instance();
}

static void closeMenu()
{
	if( data().m_closePopup )
		data().m_closePopup();
}

static void reallySaveReplay();
static std::string replayPath;

// The overwrite confirmation, kept so the error box can take its modal state over.
static GameWindow *messageBoxWin = nullptr;

static void saveReplay( UnicodeString filename )
{
	AsciiString translated;
	if (filename == TheGameText->fetch("GUI:LastReplay"))
	{
		translated = TheRecorder->getLastReplayFileName();
	}
	else
	{
		translated.translate(filename);
	}

	AsciiString fullPath = TheRecorder->getReplayDir();
	fullPath.concat(translated);
	fullPath.concat(TheRecorder->getReplayExtention());

	replayPath = fullPath.str();
	messageBoxWin = nullptr;
	if (TheLocalFileSystem->doesFileExist(fullPath.str()))
	{
		messageBoxWin = MessageBoxOkCancel(TheGameText->fetch("GUI:OverwriteReplayTitle"), TheGameText->fetch("GUI:OverwriteReplay"), reallySaveReplay, nullptr);
	}
	else
	{
		reallySaveReplay();
	}
}

static void reallySaveReplay()
{
	AsciiString filename = replayPath.c_str();

	AsciiString oldFilename;
	oldFilename = TheRecorder->getReplayDir();
	oldFilename.concat(TheRecorder->getLastReplayFileName());
	oldFilename.concat(TheRecorder->getReplayExtention());

	if (oldFilename == filename)
		return;

	if (TheLocalFileSystem->doesFileExist(filename.str()))
	{
		if(DeleteFile(filename.str()) == 0)
		{
			wchar_t buffer[1024];
			FormatMessageW ( FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, buffer, ARRAY_SIZE(buffer), nullptr);
			UnicodeString errorStr;
			errorStr.set(buffer);
			errorStr.trim();
			if(messageBoxWin)
			{
				TheWindowManager->winUnsetModal(messageBoxWin);
				messageBoxWin = nullptr;
			}
			MessageBoxOk(TheGameText->fetch("GUI:Error"),errorStr, nullptr);

			data().refresh();
			return;
		}
	}

	// copy the replay to the right place
	if(CopyFile(oldFilename.str(),filename.str(), FALSE) == 0)
	{
		wchar_t buffer[1024];
		FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, buffer, ARRAY_SIZE(buffer), nullptr);
		UnicodeString errorStr;
		errorStr.set(buffer);
		errorStr.trim();
		if(messageBoxWin)
		{
			TheWindowManager->winUnsetModal(messageBoxWin);
			messageBoxWin = nullptr;
		}
		MessageBoxOk(TheGameText->fetch("GUI:Error"),errorStr, nullptr);
		return;
	}

	data().refresh();

	data().m_showSaved = TRUE;
	data().m_savedTime = timeGetTime();
	data().touch();
}

void open( void (*closePopup)() )
{
	data().m_closePopup = closePopup;
	data().open();
}

void select( Int row )
{
	data().select( row );
}

void setName( const UnicodeString &name )
{
	data().setName( name );
}

void save()
{
	// get the filename, and see if we are overwriting
	if( data().m_name.isEmpty() )
		return;

	saveReplay( data().m_name );
}

void back()
{
	// close the save/load menu
	closeMenu();
}

void update()
{
	PopupReplayData &d = data();
	if( d.m_savedTime == 0 )
		return;

	// the replay save confirmation popup is up
	// check to see if its time to take it down.
	if( (timeGetTime() - d.m_savedTime) >= PopupReplayData::SAVED_POPUP_DURATION )
	{
		d.m_showSaved = FALSE;
		d.touch();

		// close the save/load menu
		closeMenu();

		// reset the timer to 0 cause we have to.
		d.m_savedTime = 0;
	}
}

}
