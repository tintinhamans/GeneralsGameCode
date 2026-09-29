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

#include "W3DDevice/GameClient/RmlUi/RmlCreditsScreen.h"

#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/GameAudio.h"
#include "Common/UnicodeUtf8.h"
#include "GameClient/Color.h"
#include "GameClient/Credits.h"
#include "GameClient/Display.h"
#include "GameClient/Shell.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <windows.h>

#include <cstdio>
#include <string>

//-------------------------------------------------------------------------------------------------
// HTML-escapes the handful of characters that would otherwise break SetInnerRML's parse; credit
// names/titles are plain text, not markup.
static Rml::String escapeRml(const Rml::String &text)
{
	Rml::String out;
	out.reserve(text.size());
	for (char c : text)
	{
		switch (c)
		{
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '&': out += "&amp;"; break;
		default: out += c; break;
		}
	}
	return out;
}

// Same vertical-fade-near-the-edges math as CreditsManager::draw(): full alpha through the middle
// third of the screen, fading out over the top/bottom thirds.
static float fadeAlphaForY(Int y, Int displayHeight)
{
	Int heightChunk = displayHeight / 3;
	if (y < 0 || y > displayHeight)
		return 0.0f;
	if (y < heightChunk)
		return (float)y / (float)heightChunk;
	if (y > heightChunk * 2)
		return 1.0f - (float)(y - 2 * heightChunk) / (float)heightChunk;
	return 1.0f;
}

//-------------------------------------------------------------------------------------------------
RmlCreditsScreen::RmlCreditsScreen()
{
}

RmlCreditsScreen::~RmlCreditsScreen()
{
	if (m_context && m_document)
	{
		m_context->UnloadDocument(m_document);
	}
}

void RmlCreditsScreen::load(Rml::Context *context)
{
	if (m_document)
		return; // already loaded

	m_context = context;
	m_document = context->LoadDocument("UI/CreditsMenu.rml");
}

void RmlCreditsScreen::show()
{
	if (!m_document)
		return;

	// Mirrors CreditsMenuInit(): hide the shell map behind the credits, (re)load TheCredits fresh
	// from Data/INI/Credits, and start the same fading-in "Credits" music track.
	if (TheShell)
		TheShell->showShellMap(FALSE);

	delete TheCredits;
	TheCredits = new CreditsManager;
	TheCredits->load();
	TheCredits->init();

	if (TheAudio)
	{
		TheAudio->removeAudioEvent(AHSV_StopTheMusicFade);
		AudioEventRTS event("Credits");
		event.setShouldFade(TRUE);
		TheAudio->addAudioEvent(&event);
	}

	Rml::Element *lines = m_document->GetElementById("credits_lines");
	if (lines)
		lines->SetInnerRML("");

	m_document->Show();
}

void RmlCreditsScreen::hide()
{
	// Mirrors CreditsMenuShutdown(): drop TheCredits and restore the shell map/music.
	if (TheCredits)
	{
		TheCredits->reset();
		delete TheCredits;
		TheCredits = nullptr;
	}
	if (TheShell)
		TheShell->showShellMap(TRUE);
	if (TheAudio)
		TheAudio->removeAudioEvent(AHSV_StopTheMusicFade);

	if (m_document)
		m_document->Hide();
}

bool RmlCreditsScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlCreditsScreen::onBack()
{
	// Same as CreditsMenuInput's KEY_ESC: skip straight back out.
	if (TheShell)
		TheShell->pop();
}

void RmlCreditsScreen::update()
{
	if (!TheCredits)
	{
		if (TheShell)
			TheShell->pop();
		return;
	}

	TheCredits->update();
	syncDisplayedLines();

	if (TheCredits->isFinished() && TheShell)
		TheShell->pop();
}

//-------------------------------------------------------------------------------------------------
void RmlCreditsScreen::syncDisplayedLines()
{
	Rml::Element *lines = m_document ? m_document->GetElementById("credits_lines") : nullptr;
	if (!lines || !TheCredits || !TheDisplay)
		return;

	Int displayHeight = TheDisplay->getHeight();
	Rml::String html;
	Int count = TheCredits->getDisplayedLineCount();
	for (Int i = 0; i < count; ++i)
	{
		CreditsManager::DisplayedLine line;
		TheCredits->getDisplayedLine(i, &line);
		if (line.m_text.isEmpty() && line.m_secondText.isEmpty())
			continue; // blank spacer lines carry no text

		UnsignedByte r, g, b, a;
		GameGetColorComponents(line.m_color, &r, &g, &b, &a);
		float alpha = (a / 255.0f) * fadeAlphaForY(line.m_y, displayHeight);
		char rgba[64];
		std::snprintf(rgba, sizeof(rgba), "rgba(%d,%d,%d,%.3f)", (int)r, (int)g, (int)b, alpha);

		const char *styleClass = "normal";
		if (line.m_style == CREDIT_STYLE_TITLE) styleClass = "title";
		else if (line.m_style == CREDIT_STYLE_POSITION) styleClass = "position";
		else if (line.m_style == CREDIT_STYLE_COLUMN) styleClass = "column";

		if (line.m_style == CREDIT_STYLE_COLUMN && line.m_useSecond)
		{
			// column-left/right bands are centered at width/3 and 2*width/3, matching the chunk
			// math CreditsManager::draw() uses for its two DisplayStrings.
			html += "<div class=\"credit-line column-left " + Rml::String(styleClass) + "\" style=\"top:" + std::to_string(line.m_y) +
				"px;color:" + rgba + ";\">" + escapeRml(unicodeToUtf8(line.m_text)) + "</div>";
			html += "<div class=\"credit-line column-right " + Rml::String(styleClass) + "\" style=\"top:" + std::to_string(line.m_y) +
				"px;color:" + rgba + ";\">" + escapeRml(unicodeToUtf8(line.m_secondText)) + "</div>";
		}
		else
		{
			html += "<div class=\"credit-line " + Rml::String(styleClass) + "\" style=\"top:" + std::to_string(line.m_y) +
				"px;color:" + rgba + ";\">" + escapeRml(unicodeToUtf8(line.m_text)) + "</div>";
		}
	}

	lines->SetInnerRML(html);
}

//-------------------------------------------------------------------------------------------------
RmlCreditsScreen &RmlCreditsScreen::instance()
{
	static RmlCreditsScreen s_screen;
	return s_screen;
}

void OpenRmlCreditsScreen()
{
	if (TheRmlUiManager)
		TheRmlUiManager->showScreen(&RmlCreditsScreen::instance());
}

void CloseRmlCreditsScreen()
{
	if (TheRmlUiManager && TheRmlUiManager->getCurrentScreen() == &RmlCreditsScreen::instance())
		TheRmlUiManager->hideCurrentScreen();
}
