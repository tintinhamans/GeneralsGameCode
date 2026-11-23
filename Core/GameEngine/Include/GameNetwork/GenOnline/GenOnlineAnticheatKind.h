#pragma once

#include "Common/UnicodeString.h"
#include <string>

// The anti-cheat a lobby requires. Every client runs the GenOnline one, only Eac is an opt-in requirement of a lobby.
enum class EAnticheatKind
{
	None,
	BuiltIn,
	Eac
};

// Reads the lobby anticheat string: "none", "go_integrated" or "easy_anticheat". Anything else counts as none.
EAnticheatKind ParseAnticheatKind(const std::string& wire);
EAnticheatKind LocalAnticheatKind();
// The mapped image of the kind, the dim one for a lobby this client cannot join. Null names never occur.
const char* AnticheatKindImageName(EAnticheatKind kind, bool dim);
UnicodeString AnticheatKindText(EAnticheatKind kind);
