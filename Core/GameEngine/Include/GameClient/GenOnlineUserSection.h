#pragma once

class GameWindow;

// The dock in the bottom right of the ruler on the online screens: the communicator button with the unread conversations and friend requests, a presence dot and the display name that open an upward dropdown with the presence statuses, the friends online, the profile and sign out, with a tab for every open private conversation left of the communicator button.
void GenOnlineUserSectionShow(GameWindow *parent);
void GenOnlineUserSectionHide();

// Screen rectangle left of the communicator button that is kept free for the conversation tabs, valid once the dock has drawn.
void GenOnlineDockGetChatArea(int *left, int *top, int *right, int *bottom);
