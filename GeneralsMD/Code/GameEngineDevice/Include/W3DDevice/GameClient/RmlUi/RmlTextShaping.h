/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

#pragma once

// RmlUi's text in the UI language: the HarfBuzz font engine (RmlUi/HarfBuzz, shaping and bidi),
// the fonts each language uses, and the lang/dir attributes every document gets. Uses RmlUi and
// the standard library only, so the render harness sets text up exactly like the game.

#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

namespace Rml { class FontEngineInterface; }

namespace RmlTextShaping
{

/// The heading font for Latin and Cyrillic text; body text is Noto Sans. The UI uses Exo 2; the
/// others remain for comparing pairings (the render harness).
enum HeadingFont
{
	HEADING_BARLOW,	///< Barlow Bold (Latin only; Cyrillic headings use Noto Sans Bold)
	HEADING_EXO2,	///< Exo 2 Bold
	HEADING_OSWALD,	///< Oswald Bold
};

struct FontSetup
{
	std::string language;					///< GameTextLanguages code; "" for the installed language
	std::vector<std::string> fontDirs;		///< where to look for fonts, in order ("UI/Fonts/", language packs)
	std::string windowsFontsDir;			///< the system fonts folder, for fallbacks ("C:\Windows\Fonts\")
	HeadingFont heading = HEADING_EXO2;
};

/// The font engine to give Rml::SetFontEngineInterface() before Rml::Initialise(); owned here.
Rml::FontEngineInterface *createFontEngine();
/// Frees it and the font data; call after Rml::Shutdown().
void destroyFontEngine();

/// Loads the UI fonts for the language and points the style sheets' family (Barlow, which the UI no
/// longer draws with) at them: body text (normal weight) in Noto Sans, headings (bold) in Exo 2, and
/// in Arabic, Korean and Chinese their own Noto faces. Call after Rml::Initialise(). Missing fonts
/// are skipped: the language then keeps Barlow plus fallbacks.
void loadFonts(const FontSetup &setup);

/// The font files a language looks for (for the distribution notes and the harness).
std::vector<std::string> fontFilesFor(const std::string &language);

/// True for the right-to-left languages (Arabic).
bool isRightToLeft(const std::string &language);

/// Gives every document loaded from now on lang (BCP 47) and, for right-to-left languages,
/// dir="rtl", which the shaper reads, plus mirrored style sheets (see mirrorStyleSheet()).
void tagDocuments(const std::string &language);

/// A right-to-left copy of an RCSS sheet, the way rtlcss mirrors CSS: left and right swap in
/// property names (left, margin-left, border-left-width, ...) and values (text-align, float),
/// rows become row-reverse, four-value margins/paddings/borders and radii swap their sides, and
/// horizontal gradients run the other way. A right-to-left document gets its sheets replaced by
/// these, which keeps the cascade exactly as it was. Rules whose selector names [dir=rtl] are
/// written for right-to-left already and stay as they are; at-rules other than @media too.
std::string mirrorStyleSheet(const std::string &rcss);

} // namespace RmlTextShaping
