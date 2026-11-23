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

#include "GameNetwork/GenOnline/Realtime/RealtimeHubSession.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeRest.h"

#include <functional>
#include <string>
#include <vector>

class HTTPRequest;

namespace GenOnlineRealtimeBridge
{

void RegisterHandlers(GenOnlineRealtime::HubSession& session);

// bFirstReady is true for the first Ready after a login.
void OnReady(const GenOnlineRealtime::ReadyEvent& ready, bool bFirstReady);

// The colour of the first defined badge of a user. Badges are listed highest priority first and
// operators define them at runtime, so the client never looks at badge ids. Returns false without a badge.
bool BadgeColor(const std::vector<std::string>& badges, int& color);

bool HasPermission(const char* szPermission);

bool HasModerationPermission();

// True when the service switched the client feature on for this player, for example stats_exporter or observer.
bool HasClientFeature(const char* szFeature);

std::string ActivityText(const std::string& activity);

std::wstring FailureText(const GenOnlineRealtime::InvokeOutcome& outcome);

// A code shows the same text as from the hub.
std::wstring FailureText(const GenOnlineRealtime::RestOutcome& outcome);

// The text of a section 7 failure code and its params.
std::wstring FailureText(const std::string& code, const nlohmann::json& params);

enum class RestMethod
{
	Get,
	Post,
	Put,
	Delete,
	Patch
};

typedef std::function<void(const GenOnlineRealtime::RestOutcome&)> RestCallback;

// Calls a REST endpoint on the service origin with the access token of the session. path starts with /api/v1
// and body is JSON or empty. Without a session or a service route the callback gets an undelivered outcome.
// A standby answer sends the call again after STANDBY_RETRY_DELAY_MS for up to STANDBY_RETRY_WINDOW_MS, without counting as a send.
void CallRest(RestMethod method, const std::string& path, const std::string& body, RestCallback callback);

// Calls an endpoint that is safe to repeat. A failure that RestRetryDelayMs retries sends the call again
// after its delay, and the callback gets the last outcome. A retry is dropped when the signed in user changes.
void CallRestWithRetry(RestMethod method, const std::string& path, const std::string& body, RestCallback callback);

typedef std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pRequest)> HTTPDoneCallback;
typedef std::function<void(HTTPDoneCallback)> HTTPSendFunction;

// Sends an /api/v1 request that does not go through CallRest. A standby answer sends it again after
// STANDBY_RETRY_DELAY_MS until the standby answers lasted STANDBY_RETRY_WINDOW_MS, then fnDone gets the last answer.
// fnWanted runs before each new try, and false drops the request without calling fnDone.
void SendWithStandbyRetry(const std::string& label, HTTPSendFunction fnSend, HTTPDoneCallback fnDone, std::function<bool()> fnWanted = nullptr);

// Sends the retries that are due. Runs on every services tick.
void TickRest();

// Drops every queued retry without its callback, like the HTTP manager drops its requests at the services shutdown.
void ShutdownRest();

void ShowRoomNotice(const std::wstring& text, bool bError);

} // namespace GenOnlineRealtimeBridge
