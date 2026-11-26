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

#pragma once

#include "GameNetwork/GenOnline/Realtime/RealtimeSignIn.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class LoopbackListener;

class GenOnlineSignIn
{
public:
	enum class Outcome
	{
		Succeeded,
		// The refresh token is no longer valid, so only a new sign-in helps.
		InvalidGrant,
		// The player cancelled, or the browser reported access_denied.
		Cancelled,
		Failed,
	};

	struct Result
	{
		Outcome outcome = Outcome::Failed;
		GenOnlineRealtime::TokenResponse tokens;
		std::string error;
	};

	typedef std::function<void(const Result& result)> DoneFn;

	GenOnlineSignIn();
	~GenOnlineSignIn();

	// Signs in in the system browser. origin is the origin of the environment, https or a loopback http origin.
	// A successful sign-in brings the game window back to the front.
	// promptLogin shows the sign-in form even when the browser is still signed in, for another account.
	void Start(const std::string& origin, DoneFn done, bool promptLogin = false);

	// Exchanges a refresh token for new tokens. The refresh token rotates on every use.
	void Refresh(const std::string& origin, const std::string& refreshToken, DoneFn done);

	// Revokes a refresh token. Best effort. The done callback runs once the service answered, or at once when nothing is sent.
	// A standby answer sends the request again. The send callback runs before each send.
	void Revoke(const std::string& origin, const std::string& refreshToken, std::function<void()> done = nullptr, std::function<void()> onSend = nullptr);

	// Drops the running sign-in or refresh. Its callback never runs.
	void Cancel();


	void Tick();

private:
	void WithDiscovery(const std::string& origin, int attempt, std::function<void()> next);
	void OpenBrowser(int attempt);
	void ExchangeCode(const std::string& code, int attempt);
	void PostTokenForm(const GenOnlineRealtime::FormFields& form, int attempt);
	void Finish(int attempt, const Result& result);

	int m_attempt = 0;
	DoneFn m_done;
	std::string m_discoveredOrigin;
	GenOnlineRealtime::OpenIdConfiguration m_discovery;
	std::unique_ptr<LoopbackListener> m_listener;
	std::string m_state;
	std::string m_codeVerifier;
	std::string m_redirectUri;
	int64_t m_browserDeadline = -1;
	// The running sign-in went through the browser, so a success brings the game window back to the front.
	bool m_viaBrowser = false;
	bool m_promptLogin = false;
};
