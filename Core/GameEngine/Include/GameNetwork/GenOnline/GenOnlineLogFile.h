#pragma once

// Self-contained helpers for GeneralsOnline.log: size rotation, UTC timestamps and the session header system info

#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

inline uint64_t GetLogFileSize(const std::string& strPath)
{
	WIN32_FILE_ATTRIBUTE_DATA attributes = {};
	if (!GetFileAttributesExA(strPath.c_str(), GetFileExInfoStandard, &attributes))
	{
		return 0;
	}
	return (static_cast<uint64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
}

// GeneralsOnline.log -> GeneralsOnline.1.log
inline std::string GetRotatedLogFileName(const std::string& strPath)
{
	const size_t dotPos = strPath.find_last_of('.');
	const size_t slashPos = strPath.find_last_of("\\/");
	if (dotPos == std::string::npos || (slashPos != std::string::npos && dotPos < slashPos))
	{
		return strPath + ".1";
	}
	return strPath.substr(0, dotPos) + ".1" + strPath.substr(dotPos);
}

// replaces the single backup with the current log, false leaves both files as they were
inline bool RotateLogFile(const std::string& strPath)
{
	return MoveFileExA(strPath.c_str(), GetRotatedLogFileName(strPath).c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

// ISO 8601 UTC with milliseconds, e.g. 2026-11-07T08:15:02.123Z
inline std::string FormatUtcTimestamp(const FILETIME& fileTime)
{
	SYSTEMTIME st = {};
	if (!FileTimeToSystemTime(&fileTime, &st))
	{
		return "0000-00-00T00:00:00.000Z";
	}
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	return buffer;
}

inline std::string GetUtcTimestampNow()
{
	FILETIME now = {};
	GetSystemTimeAsFileTime(&now);
	return FormatUtcTimestamp(now);
}

inline std::string WideToUtf8ForLog(const wchar_t* text)
{
	const int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 1)
	{
		return std::string();
	}
	std::string result(len - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), len, nullptr, nullptr);
	return result;
}

// local UTC offset as +HH:MM
inline std::string GetLocalUtcOffsetString()
{
	FILETIME utcNow = {};
	FILETIME localNow = {};
	GetSystemTimeAsFileTime(&utcNow);
	if (!FileTimeToLocalFileTime(&utcNow, &localNow))
	{
		return "unknown";
	}
	const int64_t utcTicks = (static_cast<int64_t>(utcNow.dwHighDateTime) << 32) | utcNow.dwLowDateTime;
	const int64_t localTicks = (static_cast<int64_t>(localNow.dwHighDateTime) << 32) | localNow.dwLowDateTime;
	const int64_t offsetMinutes = (localTicks - utcTicks) / (10000000LL * 60);
	const int64_t absMinutes = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "%c%02d:%02d", offsetMinutes < 0 ? '-' : '+', static_cast<int>(absMinutes / 60), static_cast<int>(absMinutes % 60));
	return buffer;
}

inline std::string GetProcessStartUtcTimestamp()
{
	FILETIME creationTime = {};
	FILETIME exitTime = {};
	FILETIME kernelTime = {};
	FILETIME userTime = {};
	if (!GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime))
	{
		return GetUtcTimestampNow();
	}
	return FormatUtcTimestamp(creationTime);
}

inline std::string GetExecutableFileNameForLog()
{
	wchar_t path[MAX_PATH] = {};
	const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (len == 0 || len >= MAX_PATH)
	{
		return "unknown";
	}
	const wchar_t* fileName = path;
	for (const wchar_t* p = path; *p != L'\0'; ++p)
	{
		if (*p == L'\\' || *p == L'/')
		{
			fileName = p + 1;
		}
	}
	return WideToUtf8ForLog(fileName);
}

inline std::string GetWindowsVersionForLog()
{
	typedef LONG(WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
	HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
	RtlGetVersionFn pRtlGetVersion = hNtdll != nullptr ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(hNtdll, "RtlGetVersion")) : nullptr;
	RTL_OSVERSIONINFOW versionInfo = {};
	versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
	if (pRtlGetVersion == nullptr || pRtlGetVersion(&versionInfo) != 0)
	{
		return "unknown";
	}

	DWORD ubr = 0;
	DWORD ubrSize = sizeof(ubr);
	if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &ubrSize) != ERROR_SUCCESS)
	{
		ubr = 0;
	}

	char buffer[64];
	snprintf(buffer, sizeof(buffer), "%lu.%lu build %lu.%lu", versionInfo.dwMajorVersion, versionInfo.dwMinorVersion, versionInfo.dwBuildNumber, ubr);
	return buffer;
}

// empty when not running under Wine
inline std::string GetWineVersionForLog()
{
	typedef const char*(CDECL* WineGetVersionFn)();
	HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
	WineGetVersionFn pWineGetVersion = hNtdll != nullptr ? reinterpret_cast<WineGetVersionFn>(GetProcAddress(hNtdll, "wine_get_version")) : nullptr;
	if (pWineGetVersion == nullptr)
	{
		return std::string();
	}
	const char* version = pWineGetVersion();
	return version != nullptr ? std::string(version) : std::string("unknown");
}

inline std::string GetProcessBitnessForLog()
{
#if defined(_WIN64)
	return "64-bit process";
#else
	BOOL isWow64 = FALSE;
	if (IsWow64Process(GetCurrentProcess(), &isWow64) && isWow64)
	{
		return "32-bit process on 64-bit Windows";
	}
	return "32-bit process on 32-bit Windows";
#endif
}

inline std::string GetCpuBrandForLog()
{
	int cpuInfo[4] = {};
	__cpuid(cpuInfo, 0x80000000);
	if (static_cast<unsigned int>(cpuInfo[0]) < 0x80000004)
	{
		return "unknown";
	}

	char brand[49] = {};
	for (int i = 0; i < 3; ++i)
	{
		__cpuid(cpuInfo, 0x80000002 + i);
		memcpy(brand + i * 16, cpuInfo, sizeof(cpuInfo));
	}

	std::string result(brand);
	const size_t first = result.find_first_not_of(' ');
	const size_t last = result.find_last_not_of(' ');
	if (first == std::string::npos)
	{
		return "unknown";
	}
	result = result.substr(first, last - first + 1);

	SYSTEM_INFO systemInfo = {};
	GetNativeSystemInfo(&systemInfo);
	return result + " (" + std::to_string(systemInfo.dwNumberOfProcessors) + " logical processors)";
}

inline std::string GetPhysicalMemoryForLog()
{
	MEMORYSTATUSEX memoryStatus = {};
	memoryStatus.dwLength = sizeof(memoryStatus);
	if (!GlobalMemoryStatusEx(&memoryStatus))
	{
		return "unknown";
	}
	return std::to_string(memoryStatus.ullTotalPhys / (1024ull * 1024ull)) + " MB";
}

inline std::string GetLocaleForLog()
{
	wchar_t localeName[LOCALE_NAME_MAX_LENGTH] = {};
	std::string strLocale = GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) > 0 ? WideToUtf8ForLog(localeName) : std::string("unknown");
	return strLocale + ", ANSI code page " + std::to_string(GetACP());
}

// system part of the session header, one "Label: value" entry per line
inline std::vector<std::string> GetSystemInfoLogLines()
{
	std::vector<std::string> lines;
	const std::string strWine = GetWineVersionForLog();
	lines.push_back("Executable: " + GetExecutableFileNameForLog());
	lines.push_back("Windows: " + GetWindowsVersionForLog());
	lines.push_back("Wine: " + (strWine.empty() ? std::string("no") : strWine));
	lines.push_back("Process: " + GetProcessBitnessForLog());
	lines.push_back("CPU: " + GetCpuBrandForLog());
	lines.push_back("Physical RAM: " + GetPhysicalMemoryForLog());
	lines.push_back("Locale: " + GetLocaleForLog());
	lines.push_back("Process start (UTC): " + GetProcessStartUtcTimestamp());
	lines.push_back("Local UTC offset: " + GetLocalUtcOffsetString());
	return lines;
}
