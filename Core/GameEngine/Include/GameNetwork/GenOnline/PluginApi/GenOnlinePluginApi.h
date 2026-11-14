/* SPDX-License-Identifier: MIT */
/*
 * GenOnline plugin API
 *
 * Copyright (c) 2026 The GenOnline contributors
 *
 * This file is an exception to the GNU General Public License that covers the
 * rest of this project. This file alone is licensed under the MIT license below,
 * so that plugins may include it and use any license.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * A plugin is a DLL that exports one function, GenOnlinePlugin_GetApi. The game
 * passes its host table to that function and the plugin fills in its own table.
 *
 * Every table starts with structSize. A side that reads a table reads only the
 * fields that lie inside structSize, and treats every field past it as absent.
 * New fields are only ever appended, so an older and a newer side keep working
 * together. Sub interface tables also carry their own version number, which
 * changes only when the meaning of an existing field changes.
 *
 * Strings are UTF8 and NUL terminated. A string or buffer passed into a call is
 * owned by the caller and is valid only for the duration of that call, unless a
 * comment below says otherwise.
 *
 * Every function uses the C calling convention. No C++ type, exception or
 * allocator crosses this boundary.
 */

#ifndef GENONLINE_PLUGIN_API_H
#define GENONLINE_PLUGIN_API_H

#include <stdint.h>

#if defined(_WIN32)
#define GENONLINE_PLUGIN_CALL __cdecl
#define GENONLINE_PLUGIN_EXPORT __declspec(dllexport)
#else
#define GENONLINE_PLUGIN_CALL
#define GENONLINE_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Version of the core tables. The host refuses a plugin built for another version. */
#define GENONLINE_PLUGIN_API_VERSION ((uint32_t)1)

/* Versions of the sub interface tables. */
#define GENONLINE_PLUGIN_LINKED_ACCOUNT_API_VERSION ((uint32_t)1)
#define GENONLINE_PLUGIN_ANTICHEAT_API_VERSION ((uint32_t)1)
#define GENONLINE_PLUGIN_TRANSPORT_API_VERSION ((uint32_t)1)

/* Name of the one function a plugin DLL exports. */
#define GENONLINE_PLUGIN_GET_API_EXPORT_NAME "GenOnlinePlugin_GetApi"

/* Result codes of GenOnlinePlugin_GetApi and of Initialize. */
#define GENONLINE_PLUGIN_RESULT_OK ((int32_t)0)
#define GENONLINE_PLUGIN_RESULT_VERSION_MISMATCH ((int32_t)1)
#define GENONLINE_PLUGIN_RESULT_INVALID_ARGUMENT ((int32_t)2)
#define GENONLINE_PLUGIN_RESULT_BAD_ENVIRONMENT ((int32_t)3)
#define GENONLINE_PLUGIN_RESULT_PLATFORM_OPTIONS_FAILED ((int32_t)4)
#define GENONLINE_PLUGIN_RESULT_SDK_INIT_FAILED ((int32_t)5)
#define GENONLINE_PLUGIN_RESULT_PLATFORM_CREATE_FAILED ((int32_t)6)
#define GENONLINE_PLUGIN_RESULT_MISSING_CREDENTIALS ((int32_t)7)
#define GENONLINE_PLUGIN_RESULT_INTERNAL_ERROR ((int32_t)8)

/* Boolean values used wherever a function takes or returns a flag. */
#define GENONLINE_PLUGIN_FALSE ((int32_t)0)
#define GENONLINE_PLUGIN_TRUE ((int32_t)1)

/* Capability bits in GenOnlinePluginApi.capabilities. */
#define GENONLINE_PLUGIN_CAP_LINKED_ACCOUNT ((uint32_t)0x00000001)
#define GENONLINE_PLUGIN_CAP_ANTICHEAT ((uint32_t)0x00000002)
#define GENONLINE_PLUGIN_CAP_TRANSPORT ((uint32_t)0x00000004)

/* Log levels of GenOnlinePluginHostApi.Log. */
#define GENONLINE_PLUGIN_LOG_DEBUG ((int32_t)0)
#define GENONLINE_PLUGIN_LOG_INFO ((int32_t)1)
#define GENONLINE_PLUGIN_LOG_WARNING ((int32_t)2)
#define GENONLINE_PLUGIN_LOG_ERROR ((int32_t)3)

/* Peer connection states reported through GenOnlinePluginHostApi.ConnectionStateChanged. */
#define GENONLINE_PLUGIN_CONNECTION_NOT_CONNECTED ((int32_t)0)
#define GENONLINE_PLUGIN_CONNECTION_CONNECTING_DIRECT ((int32_t)1)
#define GENONLINE_PLUGIN_CONNECTION_FINDING_ROUTE ((int32_t)2)
#define GENONLINE_PLUGIN_CONNECTION_CONNECTED_DIRECT ((int32_t)3)
#define GENONLINE_PLUGIN_CONNECTION_FAILED ((int32_t)4)
#define GENONLINE_PLUGIN_CONNECTION_DISCONNECTED ((int32_t)5)

/* Actions an anticheat asks the game to take against a user. */
#define GENONLINE_PLUGIN_ACTION_NONE ((int32_t)0)
#define GENONLINE_PLUGIN_ACTION_KICK ((int32_t)1)

/* Reasons that come with an anticheat action. */
#define GENONLINE_PLUGIN_ACTION_REASON_UNKNOWN ((int32_t)0)
#define GENONLINE_PLUGIN_ACTION_REASON_INTERNAL_ERROR ((int32_t)1)
#define GENONLINE_PLUGIN_ACTION_REASON_INVALID_MESSAGE ((int32_t)2)
#define GENONLINE_PLUGIN_ACTION_REASON_AUTH_FAILURE ((int32_t)3)
#define GENONLINE_PLUGIN_ACTION_REASON_NOT_RUNNING ((int32_t)4)
#define GENONLINE_PLUGIN_ACTION_REASON_HEARTBEAT_TIMED_OUT ((int32_t)5)
#define GENONLINE_PLUGIN_ACTION_REASON_CLIENT_VIOLATION ((int32_t)6)
#define GENONLINE_PLUGIN_ACTION_REASON_BACKEND_VIOLATION ((int32_t)7)
#define GENONLINE_PLUGIN_ACTION_REASON_TEMP_COOLDOWN ((int32_t)8)
#define GENONLINE_PLUGIN_ACTION_REASON_TEMP_BANNED ((int32_t)9)
#define GENONLINE_PLUGIN_ACTION_REASON_PERMA_BANNED ((int32_t)10)

/* Client types of a registered anticheat peer. */
#define GENONLINE_PLUGIN_CLIENT_PROTECTED ((int32_t)0)
#define GENONLINE_PLUGIN_CLIENT_UNPROTECTED ((int32_t)1)

/* Transport channels. */
#define GENONLINE_PLUGIN_CHANNEL_GAME ((uint8_t)0)
#define GENONLINE_PLUGIN_CHANNEL_ANTICHEAT ((uint8_t)1)
#define GENONLINE_PLUGIN_CHANNEL_SIGNALLING ((uint8_t)2)
#define GENONLINE_PLUGIN_CHANNEL_PING ((uint8_t)3)
#define GENONLINE_PLUGIN_CHANNEL_PONG ((uint8_t)4)

/* Transport reliability modes. */
#define GENONLINE_PLUGIN_RELIABILITY_UNRELIABLE_UNORDERED ((int32_t)0)
#define GENONLINE_PLUGIN_RELIABILITY_RELIABLE_UNORDERED ((int32_t)1)
#define GENONLINE_PLUGIN_RELIABILITY_RELIABLE_ORDERED ((int32_t)2)

/*
 * Game to plugin. The host fills this table and passes it to
 * GenOnlinePlugin_GetApi. The table and every callback in it stay valid until
 * the plugin Shutdown function returns. The plugin may call these callbacks from
 * any thread. The plugin passes userData back unchanged as the first argument.
 */
typedef struct GenOnlinePluginHostApi
{
	uint32_t structSize;
	void* userData;

	/* Writes one line to the game log. */
	void (GENONLINE_PLUGIN_CALL *Log)(void* userData, int32_t level, const char* message);

	/* Shows one line in the lobby chat as a system notice. */
	void (GENONLINE_PLUGIN_CALL *LobbyChatOutput)(void* userData, const char* message);

	/* Reports a new state of the plugin transport connection to a peer. */
	void (GENONLINE_PLUGIN_CALL *ConnectionStateChanged)(void* userData, const char* platformUserId, uint64_t genOnlineUserId, int32_t newState);

	/* Asks the game to act against a user, for example to kick a remote peer or to leave the lobby. */
	void (GENONLINE_PLUGIN_CALL *ActionRequired)(void* userData, uint64_t genOnlineUserId, const char* reason, int32_t actionType, int32_t actionReason);

	/* Reports that the integrity of the local client is violated. The game leaves the lobby. */
	void (GENONLINE_PLUGIN_CALL *IntegrityViolation)(void* userData, const char* reason, int32_t violationType);

	/* Asks the game to deliver an anticheat message to a peer over the game transport. */
	void (GENONLINE_PLUGIN_CALL *SendMessageViaTransport)(void* userData, uint64_t genOnlineUserId, const void* data, uint32_t dataLength);

	/*
	 * GENONLINE_PLUGIN_CAP_ bits of the sub interfaces that the host uses this
	 * plugin for. The plugin runs only the parts of those sub interfaces. Without
	 * GENONLINE_PLUGIN_CAP_ANTICHEAT the host still calls the session and peer
	 * functions of the anticheat table so that the transport knows its peers, and
	 * the plugin then runs no anticheat and sends no anticheat message. A host
	 * table too short to hold this field means every capability the plugin offers.
	 */
	uint32_t enabledCapabilities;
} GenOnlinePluginHostApi;

/* Receives the result of a linked account sign in or refresh. success is GENONLINE_PLUGIN_TRUE or GENONLINE_PLUGIN_FALSE. */
typedef void (GENONLINE_PLUGIN_CALL *GenOnlinePluginLinkedAccountStatusFn)(void* userData, int32_t success);

/*
 * Linked account. The plugin signs in to its platform with the GenOnline
 * token and produces the identity proof that the GenOnline service verifies.
 */
typedef struct GenOnlinePluginLinkedAccountApi
{
	uint32_t structSize;
	uint32_t version;

	/* Signs in to the platform with the GenOnline token. The plugin calls onStatus exactly once, also on failure. */
	void (GENONLINE_PLUGIN_CALL *SignIn)(const char* genOnlineToken, GenOnlinePluginLinkedAccountStatusFn onStatus, void* userData);

	/* Refreshes the platform sign in with a current GenOnline token. The plugin calls onStatus exactly once. */
	void (GENONLINE_PLUGIN_CALL *Refresh)(const char* genOnlineToken, GenOnlinePluginLinkedAccountStatusFn onStatus, void* userData);

	/* Returns GENONLINE_PLUGIN_TRUE while the platform sign in is valid. */
	int32_t (GENONLINE_PLUGIN_CALL *IsSignedIn)(void);

	/*
	 * Copies the platform user id of the signed in user into buffer. The host
	 * owns buffer. The plugin writes a NUL terminated string of at most
	 * bufferSize bytes and returns GENONLINE_PLUGIN_TRUE, or returns
	 * GENONLINE_PLUGIN_FALSE when no id is available or it does not fit.
	 */
	int32_t (GENONLINE_PLUGIN_CALL *GetPlatformUserId)(char* buffer, uint32_t bufferSize);

	/*
	 * Copies the identity proof that the GenOnline service verifies into buffer.
	 * The host owns buffer. The plugin writes a NUL terminated string of at most
	 * bufferSize bytes and returns GENONLINE_PLUGIN_TRUE, or returns
	 * GENONLINE_PLUGIN_FALSE when no proof is available or it does not fit.
	 */
	int32_t (GENONLINE_PLUGIN_CALL *GetIdentityProof)(char* buffer, uint32_t bufferSize);
} GenOnlinePluginLinkedAccountApi;

/* Anticheat session and peer handling. */
typedef struct GenOnlinePluginAnticheatApi
{
	uint32_t structSize;
	uint32_t version;

	/* Starts the anticheat session of a lobby or match. */
	void (GENONLINE_PLUGIN_CALL *BeginSession)(void);

	/* Ends the current anticheat session. Calling it without a session does nothing. */
	void (GENONLINE_PLUGIN_CALL *EndSession)(void);

	/* Adds a peer to the session. clientType is one of GENONLINE_PLUGIN_CLIENT_. Returns GENONLINE_PLUGIN_TRUE on success. */
	int32_t (GENONLINE_PLUGIN_CALL *RegisterPeer)(const char* platformUserId, uint64_t genOnlineUserId, int32_t clientType);

	/* Removes a peer from the session. Returns GENONLINE_PLUGIN_TRUE on success. */
	int32_t (GENONLINE_PLUGIN_CALL *DeregisterPeer)(const char* platformUserId, uint64_t genOnlineUserId);

	/* Delivers an anticheat message that a peer sent through the game transport. */
	void (GENONLINE_PLUGIN_CALL *MessageArrived)(uint64_t genOnlineUserId, const void* data, uint32_t dataLength);
} GenOnlinePluginAnticheatApi;

/* Peer to peer transport that the plugin provides in place of the game transport. */
typedef struct GenOnlinePluginTransportApi
{
	uint32_t structSize;
	uint32_t version;

	/* Returns GENONLINE_PLUGIN_TRUE when the plugin carries the game traffic. */
	int32_t (GENONLINE_PLUGIN_CALL *IsAvailable)(void);

	/* Tells the plugin whether the service configuration allows the plugin transport. This field may be null. */
	void (GENONLINE_PLUGIN_CALL *SetEnabled)(int32_t enabled);

	/* Starts the connection to a peer. */
	void (GENONLINE_PLUGIN_CALL *StartSignalling)(const char* platformUserId, uint64_t genOnlineUserId);

	/* Sends one packet to a peer. channel is one of GENONLINE_PLUGIN_CHANNEL_ and reliability one of GENONLINE_PLUGIN_RELIABILITY_. */
	void (GENONLINE_PLUGIN_CALL *SendPacket)(const char* platformUserId, uint64_t genOnlineUserId, const void* data, uint32_t dataLength, uint8_t channel, int32_t reliability);

	/* Returns the size in bytes of the next pending packet on channel, or 0 when none is pending. */
	uint32_t (GENONLINE_PLUGIN_CALL *GetNextPacketSize)(uint8_t channel);

	/*
	 * Takes the next pending packet on channel without a copy. On success the
	 * plugin stores its own buffer in outData and its size in outDataLength and
	 * returns GENONLINE_PLUGIN_TRUE. The buffer stays owned by the plugin and
	 * the host returns it through FreePacket exactly once.
	 */
	int32_t (GENONLINE_PLUGIN_CALL *ReceivePacket)(uint8_t channel, const uint8_t** outData, uint32_t* outDataLength);

	/* Releases a buffer that ReceivePacket returned. */
	void (GENONLINE_PLUGIN_CALL *FreePacket)(const uint8_t* data);

	/* Returns the round trip latency to a peer in milliseconds. */
	int32_t (GENONLINE_PLUGIN_CALL *GetLatency)(const char* platformUserId, uint64_t genOnlineUserId);

	/* Returns GENONLINE_PLUGIN_TRUE when the connection to a peer runs through a relay. */
	int32_t (GENONLINE_PLUGIN_CALL *IsRelayed)(const char* platformUserId, uint64_t genOnlineUserId);

	/* Closes the connection to one peer. */
	void (GENONLINE_PLUGIN_CALL *DisconnectPeer)(const char* platformUserId, uint64_t genOnlineUserId);

	/* Closes the connections to all peers. */
	void (GENONLINE_PLUGIN_CALL *DisconnectAll)(void);

	/*
	 * Works like ReceivePacket and also stores the GenOnline user id of the peer
	 * that sent the packet in outSenderGenOnlineUserId. The plugin returns only
	 * packets of peers that the game registered, and drops the others. The host
	 * uses the plugin transport only when the plugin fills this field.
	 */
	int32_t (GENONLINE_PLUGIN_CALL *ReceivePacketFrom)(uint8_t channel, const uint8_t** outData, uint32_t* outDataLength, uint64_t* outSenderGenOnlineUserId);
} GenOnlinePluginTransportApi;

/*
 * Plugin to game. The host zeroes this table, sets structSize to its own size
 * and passes it to GenOnlinePlugin_GetApi. The plugin fills the fields that lie
 * inside that size and sets structSize to the number of bytes it filled. Every
 * string and table that the plugin stores here stays valid until the DLL is
 * unloaded. A sub interface pointer is null when the plugin does not provide it.
 */
typedef struct GenOnlinePluginApi
{
	uint32_t structSize;

	/* GENONLINE_PLUGIN_API_VERSION that the plugin was built against. */
	uint32_t apiVersion;

	/* Short name of the plugin, equal to the name of its folder and DLL. */
	const char* name;

	/* Version of the plugin for the log. */
	const char* version;

	/* GENONLINE_PLUGIN_CAP_ bits of the sub interfaces below. */
	uint32_t capabilities;

	/* Starts the plugin. Returns GENONLINE_PLUGIN_RESULT_OK or a failure code, after which the host calls Shutdown and unloads the plugin. */
	int32_t (GENONLINE_PLUGIN_CALL *Initialize)(void);

	/* Runs once per services update on the main thread. */
	void (GENONLINE_PLUGIN_CALL *Tick)(void);

	/* Releases everything the plugin holds. The host calls it before it unloads the DLL. Calling it twice does nothing. */
	void (GENONLINE_PLUGIN_CALL *Shutdown)(void);

	/* Returns GENONLINE_PLUGIN_TRUE when the external process that the plugin needs is running. This field may be null when the plugin needs no external process. */
	int32_t (GENONLINE_PLUGIN_CALL *IsExternalProcessRunning)(void);

	/* Returns the anticheat identifier that the game sends to the GenOnline service. This field may be null, which reports 0. */
	int32_t (GENONLINE_PLUGIN_CALL *GetAnticheatIdentifier)(void);

	const GenOnlinePluginLinkedAccountApi* linkedAccount;
	const GenOnlinePluginAnticheatApi* anticheat;
	const GenOnlinePluginTransportApi* transport;
} GenOnlinePluginApi;

/*
 * The one export of a plugin DLL. hostApiVersion is GENONLINE_PLUGIN_API_VERSION
 * of the host. The plugin returns GENONLINE_PLUGIN_RESULT_VERSION_MISMATCH when
 * it cannot serve that version and GENONLINE_PLUGIN_RESULT_OK after it filled out.
 */
typedef int32_t (GENONLINE_PLUGIN_CALL *GenOnlinePluginGetApiFn)(uint32_t hostApiVersion, const GenOnlinePluginHostApi* host, GenOnlinePluginApi* out);

/* A plugin defines this function. The host resolves it by name and never links against it. */
GENONLINE_PLUGIN_EXPORT int32_t GENONLINE_PLUGIN_CALL GenOnlinePlugin_GetApi(uint32_t hostApiVersion, const GenOnlinePluginHostApi* host, GenOnlinePluginApi* out);

#ifdef __cplusplus
}
#endif

#endif /* GENONLINE_PLUGIN_API_H */
