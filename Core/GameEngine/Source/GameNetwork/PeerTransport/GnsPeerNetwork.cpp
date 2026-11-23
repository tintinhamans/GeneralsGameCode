#include "GameNetwork/PeerTransport/GnsPeerNetwork.h"
#include "GameNetwork/PeerTransport/PeerTransport.h"

#include <assert.h>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingcustomsignaling.h>
#include <steam/steamnetworkingsockets.h>

static_assert(k_gnsIceEnableRelay == k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Relay, "The ICE values must match GameNetworkingSockets");
static_assert(k_gnsIceEnablePrivate == k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Private, "The ICE values must match GameNetworkingSockets");
static_assert(k_gnsIceEnablePublic == k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Public, "The ICE values must match GameNetworkingSockets");
static_assert(k_gnsIceEnableAll == k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_All, "The ICE values must match GameNetworkingSockets");

static IGnsPeerListener* s_pListener = nullptr;

// Pool for deferred deletion of ConnectionSignaling objects; avoids "delete this" races during
// async Steam callbacks.
static std::mutex g_pendingDeletionMutex;
static std::vector<void*> g_pendingConnSignalingDeletions;

// Clean up pending ConnectionSignaling objects that were deferred during Release()

static void CleanupPendingConnSignalingDeletions()
{
	std::vector<void*> objectsToDelete;
	{
		std::scoped_lock<std::mutex> lock(g_pendingDeletionMutex);
		objectsToDelete.swap(g_pendingConnSignalingDeletions);
	}

	for (void* pObj : objectsToDelete)
	{
		// SECURITY: Delete through base interface to avoid nested class visibility issues
		delete static_cast<ISteamNetworkingConnectionSignaling*>(pObj);
	}
}

// The user id of a handshake identity, which is plain text chosen by the peer.
static bool ParseUserIdentity(const SteamNetworkingIdentity& identity, int64_t& userId)
{
	const char* szIdentity = identity.GetGenericString();
	if (szIdentity == nullptr)
	{
		return false;
	}

	const size_t identityLen = strlen(szIdentity);
	const char* szDigits = (szIdentity[0] == '-') ? szIdentity + 1 : szIdentity;
	const size_t numDigits = identityLen - (szDigits - szIdentity);
	if (numDigits == 0 || numDigits > 19 || strspn(szDigits, "0123456789") != numDigits)
	{
		return false;
	}

	const std::from_chars_result parseResult = std::from_chars(szIdentity, szIdentity + identityLen, userId);
	return parseResult.ec == std::errc() && parseResult.ptr == szIdentity + identityLen;
}

// Called on connection state transitions. Always runs on the main thread via RunCallbacks(), so the listener
// reads its connections here without a lock.
static void OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
{
	CleanupPendingConnSignalingDeletions();

	IGnsPeerListener* pListener = s_pListener;
	GnsPeerNetwork* pNetwork = pListener != nullptr ? pListener->GetActiveNetwork() : nullptr;

	if (pNetwork == nullptr)
	{
		return;
	}

	// find player connection
	GnsPeerRef peer;
	const bool bFound = pListener->FindConnection(pInfo->m_hConn, peer);


	//if (pPlayerConnection != nullptr)
	{
		//NetworkLog(ELogVerbosity::LOG_RELEASE, "[STEAM NETWORKING][%s] Player Connection was null", pInfo->m_info.m_szConnectionDescription);
		//return;
	}

	// What's the state of the connection?
	switch (pInfo->m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_ClosedByPeer:
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:

		PeerTransportLog(PeerLogLevel::Release, "[STEAM NETWORKING][%s] %s, reason %d: %s\n",
			pInfo->m_info.m_szConnectionDescription,
			(pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ? "closed by peer" : "problem detected locally"),
			pInfo->m_info.m_eEndReason,
			pInfo->m_info.m_szEndDebug
		);

		// Close our end
		PeerTransportLog(PeerLogLevel::Release, "[DC] Closing in callback");
		SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, nullptr, false);

		if (bFound)
		{
			GnsPeerEnd end;
			end.endReason = pInfo->m_info.m_eEndReason;
			// A peer that closed on purpose during the match has left, but closing to negotiate a new connection is not a leave.
			end.closedOnPurpose = pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer
				&& pInfo->m_info.m_eEndReason == k_ESteamNetConnectionEnd_App_Generic
				&& strstr(pInfo->m_info.m_szEndDebug, "new connection being negotiated") == nullptr
				&& strstr(pInfo->m_info.m_szEndDebug, "(Rejected)") == nullptr;
			end.wasError = pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally || pInfo->m_info.m_eEndReason != k_ESteamNetConnectionEnd_App_Generic;

			pListener->OnConnectionEnded(peer, end);
		}
		else
		{
			// Why are we hearing about any another connection?
			//assert(false);
			pNetwork->ForgetHeldIncoming(pInfo->m_hConn);
		}

		break;

	case k_ESteamNetworkingConnectionState_None:
		// Notification that a connection was destroyed.  (By us, presumably.)
		// We don't need this, so ignore it.
		break;

	case k_ESteamNetworkingConnectionState_Connecting:

		// Is this a connection we initiated, or one that we are receiving?
		if (pNetwork->GetListenSocket() != k_HSteamListenSocket_Invalid && pInfo->m_info.m_hListenSocket == pNetwork->GetListenSocket())
		{
			// Somebody's knocking
			// Note that we assume we will only ever receive a single connection

			PeerTransportLog(PeerLogLevel::Release, "[STEAM NETWORKING][%s] Considering Accepting\n", pInfo->m_info.m_szConnectionDescription);

			if (bFound)
			{
#if _DEBUG
				assert(peer.hConnection == k_HSteamNetConnection_Invalid); // not really a bug in this code, but a bug in the test
#endif

				// the identity of the peer must be the user this connection is for
				int64_t remoteUserID = 0;
				if (!ParseUserIdentity(pInfo->m_info.m_identityRemote, remoteUserID) || remoteUserID != peer.userID)
				{
					PeerTransportLog(PeerLogLevel::Release, "[SECURITY] Rejecting incoming connection %s: its identity is not user %lld", pInfo->m_info.m_szConnectionDescription, static_cast<long long>(peer.userID));
					SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 1001, "Identity mismatch (Rejected)", false);
					return;
				}

				if (!pListener->OnIncomingConnection(peer, pInfo->m_hConn, pInfo->m_info.m_szConnectionDescription))
				{
					return;
				}

				// A signalled connection is accepted even with a stale roster.
				PeerTransportLog(PeerLogLevel::Release, "[STEAM NETWORKING][%s] Accepting - Server started signalling\n", pInfo->m_info.m_szConnectionDescription);
				pNetwork->ApplyIncomingSetup(pInfo->m_hConn, peer.userID, pListener->GetIncomingSetup(peer));
				SteamNetworkingSockets()->AcceptConnection(pInfo->m_hConn);
			}
			else
			{
				// The peer started before this side. A connect of this side would merge with the unaccepted connection
				// and break its crypto handshake, so the connection waits here until this side starts the pair.
				int64_t remoteUserID = 0;
				if (!ParseUserIdentity(pInfo->m_info.m_identityRemote, remoteUserID))
				{
					PeerTransportLog(PeerLogLevel::Release, "[SECURITY] Rejecting incoming connection %s: its identity is not a user", pInfo->m_info.m_szConnectionDescription);
					SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 1001, "Identity mismatch (Rejected)", false);
					return;
				}
				pNetwork->HoldIncoming(remoteUserID, pInfo->m_hConn);
			}

		}
		else
		{
			// Note that we will get notification when our own connection that
			// we initiate enters this state.
			PeerTransportLog(PeerLogLevel::Release, "[STEAM NETWORKING][%s] Entered connecting state\n", pInfo->m_info.m_szConnectionDescription);

			if (bFound)
			{
#if _DEBUG
				assert(peer.hConnection == pInfo->m_hConn);
#endif

				pListener->OnStateChanged(peer, GnsPeerState::Connecting);
			}
		}
		break;

	case k_ESteamNetworkingConnectionState_FindingRoute:
		// P2P connections will spend a brief time here where they swap addresses
		// and try to find a route.
		if (bFound)
		{
			PeerTransportLog(PeerLogLevel::Release, "[STEAM NETWORKING][%s] finding route\n", pInfo->m_info.m_szConnectionDescription);

			pListener->OnStateChanged(peer, GnsPeerState::FindingRoute);
		}
		break;

	case k_ESteamNetworkingConnectionState_Connected:
		// We got fully connected
#if _DEBUG
		//assert(pInfo->m_hConn == pPlayerConnection->m_hSteamConnection); // We don't initiate or accept any other connections, so this should be out own connection
#endif

		PeerTransportLog(PeerLogLevel::Release, "[STEAM NETWORKING][%s] connected\n", pInfo->m_info.m_szConnectionDescription);

		if (pInfo->m_info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Unauthenticated)
		{
			PeerTransportLog(PeerLogLevel::Release, "[CONNECTION FLAGS]: has k_nSteamNetworkConnectionInfoFlags_Unauthenticated");
		}
		else if (pInfo->m_info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Unencrypted)
		{
			PeerTransportLog(PeerLogLevel::Release, "[CONNECTION FLAGS]: has k_nSteamNetworkConnectionInfoFlags_Unencrypted");
		}
		else if (pInfo->m_info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_LoopbackBuffers)
		{
			PeerTransportLog(PeerLogLevel::Release, "[CONNECTION FLAGS]: has k_nSteamNetworkConnectionInfoFlags_LoopbackBuffers");
		}
		else if (pInfo->m_info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Fast)
		{
			PeerTransportLog(PeerLogLevel::Release, "[CONNECTION FLAGS]: has k_nSteamNetworkConnectionInfoFlags_Fast");
		}
		else if (pInfo->m_info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed)
		{
			PeerTransportLog(PeerLogLevel::Release, "[CONNECTION FLAGS]: has k_nSteamNetworkConnectionInfoFlags_Relayed");
		}
		else if (pInfo->m_info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_DualWifi)
		{
			PeerTransportLog(PeerLogLevel::Release, "[CONNECTION FLAGS]: has k_nSteamNetworkConnectionInfoFlags_DualWifi");
		}

		if (bFound)
		{
			// the peer that finished the handshake must be the user this connection was made for
			int64_t remoteUserID = 0;
			if (!ParseUserIdentity(pInfo->m_info.m_identityRemote, remoteUserID) || remoteUserID != peer.userID)
			{
				PeerTransportLog(PeerLogLevel::Release, "[SECURITY] Closing connection %s: its identity is not user %lld", pInfo->m_info.m_szConnectionDescription, static_cast<long long>(peer.userID));
				SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 1001, "Identity mismatch (Rejected)", false);
				break;
			}

			pListener->OnStateChanged(peer, GnsPeerState::Connected);
		}

		break;

	default:
		PeerTransportLog(PeerLogLevel::Release, "[STEAM CALLBACK] Unhandled case");
		break;
	}
}

/// Implementation of ITrivialSignalingClient
class GnsSignalingClient
{

	// This is the thing we'll actually create to send signals for a particular
	// connection.
	struct ConnectionSignaling : ISteamNetworkingConnectionSignaling
	{
		GnsSignalingClient* const m_pOwner;
		int64_t const m_targetUserID;

		ConnectionSignaling(GnsSignalingClient* owner, int64_t target_user_id)
			: m_pOwner(owner)
			, m_targetUserID(target_user_id)
		{
		}

		//
		// Implements ISteamNetworkingConnectionSignaling
		//

		// This is called from SteamNetworkingSockets to send a signal.  This could be called from any thread,
		// so we need to be threadsafe, and avoid duoing slow stuff or calling back into SteamNetworkingSockets
		virtual bool SendSignal(HSteamNetConnection hConn, const SteamNetConnectionInfo_t& info, const void* pMsg, int cbMsg) override
		{
			// Silence warnings
			(void)info;
			(void)hConn;

			std::vector<uint8_t> vecPayload(cbMsg);
			memcpy_s(vecPayload.data(), vecPayload.size(), pMsg, cbMsg);

			m_pOwner->Send(m_targetUserID, vecPayload);
			return true;
		}

		// Self destruct.  This will be called by SteamNetworkingSockets when it's done with us.
		virtual void Release() override
		{
			// SECURITY FIX: Avoid immediate "delete this" which can cause use-after-free
			// when called from async Steam callbacks. Instead, defer deletion to prevent
			// races where CSignalingClient might be destroyed while this object is still
			// being accessed or its owner pointer is being used.
			std::scoped_lock<std::mutex> lock(g_pendingDeletionMutex);
			g_pendingConnSignalingDeletions.push_back(static_cast<void*>(this));
		}
	};

	ISteamNetworkingSockets* const m_pSteamNetworkingSockets;
	IPeerSignalChannel* const m_pChannel;

	// SendSignal() may run on any thread and queues here, Poll() drains on the main thread.
	PeerSignalOutbox m_outbox;

public:
	GnsSignalingClient(ISteamNetworkingSockets* pSteamNetworkingSockets, IPeerSignalChannel* pChannel)
		:  m_pSteamNetworkingSockets(pSteamNetworkingSockets)
		, m_pChannel(pChannel)
	{
		// Save off our identity
		SteamNetworkingIdentity identitySelf; identitySelf.Clear();
		pSteamNetworkingSockets->GetIdentity(&identitySelf);

		if (identitySelf.IsInvalid())
		{
			PeerTransportLog(PeerLogLevel::Release, "CSignalingClient: Local identity is invalid\n");
		}

		if (identitySelf.IsLocalHost())
		{
			PeerTransportLog(PeerLogLevel::Release, "CSignalingClient: Local identity is localhost\n");
		}

	}

	// May be called from any thread; always queues, Poll() flushes on the main thread.
	void Send(int64_t target_user_id, std::vector<uint8_t>& vecPayload)
	{
		m_outbox.Push(target_user_id, vecPayload);
	}

	ISteamNetworkingConnectionSignaling* CreateSignalingForConnection(
		const SteamNetworkingIdentity& identityPeer,
		SteamNetworkingErrMsg& errMsg
	) {
		SteamNetworkingIdentityRender sIdentityPeer(identityPeer);

		// FIXME - here we really ought to confirm that the string version of the
		// identity does not have spaces, since our protocol doesn't permit it.
		PeerTransportLog(PeerLogLevel::Debug, "Creating signaling session for peer '%s'\n", sIdentityPeer.c_str());

		// The identity comes from the remote peer, so only accept a plain integer user id
		int64_t user_id = 0;
		const bool bValidIdentity = ParseUserIdentity(identityPeer, user_id);

		if (!bValidIdentity)
		{
			PeerTransportLog(PeerLogLevel::Release, "[SIGNAL] Rejecting connection, peer identity '%s' is not a valid user id", sIdentityPeer.c_str());
			snprintf(errMsg, sizeof(errMsg), "Invalid peer identity");
			return nullptr;
		}

		return new ConnectionSignaling(this, user_id);
	}

	void Poll()
	{
		if (m_pChannel != nullptr && m_pChannel->IsOpen())
		{
			m_outbox.FlushTo(*m_pChannel);

			std::deque<PeerSignal> pendingSignals;
			m_pChannel->DrainSignals(pendingSignals);

			// Now dispatch any buffered signals
			if (!pendingSignals.empty())
			{
				PeerTransportLog(PeerLogLevel::Release, "[SIGNAL] PROCESS SIGNAL!");
				while (!pendingSignals.empty())
				{
					// NOTE: outbound msg doesnt need sender ID, we only need that to determine target on the server, everything else is included in the payload
					//
					// Get the next signal
					const PeerSignal pendingSignal = pendingSignals.front();
					const std::vector<uint8_t>& signalData = pendingSignal.payload;
					pendingSignals.pop_front();

					// Setup a context object that can respond if this signal is a connection request.
					struct Context : ISteamNetworkingSignalingRecvContext
					{
						GnsSignalingClient* m_pOwner;
						int64_t m_serverSender = -1;	// the user the service delivered this signal from

						virtual ISteamNetworkingConnectionSignaling* OnConnectRequest(
							HSteamNetConnection hConn,
							const SteamNetworkingIdentity& identityPeer,
							int nLocalVirtualPort
						) override {
							// Silence warnings
							(void)hConn;
							;						(void)nLocalVirtualPort;

							// We will just always handle requests through the usual listen socket state
							// machine.  See the documentation for this function for other behaviour we
							// might take.

							// Also, note that if there was routing/session info, it should have been in
							// our envelope that we know how to parse, and we should save it off in this
							// context object.
							// The identity in the handshake is whatever the peer wrote. The service stamped the signal with
							// the user it came from, so a request claiming to be somebody else is not answered.
							int64_t claimedUserID = 0;
							if (ParseUserIdentity(identityPeer, claimedUserID) && claimedUserID != m_serverSender)
							{
								PeerTransportLog(PeerLogLevel::Release, "[SECURITY] Rejecting connection request: the handshake claims user %lld but the service delivered the signal from user %lld",
									static_cast<long long>(claimedUserID), static_cast<long long>(m_serverSender));
								return nullptr;
							}

							SteamNetworkingErrMsg ignoreErrMsg;
							return m_pOwner->CreateSignalingForConnection(identityPeer, ignoreErrMsg);
						}

						virtual void SendRejectionSignal(
							const SteamNetworkingIdentity& identityPeer,
							const void* pMsg, int cbMsg
						) override {

							// We'll just silently ignore all failures.  This is actually the more secure
							// Way to handle it in many cases.  Actively returning failure might allow
							// an attacker to just scrape random peers to see who is online.  If you know
							// the peer has a good reason for trying to connect, sending an active failure
							// can improve error handling and the UX, instead of relying on timeout.  But
							// just consider the security implications.
							PeerTransportLog(PeerLogLevel::Release, "[STEAM NETWORKING] Sending rejection signal");
							// Silence warnings
							(void)identityPeer;
							(void)pMsg;
							(void)cbMsg;
						}
					};
					Context context;
					context.m_pOwner = this;
					context.m_serverSender = pendingSignal.senderUserID;

					// Dispatch.
					// Remember: From inside this function, our context object might get callbacks.
					// And we might get asked to send signals, either now, or really at any time
					// from any thread!  If possible, avoid calling this function while holding locks.
					// To process this call, SteamnetworkingSockets will need take its own internal lock.
					// That lock may be held by another thread that is asking you to send a signal!  So
					// be warned that deadlocks are a possibility here.
					m_pSteamNetworkingSockets->ReceivedP2PCustomSignal(signalData.data(), (int)signalData.size(), &context);
				}
			}
		}
		}
};


bool GnsPeerLibrary::s_bInitialized = false;

bool GnsPeerLibrary::EnsureInitialized(int64_t userID, const GnsNetworkConfig& config, IGnsPeerListener* pListener)
{
	// lives until the online services shut down; a new login always follows a full teardown
	if (s_bInitialized)
	{
		return true;
	}

	SteamNetworkingIdentity identityLocal;
	identityLocal.Clear();
	std::string userIDStr = std::to_string(userID);
	identityLocal.SetGenericString(userIDStr.c_str());

	if (identityLocal.IsInvalid())
	{
		PeerTransportLog(PeerLogLevel::Release, "NetworkMeshLibrary::EnsureInitialized: SteamNetworkingIdentity is invalid");
		return false;
	}

	SteamDatagramErrMsg errMsg;
	if (!GameNetworkingSockets_Init(&identityLocal, errMsg))
	{
		PeerTransportLog(PeerLogLevel::Release, "NetworkMeshLibrary::EnsureInitialized: GameNetworkingSockets_Init failed. %s", errMsg);
		return false;
	}

	s_bInitialized = true;

	// Every STUN entry must resolve to a distinct address, or the native ICE client retries
	// duplicates forever.
	SteamNetworkingUtils()->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_STUN_ServerList, config.stunServers.c_str());


	ESteamNetworkingSocketsDebugOutputType logType = config.verboseLogging
		? ESteamNetworkingSocketsDebugOutputType::k_ESteamNetworkingSocketsDebugOutputType_Debug
		: ESteamNetworkingSocketsDebugOutputType::k_ESteamNetworkingSocketsDebugOutputType_Msg;

	SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_LogLevel_P2PRendezvous, logType);
	SteamNetworkingUtils()->SetDebugOutputFunction(logType, [](ESteamNetworkingSocketsDebugOutputType nType, const char* pszMsg)
		{
			(void)nType;
			PeerTransportLog(PeerLogLevel::Release, "[STEAM NETWORKING LOGFUNC] %s", pszMsg);
		});

	s_pListener = pListener;
	SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(OnSteamNetConnectionStatusChanged);

	return true;
}

void GnsPeerLibrary::Shutdown()
{
	if (!s_bInitialized)
	{
		return;
	}

	SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(nullptr);
	s_pListener = nullptr;
	GameNetworkingSockets_Kill();

	s_bInitialized = false;
}

void GnsPeerLibrary::Tick()
{
	if (!s_bInitialized)
	{
		return;
	}

	if (SteamNetworkingSockets())
	{
		SteamNetworkingSockets()->RunCallbacks();
	}
}

GnsPeerNetwork::GnsPeerNetwork()
{
}

GnsPeerNetwork::~GnsPeerNetwork()
{
}

bool GnsPeerNetwork::Init(const GnsNetworkConfig& config, IPeerSignalChannel* pChannel)
{
	m_config = config;

	m_pSignaling = std::make_unique<GnsSignalingClient>(SteamNetworkingSockets(), pChannel);
	if (m_pSignaling == nullptr)
	{
		PeerTransportLog(PeerLogLevel::Release, "CreateTrivialSignalingClient failed");
		return false;
	}

	return CreateListenSocket();
}

bool GnsPeerNetwork::CreateListenSocket()
{
	std::vector<SteamNetworkingConfigValue_t> vecListenOpts;
	SteamNetworkingConfigValue_t opt;
	const GnsTimeouts& listenTimeouts = m_config.GetTimeouts(false);
	opt.SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, listenTimeouts.initialMs);
	vecListenOpts.push_back(opt);
	opt.SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, listenTimeouts.connectedMs);
	vecListenOpts.push_back(opt);
	opt.SetInt32(k_ESteamNetworkingConfig_SymmetricConnect, 1);
	vecListenOpts.push_back(opt);
	opt.SetInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable, m_config.GetIceEnable());
	vecListenOpts.push_back(opt);
	opt.SetInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Implementation, m_config.iceImplementation);
	vecListenOpts.push_back(opt);
	const std::string strTurnUserList = m_config.GetTurnUserList();
	const std::string strTurnPassList = m_config.GetTurnPassList();
	opt.SetString(k_ESteamNetworkingConfig_P2P_TURN_ServerList, m_config.turnServers.c_str());
	vecListenOpts.push_back(opt);
	opt.SetString(k_ESteamNetworkingConfig_P2P_TURN_UserList, strTurnUserList.c_str());
	vecListenOpts.push_back(opt);
	opt.SetString(k_ESteamNetworkingConfig_P2P_TURN_PassList, strTurnPassList.c_str());
	vecListenOpts.push_back(opt);

	int localPort = 0;
	m_hListenSock = SteamNetworkingSockets()->CreateListenSocketP2P(localPort, (int)vecListenOpts.size(), vecListenOpts.data());

	if (m_hListenSock == k_HSteamListenSocket_Invalid)
	{
		PeerTransportLog(PeerLogLevel::Release, "CreateListenSocketP2P failed. Sock was invalid");
		return false;
	}

	return true;
}

void GnsPeerNetwork::SetTurnCredentials(const std::string& strUsername, const std::string& strToken)
{
	m_config.turnUsername = strUsername;
	m_config.turnToken = strToken;

	// incoming connections use the listen socket's TURN settings
	if (m_hListenSock != k_HSteamListenSocket_Invalid)
	{
		const std::string strTurnUserList = m_config.GetTurnUserList();
		const std::string strTurnPassList = m_config.GetTurnPassList();
		SteamNetworkingUtils()->SetConfigValue(k_ESteamNetworkingConfig_P2P_TURN_UserList, k_ESteamNetworkingConfig_ListenSocket,
			(intptr_t)m_hListenSock, k_ESteamNetworkingConfig_String, strTurnUserList.c_str());
		SteamNetworkingUtils()->SetConfigValue(k_ESteamNetworkingConfig_P2P_TURN_PassList, k_ESteamNetworkingConfig_ListenSocket,
			(intptr_t)m_hListenSock, k_ESteamNetworkingConfig_String, strTurnPassList.c_str());
	}
}

HSteamNetConnection GnsPeerNetwork::Connect(int64_t remoteUserID, int virtualPortRemote, bool bRelay, bool bRetryTimeouts)
{
	SteamNetworkingIdentity identityRemote;
	identityRemote.Clear();
	std::string remoteUserIDStr = std::to_string(remoteUserID);
	identityRemote.SetGenericString(remoteUserIDStr.c_str());

	if (identityRemote.IsInvalid())
	{
		// TODO_STEAM: Handle this better
		PeerTransportLog(PeerLogLevel::Release, "NetworkMesh::ConnectToSingleUser - SteamNetworkingIdentity is invalid");
		return k_HSteamNetConnection_Invalid;
	}

	std::vector<SteamNetworkingConfigValue_t > vecOpts;

	int g_nLocalPort = 0;

	int g_nVirtualPortRemote = virtualPortRemote;

	// Our remote and local port don't match, so we need to set it explicitly
	if (g_nVirtualPortRemote != g_nLocalPort)
	{
		SteamNetworkingConfigValue_t opt;
		opt.SetInt32(k_ESteamNetworkingConfig_LocalVirtualPort, g_nLocalPort);
		vecOpts.push_back(opt);
	}

	// Set symmetric connect mode
	SteamNetworkingConfigValue_t opt;
	opt.SetInt32(k_ESteamNetworkingConfig_SymmetricConnect, 1);
	vecOpts.push_back(opt);

	// A direct step gathers host and server reflexive candidates only, no TURN server joins the connection.
	opt.SetInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable, bRelay ? m_config.GetIceEnable() : k_gnsIceEnableDirectOnly);
	vecOpts.push_back(opt);
	opt.SetInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Implementation, m_config.iceImplementation);
	vecOpts.push_back(opt);
	const std::string strTurnUserList = m_config.GetTurnUserList();
	const std::string strTurnPassList = m_config.GetTurnPassList();
	if (bRelay)
	{
		opt.SetString(k_ESteamNetworkingConfig_P2P_TURN_ServerList, m_config.turnServers.c_str());
		vecOpts.push_back(opt);
		opt.SetString(k_ESteamNetworkingConfig_P2P_TURN_UserList, strTurnUserList.c_str());
		vecOpts.push_back(opt);
		opt.SetString(k_ESteamNetworkingConfig_P2P_TURN_PassList, strTurnPassList.c_str());
		vecOpts.push_back(opt);
	}
	else
	{
		// An implicitly accepted incoming connection takes these options over the TURN list of the listen socket.
		opt.SetString(k_ESteamNetworkingConfig_P2P_TURN_ServerList, "");
		vecOpts.push_back(opt);
	}

	PeerTransportLog(PeerLogLevel::Debug, "Connecting to '%s' in symmetric mode, virtual port %d, from local virtual port %d.\n",
		SteamNetworkingIdentityRender(identityRemote).c_str(), g_nVirtualPortRemote, g_nLocalPort);

	if (m_pSignaling == nullptr)
	{
		PeerTransportLog(PeerLogLevel::Release, "NetworkMesh::StartConnectionSignalling - Signalling client is null (mesh failed to initialize)");
		return k_HSteamNetConnection_Invalid;
	}

	const GnsTimeouts& timeouts = m_config.GetTimeouts(bRetryTimeouts);
	opt.SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, timeouts.initialMs);
	vecOpts.push_back(opt);
	opt.SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, timeouts.connectedMs);
	vecOpts.push_back(opt);
	PeerTransportLog(PeerLogLevel::Release, "[MESH] Connection timeouts peer=%lld retry=%d initial=%dms connected=%dms", remoteUserID, bRetryTimeouts ? 1 : 0, timeouts.initialMs, timeouts.connectedMs);

	// create a signaling object for this connection
	SteamNetworkingErrMsg errMsg;
	ISteamNetworkingConnectionSignaling* pConnSignaling = m_pSignaling->CreateSignalingForConnection(identityRemote, errMsg);

	if (pConnSignaling == nullptr)
	{
		// TODO_STEAM: Handle this better
		PeerTransportLog(PeerLogLevel::Release, "NetworkMesh::ConnectToSingleUser - Could not create signalling object, error was %s", errMsg);
		return k_HSteamNetConnection_Invalid;
	}

	// make a steam connection obj
	HSteamNetConnection hSteamConnection = SteamNetworkingSockets()->ConnectP2PCustomSignaling(pConnSignaling, &identityRemote, g_nVirtualPortRemote, (int)vecOpts.size(), vecOpts.data());

	if (hSteamConnection == k_HSteamNetConnection_Invalid)
	{
		// TODO_STEAM: Handle this better
		PeerTransportLog(PeerLogLevel::Release, "NetworkMesh::ConnectToSingleUser - Steam network connection obj was k_HSteamNetConnection_Invalid");
		return k_HSteamNetConnection_Invalid;
	}

	return hSteamConnection;
}

void GnsPeerNetwork::ApplyIncomingSetup(HSteamNetConnection hConnection, int64_t userID, const GnsIncomingSetup& setup)
{
	const GnsTimeouts& timeouts = m_config.GetTimeouts(setup.retryTimeouts);
	const bool initialSet = SteamNetworkingUtils()->SetConnectionConfigValueInt32(hConnection, k_ESteamNetworkingConfig_TimeoutInitial, timeouts.initialMs);
	const bool connectedSet = SteamNetworkingUtils()->SetConnectionConfigValueInt32(hConnection, k_ESteamNetworkingConfig_TimeoutConnected, timeouts.connectedMs);
	PeerTransportLog(PeerLogLevel::Release, "[MESH] Incoming timeouts peer=%lld retry=%d initial=%dms connected=%dms applied=%d", userID, setup.retryTimeouts ? 1 : 0, timeouts.initialMs, timeouts.connectedMs, initialSet && connectedSet ? 1 : 0);

	if (!setup.directOnly)
	{
		return;
	}

	// The incoming connection inherits TURN from the listen socket, a direct step takes it away before ICE starts.
	const bool iceSet = SteamNetworkingUtils()->SetConnectionConfigValueInt32(hConnection, k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable, k_gnsIceEnableDirectOnly);
	const bool turnSet = SteamNetworkingUtils()->SetConnectionConfigValueString(hConnection, k_ESteamNetworkingConfig_P2P_TURN_ServerList, "");
	PeerTransportLog(PeerLogLevel::Release, "[MESH] Incoming connection of %lld uses step %d without TURN, applied=%d", userID, setup.stepAttempt, iceSet && turnSet ? 1 : 0);
}

void GnsPeerNetwork::HoldIncoming(int64_t userID, HSteamNetConnection hConnection)
{
	auto it = m_heldIncoming.find(userID);
	if (it != m_heldIncoming.end() && it->second != hConnection)
	{
		CloseConnection(it->second, 0, "Replaced by a newer incoming connection");
	}
	m_heldIncoming[userID] = hConnection;
	PeerTransportLog(PeerLogLevel::Release, "[MESH] Holding incoming connection %u of %lld until this side starts the pair", hConnection, static_cast<long long>(userID));
}

void GnsPeerNetwork::ForgetHeldIncoming(HSteamNetConnection hConnection)
{
	for (auto it = m_heldIncoming.begin(); it != m_heldIncoming.end(); ++it)
	{
		if (it->second == hConnection)
		{
			m_heldIncoming.erase(it);
			return;
		}
	}
}

HSteamNetConnection GnsPeerNetwork::TakeHeldIncoming(int64_t userID)
{
	auto it = m_heldIncoming.find(userID);
	if (it == m_heldIncoming.end())
	{
		return k_HSteamNetConnection_Invalid;
	}
	const HSteamNetConnection hConnection = it->second;
	m_heldIncoming.erase(it);
	return hConnection;
}

void GnsPeerNetwork::AcceptHeldIncoming(HSteamNetConnection hConnection, int64_t userID, const GnsIncomingSetup& setup)
{
	PeerTransportLog(PeerLogLevel::Release, "[MESH] Accepting held incoming connection %u of %lld", hConnection, static_cast<long long>(userID));
	ApplyIncomingSetup(hConnection, userID, setup);
	SteamNetworkingSockets()->AcceptConnection(hConnection);
}

void GnsPeerNetwork::Poll()
{
	if (m_pSignaling != nullptr)
	{
		m_pSignaling->Poll();
	}
}

void GnsPeerNetwork::CloseListenSocket()
{
	// Held incoming connections belong to the listen socket and close with it.
	if (SteamNetworkingSockets())
	{
		for (auto it = m_heldIncoming.begin(); it != m_heldIncoming.end(); ++it)
		{
			CloseConnection(it->second, 0, "Listen socket closed");
		}
	}
	m_heldIncoming.clear();

	if (SteamNetworkingSockets() && m_hListenSock != k_HSteamListenSocket_Invalid)
	{
		SteamNetworkingSockets()->CloseListenSocket(m_hListenSock);
	}

	m_hListenSock = k_HSteamListenSocket_Invalid;
}

void GnsPeerNetwork::CloseConnection(HSteamNetConnection hConnection, int reason, const char* szDebug)
{
	SteamNetworkingSockets()->CloseConnection(hConnection, reason, szDebug, false);
}
