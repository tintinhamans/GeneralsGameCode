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

// FILE: GenOnlinePluginTrust.cpp /////////////////////////////////////////////
// Description: Trust check of a GenOnline plugin folder.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GenOnline/Provider/GenOnlinePluginTrust.h"
#include "GameNetwork/GenOnline/Provider/GenOnlinePluginPlatform.h"

#include "GameNetwork/GenOnline/json.hpp"

#include <monocypher.h>
#include <monocypher-ed25519.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <set>

namespace GenOnlinePluginTrust
{
namespace
{
constexpr size_t kSha256Size = 32;
constexpr size_t kKeyIdSize = 8;
constexpr size_t kPublicKeySize = 32;
constexpr size_t kSignatureSize = 64;
constexpr size_t kBlake2bSize = 64;

constexpr uint64_t kMaxManifestSize = 64 * 1024;
constexpr uint64_t kMaxSignatureSize = 4 * 1024;
constexpr size_t kMaxManifestFiles = 256;
constexpr size_t kMaxPathLength = 200;
constexpr size_t kMaxPathDepth = 8;
constexpr size_t kMaxFolderEntries = 4096;

const char kUntrustedCommentPrefix[] = "untrusted comment: ";
const char kTrustedCommentPrefix[] = "trusted comment: ";

struct MinisignPublicKey
{
	std::array<uint8_t, kKeyIdSize> keyId;
	std::array<uint8_t, kPublicKeySize> key;
};

struct MinisignSignature
{
	std::array<uint8_t, 2> algorithm;
	std::array<uint8_t, kKeyIdSize> keyId;
	std::array<uint8_t, kSignatureSize> signature;
	std::string trustedComment;
	std::array<uint8_t, kSignatureSize> globalSignature;
};

struct ListedFile
{
	std::string path;
	std::string key;
	std::array<uint8_t, kSha256Size> sha256;
	uint64_t size;
};

namespace Platform = GenOnlinePluginPlatform;

int DecodeBase64Char(char c)
{
	if (c >= 'A' && c <= 'Z')
	{
		return c - 'A';
	}

	if (c >= 'a' && c <= 'z')
	{
		return c - 'a' + 26;
	}

	if (c >= '0' && c <= '9')
	{
		return c - '0' + 52;
	}

	if (c == '+')
	{
		return 62;
	}

	if (c == '/')
	{
		return 63;
	}

	return -1;
}

// Decodes padded base64 and refuses any other character, misplaced padding and unused bits that are set.
bool DecodeBase64(const std::string& text, std::vector<uint8_t>& out)
{
	out.clear();
	if (text.empty() || text.size() % 4 != 0)
	{
		return false;
	}

	size_t padding = 0;
	if (text[text.size() - 1] == '=')
	{
		padding = text[text.size() - 2] == '=' ? 2 : 1;
	}

	const size_t dataChars = text.size() - padding;
	uint32_t accumulator = 0;
	int bits = 0;
	for (size_t i = 0; i < dataChars; ++i)
	{
		const int value = DecodeBase64Char(text[i]);
		if (value < 0)
		{
			return false;
		}

		accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFF));
		}
	}

	return (accumulator & ((1u << bits) - 1)) == 0;
}

std::string KeyIdToHex(const std::array<uint8_t, kKeyIdSize>& keyId)
{
	// Minisign prints the key id as a little endian number.
	static const char kDigits[] = "0123456789ABCDEF";
	std::string hex;
	for (size_t i = keyId.size(); i > 0; --i)
	{
		hex.push_back(kDigits[keyId[i - 1] >> 4]);
		hex.push_back(kDigits[keyId[i - 1] & 0x0F]);
	}
	return hex;
}

bool DecodePublicKey(const char* szBase64, MinisignPublicKey& outKey)
{
	std::vector<uint8_t> bytes;
	if (!DecodeBase64(szBase64, bytes) || bytes.size() != 2 + kKeyIdSize + kPublicKeySize || bytes[0] != 'E' || bytes[1] != 'd')
	{
		return false;
	}

	std::memcpy(outKey.keyId.data(), bytes.data() + 2, kKeyIdSize);
	std::memcpy(outKey.key.data(), bytes.data() + 2 + kKeyIdSize, kPublicKeySize);
	return true;
}

bool DecodeSignature(const std::string& text, MinisignSignature& outSignature)
{
	std::vector<std::string> lines;
	size_t start = 0;
	while (start <= text.size())
	{
		size_t end = text.find('\n', start);
		if (end == std::string::npos)
		{
			end = text.size();
		}

		std::string line = text.substr(start, end - start);
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}

		lines.push_back(line);
		start = end + 1;
	}

	while (!lines.empty() && lines.back().empty())
	{
		lines.pop_back();
	}

	if (lines.size() != 4)
	{
		return false;
	}

	if (lines[0].compare(0, sizeof(kUntrustedCommentPrefix) - 1, kUntrustedCommentPrefix) != 0
		|| lines[2].compare(0, sizeof(kTrustedCommentPrefix) - 1, kTrustedCommentPrefix) != 0)
	{
		return false;
	}

	std::vector<uint8_t> signatureBytes;
	if (!DecodeBase64(lines[1], signatureBytes) || signatureBytes.size() != 2 + kKeyIdSize + kSignatureSize)
	{
		return false;
	}

	std::vector<uint8_t> globalBytes;
	if (!DecodeBase64(lines[3], globalBytes) || globalBytes.size() != kSignatureSize)
	{
		return false;
	}

	std::memcpy(outSignature.algorithm.data(), signatureBytes.data(), 2);
	std::memcpy(outSignature.keyId.data(), signatureBytes.data() + 2, kKeyIdSize);
	std::memcpy(outSignature.signature.data(), signatureBytes.data() + 2 + kKeyIdSize, kSignatureSize);
	outSignature.trustedComment = lines[2].substr(sizeof(kTrustedCommentPrefix) - 1);
	std::memcpy(outSignature.globalSignature.data(), globalBytes.data(), kSignatureSize);
	return true;
}

char ToLowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string ToLowerAscii(const std::string& text)
{
	std::string result = text;
	for (char& c : result)
	{
		c = ToLowerAscii(c);
	}
	return result;
}

bool IsReservedDeviceName(const std::string& component)
{
	const std::string stem = ToLowerAscii(component.substr(0, component.find('.')));
	if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul")
	{
		return true;
	}

	return stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && stem[3] >= '0' && stem[3] <= '9';
}

// Turns a manifest path into a relative path with backslashes. Only letters, digits, dots, underscores and
// hyphens form a component, and a component that Windows would rename or treat as a device is refused.
bool NormalizeManifestPath(const std::string& path, std::string& outPath)
{
	outPath.clear();
	if (path.empty() || path.size() > kMaxPathLength)
	{
		return false;
	}

	size_t depth = 0;
	std::string component;
	for (size_t i = 0; i <= path.size(); ++i)
	{
		const char c = i < path.size() ? path[i] : '/';
		if (c == '/' || c == '\\')
		{
			if (component.empty() || component == "." || component == ".." || component.back() == '.' || component.front() == '.' || IsReservedDeviceName(component))
			{
				return false;
			}

			if (++depth > kMaxPathDepth)
			{
				return false;
			}

			if (!outPath.empty())
			{
				outPath.push_back('\\');
			}

			outPath += component;
			component.clear();
			continue;
		}

		const bool bAllowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
		if (!bAllowed)
		{
			return false;
		}

		component.push_back(c);
	}

	return true;
}

bool DecodeSha256Hex(const std::string& hex, std::array<uint8_t, kSha256Size>& outHash)
{
	if (hex.size() != kSha256Size * 2)
	{
		return false;
	}

	for (size_t i = 0; i < kSha256Size; ++i)
	{
		uint8_t byte = 0;
		for (size_t j = 0; j < 2; ++j)
		{
			const char c = ToLowerAscii(hex[i * 2 + j]);
			uint8_t nibble = 0;
			if (c >= '0' && c <= '9')
			{
				nibble = static_cast<uint8_t>(c - '0');
			}
			else if (c >= 'a' && c <= 'f')
			{
				nibble = static_cast<uint8_t>(c - 'a' + 10);
			}
			else
			{
				return false;
			}

			byte = static_cast<uint8_t>((byte << 4) | nibble);
		}

		outHash[i] = byte;
	}

	return true;
}

bool IsVersionBelow(const PluginVersion& version, const PluginVersion& minimum)
{
	if (version.year != minimum.year)
	{
		return version.year < minimum.year;
	}

	if (version.month != minimum.month)
	{
		return version.month < minimum.month;
	}

	return version.patch < minimum.patch;
}

std::string FormatVersion(const PluginVersion& version)
{
	char buffer[48] = {};
	snprintf(buffer, sizeof(buffer), "%u.%u.%u", version.year, version.month, version.patch);
	return buffer;
}

// Opens and reads a small file and closes it again.
bool ReadSmallFile(const std::string& path, uint64_t maxSize, std::string& outData, std::string& outError)
{
	Platform::Handle file = Platform::OpenLockedFile(path, outError);
	if (file == nullptr)
	{
		return false;
	}

	const bool bSuccess = Platform::ReadWholeFile(file, maxSize, outData, outError);
	Platform::CloseHandle(file);
	if (!bSuccess)
	{
		outError = path + ": " + outError;
	}
	return bSuccess;
}

// Locks pluginsFolder and the plugin folder inside it.
bool LockPluginFolderChain(const std::string& pluginsFolder, const std::string& pluginName, std::vector<void*>& handles, std::string& outPluginFolder, std::string& outError)
{
	outPluginFolder = Platform::JoinPath(pluginsFolder, pluginName);
	const std::string chain[] = { pluginsFolder, outPluginFolder };
	for (const std::string& path : chain)
	{
		Platform::Handle folder = Platform::OpenLockedFolder(path, outError);
		if (folder == nullptr)
		{
			return false;
		}

		handles.push_back(folder);
	}

	return true;
}

// Parses the manifest and refuses duplicate keys, unknown keys and values of the wrong type.
bool ParseManifest(const std::string& text, const std::string& pluginName, uint32_t apiVersion, PluginVersion& outVersion, std::string& outVersionText, std::vector<ListedFile>& outFiles, std::string& outError)
{
	std::vector<std::vector<std::string>> objectKeys;
	bool bDuplicateKey = false;
	const nlohmann::json::parser_callback_t callback = [&objectKeys, &bDuplicateKey](int /*depth*/, nlohmann::json::parse_event_t event, nlohmann::json& parsed)
	{
		if (event == nlohmann::json::parse_event_t::object_start)
		{
			objectKeys.emplace_back();
		}
		else if (event == nlohmann::json::parse_event_t::object_end)
		{
			if (!objectKeys.empty())
			{
				objectKeys.pop_back();
			}
		}
		else if (event == nlohmann::json::parse_event_t::key && !objectKeys.empty() && parsed.is_string())
		{
			std::vector<std::string>& keys = objectKeys.back();
			const std::string& key = parsed.get_ref<const std::string&>();
			if (std::find(keys.begin(), keys.end(), key) != keys.end())
			{
				bDuplicateKey = true;
			}
			else
			{
				keys.push_back(key);
			}
		}
		return true;
	};

	const nlohmann::json manifest = nlohmann::json::parse(text, callback, false, false);
	if (manifest.is_discarded() || !manifest.is_object())
	{
		outError = "the manifest is not a JSON object";
		return false;
	}

	if (bDuplicateKey)
	{
		outError = "the manifest repeats a key";
		return false;
	}

	if (manifest.size() != 5 || !manifest.contains("format") || !manifest.contains("name") || !manifest.contains("version") || !manifest.contains("api_version") || !manifest.contains("files"))
	{
		outError = "the manifest needs exactly the keys format, name, version, api_version and files";
		return false;
	}

	const nlohmann::json& format = manifest["format"];
	if (!format.is_number_unsigned() || format.get<uint64_t>() != 1)
	{
		outError = "the manifest format is not 1";
		return false;
	}

	const nlohmann::json& name = manifest["name"];
	if (!name.is_string() || name.get_ref<const std::string&>() != pluginName)
	{
		outError = "the manifest names another plugin than " + pluginName;
		return false;
	}

	const nlohmann::json& version = manifest["version"];
	if (!version.is_string() || !ParsePluginVersion(version.get_ref<const std::string&>(), outVersion))
	{
		outError = "the manifest version is not of the form YYYY.MM.PATCH";
		return false;
	}

	outVersionText = version.get_ref<const std::string&>();

	const nlohmann::json& manifestApiVersion = manifest["api_version"];
	if (!manifestApiVersion.is_number_unsigned() || manifestApiVersion.get<uint64_t>() != apiVersion)
	{
		outError = "the manifest api_version is not " + std::to_string(apiVersion);
		return false;
	}

	const nlohmann::json& files = manifest["files"];
	if (!files.is_array() || files.empty() || files.size() > kMaxManifestFiles)
	{
		outError = "the manifest files list is empty, too long or not a list";
		return false;
	}

	outFiles.clear();
	for (const nlohmann::json& entry : files)
	{
		if (!entry.is_object() || entry.size() != 3 || !entry.contains("path") || !entry.contains("sha256") || !entry.contains("size") || !entry["path"].is_string() || !entry["sha256"].is_string())
		{
			outError = "a manifest file entry needs exactly the string keys path and sha256 and the integer key size";
			return false;
		}

		const std::string& rawPath = entry["path"].get_ref<const std::string&>();
		ListedFile file;
		if (!NormalizeManifestPath(rawPath, file.path))
		{
			outError = "the manifest path '" + rawPath + "' is not a plain relative path";
			return false;
		}

		file.key = ToLowerAscii(file.path);
		for (const ListedFile& other : outFiles)
		{
			if (other.key == file.key)
			{
				outError = "the manifest lists " + file.path + " twice";
				return false;
			}
		}

		if (!DecodeSha256Hex(entry["sha256"].get_ref<const std::string&>(), file.sha256))
		{
			outError = "the manifest sha256 of " + file.path + " is not 64 hex digits";
			return false;
		}

		const nlohmann::json& size = entry["size"];
		if (!size.is_number_unsigned())
		{
			outError = "the manifest size of " + file.path + " is not a non negative integer";
			return false;
		}

		file.size = size.get<uint64_t>();
		outFiles.push_back(file);
	}

	return true;
}

// Fails when the folder tree holds a link, a case sensitive folder, two names that differ only in case, or a
// file that is not one of the listed files that were hashed and stay locked. The manifest and its signature in
// the plugin folder itself are the only files that the manifest does not list. Every folder in the tree stays
// locked in handles.
bool CheckFolderMatchesManifest(const std::string& folder, const std::string& relativePrefix, size_t depth, const std::vector<ListedFile>& listedFiles, const std::vector<Platform::FileIdentity>& listedIdentities, std::vector<void*>& handles, size_t& entryCount, std::string& outError)
{
	if (depth > kMaxPathDepth)
	{
		outError = "the plugin folder nests deeper than " + std::to_string(kMaxPathDepth) + " folders";
		return false;
	}

	std::vector<Platform::FolderEntry> entries;
	if (!Platform::ListFolder(folder, kMaxFolderEntries, entries, outError))
	{
		return false;
	}

	std::set<std::string> foldedNames;
	for (const Platform::FolderEntry& entry : entries)
	{
		entryCount += 1;
		if (entryCount > kMaxFolderEntries)
		{
			outError = "the plugin folder holds more than " + std::to_string(kMaxFolderEntries) + " entries";
			return false;
		}

		const std::string relativePath = relativePrefix + entry.name;
		if (!foldedNames.insert(Platform::FoldName(entry.name)).second)
		{
			outError = "the plugin folder holds two entries named " + relativePath + " that differ only in case";
			return false;
		}

		if (entry.type == Platform::EntryType::Link)
		{
			outError = "the plugin folder holds the link " + relativePath;
			return false;
		}

		const std::string entryPath = Platform::JoinPath(folder, entry.name);
		if (entry.type == Platform::EntryType::Folder)
		{
			Platform::Handle subfolder = Platform::OpenLockedFolder(entryPath, outError);
			if (subfolder == nullptr)
			{
				return false;
			}

			handles.push_back(subfolder);
			if (!CheckFolderMatchesManifest(entryPath, relativePath + "\\", depth + 1, listedFiles, listedIdentities, handles, entryCount, outError))
			{
				return false;
			}
			continue;
		}

		const std::string key = ToLowerAscii(relativePath);
		if (depth == 0 && (key == "plugin.manifest" || key == "plugin.manifest.minisig"))
		{
			continue;
		}

		const auto listed = std::find_if(listedFiles.begin(), listedFiles.end(), [&key](const ListedFile& file) { return file.key == key; });
		if (listed == listedFiles.end())
		{
			outError = "the plugin folder holds the file " + relativePath + " that the manifest does not list";
			return false;
		}

		// The entry must be the very file that was hashed and stays locked, not another file under a similar name.
		if (!entry.bHasIdentity || !Platform::IsSameFile(entry.identity, listedIdentities[static_cast<size_t>(listed - listedFiles.begin())]))
		{
			outError = "the plugin folder entry " + relativePath + " is not the listed file that was hashed";
			return false;
		}
	}

	return true;
}
} // namespace

PluginFiles::~PluginFiles()
{
	Close();
}

void PluginFiles::Close()
{
	for (void* handle : m_handles)
	{
		Platform::CloseHandle(handle);
	}

	m_handles.clear();
	m_mainFile = nullptr;
	m_mainFilePath.clear();
	m_version.clear();
}

bool PluginFiles::OpenUnverifiedMainFile(const std::string& pluginsFolder, const std::string& pluginName, std::string& outError)
{
	Close();
	std::string pluginFolder;
	if (!LockPluginFolderChain(pluginsFolder, pluginName, m_handles, pluginFolder, outError))
	{
		Close();
		return false;
	}

	Platform::Handle file = Platform::OpenLockedFile(Platform::JoinPath(pluginFolder, pluginName + ".dll"), outError);
	if (file == nullptr)
	{
		Close();
		return false;
	}

	m_handles.push_back(file);
	m_mainFile = file;
	if (!Platform::GetFinalPath(file, m_mainFilePath))
	{
		outError = "cannot resolve the final path of " + pluginName + ".dll";
		Close();
		return false;
	}

	return true;
}

bool HasPublicKeys(std::span<const char* const> publicKeys)
{
	return std::any_of(publicKeys.begin(), publicKeys.end(), [](const char* szKey) { return szKey != nullptr && szKey[0] != '\0'; });
}

bool MayLoadUnverifiedPlugin(std::span<const char* const> publicKeys, bool bAllowUnsignedPlugins)
{
	return bAllowUnsignedPlugins && !HasPublicKeys(publicKeys);
}

bool VerifyMinisignSignature(const std::string& data, const std::string& signatureText, std::span<const char* const> publicKeys, std::string& outError)
{
	MinisignSignature signature;
	if (!DecodeSignature(signatureText, signature))
	{
		outError = "the signature file is not a minisign signature";
		return false;
	}

	if (signature.algorithm[0] != 'E' || signature.algorithm[1] != 'D')
	{
		outError = "the signature does not use the ED algorithm";
		return false;
	}

	std::array<uint8_t, kBlake2bSize> prehash = {};
	crypto_blake2b(prehash.data(), prehash.size(), reinterpret_cast<const uint8_t*>(data.data()), data.size());

	std::vector<uint8_t> globalData(signature.signature.begin(), signature.signature.end());
	globalData.insert(globalData.end(), signature.trustedComment.begin(), signature.trustedComment.end());

	// Two slots that carry the same key id are both tried, so a key that shares its id with another key still verifies.
	bool bKnownKey = false;
	for (const char* szKey : publicKeys)
	{
		if (szKey == nullptr || szKey[0] == '\0')
		{
			continue;
		}

		MinisignPublicKey key;
		if (!DecodePublicKey(szKey, key))
		{
			outError = "a compiled-in public key is not a minisign key";
			return false;
		}

		if (key.keyId != signature.keyId)
		{
			continue;
		}

		bKnownKey = true;
		if (crypto_ed25519_check(signature.signature.data(), key.key.data(), prehash.data(), prehash.size()) != 0)
		{
			outError = "the signature does not match the data";
			continue;
		}

		if (crypto_ed25519_check(signature.globalSignature.data(), key.key.data(), globalData.data(), globalData.size()) != 0)
		{
			outError = "the signature of the trusted comment does not match";
			continue;
		}

		return true;
	}

	if (!bKnownKey)
	{
		outError = "the signature comes from key " + KeyIdToHex(signature.keyId) + ", which this build does not trust";
	}

	return false;
}

bool ParsePluginVersion(const std::string& text, PluginVersion& outVersion)
{
	uint32_t parts[3] = {};
	size_t part = 0;
	size_t digits = 0;
	for (size_t i = 0; i <= text.size(); ++i)
	{
		const char c = i < text.size() ? text[i] : '.';
		if (c == '.')
		{
			if (digits == 0 || part >= 3)
			{
				return false;
			}

			++part;
			digits = 0;
			continue;
		}

		if (part >= 3 || c < '0' || c > '9' || ++digits > 9)
		{
			return false;
		}

		parts[part] = parts[part] * 10 + static_cast<uint32_t>(c - '0');
	}

	// The year has exactly four digits and the month lies between 1 and 12.
	if (part != 3 || text.find('.') != 4 || parts[1] < 1 || parts[1] > 12)
	{
		return false;
	}

	outVersion.year = parts[0];
	outVersion.month = parts[1];
	outVersion.patch = parts[2];
	return true;
}

bool VerifyPluginFolder(const std::string& pluginsFolder, const std::string& pluginName, uint32_t apiVersion, std::span<const char* const> publicKeys, std::span<const AllowedPlugin> allowedPlugins, PluginFiles& outFiles, std::string& outError)
{
	outFiles.Close();

	const auto allowed = std::find_if(allowedPlugins.begin(), allowedPlugins.end(), [&pluginName](const AllowedPlugin& entry) { return entry.pluginName != nullptr && pluginName == entry.pluginName; });
	if (allowed == allowedPlugins.end())
	{
		outError = "the plugin " + pluginName + " is not on the plugin allowlist of this build";
		return false;
	}

	// The folder chain is locked first, so no folder on the way to the files changes during the check or the load.
	PluginFiles files;
	std::string pluginFolder;
	if (!LockPluginFolderChain(pluginsFolder, pluginName, files.m_handles, pluginFolder, outError))
	{
		return false;
	}

	std::string manifestText;
	std::string signatureText;
	if (!ReadSmallFile(Platform::JoinPath(pluginFolder, "plugin.manifest"), kMaxManifestSize, manifestText, outError)
		|| !ReadSmallFile(Platform::JoinPath(pluginFolder, "plugin.manifest.minisig"), kMaxSignatureSize, signatureText, outError))
	{
		return false;
	}

	if (!VerifyMinisignSignature(manifestText, signatureText, publicKeys, outError))
	{
		outError = "plugin.manifest.minisig: " + outError;
		return false;
	}

	PluginVersion version = {};
	std::string versionText;
	std::vector<ListedFile> listedFiles;
	if (!ParseManifest(manifestText, pluginName, apiVersion, version, versionText, listedFiles, outError))
	{
		return false;
	}

	if (IsVersionBelow(version, allowed->minimumVersion))
	{
		outError = "the manifest version " + versionText + " is below the minimum version " + FormatVersion(allowed->minimumVersion) + " of this build";
		return false;
	}

	const std::string mainKey = ToLowerAscii(pluginName + ".dll");
	const bool bMainListed = std::any_of(listedFiles.begin(), listedFiles.end(), [&mainKey](const ListedFile& file) { return file.key == mainKey; });
	if (!bMainListed)
	{
		outError = "the manifest does not list " + pluginName + ".dll";
		return false;
	}

	std::vector<std::string> lockedFolderKeys;
	std::vector<Platform::FileIdentity> listedIdentities;
	for (const ListedFile& file : listedFiles)
	{
		// Every subfolder on the way to a listed file is locked before the file opens.
		for (size_t separator = file.path.find('\\'); separator != std::string::npos; separator = file.path.find('\\', separator + 1))
		{
			const std::string folderKey = file.key.substr(0, separator);
			if (std::find(lockedFolderKeys.begin(), lockedFolderKeys.end(), folderKey) != lockedFolderKeys.end())
			{
				continue;
			}

			Platform::Handle folder = Platform::OpenLockedFolder(Platform::JoinPath(pluginFolder, file.path.substr(0, separator)), outError);
			if (folder == nullptr)
			{
				return false;
			}

			files.m_handles.push_back(folder);
			lockedFolderKeys.push_back(folderKey);
		}

		Platform::Handle handle = Platform::OpenLockedFile(Platform::JoinPath(pluginFolder, file.path), outError);
		if (handle == nullptr)
		{
			outError = "the listed file " + file.path + ": " + outError;
			return false;
		}

		files.m_handles.push_back(handle);
		if (file.key == mainKey)
		{
			files.m_mainFile = handle;
		}

		Platform::FileIdentity identity;
		if (!Platform::GetFileIdentity(handle, identity))
		{
			outError = "cannot read the file id of " + file.path;
			return false;
		}

		listedIdentities.push_back(identity);

		// The size of the locked file must match the manifest before any byte of it is hashed.
		uint64_t fileSize = 0;
		if (!Platform::GetFileSize(handle, fileSize))
		{
			outError = "cannot read the size of " + file.path;
			return false;
		}

		if (fileSize != file.size)
		{
			outError = "the size of " + file.path + " is " + std::to_string(fileSize) + " bytes and not the manifest size " + std::to_string(file.size);
			return false;
		}

		std::array<uint8_t, kSha256Size> hash = {};
		if (!Platform::ComputeSha256(handle, hash))
		{
			outError = "cannot hash " + file.path;
			return false;
		}

		if (hash != file.sha256)
		{
			outError = "the SHA256 of " + file.path + " does not match the manifest";
			return false;
		}
	}

	size_t entryCount = 0;
	if (!CheckFolderMatchesManifest(pluginFolder, std::string(), 0, listedFiles, listedIdentities, files.m_handles, entryCount, outError))
	{
		return false;
	}

	std::string finalPath;
	if (!Platform::GetFinalPath(files.m_mainFile, finalPath))
	{
		outError = "cannot resolve the final path of " + pluginName + ".dll";
		return false;
	}

	outFiles.m_handles.swap(files.m_handles);
	outFiles.m_mainFile = files.m_mainFile;
	outFiles.m_mainFilePath = finalPath;
	outFiles.m_version = versionText;
	files.m_mainFile = nullptr;
	return true;
}
} // namespace GenOnlinePluginTrust
