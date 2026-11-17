#pragma once
#include "GenOnlineTypes.h"
#include "GameNetwork/GenOnline/GenOnlineSignIn.h"

#include <memory>
#include <vector>

// TheSuperHackers @feature arcticdolphin 08/11/2026 Where the login is, for the GO sign-in screen.
enum class ELoginStage
{
	None,
	SavedLogin,
	WaitingForBrowser,
	Finishing,
	Failed
};

class GenOnlineAuthInterface
{
public:

	// TheSuperHackers @feature arcticdolphin 08/11/2026 With the sign-in screen up, the login reports its stage instead of showing message boxes.
	void SetSignInScreen(bool bSignInScreen) { m_bSignInScreen = bSignInScreen; }
	ELoginStage GetLoginStage() const { return m_loginStage; }
	bool HasDisplayName() const { return !m_strDisplayName.empty() && m_strDisplayName != "NO_USER"; }

	// TheSuperHackers @feature arcticdolphin 09/11/2026 The sign-in dialog shows a failed login in place and closes itself, so failures are reported to its callback instead of popping a screen.
	void SetSignInDialog(bool bSignInDialog) { m_bSignInDialog = bSignInDialog; }
	bool ReportLoginFailure(const wchar_t* szTitle, const wchar_t* szText);
	const std::wstring& GetLoginFailureTitle() const { return m_strLoginFailureTitle; }
	const std::wstring& GetLoginFailureText() const { return m_strLoginFailureText; }

	void UseAnotherAccount();
	void CancelLogin();

	std::string GetDisplayName()
	{
		return m_strDisplayName;
	}

	std::wstring GetDisplayNameW()
	{
		return from_utf8(m_strDisplayName);
	}

	int64_t GetUserID() const { return m_userID; }

	// The realtime hub reports the name in Ready and after a rename.
	void OnDisplayNameChanged(const std::string& strDisplayName)
	{
		if (!strDisplayName.empty())
		{
			m_strDisplayName = strDisplayName;
		}
	}

	void GoToDetermineNetworkCaps();

	// Links the account at the provider that the plugin reports, with the provider's own proof of that account.
	void SendMiddlewareToken(const std::string& strProvider, const std::string& strProof);

	// Mints the short-lived token the plugin of the provider signs in to its platform with. The callback gets an empty token on failure, and does not run once the services tear down.
	void RequestSignInToken(const std::string& strProvider, std::function<void(const std::string& strToken)> fnDone);

	void BeginLogin();
	void BeginLoginOnSelectedRoute();

	void Tick();

	void OnLoginComplete(ELoginResult loginResult, const char* szWSAddr);

	void RegisterForLoginCallback(std::function<void(ELoginResult)> callback)
	{
		m_cb_LoginPendingCallback = callback;
	}

	void DeregisterForLoginCallback()
	{
		m_cb_LoginPendingCallback = nullptr;
	}

	std::string& GetAuthToken() { return m_strToken; }

	bool IsLoggedIn() const
	{
		return m_userID != -1 && !m_strToken.empty();
	}

	// Ends the session on the service and revokes the refresh token. The done callback runs once both answered or the logout timed out.
	// The online services must stay up until then, because their teardown cancels the requests.
	// The done callback runs inside the HTTP tick, so it only sets flags and never tears the services down itself.
	void LogoutOfMyAccount(std::function<void()> fnDone);

	static std::string GetCredentialsFilePath();

	// Refreshes the tokens and registers the session again, for a realtime hub that refused or revoked the session.
	void RecoverRealtimeSession(std::function<void(bool bRecovered)> fnDone);

	// A refused realtime upgrade carries no ban body, so the session registration, which the ban refuses
	// with the same body, reads it. Then the session ends with the ban dialog.
	void ReportRealtimeBan();

	// The origin of the realtime hub and the v1 REST endpoints, or empty without a trusted route.
	static std::string ServiceOriginV1();

private:
	void LoginAsSecondaryDevAccount();

	bool GetCredentials();
	void DeleteSavedCredentials();

	static std::string CredentialServerKey();

	void BeginLoginV1();
	// promptLogin shows the sign-in form even when the browser is still signed in: no saved login, or the player chose another account.
	void StartBrowserSignInV1(int loginAttempt, bool promptLogin);
	void CompleteSignInV1(const GenOnlineRealtime::TokenResponse& tokens, int loginAttempt);
	void ApplyTokensV1(const GenOnlineRealtime::TokenResponse& tokens);
	void RegisterSessionV1(std::function<void(int statusCode, const std::string& strBody)> fnDone);
	void RefreshTokensV1(std::function<void(bool bRefreshed)> fnDone);
	void FailLoginV1(const wchar_t* szText);
	void ClearSessionV1();
	void AbandonRefreshV1();
	void FinishLogoutV1();
	void TickV1();
	static std::string CredentialServerKeyV1();

private:
	bool m_bSignInScreen = false;
	bool m_bSignInDialog = false;
	std::wstring m_strLoginFailureTitle;
	std::wstring m_strLoginFailureText;
	ELoginStage m_loginStage = ELoginStage::None;
	// TheSuperHackers @feature arcticdolphin 08/11/2026 Bumped by each login start so replies to an abandoned attempt are dropped.
	int m_loginAttempt = 0;

	std::string m_strToken = std::string();
	int64_t m_userID = -1;
	std::string m_strDisplayName = "NO_USER";

	std::function<void(ELoginResult)> m_cb_LoginPendingCallback = nullptr;

	std::string m_strRefreshToken = std::string();

	// if a refresh fails we still have ~5m before the token actually expires, so retry quickly rather than waiting for the next scheduled refresh
	const int m_secondsUntilRefreshRetry = 30;

	// Realtime protocol v1 sign-in. The access token lives in m_strToken and the rotating refresh token in m_strRefreshToken.
	std::unique_ptr<GenOnlineSignIn> m_pSignIn = std::make_unique<GenOnlineSignIn>();
	int64_t m_accessTokenExpiry = -1;
	int64_t m_accessTokenRefreshAt = -1;
	int64_t m_nextRefreshRetryV1 = -1;
	bool m_bRefreshInFlightV1 = false;
	std::vector<std::function<void(bool)>> m_refreshWaitersV1;
	std::function<void()> m_logoutDoneV1;
	int64_t m_logoutDeadlineV1 = -1;
};
