#pragma once
#include <string>

// The rules for what a crash report may hold. Crash reports never hold a display name.
namespace GenOnlineCrashReport
{
	// Returns the CRC mismatch details without player names. A player line names the player between the slot and the CRC, the
	// result keeps the slot and the CRC only. The summary lines stay as they are and every line in another shape is dropped.
	inline std::wstring WithoutPlayerNames(const std::wstring& details)
	{
		static const wchar_t* const kSummaryPrefixes[] = { L"GameLogic frame ", L"Had ", L"Mismatched Players:" };
		std::wstring result;
		size_t start = 0;
		while (start < details.size())
		{
			size_t end = details.find(L'\n', start);
			const bool bNewline = end != std::wstring::npos;
			if (!bNewline)
			{
				end = details.size();
			}
			std::wstring line = details.substr(start, end - start);
			start = end + 1;

			bool bKeep = false;
			if (line.compare(0, 7, L"player ") == 0)
			{
				const size_t open = line.find(L" (");
				const size_t close = line.rfind(L") = ");
				if (open != std::wstring::npos && close != std::wstring::npos && close > open)
				{
					line.erase(open, close + 1 - open);
					bKeep = true;
				}
			}
			else
			{
				for (const wchar_t* prefix : kSummaryPrefixes)
				{
					if (line.compare(0, std::char_traits<wchar_t>::length(prefix), prefix) == 0)
					{
						bKeep = true;
						break;
					}
				}
			}

			if (bKeep)
			{
				result += line;
				if (bNewline)
				{
					result += L'\n';
				}
			}
		}
		return result;
	}
}
