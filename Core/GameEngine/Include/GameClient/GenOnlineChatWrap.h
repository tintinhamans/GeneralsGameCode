#pragma once

#include "Common/UnicodeString.h"

class GameWindow;

// The engine wraps list text only at spaces, so a word wider than the list column is cut into pieces that fit.
UnicodeString GenOnlineBreakLongWords(GameWindow *list, const UnicodeString &text);
