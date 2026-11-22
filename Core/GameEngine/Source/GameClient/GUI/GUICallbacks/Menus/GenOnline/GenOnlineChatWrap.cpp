#include "PreRTS.h"

#include "GameClient/GenOnlineChatWrap.h"

#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"

namespace
{

Bool isBreakChar(WideChar c)
{
	return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r';
}

// GameWindowManager::winGetTextSize is an empty stub that reports 0, so the width comes from a display string.
Int textWidth(DisplayString *measure, const UnicodeString &text)
{
	Int width = 0;
	measure->setText(text);
	measure->getSize(&width, nullptr);
	return width;
}

}

UnicodeString GenOnlineBreakLongWords(GameWindow *list, const UnicodeString &text)
{
	ListboxData *data = list != nullptr ? (ListboxData *)list->winGetUserData() : nullptr;
	if (data == nullptr || data->columns < 1)
	{
		return text;
	}

	const Int limit = data->columnWidth[0] - GameWindowManager::winScaleLayoutSize(TEXT_WIDTH_OFFSET) - GameWindowManager::winScaleLayoutSize(2);
	if (limit <= 0)
	{
		return text;
	}

	DisplayString *measure = TheDisplayStringManager->newDisplayString();
	measure->setFont(list->winGetFont());
	UnicodeString result;
	UnicodeString piece;
	const Int length = text.getLength();
	Int i = 0;
	while (i < length)
	{
		if (isBreakChar(text.getCharAt(i)))
		{
			result.concat(text.getCharAt(i));
			++i;
			continue;
		}

		Int end = i;
		while (end < length && !isBreakChar(text.getCharAt(end)))
		{
			++end;
		}

		piece.clear();
		for (Int j = i; j < end; ++j)
		{
			piece.concat(text.getCharAt(j));
			if (piece.getLength() > 1 && textWidth(measure, piece) > limit)
			{
				piece.removeLastChar();
				result.concat(piece);
				result.concat(L' ');
				piece.clear();
				piece.concat(text.getCharAt(j));
			}
		}
		result.concat(piece);
		i = end;
	}
	TheDisplayStringManager->freeDisplayString(measure);
	return result;
}
