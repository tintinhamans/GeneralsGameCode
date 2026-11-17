#include "GameNetwork/GenOnline/GenOnlineClientDescription.h"

#include <windows.h>

#include "GameNetwork/GenOnline/json.hpp"

std::string GenOnlineBuildClientDescription()
{
	nlohmann::ordered_json description;
	description["schema_version"] = GENONLINE_CLIENT_DESCRIPTION_SCHEMA_VERSION;
	description["requires_launcher"] = GENONLINE_REQUIRED_LAUNCHER_VERSION;
	description["plugins"]["anticheat_interface"] = GENONLINE_ANTICHEAT_INTERFACE_VERSION;
	description["plugins"]["providers"] = nlohmann::ordered_json::array({ "genonline_eos", "genonline_anticheat" });
	return description.dump() + "\n";
}

bool GenOnlineWriteClientDescription()
{
	// A GUI subsystem process has no console, the launcher passes a pipe as its standard output.
	HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
	if (output == nullptr || output == INVALID_HANDLE_VALUE)
		return false;

	const std::string text = GenOnlineBuildClientDescription();
	const char* data = text.data();
	DWORD remaining = static_cast<DWORD>(text.size());
	while (remaining > 0)
	{
		DWORD written = 0;
		if (!WriteFile(output, data, remaining, &written, nullptr) || written == 0)
			return false;

		data += written;
		remaining -= written;
	}

	return true;
}
