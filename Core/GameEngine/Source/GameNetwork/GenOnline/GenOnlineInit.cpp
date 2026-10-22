#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/NetworkMesh.h"
#include "GameNetwork/GenOnline/HTTP/HTTPManager.h"
#include "../json.hpp"
#include "GameClient/MessageBox.h"
#include "Common/FileSystem.h"
#include "Common/file.h"
#include "WWLib/realcrc.h"
#include "GameNetwork/DownloadManager.h"
#include <ws2tcpip.h>
#include "GameClient/DisplayStringManager.h"
#include "GameNetwork/NetworkInterface.h"
#include "Common/MultiplayerSettings.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameClient/Display.h"
#include "WW3D2/surfaceclass.h"
#include "WW3D2/dx8wrapper.h"
#include <algorithm>
#include <mutex>
#include <utility>
#include "GameNetwork/GenOnline/ServiceEndpointPolicy.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "GameNetwork/GenOnline/Vendor/stb_image/stb_image_write.h"
#include "GameNetwork/GenOnline/Vendor/stb_image/stb_image_resize.h"
#include "GameClient/GameText.h"
#include "GameLogic/GameLogic.h"
#include <unordered_set>
#include <random>
#include <algorithm>
#include "gitinfo.h"

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


bool GenOnlineServicesManager::g_bAdvancedNetworkStats;

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
	std::string strURI = GenOnlineServicesManager::GetAPIEndpoint("ServiceConfig");
	std::map<std::string, std::string> mapHeaders;
	
	// SECURITY FIX: Capture manager instance through GetInstance() to ensure thread-safety
	// Lambda will check if manager still exists before accessing members
	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendGETRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, [cbOnDone](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
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
				
				if (bSuccess && statusCode == 200)
				{
					nlohmann::json jsonObject = nlohmann::json::parse(strBody);
					pMgr->m_ServiceConfig = jsonObject.get<ServiceConfig>();
				}
				else
				{
					// keep the last good config (defaults if there never was one); resetting would turn off retry_signalling
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Failed to get service config, keeping the current one. Status code: %d", statusCode);
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

enum class EVersionCheckResponseResult : int
{
	OK = 0,
	FAILED = 1,
	NEEDS_UPDATE = 2
};

struct VersionCheckResponse
{
	EVersionCheckResponseResult result;
	std::string patcher_name;
	std::string patcher_path;
	int64_t patcher_size;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(VersionCheckResponse, result, patcher_name, patcher_path, patcher_size)
};

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

bool GenOnlineServicesManager::IsWebSocketURLTrusted(const std::string& url)
{
	return ServiceConnection::IsTrustedServiceURL(url, GetAPIEndpoint(""), true);
}

void GenOnlineServicesManager::EnsureServiceRoute(std::function<void(bool)> callback)
{
	// never change the route of an authenticated session behind its caller
	if (m_pAuthInterface && m_pAuthInterface->IsLoggedIn())
	{
		callback(!m_serviceBase.empty());
		return;
	}
	if (!m_serviceBase.empty() && std::chrono::steady_clock::now() - m_routeValidatedAt < std::chrono::seconds(30))
	{
		callback(true);
		return;
	}
	m_routeCallbacks.push_back(std::move(callback));
	if (m_routeCallbacks.size() > 1)
	{
		return;
	}
	m_bootstrapRoutes = ServiceConnection::BootstrapRoutes(ServiceEnvironment(), Settings.Network_GetConnectionSettings(), Settings.Network_GetLastRoute());
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
	const std::string url = m_serviceBase + "VersionManifest";
	std::map<std::string, std::string> headers;
	m_pHTTPManager->SendGETRequest(url.c_str(), EIPProtocolVersion::DONT_CARE, headers,
		[](bool success, int status, std::string body, HTTPRequest*)
		{
			auto* manager = GetInstance();
			if (!manager)
			{
				return;
			}
			if (success && status == 200 && ServiceConnection::IsExpectedVersionManifest(body))
			{
				manager->m_routeValidatedAt = std::chrono::steady_clock::now();
				if (g_Environment == EEnvironment::PROD)
				{
					Settings.Network_SaveLastRoute(ServiceConnection::RouteHost(manager->m_serviceBase));
				}
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[Connection] Trusted service route validated: %s", manager->m_serviceBase.c_str());
				manager->FinishServiceRoute(true);
				return;
			}
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Connection] Route validation failed (HTTP %d); credentials were not sent", status);
			// an access rejection is not evidence that another route would work
			manager->m_bootstrapIndex = (status == 401 || status == 403) ? manager->m_bootstrapRoutes.size() : manager->m_bootstrapIndex + 1;
			manager->ProbeServiceRoute();
		}, nullptr, 6000);
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

	NetworkMeshLibrary::Shutdown();

	// Shutdown and completely destroy WebSocket BEFORE cleaning up HTTPManager
	// This is critical because WebSocket has curl handles that must be freed
	// before curl_global_cleanup() is called by HTTPManager
	if (m_pWebSocket)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Shutting down WebSocket...");
		m_pWebSocket->Shutdown();
		
		// Reset shared_ptr to fully destroy WebSocket and free all its curl resources
		// This must happen before HTTPManager shutdown to avoid accessing freed curl state
		m_pWebSocket.reset();
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] WebSocket shutdown complete");
	}

	// Now safe to shutdown HTTP manager which calls curl_global_cleanup()
	if (m_pHTTPManager != nullptr)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Shutting down HTTPManager...");
		m_pHTTPManager->Shutdown();
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] HTTPManager shutdown complete");
	}

	GenOnlinePluginHost::UnloadPlugin();

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] OnlineServicesManager shutdown complete");
}

// The client version is the closest release tag without its v, else 0.0.0 with the commit hash.
static std::string GetClientVersion()
{
	if (GitReleaseTag[0] == 'v' && GitReleaseTag[1] != '\0')
	{
		return std::string(GitReleaseTag + 1);
	}
	return std::string("0.0.0-g") + GitShortSHA1;
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
	std::string strURI = GenOnlineServicesManager::GetAPIEndpoint("VersionCheck");

	// NOTE: Generals 'CRCs' are not true CRC's, its a custom algorithm. This is fine for lobby comparisons, but its not good for patch comparisons.
	
	// exe crc
	Char filePath[_MAX_PATH];
	GetModuleFileName(NULL, filePath, sizeof(filePath));
	std::ifstream file(filePath, std::ios::binary | std::ios::ate);
	std::streamsize size = file.tellg();

	if (!file.is_open() || size <= 0)
	{
		fnCallback(false, false);
		return;
	}

	file.seekg(0, std::ios::beg);
	std::vector<uint8_t> buffer(size);
	file.read((char*)buffer.data(), size);
	uint32_t realExeCRC = CRC_Memory((unsigned char*)buffer.data(), size);

	nlohmann::json j;
	j["client_version"] = GetClientVersion();
#if defined(RTS_ZEROHOUR)
	j["game"] = "generalsmd";
#else
	j["game"] = "generals";
#endif
	j["exe_crc"] = realExeCRC;
	std::string strPostData = j.dump();

	std::map<std::string, std::string> mapHeaders;
	GenOnlineServicesManager::GetInstance()->GetHTTPManager()->SendPOSTRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, strPostData.c_str(), [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "Version Check: Response code was %d and body was %s", statusCode, strBody.c_str());
			try
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "VERSION CHECK: Up To Date");
				nlohmann::json jsonObject = nlohmann::json::parse(strBody);
				VersionCheckResponse authResp = jsonObject.get<VersionCheckResponse>();

				if (authResp.result == EVersionCheckResponseResult::OK)
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "VERSION CHECK: Up To Date");
					fnCallback(true, false);
				}
				else if (authResp.result == EVersionCheckResponseResult::NEEDS_UPDATE)
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "VERSION CHECK: Needs Update");

					// the name becomes a local path, so it must be a bare file name
					const std::string& strName = authResp.patcher_name;
					if (strName.empty() || strName.find_first_of("/\\:") != std::string::npos || strName.find("..") != std::string::npos)
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "VERSION CHECK: Rejected invalid patcher name");
						fnCallback(false, false);
						return;
					}

					// cache the data
					m_patcher_name = authResp.patcher_name;
					m_patcher_path = authResp.patcher_path;
					m_patcher_size = authResp.patcher_size;

					fnCallback(true, true);
				}
				else
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "VERSION CHECK: Failed");
					fnCallback(false, false);
				}
			}
			catch (...)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "VERSION CHECK: Failed to parse response");
				fnCallback(false, false);
			}
		}, nullptr, -1);
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

		// this isnt a super nice way of doing this, lets make a download manager
		std::map<std::string, std::string> mapHeaders;
		m_pHTTPManager->SendGETRequest(strDownloadPath.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
			{
				auto fnShowUpdateFailed = []()
					{
						// show msg
						ClearGSMessageBoxes();
						MessageBoxOk(UnicodeString(L"Update Failed"), UnicodeString(L"Could not download the updater. Press below to exit."), []()
							{
								TheGameEngine->setQuitting(TRUE);
							});
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

}

void GenOnlineServicesManager::LaunchPatcher()
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
		// show msg
		ClearGSMessageBoxes();
		MessageBoxOk(UnicodeString(L"Update Failed"), UnicodeString(L"Could not run the updater. Press below to exit."), []()
			{
				TheGameEngine->setQuitting(TRUE);
			});
		ShellExecuteA(NULL, "open", "https://www.playgenerals.online/updatefailed", NULL, NULL, SW_SHOWNORMAL);
	}
}

void GenOnlineServicesManager::StartDownloadUpdate(std::function<void(void)> cb)
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
		m_updateCompleteCallback = cb;
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
        const std::string strWebsocketAddr = ServiceConnection::WebSocketURLForBase(GetAPIEndpoint(""));
        const std::string strServiceAddr = szWSAddr ? szWSAddr : "";
        if (strServiceAddr != strWebsocketAddr)
        {
            const std::string strServiceHost = ServiceConnection::RouteHost(strServiceAddr);
            const bool bPrintable = std::none_of(strServiceHost.begin(), strServiceHost.end(), [](unsigned char c) { return c <= 32 || c >= 127; });
            NetworkLog(ELogVerbosity::LOG_RELEASE, "[Connection] Service websocket address (host %s) differs from the route, using %s", bPrintable ? strServiceHost.c_str() : "[invalid]", strWebsocketAddr.c_str());
        }

        m_pWebSocket->Connect(strWebsocketAddr.c_str(), false, [=]()
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

	// Empty name means no anticheat plugin configured; skip loading.
	GenOnlinePluginHost::LoadPlugin(strPlugin);

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

void GenOnlineServicesManager::InitSentry()
{
	// Initialize libcurl global state here, before any plugins (e.g. EasyAntiCheat) are loaded.
	// This ensures libcurl's internal mutexes are fully initialized before the EAC plugin
	// attempts to use them, preventing an access violation in mtx_do_lock on null mutex state.
	curl_global_init(CURL_GLOBAL_DEFAULT);

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

	std::string strVersionStr = std::format("generalsonline-client@{}", GENONLINE_VERSION_STRING);
	sentry_options_set_release(options, strVersionStr.c_str());

#if defined(USE_TEST_ENV)
	sentry_options_set_environment(options, "test");
#else
	sentry_options_set_environment(options, "production");
#endif

	// local player info
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

	sentry_init(options);
#endif
}

void GenOnlineServicesManager::ShutdownSentry()
{
	sentry_close();
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
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::LOBBY_CHANGE_PASSWORD;
	j["new_password"] = to_utf8(strNewPassword.str());
	std::string strBody = j.dump();

	Send(strBody.c_str());
}


void WebSocket::SendData_RemoveLobbyPassword()
{
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::LOBBY_REMOVE_PASSWORD;
	std::string strBody = j.dump();

	Send(strBody.c_str());
}

void WebSocket::SendData_ChangeName(UnicodeString& strNewName)
{
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::PLAYER_NAME_CHANGE;
	j["name"] = to_utf8(strNewName.str());
	std::string strBody = j.dump();

	Send(strBody.c_str());
}


void WebSocket::SendData_FriendMessage(UnicodeString& msg, int64_t target_user_id)
{
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::SOCIAL_FRIEND_CHAT_MESSAGE_CLIENT_TO_SERVER;
	j["target_user_id"] = target_user_id;
	j["message"] = to_utf8(msg.str());
	std::string strBody = j.dump();

	Send(strBody.c_str());
}

void WebSocket::SendData_LobbyChatMessage(UnicodeString& msg, bool bIsAction, bool bIsAnnouncement, bool bShowAnnouncementToHost)
{
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::LOBBY_ROOM_CHAT_FROM_CLIENT;
	j["message"] = to_utf8(msg.str());
	j["action"] = bIsAction;
	j["announcement"] = bIsAnnouncement;
	j["show_announcement_to_host"] = bShowAnnouncementToHost;
	std::string strBody = j.dump();

	Send(strBody.c_str());
}

void WebSocket::SendData_LeaveNetworkRoom()
{
	SendData_JoinNetworkRoom(-1);
}


void WebSocket::SendData_RequestSignalling(int64_t targetUserID)
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] SEND REQUEST SIGNALING!");
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::NETWORK_CONNECTION_CLIENT_REQUEST_SIGNALLING;
	j["target_user_id"] = targetUserID;
	std::string strBody = j.dump();
	Send(strBody.c_str());
}

void WebSocket::SendData_Signalling(int64_t targetUserID, std::vector<uint8_t> vecPayload)
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] SEND SIGNAL!");
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::NETWORK_SIGNAL;
	j["target_user_id"] = targetUserID;
	j["payload"] = vecPayload;
	std::string strBody = j.dump();
	Send(strBody.c_str());
}

void WebSocket::SendData_StartGame()
{
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::START_GAME;
	std::string strBody = j.dump();
	Send(strBody.c_str());
}


void WebSocket::SendData_ACMessage(int64_t targetUserID, std::vector<uint8_t> vecPayload)
{
    nlohmann::json j;
    j["msg_id"] = EWebSocketMessageID::ANTICHEAT_MESSAGE;
    j["target_user_id"] = targetUserID;
    j["payload"] = vecPayload;
    std::string strBody = j.dump();
    Send(strBody.c_str());
}

void WebSocket::SendData_SubscribeRealtimeUpdates()
{
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::SOCIAL_SUBSCRIBE_REALTIME_UPDATES;
	std::string strBody = j.dump();
	Send(strBody.c_str());
}


void WebSocket::SendData_UnsubscribeRealtimeUpdates()
{
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::SOCIAL_UNSUBSCRIBE_REALTIME_UPDATES;
	std::string strBody = j.dump();
	Send(strBody.c_str());
}

void WebSocket::SendData_HostStartIntent()
{
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::LOBBY_HOST_START_INTENT;
	std::string strBody = j.dump();
	Send(strBody.c_str());
}
