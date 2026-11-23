#include "PreRTS.h"

#include "GameNetwork/GenOnline/GenOnlineAnticheatKind.h"
#include "GameNetwork/GenOnline/Provider/GenOnlinePluginHost.h"

EAnticheatKind ParseAnticheatKind(const std::string& wire)
{
	if (wire == "go_integrated")
		return EAnticheatKind::BuiltIn;
	if (wire == "easy_anticheat")
		return EAnticheatKind::Eac;
	return EAnticheatKind::None;
}

EAnticheatKind LocalAnticheatKind()
{
	// The service enum: NONE -1, GO_INTEGRATED_AC 0, EASY_ANTICHEAT 9481.
	switch (GenOnlinePluginHost::GetAnticheatIdentifier())
	{
		case 0: return EAnticheatKind::BuiltIn;
		case 9481: return EAnticheatKind::Eac;
		default: return EAnticheatKind::None;
	}
}

const char* AnticheatKindImageName(EAnticheatKind kind, bool dim)
{
	switch (kind)
	{
		case EAnticheatKind::BuiltIn: return dim ? "GOShieldBuiltInDim" : "GOShieldBuiltIn";
		case EAnticheatKind::Eac: return dim ? "GOShieldEacDim" : "GOShieldEac";
		default: return dim ? "GOShieldNoneDim" : "GOShieldNone";
	}
}

UnicodeString AnticheatKindText(EAnticheatKind kind)
{
	switch (kind)
	{
		case EAnticheatKind::BuiltIn: return UnicodeString(L"GenOnline Anti-Cheat");
		case EAnticheatKind::Eac: return UnicodeString(L"Easy Anti-Cheat");
		default: return UnicodeString(L"No anti-cheat");
	}
}
