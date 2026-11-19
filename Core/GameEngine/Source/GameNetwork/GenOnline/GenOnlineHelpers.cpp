#include "GameNetwork/GenOnline/GenOnlineInclude.h"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <ctime>
#include <mutex>
#include <string>
#include "../GenOnlineInit.h"
#include "../GenOnlineAuth.h"
#include "GameNetwork/GenOnline/NGMP_LogFile.h"
#include "gitinfo.h"

std::string m_strNetworkLogFileName;
std::mutex m_logMutex;

// one log across sessions, moved to GeneralsOnline.1.log once it grows past the limit
static constexpr uint64_t NETWORK_LOG_MAX_BYTES = 10ull * 1024ull * 1024ull;
static constexpr const char* NETWORK_LOG_SESSION_SEPARATOR = "==================== Generals Online session start ====================";
static constexpr const char* NETWORK_LOG_CONTINUED_SEPARATOR = "==================== Generals Online session continued after log rotation ====================";
static uint64_t m_networkLogBytes = 0;
static bool m_bNetworkLogRotateFailed = false;

static void AppendNetworkLogLine(std::string& strOut, const std::string& strLine)
{
	strOut += std::format("[{}] {}\n", GetUtcTimestampNow(), strLine);
}

static std::string GetBuildDescriptionForLog()
{
#if defined(_DEBUG)
	std::string strBuild = "Debug";
#elif defined(RTS_DEBUG)
	std::string strBuild = "Release (RTS_DEBUG)";
#else
	std::string strBuild = "Release";
#endif
	if (GenOnlineServicesManager::GetEnvironment() == GenOnlineServicesManager::EEnvironment::TEST)
	{
		strBuild += ", test environment";
	}
	if (GitHaveInfo)
	{
		strBuild += std::format(", commit {}{} from {}", static_cast<const char*>(GitShortSHA1), GitUncommittedChanges ? " (modified)" : "", static_cast<const char*>(GitCommitDate));
	}
	return strBuild;
}

// written under every session separator, never with secrets, paths or addresses
static void AppendNetworkLogSessionHeader(std::string& strOut)
{
	AppendNetworkLogLine(strOut, std::format("[Session] Client version: {} ({})", GENONLINE_VERSION_STRING, GENONLINE_CLIENT_ID));
	AppendNetworkLogLine(strOut, std::format("[Session] Sentry release: {}", GenOnlineServicesManager::GetSentryRelease()));
	AppendNetworkLogLine(strOut, std::format("[Session] Sentry: {}", GenOnlineServicesManager::GetSentryStatus()));
	AppendNetworkLogLine(strOut, std::format("[Session] Build: {}", GetBuildDescriptionForLog()));
	for (const std::string& strLine : GetSystemInfoLogLines())
	{
		AppendNetworkLogLine(strOut, "[Session] " + strLine);
	}
}

// a failed rotation keeps appending to the current file and retries on the next start
static bool RotateNetworkLogIfNeeded()
{
	if (m_bNetworkLogRotateFailed || m_networkLogBytes <= NETWORK_LOG_MAX_BYTES)
	{
		return false;
	}

	if (!RotateLogFile(m_strNetworkLogFileName))
	{
		m_bNetworkLogRotateFailed = true;
		return false;
	}
	m_networkLogBytes = GetLogFileSize(m_strNetworkLogFileName);
	return true;
}

extern GenOnlineGame* TheGenOnlineGame;

// Win32 conversions handle surrogate pairs and substitute U+FFFD for invalid input instead of throwing,
// so a malformed name from the server can't take down the UI
std::string to_utf8(const std::wstring& wstr)
{
	if (wstr.empty())
		return std::string();

	int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
	std::string result(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), result.data(), len, nullptr, nullptr);
	return result;
}

std::wstring from_utf8(const std::string& utf8_str)
{
	if (utf8_str.empty())
		return std::wstring();

	int len = MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(), (int)utf8_str.size(), nullptr, 0);
	std::wstring result(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(), (int)utf8_str.size(), result.data(), len);
	return result;
}

#ifndef WC_NO_BEST_FIT_CHARS
#define WC_NO_BEST_FIT_CHARS 0x00000400
#endif

// Process code page text (paths from local file APIs) to UTF-8 for the wire
std::string local_to_utf8(const std::string& local_str)
{
	if (local_str.empty() || GetACP() == CP_UTF8)
		return local_str;

	int wlen = MultiByteToWideChar(CP_ACP, 0, local_str.data(), (int)local_str.size(), nullptr, 0);
	if (wlen <= 0)
		return local_str;

	std::wstring wide(wlen, L'\0');
	MultiByteToWideChar(CP_ACP, 0, local_str.data(), (int)local_str.size(), wide.data(), wlen);
	return to_utf8(wide);
}

// UTF-8 from the wire to the process code page that local file APIs expect; unrepresentable characters become '_'
std::string utf8_to_local(const std::string& utf8_str)
{
	if (utf8_str.empty() || GetACP() == CP_UTF8)
		return utf8_str;

	std::wstring wide = from_utf8(utf8_str);
	if (wide.empty())
		return std::string();

	int len = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), (int)wide.size(), nullptr, 0, "_", nullptr);
	if (len <= 0)
		return utf8_str;

	std::string result(len, '\0');
	WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), (int)wide.size(), result.data(), len, "_", nullptr);
	return result;
}

// Legacy format strings take player names as narrow %hs arguments, which get widened byte by byte and mangle UTF-8.
// Rewriting %hs to %s lets callers pass a from_utf8() decoded name instead.
std::wstring WidenFormatSpecifiers(const std::wstring& format)
{
	std::wstring widened = format;
	for (size_t pos = widened.find(L"%hs"); pos != std::wstring::npos; pos = widened.find(L"%hs", pos + 2))
	{
		widened.erase(pos + 1, 1);
	}
	return widened;
}

std::wstring NormalizeSingleLineText(const std::wstring& text)
{
	std::wstring normalizedText;
	normalizedText.reserve(text.size());
	bool pendingSpace = false;

	for (wchar_t character : text)
	{
		if (std::iswspace(character) || std::iswcntrl(character))
		{
			pendingSpace = !normalizedText.empty();
			continue;
		}

		if (pendingSpace)
		{
			normalizedText += L' ';
			pendingSpace = false;
		}
		normalizedText += character;
	}

	return normalizedText;
}

void NetworkLog(ELogVerbosity logVerbosity, const char* fmt, ...)
{
	if (!GenOnlineServicesManager::Settings.Debug_VerboseLogging())
	{
		if (logVerbosity < g_LogVerbosity)
		{
			return;
		}
	}

	std::scoped_lock scopedLock { m_logMutex };

	std::string strOutput;
	if (m_strNetworkLogFileName.empty())
	{
#if defined(_DEBUG)
		// for debug, put the user ID in the log name so we can easily track it (NOte that this does mean we dont get very early logging, pre login, but that's OK normally)

		GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		if (pAuthInterface != nullptr && pAuthInterface->GetUserID() != -1)
		{
			m_strNetworkLogFileName = std::format("{}\\GeneralsOnlineData\\GeneralsOnline_UserID_{}.log", TheGlobalData->getPath_UserData().str(), pAuthInterface->GetUserID());
		}
		else
		{
			return;
		}
#else
		m_strNetworkLogFileName = std::format("{}\\GeneralsOnlineData\\GeneralsOnline.log", TheGlobalData->getPath_UserData().str());
#endif
		/*
			std::stringstream ss;

#if defined(_DEBUG)
		if (IsDebuggerPresent())
		{
			ss << std::put_time(std::localtime(&in_time_t),
				"GeneralsOnline_Debugger_%Y-%m-%d-%H-%M-%S.log");
		}
		else
		{
			ss << std::put_time(std::localtime(&in_time_t),
				"GeneralsOnline_%Y-%m-%d-%H-%M-%S.log");
		}
#else
		ss << "GeneralsOnline.log";
#endif
*/
		m_networkLogBytes = GetLogFileSize(m_strNetworkLogFileName);
		RotateNetworkLogIfNeeded();

		// log start msg
		if (m_networkLogBytes > 0)
		{
			strOutput += "\n";
		}
		AppendNetworkLogLine(strOutput, NETWORK_LOG_SESSION_SEPARATOR);
		AppendNetworkLogSessionHeader(strOutput);
	}
	else if (RotateNetworkLogIfNeeded())
	{
		AppendNetworkLogLine(strOutput, NETWORK_LOG_CONTINUED_SEPARATOR);
		AppendNetworkLogSessionHeader(strOutput);
	}

	char buffer[8192];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, 8192, fmt, args);
	buffer[8192 - 1] = 0;
	va_end(args);

	const std::string strTimestamp = GetUtcTimestampNow();
	std::string strLogBuffer = std::format("[{}] {}", strTimestamp, buffer);
	strOutput += strLogBuffer;
	strOutput += "\n";

	// TODO_NGMP: Keep open and flush regularly
	std::ofstream logFile;
	logFile.open(m_strNetworkLogFileName, std::ios_base::app);
	logFile << strOutput;
	logFile.close();

	// text mode writes each newline as CRLF
	m_networkLogBytes += strOutput.size() + std::count(strOutput.begin(), strOutput.end(), '\n');

#if defined(GENONLINE_BRANCH_JMARSHALL)
	DevConsole.AddLog(strLogBuffer.c_str());
#endif

	OutputDebugString(strLogBuffer.c_str());
	OutputDebugString("\n");
}

std::string Base64Encode(const std::vector<uint8_t>& data)
{
	static const char base64_chars[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	std::string encoded;
	size_t i = 0;
	uint32_t octet_a, octet_b, octet_c, triple;

	while (i < data.size())
	{
		octet_a = i < data.size() ? data[i++] : 0;
		octet_b = i < data.size() ? data[i++] : 0;
		octet_c = i < data.size() ? data[i++] : 0;

		triple = (octet_a << 16) | (octet_b << 8) | octet_c;

		encoded += base64_chars[(triple >> 18) & 0x3F];
		encoded += base64_chars[(triple >> 12) & 0x3F];
		encoded += (i >= data.size() + 1) ? '=' : base64_chars[(triple >> 6) & 0x3F];
		encoded += (i >= data.size())     ? '=' : base64_chars[triple & 0x3F];
	}

	return encoded;
}

std::vector<uint8_t> Base64Decode(const std::string& encodedData) {
	static const std::string base64Chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	auto isBase64 = [](unsigned char c) {
		return std::isalnum(c) || (c == '+') || (c == '/');
		};

	std::vector<uint8_t> decodedData;
	int inLen = encodedData.size();
	int i = 0;
	int in_ = 0;
	uint8_t charArray4[4], charArray3[3];

	while (inLen-- && (encodedData[in_] != '=') && isBase64(encodedData[in_])) {
		if (i >= 4) break;
		charArray4[i++] = encodedData[in_]; in_++;
		if (i == 4) {
			for (i = 0; i < 4; i++)
				charArray4[i] = base64Chars.find(charArray4[i]);

			charArray3[0] = (charArray4[0] << 2) + ((charArray4[1] & 0x30) >> 4);
			charArray3[1] = ((charArray4[1] & 0xf) << 4) + ((charArray4[2] & 0x3c) >> 2);
			charArray3[2] = ((charArray4[2] & 0x3) << 6) + charArray4[3];

			for (i = 0; i < 3; i++)
				decodedData.push_back(charArray3[i]);
			i = 0;
		}
	}

	if (i) {
		for (int j = i; j < 4; j++)
			charArray4[j] = 0;

		for (int j = 0; j < 4; j++)
			charArray4[j] = base64Chars.find(charArray4[j]);

		charArray3[0] = (charArray4[0] << 2) + ((charArray4[1] & 0x30) >> 4);
		charArray3[1] = ((charArray4[1] & 0xf) << 4) + ((charArray4[2] & 0x3c) >> 2);
		charArray3[2] = ((charArray4[2] & 0x3) << 6) + charArray4[3];

		for (int j = 0; j < i - 1; j++)
			decodedData.push_back(charArray3[j]);
	}

	return decodedData;
}

std::string getGameExeCRC()
{
    HMODULE hModule = GetModuleHandle(NULL);
    if (!hModule) return "";

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hModule;
    PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((BYTE*)hModule + dosHeader->e_lfanew);

    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeader);
    for (int i = 0; i < ntHeader->FileHeader.NumberOfSections; i++, section++) {
        if (strcmp((char*)section->Name, ".text") == 0) {
            BYTE* codeBase = (BYTE*)hModule + section->VirtualAddress;
            DWORD codeSize = section->Misc.VirtualSize;

            HCRYPTPROV hProv;
            HCRYPTHASH hHash;
            BYTE hash[32]; // SHA-256
            DWORD hashLen = sizeof(hash);

            if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
                CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash) &&
                CryptHashData(hHash, codeBase, codeSize, 0) &&
                CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {

                std::ostringstream oss;
                oss << std::hex << std::setfill('0');
                for (DWORD j = 0; j < hashLen; j++) {
                    oss << std::setw(2) << static_cast<int>(hash[j]);
                }

                CryptDestroyHash(hHash);
                CryptReleaseContext(hProv, 0);

                return oss.str(); // return as std::string
            }

            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
        }
    }
    return "";
}

int RoundUpLatencyToFrameInterval(int latency, int frameInterval)
{
	if (frameInterval == 0)
		return latency;

	int remainder = latency % frameInterval;
	if (remainder == 0)
		return latency;

	return latency + frameInterval - remainder;
}
int ConvertMSLatencyToFrames(int ms)
{
	ms = RoundUpLatencyToFrameInterval(ms, MSEC_PER_SECOND / LOGICFRAMES_PER_SECOND);
	return (int)ceil((ms / (float)MSEC_PER_SECOND) * (float)LOGICFRAMES_PER_SECOND);
}

#include <windows.h>
#include <iphlpapi.h>
#include <iostream>
#include <string>

#pragma comment(lib, "iphlpapi.lib")

// Helper: read MachineGuid from registry
std::string GetMachineGuid() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Cryptography",
        0, KEY_READ | KEY_WOW64_64KEY, &hKey) != ERROR_SUCCESS) {
        return "";
    }

    char buffer[256];
    DWORD bufferSize = sizeof(buffer);
    DWORD type = 0;
    if (RegQueryValueExA(hKey, "MachineGuid", nullptr, &type,
        reinterpret_cast<LPBYTE>(buffer), &bufferSize) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return std::string(buffer, bufferSize - 1).append(std::string("YY")); // remove null terminator
    }

    RegCloseKey(hKey);
    return "";
}

// Helper: get MAC address of first adapter
std::string GetPrimaryMacAddress() {
    PIP_ADAPTER_INFO adapterInfo;
    DWORD bufLen = sizeof(IP_ADAPTER_INFO);
    adapterInfo = (IP_ADAPTER_INFO*)malloc(bufLen);

    if (GetAdaptersInfo(adapterInfo, &bufLen) == ERROR_BUFFER_OVERFLOW) {
        free(adapterInfo);
        adapterInfo = (IP_ADAPTER_INFO*)malloc(bufLen);
    }

    if (GetAdaptersInfo(adapterInfo, &bufLen) == NO_ERROR) {
        char macAddr[32];
        sprintf_s(macAddr, "%02X:%02X:%02X:%02X:%02X:%02XZZ",
            adapterInfo->Address[0], adapterInfo->Address[1],
            adapterInfo->Address[2], adapterInfo->Address[3],
            adapterInfo->Address[4], adapterInfo->Address[5]);
        free(adapterInfo);
        return std::string(macAddr);
    }

    free(adapterInfo);
    return "";
}

// Helper: get Volume Serial Number of C: drive
std::string GetVolumeSerial() {
    DWORD serialNumber = 0;
    if (GetVolumeInformationA("C:\\", nullptr, 0, &serialNumber,
        nullptr, nullptr, nullptr, 0)) {
        char serialStr[32];
        sprintf_s(serialStr, "%08XZZ", serialNumber);
        return std::string(serialStr);
    }
    return "";
}
