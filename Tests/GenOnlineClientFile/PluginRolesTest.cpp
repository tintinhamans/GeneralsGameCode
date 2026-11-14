// Standalone checks of the plugin role rules in GenOnlinePluginRoles. Builds without the game, see CMakeLists.txt.
#include "GameNetwork/GenOnline/GenOnlinePluginRoles.h"
#include <cstdio>

using namespace GenOnlinePluginRoles;

static int s_failures = 0;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			++s_failures; \
		} \
	} while (0)

static void TestNoExternalPlugin()
{
	// GenOnline Anti-Cheat with transport [] loads no plugin DLL.
	Selection none = Select("", {});
	CHECK(none.pluginName.empty());
	CHECK(!none.bAnticheat);
	CHECK(!none.bTransport);
	CHECK(none.skippedTransports.empty());

	// Empty transport names load nothing.
	Selection empty = Select("", { "" });
	CHECK(empty.pluginName.empty());
	CHECK(!empty.bAnticheat);
	CHECK(!empty.bTransport);
	CHECK(empty.skippedTransports.empty());
}

static void TestTransportOnly()
{
	// GenOnline Anti-Cheat with genonline_eos in transport loads the EOS plugin for its transport only.
	Selection eos = Select("", { "genonline_eos" });
	CHECK(eos.pluginName == "genonline_eos");
	CHECK(!eos.bAnticheat);
	CHECK(eos.bTransport);
	CHECK(eos.skippedTransports.empty());

	// The first transport plugin loads, the others are reported as skipped.
	Selection first = Select("", { "", "genonline_eos", "other", "genonline_eos" });
	CHECK(first.pluginName == "genonline_eos");
	CHECK(!first.bAnticheat);
	CHECK(first.bTransport);
	CHECK(first.skippedTransports.size() == 1);
	CHECK(first.skippedTransports.size() == 1 && first.skippedTransports[0] == "other");
}

static void TestExternalAnticheat()
{
	// Easy Anti-Cheat through the EOS plugin runs the anticheat and the transport.
	Selection eac = Select("genonline_eos", { "genonline_eos" });
	CHECK(eac.pluginName == "genonline_eos");
	CHECK(eac.bAnticheat);
	CHECK(eac.bTransport);
	CHECK(eac.skippedTransports.empty());

	// An external anticheat that the transport list does not name runs without its transport.
	Selection acOnly = Select("genonline_eos", {});
	CHECK(acOnly.pluginName == "genonline_eos");
	CHECK(acOnly.bAnticheat);
	CHECK(!acOnly.bTransport);
	CHECK(acOnly.skippedTransports.empty());

	// The anticheat plugin wins over a different transport plugin, which does not load.
	Selection other = Select("other_anticheat", { "genonline_eos" });
	CHECK(other.pluginName == "other_anticheat");
	CHECK(other.bAnticheat);
	CHECK(!other.bTransport);
	CHECK(other.skippedTransports.size() == 1 && other.skippedTransports[0] == "genonline_eos");

	// The anticheat plugin keeps its transport when the list names it after another plugin.
	Selection later = Select("genonline_eos", { "other", "genonline_eos" });
	CHECK(later.pluginName == "genonline_eos");
	CHECK(later.bAnticheat);
	CHECK(later.bTransport);
	CHECK(later.skippedTransports.size() == 1 && later.skippedTransports[0] == "other");
}

static void TestEnabledCapabilities()
{
	// The anticheat plugin with its transport gets every role.
	CHECK(GetEnabledCapabilities(Select("genonline_eos", { "genonline_eos" }))
		== (GENONLINE_PLUGIN_CAP_LINKED_ACCOUNT | GENONLINE_PLUGIN_CAP_ANTICHEAT | GENONLINE_PLUGIN_CAP_TRANSPORT));

	// An anticheat plugin outside the transport list runs no transport.
	CHECK(GetEnabledCapabilities(Select("other_anticheat", { "genonline_eos" }))
		== (GENONLINE_PLUGIN_CAP_LINKED_ACCOUNT | GENONLINE_PLUGIN_CAP_ANTICHEAT));

	// A transport only plugin runs no anticheat.
	CHECK(GetEnabledCapabilities(Select("", { "genonline_eos" }))
		== (GENONLINE_PLUGIN_CAP_LINKED_ACCOUNT | GENONLINE_PLUGIN_CAP_TRANSPORT));

	// No plugin gets no role.
	CHECK(GetEnabledCapabilities(Select("", {})) == 0);
}

static void TestSessionAndPeerCalls()
{
	// A transport only plugin gets the session and peer calls next to GenOnline Anti-Cheat so that its transport knows its peers.
	CHECK(DoesPluginTrackPeersOnly(Select("", { "genonline_eos" })));

	// The anticheat plugin gets them as the anticheat.
	CHECK(!DoesPluginTrackPeersOnly(Select("genonline_eos", { "genonline_eos" })));

	// Without a plugin nothing is forwarded.
	CHECK(!DoesPluginTrackPeersOnly(Select("", {})));
}

static void TestLoadFailure()
{
	// A failed anticheat plugin blocks online play.
	CHECK(DoesLoadFailureBlockOnline(Select("genonline_eos", { "genonline_eos" })));
	CHECK(DoesLoadFailureBlockOnline(Select("other_anticheat", {})));

	// A failed transport only plugin falls back to the standard connection.
	CHECK(!DoesLoadFailureBlockOnline(Select("", { "genonline_eos" })));

	// Without a plugin nothing can fail.
	CHECK(!DoesLoadFailureBlockOnline(Select("", {})));
}

int main()
{
	TestNoExternalPlugin();
	TestTransportOnly();
	TestExternalAnticheat();
	TestEnabledCapabilities();
	TestSessionAndPeerCalls();
	TestLoadFailure();
	if (s_failures != 0)
	{
		std::printf("%d check(s) failed\n", s_failures);
		return 1;
	}
	std::printf("All plugin role checks passed\n");
	return 0;
}
