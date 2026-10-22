#include "GameNetwork/GeneralsOnline/ServiceConnectionPolicy.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <wincred.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <thread>
#include <deque>
#include <mutex>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

namespace ServiceConnection
{
namespace
{
std::string Lower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return value;
}

std::wstring Wide(const std::string& value)
{
	if (value.empty())
		return {};
	int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0);
	if (!size)
		return {};
	std::wstring result(size, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), &result[0], size);
	return result;
}

std::string UTF8(const std::wstring& value)
{
	if (value.empty())
		return {};
	int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
	if (!size)
		return {};
	std::string result(size, '\0');
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), (int)value.size(), &result[0], size, nullptr, nullptr);
	return result;
}

std::string Part(CURLU* url, CURLUPart part, unsigned flags = 0)
{
	char* value = nullptr;
	if (curl_url_get(url, part, &value, flags) != CURLUE_OK)
		return {};
	std::string result(value);
	curl_free(value);
	return result;
}

bool CanonicalProxy(const std::string& input, std::string& canonical, std::string& error)
{
	error.clear();
	CURLU* url = curl_url();
	if (!url)
	{
		error = "Unable to allocate proxy URL parser.";
		return false;
	}
	bool valid = input.find_first_of("\r\n\t ") == std::string::npos &&
		curl_url_set(url, CURLUPART_URL, input.c_str(), CURLU_NON_SUPPORT_SCHEME) == CURLUE_OK;
	const auto scheme = Lower(Part(url, CURLUPART_SCHEME));
	const auto host = Lower(Part(url, CURLUPART_HOST));
	auto port = Part(url, CURLUPART_PORT);
	const auto path = Part(url, CURLUPART_PATH);
	const auto zone = Part(url, CURLUPART_ZONEID);
	valid = valid && (scheme == "http" || scheme == "https" || scheme == "socks5h") && !host.empty() && zone.empty() && port != "0" &&
		input.find('@') == std::string::npos && Part(url, CURLUPART_USER).empty() && Part(url, CURLUPART_PASSWORD).empty() &&
		input.find('?') == std::string::npos && input.find('#') == std::string::npos && (path.empty() || path == "/");
	if (valid)
	{
		if (port.empty())
			port = scheme == "https" ? "443" : scheme == "http" ? "80" : "1080";
		canonical = scheme + "://" + host + ":" + port;
	}
	else
		error = "Enter an http://, https:// or socks5h:// proxy address without credentials, path or query.";
	curl_url_cleanup(url);
	return valid;
}

std::wstring CredentialTarget(const std::string& canonical)
{
	return L"GeneralsOnline/Proxy/" + Wide(canonical);
}

bool Wildcard(const std::string& pattern, const std::string& value)
{
	size_t p = 0, v = 0, star = std::string::npos, retry = 0;
	while (v < value.size())
	{
		if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == value[v]))
		{
			++p;
			++v;
		}
		else if (p < pattern.size() && pattern[p] == '*')
		{
			star = p++;
			retry = v;
		}
		else if (star != std::string::npos)
		{
			p = star + 1;
			v = ++retry;
		}
		else
			return false;
	}
	while (p < pattern.size() && pattern[p] == '*')
		++p;
	return p == pattern.size();
}

std::vector<std::string> Tokens(std::string input)
{
	std::replace(input.begin(), input.end(), ';', ' ');
	std::istringstream stream(input);
	std::vector<std::string> result;
	for (std::string token; stream >> token;)
		result.push_back(token);
	return result;
}

bool StaticProxy(const std::wstring& proxy, const std::wstring& bypass, const std::string& destination,
	ResolvedProxyPolicy& result)
{
	CURLU* url = curl_url();
	if (!url || curl_url_set(url, CURLUPART_URL, destination.c_str(), 0) != CURLUE_OK)
	{
		if (url)
			curl_url_cleanup(url);
		result.error = "Invalid proxy lookup destination.";
		return false;
	}
	const auto host = Lower(Part(url, CURLUPART_HOST));
	const auto scheme = Lower(Part(url, CURLUPART_SCHEME));
	const auto port = Part(url, CURLUPART_PORT, CURLU_DEFAULT_PORT);
	curl_url_cleanup(url);
	for (auto token : Tokens(Lower(UTF8(bypass))))
	{
		if ((token == "<local>" && host.find('.') == std::string::npos && host.find(':') == std::string::npos) ||
			Wildcard(token, host) || Wildcard(token, host + ":" + port))
		{
			result.routes.push_back({});
			return true;
		}
	}
	bool specific = false;
	bool socksFallback = false;
	const auto entries = Tokens(UTF8(proxy));
	if (entries.empty())
	{
		result.error = "Windows has an empty static proxy list.";
		return false;
	}
	for (const auto& entry : entries)
	{
		const auto equal = entry.find('=');
		if (equal == std::string::npos)
			continue;
		const auto protocol = Lower(entry.substr(0, equal));
		if (protocol == scheme)
			specific = true;
		if (protocol == "socks")
			socksFallback = true;
	}
	for (auto entry : entries)
	{
		const auto equal = entry.find('=');
		if (equal != std::string::npos)
		{
			if (Lower(entry.substr(0, equal)) != scheme)
				continue;
			entry.erase(0, equal + 1);
		}
		else if (specific)
			continue;
		if (entry.find("://") == std::string::npos)
			entry = "http://" + entry;
		std::string canonical;
		if (!CanonicalProxy(entry, canonical, result.error))
		{
			result.routes.clear();
			return false;
		}
		result.routes.push_back({canonical, false, false});
	}
	// A protocol-specific system configuration without this protocol explicitly leaves it direct.
	if (result.routes.empty())
	{
		if (socksFallback)
		{
			result.error = "Windows SOCKS fallback is unsupported. Select an explicit supported proxy.";
			return false;
		}
		result.routes.push_back({});
	}
	return true;
}

struct ResolverContext
{
	std::atomic<unsigned> references{1};
	std::atomic<DWORD> error{ERROR_IO_PENDING};
	HANDLE completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	HANDLE closing = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	void Release()
	{
		if (references.fetch_sub(1) == 1)
		{
			if (completed)
				CloseHandle(completed);
			if (closing)
				CloseHandle(closing);
			delete this;
		}
	}
};

void CALLBACK ProxyCallback(HINTERNET, DWORD_PTR value, DWORD status, void* information, DWORD)
{
	auto* context = reinterpret_cast<ResolverContext*>(value);
	if (!context)
		return;
	if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
	{
		// signal before releasing: this reference keeps the event alive
		if (context->closing)
			SetEvent(context->closing);
		context->Release();
		return;
	}
	if (status == WINHTTP_CALLBACK_STATUS_GETPROXYFORURL_COMPLETE || status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
	{
		DWORD error = status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR ?
			static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError : ERROR_SUCCESS;
		context->error.store(error, std::memory_order_release);
		SetEvent(context->completed);
	}
}

ResolvedProxyPolicy AutoProxy(const std::wstring& destination, WINHTTP_AUTOPROXY_OPTIONS& options)
{
	ResolvedProxyPolicy result;
	HINTERNET session = WinHttpOpen(L"GeneralsOnline", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, WINHTTP_FLAG_ASYNC);
	if (!session)
	{
		result.error = "Unable to initialize Windows proxy resolution.";
		return result;
	}
	WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
	HINTERNET resolver = nullptr;
	DWORD status = ERROR_SUCCESS;
	if (WinHttpSetStatusCallback(session, ProxyCallback, WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
		WINHTTP_CALLBACK_FLAG_GETPROXYFORURL_COMPLETE | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
		status = GetLastError();
	if (!status)
		status = WinHttpCreateProxyResolver(session, &resolver);
	auto* context = new ResolverContext;
	bool callbackOwnsReference = false;
	if (!context->completed || !context->closing)
		status = GetLastError();
	if (!status)
	{
		DWORD_PTR value = reinterpret_cast<DWORD_PTR>(context);
		if (!WinHttpSetOption(resolver, WINHTTP_OPTION_CONTEXT_VALUE, &value, sizeof(value)))
			status = GetLastError();
		else
		{
			++context->references;
			callbackOwnsReference = true;
		}
	}
	if (!status)
	{
		status = WinHttpGetProxyForUrlEx(resolver, destination.c_str(), &options, reinterpret_cast<DWORD_PTR>(context));
		if (status == ERROR_IO_PENDING)
		{
			const DWORD wait = WaitForSingleObject(context->completed, 5000);
			status = wait == WAIT_OBJECT_0 ? context->error.load(std::memory_order_acquire) : ERROR_TIMEOUT;
		}
	}
	if (!status)
	{
		WINHTTP_PROXY_RESULT proxies{};
		status = WinHttpGetProxyResult(resolver, &proxies);
		if (!status)
		{
			for (DWORD i = 0; i < proxies.cEntries; ++i)
			{
				const auto& entry = proxies.pEntries[i];
				if (!entry.fProxy) { result.routes.push_back({}); continue; }
				std::string scheme;
				if (entry.ProxyScheme == INTERNET_SCHEME_HTTP)
					scheme = "http";
				else if (entry.ProxyScheme == INTERNET_SCHEME_HTTPS)
					scheme = "https";
				else
				{
					result.error = "Windows returned an unsupported proxy protocol.";
					break;
				}
				std::string host = entry.pwszProxy ? UTF8(entry.pwszProxy) : std::string();
				if (host.find(':') != std::string::npos && host.front() != '[')
					host = "[" + host + "]";
				std::string canonical;
				if (!CanonicalProxy(scheme + "://" + host + ":" + std::to_string(entry.ProxyPort), canonical, result.error))
					break;
				result.routes.push_back({canonical, false, false});
			}
			WinHttpFreeProxyResult(&proxies);
		}
	}
	if (resolver)
		WinHttpCloseHandle(resolver);
	// the resolver's final callback must run while its session is still open
	if (callbackOwnsReference)
		WaitForSingleObject(context->closing, 5000);
	WinHttpCloseHandle(session);
	// the callback owns the second reference, so a late or missed callback never frees a live context
	context->Release();
	if (status)
		result.error = "Windows proxy resolution failed (" + std::to_string(status) + "). Check system proxy settings.";
	if (result.routes.empty() && result.error.empty())
		result.error = "Windows returned no permitted proxy route.";
	if (!result.error.empty())
		result.routes.clear();
	return result;
}
}

bool ValidateProxyURL(const std::string& proxyURL, std::string& error)
{
	std::string canonical;
	return CanonicalProxy(proxyURL, canonical, error);
}

ResolvedProxyPolicy ResolveProxyPolicy(const ServiceConnectionSettings& settings, const std::string& destinationURL)
{
	ResolvedProxyPolicy result;
	if (settings.proxyMode == "direct") { result.routes.push_back({}); return result; }
	if (settings.proxyMode == "custom")
	{
		std::string canonical;
		if (CanonicalProxy(settings.proxyURL, canonical, result.error))
			result.routes.push_back({canonical, false, settings.integratedProxyAuth && canonical.find("socks5h://") != 0});
		return result;
	}
	if (settings.proxyMode != "system")
	{
		result.error = "Unknown proxy mode.";
		return result;
	}
	std::string destination = destinationURL;
	if (destination.compare(0, 6, "wss://") == 0)
		destination.replace(0, 6, "https://");
	else if (destination.compare(0, 5, "ws://") == 0)
		destination.replace(0, 5, "http://");
	WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config{};
	if (!WinHttpGetIEProxyConfigForCurrentUser(&config))
	{
		result.error = "Unable to read Windows proxy settings (" + std::to_string(GetLastError()) + ").";
		return result;
	}
	const std::wstring proxy = config.lpszProxy ? config.lpszProxy : L"";
	const std::wstring bypass = config.lpszProxyBypass ? config.lpszProxyBypass : L"";
	const std::wstring pac = config.lpszAutoConfigUrl ? config.lpszAutoConfigUrl : L"";
	const bool autoDetect = config.fAutoDetect != FALSE;
	GlobalFree(config.lpszProxy); GlobalFree(config.lpszProxyBypass); GlobalFree(config.lpszAutoConfigUrl);
	if (!pac.empty() || autoDetect)
	{
		WINHTTP_AUTOPROXY_OPTIONS options{};
		if (!pac.empty())
		{
			options.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
			options.lpszAutoConfigUrl = pac.c_str();
		}
		if (autoDetect)
		{
			options.dwFlags |= WINHTTP_AUTOPROXY_AUTO_DETECT;
			options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
		}
		options.fAutoLogonIfChallenged = FALSE;
		return AutoProxy(Wide(destination), options);
	}
	if (!proxy.empty())
		StaticProxy(proxy, bypass, destination, result);
	else
		result.routes.push_back({});
	return result;
}

std::shared_ptr<ProxyResolution> StartProxyResolution(ServiceConnectionSettings settings, std::string destinationURL)
{
	struct Work
	{
		ServiceConnectionSettings settings;
		std::string destination;
		std::shared_ptr<ProxyResolution> state;
	};
	struct Queue
	{
		std::mutex mutex;
		std::deque<Work> pending;
		unsigned workers = 0;
	};
	static auto queue = std::make_shared<Queue>();
	auto state = std::make_shared<ProxyResolution>();
	const auto sharedQueue = queue;
	std::lock_guard<std::mutex> lock(sharedQueue->mutex);
	if (sharedQueue->pending.size() >= 64)
	{
		state->result.error = "Too many pending proxy lookups. Try again shortly.";
		state->ready.store(true, std::memory_order_release);
		return state;
	}
	sharedQueue->pending.push_back({std::move(settings), std::move(destinationURL), state});
	if (sharedQueue->workers >= 4)
		return state;
	++sharedQueue->workers;
	try
	{
		std::thread([sharedQueue]() {
			for (;;)
			{
				Work work;
				{
					std::lock_guard<std::mutex> lock(sharedQueue->mutex);
					if (sharedQueue->pending.empty())
					{
						--sharedQueue->workers;
						return;
					}
					work = std::move(sharedQueue->pending.front());
					sharedQueue->pending.pop_front();
				}
				if (work.state.use_count() == 1)
					continue;
				try { work.state->result = ResolveProxyPolicy(work.settings, work.destination); }
				catch (...) { work.state->result.error = "Proxy resolution failed."; }
				work.state->ready.store(true, std::memory_order_release);
			}
		}).detach();
	}
	catch (...)
	{
		--sharedQueue->workers;
		sharedQueue->pending.pop_back();
		state->result.error = "Unable to start proxy resolution.";
		state->ready.store(true, std::memory_order_release);
	}
	return state;
}
bool SaveProxyCredentials(const std::string& proxyURL, const std::string& username,
	const std::string& password, std::string& error)
{
	std::string canonical;
	if (!CanonicalProxy(proxyURL, canonical, error))
		return false;
	auto user = Wide(username);
	auto secret = Wide(password);
	if (user.empty() || username.find('\0') != std::string::npos || password.find('\0') != std::string::npos ||
		(!password.empty() && secret.empty()) || secret.size() * sizeof(wchar_t) > CRED_MAX_CREDENTIAL_BLOB_SIZE)
	{
		if (!secret.empty())
			SecureZeroMemory(&secret[0], secret.size() * sizeof(wchar_t));
		error = "Proxy credentials are invalid or too long.";
		return false;
	}
	auto target = CredentialTarget(canonical);
	CREDENTIALW credential{};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = &target[0];
	credential.UserName = &user[0];
	credential.CredentialBlobSize = (DWORD)(secret.size() * sizeof(wchar_t));
	credential.CredentialBlob = reinterpret_cast<LPBYTE>(secret.empty() ? nullptr : &secret[0]);
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	bool success = CredWriteW(&credential, 0) != FALSE;
	DWORD status = success ? ERROR_SUCCESS : GetLastError();
	if (!secret.empty())
		SecureZeroMemory(&secret[0], secret.size() * sizeof(wchar_t));
	if (!success)
		error = "Unable to save proxy credentials (" + std::to_string(status) + ").";
	return success;
}

bool DeleteProxyCredentials(const std::string& proxyURL, std::string& error)
{
	std::string canonical;
	if (!CanonicalProxy(proxyURL, canonical, error))
		return false;
	if (CredDeleteW(CredentialTarget(canonical).c_str(), CRED_TYPE_GENERIC, 0))
		return true;
	DWORD status = GetLastError();
	if (status == ERROR_NOT_FOUND)
		return true;
	error = "Unable to remove proxy credentials (" + std::to_string(status) + ").";
	return false;
}

bool ConfigureCurl(CURL* curl, const std::string& destinationURL, const ProxyRoute& route, std::string& error)
{
	if (!ConfigureServiceTransport(curl, destinationURL, error))
		return false;
	const bool websocket = destinationURL.compare(0, 6, "wss://") == 0;
	auto set = [&](CURLoption option, auto value) {
		CURLcode code = curl_easy_setopt(curl, option, value);
		if (code != CURLE_OK && error.empty())
			error = "Unable to configure service transport: " + std::string(curl_easy_strerror(code));
		return code == CURLE_OK;
	};
	const auto* version = curl_version_info(CURLVERSION_NOW);
	const long protocol = websocket ? CURL_HTTP_VERSION_1_1 :
		route.direct && (version->features & CURL_VERSION_HTTP3) ? CURL_HTTP_VERSION_3 :
		(version->features & CURL_VERSION_HTTP2) ? CURL_HTTP_VERSION_2TLS : CURL_HTTP_VERSION_1_1;
	if (!set(CURLOPT_HTTP_VERSION, protocol) || !set(CURLOPT_PROXY_SSL_VERIFYPEER, 1L) || !set(CURLOPT_PROXY_SSL_VERIFYHOST, 2L) ||
		!set(CURLOPT_PROXY_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA) || !set(CURLOPT_SOCKS5_AUTH, (long)CURLAUTH_BASIC) ||
		!set(CURLOPT_HEADEROPT, (long)CURLHEADER_SEPARATE) || !set(CURLOPT_NOPROXY, "") || !set(CURLOPT_PROXYAUTH, (long)CURLAUTH_NONE))
		return false;
	if (route.direct)
		return true;
	std::string canonical;
	if (!CanonicalProxy(route.url, canonical, error))
		return false;
	if (!set(CURLOPT_PROXY, canonical.c_str()))
		return false;
	PCREDENTIALW credential = nullptr;
	if (CredReadW(CredentialTarget(canonical).c_str(), CRED_TYPE_GENERIC, 0, &credential))
	{
		bool valid = credential->UserName && *credential->UserName && credential->CredentialBlobSize % sizeof(wchar_t) == 0;
		std::string user = valid ? UTF8(credential->UserName) : std::string();
		std::wstring wideSecret;
		if (valid && credential->CredentialBlobSize)
			wideSecret.assign(reinterpret_cast<wchar_t*>(credential->CredentialBlob), credential->CredentialBlobSize / sizeof(wchar_t));
		std::string secret = UTF8(wideSecret);
		valid = valid && !user.empty() && secret.find('\0') == std::string::npos;
		bool success = valid && set(CURLOPT_PROXYUSERNAME, user.c_str()) && set(CURLOPT_PROXYPASSWORD, secret.c_str()) &&
			set(CURLOPT_PROXYAUTH, (long)(CURLAUTH_BASIC | CURLAUTH_DIGEST | CURLAUTH_NTLM | CURLAUTH_NEGOTIATE));
		if (!secret.empty())
			SecureZeroMemory(&secret[0], secret.size());
		if (!wideSecret.empty())
			SecureZeroMemory(&wideSecret[0], wideSecret.size() * sizeof(wchar_t));
		if (credential->CredentialBlobSize)
			SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
		CredFree(credential);
		if (!valid)
			error = "Stored proxy credentials are invalid. Save them again.";
		return success;
	}
	DWORD status = GetLastError();
	if (status != ERROR_NOT_FOUND)
	{
		error = "Unable to read saved proxy credentials (" + std::to_string(status) + ").";
		return false;
	}
	if (route.allowIntegratedAuth)
	{
		if (!(version->features & CURL_VERSION_SSPI))
		{
			error = "This build does not support Windows proxy authentication.";
			return false;
		}
		return set(CURLOPT_PROXYAUTH, (long)(CURLAUTH_NEGOTIATE | CURLAUTH_NTLM)) && set(CURLOPT_PROXYUSERPWD, ":");
	}
	return true;
}

const char* ProtocolName(long version)
{
	switch (version)
	{
		case CURL_HTTP_VERSION_1_0: return "HTTP/1.0";
		case CURL_HTTP_VERSION_1_1: return "HTTP/1.1";
		case CURL_HTTP_VERSION_2_0: return "HTTP/2";
		case CURL_HTTP_VERSION_3: return "HTTP/3";
		default: return "unknown";
	}
}

const char* FailurePhase(CURLcode code, long status, long proxyStatus)
{
	if (proxyStatus == 407 || status == 407)
		return "proxy authentication";
	if (code == CURLE_COULDNT_RESOLVE_PROXY)
		return "proxy DNS";
	if (code == CURLE_COULDNT_RESOLVE_HOST)
		return "endpoint DNS";
	if (code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CONNECT_ERROR ||
		code == CURLE_SSL_CACERT_BADFILE || code == CURLE_SSL_CERTPROBLEM) return "TLS";
	if (code == CURLE_COULDNT_CONNECT)
		return "connection";
	if (code == CURLE_OPERATION_TIMEDOUT)
		return "timeout";
	if (status == 401 || status == 403)
		return "service authentication";
	if (code != CURLE_OK)
		return "transport";
	if (status >= 400)
		return "service response";
	return "none";
}
}
