#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"

#include "GameNetwork/GenOnline/HTTP/HTTPManager.h"
#include "GameNetwork/GenOnline/HTTP/HTTPRequest.h"
#include "GameNetwork/GenOnline/GenOnlineModeration.h"
#include "GameNetwork/GenOnline/GenOnlineRealtimeBridge.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeRest.h"
#include "GameNetwork/GenOnline/json.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include <windows.h>
#include "GameNetwork/GenOnline/CredentialStore.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "Common/GameEngine.h"
#include "../json.hpp"

#pragma comment(lib, "Crypt32.lib")

#if defined(USE_TEST_ENV)
#define CREDENTIALS_FILENAME "credentials_env_test.json"
#elif !defined(DEBUG) || defined(USE_DEBUG_ON_LIVE_SERVER)
#define CREDENTIALS_FILENAME "credentials.json"
#endif

#include <curl/curl.h>
#include "GameClient/ClientInstance.h"

void GenOnlineAuthInterface::GoToDetermineNetworkCaps()
{
	// GET the service config and its MOTD
	const int loginAttempt = m_loginAttempt;
	GenOnlineRealtimeBridge::CallRest(GenOnlineRealtimeBridge::RestMethod::Get, GenOnlineRealtime::RestConfigPath(), std::string(), [=](const GenOnlineRealtime::RestOutcome& outcome)
		{
			// TheSuperHackers @bugfix arcticdolphin 09/11/2026 The player cancelled the sign-in while it was finishing.
			if (loginAttempt != m_loginAttempt)
			{
				return;
			}

			try
			{
				GenOnlineRealtime::ConfigResponse config;
				if (!outcome.Data(config))
				{
					throw std::runtime_error("no config");
				}

				GenOnlineServicesManager::GetInstance()->ProcessMOTD(config.motd.c_str());

				ELoginResult loginResult = ELoginResult::Success;

				// WS should be connected by this point
				std::shared_ptr<WebSocket>  pWS = GenOnlineServicesManager::GetWebSocket();
				bool bWSConnected = pWS == nullptr ? false : pWS->IsConnected();
				if (!bWSConnected)
				{
					loginResult = ELoginResult::Failed;
					// TheSuperHackers @bugfix arcticdolphin 09/11/2026 The stage shows the failure, so Cancel is gone while the error box or dialog step is up.
					m_loginStage = ELoginStage::Failed;
				}

				// NOTE: Don't need to get stats here, PopulatePlayerInfoWindows is called as part of going to MP...
				// cache our local stats 
				// 
				// go to next screen
				ClearGSMessageBoxes();

				if (m_cb_LoginPendingCallback != nullptr)
				{
					m_cb_LoginPendingCallback(loginResult);
				}


			}
			catch (...)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "MOTD: Failed to parse response");

				// if MOTD was bad, still proceed, its a soft error
				GenOnlineServicesManager::GetInstance()->ProcessMOTD("Error retrieving MOTD");

				ELoginResult loginResult = ELoginResult::Success;

				// WS should be connected by this point
				std::shared_ptr<WebSocket>  pWS = GenOnlineServicesManager::GetWebSocket();;
				bool bWSConnected = pWS == nullptr ? false : pWS->IsConnected();
				if (!bWSConnected)
				{
					loginResult = ELoginResult::Failed;
					// TheSuperHackers @bugfix arcticdolphin 09/11/2026 The stage shows the failure, so Cancel is gone while the error box or dialog step is up.
					m_loginStage = ELoginStage::Failed;
				}

				// NOTE: Don't need to get stats here, PopulatePlayerInfoWindows is called as part of going to MP...
				// cache our local stats 
				// 
				// go to next screen
				ClearGSMessageBoxes();

				if (m_cb_LoginPendingCallback != nullptr)
				{
					m_cb_LoginPendingCallback(loginResult);
				}
			}
		});
}

void GenOnlineAuthInterface::RequestSignInToken(const std::string& strProvider, std::function<void(const std::string& strToken)> fnDone)
{
	GenOnlineRealtimeBridge::CallRest(GenOnlineRealtimeBridge::RestMethod::Post, GenOnlineRealtime::RestSignInTokenPath(strProvider), "", [fnDone, strProvider](const GenOnlineRealtime::RestOutcome& outcome)
		{
			GenOnlineRealtime::SignInTokenResponse token;
			if (!outcome.Succeeded() || !outcome.Data(token) || token.token.empty())
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Sign in token request for %s failed: status %d code %s", strProvider.c_str(), outcome.status, outcome.error.code.c_str());
				fnDone(std::string());
				return;
			}
			// The token is a credential, so only its length and lifetime are logged.
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Got a sign in token for %s (%zu chars, %d s)", strProvider.c_str(), token.token.size(), token.expires_in);
			fnDone(token.token);
		});
}

void GenOnlineAuthInterface::SendMiddlewareToken(const std::string& strProvider, const std::string& strProof)
{
    // login
    GenOnlineRealtime::LinkAccountBody body;
    body.proof = strProof;

    GenOnlineRealtimeBridge::CallRest(GenOnlineRealtimeBridge::RestMethod::Put, GenOnlineRealtime::RestLinkedAccountPath(strProvider), nlohmann::json(body).dump(), [](const GenOnlineRealtime::RestOutcome& outcome)
        {
            if (!outcome.Succeeded())
            {
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] MW LOGIN: Failed: status %d code %s", outcome.status, outcome.error.code.c_str());
				GenOnlineServicesManager* pManager = GenOnlineServicesManager::GetInstance();
				if (pManager != nullptr)
				{
					pManager->SetPendingFullTeardown(EGOTearDownReason::MIDDLEWARE_LOGIN_FAILED);
				}
            }
            else
            {
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] MW LOGIN: Logged in");
            }

        });
}

bool GenOnlineAuthInterface::ReportLoginFailure(const wchar_t* szTitle, const wchar_t* szText)
{
	if (!m_bSignInDialog)
	{
		return false;
	}

	m_loginStage = ELoginStage::Failed;
	m_strLoginFailureTitle = szTitle;
	m_strLoginFailureText = szText;
	if (m_cb_LoginPendingCallback != nullptr)
	{
		m_cb_LoginPendingCallback(ELoginResult::Failed);
	}
	return true;
}

void GenOnlineAuthInterface::BeginLogin()
{
	auto* manager = GenOnlineServicesManager::GetInstance();
	if (!manager)
	{
		return;
	}
	m_strLoginFailureTitle.clear();
	m_strLoginFailureText.clear();
	manager->EnsureServiceRoute([](bool success)
	{
		auto* auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		if (!auth)
		{
			return;
		}
		if (success)
		{
			auth->BeginLoginOnSelectedRoute();
		}
		else
		{
			ClearGSMessageBoxes();
			const bool proxyAuth = GenOnlineServicesManager::GetInstance() != nullptr && GenOnlineServicesManager::GetInstance()->RouteFailedProxyAuth();
			const wchar_t* szText = proxyAuth ? L"The proxy did not accept the login. Try again, or check the proxy settings in Options." : L"No trusted service route could be reached. Check Connection settings in Options, then try again.";
			if (auth->ReportLoginFailure(L"Connection failed", szText))
			{
				return;
			}
			GSMessageBoxOk(UnicodeString(L"Connection failed"), UnicodeString(proxyAuth ? L"The proxy did not accept the login. Try again, or check the proxy settings in Options." : L"No trusted service route could be reached. Check Connection settings in Options, then try again."), []() { TheShell->pop(); });
		}
	});
}

void GenOnlineAuthInterface::BeginLoginOnSelectedRoute()
{
	BeginLoginV1();
}

void GenOnlineAuthInterface::Tick()
{
	TickV1();
}

void GenOnlineAuthInterface::UseAnotherAccount()
{
	if (m_loginStage != ELoginStage::SavedLogin)
	{
		return;
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Forgetting the saved login, the player chose another account");
	++m_loginAttempt;
	DeleteSavedCredentials();
	m_strRefreshToken = std::string();
	StartBrowserSignInV1(m_loginAttempt);
}

void GenOnlineAuthInterface::CancelLogin()
{
	// TheSuperHackers @feature arcticdolphin 08/11/2026 Drop the replies still on their way and stop polling, so a cancelled login never completes later.
	++m_loginAttempt;
	m_pSignIn->Cancel();
	AbandonRefreshV1();

	if (GenOnlineServicesManager::GetInstance() != nullptr)
	{
		GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::USER_REQUESTED_SILENT);

		// TheSuperHackers @bugfix arcticdolphin 09/11/2026 Nothing on the main menu acts on the pending teardown, so tear down now; this also closes a websocket a finishing login opened.
		GenOnlineServicesManager::GetInstance()->ConsumePendingFullTeardown();
		TearDownGeneralsOnline();
	}

	OnLoginComplete(ELoginResult::UserCancelled, "");
}

void GenOnlineAuthInterface::OnLoginComplete(ELoginResult loginResult, const char* szWSAddr)
{
	m_loginStage = loginResult == ELoginResult::Success ? ELoginStage::Finishing : (loginResult == ELoginResult::Failed ? ELoginStage::Failed : ELoginStage::None);

	if (loginResult == ELoginResult::Success)
	{
		GenOnlineServicesManager::UpdateSentryUser();
		NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Logged in as user %lld", GetUserID());

		// TODO_AC: Consider chaining this
		// login to AC
		GenOnlinePluginHost::LinkedAccount::SignIn();

		const int loginAttempt = m_loginAttempt;
		GenOnlineServicesManager::GetInstance()->OnLogin(loginResult, szWSAddr, [=]() // wait for WS to connect
			{
				// TheSuperHackers @bugfix arcticdolphin 09/11/2026 The player cancelled the sign-in before the websocket connected.
				if (loginAttempt != m_loginAttempt)
				{
					return;
				}

                // move on to network capabilities section
                ClearGSMessageBoxes();
                GoToDetermineNetworkCaps();
			});
	}
	else
	{
		if (m_cb_LoginPendingCallback != nullptr)
		{
			m_cb_LoginPendingCallback(loginResult);
		}

		// TheSuperHackers @bugfix arcticdolphin 09/11/2026 A failed login leaves the screen through the OK of the login callback's message box, so only a cancel pops here.
		// TheSuperHackers @feature arcticdolphin 09/11/2026 The sign-in dialog closes itself on cancel; there is no screen to pop.
		if (loginResult == ELoginResult::UserCancelled && !m_bSignInDialog)
		{
			TheShell->pop();
		}
	}
}

static int64_t NowMsV1();

// The longest wait for an answer to one logout send.
static const int64_t LOGOUT_TIMEOUT_MS = 3000;

void GenOnlineAuthInterface::LogoutOfMyAccount(std::function<void()> fnDone)
{
	// A second logout while one runs only waits for the first.
	if (m_logoutDoneV1 != nullptr)
	{
		return;
	}
	m_logoutDoneV1 = fnDone != nullptr ? fnDone : []() {};
	m_logoutDeadlineV1 = NowMsV1() + LOGOUT_TIMEOUT_MS;

	// A refresh that finishes after the logout would save a new refresh token, so it is dropped.
	m_pSignIn->Cancel();
	AbandonRefreshV1();

	// The session ends first, then the refresh token is revoked. The token is attached here because the session is cleared before the request starts.
	const std::string strOrigin = ServiceOriginV1();
	const std::string strRefreshToken = m_strRefreshToken;
	const std::string strURI = strOrigin + "/api/v1/auth/session";
	const std::string strAuthorization = "Bearer " + m_strToken;
	// Each send gets the full wait for its answer, so standby retries during a service switch finish before the teardown.
	auto fnRestartWait = [this]()
	{
		if (m_logoutDoneV1 != nullptr)
		{
			m_logoutDeadlineV1 = NowMsV1() + LOGOUT_TIMEOUT_MS;
		}
	};
	// A standby retry ends with the logout, so a late answer never revokes during a new sign-in.
	GenOnlineRealtimeBridge::SendWithStandbyRetry("DELETE /api/v1/auth/session",
		[strURI, strAuthorization, fnRestartWait](GenOnlineRealtimeBridge::HTTPDoneCallback fnAnswer)
		{
			fnRestartWait();
			GenOnlineServicesManager* pManager = GenOnlineServicesManager::GetInstance();
			if (pManager == nullptr || pManager->GetHTTPManager() == nullptr)
			{
				fnAnswer(false, 0, std::string(), nullptr);
				return;
			}
			std::map<std::string, std::string> mapHeaders;
			mapHeaders["Authorization"] = strAuthorization;
			pManager->GetHTTPManager()->SendDELETERequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, "", fnAnswer);
		},
		[this, strOrigin, strRefreshToken, fnRestartWait](bool, int statusCode, std::string, HTTPRequest*)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: Session ended with status %d", statusCode);
			m_pSignIn->Revoke(strOrigin, strRefreshToken, [this]()
				{
					FinishLogoutV1();
				}, fnRestartWait);
		},
		[this]()
		{
			return GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>() == this && m_logoutDoneV1 != nullptr;
		});
	DeleteSavedCredentials();
	ClearSessionV1();
}

void GenOnlineAuthInterface::FinishLogoutV1()
{
	if (m_logoutDoneV1 == nullptr)
	{
		return;
	}
	std::function<void()> fnDone;
	fnDone.swap(m_logoutDoneV1);
	m_logoutDeadlineV1 = -1;
	fnDone();
}

void GenOnlineAuthInterface::DeleteSavedCredentials()
{
	// delete the stored login token
	ServiceConnection::DeleteRefreshToken(CredentialServerKeyV1());
}

void GenOnlineAuthInterface::LoginAsSecondaryDevAccount()
{

}

bool GenOnlineAuthInterface::GetCredentials()
{
#if defined(_DEBUG) && !defined(USE_TEST_ENV) && !defined(USE_DEBUG_ON_LIVE_SERVER)
	return false;
#endif
	std::string strToken;
	if (!ServiceConnection::LoadRefreshToken(CredentialServerKeyV1(), strToken))
	{
		return false;
	}
	m_strRefreshToken = strToken;
	return true;
}

std::string GenOnlineAuthInterface::CredentialServerKey()
{
	return ServiceConnection::CredentialServerKey(GenOnlineServicesManager::Settings.Network_GetConnectionSettings());
}

std::string GenOnlineAuthInterface::GetCredentialsFilePath()
{
	// debug supports multi inst, so needs seperate tokens
#if defined(_DEBUG) && !defined(USE_TEST_ENV) && !defined(USE_DEBUG_ON_LIVE_SERVER)
	std::string strCredsPath = std::format("{}/GeneralsOnlineData/credentials_dev_env_{}.json", TheGlobalData->getPath_UserData().str(), rts::ClientInstance::getInstanceIndex());
#else
	std::string strCredsPath = std::format("{}/GeneralsOnlineData/{}", TheGlobalData->getPath_UserData().str(), CREDENTIALS_FILENAME);
#endif
	return strCredsPath;
}

// Realtime protocol v1 sign-in /////////////////////////////////////////////////

static int64_t NowMsV1()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
}

// The OAuth refresh token has its own key, so it never mixes with a legacy login token of the same server.
std::string GenOnlineAuthInterface::CredentialServerKeyV1()
{
	const std::string key = CredentialServerKey();
	return key.empty() ? std::string() : "oauth:" + key;
}

std::string GenOnlineAuthInterface::ServiceOriginV1()
{
	return GenOnlineRealtime::OriginOfBase(GenOnlineServicesManager::GetAPIEndpoint(""));
}

void GenOnlineAuthInterface::BeginLoginV1()
{
	const int loginAttempt = ++m_loginAttempt;
	AbandonRefreshV1();
	if (ServiceOriginV1().empty())
	{
		FailLoginV1(L"No trusted service route could be reached. Check Connection settings in Options, then try again.");
		return;
	}

	if (!GetCredentials() || m_strRefreshToken.empty())
	{
		StartBrowserSignInV1(loginAttempt);
		return;
	}

	m_loginStage = ELoginStage::SavedLogin;
	m_pSignIn->Refresh(ServiceOriginV1(), m_strRefreshToken, [this, loginAttempt](const GenOnlineSignIn::Result& result)
		{
			if (loginAttempt != m_loginAttempt)
			{
				return;
			}
			if (result.outcome == GenOnlineSignIn::Outcome::Succeeded)
			{
				CompleteSignInV1(result.tokens, loginAttempt);
				return;
			}
			if (result.outcome == GenOnlineSignIn::Outcome::InvalidGrant)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: The saved login is no longer valid, signing in again");
				DeleteSavedCredentials();
				m_strRefreshToken = std::string();
				StartBrowserSignInV1(loginAttempt);
				return;
			}
			FailLoginV1(L"The sign-in service could not be reached. Try again later.");
		});
}

void GenOnlineAuthInterface::StartBrowserSignInV1(int loginAttempt)
{
	AbandonRefreshV1();
	// The accounts page offers every sign-in provider, so the client opens it directly.
	m_loginStage = ELoginStage::WaitingForBrowser;
	if (!m_bSignInScreen)
	{
		ClearGSMessageBoxes();
		GSMessageBoxCancel(UnicodeString(L"Logging In"), UnicodeString(L"Please continue in your web browser"), []()
			{
				GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
				if (pAuthInterface != nullptr)
				{
					pAuthInterface->CancelLogin();
				}
			});
	}

	m_pSignIn->Start(ServiceOriginV1(), [this, loginAttempt](const GenOnlineSignIn::Result& result)
		{
			if (loginAttempt != m_loginAttempt)
			{
				return;
			}
			if (result.outcome == GenOnlineSignIn::Outcome::Succeeded)
			{
				CompleteSignInV1(result.tokens, loginAttempt);
				return;
			}
			if (result.outcome == GenOnlineSignIn::Outcome::Cancelled)
			{
				CancelLogin();
				return;
			}
			FailLoginV1(L"The sign-in did not complete. Try again.");
		});
}

// A body without the moderation.banned code leaves the length of the ban unsaid.
static OnlineBanDetails BanDetailsFromError(const GenOnlineRealtime::ApiErrorInfo& error)
{
	OnlineBanDetails ban;
	ban.reason = error.reason.value_or(std::string());
	ban.expiresAt = error.expires_at;
	ban.bKnown = error.code == "moderation.banned";
	return ban;
}

void GenOnlineAuthInterface::CompleteSignInV1(const GenOnlineRealtime::TokenResponse& tokens, int loginAttempt)
{
	ApplyTokensV1(tokens);
	m_loginStage = ELoginStage::Finishing;
	RegisterSessionV1([this, loginAttempt](int statusCode, const std::string& strBody)
		{
			if (loginAttempt != m_loginAttempt)
			{
				return;
			}
			if (statusCode == 200)
			{
				try
				{
					const GenOnlineRealtime::AuthSessionResult session = nlohmann::json::parse(strBody).get<GenOnlineRealtime::AuthSessionResult>();
					m_userID = session.user_id;
					m_strDisplayName = session.display_name;
					OnLoginComplete(ELoginResult::Success, session.ws_url.c_str());
					return;
				}
				catch (const std::exception&)
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Malformed session registration response");
				}
			}
			else if (statusCode == 403)
			{
				const GenOnlineRealtime::ApiErrorInfo error = GenOnlineRealtime::ParseApiError(strBody);
				ClearSessionV1();
				// A build the service does not accept is no ban.
				if (error.code == "session.build_not_accepted")
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: The service does not accept this build");
					FailLoginV1(GenOnlineRealtimeBridge::FailureText(error.code, error.params).c_str());
					return;
				}
				m_loginStage = ELoginStage::Failed;
				ClearGSMessageBoxes();
				ShowLoginBanDialog(BanDetailsFromError(error));
				return;
			}
			else
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Session registration answered %d", statusCode);
			}
			if (statusCode == 401)
			{
				DeleteSavedCredentials();
				ClearSessionV1();
				FailLoginV1(GenOnlineRealtimeBridge::FailureText("unauthorized", nlohmann::json::object()).c_str());
				return;
			}
			ClearSessionV1();
			FailLoginV1(L"The game session could not be registered. Try again.");
		});
}

void GenOnlineAuthInterface::ApplyTokensV1(const GenOnlineRealtime::TokenResponse& tokens)
{
	m_strToken = tokens.access_token;
	m_strRefreshToken = tokens.refresh_token;
	// A failed save keeps the token for this session.
	ServiceConnection::SaveRefreshToken(CredentialServerKeyV1(), m_strRefreshToken);
	// The login token of the legacy service is no longer used, so a sign-in deletes it.
	ServiceConnection::DeleteRefreshToken(CredentialServerKey());
	// The refresh runs 2 minutes before the expiry, or halfway through a token that lasts 4 minutes or less.
	const int64_t lifetimeMs = std::max<int64_t>(tokens.expires_in, 60) * 1000;
	const int64_t now = NowMsV1();
	m_accessTokenExpiry = now + lifetimeMs;
	m_accessTokenRefreshAt = now + std::max<int64_t>(lifetimeMs / 2, lifetimeMs - 120 * 1000);
	m_nextRefreshRetryV1 = -1;
}

// The session carries the build identity, so the lobby and queue calls of this session do not.
void GenOnlineAuthInterface::RegisterSessionV1(std::function<void(int statusCode, const std::string& strBody)> fnDone)
{
	GenOnlineRealtime::AuthSessionRequest request;
	request.machine_guid = GetMachineGuid();
	request.mac_addr = GetPrimaryMacAddress();
	request.vol_serial = GetVolumeSerial();
	request.exe_crc = TheGlobalData->m_exeCRC;
	request.ini_crc = TheGlobalData->m_iniCRC;
	request.anticheat_id = GenOnlinePluginHost::GetAnticheatIdentifier();
	const std::string strBody = nlohmann::json(request).dump();

	// A cancelled sign-in, a new sign-in or a logout during a standby wait drops the registration.
	const int loginAttempt = m_loginAttempt;
	GenOnlineRealtimeBridge::SendWithStandbyRetry("POST /api/v1/auth/session",
		[this, strBody](GenOnlineRealtimeBridge::HTTPDoneCallback fnAnswer)
		{
			GenOnlineServicesManager* pManager = GenOnlineServicesManager::GetInstance();
			if (pManager == nullptr || pManager->GetHTTPManager() == nullptr)
			{
				fnAnswer(false, 0, std::string(), nullptr);
				return;
			}
			const std::string strURI = ServiceOriginV1() + "/api/v1/auth/session";
			std::map<std::string, std::string> mapHeaders;
			mapHeaders["Content-Type"] = "application/json";
			mapHeaders["Authorization"] = "Bearer " + m_strToken;
			pManager->GetHTTPManager()->SendPOSTRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, strBody.c_str(), fnAnswer, nullptr, -1, true);
		},
		[fnDone](bool bSuccess, int statusCode, std::string strResponse, HTTPRequest*)
		{
			fnDone(bSuccess ? statusCode : 0, strResponse);
		},
		[this, loginAttempt]()
		{
			return GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>() == this && loginAttempt == m_loginAttempt && !m_strToken.empty();
		});
}

// Callers that arrive while a refresh runs wait for it, since a second refresh would spend the rotated token twice.
void GenOnlineAuthInterface::RefreshTokensV1(std::function<void(bool bRefreshed)> fnDone)
{
	if (fnDone)
	{
		m_refreshWaitersV1.push_back(fnDone);
	}
	if (m_bRefreshInFlightV1)
	{
		return;
	}
	m_bRefreshInFlightV1 = true;
	m_pSignIn->Refresh(ServiceOriginV1(), m_strRefreshToken, [this](const GenOnlineSignIn::Result& result)
		{
			m_bRefreshInFlightV1 = false;
			bool bRefreshed = false;
			if (result.outcome == GenOnlineSignIn::Outcome::Succeeded)
			{
				ApplyTokensV1(result.tokens);
				bRefreshed = true;
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: Token refreshed");
			}
			else if (result.outcome == GenOnlineSignIn::Outcome::InvalidGrant)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: The sign-in was revoked, tearing down");
				DeleteSavedCredentials();
				GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::AUTH_FAILED);
			}
			else if (NowMsV1() + m_secondsUntilRefreshRetry * 1000 < m_accessTokenExpiry)
			{
				m_nextRefreshRetryV1 = NowMsV1() + m_secondsUntilRefreshRetry * 1000;
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: Token refresh failed, retrying in %ds", m_secondsUntilRefreshRetry);
			}
			else
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: Token refresh failed before the token expired, tearing down");
				GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::AUTH_FAILED);
			}

			std::vector<std::function<void(bool)>> waiters;
			waiters.swap(m_refreshWaitersV1);
			for (auto& waiter : waiters)
			{
				waiter(bRefreshed);
			}
		});
}

void GenOnlineAuthInterface::RecoverRealtimeSession(std::function<void(bool bRecovered)> fnDone)
{
	if (!IsLoggedIn() || m_strRefreshToken.empty())
	{
		fnDone(false);
		return;
	}
	RefreshTokensV1([this, fnDone](bool bRefreshed)
		{
			if (!bRefreshed)
			{
				fnDone(false);
				return;
			}
			RegisterSessionV1([fnDone](int statusCode, const std::string& strBody)
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: Session registered again with status %d", statusCode);
					// A ban ends the session with its dialog, which a later teardown reason does not replace.
					const GenOnlineRealtime::ApiErrorInfo error = GenOnlineRealtime::ParseApiError(strBody);
					if (statusCode == 403 && error.code == "moderation.banned")
					{
						HandleModerationBan(BanDetailsFromError(error));
					}
					fnDone(statusCode == 200);
				});
		});
}

void GenOnlineAuthInterface::ReportRealtimeBan()
{
	if (!IsLoggedIn())
	{
		HandleModerationBan(BanDetailsFromError(GenOnlineRealtime::ApiErrorInfo()));
		return;
	}
	// A logout or another sign-in during the check drops its answer.
	const int64_t userID = m_userID;
	RegisterSessionV1([this, userID](int statusCode, const std::string& strBody)
		{
			if (m_userID != userID)
			{
				return;
			}
			const GenOnlineRealtime::ApiErrorInfo error = GenOnlineRealtime::ParseApiError(strBody);
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: Ban check of the refused realtime upgrade answered %d %s", statusCode, error.code.c_str());
			HandleModerationBan(BanDetailsFromError(statusCode == 403 ? error : GenOnlineRealtime::ApiErrorInfo()));
		});
}

void GenOnlineAuthInterface::FailLoginV1(const wchar_t* szText)
{
	m_loginStage = ELoginStage::Failed;
	ClearGSMessageBoxes();
	if (ReportLoginFailure(L"Login Failed", szText))
	{
		return;
	}
	GSMessageBoxOk(UnicodeString(L"Login Failed"), UnicodeString(szText), []()
		{
			TheShell->pop();
		});
}

void GenOnlineAuthInterface::ClearSessionV1()
{
	m_strToken = std::string();
	m_strRefreshToken = std::string();
	m_userID = -1;
	m_accessTokenExpiry = -1;
	m_accessTokenRefreshAt = -1;
	m_nextRefreshRetryV1 = -1;
}

// The sign-in object runs one request at a time. Starting another one drops a running refresh, whose waiters fail.
void GenOnlineAuthInterface::AbandonRefreshV1()
{
	if (!m_bRefreshInFlightV1)
	{
		return;
	}
	m_bRefreshInFlightV1 = false;
	std::vector<std::function<void(bool)>> waiters;
	waiters.swap(m_refreshWaitersV1);
	for (auto& waiter : waiters)
	{
		waiter(false);
	}
}

void GenOnlineAuthInterface::TickV1()
{
	m_pSignIn->Tick();

	if (m_logoutDeadlineV1 != -1 && NowMsV1() >= m_logoutDeadlineV1)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: The service did not answer the logout in time");
		FinishLogoutV1();
	}

	if (!IsLoggedIn() || m_accessTokenExpiry == -1 || m_bRefreshInFlightV1)
	{
		return;
	}
	// Refreshing before the expiry leaves room for a retry.
	const int64_t now = NowMsV1();
	const bool bRetryDue = m_nextRefreshRetryV1 != -1 && now >= m_nextRefreshRetryV1;
	const bool bRefreshDue = m_nextRefreshRetryV1 == -1 && now >= m_accessTokenRefreshAt;
	if (bRetryDue || bRefreshDue)
	{
		m_nextRefreshRetryV1 = -1;
		RefreshTokensV1(nullptr);
	}
}
