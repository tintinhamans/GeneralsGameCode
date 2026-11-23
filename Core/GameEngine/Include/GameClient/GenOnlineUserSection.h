#pragma once

class GameWindow;

// The local player section in the top ruler strip of the online screens: a presence dot, the display name and the pending notifications. Clicking it opens a dropdown with the presence statuses and the friends under it.
void GenOnlineUserSectionShow(GameWindow *parent);
void GenOnlineUserSectionHide();
