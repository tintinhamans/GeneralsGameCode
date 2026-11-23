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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DGameEngine.cpp ////////////////////////////////////////////////////////////////////////
// Author: Colin Day, April 2001
// Description:
//   Implementation of the Win32 game engine, this is the highest level of
//   the game application, it creates all the devices we will use for the game
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <windows.h>

#include "Win32Device/Common/Win32GameEngine.h"
#if defined(RTS_HAS_MODERN_MEDIA)
#include "OpenALAudioDevice/MediaBackend.h"
#include "OpenALAudioDevice/OpenALAudioManager.h"
#endif
#if defined(RTS_HAS_LEGACY_MEDIA)
#include "MilesAudioDevice/MilesAudioManager.h"
#endif
#include "Common/PerfTimer.h"
#include "GameClient/GameText.h"
#include "Common/FramePacer.h"
#include "Common/MessageStream.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
#include "GameClient/SelectionXlat.h"
#include "GameClient/View.h"

#include "GameNetwork/LANAPICallbacks.h"
#include "../GenOnlineInit.h"

AudioManager *Win32GameEngine::createAudioManager(Bool dummy)
{
#if defined(RTS_HAS_MODERN_MEDIA)
	if (isOpenALMediaSelected())
		return NEW OpenALAudioManager(dummy);
#endif
#if defined(RTS_HAS_LEGACY_MEDIA)
	if (dummy)
		return NEW MilesAudioManagerDummy;
	return NEW MilesAudioManager;
#else
	return NEW OpenALAudioManager(dummy);
#endif
}

extern DWORD TheMessageTime;

//-------------------------------------------------------------------------------------------------
/** Constructor for Win32GameEngine */
//-------------------------------------------------------------------------------------------------
Win32GameEngine::Win32GameEngine()
{
	m_isAtFrameBoundary = FALSE;
	m_isInModalStep = FALSE;

	// Stop blue screen
	m_previousErrorMode = SetErrorMode( SEM_FAILCRITICALERRORS );
}

//-------------------------------------------------------------------------------------------------
/** Destructor for Win32GameEngine */
//-------------------------------------------------------------------------------------------------
Win32GameEngine::~Win32GameEngine()
{
	// restore it (this isn't really necessary, but feels good.)
	SetErrorMode( m_previousErrorMode );
}


//-------------------------------------------------------------------------------------------------
/** Initialize the game engine */
//-------------------------------------------------------------------------------------------------
void Win32GameEngine::init()
{

	// extending functionality
	GameEngine::init();

}

//-------------------------------------------------------------------------------------------------
/** Reset the system */
//-------------------------------------------------------------------------------------------------
void Win32GameEngine::reset()
{

	// extending functionality
	GameEngine::reset();

}

//-------------------------------------------------------------------------------------------------
Bool Win32GameEngine::shouldKeepRunningInBackground()
{
	if (TheNetwork != nullptr || TheLAN != nullptr)
	{
		return TRUE;
	}

	if (TheGameLogic != nullptr)
	{
		if (TheGameLogic->isInMultiplayerGame())
		{
			return TRUE;
		}

		// a single player, skirmish or replay game pauses, also when logged in online
		if (TheGameLogic->isInGame() && !TheGameLogic->isInShellGame())
		{
			return FALSE;
		}
	}

	if (GenOnlineServicesManager::GetInstance() != nullptr)
	{
		return TRUE;
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Runs one frame from a Windows modal loop, only at the end of the frame message service. */
//-------------------------------------------------------------------------------------------------
Bool Win32GameEngine::stepFrameFromModalLoop()
{
	if (m_isInModalStep || !m_isAtFrameBoundary || getQuitting() || !shouldKeepRunningInBackground())
	{
		return FALSE;
	}

	m_isInModalStep = TRUE;
	update();
	TheFramePacer->update();
	m_isInModalStep = FALSE;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Drops buttons, keys, scrolling and area selection that a Windows modal loop leaves stale. */
//-------------------------------------------------------------------------------------------------
void Win32GameEngine::releaseInputForModalLoop()
{
	if (TheInGameUI != nullptr)
	{
		TheInGameUI->setScrolling(FALSE);
		TheInGameUI->setSelecting(FALSE);
		TheInGameUI->endAreaSelectHint(nullptr);
	}

	if (TheSelectionTranslator != nullptr)
	{
		TheSelectionTranslator->setLeftMouseButton(FALSE);
		TheSelectionTranslator->setDragSelecting(FALSE);
	}

	if (TheTacticalView != nullptr)
	{
		TheTacticalView->setMouseLock(FALSE);
	}

	if (TheMouse != nullptr)
	{
		TheMouse->UPDATE();
		TheMouse->resetButtons();
	}

	if (TheKeyboard != nullptr && TheMessageStream != nullptr)
	{
		TheKeyboard->UPDATE();
		TheKeyboard->resetKeys();
	}
}

//-------------------------------------------------------------------------------------------------
/** Update the game engine by updating the GameClient and
	* GameLogic singletons. */
//-------------------------------------------------------------------------------------------------
void Win32GameEngine::update()
{


	// call the engine normal update
	GameEngine::update();

	if (m_isInModalStep)
	{
		return;
	}

	extern HWND ApplicationHWnd;
	if (ApplicationHWnd && ::IsIconic(ApplicationHWnd)) {
		while (ApplicationHWnd && ::IsIconic(ApplicationHWnd)) {
			// We are alt-tabbed out here.  Sleep a bit, & process windows
			// so that we can become un-alt-tabbed out.
			Sleep(5);
			serviceWindowsOS();

			if (TheLAN != nullptr) {
				// BGC - need to update TheLAN so we can process and respond to other
				// people's messages who may not be alt-tabbed out like we are.
				TheLAN->setIsActive(isActive());
				TheLAN->update();
			}

			if (GenOnlineServicesManager::GetInstance() != nullptr)
			{
				GenOnlineServicesManager::GetInstance()->Tick();
			}

			// If we are running a multiplayer game, keep running the logic.
			// There is code in the client to skip client redraw if we are
			// iconic.  jba.
			// GO_CHANGE: If we have an active network session, keep running to prevent disconnecting us from
			// other players during lobby and loading screen where isInMultiplayerGame() returns false
			if (TheGameEngine->getQuitting() || shouldKeepRunningInBackground()) {
				break; // keep running.
			}
		}

    // When we are alt-tabbed out... the MilesAudioManager seems to go into a coma sometimes
    // and not regain focus properly when we come back. This seems to wake it up nicely.
    AudioAffect aa = (AudioAffect)0x10;
		TheAudio->setVolume(TheAudio->getVolume( aa ), aa );

	}

	// allow windows to perform regular windows maintenance stuff like msgs
	m_isAtFrameBoundary = TRUE;
	serviceWindowsOS();
	m_isAtFrameBoundary = FALSE;

}

//-------------------------------------------------------------------------------------------------
/** This function may be called from within this application to let
  * Microsoft Windows do its message processing and dispatching.  Presumably
	* we would call this at least once each time around the game loop to keep
	* Windows services from backing up */
//-------------------------------------------------------------------------------------------------
void Win32GameEngine::serviceWindowsOS()
{
	MSG msg;
  Int returnValue;

	// The modal loop that runs the frame pumps the messages itself.
	if (m_isInModalStep)
	{
		return;
	}

	//
	// see if we have any messages to process, a nullptr window handle tells the
	// OS to look at the main window associated with the calling thread, us!
	//
	// W variants: an A pump would convert the Unicode window's WM_CHAR back to the code page
	while( PeekMessageW( &msg, nullptr, 0, 0, PM_NOREMOVE ) )
	{

		// get the message
		returnValue = GetMessageW( &msg, nullptr, 0, 0 );

		// this is one possible way to check for quitting conditions as a message
		// of WM_QUIT will cause GetMessage() to return 0
/*
		if( returnValue == 0 )
		{

			setQuitting( true );
			break;

		}
*/

		TheMessageTime = msg.time;
		// translate and dispatch the message
		TranslateMessage( &msg );
		DispatchMessageW( &msg );
		TheMessageTime = 0;

	}

}

//-------------------------------------------------------------------------------------------------
void Win32GameEngine::setBusyState(Bool busyDisplay, Bool busySystem)
{
	EXECUTION_STATE state = ES_CONTINUOUS;
	state |= busyDisplay ? ES_DISPLAY_REQUIRED : 0;
	state |= busySystem ? ES_SYSTEM_REQUIRED : 0;

	::SetThreadExecutionState(state);
}

//-------------------------------------------------------------------------------------------------
static void RTSFlagsToOSFlags(UnsignedInt buttonFlags, UnsignedInt otherFlags, UnsignedInt& outWindowsFlags)
{
	outWindowsFlags = 0;

	if (BitIsSet(buttonFlags, OSDBT_OK)) {
		outWindowsFlags |= MB_OK;
	}

	if (BitIsSet(buttonFlags, OSDBT_CANCEL)) {
		outWindowsFlags |= MB_OKCANCEL;
	}

	//-----------------------------------------------------------------------------------------------
	if (BitIsSet(otherFlags, OSDOF_SYSTEMMODAL)) {
		outWindowsFlags |= MB_SYSTEMMODAL;
	}

	if (BitIsSet(otherFlags, OSDOF_APPLICATIONMODAL)) {
		outWindowsFlags |= MB_APPLMODAL;
	}

	if (BitIsSet(otherFlags, OSDOF_TASKMODAL)) {
		outWindowsFlags |= MB_TASKMODAL;
	}

	if (BitIsSet(otherFlags, OSDOF_EXCLAMATIONICON)) {
		outWindowsFlags |= MB_ICONEXCLAMATION;
	}

	if (BitIsSet(otherFlags, OSDOF_INFORMATIONICON)) {
		outWindowsFlags |= MB_ICONINFORMATION;
	}

	if (BitIsSet(otherFlags, OSDOF_ERRORICON)) {
		outWindowsFlags |= MB_ICONERROR;
	}

	if (BitIsSet(otherFlags, OSDOF_STOPICON)) {
		outWindowsFlags |= MB_ICONSTOP;
	}

}

//-------------------------------------------------------------------------------------------------
OSDisplayButtonType Win32GameEngine::displayWarningBox(AsciiString p, AsciiString m, UnsignedInt buttonFlags, UnsignedInt otherFlags)
{
	if (!TheGameText) {
		return OSDBT_ERROR;
	}

	UnicodeString promptStr = TheGameText->fetch(p);
	UnicodeString mesgStr = TheGameText->fetch(m);

	UnsignedInt windowsOptionsFlags = 0;
	RTSFlagsToOSFlags(buttonFlags, otherFlags, windowsOptionsFlags);

	// @todo Make this return more than just ok/cancel - jkmcd
	// (we need a function to translate back the other way.)
	const Int returnResult = ::MessageBoxW(nullptr, mesgStr.str(), promptStr.str(), windowsOptionsFlags);
	if (returnResult == IDOK) {
		return OSDBT_OK;
	}

	return OSDBT_CANCEL;
}
