// Adapted from RmlUi 6.3's HarfBuzz text-shaping sample (Samples/basic/harfbuzz), MIT License;
// see FontEngineInterfaceHarfBuzz.h for the copyright and permission notice.

#pragma once

#include <RmlUi/Core.h>

namespace RmlHarfBuzz {

// As RmlUi's private Source/Core/FontEngineDefault/FontTypes.h.
using FontFaceHandleFreetype = uintptr_t;

struct FaceVariation {
	Rml::Style::FontWeight weight;
	uint16_t width;
	int named_instance_index;
};

inline bool operator<(const FaceVariation& a, const FaceVariation& b)
{
	if (a.weight == b.weight)
		return a.width < b.width;
	return a.weight < b.weight;
}

} // namespace RmlHarfBuzz
