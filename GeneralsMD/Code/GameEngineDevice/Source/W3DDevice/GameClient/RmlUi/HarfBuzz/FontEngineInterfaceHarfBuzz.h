// Adapted from RmlUi 6.3's HarfBuzz text-shaping sample (Samples/basic/harfbuzz), under its license:
//
// MIT License
//
// Copyright (c) 2008-2014 CodePoint Ltd, Shift Technology Ltd, and contributors
// Copyright (c) 2019-2026 The RmlUi Team, and contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Changes for Generals Online: everything is in namespace RmlHarfBuzz; FreeType is set up here
// instead of through RmlUi's private FontEngineDefault headers; the interface below lays text out
// as bidi runs (Common/RtlText's subset of UAX #9), shapes Arabic runs with the first face that has
// the script when the element's face doesn't, and can substitute font families per weight.

#pragma once

#include "LanguageData.h"
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/FontEffect.h>

namespace RmlHarfBuzz {

using Rml::byte;
using Rml::Character;
using Rml::ColourbPremultiplied;
using Rml::FontEffectList;
using Rml::FontEffectsHandle;
using Rml::FontFaceHandle;
using Rml::FontMetrics;
using Rml::RenderManager;
using Rml::Span;
using Rml::String;
using Rml::StringView;
using Rml::TextShapingContext;
using Rml::TexturedMeshList;
using Rml::Vector2f;
namespace Style = Rml::Style;

class FontFaceHandleHarfBuzz;

class FontEngineInterfaceHarfBuzz : public Rml::FontEngineInterface {
public:
	void Initialize() override;
	void Shutdown() override;

	/// Adds a new font face to the database. The face's family, style and weight will be determined from the face itself.
	bool LoadFontFace(const String& file_name, int face_index, bool fallback_face, Style::FontWeight weight) override;

	/// Adds a new font face to the database using the provided family, style and weight.
	bool LoadFontFace(Span<const byte> data, int face_index, const String& font_family, Style::FontStyle style, Style::FontWeight weight,
		bool fallback_face) override;

	/// Returns a handle to a font face that can be used to position and render text. This will return the closest match
	/// it can find, but in the event a font family is requested that does not exist, NULL will be returned instead of a
	/// valid handle.
	FontFaceHandle GetFontFaceHandle(const String& family, Style::FontStyle style, Style::FontWeight weight, int size) override;

	/// Prepares for font effects by configuring a new, or returning an existing, layer configuration.
	FontEffectsHandle PrepareFontEffects(FontFaceHandle handle, const FontEffectList& font_effects) override;

	/// Returns the font metrics of the given font face.
	const FontMetrics& GetFontMetrics(FontFaceHandle handle) override;

	/// Returns the width a string will take up if rendered with this handle.
	int GetStringWidth(FontFaceHandle handle, StringView string, const TextShapingContext& text_shaping_context, Character prior_character) override;

	/// Generates the geometry required to render a single line of text.
	int GenerateString(RenderManager& render_manager, FontFaceHandle face_handle, FontEffectsHandle effects_handle, StringView string,
		Vector2f position, ColourbPremultiplied colour, float opacity, const TextShapingContext& text_shaping_context,
		TexturedMeshList& mesh_list) override;

	/// Returns the current version of the font face.
	int GetVersion(FontFaceHandle handle) override;

	/// Releases resources owned by sized font faces, including their textures and rendered glyphs.
	void ReleaseFontResources() override;

	/// Registers a new language to assist with text shaping.
	void RegisterLanguage(const String& language_bcp47_code, const String& script_iso15924_code, const TextFlowDirection text_flow_direction);

	struct FamilySubstitute {
		String family;             ///< empty keeps the requested family
		Style::FontWeight weight;  ///< Auto keeps the requested weight
	};
	/// Draws the family (as the style sheets name it) with the regular substitute below semibold weight
	/// and the bold one from it; a substitute that has no loaded faces leaves that weight as it is.
	void SetFamilySubstitute(const String& family, const FamilySubstitute& regular, const FamilySubstitute& bold);

private:
	struct Run {
		size_t begin;
		size_t end;
		bool right_to_left;
		FontFaceHandleHarfBuzz* face;
	};

	/// Splits one line into runs in visual order: bidi level runs, with right-to-left runs further split
	/// where their Arabic letters need a face other than the element's.
	void SplitRuns(FontFaceHandleHarfBuzz* primary, StringView string, const TextShapingContext& text_shaping_context,
		Rml::Vector<Run>& runs) const;
	FontFaceHandleHarfBuzz* FaceForScript(FontFaceHandleHarfBuzz* primary, Character character) const;
	int LayerConfigurationFor(FontFaceHandleHarfBuzz* face, FontFaceHandleHarfBuzz* primary, int layer_configuration);

	LanguageDataMap registered_languages;
	Rml::UnorderedMap<String, std::pair<FamilySubstitute, FamilySubstitute>> family_substitutes;
	// The font effects behind each (handle, layer configuration), to configure a fallback face the same way.
	Rml::UnorderedMap<FontFaceHandleHarfBuzz*, Rml::Vector<FontEffectList>> layer_effects;
	// The faces other than the element's that runs were drawn with, by the element's face; their versions
	// count towards its version.
	Rml::UnorderedMap<FontFaceHandleHarfBuzz*, Rml::Vector<FontFaceHandleHarfBuzz*>> run_faces;
};

} // namespace RmlHarfBuzz
