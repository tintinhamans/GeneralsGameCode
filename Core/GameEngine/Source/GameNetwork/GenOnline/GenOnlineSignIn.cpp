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

///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <shellapi.h>

#include "GameNetwork/GenOnline/GenOnlineSignIn.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineRealtimeBridge.h"
#include "GameNetwork/GenOnline/HTTP/HTTPManager.h"
#include "GameNetwork/GenOnline/HTTP/HTTPRequest.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

using namespace GenOnlineRealtime;

extern HWND ApplicationHWnd;

namespace
{

// Brings the game window back in front of the browser. Windows refuses this while another program has the focus, the taskbar button then flashes instead.
void BringGameWindowToFront()
{
	if (ApplicationHWnd == nullptr)
	{
		return;
	}
	if (IsIconic(ApplicationHWnd))
	{
		ShowWindow(ApplicationHWnd, SW_RESTORE);
	}
	if (SetForegroundWindow(ApplicationHWnd))
	{
		return;
	}
	FLASHWINFO flash = {};
	flash.cbSize = sizeof(flash);
	flash.hwnd = ApplicationHWnd;
	flash.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
	FlashWindowEx(&flash);
}

constexpr int64_t BROWSER_TIMEOUT_MS = 10 * 60 * 1000;
constexpr int64_t LOOPBACK_REQUEST_TIMEOUT_MS = 3000;
constexpr size_t LOOPBACK_MAX_REQUEST_BYTES = 8192;

int64_t SignInNowMs()
{
	return static_cast<int64_t>(GetTickCount64());
}

std::vector<uint8_t> RandomBytes(size_t count)
{
	std::vector<uint8_t> bytes(count);
	if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
	{
		bytes.clear();
	}
	return bytes;
}

std::vector<uint8_t> Sha256(const std::string& data)
{
	std::vector<uint8_t> digest(32);
	if (!BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)data.data(), static_cast<ULONG>(data.size()), digest.data(), static_cast<ULONG>(digest.size()))))
	{
		digest.clear();
	}
	return digest;
}

} // namespace

// Waits on 127.0.0.1 for the browser to return from the sign-in. Only the loopback interface is
// bound, so other machines cannot reach it. Requests other than the callback get a 404.
class LoopbackListener
{
public:
	~LoopbackListener()
	{
		Close();
	}

	bool Open()
	{
		WSADATA data;
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
		{
			return false;
		}
		m_wsaStarted = true;

		m_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_listen == INVALID_SOCKET)
		{
			return false;
		}
		sockaddr_in address = {};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;
		u_long nonBlocking = 1;
		int length = sizeof(address);
		if (bind(m_listen, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
			listen(m_listen, 4) != 0 ||
			ioctlsocket(m_listen, FIONBIO, &nonBlocking) != 0 ||
			getsockname(m_listen, reinterpret_cast<sockaddr*>(&address), &length) != 0)
		{
			return false;
		}
		m_port = ntohs(address.sin_port);
		return true;
	}

	int Port() const
	{
		return m_port;
	}

	// Returns true when a request carried the callback of this sign-in. Other requests, including a callback
	// with another state, are answered and dropped, so a stray request cannot end the sign-in.
	bool Poll(const std::string& expectedState, LoopbackCallback& result)
	{
		if (m_listen == INVALID_SOCKET)
		{
			return false;
		}
		if (m_client == INVALID_SOCKET)
		{
			m_client = accept(m_listen, nullptr, nullptr);
			if (m_client == INVALID_SOCKET)
			{
				return false;
			}
			u_long nonBlocking = 1;
			ioctlsocket(m_client, FIONBIO, &nonBlocking);
			m_request.clear();
			m_clientDeadline = SignInNowMs() + LOOPBACK_REQUEST_TIMEOUT_MS;
		}

		char buffer[2048];
		const int received = recv(m_client, buffer, sizeof(buffer), 0);
		if (received > 0)
		{
			m_request.append(buffer, received);
		}
		else if (received == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
		{
			CloseClient();
			return false;
		}

		if (m_request.find("\r\n") == std::string::npos)
		{
			if (m_request.size() > LOOPBACK_MAX_REQUEST_BYTES || SignInNowMs() > m_clientDeadline)
			{
				CloseClient();
			}
			return false;
		}

		result = ParseLoopbackRequest(m_request, expectedState);
		switch (result.kind)
		{
		case LoopbackCallback::Kind::Code:
			Respond("200 OK", "Signed in to GenOnline. You can close this tab and return to the game.");
			break;
		case LoopbackCallback::Kind::Ignore:
			Respond("404 Not Found", "Not found.");
			break;
		default:
			Respond("400 Bad Request", "The sign-in did not complete. Return to the game and try again.");
			break;
		}
		CloseClient();
		return result.kind == LoopbackCallback::Kind::Code || result.kind == LoopbackCallback::Kind::Error;
	}

	void Close()
	{
		CloseClient();
		if (m_listen != INVALID_SOCKET)
		{
			closesocket(m_listen);
			m_listen = INVALID_SOCKET;
		}
		if (m_wsaStarted)
		{
			WSACleanup();
			m_wsaStarted = false;
		}
	}

private:
	void Respond(const char* status, const char* text)
	{
		std::string body = std::string("<!doctype html><html><head><meta charset=\"utf-8\"><title>GenOnline</title></head><body><p>") + text + "</p></body></html>";
		std::string response = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
			std::to_string(body.size()) + "\r\n\r\n" + body;
		// The answer is small, a short wait for the send buffer is enough.
		u_long blocking = 0;
		ioctlsocket(m_client, FIONBIO, &blocking);
		DWORD timeout = 1000;
		setsockopt(m_client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
		send(m_client, response.data(), static_cast<int>(response.size()), 0);
		shutdown(m_client, SD_SEND);
	}

	void CloseClient()
	{
		if (m_client != INVALID_SOCKET)
		{
			closesocket(m_client);
			m_client = INVALID_SOCKET;
		}
		m_request.clear();
	}

	bool m_wsaStarted = false;
	SOCKET m_listen = INVALID_SOCKET;
	SOCKET m_client = INVALID_SOCKET;
	int m_port = 0;
	std::string m_request;
	int64_t m_clientDeadline = 0;
};

GenOnlineSignIn::GenOnlineSignIn()
{
}

GenOnlineSignIn::~GenOnlineSignIn()
{
}

void GenOnlineSignIn::Start(const std::string& origin, DoneFn done)
{
	Cancel();
	const int attempt = m_attempt;
	m_done = done;
	m_viaBrowser = true;
	WithDiscovery(origin, attempt, [this, attempt]()
		{
			OpenBrowser(attempt);
		});
}

void GenOnlineSignIn::Refresh(const std::string& origin, const std::string& refreshToken, DoneFn done)
{
	Cancel();
	const int attempt = m_attempt;
	m_done = done;
	m_viaBrowser = false;
	WithDiscovery(origin, attempt, [this, attempt, refreshToken]()
		{
			PostTokenForm(RefreshTokenForm(GENONLINE_CLIENT_ID, refreshToken), attempt);
		});
}

void GenOnlineSignIn::Revoke(const std::string& origin, const std::string& refreshToken, std::function<void()> done, std::function<void()> onSend)
{
	if (refreshToken.empty() || origin != m_discoveredOrigin || m_discovery.revocation_endpoint.empty())
	{
		if (done != nullptr)
		{
			done();
		}
		return;
	}
	const std::string endpoint = m_discovery.revocation_endpoint;
	const std::string body = EncodeForm(RevokeTokenForm(GENONLINE_CLIENT_ID, refreshToken));
	// Revoking the old token is safe at any time, so the retry needs no wanted check.
	GenOnlineRealtimeBridge::SendWithStandbyRetry("POST revoke",
		[endpoint, body, onSend](GenOnlineRealtimeBridge::HTTPDoneCallback fnAnswer)
		{
			if (onSend != nullptr)
			{
				onSend();
			}
			GenOnlineServicesManager* pManager = GenOnlineServicesManager::GetInstance();
			if (pManager == nullptr || pManager->GetHTTPManager() == nullptr)
			{
				fnAnswer(false, 0, std::string(), nullptr);
				return;
			}
			std::map<std::string, std::string> headers;
			headers["Content-Type"] = "application/x-www-form-urlencoded";
			pManager->GetHTTPManager()->SendPOSTRequest(endpoint.c_str(), EIPProtocolVersion::DONT_CARE, headers, body.c_str(), fnAnswer, nullptr, -1, true);
		},
		[done](bool, int statusCode, std::string, HTTPRequest*)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[SignIn] Revoke answered %d", statusCode);
			if (done != nullptr)
			{
				done();
			}
		});
}

void GenOnlineSignIn::Cancel()
{
	++m_attempt;
	m_done = nullptr;
	m_listener.reset();
	m_browserDeadline = -1;
	m_state.clear();
	m_codeVerifier.clear();
}

bool GenOnlineSignIn::IsWaitingForBrowser() const
{
	return m_listener != nullptr;
}

void GenOnlineSignIn::Tick()
{
	if (m_listener == nullptr)
	{
		return;
	}
	const int attempt = m_attempt;
	if (SignInNowMs() > m_browserDeadline)
	{
		Result result;
		result.error = "the browser sign-in timed out";
		Finish(attempt, result);
		return;
	}

	LoopbackCallback callback;
	if (!m_listener->Poll(m_state, callback))
	{
		return;
	}
	m_listener.reset();
	m_browserDeadline = -1;

	if (callback.kind == LoopbackCallback::Kind::Code)
	{
		ExchangeCode(callback.code, attempt);
		return;
	}
	Result result;
	result.outcome = callback.kind == LoopbackCallback::Kind::Error && callback.error == "access_denied" ? Outcome::Cancelled : Outcome::Failed;
	result.error = callback.error;
	Finish(attempt, result);
}

void GenOnlineSignIn::WithDiscovery(const std::string& origin, int attempt, std::function<void()> next)
{
	if (!origin.empty() && origin == m_discoveredOrigin)
	{
		next();
		return;
	}
	if (origin.empty())
	{
		Result result;
		result.error = "no service origin";
		Finish(attempt, result);
		return;
	}

	const std::string url = origin + "/.well-known/openid-configuration";
	std::map<std::string, std::string> headers;
	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendGETRequest(url.c_str(), EIPProtocolVersion::DONT_CARE, headers,
		[this, attempt, origin, next](bool bSuccess, int statusCode, std::string strBody, HTTPRequest*)
		{
			if (attempt != m_attempt)
			{
				return;
			}
			Result result;
			if (!bSuccess || statusCode != 200)
			{
				result.error = "the discovery document answered " + std::to_string(statusCode);
				Finish(attempt, result);
				return;
			}
			OpenIdConfiguration configuration;
			if (!ParseOpenIdConfiguration(strBody, origin, configuration, result.error))
			{
				Finish(attempt, result);
				return;
			}
			m_discovery = configuration;
			m_discoveredOrigin = origin;
			next();
		});
}

void GenOnlineSignIn::OpenBrowser(int attempt)
{
	if (attempt != m_attempt)
	{
		return;
	}
	Result failure;
	const std::vector<uint8_t> verifierBytes = RandomBytes(32);
	const std::vector<uint8_t> stateBytes = RandomBytes(16);
	if (verifierBytes.empty() || stateBytes.empty())
	{
		failure.error = "no random bytes for the sign-in";
		Finish(attempt, failure);
		return;
	}

	m_listener = std::make_unique<LoopbackListener>();
	if (!m_listener->Open())
	{
		m_listener.reset();
		failure.error = "the sign-in listener could not start";
		Finish(attempt, failure);
		return;
	}

	m_codeVerifier = PkceVerifier(verifierBytes);
	m_state = Base64UrlEncode(stateBytes);
	m_redirectUri = LoopbackRedirectUri(m_listener->Port());
	const std::string challenge = PkceChallenge(m_codeVerifier, Sha256);
	if (challenge.empty())
	{
		m_listener.reset();
		failure.error = "the sign-in challenge could not be computed";
		Finish(attempt, failure);
		return;
	}
	m_browserDeadline = SignInNowMs() + BROWSER_TIMEOUT_MS;

	const std::string url = BuildAuthorizeUrl(m_discovery.authorization_endpoint, GENONLINE_CLIENT_ID, m_redirectUri, m_state, challenge);
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[SignIn] Opening the browser sign-in, listening on port %d", m_listener->Port());
	ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void GenOnlineSignIn::ExchangeCode(const std::string& code, int attempt)
{
	PostTokenForm(CodeTokenForm(GENONLINE_CLIENT_ID, code, m_codeVerifier, m_redirectUri), attempt);
}

// A public client sends no secret, and the session token never goes to the token endpoint.
void GenOnlineSignIn::PostTokenForm(const FormFields& form, int attempt)
{
	if (attempt != m_attempt)
	{
		return;
	}
	std::map<std::string, std::string> headers;
	headers["Content-Type"] = "application/x-www-form-urlencoded";
	const std::string body = EncodeForm(form);
	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendPOSTRequest(m_discovery.token_endpoint.c_str(), EIPProtocolVersion::DONT_CARE, headers, body.c_str(),
		[this, attempt](bool bSuccess, int statusCode, std::string strBody, HTTPRequest*)
		{
			if (attempt != m_attempt)
			{
				return;
			}
			Result result;
			if (bSuccess && statusCode == 200)
			{
				try
				{
					result.tokens = nlohmann::json::parse(strBody).get<TokenResponse>();
					if (!result.tokens.access_token.empty() && !result.tokens.refresh_token.empty())
					{
						result.outcome = Outcome::Succeeded;
					}
					else
					{
						result.error = "the token response has no tokens";
					}
				}
				catch (const std::exception&)
				{
					result.error = "malformed token response";
				}
				Finish(attempt, result);
				return;
			}

			// The body of a failed token request holds only the OAuth error, never a token.
			OAuthError error;
			try
			{
				error = nlohmann::json::parse(strBody).get<OAuthError>();
			}
			catch (const std::exception&)
			{
			}
			result.outcome = error.error == "invalid_grant" ? Outcome::InvalidGrant : Outcome::Failed;
			result.error = "the token endpoint answered " + std::to_string(statusCode) + (error.error.empty() ? std::string() : " " + error.error);
			Finish(attempt, result);
		}, nullptr, -1, true);
}

void GenOnlineSignIn::Finish(int attempt, const Result& result)
{
	if (attempt != m_attempt)
	{
		return;
	}
	m_listener.reset();
	m_browserDeadline = -1;
	m_codeVerifier.clear();
	m_state.clear();
	DoneFn done = m_done;
	m_done = nullptr;
	if (result.outcome != Outcome::Succeeded)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[SignIn] %s", result.error.c_str());
	}
	else if (m_viaBrowser)
	{
		BringGameWindowToFront();
	}
	if (done)
	{
		done(result);
	}
}
