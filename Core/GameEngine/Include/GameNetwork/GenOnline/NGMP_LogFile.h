#pragma once

// Self-contained helpers for GeneralsOnline.log: size rotation and UTC timestamps

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>

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

// replaces the single backup with the current log; false leaves both files as they were
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
