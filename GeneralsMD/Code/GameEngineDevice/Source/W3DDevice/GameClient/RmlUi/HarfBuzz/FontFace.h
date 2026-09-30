// Adapted from RmlUi 6.3's HarfBuzz text-shaping sample (Samples/basic/harfbuzz), MIT License;
// see FontEngineInterfaceHarfBuzz.h for the copyright and permission notice.

#pragma once

#include "FontTypes.h"
#include "FontFaceHandleHarfBuzz.h"
#include <RmlUi/Core.h>

namespace RmlHarfBuzz {

using Rml::UniquePtr;
using Rml::UnorderedMap;
namespace Style = Rml::Style;

/**
    Modified to support HarfBuzz text shaping.
 */

class FontFace {
public:
	FontFace(FontFaceHandleFreetype face, Style::FontStyle style, Style::FontWeight weight);
	~FontFace();

	Style::FontStyle GetStyle() const;
	Style::FontWeight GetWeight() const;

	/// Returns a handle for positioning and rendering this face at the given size.
	/// @param[in] size The size of the desired handle, in points.
	/// @param[in] load_default_glyphs True to load the default set of glyph (ASCII range).
	/// @return The font handle.
	FontFaceHandleHarfBuzz* GetHandle(int size, bool load_default_glyphs);

	/// Releases resources owned by sized font faces, including their textures and rendered glyphs.
	void ReleaseFontResources();

private:
	Style::FontStyle style;
	Style::FontWeight weight;

	// Key is font size
	using HandleMap = UnorderedMap<int, UniquePtr<FontFaceHandleHarfBuzz>>;
	HandleMap handles;

	FontFaceHandleFreetype face;
};

} // namespace RmlHarfBuzz
