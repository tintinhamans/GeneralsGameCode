// Adapted from RmlUi 6.3's HarfBuzz text-shaping sample (Samples/basic/harfbuzz), MIT License;
// see FontEngineInterfaceHarfBuzz.h for the copyright and permission notice.

#include "FontEngineInterfaceHarfBuzz.h"
#include "FontFaceHandleHarfBuzz.h"
#include "FontProvider.h"
#include "Common/RtlText.h"
#include <RmlUi/Core.h>
#include <algorithm>

namespace RmlHarfBuzz {

namespace {

bool IsArabicScript(unsigned int c)
{
	return (c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F) || (c >= 0x0870 && c <= 0x08FF) || (c >= 0xFB50 && c <= 0xFDFF) ||
		(c >= 0xFE70 && c <= 0xFEFF);
}

} // namespace

void FontEngineInterfaceHarfBuzz::Initialize()
{
	FontProvider::Initialise();
}
void FontEngineInterfaceHarfBuzz::Shutdown()
{
	layer_effects.clear();
	run_faces.clear();
	FontProvider::Shutdown();
}

bool FontEngineInterfaceHarfBuzz::LoadFontFace(const String& file_name, int face_index, bool fallback_face, Style::FontWeight weight)
{
	return FontProvider::LoadFontFace(file_name, face_index, fallback_face, weight);
}

bool FontEngineInterfaceHarfBuzz::LoadFontFace(Span<const byte> data, int face_index, const String& font_family, Style::FontStyle style,
	Style::FontWeight weight, bool fallback_face)
{
	return FontProvider::LoadFontFace(data, face_index, font_family, style, weight, fallback_face);
}

FontFaceHandle FontEngineInterfaceHarfBuzz::GetFontFaceHandle(const String& family, Style::FontStyle style, Style::FontWeight weight, int size)
{
	auto substitute = family_substitutes.find(family);
	if (substitute != family_substitutes.end())
	{
		const FamilySubstitute& to = (int)weight >= 600 ? substitute->second.second : substitute->second.first;
		const String& to_family = to.family.empty() ? family : to.family;
		const Style::FontWeight to_weight = to.weight == Style::FontWeight::Auto ? weight : to.weight;
		if (FontFaceHandleHarfBuzz* handle = FontProvider::GetFontFaceHandle(to_family, style, to_weight, size))
			return reinterpret_cast<FontFaceHandle>(handle);
	}

	auto handle = FontProvider::GetFontFaceHandle(family, style, weight, size);
	return reinterpret_cast<FontFaceHandle>(handle);
}

FontEffectsHandle FontEngineInterfaceHarfBuzz::PrepareFontEffects(FontFaceHandle handle, const FontEffectList& font_effects)
{
	auto handle_harfbuzz = reinterpret_cast<FontFaceHandleHarfBuzz*>(handle);
	const int configuration = handle_harfbuzz->GenerateLayerConfiguration(font_effects);

	Rml::Vector<FontEffectList>& effects = layer_effects[handle_harfbuzz];
	if ((int)effects.size() <= configuration)
		effects.resize(configuration + 1);
	effects[configuration] = font_effects;

	return (FontEffectsHandle)configuration;
}

const FontMetrics& FontEngineInterfaceHarfBuzz::GetFontMetrics(FontFaceHandle handle)
{
	auto handle_harfbuzz = reinterpret_cast<FontFaceHandleHarfBuzz*>(handle);
	return handle_harfbuzz->GetFontMetrics();
}

int FontEngineInterfaceHarfBuzz::GetStringWidth(FontFaceHandle handle, StringView string, const TextShapingContext& text_shaping_context,
	Character prior_character)
{
	auto handle_harfbuzz = reinterpret_cast<FontFaceHandleHarfBuzz*>(handle);

	Rml::Vector<Run> runs;
	SplitRuns(handle_harfbuzz, string, text_shaping_context, runs);

	int width = 0;
	for (const Run& run : runs)
	{
		TextShapingContext run_context = text_shaping_context;
		run_context.text_direction = run.right_to_left ? Style::Direction::Rtl : Style::Direction::Ltr;
		// Tracking would pull joined (cursive) letters apart; CSS leaves it off them too.
		if (run.right_to_left)
			run_context.letter_spacing = 0.0f;
		width += run.face->GetStringWidth(StringView(string.begin() + run.begin, string.begin() + run.end), run_context, registered_languages,
			prior_character);
		prior_character = Character::Null;
	}
	return width;
}

int FontEngineInterfaceHarfBuzz::GenerateString(RenderManager& render_manager, FontFaceHandle handle, FontEffectsHandle font_effects_handle,
	StringView string, Vector2f position, ColourbPremultiplied colour, float opacity, const TextShapingContext& text_shaping_context,
	TexturedMeshList& mesh_list)
{
	auto handle_harfbuzz = reinterpret_cast<FontFaceHandleHarfBuzz*>(handle);

	Rml::Vector<Run> runs;
	SplitRuns(handle_harfbuzz, string, text_shaping_context, runs);

	if (runs.size() == 1 && runs[0].face == handle_harfbuzz)
	{
		TextShapingContext run_context = text_shaping_context;
		run_context.text_direction = runs[0].right_to_left ? Style::Direction::Rtl : Style::Direction::Ltr;
		if (runs[0].right_to_left)
			run_context.letter_spacing = 0.0f;
		return handle_harfbuzz->GenerateString(render_manager, mesh_list, string, position, colour, opacity, run_context, registered_languages,
			(int)font_effects_handle);
	}

	// Each run appends its own meshes; runs drawn with another face use that face's textures.
	mesh_list.clear();
	int width = 0;
	for (const Run& run : runs)
	{
		TextShapingContext run_context = text_shaping_context;
		run_context.text_direction = run.right_to_left ? Style::Direction::Rtl : Style::Direction::Ltr;
		// Tracking would pull joined (cursive) letters apart; CSS leaves it off them too.
		if (run.right_to_left)
			run_context.letter_spacing = 0.0f;
		const int configuration = LayerConfigurationFor(run.face, handle_harfbuzz, (int)font_effects_handle);

		TexturedMeshList run_meshes;
		width += run.face->GenerateString(render_manager, run_meshes, StringView(string.begin() + run.begin, string.begin() + run.end),
			Vector2f(position.x + (float)width, position.y), colour, opacity, run_context, registered_languages, configuration);
		for (auto& mesh : run_meshes)
			mesh_list.push_back(std::move(mesh));
	}
	return width;
}

int FontEngineInterfaceHarfBuzz::GetVersion(FontFaceHandle handle)
{
	auto handle_harfbuzz = reinterpret_cast<FontFaceHandleHarfBuzz*>(handle);
	int version = handle_harfbuzz->GetVersion();
	auto faces = run_faces.find(handle_harfbuzz);
	if (faces != run_faces.end())
	{
		for (FontFaceHandleHarfBuzz* face : faces->second)
			version += face->GetVersion();
	}
	return version;
}

void FontEngineInterfaceHarfBuzz::ReleaseFontResources()
{
	layer_effects.clear();
	run_faces.clear();
	FontProvider::ReleaseFontResources();
}

void FontEngineInterfaceHarfBuzz::RegisterLanguage(const String& language_bcp47_code, const String& script_iso15924_code,
	const TextFlowDirection text_flow_direction)
{
	registered_languages[language_bcp47_code] = LanguageData{script_iso15924_code, text_flow_direction};
}

void FontEngineInterfaceHarfBuzz::SetFamilySubstitute(const String& family, const FamilySubstitute& regular, const FamilySubstitute& bold)
{
	family_substitutes[Rml::StringUtilities::ToLower(family)] = {
		FamilySubstitute{Rml::StringUtilities::ToLower(regular.family), regular.weight},
		FamilySubstitute{Rml::StringUtilities::ToLower(bold.family), bold.weight}};
}

FontFaceHandleHarfBuzz* FontEngineInterfaceHarfBuzz::FaceForScript(FontFaceHandleHarfBuzz* primary, Character character) const
{
	if (primary->HasCharacter(character))
		return primary;

	const int count = FontProvider::CountFallbackFontFaces();
	for (int i = 0; i < count; ++i)
	{
		FontFaceHandleHarfBuzz* fallback = FontProvider::GetFallbackFontFace(i, primary->GetFontMetrics().size);
		if (fallback && fallback->HasCharacter(character))
			return fallback;
	}
	return primary;
}

int FontEngineInterfaceHarfBuzz::LayerConfigurationFor(FontFaceHandleHarfBuzz* face, FontFaceHandleHarfBuzz* primary, int layer_configuration)
{
	if (face == primary)
		return layer_configuration;

	Rml::Vector<FontFaceHandleHarfBuzz*>& faces = run_faces[primary];
	if (std::find(faces.begin(), faces.end(), face) == faces.end())
		faces.push_back(face);

	auto effects = layer_effects.find(primary);
	if (effects == layer_effects.end() || layer_configuration >= (int)effects->second.size())
		return 0;
	return face->GenerateLayerConfiguration(effects->second[layer_configuration]);
}

void FontEngineInterfaceHarfBuzz::SplitRuns(FontFaceHandleHarfBuzz* primary, StringView string, const TextShapingContext& text_shaping_context,
	Rml::Vector<Run>& runs) const
{
	runs.clear();

	// Most text is plain ASCII in a left-to-right document: one run, as the sample shapes it.
	bool ascii = true;
	for (const char* c = string.begin(); c != string.end() && ascii; ++c)
		ascii = (unsigned char)*c < 0x80;
	if (ascii && text_shaping_context.text_direction != Style::Direction::Rtl)
	{
		runs.push_back(Run{0, string.size(), false, primary});
		return;
	}

	Rml::Vector<unsigned int> codepoints;
	Rml::Vector<size_t> offsets;
	for (const char* c = string.begin(); c != string.end();)
	{
		offsets.push_back((size_t)(c - string.begin()));
		codepoints.push_back((unsigned int)Rml::StringUtilities::ToCharacter(c, string.end()));
		c += Rml::Math::Max((size_t)1, Rml::StringUtilities::BytesUTF8(Rml::StringUtilities::ToCharacter(c, string.end())));
	}
	offsets.push_back(string.size());
	const size_t count = codepoints.size();
	if (count == 0)
	{
		runs.push_back(Run{0, string.size(), text_shaping_context.text_direction == Style::Direction::Rtl, primary});
		return;
	}

	// The line's direction. In a right-to-left element a line with left-to-right letters and no
	// right-to-left ones (a player or map name, English chat) still reads left to right, so its
	// trailing punctuation and brackets stay where they belong; one with no letters at all (a
	// chevron, a number) takes the element's direction. Elsewhere the first strong character
	// decides (UAX #9 P2/P3), so Arabic in a left-to-right document reads right to left.
	bool rtl_base = false;
	if (text_shaping_context.text_direction == Style::Direction::Rtl)
	{
		bool any_rtl = false;
		bool any_ltr = false;
		for (unsigned int c : codepoints)
		{
			const RtlText::BidiType type = RtlText::bidiType(c);
			any_rtl = any_rtl || type == RtlText::BIDI_R;
			any_ltr = any_ltr || type == RtlText::BIDI_L;
		}
		rtl_base = any_rtl || !any_ltr;
	}
	else if (text_shaping_context.text_direction == Style::Direction::Auto)
	{
		for (unsigned int c : codepoints)
		{
			const RtlText::BidiType type = RtlText::bidiType(c);
			if (type == RtlText::BIDI_L || type == RtlText::BIDI_R)
			{
				rtl_base = type == RtlText::BIDI_R;
				break;
			}
		}
	}

	Rml::Vector<unsigned char> levels(count);
	RtlText::resolveLevels(codepoints.data(), count, rtl_base, levels.data());

	// Level runs in logical order; right-to-left ones split where an Arabic letter needs another face
	// (spaces and marks stay with the face of the letter before them).
	struct LevelRun {
		size_t first;
		size_t last;
		unsigned char level;
		FontFaceHandleHarfBuzz* face;
	};
	Rml::Vector<LevelRun> logical_runs;
	for (size_t i = 0; i < count;)
	{
		const unsigned char level = levels[i];
		size_t end = i;
		while (end < count && levels[end] == level)
			++end;

		if (level & 1)
		{
			FontFaceHandleHarfBuzz* face = nullptr;
			size_t start = i;
			for (size_t k = i; k < end; ++k)
			{
				if (!IsArabicScript(codepoints[k]) || RtlText::bidiType(codepoints[k]) != RtlText::BIDI_R)
					continue;
				FontFaceHandleHarfBuzz* letter_face = FaceForScript(primary, (Character)codepoints[k]);
				if (!face)
				{
					face = letter_face;
				}
				else if (letter_face != face)
				{
					logical_runs.push_back(LevelRun{start, k, level, face});
					start = k;
					face = letter_face;
				}
			}
			logical_runs.push_back(LevelRun{start, end, level, face ? face : primary});
		}
		else
		{
			logical_runs.push_back(LevelRun{i, end, level, primary});
		}
		i = end;
	}

	// L2: reverse sequences of runs from the highest level down to the lowest odd one.
	unsigned char highest = 0;
	unsigned char lowest_odd = 255;
	for (const LevelRun& run : logical_runs)
	{
		highest = Rml::Math::Max(highest, run.level);
		if (run.level & 1)
			lowest_odd = Rml::Math::Min(lowest_odd, run.level);
	}
	for (int level = highest; level >= (int)lowest_odd && level > 0; --level)
	{
		for (size_t i = 0; i < logical_runs.size();)
		{
			if (logical_runs[i].level < level)
			{
				++i;
				continue;
			}
			size_t end = i;
			while (end < logical_runs.size() && logical_runs[end].level >= level)
				++end;
			std::reverse(logical_runs.begin() + i, logical_runs.begin() + end);
			i = end;
		}
	}

	for (const LevelRun& run : logical_runs)
		runs.push_back(Run{offsets[run.first], offsets[run.last], (run.level & 1) != 0, run.face});
}

} // namespace RmlHarfBuzz
