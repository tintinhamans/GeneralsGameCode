#pragma once

#include <GenOnlinePluginApi.h>
#include <string>

// Version of the -describe JSON object.
#define GENONLINE_CLIENT_DESCRIPTION_SCHEMA_VERSION 1

#define GENONLINE_ANTICHEAT_INTERFACE_VERSION GENONLINE_PLUGIN_ANTICHEAT_API_VERSION

// Oldest launcher version that can start this client.
#define GENONLINE_REQUIRED_LAUNCHER_VERSION "2026.11.0"

// Returns the -describe JSON object the launcher reads, followed by a newline.
std::string GenOnlineBuildClientDescription();

// Writes the client description to the inherited standard output.
// Returns false when the process has no standard output or the write fails.
bool GenOnlineWriteClientDescription();
