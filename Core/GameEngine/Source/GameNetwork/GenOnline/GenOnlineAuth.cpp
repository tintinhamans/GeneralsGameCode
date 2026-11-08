#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"

#include "GameNetwork/GenOnline/HTTP/HTTPManager.h"
#include "GameNetwork/GenOnline/HTTP/HTTPRequest.h"
#include "GameNetwork/GenOnline/GenOnlineModeration.h"
#include "GameNetwork/GenOnline/json.hpp"
#include <shellapi.h>
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

#if defined(USE_TEST_ENV)
#define LOGIN_ENV "test"
#else
#define LOGIN_ENV "prod"
#endif

#include <curl/curl.h>
#include "GameClient/ClientInstance.h"

enum class EAuthResponseResult : int
{
	CODE_INVALID = -1,
	WAITING_USER_ACTION = 0,
	SUCCEEDED = 1,
	FAILED = 2
};

struct GetLoginCodeResponse
{
	bool success = false;
    std::string login_code = "";
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(GetLoginCodeResponse, success, login_code)
};

struct AuthResponse
{
	EAuthResponseResult result = EAuthResponseResult::FAILED;
	std::string session_token = "";
	std::string refresh_token = "";
	int64_t user_id = -1;
	std::string display_name = "";
	std::string ws_uri = "";

	// NOTE: _WITH_DEFAULT so endpoints that only return a subset of these fields (e.g. refresh, which doesn't resend profile data) don't throw during parsing
	NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AuthResponse, result, session_token, refresh_token, user_id, display_name, ws_uri)
};

namespace
{
	std::string GetBanReason(const std::string& responseBody)
	{
		nlohmann::json jsonObject = nlohmann::json::parse(responseBody, nullptr, false, true);
		if (jsonObject.is_object())
		{
			const auto banReason = jsonObject.find("ban_reason");
			if (banReason != jsonObject.end() && banReason->is_string())
			{
				return banReason->get_ref<const std::string&>();
			}
		}

		return std::string();
	}

	// TheSuperHackers @feature arcticdolphin 08/11/2026 Provider sign-in requests the playgenerals.online login page sends; Keycloak replaces this block with its auth URL plus kc_idp_hint.
	const char* const LOGIN_PAGE_URI = "https://www.playgenerals.online/login/";
	const char* const STEAM_RETURN_URI = "https://www.playgenerals.online/login/index.php";
	const char* const DISCORD_AUTHORIZE_URI = "https://discord.com/oauth2/authorize";
	const char* const DISCORD_CLIENT_ID = "1354979004507226294";
	const char* const GAMEREPLAYS_AUTHORIZE_URI = "https://www.gamereplays.org/community/modules/oauth/authorize.php";
	const char* const GAMEREPLAYS_CLIENT_ID = "GeneralsOnlineAuth";
	const char* const STEAM_OPENID_URI = "https://steamcommunity.com/openid/login";
	const char* const OPENID_NS = "http://specs.openid.net/auth/2.0";
	const char* const OPENID_IDENTIFIER_SELECT = "http://specs.openid.net/auth/2.0/identifier_select";
	const char* const GAME_LOGIN_TYPE = "0";

	// TheSuperHackers @feature arcticdolphin 08/11/2026 A login code goes into URLs and the state JSON, so only letters, digits and dashes are accepted.
	bool IsValidLoginCode(const std::string& strCode)
	{
		if (strCode.empty())
		{
			return false;
		}
		for (unsigned char ch : strCode)
		{
			if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-'))
			{
				return false;
			}
		}
		return true;
	}

	// the state the website parses, in its exact format: type 0 is GameReplays, 1 is Discord
	std::string ProviderLoginState(int type, const std::string& strCode, const std::string& strEnv)
	{
		return std::format("{{\"type\":{}, \"code\": \"{}\", \"env\": \"{}\", \"login_type\": \"{}\"}}", type, strCode, strEnv, GAME_LOGIN_TYPE);
	}
}

std::string GenOnlineAuthInterface::PercentEncode(const std::string& strValue)
{
	static const char* const hexDigits = "0123456789ABCDEF";
	std::string strEncoded;
	for (unsigned char ch : strValue)
	{
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~')
		{
			strEncoded += (char)ch;
		}
		else
		{
			strEncoded += '%';
			strEncoded += hexDigits[ch >> 4];
			strEncoded += hexDigits[ch & 0x0F];
		}
	}
	return strEncoded;
}

std::string GenOnlineAuthInterface::GetProviderLoginURL(ELoginProvider provider, const std::string& strCode, const std::string& strEnv)
{
	switch (provider)
	{
	case ELoginProvider::Discord:
		return std::format("{}?client_id={}&redirect_uri={}&response_type=code&scope=identify&state={}", DISCORD_AUTHORIZE_URI, DISCORD_CLIENT_ID,
			PercentEncode(LOGIN_PAGE_URI), PercentEncode(ProviderLoginState(1, strCode, strEnv)));

	case ELoginProvider::GameReplays:
		return std::format("{}?client_id={}&redirect_uri={}&response_type=code&scope=user_profile&state={}", GAMEREPLAYS_AUTHORIZE_URI, GAMEREPLAYS_CLIENT_ID,
			PercentEncode(LOGIN_PAGE_URI), PercentEncode(ProviderLoginState(0, strCode, strEnv)));

	case ELoginProvider::Steam:
	{
		// OpenID 2.0 checkid_setup as a GET request; the website sends no openid.realm
		std::string strReturnTo = std::format("{}?gamecode={}&auth_type=steam&env={}&login_type={}", STEAM_RETURN_URI, PercentEncode(strCode), strEnv, GAME_LOGIN_TYPE);
		return std::format("{}?openid.ns={}&openid.mode=checkid_setup&openid.identity={}&openid.claimed_id={}&openid.return_to={}", STEAM_OPENID_URI,
			PercentEncode(OPENID_NS), PercentEncode(OPENID_IDENTIFIER_SELECT), PercentEncode(OPENID_IDENTIFIER_SELECT), PercentEncode(strReturnTo));
	}

	default:
		break;
	}

	// the website page, which offers every provider itself
	if (strEnv == "test")
	{
		return std::format("{}?gamecode={}&env=test", LOGIN_PAGE_URI, PercentEncode(strCode));
	}
	return std::format("{}?gamecode={}", LOGIN_PAGE_URI, PercentEncode(strCode));
}

struct MOTDResponse
{
	std::string MOTD;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(MOTDResponse, MOTD)
};

void GenOnlineAuthInterface::GoToDetermineNetworkCaps()
{
	// GET MOTD
	std::string strURI = GenOnlineServicesManager::GetAPIEndpoint("MOTD");
	std::map<std::string, std::string> mapHeaders;
	const int loginAttempt = m_loginAttempt;
	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendGETRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{
			// TheSuperHackers @bugfix arcticdolphin 09/11/2026 The player cancelled the sign-in while it was finishing.
			if (loginAttempt != m_loginAttempt)
			{
				return;
			}

			try
			{
				nlohmann::json jsonObject = nlohmann::json::parse(strBody);
				MOTDResponse motdResp = jsonObject.get<MOTDResponse>();

				GenOnlineServicesManager::GetInstance()->ProcessMOTD(motdResp.MOTD.c_str());

				ELoginResult loginResult = ELoginResult::Success;

				// WS should be connected by this point
				std::shared_ptr<WebSocket>  pWS = GenOnlineServicesManager::GetWebSocket();
				bool bWSConnected = pWS == nullptr ? false : pWS->IsConnected();
				if (!bWSConnected)
				{
					loginResult = ELoginResult::Failed;
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

void GenOnlineAuthInterface::SendMiddlewareToken(std::string strMWToken)
{
    std::string strLoginURI = GenOnlineServicesManager::GetAPIEndpoint("ProvideMWToken");

    // login
    std::map<std::string, std::string> mapHeaders;

    nlohmann::json j;
	j["mw_token"] = strMWToken;
    std::string strPostData = j.dump();

    GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendPOSTRequest(strLoginURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, strPostData.c_str(), [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
        {
            if (!bSuccess || statusCode < 200 || statusCode >= 300)
            {
				GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::MIDDLEWARE_LOGIN_FAILED);
            }
            else
            {
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] MW LOGIN: Logged in");
            }

        }, nullptr);
}

void GenOnlineAuthInterface::OnRefreshTokenFailed(const char* szReason, const std::string& strBody)
{
	// the body can hold a rotated refresh token, so only its size is logged
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: Refresh response body was %zu bytes", strBody.size());

	if (m_currentRefreshAttempt < m_maxRefreshAttempts)
	{
		// the token itself is still valid for a few more minutes, so try again shortly instead of waiting for the next scheduled refresh
		m_nextRefreshRetryTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count() + (m_secondsUntilRefreshRetry * 1000);

		NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: Token refresh attempt %d of %d failed (%s), retrying in %ds", m_currentRefreshAttempt, m_maxRefreshAttempts, szReason, m_secondsUntilRefreshRetry);
		return;
	}

	// TODO_JWT: More graceful handling
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: Token refresh failed after %d attempts (%s), tearing down", m_currentRefreshAttempt, szReason);

	// we couldnt renew our token, so things are about to go really bad
	m_nextRefreshRetryTime = -1;
	m_currentRefreshAttempt = 0;
	GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::AUTH_FAILED);
}

void GenOnlineAuthInterface::RefreshToken()
{
	++m_currentRefreshAttempt;

	// clear any pending retry, this attempt supersedes it
	m_nextRefreshRetryTime = -1;

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: Starting token refresh (attempt %d of %d)", m_currentRefreshAttempt, m_maxRefreshAttempts);

	// so we dont keep retrying while the request is pending
	m_tokenCreationTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();

    std::string strRefreshURI = GenOnlineServicesManager::GetAPIEndpoint("RefreshToken");

    if (!m_strRefreshToken.empty())
    {
        std::map<std::string, std::string> mapHeaders;

        // attach refresh token. NOTE: service auth is disabled for this request, otherwise the session token would overwrite this header
        mapHeaders["Authorization"] = "Bearer " + m_strRefreshToken;

        GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendPOSTRequest(strRefreshURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, "", [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
            {
                if (statusCode >= 400 && statusCode < 500)
                {
					if (statusCode == 423)
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: Account ban detected during token refresh, tearing down");
						m_tokenCreationTime = -1;
						m_nextRefreshRetryTime = -1;
						m_currentRefreshAttempt = 0;
						HandleModerationDisconnect(EOnlineModerationAction::BAN, GetBanReason(strBody));
						return;
					}

					OnRefreshTokenFailed(std::format("HTTP {}", statusCode).c_str(), strBody);
                }
                else
                {
                    try
                    {
                        nlohmann::json jsonObject = nlohmann::json::parse(strBody, nullptr, false, true);
                        AuthResponse authResp = jsonObject.get<AuthResponse>();

                        if (authResp.result == EAuthResponseResult::SUCCEEDED && !authResp.session_token.empty())
                        {
                            NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH]: Token refreshed successfully!");

                            m_currentRefreshAttempt = 0;
                            m_nextRefreshRetryTime = -1;

                            // the refresh endpoint may not rotate the refresh token, only save it if we actually got a new one, otherwise we'd wipe the stored credentials
                            if (!authResp.refresh_token.empty())
                            {
                                SaveCredentials(authResp.refresh_token.c_str());
                            }
                            else
                            {
                                m_tokenCreationTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
                            }

                            // store data locally
                            m_strToken = authResp.session_token;

                            // refresh responses don't necessarily resend profile data, don't clobber what we already have
                            if (authResp.user_id != -1)
                            {
                                m_userID = authResp.user_id;
                            }

                            if (!authResp.display_name.empty())
                            {
                                m_strDisplayName = authResp.display_name;
                            }
                        }
                        else if (authResp.result == EAuthResponseResult::SUCCEEDED)
                        {
                            OnRefreshTokenFailed("succeeded but no session_token", strBody);
                        }
                        else
                        {
                            OnRefreshTokenFailed(std::format("auth result {}", (int)authResp.result).c_str(), strBody);
                        }
                    }
                    catch (const std::exception& e)
                    {
                        OnRefreshTokenFailed(std::format("malformed response ({})", e.what()).c_str(), strBody);
                    }
                    catch (...)
                    {
                        OnRefreshTokenFailed("malformed response", strBody);
                    }
                }

            }, nullptr, -1, true /* bDisableServiceAuth, we're authenticating with the refresh token here, not the session token */);
    }
    else
    {
		// nothing to refresh with, we're going through the full login flow instead
		DoFullLoginFlow();
    }
}

void GenOnlineAuthInterface::BeginLogin()
{
	auto* manager = GenOnlineServicesManager::GetInstance();
	if (!manager)
	{
		return;
	}
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
			GSMessageBoxOk(UnicodeString(L"Connection failed"), UnicodeString(proxyAuth ? L"The proxy did not accept the login. Try again, or check the proxy settings in Options." : L"No trusted service route could be reached. Check Connection settings in Options, then try again."), []() { TheShell->pop(); });
		}
	});
}

void GenOnlineAuthInterface::BeginLoginOnSelectedRoute()
{
	m_tokenCreationTime = -1;

	std::string strLoginURI = GenOnlineServicesManager::GetAPIEndpoint("LoginWithToken");

	std::string strRefreshToken;
	bool bValidCreds = GetCredentials();
	if (bValidCreds && !m_strRefreshToken.empty())
	{
		const int loginAttempt = ++m_loginAttempt;
		m_loginStage = ELoginStage::SavedLogin;

		// login
		std::map<std::string, std::string> mapHeaders;

		nlohmann::json j;
		j["machine_guid"] = GetMachineGuid();
		j["mac_addr"] = GetPrimaryMacAddress();
		j["vol_serial"] = GetVolumeSerial();
		j["exe_crc"] = getGameExeCRC();
		j["ini_crc"] = TheGlobalData->m_iniCRC;
		std::string strPostData = j.dump();

		// attach refresh token. NOTE: service auth is disabled for this request, otherwise a stale session token would overwrite this header
		mapHeaders["Authorization"] = "Bearer " + m_strRefreshToken;


		GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendPOSTRequest(strLoginURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, strPostData.c_str(), [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
			{
				// TheSuperHackers @feature arcticdolphin 08/11/2026 The player chose another account while this saved login was on its way.
				if (loginAttempt != m_loginAttempt)
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Dropping the saved login reply, the player chose another account");
					return;
				}

				// if 4XX, just log in again
				if (statusCode >= 400 && statusCode < 500)
				{
					if (statusCode == 423)
					{
						ShowLoginBanDialog(GetBanReason(strBody));
						return;
					}
					else
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Login failed due to 4XX code, trying to re-auth");
						DoFullLoginFlow();
					}
				}
				else
				{
					try
					{
						nlohmann::json jsonObject = nlohmann::json::parse(strBody, nullptr, false, true);
						AuthResponse authResp = jsonObject.get<AuthResponse>();

						if (authResp.result == EAuthResponseResult::SUCCEEDED)
						{
							ClearGSMessageBoxes();
							if (!m_bSignInScreen)
							{
								GSMessageBoxNoButtons(UnicodeString(L"Logging In"), UnicodeString(L"Logged in!"), true);
							}

							NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Logged in");
							m_bWaitingLogin = false;

							SaveCredentials(authResp.refresh_token.c_str());

							// store data locally
							m_strToken = authResp.session_token;
							m_userID = authResp.user_id;
							m_strDisplayName = authResp.display_name;

							// trigger callback
							OnLoginComplete(ELoginResult::Success, authResp.ws_uri.c_str());
						}
						else if (authResp.result == EAuthResponseResult::FAILED)
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Login failed, trying to re-auth");
							DoFullLoginFlow();
						}
					}
					catch (...)
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Resp parse failed, trying to re-auth");
						DoFullLoginFlow();
					}
				}

			}, nullptr, -1, true /* bDisableServiceAuth, we're authenticating with the refresh token here, not the session token */);
	}
	else
	{
		DoFullLoginFlow();
	}
}

void GenOnlineAuthInterface::DoFullLoginFlow()
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: DoFullLoginFlow");

	const int loginAttempt = m_loginAttempt;
	m_loginStage = ELoginStage::RequestingCode;

    // get a game login code from server
    std::string strGetLoginCodeURI = GenOnlineServicesManager::GetAPIEndpoint("LoginCode");
    std::map<std::string, std::string> mapHeaders;
	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendGETRequest(strGetLoginCodeURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{
			if (loginAttempt != m_loginAttempt)
			{
				return;
			}

			std::function<void(void)> fnGetLoginCodeFailed = [this]()
				{
					// stop checking
					m_bWaitingLogin = false;
					m_strCode = std::string();
					m_lastCheckCode = -1;
					m_loginStage = ELoginStage::Failed;


					ClearGSMessageBoxes();
					GSMessageBoxOk(UnicodeString(L"Login Failed"), UnicodeString(L"Failed to retrieve login code"), []()
						{
							TheShell->pop();
						});
				};

			if (!bSuccess || (statusCode >= 400 && statusCode < 500))
			{
				fnGetLoginCodeFailed();
			}
			else
			{
				try
				{
					nlohmann::json jsonObject = nlohmann::json::parse(strBody, nullptr, false, true);
					GetLoginCodeResponse authResp = jsonObject.get<GetLoginCodeResponse>();

					if (authResp.success && IsValidLoginCode(authResp.login_code))
					{
						m_currentRefreshAttempt = 0;

						m_bWaitingLogin = true;
						m_lastCheckCode = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();

						m_strCode = authResp.login_code;
						if (GenOnlineServicesManager::Settings.Debug_VerboseLogging())
						{
							NetworkLog(ELogVerbosity::LOG_DEBUG, "Login Code is %s", m_strCode.c_str());
						}

                        ClearGSMessageBoxes();

                        // TheSuperHackers @feature arcticdolphin 08/11/2026 The sign-in screen shows the code and opens the browser on the provider the player picks.
                        if (m_bSignInScreen)
                        {
                            m_loginStage = ELoginStage::WaitingForProvider;
                            return;
                        }

                        GSMessageBoxCancel(UnicodeString(L"Logging In"), UnicodeString(L"Please continue in your web browser"), []()
                            {
                                GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
                                if (pAuthInterface != nullptr)
                                {
                                    pAuthInterface->CancelLogin();
                                }
                                else if (GenOnlineServicesManager::GetInstance() != nullptr)
                                {
                                    GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::USER_REQUESTED_SILENT);
                                }
                            });

                        OpenProviderLogin(ELoginProvider::Website);
                    }
                    else
                    {
						fnGetLoginCodeFailed();
                    }
                }
                catch (const std::exception& /*e*/)
                {
					fnGetLoginCodeFailed();
                }
                catch (...)
                {
					fnGetLoginCodeFailed();
                }
            }
        });
}

void GenOnlineAuthInterface::Tick()
{
    // Do we need to refresh our token?
    if (IsLoggedIn())
    {
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();

        if (m_nextRefreshRetryTime != -1)
        {
            // a refresh failed, retry it before the token actually expires
            if (now >= m_nextRefreshRetryTime)
            {
                NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH] Retrying token refresh...");
                RefreshToken();
            }
        }
        else if (m_tokenCreationTime != -1 && now - m_tokenCreationTime >= m_minutesUntilTokenRefresh * 60 * 1000) // refresh every 10m, tokens last 15m
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE, "[AUTH] Token is about to expire, refreshing...");
            m_currentRefreshAttempt = 0;
            RefreshToken();
        }
    }

	if (m_bWaitingLogin)
	{
		const int64_t timeBetweenChecks = 1000;
		int64_t currTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();

		if (currTime - m_lastCheckCode >= timeBetweenChecks)
		{
			m_lastCheckCode = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();

			// check again
			std::string strURI = GenOnlineServicesManager::GetAPIEndpoint("CheckLogin");
			std::map<std::string, std::string> mapHeaders;

			nlohmann::json j;
			j["code"] = m_strCode.c_str();
			j["client_id"] = GENONLINE_CLIENT_ID;
            j["machine_guid"] = GetMachineGuid();
            j["mac_addr"] = GetPrimaryMacAddress();
            j["vol_serial"] = GetVolumeSerial();
            j["exe_crc"] = getGameExeCRC();
            j["ini_crc"] = TheGlobalData->m_iniCRC;
			std::string strPostData = j.dump();

			const int loginAttempt = m_loginAttempt;
			GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendPOSTRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, strPostData.c_str(), [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
				{
					// TheSuperHackers @feature arcticdolphin 08/11/2026 The login was cancelled or restarted after this check was sent.
					if (loginAttempt != m_loginAttempt)
					{
						return;
					}

					try
					{
						if (statusCode == 423)
						{
							m_bWaitingLogin = false;
							ShowLoginBanDialog(GetBanReason(strBody));
							return;
						}

						nlohmann::json jsonObject = nlohmann::json::parse(strBody);
						AuthResponse authResp = jsonObject.get<AuthResponse>();

						if (GenOnlineServicesManager::Settings.Debug_VerboseLogging())
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "PageBody: %zu bytes", strBody.size());
						}
						if (authResp.result == EAuthResponseResult::CODE_INVALID)
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Code didnt exist, trying again soon");
						}
						else if (authResp.result == EAuthResponseResult::WAITING_USER_ACTION)
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Waiting for user action");
						}
						else if (authResp.result == EAuthResponseResult::SUCCEEDED)
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Logged in");
							m_bWaitingLogin = false;

							SaveCredentials(authResp.refresh_token.c_str());

							// store data locally
							m_strToken = authResp.session_token;
							m_userID = authResp.user_id;
							m_strDisplayName = authResp.display_name;

							// trigger callback
							OnLoginComplete(ELoginResult::Success, authResp.ws_uri.c_str());
						}
						else if (authResp.result == EAuthResponseResult::FAILED)
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Login failed");
							m_bWaitingLogin = false;

							// trigger callback
							OnLoginComplete(ELoginResult::Failed, "");
						}
					}
					catch (...)
					{

					}

				}, nullptr);
		}
	}
}

void GenOnlineAuthInterface::OpenProviderLogin(ELoginProvider provider)
{
	if (m_strCode.empty())
	{
		return;
	}

	m_loginStage = ELoginStage::WaitingForBrowser;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "LOGIN: Opening the browser sign-in for provider %d", (int)provider);
	std::string strURI = GetProviderLoginURL(provider, m_strCode, LOGIN_ENV);

#if !defined(_DEBUG) || defined(USE_TEST_ENV) || defined(USE_DEBUG_ON_LIVE_SERVER)
	ShellExecuteA(NULL, "open", strURI.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
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
	DoFullLoginFlow();
}

void GenOnlineAuthInterface::CancelLogin()
{
	// TheSuperHackers @feature arcticdolphin 08/11/2026 Drop the replies still on their way and stop polling, so a cancelled login never completes later.
	++m_loginAttempt;
	m_bWaitingLogin = false;
	m_strCode = std::string();
	m_lastCheckCode = -1;

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
		if (loginResult == ELoginResult::UserCancelled)
		{
			TheShell->pop();
		}
	}
}

void GenOnlineAuthInterface::LogoutOfMyAccount()
{
	std::string strURI = std::format("{}/{}", GenOnlineServicesManager::GetAPIEndpoint("User"), m_userID);
	std::map<std::string, std::string> mapHeaders;
	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendDELETERequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, "", nullptr);

	DeleteSavedCredentials();
}

void GenOnlineAuthInterface::DeleteSavedCredentials()
{
	ServiceConnection::DeleteRefreshToken(CredentialServerKey());
}

void GenOnlineAuthInterface::LoginAsSecondaryDevAccount()
{

}

void GenOnlineAuthInterface::SaveCredentials(const char* szRefreshToken)
{
	m_strRefreshToken = std::string(szRefreshToken); // store the new refresh token, we'll need it for the next refresh
	m_tokenCreationTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();

	// a failed save keeps the token for this session
	ServiceConnection::SaveRefreshToken(CredentialServerKey(), m_strRefreshToken);
}

bool GenOnlineAuthInterface::GetCredentials()
{
#if defined(_DEBUG) && !defined(USE_TEST_ENV) && !defined(USE_DEBUG_ON_LIVE_SERVER)
	return false;
#endif
	std::string strToken;
	if (!ServiceConnection::LoadRefreshToken(CredentialServerKey(), strToken))
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
