// Adapted from RmlUi 6.3's HarfBuzz text-shaping sample (Samples/basic/harfbuzz), MIT License;
// see FontEngineInterfaceHarfBuzz.h for the copyright and permission notice.

#pragma once

#include "FontTypes.h"
#include "FontGlyph.h"
#include <RmlUi/Core.h>

namespace RmlHarfBuzz {

using Rml::Character;
using Rml::FontMetrics;

namespace FreeType {

// The FreeType library and face lifetime, as RmlUi's private Rml::FreeType (Source/Core/
// FontEngineDefault/FreeTypeInterface.cpp) does it for its default font engine.
bool Initialise();
void Shutdown();
// Returns a sorted list of available font variations for the font face located in memory.
bool GetFaceVariations(Rml::Span<const Rml::byte> data, Rml::Vector<FaceVariation>& out_face_variations, int face_index);
// Loads a FreeType face from memory, 'source' is only used for logging.
FontFaceHandleFreetype LoadFace(Rml::Span<const Rml::byte> data, const Rml::String& source, int face_index, int named_instance_index = 0);
bool ReleaseFace(FontFaceHandleFreetype face);
// Retrieves the font family, style and weight of the given font face. Use nullptr to ignore a property.
void GetFaceStyle(FontFaceHandleFreetype face, Rml::String* font_family, Rml::Style::FontStyle* style, Rml::Style::FontWeight* weight);
// True if the face maps the character to a glyph.
bool HasCharacter(FontFaceHandleFreetype face, Character character);

// Initializes a face for a given font size. Glyphs are filled with the ASCII subset, and the font face metrics are set.
bool InitialiseFaceHandle(FontFaceHandleFreetype face, int font_size, FontGlyphMap& glyphs, FontMetrics& metrics, bool load_default_glyphs);

// Build a new glyph representing the given glyph index and append to 'glyphs'.
bool AppendGlyph(FontFaceHandleFreetype face, int font_size, FontGlyphIndex glyph_index, Character character, FontGlyphMap& glyphs);

// Returns the corresponding glyph index from a character code.
FontGlyphIndex GetGlyphIndexFromCharacter(FontFaceHandleFreetype face, Character character);

} // namespace FreeType

} // namespace RmlHarfBuzz
