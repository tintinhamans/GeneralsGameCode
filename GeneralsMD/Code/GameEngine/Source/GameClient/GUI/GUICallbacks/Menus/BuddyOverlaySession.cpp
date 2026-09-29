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

#include "GameClient/GUI/GUICallbacks/Menus/BuddyOverlaySession.h"

#include "Common/AsciiString.h"
#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

namespace BuddyOverlaySignals
{
	Signal3<int64_t, int64_t, const UnicodeString &> &chatMessage() { static Signal3<int64_t, int64_t, const UnicodeString &> s; return s; }
	Signal0 &requestArrived() { static Signal0 s; return s; }
	Signal2<bool, bool> &rosterNeedsRefresh() { static Signal2<bool, bool> s; return s; }
	Signal1<int> &notificationCountChanged() { static Signal1<int> s; return s; }
}

// Function-local statics: the .wnd toast presenter connects from another file's static initializer.
namespace BuddyToastSignals
{
	Signal1<const UnicodeString &> &shown() { static Signal1<const UnicodeString &> s; return s; }
	Signal0 &dismissed() { static Signal0 s; return s; }
}

namespace BuddyOverlaySession
{

namespace
{
	// Same value as WOLBuddyOverlay.cpp's GENERALS_ONLINE NOTIFICATION_EXPIRES (107-108); duplicated
	// rather than shared since the legacy (non-GO) NOTIFICATION_EXPIRES=3000 branch stays entirely
	// inside WOLBuddyOverlay.cpp's own dead-code path and never touches this session.
	enum { NOTIFICATION_EXPIRES = 5000 };

	UnsignedInt s_noticeExpires = 0;
	bool s_toastActive = false;
}

//-------------------------------------------------------------------------------------------------
void enter()
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if ( pSocialInterface == nullptr )
		return;

	pSocialInterface->RegisterForRealtimeServiceUpdates();
	pSocialInterface->ClearGlobalNotificatations(); // we opened, clear pending global notifications

	pSocialInterface->RegisterForCallback_NewFriendRequest( []( std::string strDisplayName )
		{
			BuddyOverlaySignals::requestArrived().emit();
			BuddyOverlaySignals::rosterNeedsRefresh().emit( false, false );
		} );

	pSocialInterface->RegisterForCallback_OnChatMessage( []( int64_t source_user_id, int64_t target_user_id, UnicodeString unicodeStr )
		{
			BuddyOverlaySignals::chatMessage().emit( source_user_id, target_user_id, unicodeStr );
			BuddyOverlaySignals::rosterNeedsRefresh().emit( true, true );
		} );

	pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged( []( int newNumNotifications )
		{
			BuddyOverlaySignals::notificationCountChanged().emit( newNumNotifications );
		} );
}

//-------------------------------------------------------------------------------------------------
void leave()
{
	NGMP_OnlineServices_SocialInterface *pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if ( pSocialInterface != nullptr )
		pSocialInterface->DeregisterForRealtimeServiceUpdates();
}

//-------------------------------------------------------------------------------------------------
void showToast( const AsciiString &nick, UnicodeString message, bool bPlaySound )
{
	if ( nick.isNotEmpty() )
		message.format( message, nick.str() );

	s_noticeExpires = timeGetTime() + NOTIFICATION_EXPIRES;
	s_toastActive = true;

	if ( TheAudio && bPlaySound )
	{
		AudioEventRTS buttonClick( "GUICommunicatorIncoming" );
		TheAudio->addAudioEvent( &buttonClick );
	}

	BuddyToastSignals::shown().emit( message );
}

//-------------------------------------------------------------------------------------------------
void dismissToast()
{
	if ( !s_toastActive )
		return;

	s_toastActive = false;
	s_noticeExpires = 0;

	BuddyToastSignals::dismissed().emit();
}

//-------------------------------------------------------------------------------------------------
void tickToast()
{
	if ( s_toastActive && timeGetTime() > s_noticeExpires )
		dismissToast();
}

} // namespace BuddyOverlaySession
