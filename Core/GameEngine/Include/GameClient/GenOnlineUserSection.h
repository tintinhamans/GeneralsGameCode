#pragma once

class GameWindow;

// The local player section in the top ruler strip of the online screens: a presence dot and the display name, which open a dropdown with the presence statuses, the friends online, the profile and sign out. The communicator button left of it shows the unread conversations and friend requests.
void GenOnlineUserSectionShow(GameWindow *parent);
void GenOnlineUserSectionHide();
