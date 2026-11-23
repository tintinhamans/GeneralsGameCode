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

#include "GameClient/GenOnlineMenus.h"

#include "Common/NameKeyGenerator.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameNetwork/GenOnline/ServiceConnectionPolicy.h"
#include "GameNetwork/GenOnline/ProxyLogin.h"
#include <memory>

struct NetworkControls
{
	GameWindow *tabOnline = nullptr;
	GameWindow *tabLan = nullptr;
	GameWindow *panelOnline = nullptr;
	GameWindow *panelLan = nullptr;
	GameWindow *proxyModeLabel = nullptr;
	GameWindow *proxyMode = nullptr;
	GameWindow *proxyAddressLabel = nullptr;
	GameWindow *proxyAddress = nullptr;
	GameWindow *serverLabel = nullptr;
	GameWindow *server = nullptr;
	GameWindow *serverAddressLabel = nullptr;
	GameWindow *serverAddress = nullptr;
	GameWindow *lanLabel = nullptr;
};

static GameWindow *networkControl(const char *name)
{
	return TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey(name));
}

static NetworkControls getNetworkControls()
{
	NetworkControls c;
	c.tabOnline = networkControl("OptionsMenu.wnd:ButtonTabOnline");
	c.tabLan = networkControl("OptionsMenu.wnd:ButtonTabLan");
	c.panelOnline = networkControl("OptionsMenu.wnd:PanelOnline");
	c.panelLan = networkControl("OptionsMenu.wnd:PanelLan");
	c.proxyModeLabel = networkControl("OptionsMenu.wnd:StaticTextProxyMode");
	c.proxyMode = networkControl("OptionsMenu.wnd:ComboBoxProxyMode");
	c.proxyAddressLabel = networkControl("OptionsMenu.wnd:StaticTextProxyAddress");
	c.proxyAddress = networkControl("OptionsMenu.wnd:TextEntryProxyAddress");
	c.serverLabel = networkControl("OptionsMenu.wnd:StaticTextServer");
	c.server = networkControl("OptionsMenu.wnd:ComboBoxServer");
	c.serverAddressLabel = networkControl("OptionsMenu.wnd:StaticTextServerAddress");
	c.serverAddress = networkControl("OptionsMenu.wnd:TextEntryServerAddress");
	c.lanLabel = networkControl("OptionsMenu.wnd:StaticTextLANIpAddresses");
	return c;
}

static bool networkTestPassed = false;
static bool networkDiscard = false;
static bool networkLocked = false;

static UnicodeString networkWide(const std::string &text)
{
	UnicodeString result;
	result.translate(text.c_str());
	return result;
}

static std::string networkNarrow(const UnicodeString &text)
{
	AsciiString ascii;
	ascii.translate(text);
	return ascii.str();
}

static std::string networkTrimmed(std::string text)
{
	text.erase(0, text.find_first_not_of(" \t"));
	text.erase(text.find_last_not_of(" \t") + 1);
	return text;
}

static void networkSetText(GameWindow *label, const wchar_t *text)
{
	if (label != nullptr)
	{
		GadgetStaticTextSetText(label, UnicodeString(text));
	}
}

static void networkSetTooltip(GameWindow *window, const wchar_t *text)
{
	if (window != nullptr)
	{
		window->winSetTooltip(UnicodeString(text));
	}
}

static void networkFillCombo(GameWindow *combo, std::initializer_list<const wchar_t *> entries, Int selected)
{
	if (combo == nullptr)
	{
		return;
	}
	const Color white = GameMakeColor(255, 255, 255, 255);
	GadgetComboBoxReset(combo);
	for (const wchar_t *entry : entries)
	{
		GadgetComboBoxAddEntry(combo, UnicodeString(entry), white);
	}
	GadgetComboBoxSetSelectedPos(combo, selected);
}

static void networkShowTab(bool online)
{
	const NetworkControls c = getNetworkControls();
	if (c.panelOnline != nullptr)
	{
		c.panelOnline->winHide(!online);
	}
	if (c.panelLan != nullptr)
	{
		c.panelLan->winHide(online);
	}
	if (c.tabOnline != nullptr)
	{
		GadgetCheckLikeButtonSetVisualCheck(c.tabOnline, online);
	}
	if (c.tabLan != nullptr)
	{
		GadgetCheckLikeButtonSetVisualCheck(c.tabLan, !online);
	}
}

// The address entries show what the choice resolves to while they cannot be edited, what was typed for Custom is kept meanwhile.
static std::string networkProxyCustomText;
static std::string networkServerCustomText;
static bool networkProxyEntryCustom = false;
static bool networkServerEntryCustom = false;
static std::shared_ptr<ServiceConnection::ProxyResolution> networkSystemProxy;
static std::string networkSystemProxyText;

static const char *networkEnvironment()
{
	return GenOnlineServicesManager::EnvironmentName();
}

// the host the Automatic server choice uses, the route that worked last first
static std::string networkServerHost()
{
	ServiceConnectionSettings settings;
	const auto routes = ServiceConnection::BootstrapRoutes(networkEnvironment(), settings, GenOnlineServicesManager::Settings.Network_GetLastRoute());
	if (routes.empty())
	{
		return std::string();
	}
	std::string host = routes.front();
	host.erase(0, host.find("://") == std::string::npos ? 0 : host.find("://") + 3);
	return host.substr(0, host.find('/'));
}

// the Windows proxy policy is looked up on a worker thread, networkPoll picks the result up
static void networkStartSystemLookup()
{
	ServiceConnectionSettings system;
	system.proxyMode = "automatic";
	ServiceConnectionSettings selected = GenOnlineServicesManager::Settings.Network_GetConnectionSettings();
	const auto routes = ServiceConnection::BootstrapRoutes(networkEnvironment(), selected, GenOnlineServicesManager::Settings.Network_GetLastRoute());
	networkSystemProxyText.clear();
	networkSystemProxy = ServiceConnection::StartProxyResolution(system, routes.empty() ? std::string("https://api.generals.online/") : routes.front());
}

static void networkRefresh()
{
	const NetworkControls c = getNetworkControls();
	Int mode = 0;
	if (c.proxyMode != nullptr)
	{
		GadgetComboBoxGetSelectedPos(c.proxyMode, &mode);
	}
	if (c.proxyAddress != nullptr)
	{
		if (networkProxyEntryCustom)
		{
			networkProxyCustomText = networkNarrow(GadgetTextEntryGetText(c.proxyAddress));
		}
		networkProxyEntryCustom = mode == 2;
		const std::string shown = mode == 2 ? networkProxyCustomText : mode == 1 ? std::string("Direct") : networkSystemProxyText.empty() ? std::string("Checking...") : networkSystemProxyText;
		GadgetTextEntrySetText(c.proxyAddress, networkWide(shown));
		c.proxyAddress->winEnable(!networkLocked && mode == 2);
	}
	Int server = 0;
	if (c.server != nullptr)
	{
		GadgetComboBoxGetSelectedPos(c.server, &server);
	}
	if (c.serverAddress != nullptr)
	{
		if (networkServerEntryCustom)
		{
			networkServerCustomText = networkNarrow(GadgetTextEntryGetText(c.serverAddress));
		}
		networkServerEntryCustom = server == 1;
		GadgetTextEntrySetText(c.serverAddress, networkWide(server == 1 ? networkServerCustomText : networkServerHost()));
		c.serverAddress->winEnable(!networkLocked && server == 1);
	}
}

static void networkPoll()
{
	if (networkSystemProxy == nullptr || !networkSystemProxy->ready.load(std::memory_order_acquire))
	{
		return;
	}
	networkSystemProxyText = networkSystemProxy->result.error.empty() && !networkSystemProxy->result.source.empty() ? networkSystemProxy->result.source : std::string("Unavailable");
	networkSystemProxy.reset();
	networkRefresh();
}

// The options layout is not on the shell stack and has no update of its own, so the poll runs from the main thread tasks until the lookup is in
static void networkPollLater()
{
	ServiceConnection::RunOnMainThread([]()
	{
		networkPoll();
		if (networkSystemProxy != nullptr)
		{
			networkPollLater();
		}
	});
}

static void networkShow(const ServiceConnectionSettings &settings)
{
	const NetworkControls c = getNetworkControls();
	const Int proxy = settings.proxyMode == "disabled" ? 1 : settings.proxyMode == "custom" ? 2 : 0;
	networkFillCombo(c.proxyMode, { L"Automatic", L"Disabled", L"Custom" }, proxy);
	networkProxyCustomText = settings.proxyURL;
	if (networkProxyCustomText.compare(0, 7, "http://") == 0)
	{
		networkProxyCustomText.erase(0, 7);
	}
	const Int server = settings.serverMode == "custom" ? 1 : 0;
	networkFillCombo(c.server, { L"Automatic", L"Custom" }, server);
	networkServerCustomText = settings.serverAddress;
	if (networkServerCustomText.compare(0, 8, "https://") == 0)
	{
		networkServerCustomText.erase(0, 8);
	}
	networkProxyEntryCustom = false;
	networkServerEntryCustom = false;
	networkRefresh();
}

static void networkInit()
{
	networkTestPassed = false;
	networkDiscard = false;
	const NetworkControls c = getNetworkControls();
	if (c.tabOnline != nullptr)
	{
		GadgetButtonSetText(c.tabOnline, UnicodeString(L"Online"));
		c.tabOnline->winSetTooltip(UnicodeString(L"Settings for playing on Generals Online."));
	}
	if (c.tabLan != nullptr)
	{
		GadgetButtonSetText(c.tabLan, UnicodeString(L"LAN"));
		c.tabLan->winSetTooltip(UnicodeString(L"Settings for LAN games."));
	}
	networkSetText(c.proxyModeLabel, L"Proxy");
	networkSetText(c.proxyAddressLabel, L"Proxy address");
	networkSetText(c.serverLabel, L"Server");
	networkSetText(c.serverAddressLabel, L"Server address");
	networkSetText(c.lanLabel, L"LAN IP");
	networkSetTooltip(c.proxyMode, L"Automatic follows the Windows proxy settings. Disabled connects directly. Custom uses the proxy address.");
	networkSetTooltip(c.proxyAddress, L"Proxy as host:port, or a full http://, https:// or socks5h:// address. Used with Custom.");
	networkSetTooltip(c.server, L"Automatic uses the Generals Online servers. Custom uses your own server. Applies at the next login.");
	networkSetTooltip(c.serverAddress, L"Your own server as host:port. Only https is allowed, except http://localhost, http://127.0.0.1 or http://[::1] for local development. Used with Custom.");
	networkSetTooltip(networkControl("OptionsMenu.wnd:ComboBoxIP"), L"The network adapter used for LAN games.");
	networkStartSystemLookup();
	networkPollLater();
	networkShow(GenOnlineServicesManager::Settings.Network_GetConnectionSettings());
	networkShowTab(true);
}

// The connection settings cannot change while a session is open or in a game
static void networkLock()
{
	networkLocked = true;
	const NetworkControls c = getNetworkControls();
	for (GameWindow *window : { c.proxyMode, c.proxyAddress, c.server, c.serverAddress })
	{
		if (window != nullptr)
		{
			window->winEnable(FALSE);
		}
	}
}

static void networkDefaults()
{
	networkShow(ServiceConnectionSettings());
	GameWindow *comboBoxLANIP = networkControl("OptionsMenu.wnd:ComboBoxIP");
	if (comboBoxLANIP != nullptr)
	{
		GadgetComboBoxSetSelectedPos(comboBoxLANIP, 0);
		GadgetComboBoxSetText(comboBoxLANIP, L"***.***.***.***");
	}
}

static bool networkRead(ServiceConnectionSettings &settings, std::string &error)
{
	const NetworkControls c = getNetworkControls();
	Int mode = 0;
	if (c.proxyMode != nullptr)
	{
		GadgetComboBoxGetSelectedPos(c.proxyMode, &mode);
		settings.proxyMode = mode == 1 ? "disabled" : mode == 2 ? "custom" : "automatic";
	}
	if (c.proxyAddress != nullptr)
	{
		std::string address = networkTrimmed(networkProxyEntryCustom ? networkNarrow(GadgetTextEntryGetText(c.proxyAddress)) : networkProxyCustomText);
		if (!address.empty() && address.find("://") == std::string::npos)
		{
			address = "http://" + address;
		}
		settings.proxyURL = address;
		if (settings.proxyMode == "custom" && !ServiceConnection::ValidateProxyURL(address, error))
		{
			return false;
		}
		if (settings.proxyMode != "custom" && !ServiceConnection::ValidateProxyURL(address, error))
		{
			settings.proxyURL.clear();
			error.clear();
		}
	}
	Int server = 0;
	if (c.server != nullptr)
	{
		GadgetComboBoxGetSelectedPos(c.server, &server);
		settings.serverMode = server == 1 ? "custom" : "automatic";
	}
	if (c.serverAddress != nullptr)
	{
		settings.serverAddress = networkTrimmed(networkServerEntryCustom ? networkNarrow(GadgetTextEntryGetText(c.serverAddress)) : networkServerCustomText);
		if (settings.serverMode == "custom")
		{
			return ServiceConnection::NormalizeCustomEndpoint(settings.serverAddress, settings.serverAddress, error);
		}
		settings.serverAddress.clear();
	}
	return true;
}

static bool networkChanged(const ServiceConnectionSettings &saved, const ServiceConnectionSettings &edited)
{
	if (saved.proxyMode != edited.proxyMode || saved.serverMode != edited.serverMode)
	{
		return true;
	}
	if (edited.proxyMode == "custom" && ServiceConnection::CanonicalProxyKey(saved.proxyURL) != ServiceConnection::CanonicalProxyKey(edited.proxyURL))
	{
		return true;
	}
	return edited.serverMode == "custom" && saved.serverAddress != edited.serverAddress;
}

static void pressAccept()
{
	GameWindow *accept = networkControl("OptionsMenu.wnd:ButtonAccept");
	if (accept != nullptr)
	{
		TheWindowManager->winSendSystemMsg(accept->winGetParent(), GBM_SELECTED, (WindowMsgData)accept, accept->winGetWindowId());
	}
}

static bool networkAccept()
{
	if (networkDiscard)
	{
		networkDiscard = false;
		return true;
	}
	const NetworkControls c = getNetworkControls();
	if (networkLocked || (c.proxyMode == nullptr && c.server == nullptr))
	{
		return true;
	}
	const ServiceConnectionSettings saved = GenOnlineServicesManager::Settings.Network_GetConnectionSettings();
	ServiceConnectionSettings edited = saved;
	std::string error;
	if (!networkRead(edited, error))
	{
		MessageBoxOk(UnicodeString(L"Connection settings"), networkWide(error), nullptr);
		return false;
	}
	if (networkChanged(saved, edited) && !networkTestPassed)
	{
		ServiceConnection::ShowAcceptTest(edited, [](ServiceConnection::AcceptChoice choice)
		{
			if (choice == ServiceConnection::AcceptChoice::Save)
			{
				networkTestPassed = true;
			}
			else if (choice == ServiceConnection::AcceptChoice::Discard)
			{
				networkDiscard = true;
			}
			if (choice != ServiceConnection::AcceptChoice::KeepEditing)
			{
				pressAccept();
			}
		});
		return false;
	}
	networkTestPassed = false;
	if (!GenOnlineServicesManager::Settings.Network_SaveConnectionSettings(edited, error))
	{
		MessageBoxOk(UnicodeString(L"Connection settings"), networkWide(error), nullptr);
		return false;
	}
	return true;
}

static bool isNetworkControl(GameWindow *control, const char *name)
{
	return control->winGetWindowId() == TheNameKeyGenerator->nameToKey(name);
}

void GenOnlineOptionsMenuInit( Bool locked )
{
	networkInit();
	if (locked)
	{
		networkLock();
	}
}

Bool GenOnlineOptionsMenuComboSelected( GameWindow *control )
{
	if (isNetworkControl(control, "OptionsMenu.wnd:ComboBoxProxyMode") || isNetworkControl(control, "OptionsMenu.wnd:ComboBoxServer"))
	{
		networkRefresh();
		return TRUE;
	}
	return FALSE;
}

Bool GenOnlineOptionsMenuButtonSelected( GameWindow *control )
{
	if (isNetworkControl(control, "OptionsMenu.wnd:ButtonTabOnline"))
	{
		networkShowTab(true);
		return TRUE;
	}
	if (isNetworkControl(control, "OptionsMenu.wnd:ButtonTabLan"))
	{
		networkShowTab(false);
		return TRUE;
	}
	return FALSE;
}

Bool GenOnlineOptionsMenuAccept()
{
	return networkAccept();
}

void GenOnlineOptionsMenuDefaults()
{
	networkDefaults();
}
