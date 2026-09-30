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

// Right-to-left text helpers shared by the string table and the RmlUi text shaper. Standalone
// (standard library only) so tools and the RmlUi render harness can compile it as is.
//
// The bidi part is a small subset of the Unicode Bidirectional Algorithm (UAX #9) for one line
// with no explicit embeddings: rules W1-W7 (numbers and their separators/terminators), N1-N2
// (neutrals) and I1-I2, so levels never exceed 2, plus L1's trailing whitespace and L2's reorder.
//
// The legacy part turns the Arabic in older string tables back into ordinary text. Those tables
// (Patch104p's Arabic, for one) store it pre-shaped with Arabic Presentation Forms and in visual
// order, because the original GameFont renderer draws code units left to right with no shaping.
// legacyVisualToLogical() reverses that: per line, it reorders visual back to logical with the
// same rules the shaper uses to display it (so displaying the result reproduces the original
// line), swaps mirrored brackets in the right-to-left runs and maps presentation forms to their
// NFKC base letters (ligatures such as lam-alef expand to their letters).

#include <cstddef>
#include <string>

namespace RtlText
{

enum BidiType : unsigned char
{
	BIDI_L,		///< strong left-to-right
	BIDI_R,		///< strong right-to-left (R and AL)
	BIDI_EN,	///< number (EN and AN)
	BIDI_ES,	///< number separator (+ -)
	BIDI_ET,	///< number terminator ($ % # ...)
	BIDI_CS,	///< common number separator (, . / :)
	BIDI_NSM,	///< nonspacing mark (and boundary neutral): takes the type before it
	BIDI_N,		///< everything else: whitespace, punctuation, symbols
};

BidiType bidiType(unsigned int codepoint);

/// True for code points of right-to-left scripts (Hebrew, Arabic and their presentation forms).
bool isRtl(unsigned int codepoint);

/// Resolves the embedding level (0, 1 or 2) of each of count code points of one line; odd
/// levels run right to left. rtlBase picks the paragraph direction.
void resolveLevels(const unsigned int *text, size_t count, bool rtlBase, unsigned char *levels);

/// Fills order[] with the logical index shown at each visual position, left to right (L2).
void visualOrder(const unsigned char *levels, size_t count, int *order);

/// Unicode's Bidi_Mirrored bracket pairs: the partner of an opening/closing bracket, else 0.
unsigned int mirroredBracket(unsigned int codepoint);

/// True if some line of text looks like legacy visual-order Arabic: it has presentation forms,
/// or its words start and end the way reversed Arabic words do.
bool looksLegacyVisual(const std::wstring &text);

/// Converts legacy visual-order, pre-shaped Arabic to logical order with base letters, line by
/// line; lines that don't look legacy are returned unchanged, so logical text passes through.
std::wstring legacyVisualToLogical(const std::wstring &text);

/// The reverse, for a renderer with no shaping of its own (the GameFont path): per line, Arabic
/// letters become their contextual presentation forms (lam-alef its ligature) and the line is
/// reordered to visual order, right to left if preferRtl, else in the direction of its first
/// strong character. Lines with no Arabic letters are returned unchanged.
std::wstring logicalToLegacyVisual(const std::wstring &text, bool preferRtl);

} // namespace RtlText
