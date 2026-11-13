/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: GenOnlinePluginTrust.h ///////////////////////////////////////////////
// Description: Trust check of a GenOnline plugin folder. The folder carries
//              plugin.manifest and plugin.manifest.minisig. The minisign
//              signature of the manifest must verify against a compiled-in
//              public key, and the manifest lists the size in bytes and the
//              SHA256 of every file in the folder tree. The check is platform
//              independent and reaches the file system only through
//              GenOnlinePluginPlatform.h.
//
//              plugin.manifest format 1:
//              {"format":1,"name":"<name>","version":"YYYY.MM.PATCH",
//               "api_version":<n>,"files":[{"path":"<name>.dll",
//               "sha256":"<64 hex digits>","size":<bytes>}]}
//              Every key is required and no other key is allowed.
///////////////////////////////////////////////////////////////////////////////

// Threat model. The check protects against plugin folders that are distributed or tampered with, so only
// plugin files that a GenOnline key signed reach the loader. It is not a boundary against a local user who
// can already write the game folder, because that user can replace the game executable itself.
// Two races remain open, and both need a process that runs as that user while the game loads the plugin.
// A brand new file that appears in a locked folder after the folder walk and before the load is not seen,
// because share modes on a folder do not stop the creation of new files in it. A module that the loader
// maps from another file is detected only after the loader ran its code and the code of its imports.
// The size of every locked file is compared with the manifest before the file is hashed, so a listed file that
// grew is refused without reading it.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace GenOnlinePluginTrust
{
struct PluginVersion
{
	uint32_t year;
	uint32_t month;
	uint32_t patch;
};

// A plugin name that this build loads and the lowest manifest version that it accepts for that name.
struct AllowedPlugin
{
	const char* pluginName;
	PluginVersion minimumVersion;
};

// Holds the files and folders of a plugin open. The handles deny writes, renames and deletes, so the files
// that are hashed stay the files that the loader maps, and the folders on the way to them stay in place,
// until Close runs.
class PluginFiles
{
public:
	PluginFiles() {}
	~PluginFiles();

	PluginFiles(const PluginFiles&) = delete;
	PluginFiles& operator=(const PluginFiles&) = delete;

	void Close();

	// Locks the folder chain and the main DLL without any signature check, for builds that load unsigned plugins.
	// pluginsFolder is the absolute UTF-8 plugins folder path without a trailing separator.
	bool OpenUnverifiedMainFile(const std::string& pluginsFolder, const std::string& pluginName, std::string& outError);

	// The locked main DLL, which GenOnlinePluginPlatform::LoadModule loads.
	void* GetMainFile() const { return m_mainFile; }

	// The final path of the locked main DLL.
	const std::string& GetMainFilePath() const { return m_mainFilePath; }

	const std::string& GetVersion() const { return m_version; }

private:
	friend bool VerifyPluginFolder(const std::string&, const std::string&, uint32_t, std::span<const char* const>, std::span<const AllowedPlugin>, PluginFiles&, std::string&);

	std::vector<void*> m_handles;
	void* m_mainFile = nullptr;
	std::string m_mainFilePath;
	std::string m_version;
};

// True when at least one of the keys is not empty.
bool HasPublicKeys(std::span<const char* const> publicKeys);

// True when a plugin that fails the check or carries no signature may still load. That needs a build without
// any plugin key that defines GENONLINE_ALLOW_UNSIGNED_PLUGINS. A build with a key refuses such a plugin always.
bool MayLoadUnverifiedPlugin(std::span<const char* const> publicKeys, bool bAllowUnsignedPlugins);

// Verifies a minisign signature file over data against the minisign public key lines in publicKeys.
// Only the ED algorithm verifies, which signs the BLAKE2b-512 hash of the data, and the signature over the
// trusted comment has to verify too. Keys that share a key id are all tried.
bool VerifyMinisignSignature(const std::string& data, const std::string& signatureText, std::span<const char* const> publicKeys, std::string& outError);

// Parses a version of the form YYYY.MM.PATCH.
bool ParsePluginVersion(const std::string& text, PluginVersion& outVersion);

// Verifies the folder of the plugin pluginName inside pluginsFolder and keeps its folders and listed files open
// in outFiles on success. pluginsFolder is the absolute UTF-8 plugins folder path without a trailing separator.
// A plugin name that allowedPlugins does not list, or a manifest version below its minimum, is refused.
bool VerifyPluginFolder(const std::string& pluginsFolder, const std::string& pluginName, uint32_t apiVersion, std::span<const char* const> publicKeys, std::span<const AllowedPlugin> allowedPlugins, PluginFiles& outFiles, std::string& outError);
} // namespace GenOnlinePluginTrust
