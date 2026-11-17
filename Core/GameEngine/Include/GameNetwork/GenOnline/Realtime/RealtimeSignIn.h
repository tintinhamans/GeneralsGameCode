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

#include "GameNetwork/GenOnline/Realtime/RealtimeMessages.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace GenOnlineRealtime
{

constexpr const char* SIGN_IN_SCOPE = "openid offline_access game";
constexpr const char* LOOPBACK_CALLBACK_PATH = "/callback";

typedef std::function<std::vector<uint8_t>(const std::string& data)> Sha256Fn;

inline std::string Base64UrlEncode(const uint8_t* data, size_t length)
{
	static const char* const ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string out;
	out.reserve((length + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 3 <= length; i += 3)
	{
		uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | uint32_t(data[i + 2]);
		out += ALPHABET[(n >> 18) & 63];
		out += ALPHABET[(n >> 12) & 63];
		out += ALPHABET[(n >> 6) & 63];
		out += ALPHABET[n & 63];
	}
	if (length - i == 1)
	{
		uint32_t n = uint32_t(data[i]) << 16;
		out += ALPHABET[(n >> 18) & 63];
		out += ALPHABET[(n >> 12) & 63];
	}
	else if (length - i == 2)
	{
		uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
		out += ALPHABET[(n >> 18) & 63];
		out += ALPHABET[(n >> 12) & 63];
		out += ALPHABET[(n >> 6) & 63];
	}
	return out;
}

inline std::string Base64UrlEncode(const std::vector<uint8_t>& data)
{
	return Base64UrlEncode(data.data(), data.size());
}

// The S256 code challenge of a verifier.
inline std::string PkceChallenge(const std::string& verifier, const Sha256Fn& sha256)
{
	return Base64UrlEncode(sha256(verifier));
}

// A verifier from 32 random bytes is 43 characters, the shortest one RFC 7636 allows.
inline std::string PkceVerifier(const std::vector<uint8_t>& randomBytes)
{
	return Base64UrlEncode(randomBytes);
}

// Percent-encodes everything but the RFC 3986 unreserved characters.
inline std::string UrlEncode(const std::string& value)
{
	static const char* const HEX = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : value)
	{
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~')
		{
			out += static_cast<char>(c);
		}
		else
		{
			out += '%';
			out += HEX[c >> 4];
			out += HEX[c & 15];
		}
	}
	return out;
}

// Decodes percent escapes, and a plus as a space. Returns false for a broken escape.
inline bool UrlDecode(const std::string& value, std::string& out)
{
	out.clear();
	for (size_t i = 0; i < value.size(); ++i)
	{
		char c = value[i];
		if (c == '+')
		{
			out += ' ';
		}
		else if (c == '%')
		{
			if (i + 2 >= value.size())
				return false;
			auto hex = [](char h) -> int
			{
				if (h >= '0' && h <= '9')
					return h - '0';
				if (h >= 'a' && h <= 'f')
					return h - 'a' + 10;
				if (h >= 'A' && h <= 'F')
					return h - 'A' + 10;
				return -1;
			};
			int high = hex(value[i + 1]);
			int low = hex(value[i + 2]);
			if (high < 0 || low < 0)
				return false;
			out += static_cast<char>((high << 4) | low);
			i += 2;
		}
		else
		{
			out += c;
		}
	}
	return true;
}

typedef std::vector<std::pair<std::string, std::string> > FormFields;

// Encodes fields as application/x-www-form-urlencoded, which is also the query string format.
inline std::string EncodeForm(const FormFields& fields)
{
	std::string out;
	for (const auto& field : fields)
	{
		if (!out.empty())
			out += '&';
		out += UrlEncode(field.first);
		out += '=';
		out += UrlEncode(field.second);
	}
	return out;
}

inline bool DecodeForm(const std::string& text, std::map<std::string, std::string>& fields)
{
	fields.clear();
	size_t start = 0;
	while (start <= text.size())
	{
		size_t end = text.find('&', start);
		if (end == std::string::npos)
			end = text.size();
		std::string pair = text.substr(start, end - start);
		if (!pair.empty())
		{
			size_t equals = pair.find('=');
			std::string key, value;
			if (!UrlDecode(pair.substr(0, equals), key))
				return false;
			if (equals != std::string::npos && !UrlDecode(pair.substr(equals + 1), value))
				return false;
			// The first value of a repeated key counts.
			fields.insert(std::make_pair(key, value));
		}
		start = end + 1;
	}
	return true;
}

// The loopback redirect URI for a port. An IPv4 literal avoids the resolver, as RFC 8252 advises.
inline std::string LoopbackRedirectUri(int port)
{
	return "http://127.0.0.1:" + std::to_string(port) + LOOPBACK_CALLBACK_PATH;
}

// The authorize URL opened in the system browser, on the authorization endpoint of the discovery document.
// promptLogin asks for the sign-in form even when the browser is still signed in, so the player can pick another account.
inline std::string BuildAuthorizeUrl(const std::string& authorizationEndpoint, const std::string& clientId, const std::string& redirectUri,
	const std::string& state, const std::string& codeChallenge, bool promptLogin = false)
{
	FormFields query;
	query.push_back(std::make_pair("client_id", clientId));
	query.push_back(std::make_pair("response_type", "code"));
	query.push_back(std::make_pair("redirect_uri", redirectUri));
	query.push_back(std::make_pair("scope", SIGN_IN_SCOPE));
	query.push_back(std::make_pair("state", state));
	query.push_back(std::make_pair("code_challenge", codeChallenge));
	query.push_back(std::make_pair("code_challenge_method", "S256"));
	if (promptLogin)
	{
		query.push_back(std::make_pair("prompt", "login"));
	}
	return authorizationEndpoint + "?" + EncodeForm(query);
}

inline FormFields CodeTokenForm(const std::string& clientId, const std::string& code, const std::string& codeVerifier, const std::string& redirectUri)
{
	FormFields form;
	form.push_back(std::make_pair("grant_type", "authorization_code"));
	form.push_back(std::make_pair("client_id", clientId));
	form.push_back(std::make_pair("code", code));
	form.push_back(std::make_pair("code_verifier", codeVerifier));
	form.push_back(std::make_pair("redirect_uri", redirectUri));
	return form;
}

// The refresh token rotates on every use. Sending an older one revokes the sign-in.
inline FormFields RefreshTokenForm(const std::string& clientId, const std::string& refreshToken)
{
	FormFields form;
	form.push_back(std::make_pair("grant_type", "refresh_token"));
	form.push_back(std::make_pair("client_id", clientId));
	form.push_back(std::make_pair("refresh_token", refreshToken));
	return form;
}

inline FormFields RevokeTokenForm(const std::string& clientId, const std::string& refreshToken)
{
	FormFields form;
	form.push_back(std::make_pair("client_id", clientId));
	form.push_back(std::make_pair("token", refreshToken));
	form.push_back(std::make_pair("token_type_hint", "refresh_token"));
	return form;
}

struct LoopbackCallback
{
	enum class Kind
	{
		// Not the callback, for example a favicon request. The listener answers 404 and keeps waiting.
		Ignore,
		// The browser brought an authorization code with the expected state.
		Code,
		Error,
		// The callback does not belong to this sign-in. The sign-in fails.
		Rejected,
	};

	Kind kind = Kind::Ignore;
	std::string code;
	std::string error;
};

inline LoopbackCallback ParseLoopbackRequest(const std::string& request, const std::string& expectedState)
{
	LoopbackCallback result;
	size_t lineEnd = request.find("\r\n");
	std::string line = request.substr(0, lineEnd);
	if (line.compare(0, 4, "GET ") != 0)
		return result;
	size_t targetEnd = line.find(' ', 4);
	if (targetEnd == std::string::npos)
		return result;
	std::string target = line.substr(4, targetEnd - 4);
	size_t queryStart = target.find('?');
	std::string path = target.substr(0, queryStart);
	if (path != LOOPBACK_CALLBACK_PATH)
		return result;

	std::map<std::string, std::string> query;
	if (queryStart == std::string::npos || !DecodeForm(target.substr(queryStart + 1), query))
	{
		result.kind = LoopbackCallback::Kind::Rejected;
		result.error = "malformed callback";
		return result;
	}
	auto state = query.find("state");
	if (expectedState.empty() || state == query.end() || state->second != expectedState)
	{
		result.kind = LoopbackCallback::Kind::Rejected;
		result.error = "state mismatch";
		return result;
	}
	auto error = query.find("error");
	if (error != query.end())
	{
		result.kind = LoopbackCallback::Kind::Error;
		result.error = error->second;
		return result;
	}
	auto code = query.find("code");
	if (code == query.end() || code->second.empty())
	{
		result.kind = LoopbackCallback::Kind::Rejected;
		result.error = "callback has no code";
		return result;
	}
	result.kind = LoopbackCallback::Kind::Code;
	result.code = code->second;
	return result;
}

// A loopback host is exactly localhost, 127.0.0.1 or [::1], compared without case.
inline bool IsLoopbackHost(const std::string& host)
{
	std::string lower = host;
	for (char& c : lower)
	{
		if (c >= 'A' && c <= 'Z')
		{
			c = static_cast<char>(c - 'A' + 'a');
		}
	}
	return lower == "localhost" || lower == "127.0.0.1" || lower == "[::1]";
}

// True for a URL with this scheme whose authority is a loopback host with an optional numeric port.
// An authority with credentials or any other text, such as localhost@evil.example, is refused.
inline bool IsLoopbackURL(const std::string& url, const std::string& scheme)
{
	const std::string prefix = scheme + "://";
	if (url.compare(0, prefix.size(), prefix) != 0)
	{
		return false;
	}
	const size_t start = prefix.size();
	const size_t authorityEnd = url.find_first_of("/?#", start);
	const std::string authority = url.substr(start, authorityEnd == std::string::npos ? std::string::npos : authorityEnd - start);
	size_t hostEnd = authority.find(':');
	if (!authority.empty() && authority[0] == '[')
	{
		hostEnd = authority.find(']');
		if (hostEnd != std::string::npos)
		{
			++hostEnd;
		}
	}
	if (hostEnd == std::string::npos)
	{
		hostEnd = authority.size();
	}
	const std::string port = authority.substr(hostEnd);
	if (!port.empty() && (port[0] != ':' || port.size() == 1 || port.find_first_not_of("0123456789", 1) != std::string::npos))
	{
		return false;
	}
	return IsLoopbackHost(authority.substr(0, hostEnd));
}

// The origin of a service base: scheme, host and port without a path. The scheme is https for any host,
// or http for a loopback host during local development. Empty for any other base.
inline std::string OriginOfBase(const std::string& base)
{
	size_t authorityStart = 8;
	if (base.compare(0, 8, "https://") != 0)
	{
		if (!IsLoopbackURL(base, "http"))
		{
			return std::string();
		}
		authorityStart = 7;
	}
	size_t authorityEnd = base.find_first_of("/?#", authorityStart);
	return base.substr(0, authorityEnd);
}

// The websocket scheme of an origin from OriginOfBase: wss:// for https and ws:// for a loopback http origin.
inline std::string WebSocketPrefixOfOrigin(const std::string& origin)
{
	return origin.compare(0, 8, "https://") == 0 ? "wss://" : "ws://";
}

// Accepts the realtime URL of a session registration only on the origin of the service route,
// so the access token never goes to another host. Returns an empty string otherwise.
inline std::string AcceptRealtimeUrl(const std::string& wsUrl, const std::string& serviceBase)
{
	std::string origin = OriginOfBase(serviceBase);
	if (origin.empty())
		return std::string();
	std::string expectedPrefix = WebSocketPrefixOfOrigin(origin) + origin.substr(origin.find("://") + 3) + "/";
	if (wsUrl.compare(0, expectedPrefix.size(), expectedPrefix) != 0)
		return std::string();
	if (wsUrl.find_first_of("?#@\\%\r\n\t ") != std::string::npos)
		return std::string();
	if (wsUrl.find("/../") != std::string::npos || wsUrl.find("/./") != std::string::npos)
		return std::string();
	// The hub path is /realtime below the path base of the server, never the legacy /ws.
	static const std::string HUB_PATH = "/realtime";
	const std::string path = wsUrl.substr(expectedPrefix.size() - 1);
	if (path.size() < HUB_PATH.size() || path.compare(path.size() - HUB_PATH.size(), HUB_PATH.size(), HUB_PATH) != 0)
		return std::string();
	return wsUrl;
}

// The discovery document of an environment, read from <origin>/.well-known/openid-configuration.
struct OpenIdConfiguration
{
	std::string issuer;
	std::string authorization_endpoint;
	std::string token_endpoint;
	std::string revocation_endpoint;
};

// An endpoint must be an https URL without credentials, query or fragment. When origin is a loopback
// http origin, an http URL on a loopback host is accepted too. An https origin never takes http.
inline bool IsAcceptableEndpoint(const std::string& url, const std::string& origin = std::string())
{
	if (url.find_first_of("?#@\\\r\n\t ") != std::string::npos)
		return false;
	if (IsLoopbackURL(url, "http") && IsLoopbackURL(origin, "http"))
	{
		return true;
	}
	if (url.compare(0, 8, "https://") != 0 || url.size() <= 8)
		return false;
	size_t authorityEnd = url.find('/', 8);
	return authorityEnd != 8;
}

// Reads the discovery document of origin. The issuer must be that origin, so a document copied
// from another environment is refused. The endpoints may sit on another https host when an operator
// runs the accounts service separately, or on a loopback http host for a loopback http origin.
inline bool ParseOpenIdConfiguration(const std::string& body, const std::string& origin, OpenIdConfiguration& out, std::string& error)
{
	out = OpenIdConfiguration();
	nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
	if (j.is_discarded() || !j.is_object())
	{
		error = "discovery document is not a JSON object";
		return false;
	}
	auto text = [&](const char* key, std::string& value) -> bool
	{
		auto it = j.find(key);
		if (it == j.end() || !it->is_string())
			return false;
		value = it->get<std::string>();
		return true;
	};
	if (!text("issuer", out.issuer))
	{
		error = "discovery document has no issuer";
		return false;
	}
	std::string issuer = out.issuer;
	if (!issuer.empty() && issuer.back() == '/')
		issuer.pop_back();
	if (issuer != origin)
	{
		error = "discovery issuer does not match the service origin";
		return false;
	}
	if (!text("authorization_endpoint", out.authorization_endpoint) || !IsAcceptableEndpoint(out.authorization_endpoint, origin) ||
		!text("token_endpoint", out.token_endpoint) || !IsAcceptableEndpoint(out.token_endpoint, origin))
	{
		error = "discovery document has no usable authorization or token endpoint";
		return false;
	}
	// Without a revocation endpoint, logout only ends the session.
	if (text("revocation_endpoint", out.revocation_endpoint) && !IsAcceptableEndpoint(out.revocation_endpoint, origin))
		out.revocation_endpoint.clear();
	return true;
}


struct TokenResponse : WireObject
{
	std::string access_token;
	std::string token_type;
	int64_t expires_in = 0;
	std::string scope;
	std::optional<std::string> id_token;
	std::string refresh_token;

	template <class V> void Fields(V& v)
	{
		v("access_token", access_token);
		v("token_type", token_type);
		v("expires_in", expires_in);
		v("scope", scope);
		v("id_token", id_token);
		v("refresh_token", refresh_token);
	}
};

// An OAuth error, for example invalid_grant after a reused refresh token.
struct OAuthError : WireObject
{
	std::string error;
	std::optional<std::string> error_description;

	template <class V> void Fields(V& v)
	{
		v("error", error);
		v("error_description", error_description);
	}
};

// The game client always sends its build identity.
struct AuthSessionRequest : WireObject
{
	std::optional<std::string> machine_guid;
	std::optional<std::string> mac_addr;
	std::optional<std::string> vol_serial;
	uint32_t exe_crc = 0;
	uint32_t ini_crc = 0;
	// -1 for none.
	int anticheat_id = -1;

	template <class V> void Fields(V& v)
	{
		v("machine_guid", machine_guid);
		v("mac_addr", mac_addr);
		v("vol_serial", vol_serial);
		v("exe_crc", exe_crc);
		v("ini_crc", ini_crc);
		v("anticheat_id", anticheat_id);
	}
};

struct AuthSessionResult : WireObject
{
	int64_t user_id = 0;
	std::string display_name;
	std::string session_type;
	std::string ws_url;

	template <class V> void Fields(V& v)
	{
		v("user_id", user_id);
		v("display_name", display_name);
		v("session_type", session_type);
		v("ws_url", ws_url);
	}
};

// An error of the game API. The spec form is error and params. Older services add a message, and put
// the reason and expires_at of a play ban at the top level, where a null expires_at is permanent.
// Callers read failures through ParseApiError of RealtimeRest.h, which accepts every shape.
struct ApiError : WireObject
{
	std::optional<std::string> message;
	std::string error;
	std::optional<nlohmann::json> params;
	std::optional<std::string> reason;
	std::optional<Nullable<std::string> > expires_at;

	template <class V> void Fields(V& v)
	{
		v("message", message);
		v("error", error);
		v("params", params);
		v("reason", reason);
		v("expires_at", expires_at);
	}
};

} // namespace GenOnlineRealtime
