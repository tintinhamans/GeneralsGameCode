// Adapted from RmlUi 6.3's HarfBuzz text-shaping sample (Samples/basic/harfbuzz), MIT License;
// see FontEngineInterfaceHarfBuzz.h for the copyright and permission notice.

#pragma once

#include <RmlUi/Core.h>

namespace RmlHarfBuzz {

enum class TextFlowDirection {
	LeftToRight,
	RightToLeft,
};

struct LanguageData {
	Rml::String script_code;
	TextFlowDirection text_flow_direction;
};

using LanguageDataMap = Rml::UnorderedMap<Rml::String, LanguageData>;

} // namespace RmlHarfBuzz
