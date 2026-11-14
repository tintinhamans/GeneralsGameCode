#include "GameNetwork/GenOnline/GenOnlineModeration.h"

#include "GameNetwork/GenOnline/GenOnlineInclude.h"
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameNetwork/GenOnline/GenOnlineLobbyInterface.h"
#include "GameNetwork/GenOnline/GenOnlineRoomsInterface.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"
#include "GameNetwork/GenOnline/GenOnlineMainMenuUtils.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/GenOnlineMenus.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeNoticeText.h"

#include <chrono>
#include <map>
#include <stdexcept>
#include <windows.h>
#include <shellapi.h>

namespace
{
	enum class EModerationDialogContext
	{
		LOGIN,
		ACTIVE_SESSION
	};

	void LeaveLoginScreen()
	{
		TheShell->pop();
	}

	void OpenDiscord()
	{
		ShellExecuteA(nullptr, "open", "https://discord.playgenerals.online", nullptr, nullptr, SW_SHOWNORMAL);
	}

	void OpenDiscordFromLogin()
	{
		OpenDiscord();
		LeaveLoginScreen();
	}

	std::wstring GetNormalizedReason(const std::string& reason)
	{
		try
		{
			return NormalizeSingleLineText(from_utf8(reason));
		}
		catch (const std::range_error&)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[MODERATION]: Ignoring an invalid UTF-8 reason");
			return std::wstring();
		}
	}

	void AppendReason(std::wstring& message, const std::string& reason)
	{
		const std::wstring normalizedReason = GetNormalizedReason(reason);
		if (!normalizedReason.empty())
		{
			message += L"\n\nReason: ";
			message += normalizedReason;
		}
	}

	void ShowModerationDialog(
		UnicodeString title,
		std::wstring message,
		const std::string& reason,
		EModerationDialogContext context)
	{
		AppendReason(message, reason);
		message += L"\n\nVisit Discord for more information or support.";

		GameWinMsgBoxFunc discordCallback = OpenDiscord;
		GameWinMsgBoxFunc closeCallback = nullptr;
		if (context == EModerationDialogContext::LOGIN)
		{
			discordCallback = OpenDiscordFromLogin;
			closeCallback = LeaveLoginScreen;
		}

		GSMessageBoxOkCancelWithLabels(
			title,
			UnicodeString(message.c_str()),
			UnicodeString(L"Discord"),
			UnicodeString(L"Close"),
			discordCallback,
			closeCallback);
	}

	void ShowBanDialog(const OnlineBanDetails& ban, EModerationDialogContext context)
	{
		ShowModerationDialog(
			UnicodeString(L"Banned"),
			from_utf8(GenOnlineRealtime::BanDialogText(ban.expiresAt, ban.bKnown, GenOnlineLocalTimeText)),
			ban.reason,
			context);
	}

	void ShowKickDialog(const std::string& reason)
	{
		ShowModerationDialog(
			UnicodeString(L"Kicked"),
			L"You have been kicked from Generals Online.",
			reason,
			EModerationDialogContext::ACTIVE_SESSION);
	}
}

void ShowLoginBanDialog(const OnlineBanDetails& ban)
{
	// TheSuperHackers @feature arcticdolphin 09/11/2026 The ban notice ends a sign-in in the dialog, which closes first; its buttons then have no screen to leave.
	if (GOSignInDialogIsOpen())
	{
		GenOnlineCancelPatchCheckCallbackAndReopenDropdown();
		ShowBanDialog(ban, EModerationDialogContext::ACTIVE_SESSION);
		return;
	}

	ShowBanDialog(ban, EModerationDialogContext::LOGIN);
}

void ShowChatRateLimitNotice(const std::string& reason, const std::string& scopeType)
{
	using namespace std::chrono;

	const std::wstring normalizedReason = GetNormalizedReason(reason);
	if (normalizedReason.empty())
	{
		return;
	}

	using NoticeKey = std::pair<std::string, std::wstring>;
	static std::map<NoticeKey, steady_clock::time_point> lastNotices;

	const auto now = steady_clock::now();
	const auto window = seconds(9);
	for (auto it = lastNotices.begin(); it != lastNotices.end();)
	{
		if (now - it->second >= window)
		{
			it = lastNotices.erase(it);
		}
		else
		{
			++it;
		}
	}

	const NoticeKey key(scopeType, normalizedReason);
	if (lastNotices.find(key) != lastNotices.end())
	{
		return;
	}
	lastNotices[key] = now;

	UnicodeString message(normalizedReason.c_str());
	const Color color = DetermineSystemNoticeColor(true, false);
	if (scopeType == "lobby")
	{
		GenOnlineLobbyInterface* lobbyInterface =
			GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
		if (lobbyInterface != nullptr && lobbyInterface->m_OnChatCallback != nullptr)
		{
			lobbyInterface->m_OnChatCallback(message, color);
		}
		return;
	}

	GenOnlineRoomsInterface* roomsInterface =
		GenOnlineServicesManager::GetInterface<GenOnlineRoomsInterface>();
	if (roomsInterface != nullptr && roomsInterface->m_OnChatCallback != nullptr)
	{
		roomsInterface->m_OnChatCallback(message, color);
	}
}

void HandleModerationDisconnect(EOnlineModerationAction action, const std::string& reason)
{
	GenOnlineServicesManager* manager = GenOnlineServicesManager::GetInstance();
	if (manager == nullptr || IsModerationTeardownReason(manager->GetTeardownReason()))
	{
		return;
	}

	switch (action)
	{
	case EOnlineModerationAction::BAN:
	{
		// Without the expiry the dialog does not say how long the ban lasts.
		OnlineBanDetails ban;
		ban.reason = reason;
		ban.bKnown = false;
		manager->SetPendingFullTeardown(EGOTearDownReason::MODERATION_BAN);
		ShowBanDialog(ban, EModerationDialogContext::ACTIVE_SESSION);
		break;
	}

	case EOnlineModerationAction::KICK:
		manager->SetPendingFullTeardown(EGOTearDownReason::MODERATION_KICK);
		ShowKickDialog(reason);
		break;
	}
}

void HandleModerationBan(const OnlineBanDetails& ban)
{
	GenOnlineServicesManager* manager = GenOnlineServicesManager::GetInstance();
	if (manager == nullptr || IsModerationTeardownReason(manager->GetTeardownReason()))
	{
		return;
	}
	manager->SetPendingFullTeardown(EGOTearDownReason::MODERATION_BAN);
	ShowBanDialog(ban, EModerationDialogContext::ACTIVE_SESSION);
}
