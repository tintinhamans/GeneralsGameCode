#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineRealtimeBridge.h"
#include "GameNetwork/GenOnline/NetworkMesh.h"
#include "GameNetwork/GenOnline/WifiLowLatency.h"
#include "GameNetwork/GenOnline/HTTP/HTTPManager.h"
#include "../json.hpp"
#include "GameClient/MessageBox.h"
#include "Common/FileSystem.h"
#include "Common/file.h"
#include "GameNetwork/DownloadManager.h"
#include <ws2tcpip.h>
#include "GameClient/DisplayStringManager.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/ConnectionManager.h"
#include "Common/MultiplayerSettings.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameClient/Display.h"
#include "WW3D2/surfaceclass.h"
#include "WW3D2/dx8wrapper.h"
#include <algorithm>
#include <climits>
#include <mutex>
#include <utility>
#include "GameNetwork/GenOnline/ServiceEndpointPolicy.h"
#include "GameNetwork/GenOnline/ProxyLogin.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "GameNetwork/GenOnline/Vendor/stb_image/stb_image_write.h"
#include "GameNetwork/GenOnline/Vendor/stb_image/stb_image_resize.h"
#include "GameClient/GameText.h"
#include "GameLogic/GameLogic.h"
#include <unordered_set>
#include <random>
#include <algorithm>

extern "C"
{
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

GenOnlineServicesManager* GenOnlineServicesManager::m_pOnlineServicesManager = nullptr;
std::recursive_mutex GenOnlineServicesManager::m_singletonMutex;

std::thread::id GenOnlineServicesManager::g_MainThreadID;
std::mutex GenOnlineServicesManager::m_ScreenshotMutex;
std::vector<S3ScreenshotEntry> GenOnlineServicesManager::m_vecGuardedSSData;


int GenOnlineServicesManager::g_networkOverlayMode;

NetworkMesh* GenOnlineServicesManager::GetNetworkMesh()
{
	if (m_pOnlineServicesManager != nullptr)
	{
		GenOnlineLobbyInterface* pLobbyInterface = GetInterface< GenOnlineLobbyInterface>();
		if (pLobbyInterface != nullptr)
		{
			return pLobbyInterface->GetNetworkMeshForLobby();
		}
	}

	return nullptr;
}


void GenOnlineServicesManager::GetAndParseServiceConfig(std::function<void(void)> cbOnDone)
{
	// SECURITY FIX: Capture manager instance through GetInstance() to ensure thread-safety
	// Lambda will check if manager still exists before accessing members
	GenOnlineRealtimeBridge::CallRest(GenOnlineRealtimeBridge::RestMethod::Get, GenOnlineRealtime::RestConfigPath(), std::string(), [cbOnDone](const GenOnlineRealtime::RestOutcome& outcome)
		{
			try
			{
				// SECURITY FIX: Re-acquire manager pointer inside lambda to check for shutdown
				GenOnlineServicesManager* pMgr = GenOnlineServicesManager::GetInstance();
				if (pMgr == nullptr)
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Manager destroyed during service config request");
					return;
				}
				
				GenOnlineRealtime::ConfigResponse config;
				if (outcome.Data(config))
				{
					pMgr->m_ServiceConfig = config.service_config.get<ServiceConfig>();
				}
				else
				{
					// keep the last good config (defaults if there never was one); resetting would turn off retry_signalling
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Failed to get service config, keeping the current one. Status code: %d", outcome.status);
				}
				
			}
			catch (...)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Failed to parse service config, keeping the current one.");
			}

			if (cbOnDone != nullptr)
			{
				cbOnDone();
			}
		});
}

// Staggers the screenshot upload by match slot so players do not upload at once.
static std::chrono::steady_clock::time_point GetScreenshotUploadTime()
{
	int slotIndex = 0;

	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pAuthInterface != nullptr && pLobbyInterface != nullptr)
	{
		const int64_t localUserID = pAuthInterface->GetUserID();
		for (const LobbyMemberEntry& lobbyMember : pLobbyInterface->GetMembersListForCurrentRoom())
		{
			if (lobbyMember.user_id == localUserID)
			{
				slotIndex = lobbyMember.HasValidSlot() ? lobbyMember.m_SlotIndex : 0;
				break;
			}
		}
	}

	const int slotOffsetMS = std::clamp(GenOnlineServicesManager::GetInstance()->GetServiceConfig().telemetry.screenshot_slot_offset_ms, 0, 10000);

	static std::mt19937 rng{ std::random_device{}() };
	const int jitterMS = std::uniform_int_distribution<int>(0, 500)(rng);

	return std::chrono::steady_clock::now() + std::chrono::milliseconds(slotIndex * slotOffsetMS + jitterMS);
}

void GenOnlineServicesManager::CaptureScreenshotForProbe(EScreenshotType screenshotType, std::string strURI)
{
	GenOnlineServicesManager* pOnlineServicesMgr = GenOnlineServicesManager::GetInstance();
	if (pOnlineServicesMgr != nullptr)
	{
		ServiceConfig& serviceConf = pOnlineServicesMgr->GetServiceConfig();

		if (serviceConf.telemetry.screenshot_probes)
		{
			CHECK_MAIN_THREAD;

			// the capture stalls every client at the same time, the run ahead controller must not read that as the network
			if (TheNetwork != nullptr && TheNetwork->GetConnectionManager() != nullptr)
			{
				TheNetwork->GetConnectionManager()->noteScreenshotCapture(screenshotType == EScreenshotType::SCREENSHOT_TYPE_GAMEPLAY);
			}

			GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobbyInterface != nullptr)
			{
				const uint64_t matchID = pLobbyInterface->GetCurrentMatchID();
				const std::chrono::steady_clock::time_point uploadTime = GetScreenshotUploadTime();
				GenOnlineServicesManager::GetInstance()->CaptureScreenshot(true, [strURI = std::move(strURI), screenshotType, matchID, uploadTime](std::vector<uint8_t> vecData)
					{
						CHECK_WORKER_THREAD;

						if (vecData.empty())
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "Screenshot capture failed, no data");
							return;
						}

                        // certain screenshots require caching for later upload when we have a valid match id, so we store them
						if (screenshotType == EScreenshotType::SCREENSHOT_TYPE_LOADSCREEN)
						{
							GenOnlineServicesManager::GetInstance()->CacheScreenshotBytes_StartMatch(std::move(vecData));
						}
                        else if (screenshotType == EScreenshotType::SCREENSHOT_TYPE_SCORESCREEN)
                        {
							GenOnlineServicesManager::GetInstance()->CacheScreenshotBytes_EndMatch(matchID, std::move(vecData));
                        }
						else
						{
                            // send back to main thread for processing
							// NOTE: we don't lock in the above cases because the called functions lock
                            std::scoped_lock<std::mutex> ssLock(m_ScreenshotMutex);

                            S3ScreenshotEntry newEntry;
                            newEntry.vecBytes = std::move(vecData);
                            newEntry.strSignedURI = strURI;
                            newEntry.screenshotType = screenshotType;
                            newEntry.notBefore = uploadTime;
                            m_vecGuardedSSData.push_back(std::move(newEntry));
						}
					});
			}
		}
	}
}

GenOnlineSettings GenOnlineServicesManager::Settings;

GenOnlineServicesManager::GenOnlineServicesManager()
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Init");
#if _DEBUG
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] WARNING: TLS verification is disabled in debug builds");
#endif

	m_pOnlineServicesManager = this;
}

static const char* ServiceEnvironment()
{
	switch (GenOnlineServicesManager::g_Environment)
	{
		case GenOnlineServicesManager::EEnvironment::DEV: return "dev";
		case GenOnlineServicesManager::EEnvironment::TEST: return "test";
		default: return "prod";
	}
}

std::string GenOnlineServicesManager::GetAPIEndpoint(const char* szEndpoint)
{
	auto* manager = GetInstance();
	if (manager && !manager->m_serviceBase.empty())
	{
		return manager->m_serviceBase + szEndpoint;
	}
	const auto routes = ServiceConnection::BootstrapRoutes(ServiceEnvironment(), Settings.Network_GetConnectionSettings(), Settings.Network_GetLastRoute());
	return routes.empty() ? std::string() : routes.front() + szEndpoint;
}

bool GenOnlineServicesManager::IsServiceURLTrusted(const std::string& url)
{
	return ServiceConnection::IsTrustedServiceURL(url, GetAPIEndpoint(""), false);
}

void GenOnlineServicesManager::EnsureServiceRoute(std::function<void(bool)> callback)
{
	// never change the route of an authenticated session behind its caller
	if (m_pAuthInterface && m_pAuthInterface->IsLoggedIn())
	{
		callback(!m_serviceBase.empty());
		return;
	}
	const auto settings = Settings.Network_GetConnectionSettings();
	const std::string key = settings.serverMode + "|" + settings.serverAddress + "|" + settings.proxyMode + "|" + settings.proxyURL + (settings.windowsLogin ? "|1" : "|0");
	if (key == m_routeSettingsKey && !m_serviceBase.empty() && std::chrono::steady_clock::now() - m_routeValidatedAt < std::chrono::seconds(30))
	{
		callback(true);
		return;
	}
	m_routeCallbacks.push_back(std::move(callback));
	if (m_routeCallbacks.size() > 1)
	{
		return;
	}
	m_routeSettingsKey = key;
	m_routeProxyPrompted = false;
	m_routeProxyAuthFailed = false;
	m_bootstrapRoutes = ServiceConnection::BootstrapRoutes(ServiceEnvironment(), settings, Settings.Network_GetLastRoute());
	m_bootstrapIndex = 0;
	ProbeServiceRoute();
}

void GenOnlineServicesManager::FinishServiceRoute(bool success)
{
	auto callbacks = std::move(m_routeCallbacks);
	m_routeCallbacks.clear();
	for (auto& callback : callbacks)
	{
		callback(success);
	}
}

void GenOnlineServicesManager::ProbeServiceRoute()
{
	if (m_bootstrapIndex >= m_bootstrapRoutes.size())
	{
		m_serviceBase.clear();
		FinishServiceRoute(false);
		return;
	}

	// anonymous request: no credentials are sent until a route is validated
	m_serviceBase = m_bootstrapRoutes[m_bootstrapIndex];
	const std::string url = m_serviceBase + ServiceConnection::CompatibilityProbePath();
	// A standby answer of the route probes the same route again.
	GenOnlineRealtimeBridge::SendWithStandbyRetry("GET /api/v1/compatibility route probe",
		[url](GenOnlineRealtimeBridge::HTTPDoneCallback fnAnswer)
		{
			auto* manager = GetInstance();
			if (manager == nullptr || manager->m_pHTTPManager == nullptr)
			{
				fnAnswer(false, 0, std::string(), nullptr);
				return;
			}
			std::map<std::string, std::string> headers;
			manager->m_pHTTPManager->SendGETRequest(url.c_str(), EIPProtocolVersion::DONT_CARE, headers, fnAnswer, nullptr, 6000);
		},
		[](bool success, int status, std::string body, HTTPRequest* request)
		{
			auto* manager = GetInstance();
			if (!manager)
			{
				return;
			}
			// the proxy wants a login: ask once per attempt, then probe the same route again
			const ServiceConnection::ProxyRoute route = status == 407 && request != nullptr ? request->GetProxyRoute() : ServiceConnection::ProxyRoute();
			if (!route.direct)
			{
				manager->m_routeProxyAuthFailed = true;
				if (!manager->m_routeProxyPrompted)
				{
					manager->m_routeProxyPrompted = true;
					ServiceConnection::RequestProxyLogin(route, [](bool loggedIn)
					{
						auto* manager = GetInstance();
						if (!manager)
						{
							return;
						}
						if (!loggedIn)
						{
							manager->m_bootstrapIndex = manager->m_bootstrapRoutes.size();
						}
						manager->ProbeServiceRoute();
					});
					return;
				}
			}
			if (success && status == 200 && GenOnlineRealtime::IsCompatibilityAnswer(body))
			{
				manager->m_routeProxyAuthFailed = false;
				manager->m_routeValidatedAt = std::chrono::steady_clock::now();
				if (g_Environment == EEnvironment::PROD && Settings.Network_GetConnectionSettings().serverMode != "custom")
				{
					Settings.Network_SaveLastRoute(ServiceConnection::RouteHost(manager->m_serviceBase));
				}
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Connection] Trusted service route validated: %s", manager->m_serviceBase.c_str());
				manager->FinishServiceRoute(true);
				return;
			}
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Connection] Route validation failed (HTTP %d); credentials were not sent", status);
			// an access or proxy rejection is not evidence that another route would work
			manager->m_bootstrapIndex = (status == 401 || status == 403 || status == 407) ? manager->m_bootstrapRoutes.size() : manager->m_bootstrapIndex + 1;
			manager->ProbeServiceRoute();
		});
}

void GenOnlineServicesManager::AttemptLoadSteam()
{
    // app id for ZH
    SetEnvironmentVariableA("SteamAppId", "2732960");

    // load steam 32bit dll from local dir if present
    HMODULE steamDll = LoadLibraryA("steam_api.dll");	
    if (!steamDll)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "steam_api.dll not found. Running without Steam.");
        return;
    }

	// Init func
    typedef bool (*SteamAPI_Init_t)();
    SteamAPI_Init_t SteamAPI_Init = (SteamAPI_Init_t)GetProcAddress(steamDll, "SteamAPI_InitSafe");

    if (!SteamAPI_Init)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "SteamAPI_Init not found in DLL.");
        FreeLibrary(steamDll);
        return;
    }

    // steam init
    if (SteamAPI_Init())
	{
        NetworkLog(ELogVerbosity::LOG_RELEASE, "SteamAPI_Init succeeded.");
    }
    else
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "SteamAPI_Init failed.");
    }
}

void GenOnlineServicesManager::CommitReplay(AsciiString absoluteReplayPath)
{
	GenOnlineServicesManager* pOnlineServicesMgr = GenOnlineServicesManager::GetInstance();
	if (pOnlineServicesMgr != nullptr)
	{
		ServiceConfig& serviceConf = pOnlineServicesMgr->GetServiceConfig();

		if (serviceConf.telemetry.replay_upload)
		{
			GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			const uint64_t matchID = pLobbyInterface == nullptr ? 0 : pLobbyInterface->GetCurrentMatchID();
			if (matchID == 0)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[MediaUpload] Cannot cache replay: match ID is unavailable");
				return;
			}
			FILE* pFile = fopen(absoluteReplayPath.str(), "rb");

			std::vector<unsigned char> replayData;
			if (pFile)
			{
				fseek(pFile, 0, SEEK_END);
				long fileSize = ftell(pFile);
				fseek(pFile, 0, SEEK_SET);
				if (fileSize > 0)
				{
					replayData.resize(fileSize);
					fread(replayData.data(), 1, fileSize, pFile);
				}
				fclose(pFile);
			}

			// cache the data until we get an S3 URL from server
			GenOnlineServicesManager::GetInstance()->CacheReplayBytes(matchID, std::move(replayData));
		}
	}
}

void GenOnlineServicesManager::CacheMatchUploadBytes(CachedMatchUpload& upload, uint64_t matchID, std::vector<uint8_t> data)
{
	if (matchID == 0)
	{
		return;
	}

	std::scoped_lock<std::mutex> ssLock(m_ScreenshotMutex);
	upload.dataMatchID = matchID;
	upload.bytes = std::move(data);
}

void GenOnlineServicesManager::CacheMatchUploadURI(CachedMatchUpload& upload, uint64_t matchID, std::string uri)
{
	if (matchID == 0)
	{
		return;
	}

	std::scoped_lock<std::mutex> ssLock(m_ScreenshotMutex);
	upload.uriMatchID = matchID;
	upload.signedURI = std::move(uri);
}

void GenOnlineServicesManager::CacheScreenshotBytes_EndMatch(uint64_t matchID, std::vector<uint8_t> data)
{
	CacheMatchUploadBytes(m_cachedMatchEndUpload, matchID, std::move(data));
}

void GenOnlineServicesManager::CacheReplayBytes(uint64_t matchID, std::vector<uint8_t> data)
{
	CacheMatchUploadBytes(m_cachedReplayUpload, matchID, std::move(data));
}

void GenOnlineServicesManager::SetScreenshotS3URI_EndMatch(uint64_t matchID, std::string uri)
{
	CacheMatchUploadURI(m_cachedMatchEndUpload, matchID, std::move(uri));
}

void GenOnlineServicesManager::SetScreenshotS3URI_Replay(uint64_t matchID, std::string uri)
{
	CacheMatchUploadURI(m_cachedReplayUpload, matchID, std::move(uri));
}

void GenOnlineServicesManager::WaitForScreenshotThreads()
{
	std::scoped_lock<std::mutex> lock(m_mutexScreenshotThreads);
	
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Waiting for %d screenshot threads to complete...", (int)m_vecScreenshotThreads.size());
	
	for (std::thread* pThread : m_vecScreenshotThreads)
	{
		if (pThread != nullptr && pThread->joinable())
		{
			pThread->join();
			delete pThread;
		}
	}
	
	m_vecScreenshotThreads.clear();
	
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] All screenshot threads completed");
}

void GenOnlineServicesManager::Shutdown()
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] OnlineServicesManager shutdown initiated");

	// First, wait for all screenshot threads to complete
	// This prevents race conditions where threads might still be using resources
	WaitForScreenshotThreads();

	WifiLowLatency::End();

	// Queued retries capture the interfaces of this instance, so none of them may run on the next one.
	GenOnlineRealtimeBridge::ShutdownRest();

	NetworkMeshLibrary::Shutdown();

	// Shutdown and completely destroy WebSocket BEFORE cleaning up HTTPManager
	// This is critical because WebSocket has curl handles that must be freed
	// before the HTTP multi handle is destroyed.
	if (m_pWebSocket)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Shutting down WebSocket...");
		m_pWebSocket->Shutdown();
		
		// Reset shared_ptr to fully destroy WebSocket and free all its curl resources
		// This must happen before HTTPManager shutdown to avoid accessing freed curl state
		m_pWebSocket.reset();
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] WebSocket shutdown complete");
	}

	// cancel remaining HTTP requests
	if (m_pHTTPManager != nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Shutting down HTTPManager...");
		m_pHTTPManager->Shutdown();
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] HTTPManager shutdown complete");
	}

	GenOnlinePluginHost::UnloadPlugin();

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] OnlineServicesManager shutdown complete");
}

void GenOnlineServicesManager::StartVersionCheck(std::function<void(bool bSuccess, bool bNeedsUpdate)> fnCallback)
{
    EnsureServiceRoute([fnCallback](bool success)
    {
        auto* manager = GetInstance();
        if (!manager)
            return;
        if (!success)
        {
            fnCallback(false, false);
            return;
        }
        manager->StartVersionCheckOnSelectedRoute(fnCallback);
    });
}

void GenOnlineServicesManager::StartVersionCheckOnSelectedRoute(std::function<void(bool, bool)> fnCallback)
{
	// The check is anonymous, the session registration checks the executable.
	const std::string strURI = GetAPIEndpoint("") + ServiceConnection::CompatibilityProbePath();
	GenOnlineRealtimeBridge::SendWithStandbyRetry("GET /api/v1/compatibility version check",
		[strURI](GenOnlineRealtimeBridge::HTTPDoneCallback fnAnswer)
		{
			auto* manager = GetInstance();
			if (manager == nullptr || manager->m_pHTTPManager == nullptr)
			{
				fnAnswer(false, 0, std::string(), nullptr);
				return;
			}
			std::map<std::string, std::string> mapHeaders;
			manager->m_pHTTPManager->SendGETRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, fnAnswer, nullptr, -1);
		},
		[=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "Version Check: Response code was %d and body was %s", statusCode, strBody.c_str());
			GenOnlineRealtime::CompatibilityResponse answer;
			switch (GenOnlineRealtime::CompatibilityVerdictOf(GenOnlineRealtime::MakeRestOutcome(bSuccess, statusCode, strBody), answer))
			{
			case GenOnlineRealtime::CompatibilityVerdict::UpToDate:
				NetworkLog(ELogVerbosity::LOG_RELEASE, "VERSION CHECK: Up To Date");
				fnCallback(true, false);
				break;
			case GenOnlineRealtime::CompatibilityVerdict::NeedsUpdate:
				NetworkLog(ELogVerbosity::LOG_RELEASE, "VERSION CHECK: Needs Update");

				// cache the data
				m_patcher_name = answer.update.value.patcher_name;
				m_patcher_path = answer.update.value.patcher_path;
				m_patcher_size = answer.update.value.patcher_size.value;

				fnCallback(true, true);
				break;
			default:
				NetworkLog(ELogVerbosity::LOG_RELEASE, "VERSION CHECK: Failed");
				fnCallback(false, false);
				break;
			}
		},
		[this]()
		{
			return GetInstance() == this;
		});
}

void GenOnlineServicesManager::ContinueUpdate()
{
	if (m_vecFilesToDownload.size() > 0 && m_pHTTPManager != nullptr) // download next
	{
		std::string strDownloadPath = m_vecFilesToDownload.front();
		m_vecFilesToDownload.pop();

		uint32_t downloadSize = m_vecFilesSizes.front();
		m_vecFilesSizes.pop();

		if (TheDownloadManager != nullptr)
		{
			TheDownloadManager->SetFileName(AsciiString(strDownloadPath.c_str()));
			TheDownloadManager->OnStatusUpdate(DOWNLOADSTATUS_DOWNLOADING);
		}

		// TheSuperHackers @bugfix arcticdolphin 09/11/2026 The replies of a download cancelled or restarted since are ignored.
		const int updateAttempt = m_updateAttempt;

		// this isnt a super nice way of doing this, lets make a download manager
		std::map<std::string, std::string> mapHeaders;
		m_pHTTPManager->SendGETRequest(strDownloadPath.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
			{
				if (updateAttempt != m_updateAttempt)
				{
					return;
				}

				auto fnShowUpdateFailed = [this]()
					{
						// TheSuperHackers @feature arcticdolphin 09/11/2026 The sign-in dialog shows the failure itself when it started the download.
						std::function<void(void)> fnFailed;
						{
							std::scoped_lock<std::recursive_mutex> lock(m_updateCallbackMutex);
							fnFailed = m_updateFailedCallback;
						}
						if (fnFailed != nullptr)
						{
							fnFailed();
						}
						else
						{
							// show msg
							ClearGSMessageBoxes();
							MessageBoxOk(UnicodeString(L"Update Failed"), UnicodeString(L"Could not download the updater. Press below to exit."), []()
								{
									TheGameEngine->setQuitting(TRUE);
								});
						}
						ShellExecuteA(NULL, "open", "https://www.playgenerals.online/updatefailed", NULL, NULL, SW_SHOWNORMAL);
					};

				if (statusCode != 200)
				{
					fnShowUpdateFailed();
				}
				else
				{
					// set done
					if (TheDownloadManager != nullptr)
					{
						TheDownloadManager->OnProgressUpdate(downloadSize, downloadSize, 0, 0);
					}

					m_vecFilesDownloaded.push_back(strDownloadPath);

					std::string strPatchDir = GetPatcherDirectoryPath();
					if (strPatchDir.empty())
						return;

					// save under the validated patcher name so LaunchPatcher runs this exact file
					std::string strOutPath = std::format("{}/{}", strPatchDir, m_patcher_name);

					std::vector<uint8_t> vecBuffer = pReq->GetBuffer();
					size_t bufSize = pReq->GetBufferSize();

					std::error_code ec;
					if (!std::filesystem::exists(strPatchDir, ec))
					{
						std::filesystem::create_directory(strPatchDir, ec);
					}

					// a partially written updater must not be treated as downloaded
					bool bSaved = false;
					FILE* pFile = fopen(strOutPath.c_str(), "wb");
					if (pFile != nullptr) {
						bSaved = fwrite(vecBuffer.data(), sizeof(uint8_t), bufSize, pFile) == bufSize;
						if (fclose(pFile) != 0)
							bSaved = false;
					}

					if (!bSaved)
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "Failed to save downloaded file: %s", strOutPath.c_str());
						fnShowUpdateFailed();
						return;
					}

					// call continue update again, thisll check if we're done or have more work to do
					ContinueUpdate();

					NetworkLog(ELogVerbosity::LOG_RELEASE, "GOT FILE: %s", strDownloadPath.c_str());
				}
			},
			[=](size_t bytesReceived)
			{
				if (updateAttempt != m_updateAttempt)
				{
					return;
				}

				//m_bytesReceivedSoFar += bytesReceived;

				if (TheDownloadManager != nullptr)
				{
					TheDownloadManager->OnProgressUpdate(bytesReceived, downloadSize, -1, -1);
				}
			},
			30 * 60 * 1000 // the 5s default aborts any real download
			);
	}
	else if (m_vecFilesToDownload.size() == 0 && m_vecFilesDownloaded.size() > 0) // nothing left but we did download something
	{
		if (TheDownloadManager != nullptr)
		{
			TheDownloadManager->SetFileName("Update is complete!");
			TheDownloadManager->OnStatusUpdate(DOWNLOADSTATUS_FINISHING);
		}

		std::scoped_lock<std::recursive_mutex> lock(m_updateCallbackMutex);
		if (m_updateCompleteCallback != nullptr)
		{
			m_updateCompleteCallback();
		}
	}
	
}


void GenOnlineServicesManager::CaptureScreenshot(bool bResizeForTransmit, std::function<void(std::vector<unsigned char>)> cbOnDataAvailable)
{
	CHECK_MAIN_THREAD;

	bool bSucceeded = false;

	// no callback, nothing to do, early out
	if (cbOnDataAvailable == nullptr)
	{
		return;
	}

	SurfaceClass* surface = DX8Wrapper::_Get_DX8_Back_Buffer();
	SurfaceClass* surfaceCopy = nullptr;
	void* pBits = nullptr;
	IDirect3DSurface8* pDXsurf = nullptr;

	if (surface != nullptr)
	{
		SurfaceClass::SurfaceDescription surfaceDesc;
		surface->Get_Description(surfaceDesc);
		
		pDXsurf = DX8Wrapper::_Create_DX8_Surface(surfaceDesc.Width, surfaceDesc.Height, surfaceDesc.Format);
		
		if (pDXsurf != nullptr)
		{
			surfaceCopy = NEW_REF(SurfaceClass, (pDXsurf));

			if (surfaceCopy != nullptr)
			{
				DX8Wrapper::_Copy_DX8_Rects(surface->Peek_D3D_Surface(), NULL, 0, surfaceCopy->Peek_D3D_Surface(), NULL);

				// gather all our data
				int pitch = 0;
				pBits = surfaceCopy->Lock(&pitch);

				if (pBits != nullptr)
				{
					int width = surfaceDesc.Width;
					int height = surfaceDesc.Height;

					// Copy pixel data into an owned buffer before spawning the thread so
					// the surface can be safely unlocked on the main thread immediately after.
					const uint8_t* pSurfaceBytes = static_cast<const uint8_t*>(pBits);
					std::vector<uint8_t> pixelData(pSurfaceBytes, pSurfaceBytes + height * pitch);

					// process on thread - track the thread so we can join it during shutdown
					std::thread* pNewThread = new std::thread([cbOnDataAvailable, width, height, pixelData = std::move(pixelData), pitch, bResizeForTransmit]() mutable
						{
							CHECK_WORKER_THREAD;

							const int channels = 3;
							int finalWidth = width;
							int finalHeight = height;

							// Packs BGRA rows to tight RGB in place because the write never passes the read.
							uint8_t* pixels = pixelData.data();
							for (int y = 0; y < height; ++y)
							{
								const uint8_t* src = pixels + y * pitch;
								uint8_t* dst = pixels + y * width * channels;
								for (int x = 0; x < width; ++x, src += 4, dst += channels)
								{
									const uint8_t blue = src[0];
									dst[0] = src[2]; // R
									dst[1] = src[1]; // G
									dst[2] = blue;   // B
								}
							}

							// resize
							std::vector<uint8_t> resized;
							if (bResizeForTransmit)
							{
								ServiceConfig& serviceConf = GenOnlineServicesManager::GetInstance()->GetServiceConfig();
								finalWidth = serviceConf.telemetry.screenshot_width;
								finalHeight = serviceConf.telemetry.screenshot_height;
								resized.resize(finalWidth * finalHeight * channels);

								stbir_resize_uint8(pixels, width, height, 0,
									resized.data(), finalWidth, finalHeight, 0,
									channels
								);

								// Frees the full size capture before the encode.
								std::vector<uint8_t>().swap(pixelData);
							}
							// end resize

							// room for a typical JPEG, so the write callback rarely has to grow it
							std::vector<unsigned char> vecData;
							vecData.reserve(64 * 1024);

							stbi_write_jpg_to_func([](void* context, void* data, int size)
								{
									std::vector<unsigned char>* buffer = static_cast<std::vector<unsigned char>*>(context);
									buffer->insert(buffer->end(), (unsigned char*)data, (unsigned char*)data + size);
								}, &vecData, finalWidth, finalHeight, channels, bResizeForTransmit ? resized.data() : pixelData.data(), bResizeForTransmit ? 0 : 90);

							// invoke cb
							if (cbOnDataAvailable != nullptr)
							{
								cbOnDataAvailable(std::move(vecData));
							}
						}
					);

					// Store the thread so we can join it during shutdown
					// SECURITY FIX: Capture manager pointer before spawning thread to avoid TOCTOU race
					GenOnlineServicesManager* pMgr = GenOnlineServicesManager::GetInstance();
					if (pMgr != nullptr)
					{
						std::scoped_lock<std::mutex> lock(pMgr->m_mutexScreenshotThreads);
						pMgr->m_vecScreenshotThreads.push_back(pNewThread);
					}
					else
					{
						// Manager was destroyed, cannot store thread. Thread will leak but won't crash.
						NetworkLog(ELogVerbosity::LOG_RELEASE, "[Screenshot] Manager destroyed before thread could be registered");
					}

					bSucceeded = true;
				}
			}
		}
	}

	// clean everything up, whether we succeeded or not

	// unlock
	if (surface != nullptr)
	{
		surface->Release_Ref();
 		surface = nullptr;
	}

	if (surfaceCopy != nullptr)
	{
		surfaceCopy->Unlock();
		surfaceCopy->Release_Ref();
 		surfaceCopy = nullptr;
	}

	if (pDXsurf != nullptr)
	{
		pDXsurf->Release();
		pDXsurf = nullptr;
	}

	// callback if failed
	if (!bSucceeded)
	{
		cbOnDataAvailable(std::vector<unsigned char>());
	}
}

void GenOnlineServicesManager::CancelUpdate()
{
	// TheSuperHackers @bugfix arcticdolphin 09/11/2026 A cancelled download stops here: its files and callbacks go and its in-flight replies are ignored.
	std::scoped_lock<std::recursive_mutex> lock(m_updateCallbackMutex);
	++m_updateAttempt;
	m_vecFilesToDownload = std::queue<std::string>();
	m_vecFilesSizes = std::queue<int64_t>();
	m_vecFilesDownloaded.clear();
	m_updateCompleteCallback = nullptr;
	m_updateFailedCallback = nullptr;
}

void GenOnlineServicesManager::LaunchPatcher(std::function<void(void)> cbFailed)
{
	char GameDir[MAX_PATH + 1] = {};
	::GetCurrentDirectoryA(MAX_PATH + 1u, GameDir);

	// Extract the filename with extension from strDownloadPath
	std::string strPatcherDir = GetPatcherDirectoryPath();
	std::string strPatcherPath = std::format("{}/{}", strPatcherDir, m_patcher_name);

	SHELLEXECUTEINFOA shellexInfo = { sizeof(shellexInfo) };
	shellexInfo.lpVerb = "runas"; // admin
	shellexInfo.lpFile = strPatcherPath.c_str();
	shellexInfo.nShow = SW_SHOWNORMAL;
	shellexInfo.lpDirectory = GameDir;
	//shellexInfo.lpParameters = "/VERYSILENT";

	bool bPatcherExeExists = std::filesystem::exists(strPatcherPath) && std::filesystem::is_regular_file(strPatcherPath);
	bool bPatcherDirExists = std::filesystem::exists(strPatcherDir) && std::filesystem::is_directory(strPatcherDir);
	bool bInvalidSize = true;

	// TODO_NGMP: Replace with CRC ASAP

	// does the file size match?
	if (bPatcherExeExists && bPatcherDirExists)
	{
		std::uintmax_t file_size = std::filesystem::file_size(strPatcherPath);
		if (file_size == m_patcher_size)
		{
			bInvalidSize = false;
		}
	}

	if (!bInvalidSize && bPatcherExeExists && bPatcherDirExists && ShellExecuteExA(&shellexInfo))
	{
		// Exit the application  
		TheGameEngine->setQuitting(TRUE);
	}
	else
	{
		if (cbFailed != nullptr)
		{
			cbFailed();
		}
		else
		{
			// show msg
			ClearGSMessageBoxes();
			MessageBoxOk(UnicodeString(L"Update Failed"), UnicodeString(L"Could not run the updater. Press below to exit."), []()
				{
					TheGameEngine->setQuitting(TRUE);
				});
		}
		ShellExecuteA(NULL, "open", "https://www.playgenerals.online/updatefailed", NULL, NULL, SW_SHOWNORMAL);
	}
}

void GenOnlineServicesManager::StartDownloadUpdate(std::function<void(void)> cb, std::function<void(void)> cbFailed)
{
	TheDownloadManager->SetFileName("Connecting to update service...");
	TheDownloadManager->OnStatusUpdate(DOWNLOADSTATUS_CONNECTING);

	m_vecFilesToDownload = std::queue<std::string>();
	m_vecFilesDownloaded.clear();

	// patcher
	m_vecFilesToDownload.emplace(m_patcher_path);
	m_vecFilesSizes.emplace(m_patcher_size);
	
	{
		std::scoped_lock<std::recursive_mutex> lock(m_updateCallbackMutex);
		++m_updateAttempt;
		m_updateCompleteCallback = cb;
		m_updateFailedCallback = cbFailed;
	}

	// cleanup current folder
	std::string strPatchDir = GetPatcherDirectoryPath();
	if (std::filesystem::exists(strPatchDir) && std::filesystem::is_directory(strPatchDir))
	{
		for (const auto& entry : std::filesystem::directory_iterator(strPatchDir))
		{
			std::filesystem::remove_all(entry.path());
		}
	}

	// start for real
	ContinueUpdate();


}

void GenOnlineServicesManager::OnLogin(ELoginResult loginResult, const char* szWSAddr, std::function<void(void)> fnWebsocketConnectedCallback)
{
	if (loginResult == ELoginResult::Success)
	{
		GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		if (pAuthInterface != nullptr)
		{
			NetworkMeshLibrary::EnsureInitialized(pAuthInterface->GetUserID());
		}

		// Tear down any previous session's websocket before replacing it.
		if (m_pWebSocket != nullptr)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] OnLogin: shutting down previous websocket before re-login");
			m_pWebSocket->Shutdown();
			m_pWebSocket->m_fnWebsocketConnectedCallback = nullptr;
			m_pWebSocket.reset();
		}

		// connect to WS
		m_pWebSocket = std::make_shared<WebSocket>();

        // the websocket address comes from the route we logged in through, not from the service
        const std::string strServiceAddr = szWSAddr ? szWSAddr : "";
        std::string strWebsocketAddr = ServiceConnection::RealtimeHubURLForBase(GetAPIEndpoint(""));
        // the session registration names the hub, which is used only on the origin of the route
        const std::string strAccepted = GenOnlineRealtime::AcceptRealtimeUrl(strServiceAddr, GetAPIEndpoint(""));
        if (!strAccepted.empty())
        {
            strWebsocketAddr = strAccepted;
        }
        else if (!strServiceAddr.empty())
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE, "[Connection] Ignoring a realtime address that is not the hub on the service origin, using %s", strWebsocketAddr.c_str());
        }

        m_pWebSocket->Connect(strWebsocketAddr.c_str(), [=]()
            {
                // Get friends list and blocked list
                // we need to wait until the websocket is connected so we have a session
                GenOnlineSocialInterface* pSocialInterface = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
                if (pSocialInterface == nullptr)
                {
                    return;
                }

                pSocialInterface->GetFriendsList(false, nullptr);
                pSocialInterface->GetBlockList(nullptr);

                // and invoke callback
                fnWebsocketConnectedCallback();
            });
	}
}

void GenOnlineServicesManager::Init()
{
	g_MainThreadID = std::this_thread::get_id();

	// initialize child classes, these need the platform handle
	m_pAuthInterface = new GenOnlineAuthInterface();
	m_pLobbyInterface = new GenOnlineLobbyInterface();
	m_pRoomInterface = new GenOnlineRoomsInterface();
	m_pStatsInterface = new GenOnlineStatsInterface();
	m_pMatchmakingInterface = new GenOnlineMatchmakingInterface();
	m_pSocialInterface = new GenOnlineSocialInterface();

	m_pHTTPManager = new HTTPManager();
	m_pHTTPManager->Initialize();

    std::string strPlugin = GenOnlineServicesManager::Settings.GetAnticheatPlugin();

	// Empty name means no anticheat plugin configured. The first transport plugin then loads for its transport only.
	GenOnlinePluginHost::LoadPlugin(strPlugin, GenOnlineServicesManager::Settings.GetTransportPlugins());

	// TODO_NGMP: Better location
	// TODO_NGMP: Get all of this from the service
	int moneyVal = 100000;
	int maxMoneyVal = 1000000;

	while (moneyVal <= maxMoneyVal)
	{
		
		Money newMoneyVal;
		newMoneyVal.deposit(moneyVal, false);
		TheMultiplayerSettings->addStartingMoneyChoice(newMoneyVal, false);

		moneyVal += 50000;
	}

#if 0
	std::map<AsciiString, RGBColor> mapColors;
	mapColors["Dark Red"] = RGBColor{ 0.53f, 0.f, 0.08f };
	mapColors["Brown"] = RGBColor{ 0.46f, 0.26f, 0.26f };
	mapColors["Dark Green"] = RGBColor{ 0.09f, 0.24f, 0.04f };

	for (const auto& [colorName, rgbColor] : mapColors)
	{
		MultiplayerColorDefinition* newDef = TheMultiplayerSettings->newMultiplayerColorDefinition(colorName.str());
		newDef->setColor(rgbColor);
		newDef->setNightColor(rgbColor);
	}
#endif
}



void GenOnlineServicesManager::Tick()
{
	GenOnlinePluginHost::Tick();

	// screenshots
	{
		// send screenshot
		std::scoped_lock<std::mutex> ssLock(m_ScreenshotMutex);


		const int uploadBytesPerSec = std::clamp(m_ServiceConfig.telemetry.screenshot_upload_bytes_per_sec, 16384, 10485760);
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		const bool bMatchOver = TheGameLogic == nullptr || !TheGameLogic->isInMultiplayerGame(); // uploads no longer compete with game traffic

		// screenshot types that already have a presigned URL
		for (auto it = m_vecGuardedSSData.begin(); it != m_vecGuardedSSData.end();)
		{
			S3ScreenshotEntry& screenshotEntry = *it;
			const bool bUnthrottled = bMatchOver || screenshotEntry.screenshotType == EScreenshotType::SCREENSHOT_TYPE_SCORESCREEN;

			// not this client's turn to upload yet
			if (!bUnthrottled && (screenshotEntry.notBefore > now || m_nextThrottledUploadTime > now))
			{
				++it;
				continue;
			}

			// NOTE: Screenshot types start and end of match are captures and cached in memory until the server tells us where to upload them, so we need to wait for the upload URI

			if ((screenshotEntry.screenshotType == EScreenshotType::SCREENSHOT_TYPE_LOADSCREEN || screenshotEntry.screenshotType == EScreenshotType::SCREENSHOT_TYPE_SCORESCREEN) && screenshotEntry.strSignedURI.empty())
			{
				it = m_vecGuardedSSData.erase(it);
				continue;
			}
			else // we have the data we need, send away
			{
				if (!bUnthrottled)
				{
					const int64_t sendMS = (int64_t)screenshotEntry.vecBytes.size() * 1000 / uploadBytesPerSec;
					m_nextThrottledUploadTime = now + std::chrono::milliseconds(sendMS);
				}

                // the delayed upload starts long after the capture, hold the controller from here
                if (screenshotEntry.screenshotType == EScreenshotType::SCREENSHOT_TYPE_GAMEPLAY && TheNetwork != nullptr && TheNetwork->GetConnectionManager() != nullptr)
                {
                    TheNetwork->GetConnectionManager()->noteScreenshotCapture(true);
                }
                std::map<std::string, std::string> mapHeaders;
                mapHeaders["Content-Type"] = "image/jpeg";
                GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendS3PUTRequest(screenshotEntry.strSignedURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, std::move(screenshotEntry.vecBytes), [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
                    {
#if _DEBUG
                        if (statusCode != 200)
                        {
                            __debugbreak();
                        }
#endif
                        NetworkLog(ELogVerbosity::LOG_RELEASE, "Screenshot upload, result: %d", statusCode);
                        if (screenshotEntry.screenshotType == EScreenshotType::SCREENSHOT_TYPE_GAMEPLAY && TheNetwork != nullptr && TheNetwork->GetConnectionManager() != nullptr)
                        {
                            TheNetwork->GetConnectionManager()->noteScreenshotDone();
                        }
                    }, nullptr, HTTP_UPLOAD_TIMEOUT, bUnthrottled ? 0 : uploadBytesPerSec);
			}

			it = m_vecGuardedSSData.erase(it);
		}

		// screenshots and replays that are cached and awaiting S3 url, and now have said URL
		if (!m_vecCachedScreenshotBytes_MatchStart.empty()) // we have data waiting
		{
			if (!m_strCachedScreenshot_MatchStart_S3URI.empty()) // and we have a URL
			{
				// queue it
				S3ScreenshotEntry newEntry;
				newEntry.screenshotType = EScreenshotType::SCREENSHOT_TYPE_LOADSCREEN;
				newEntry.vecBytes = std::move(m_vecCachedScreenshotBytes_MatchStart);
				newEntry.strSignedURI = std::move(m_strCachedScreenshot_MatchStart_S3URI);
				newEntry.notBefore = GetScreenshotUploadTime();
				m_vecGuardedSSData.push_back(std::move(newEntry));

				// clear data
                m_vecCachedScreenshotBytes_MatchStart = std::vector<uint8_t>();
				m_strCachedScreenshot_MatchStart_S3URI = std::string();
			}
		}

        if (!m_cachedMatchEndUpload.bytes.empty()) // we have data waiting
        {
            if (m_cachedMatchEndUpload.dataMatchID == m_cachedMatchEndUpload.uriMatchID && !m_cachedMatchEndUpload.signedURI.empty()) // and we have a matching URL
            {
                // queue it
                S3ScreenshotEntry newEntry;
                newEntry.screenshotType = EScreenshotType::SCREENSHOT_TYPE_SCORESCREEN;
                newEntry.vecBytes = std::move(m_cachedMatchEndUpload.bytes);
                newEntry.strSignedURI = std::move(m_cachedMatchEndUpload.signedURI);
                m_vecGuardedSSData.push_back(std::move(newEntry));

                // clear data
                m_cachedMatchEndUpload = {};
            }
        }

        if (!m_cachedReplayUpload.bytes.empty()) // we have data waiting
        {
            if (m_cachedReplayUpload.dataMatchID == m_cachedReplayUpload.uriMatchID && !m_cachedReplayUpload.signedURI.empty()) // and we have a matching URL
            {
				// do the upload
                std::map<std::string, std::string> mapHeaders;
                mapHeaders["Content-Type"] = "application/octet-stream";
                GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendS3PUTRequest(m_cachedReplayUpload.signedURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, std::move(m_cachedReplayUpload.bytes), [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
                    {
#if _DEBUG
                        if (statusCode != 200)
                        {
                            __debugbreak();
                        }
#endif

                        NetworkLog(ELogVerbosity::LOG_RELEASE, "Replay upload, result: %d", statusCode);
                    }, nullptr, HTTP_UPLOAD_TIMEOUT);

                // clear data
                m_cachedReplayUpload = {};
            }
        }
	}

	if (m_pWebSocket != nullptr)
	{
		m_pWebSocket->Tick();
	}

	if (m_pHTTPManager != nullptr)
	{
		m_pHTTPManager->Tick();
	}

	GenOnlineRealtimeBridge::TickRest();

	if (m_pAuthInterface != nullptr)
	{
		m_pAuthInterface->Tick();
	}

	if (m_pRoomInterface != nullptr)
	{
		m_pRoomInterface->Tick();
	}

	if (m_pLobbyInterface != nullptr)
	{
		m_pLobbyInterface->Tick();
	}

	NetworkMeshLibrary::Tick();
}

// INT_MIN until InitSentry ran, then the sentry_init result
static int s_sentryInitResult = INT_MIN;
// crashpad's unhandled exception filter, called by the game's filter once the game wrote its own dumps
static LPTOP_LEVEL_EXCEPTION_FILTER s_sentryCrashFilter = nullptr;

void GenOnlineServicesManager::InitSentry()
{
	// Initialize libcurl global state here, before any plugins (e.g. EasyAntiCheat) are loaded.
	// This ensures libcurl's internal mutexes are fully initialized before the EAC plugin
	// attempts to use them, preventing an access violation in mtx_do_lock on null mutex state.
	static const CURLcode curlInitialization = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (curlInitialization != CURLE_OK)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Connection] curl initialization failed (%d)", curlInitialization);
		return;
	}

#if !_DEBUG
	// game paths are in the process code page, sentry's char* API wants UTF-8, so hand it the wide path
	std::wstring strDumpPath;
	const AsciiString& strUserDataPath = TheGlobalData->getPath_UserData();
	const int wideLen = MultiByteToWideChar(CP_ACP, 0, strUserDataPath.str(), -1, nullptr, 0);
	if (!strUserDataPath.isEmpty() && wideLen > 1)
	{
		std::wstring strUserDataPathW(wideLen - 1, L'\0');
		MultiByteToWideChar(CP_ACP, 0, strUserDataPath.str(), -1, strUserDataPathW.data(), wideLen);
		strDumpPath = strUserDataPathW + L"GeneralsOnlineCrashData\\";

		std::error_code ec;
		std::filesystem::create_directories(strDumpPath, ec);
	}

	sentry_options_t* options = sentry_options_new();

	sentry_options_set_dsn(options, "https://61750bebd112d279bcc286d617819269@o4509316925554688.ingest.us.sentry.io/4509316927586304");
	if (!strDumpPath.empty())
	{
		sentry_options_set_database_pathw(options, strDumpPath.c_str());
	}

	std::string strVersionStr = GetSentryRelease();
	sentry_options_set_release(options, strVersionStr.c_str());

	// keep crashpad_handler alive until the crash report upload completes
	sentry_options_set_crashpad_wait_for_upload(options, 1);

#if defined(USE_TEST_ENV)
	sentry_options_set_environment(options, "test");
#else
	sentry_options_set_environment(options, "production");
#endif

#if _DEBUG
	sentry_options_set_debug(options, 1);
	sentry_options_set_logger_level(options, SENTRY_LEVEL_DEBUG);

	sentry_options_set_logger(options,	[](sentry_level_t level, const char* message, va_list args, void* userdata)
	{
			char buffer[1024];
			va_start(args, message);
			vsnprintf(buffer, 1024, message, args);
			buffer[1024 - 1] = 0;
			va_end(args);

			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Sentry] %s", buffer);
	}, nullptr);
#endif

	// fails as a whole when crashpad_handler.exe is missing or older than this client
	// sentry_init puts crashpad's unhandled exception filter in place of the game's, which writes the game's dumps and crash info
	const LPTOP_LEVEL_EXCEPTION_FILTER gameCrashFilter = SetUnhandledExceptionFilter(nullptr);
	SetUnhandledExceptionFilter(gameCrashFilter);
	const int sentryInitResult = sentry_init(options);
	s_sentryInitResult = sentryInitResult;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Sentry] Init %s (%d)", sentryInitResult == 0 ? "succeeded" : "FAILED", sentryInitResult);

	// put the game's filter back on top; it hands each crash on to crashpad after writing its own dumps
	if (gameCrashFilter != nullptr)
	{
		const LPTOP_LEVEL_EXCEPTION_FILTER sentryCrashFilter = SetUnhandledExceptionFilter(gameCrashFilter);
		s_sentryCrashFilter = sentryCrashFilter != gameCrashFilter ? sentryCrashFilter : nullptr;
	}
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Sentry] Crash handling: %s", s_sentryCrashFilter != nullptr ? "game dumps, then Sentry" : "game dumps only");

	// scope data set before sentry_init is dropped
	UpdateSentryUser();
#endif
}

void GenOnlineServicesManager::UpdateSentryUser()
{
	int64_t userID = -1;
	std::string strDisplayname = "Unknown";
	GenOnlineAuthInterface* pAuthInterface = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (pAuthInterface != nullptr)
	{
		userID = pAuthInterface->GetUserID();
		strDisplayname = pAuthInterface->GetDisplayName();
	}
	std::string strUserID = std::format("{}", userID);

	sentry_value_t userinfoVal = sentry_value_new_object();
	sentry_value_set_by_key(userinfoVal, "user_id", sentry_value_new_string(strUserID.c_str()));
	sentry_value_set_by_key(userinfoVal, "user_displayname", sentry_value_new_string(strDisplayname.c_str()));
	sentry_set_context("user_info", userinfoVal);

	sentry_set_tag("user_id", strUserID.c_str());
	sentry_set_tag("user_displayname", strDisplayname.c_str());
}

void GenOnlineServicesManager::ShutdownSentry()
{
	// crashpad's filter must not run once its handler is gone; later crashes only get the game's dumps
	s_sentryCrashFilter = nullptr;
	sentry_close();
}

Bool GenOnlineServicesManager::ForwardCrashToSentry(struct _EXCEPTION_POINTERS* exceptionInfo, LONG* result)
{
	const LPTOP_LEVEL_EXCEPTION_FILTER sentryCrashFilter = s_sentryCrashFilter;
	if (sentryCrashFilter == nullptr)
	{
		return FALSE;
	}
	*result = sentryCrashFilter(exceptionInfo);
	return TRUE;
}

std::string GenOnlineServicesManager::GetSentryRelease()
{
	return std::format("generalsonline-client@{}", GENONLINE_VERSION_STRING);
}

std::string GenOnlineServicesManager::GetSentryStatus()
{
#if _DEBUG
	return "off in debug builds";
#else
	if (s_sentryInitResult == INT_MIN)
	{
		return "not started yet";
	}
	if (s_sentryInitResult != 0)
	{
		return std::format("init FAILED ({})", s_sentryInitResult);
	}
	return "initialized";
#endif
}

void GenOnlineServicesManager::LogDisplayAdapter()
{
	const D3DADAPTER_IDENTIFIER8& adapter = DX8Wrapper::Get_Current_Adapter_Identifier();
	if (adapter.Description[0] == '\0')
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[Session] GPU: no D3D8 adapter (headless or no device)");
		return;
	}

	const DWORD driverHigh = static_cast<DWORD>(adapter.DriverVersion.HighPart);
	const DWORD driverLow = adapter.DriverVersion.LowPart;
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Session] GPU: %.512s (vendor 0x%04lX, device 0x%04lX)", adapter.Description, static_cast<unsigned long>(adapter.VendorId), static_cast<unsigned long>(adapter.DeviceId));
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[Session] GPU driver: %.512s version %u.%u.%u.%u", adapter.Driver,
		static_cast<unsigned int>(HIWORD(driverHigh)), static_cast<unsigned int>(LOWORD(driverHigh)), static_cast<unsigned int>(HIWORD(driverLow)), static_cast<unsigned int>(LOWORD(driverLow)));
}


std::string GenOnlineServicesManager::GetPatcherDirectoryPath()
{
	if (!TheGlobalData)
		return {};
	std::string strPatcherDirPath = std::format("{}/GeneralsOnlineData/Update/", TheGlobalData->getPath_UserData().str());
	return strPatcherDirPath;
}

void WebSocket::Shutdown()
{
	// Return immediately if already shut down to prevent double-shutdown
	// (e.g., GenOnlineServicesManager::Shutdown() calls this before releasing the shared_ptr,
	// and then the shared_ptr destructor also calls Shutdown() via ~WebSocket())
	if (m_bShuttingDown)
	{
		return;
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Shutdown initiated");
	
	// Signal that we're shutting down
	m_bShuttingDown = true;
	m_proxyResolution.reset();
	
	// Disconnect from the websocket (handles the connected case)
	Disconnect();

	// Clean up curl easy handle if still active (e.g., mid-connection, not yet fully connected)
	// Disconnect() returns early when m_bConnected is false, so m_pCurlWS may still be alive here
	if (m_pCurlWS != nullptr)
	{
		if (m_pMulti != nullptr)
		{
			curl_multi_remove_handle(m_pMulti, m_pCurlWS);
		}
		curl_easy_cleanup(m_pCurlWS);
		m_pCurlWS = nullptr;
	}

	// Clean up multi handle
	if (m_pMulti != nullptr)
	{
		curl_multi_cleanup(m_pMulti);
		m_pMulti = nullptr;
	}

	// Free headers (may already be freed by Disconnect, but check anyway)
	if (m_pHeaders != nullptr)
	{
		curl_slist_free_all(m_pHeaders);
		m_pHeaders = nullptr;
	}
	
	// Give CURL time to process the disconnect and cease operations
	// This ensures any background I/O threads have completed before we return
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Shutdown complete");
}

void WebSocket::SendData_ChangeLobbyPassword(UnicodeString& strNewPassword)
{
	GenOnlineRealtime::SetLobbyPasswordRequest request;
	request.password = to_utf8(strNewPassword.str());
	InvokeHub(request, [](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (!outcome.Succeeded() && pLobbyInterface != nullptr)
			{
				pLobbyInterface->ShowRealtimeLobbyFailure(outcome);
			}
		});
}


void WebSocket::SendData_RemoveLobbyPassword()
{
	// A null password removes it.
	GenOnlineRealtime::SetLobbyPasswordRequest request;
	InvokeHub(request, [](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (!outcome.Succeeded() && pLobbyInterface != nullptr)
			{
				pLobbyInterface->ShowRealtimeLobbyFailure(outcome);
			}
		});
}

void WebSocket::SendData_ChangeName(UnicodeString& strNewName)
{
	GenOnlineRealtime::ProfileUpdateBody body;
	body.name = to_utf8(strNewName.str());
	GenOnlineRealtimeBridge::CallRest(GenOnlineRealtimeBridge::RestMethod::Patch, GenOnlineRealtime::RestMePath(), nlohmann::json(body).dump(), [](const GenOnlineRealtime::RestOutcome& outcome)
		{
			if (!outcome.Succeeded())
			{
				GenOnlineRealtimeBridge::ShowRoomNotice(GenOnlineRealtimeBridge::FailureText(outcome), true);
			}
		});
}


void WebSocket::SendData_FriendMessage(UnicodeString& msg, int64_t target_user_id)
{
	GenOnlineRealtime::SendMessageRequest request;
	request.channel_id = "dm:" + std::to_string(target_user_id);
	request.type = "chat";
	request.content = to_utf8(msg.str());
	InvokeHub(request, [target_user_id](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			if (outcome.Succeeded())
			{
				return;
			}
			// The failure shows in the conversation with that friend.
			GenOnlineSocialInterface* pSocial = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
			GenOnlineAuthInterface* pAuth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
			if (pSocial != nullptr && pAuth != nullptr)
			{
				const std::wstring text = L"Not delivered: " + GenOnlineRealtimeBridge::FailureText(outcome);
				pSocial->OnChatMessage(pAuth->GetUserID(), target_user_id, UnicodeString(text.c_str()));
			}
		});
}

void WebSocket::SendData_LobbyChatMessage(UnicodeString& msg, bool bIsAction, bool bIsAnnouncement, bool bShowAnnouncementToHost)
{
	GenOnlineLobbyInterface* pLobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pLobby == nullptr || !pLobby->IsInLobby())
	{
		return;
	}
	GenOnlineRealtime::SendMessageRequest request;
	request.channel_id = "lobby:" + std::to_string(pLobby->GetCurrentLobby().lobbyID);
	request.type = bIsAnnouncement ? "announcement" : (bIsAction ? "action" : "chat");
	request.content = to_utf8(msg.str());
	InvokeHub(request, [](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (!outcome.Succeeded() && pLobbyInterface != nullptr)
			{
				pLobbyInterface->ShowRealtimeLobbyFailure(outcome);
			}
		});
}

void WebSocket::SendData_LeaveNetworkRoom()
{
	SendData_JoinNetworkRoom(-1);
}


void WebSocket::SendData_RequestSignalling(int64_t targetUserID)
{
	GenOnlineRealtime::RequestSignallingRequest request;
	request.target_user_id = targetUserID;
	if (!FireHub(request))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] RequestSignalling for %lld dropped, the hub is not ready", targetUserID);
	}
}

void WebSocket::SendData_RequestSignalling(int64_t targetUserID, int failedAttempt)
{
	GenOnlineRealtime::RequestSignallingRequest request;
	request.target_user_id = targetUserID;
	request.failed_attempt = failedAttempt;
	if (!FireHub(request))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] RequestSignalling for %lld with failed attempt %d dropped, the hub is not ready", targetUserID, failedAttempt);
	}
}

void WebSocket::SendData_Signalling(int64_t targetUserID, std::vector<uint8_t> vecPayload)
{
	GenOnlineRealtime::SignalRequest request;
	request.to = targetUserID;
	request.payload = GenOnlineRealtime::EncodePeerPayload(vecPayload);
	if (!FireHub(request))
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] Signal for %lld dropped, the hub is not ready", targetUserID);
	}
}

void WebSocket::SendData_StartGame()
{
	GenOnlineRealtime::StartGameRequest request;
	InvokeHub(request, [](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobbyInterface != nullptr)
			{
				pLobbyInterface->OnRealtimeStartGameOutcome(outcome);
			}
		});
}


void WebSocket::SendData_HostStartIntent()
{
	// StartGame runs the ready check on the hub and refuses the start while a player is not ready.
	GenOnlineRealtime::StartGameRequest request;
	InvokeHub(request, [](const GenOnlineRealtime::InvokeOutcome& outcome)
		{
			GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
			if (pLobbyInterface != nullptr)
			{
				pLobbyInterface->OnRealtimeStartGameOutcome(outcome);
			}
		});
}
