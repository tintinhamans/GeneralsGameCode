#pragma once

class GameWindow;

// The details panel of the selected game in the GenOnline lobby sidebar. It replaces the room player list while a game is selected.
void GenOnlineLobbyGameDetailsInit(GameWindow *parent);
void GenOnlineLobbyGameDetailsShutdown();
// Reads the selection of the game list again, so it is safe to call after any selection or list change.
void GenOnlineLobbyGameDetailsUpdate();
