/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

// Ronin @feature 29/10/2026 DX9: Options. Accept applies in OptionsMenu.cpp's saveOptions() order - the resolution last - and
// the renderer's settings live, as the panel's commands do; after tintinhamans' fork (arctic/go/feat/rmlui).
#include "W3DDevice/GameClient/RmlUi/RmlOptionsScreen.h"

#include "Common/GlobalData.h"
#include "Common/OptionPreferences.h"
#include "GameClient/Display.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "W3DDevice/GameClient/R3DLocalLights.h"
#include "W3DDevice/GameClient/R3DShadowMap.h"
#include "W3DDevice/GameClient/R3DTaa.h"
#include "W3DDevice/GameClient/R3DWaterSeaTuning.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/RmlUi/RmlUiManager.h"
#include "WW3D2/dx8wrapper.h"

// Ronin @feature 21/11/2026 DX9: Generals Online's connection settings and its Accept test
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameNetwork/GenOnline/ProxyLogin.h"
#include "GameNetwork/Online/OnlineProviderFactory.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/FileInterface.h>

#include <stdio.h>

extern void DoResolutionDialog();	// MainMenu.cpp: keep the new resolution, or go back

using namespace OptionsValues;

namespace
{
	const char *const kDocument = "Data/UI/Options.rml";
	const char *const kQualityNames[] = { "Off", "Normal", "High", "Ultra" };	// DX9ShadowQuality / DX9SSAOQuality

	const char *yesNo(bool b) { return b ? "yes" : "no"; }
	int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

	bool sameDetail(const DetailPresetValues &a, const DetailPresetValues &b)
	{
		return a.textureResolutionSliderPos == b.textureResolutionSliderPos && a.particleCap == b.particleCap
			&& a.shadow3D == b.shadow3D && a.shadow2D == b.shadow2D && a.cloudShadows == b.cloudShadows
			&& a.groundLighting == b.groundLighting && a.smoothWater == b.smoothWater && a.extraAnimations == b.extraAnimations
			&& a.noDynamicLod == b.noDynamicLod && a.heatEffects == b.heatEffects && a.buildingOcclusion == b.buildingOcclusion
			&& a.props == b.props;
	}

	// An overlay kept shown keeps its size; one turned on again gets the default, 8.
	int overlaySize(bool shown, int loaded)
	{
		return shown ? (loaded > 0 ? loaded : 8) : 0;
	}

	// Ronin @feature 21/11/2026 DX9: Generals Online's connection rows, a fragment its build loads into the Network page.
	const char *const kOnlineRows = "Data/UI/OptionsOnline.rml";

	// Ronin @feature 21/11/2026 DX9: GenOnlineOptionsMenu.cpp's networkTrimmed() and networkWide().
	std::string trimmed(std::string text)
	{
		text.erase(0, text.find_first_not_of(" \t"));
		text.erase(text.find_last_not_of(" \t") + 1);
		return text;
	}

	UnicodeString wideText(const std::string &text)
	{
		UnicodeString result;
		result.translate(text.c_str());
		return result;
	}

	// Ronin @feature 21/11/2026 DX9: networkChanged(): the proxy or the server differs from the saved one; an address counts
	// only for its Custom choice.
	bool onlineChanged(const ServiceConnectionSettings &saved, const ServiceConnectionSettings &edited)
	{
		if (saved.proxyMode != edited.proxyMode || saved.serverMode != edited.serverMode)
			return true;
		if (edited.proxyMode == "custom" && ServiceConnection::CanonicalProxyKey(saved.proxyURL) != ServiceConnection::CanonicalProxyKey(edited.proxyURL))
			return true;
		return edited.serverMode == "custom" && saved.serverAddress != edited.serverAddress;
	}

	// Ronin @feature 21/11/2026 DX9: networkServerHost(): the host Automatic's server uses.
	std::string automaticServerHost()
	{
		const std::vector<std::string> routes = ServiceConnection::BootstrapRoutes(GenOnlineServicesManager::EnvironmentName(),
			ServiceConnectionSettings());
		if (routes.empty())
			return std::string();
		std::string host = routes.front();
		const size_t scheme = host.find("://");
		if (scheme != std::string::npos)
			host.erase(0, scheme + 3);
		return host.substr(0, host.find('/'));
	}
}

RmlOptionsScreen &RmlOptionsScreen::instance()
{
	static RmlOptionsScreen s_screen;
	return s_screen;
}

void RmlOptionsScreen::load()
{
	if (m_document || !TheRmlUiManager || !TheRmlUiManager->getContext())
		return;
	if (!m_model)
	{
		Rml::DataModelConstructor c = TheRmlUiManager->getContext()->CreateDataModel("options");
		if (!c)
			return;
		// His document's names (Options.rml); the rows he has not got bind ours.
		m_hq.bind(c);
		c.Bind("tab", &m.tab);
		c.Bind("graphics_view", &m.graphicsView);
		c.Bind("taa_mip_bias", &m.taaMipBias);
		c.Bind("text_language", &m.textLanguage);
		c.Bind("interface_width", &m.interfaceWidth);
		c.Bind("restricted", &m.restricted);
		c.Bind("detail_controls_disabled", &m.detailLocked);
		c.Bind("resolution_index", &m.resolution);
		c.Bind("brightness", &m.brightness);
		c.Bind("detail_level", &m.detailLevel);
		c.Bind("texture_resolution", &m.textureResolution);
		c.Bind("particle_cap", &m.particleCap);
		c.Bind("shadow_3d", &m.shadow3D);
		c.Bind("shadow_2d", &m.shadow2D);
		c.Bind("cloud_shadows", &m.cloudShadows);
		c.Bind("ground_lighting", &m.groundLighting);
		c.Bind("smooth_water", &m.smoothWater);
		c.Bind("extra_animations", &m.extraAnimations);
		c.Bind("no_dynamic_lod", &m.noDynamicLod);
		c.Bind("heat_effects", &m.heatEffects);
		c.Bind("building_occlusion", &m.buildingOcclusion);
		c.Bind("props", &m.props);
		c.Bind("anti_aliasing", &m.antiAliasing);
		c.Bind("taa_sharpness", &m.taaSharpness);
		c.Bind("shadow_quality", &m.shadowQuality);
		c.Bind("ssao_quality", &m.ssaoQuality);
		c.Bind("lighting", &m.lighting);
		c.Bind("light_effects", &m.lightEffects);
		c.Bind("hdr", &m.hdr);
		c.Bind("water_reflections", &m.waterReflections);
		c.Bind("improved_water", &m.improvedWater);
		c.Bind("shroud_interpolation", &m.shroudInterpolation);
		c.Bind("splat", &m.splat);
		c.Bind("pom", &m.pom);
		c.Bind("modern_ui", &m.modernUI);
		c.Bind("music_volume", &m.music);
		c.Bind("sfx_volume", &m.sfx);
		c.Bind("voice_volume", &m.voice);
		c.Bind("money_volume", &m.money);
		c.Bind("scroll_speed", &m.scroll);
		c.Bind("alternate_mouse", &m.alternateMouse);
		c.Bind("right_mouse_scroll", &m.rightMouseScroll);
		c.Bind("retaliation", &m.retaliation);
		c.Bind("double_click_attack_move", &m.doubleClickAttackMove);
		c.Bind("draw_anchor", &m.drawAnchor);
		c.Bind("move_anchor", &m.moveAnchor);
		c.Bind("edge_windowed", &m.edgeWindowed);
		c.Bind("edge_fullscreen", &m.edgeFullscreen);
		c.Bind("capture_windowed_game", &m.captureWindowedGame);
		c.Bind("capture_windowed_menu", &m.captureWindowedMenu);
		c.Bind("capture_fullscreen_game", &m.captureFullscreenGame);
		c.Bind("capture_fullscreen_menu", &m.captureFullscreenMenu);
		c.Bind("language_filter", &m.languageFilter);
		c.Bind("money_per_minute", &m.moneyPerMinute);
		c.Bind("player_observer", &m.playerObserver);
		c.Bind("archive_replays", &m.archiveReplays);
		c.Bind("save_camera", &m.saveCamera);
		c.Bind("use_camera", &m.useCamera);
		c.Bind("show_fps", &m.showFps);
		c.Bind("show_clock", &m.showClock);
		c.Bind("show_game_time", &m.showGameTime);
		c.Bind("show_latency", &m.showLatency);
		c.Bind("show_player_list", &m.showPlayerList);
		c.Bind("lan_ip_index", &m.lanIP);
		c.Bind("online_ip_index", &m.onlineIP);
		c.Bind("firewall_port_override", &m.firewallPort);
		c.Bind("proxy_mode", &m.proxyMode);			// Ronin @feature 21/11/2026 DX9: Generals Online's rows (OptionsOnline.rml)
		c.Bind("proxy_address", &m.proxyAddress);
		c.Bind("proxy_system", &m.proxySystem);
		c.Bind("server_mode", &m.serverMode);
		c.Bind("server_address", &m.serverAddress);
		c.Bind("server_host", &m.serverHost);
		struct Event { const char *name, *what, *arg; };
		static const Event events[] =
		{
			{ "select_video", "tab", "video" }, { "select_graphics", "tab", "graphics" },
			{ "graphics_legacy", "view", "legacy" }, { "graphics_enhanced", "view", "enhanced" },
			{ "select_audio", "tab", "audio" }, { "select_controls", "tab", "controls" }, { "select_interface", "tab", "interface" },
			{ "select_network", "tab", "network" }, { "accept", "accept", "" }, { "cancel", "cancel", "" },
			{ "defaults", "defaults", "" }, { "firewall_refresh", "firewall", "" },
			// the preset and its switches: update() reads the values (RmlUi fires change for the model's own writes too)
			{ "detail_level_changed", "", "" }, { "detail_control_changed", "", "" },
		};
		for (const Event &e : events)
		{
			const Rml::String what = e.what, arg = e.arg;
			c.BindEventCallback(e.name, [this, what, arg](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) { act(what, arg); });
		}
		m_model = c.GetModelHandle();
	}
	m_document = TheRmlUiManager->loadScreen(kDocument);
	loadOnlineRows();	// Ronin @feature 21/11/2026 DX9: Generals Online's rows, in its build
}

//-------------------------------------------------------------------------------------------------
void RmlOptionsScreen::showDetail(const DetailPresetValues &v)
{
	m.textureResolution = v.textureResolutionSliderPos;
	m.particleCap = v.particleCap;
	m.shadow3D = v.shadow3D;
	m.shadow2D = v.shadow2D;
	m.cloudShadows = v.cloudShadows;
	m.groundLighting = v.groundLighting;
	m.smoothWater = v.smoothWater;
	m.extraAnimations = v.extraAnimations;
	m.noDynamicLod = v.noDynamicLod;
	m.heatEffects = v.heatEffects;
	m.buildingOcclusion = v.buildingOcclusion;
	m.props = v.props;
	m_detailShown = shownDetail();
}

DetailPresetValues RmlOptionsScreen::shownDetail() const
{
	DetailPresetValues v;
	v.textureResolutionSliderPos = m.textureResolution;
	v.particleCap = m.particleCap;
	v.shadow3D = m.shadow3D;
	v.shadow2D = m.shadow2D;
	v.cloudShadows = m.cloudShadows;
	v.groundLighting = m.groundLighting;
	v.smoothWater = m.smoothWater;
	v.extraAnimations = m.extraAnimations;
	v.noDynamicLod = m.noDynamicLod;
	v.heatEffects = m.heatEffects;
	v.buildingOcclusion = m.buildingOcclusion;
	v.props = m.props;
	return v;
}

// OptionsMenuInit(): the game's settings now, the running renderer's for the DX9 page.
void RmlOptionsScreen::readCurrent()
{
	OptionPreferences &pref = *m_pref;
	m.restricted = IsOptionsRestrictedContext() != FALSE;

	m.resolution = GetCurrentDisplayModeIndex();
	m.brightness = GetCurrentBrightnessPercent(pref);

	m.detailLevel = GetCurrentDetailLevel();
	m_detailLevelShown = m.detailLevel;
	showDetail(GetCurrentGraphicsValues());
	m.detailLocked = m.restricted && m.detailLevel != GetDetailCustomLevel();

	const int msaa = DX8Wrapper::Get_Anti_Aliasing_Level();
	m.antiAliasing = msaa >= 8 ? 4 : (msaa >= 4 ? 3 : (msaa >= 2 ? 2 : (R3DTaa::isEnabled() ? 1 : 0)));
	m.taaSharpness = (int)(R3DTaa::getSharpen() * 100.0f + 0.5f);
	m.taaMipBias = clampInt((int)(R3DTaa::getMipBias() * 10.0f - 0.5f), -20, 0);	// rounds the negative tenths
	m.shadowQuality = clampInt(R3DShadowMap::getQuality(), 0, 3);
	m.ssaoQuality = clampInt(TheGlobalData->m_ssaoQuality, 0, 3);
	m.lighting = R3DLocalLights::isEnabled() != FALSE;
	m.lightEffects = R3DLocalLights::isEffects() != FALSE;
	m.hdr = R3DTaa::getFloatScene() != FALSE;
	m.waterReflections = TheGlobalData->m_waterReflections != FALSE;
	m.shroudInterpolation = TheGlobalData->m_shroudInterpolation != FALSE;
	m.splat = true;		// always: the 2003 terrain path does not draw right in this build
	m.improvedWater = TheGlobalData->m_waterType == 2;	// switches on Accept (WaterRenderObjClass::setWaterType)
	m.pom = TheGlobalData->m_useTerrainPOM != FALSE;
	OptionPreferences::const_iterator modern = pref.find("ModernUI");
	m.modernUI = modern != pref.end() && (stricmp(modern->second.str(), "yes") == 0 || atoi(modern->second.str()) != 0);

	m.music = GetCurrentMusicVolumePercent(pref);
	m.sfx = GetCurrentSFXVolumePercent(pref);
	m.voice = GetCurrentVoiceVolumePercent(pref);
	m.money = GetCurrentMoneyVolumePercent(pref);

	const ControlValues cv = GetCurrentControls(pref);
	m_scrollLoaded = cv.scrollSpeed;
	m.scroll = cv.scrollSpeed;
	m.alternateMouse = cv.alternateMouse != FALSE;
	m.rightMouseScroll = cv.rightMouseScrollWithAlternate != FALSE;
	m.retaliation = cv.retaliation != FALSE;
	m.doubleClickAttackMove = cv.doubleClickAttackMove != FALSE;
	m.drawAnchor = cv.drawScrollAnchor != FALSE;
	m.moveAnchor = cv.moveScrollAnchor != FALSE;
	m.edgeWindowed = cv.edgeScrollWindowed != FALSE;
	m.edgeFullscreen = cv.edgeScrollFullscreen != FALSE;
	m.captureWindowedGame = cv.captureWindowedGame != FALSE;
	m.captureWindowedMenu = cv.captureWindowedMenu != FALSE;
	m.captureFullscreenGame = cv.captureFullscreenGame != FALSE;
	m.captureFullscreenMenu = cv.captureFullscreenMenu != FALSE;

	m_interfaceLoaded = GetCurrentInterface(pref);
	const InterfaceValues &iv = m_interfaceLoaded;
	m.languageFilter = iv.languageFilter != FALSE;
	m.moneyPerMinute = iv.showMoneyPerMinute != FALSE;
	m.playerObserver = iv.playerObserver != FALSE;
	m.archiveReplays = iv.archiveReplays != FALSE;
	m.saveCamera = iv.saveCameraInReplays != FALSE;
	m.useCamera = iv.useCameraInReplays != FALSE;
	m.showFps = iv.renderFpsFontSize > 0;
	m.showClock = iv.systemTimeFontSize > 0;
	m.showGameTime = iv.gameTimeFontSize > 0;
	m.showLatency = iv.networkLatencyFontSize > 0;
	m.showPlayerList = iv.playerInfoListFontSize > 0;

	m.lanIP = GetCurrentLANIPIndex(pref);
	m.onlineIP = GetCurrentOnlineIPIndex(pref);
	const Int port = GetCurrentFirewallPortOverride();
	char portText[16] = "";
	if (port != 0)
		_snprintf_s(portText, sizeof(portText), _TRUNCATE, "%d", port);
	m.firewallPort = portText;

	// Ronin @feature 21/11/2026 DX9: networkInit(): GO's connection as saved; Automatic's proxy is looked up on a worker
	m_onlineTesting = m_onlineTested = m_onlineDiscard = false;
	if (m_onlineRows)
	{
		showOnline(GenOnlineServicesManager::Settings.Network_GetConnectionSettings());
		m.serverHost = automaticServerHost();
		m.proxySystem = "Checking...";
		startOnlineLookup();
	}
}

// The lists the game knows only now: the display's modes (the running size added when it is not one, as the .wnd does) and
// the machine's addresses. Through the select's own calls: inner RML would add to the options already in its list.
namespace
{
	Rml::ElementFormControlSelect *emptySelect(Rml::ElementDocument *doc, const char *id)
	{
		Rml::ElementFormControlSelect *select = rmlui_dynamic_cast<Rml::ElementFormControlSelect *>(doc->GetElementById(id));
		if (select)
			select->RemoveAll();
		return select;
	}
}

// Ronin @bugfix 30/10/2026 DX9: each list selects the model's entry as it is filled. Emptied, a list of a hidden document
// falls to its first entry at the next update and writes it into the model: Options reopened at Low and 800 x 600.
void RmlOptionsScreen::fillChoices()
{
	const int wantDetail = m.detailLevel, wantResolution = m.resolution, wantLan = m.lanIP, wantOnline = m.onlineIP;

	// His document's lists: the preset (the .wnd combo's labels; "Very High" has no string in the shipped table) and the
	// language, Generals Online's, only the installed one until that merge.
	if (Rml::ElementFormControlSelect *detail = emptySelect(m_document, "select_detail"))
	{
		detail->Add("<gametext key=\"GUI:Low\"/>", "0");
		detail->Add("<gametext key=\"GUI:Medium\"/>", "1");
		detail->Add("<gametext key=\"GUI:High\"/>", "2");
		detail->Add("Very High", "3");
		detail->Add("<gametext key=\"GUI:Custom\"/>", "4");
		detail->SetSelection(wantDetail);	// the values are the positions
	}
	if (Rml::ElementFormControlSelect *language = emptySelect(m_document, "select_language"))
	{
		language->Add("Installed language", "0");
		language->SetSelection(0);
	}

	char text[64], value[16];
	if (Rml::ElementFormControlSelect *res = emptySelect(m_document, "select_resolution"))
	{
		const Int modes = GetDisplayModeCount();
		for (Int i = 0; i < modes; ++i)
		{
			Int w, h;
			GetDisplayMode(i, w, h);
			_snprintf_s(text, sizeof(text), _TRUNCATE, "%d x %d", (int)w, (int)h);
			_snprintf_s(value, sizeof(value), _TRUNCATE, "%d", (int)i);
			res->Add(text, value);
		}
		if (wantResolution < 0)	// the running size, not in the list: added last
		{
			_snprintf_s(text, sizeof(text), _TRUNCATE, "%u x %u", TheDisplay->getWidth(), TheDisplay->getHeight());
			res->Add(text, "-1");
		}
		res->SetSelection(wantResolution < 0 ? (int)modes : wantResolution);
	}
	const char *ipLists[] = { "select_lan_ip", "select_online_ip" };
	for (const char *id : ipLists)
	{
		Rml::ElementFormControlSelect *list = emptySelect(m_document, id);
		if (!list)
			continue;
		for (Int i = 0; i < GetIPChoiceCount(); ++i)
		{
			_snprintf_s(value, sizeof(value), _TRUNCATE, "%d", (int)i);
			list->Add(GetIPChoiceLabel(i).str(), value);
		}
		list->SetSelection(strcmp(id, "select_lan_ip") == 0 ? wantLan : wantOnline);
	}

	// The lists wrote their passing values into the model on the way: the settings again.
	m.detailLevel = wantDetail;
	m.resolution = wantResolution;
	m.lanIP = wantLan;
	m.onlineIP = wantOnline;
}

void RmlOptionsScreen::open()
{
	load();
	if (!m_document)
		return;
	if (!isVisible())
	{
		delete m_pref;
		m_pref = new OptionPreferences;
		readCurrent();
		fillChoices();
		m_hq.refresh(m_model, true);
		if (m_model)
			m_model.DirtyAllVariables();
		SignalUIInteraction(SHELL_SCRIPT_HOOK_OPTIONS_OPENED);
	}
	TheRmlUiManager->showScreen(m_document);
}

void RmlOptionsScreen::close()
{
	if (m_document)
		m_document->Hide();
	delete m_pref;
	m_pref = nullptr;
}

bool RmlOptionsScreen::isVisible() const
{
	return m_document && m_document->IsVisible();
}

void RmlOptionsScreen::back()
{
	if (m_onlineTesting)	// Ronin @feature 21/11/2026 DX9: GO's test dialog answers first (modal in the .wnd)
		return;
	close();
	finish();
}

// The .wnd's Back and Accept: an online overlay closes as one, the shell's own Options layout goes (if one was made for it).
void RmlOptionsScreen::finish()
{
	if (GameSpyIsOverlayOpen(GSOVERLAY_OPTIONS))
		GameSpyCloseOverlay(GSOVERLAY_OPTIONS);
	else
	{
		SignalUIInteraction(SHELL_SCRIPT_HOOK_OPTIONS_CLOSED);
		if (TheShell)
			TheShell->destroyOptionsLayout();
	}
}

void RmlOptionsScreen::reload()
{
	if (!m_document)
		return;
	const bool shown = isVisible();
	m_document->Close();
	m_document = nullptr;
	load();
	if (shown && m_document)
	{
		fillChoices();
		if (m_model)
			m_model.DirtyAllVariables();
		TheRmlUiManager->showScreen(m_document);
	}
}

void RmlOptionsScreen::release()
{
	m_document = nullptr;
	m_model = Rml::DataModelHandle();
	delete m_pref;
	m_pref = nullptr;
}

//-------------------------------------------------------------------------------------------------
// The .wnd's preset combo, without its separate Custom window: a preset picked shows what it sets; a switch edited makes it Custom.
void RmlOptionsScreen::update()
{
	if (!isVisible() || !m_model)
		return;
	m_hq.refresh(m_model);	// the header's clock
	// Ronin @feature 21/11/2026 DX9: networkPoll(): the Windows proxy Automatic shows, once the worker has it
	if (m_systemProxy && m_systemProxy->ready.load(std::memory_order_acquire))
	{
		const ServiceConnection::ResolvedProxyPolicy &policy = m_systemProxy->result;
		m.proxySystem = policy.error.empty() && !policy.source.empty() ? policy.source : std::string("Unavailable");
		m_systemProxy.reset();
		m_model.DirtyVariable("proxy_system");
	}
	const int custom = GetDetailCustomLevel();
	// The new lighting keeps cloud shadows and ground lighting on (R3DLocalLights): shown on, and not an edit of the preset.
	if (m.lighting && (!m.cloudShadows || !m.groundLighting))
	{
		m.cloudShadows = m.groundLighting = true;
		m_detailShown.cloudShadows = m_detailShown.groundLighting = TRUE;
		m_model.DirtyVariable("cloud_shadows");
		m_model.DirtyVariable("ground_lighting");
	}
	// Improved Water keeps Smooth Water Borders on (WaterRenderObjClass::update): the same.
	if (m.improvedWater && !m.smoothWater)
	{
		m.smoothWater = true;
		m_detailShown.smoothWater = TRUE;
		m_model.DirtyVariable("smooth_water");
	}
	if (m.detailLevel != m_detailLevelShown)
	{
		m_detailLevelShown = m.detailLevel;
		if (m.detailLevel != custom)
			showDetail(GetDetailPresetPreview(m.detailLevel));
		m.detailLocked = m.restricted && m.detailLevel != custom;
		m_model.DirtyAllVariables();
	}
	else if (!sameDetail(shownDetail(), m_detailShown))
	{
		m_detailShown = shownDetail();
		if (m.detailLevel != custom && !m.detailLocked)
		{
			m.detailLevel = custom;
			m_detailLevelShown = custom;
			m_model.DirtyVariable("detail_level");
		}
	}
}

//-------------------------------------------------------------------------------------------------
// Ronin @feature 21/11/2026 DX9: Generals Online's connection rows (Data/UI/OptionsOnline.rml) in the Network page, and the
// rows its own layout drops hidden. Options.rml stays one file: both builds read the game's Data/UI.
void RmlOptionsScreen::loadOnlineRows()
{
	m_onlineRows = false;
	if (!m_document || ResolveConfiguredProvider() != ONLINE_PROVIDER_GENONLINE)
		return;
	Rml::Element *rows = m_document->GetElementById("online_rows");
	Rml::String rml;
	if (!rows || !Rml::GetFileInterface()->LoadFile(kOnlineRows, rml))
		return;
	rows->SetInnerRML(rml);
	static const char *const gameSpyRows[] = { "row_online_ip", "row_firewall_port", "row_firewall_refresh" };
	for (const char *id : gameSpyRows)
		if (Rml::Element *row = m_document->GetElementById(id))
			row->SetProperty("display", "none");
	m_onlineRows = true;
}

// Ronin @feature 21/11/2026 DX9: networkShow(): the combos' positions; the Custom addresses without the scheme GO puts back.
void RmlOptionsScreen::showOnline(const ServiceConnectionSettings &s)
{
	m.proxyMode = s.proxyMode == "disabled" ? 1 : (s.proxyMode == "custom" ? 2 : 0);
	m.proxyAddress = s.proxyURL.compare(0, 7, "http://") == 0 ? s.proxyURL.substr(7) : s.proxyURL;
	m.serverMode = s.serverMode == "custom" ? 1 : 0;
	m.serverAddress = s.serverAddress.compare(0, 8, "https://") == 0 ? s.serverAddress.substr(8) : s.serverAddress;
}

// Ronin @feature 21/11/2026 DX9: networkStartSystemLookup(): Automatic's proxy, looked up on a worker; update() picks it up.
void RmlOptionsScreen::startOnlineLookup()
{
	ServiceConnectionSettings system;
	system.proxyMode = "automatic";
	const std::vector<std::string> routes = ServiceConnection::BootstrapRoutes(GenOnlineServicesManager::EnvironmentName(),
		GenOnlineServicesManager::Settings.Network_GetConnectionSettings());
	m_systemProxy = ServiceConnection::StartProxyResolution(system, routes.empty() ? std::string("https://api.generals.online/") : routes.front());
}

// Ronin @feature 21/11/2026 DX9: networkRead(): the rows as settings; false with the reason when a Custom address is not valid.
bool RmlOptionsScreen::readOnline(ServiceConnectionSettings &settings, std::string &error) const
{
	settings.proxyMode = m.proxyMode == 1 ? "disabled" : (m.proxyMode == 2 ? "custom" : "automatic");
	std::string proxy = trimmed(m.proxyAddress);
	if (!proxy.empty() && proxy.find("://") == std::string::npos)
		proxy = "http://" + proxy;
	settings.proxyURL = proxy;
	if (!ServiceConnection::ValidateProxyURL(proxy, error))
	{
		if (settings.proxyMode == "custom")
			return false;
		settings.proxyURL.clear();	// an address not in use is dropped, not refused
		error.clear();
	}
	settings.serverMode = m.serverMode == 1 ? "custom" : "automatic";
	settings.serverAddress = trimmed(m.serverAddress);
	if (settings.serverMode == "custom")
		return ServiceConnection::NormalizeCustomEndpoint(settings.serverAddress, settings.serverAddress, error);
	settings.serverAddress.clear();
	return true;
}

// Ronin @feature 21/11/2026 DX9: networkAccept(): changed settings are tested first, in GO's dialog over this screen; false
// keeps Options open. The dialog's answer comes back through accept().
bool RmlOptionsScreen::acceptOnline()
{
	if (!m_onlineRows || m.restricted)
		return true;
	if (m_onlineDiscard)	// the test's Discard: the connection stays as saved
	{
		m_onlineDiscard = false;
		return true;
	}
	const ServiceConnectionSettings saved = GenOnlineServicesManager::Settings.Network_GetConnectionSettings();
	ServiceConnectionSettings edited = saved;
	std::string error;
	if (!readOnline(edited, error))
	{
		MessageBoxOk(UnicodeString(L"Connection settings"), wideText(error), nullptr);
		return false;
	}
	if (onlineChanged(saved, edited) && !m_onlineTested)
	{
		Rml::Context *context = TheRmlUiManager ? TheRmlUiManager->getContext() : nullptr;
		if (Rml::Element *focus = context ? context->GetFocusElement() : nullptr)
			focus->Blur();	// the dialog's login fields take the keys
		m_onlineTesting = true;
		ServiceConnection::ShowAcceptTest(edited, [](ServiceConnection::AcceptChoice choice)
		{
			RmlOptionsScreen &screen = instance();
			screen.m_onlineTesting = false;
			if (!screen.isVisible() || !screen.m_pref)
				return;
			if (choice == ServiceConnection::AcceptChoice::Save)
				screen.m_onlineTested = true;
			else if (choice == ServiceConnection::AcceptChoice::Discard)
				screen.m_onlineDiscard = true;
			if (choice != ServiceConnection::AcceptChoice::KeepEditing)
				screen.accept();
		});
		return false;
	}
	m_onlineTested = false;
	if (!GenOnlineServicesManager::Settings.Network_SaveConnectionSettings(edited, error))
	{
		MessageBoxOk(UnicodeString(L"Connection settings"), wideText(error), nullptr);
		return false;
	}
	return true;
}

//-------------------------------------------------------------------------------------------------
// saveOptions(): every setting, then the renderer's, then the resolution - it rebuilds the shell - and the MSAA reset.
void RmlOptionsScreen::accept()
{
	if (!acceptOnline())	// Ronin @feature 21/11/2026 DX9: GO's connection first, as OptionsMenu.cpp's Accept
		return;
	OptionPreferences &pref = *m_pref;

	InterfaceValues iv = m_interfaceLoaded;
	iv.languageFilter = m.languageFilter;
	iv.showMoneyPerMinute = m.moneyPerMinute;
	iv.playerObserver = m.playerObserver;
	iv.archiveReplays = m.archiveReplays;
	iv.saveCameraInReplays = m.saveCamera;
	iv.useCameraInReplays = m.useCamera;
	iv.renderFpsFontSize = overlaySize(m.showFps, m_interfaceLoaded.renderFpsFontSize);
	iv.systemTimeFontSize = overlaySize(m.showClock, m_interfaceLoaded.systemTimeFontSize);
	iv.gameTimeFontSize = overlaySize(m.showGameTime, m_interfaceLoaded.gameTimeFontSize);
	iv.networkLatencyFontSize = overlaySize(m.showLatency, m_interfaceLoaded.networkLatencyFontSize);
	iv.playerInfoListFontSize = overlaySize(m.showPlayerList, m_interfaceLoaded.playerInfoListFontSize);
	ApplyInterface(pref, iv);

	if (m.detailLevel == GetDetailCustomLevel())
		ApplyCustomGraphicsSettings(pref, shownDetail());
	if (!m.restricted)
	{
		ApplyDetailLevel(pref, m.detailLevel);
		ApplyLANIPChoice(pref, GetIPChoiceIP(m.lanIP));
		ApplyOnlineIPChoice(pref, GetIPChoiceIP(m.onlineIP));
		ApplyFirewallPortOverride(pref, atoi(m.firewallPort.c_str()));
	}

	ControlValues cv;
	cv.scrollSpeed = m.scroll;
	cv.alternateMouse = m.alternateMouse;
	cv.rightMouseScrollWithAlternate = m.rightMouseScroll;
	cv.retaliation = m.retaliation;
	cv.doubleClickAttackMove = m.doubleClickAttackMove;
	cv.drawScrollAnchor = m.drawAnchor;
	cv.moveScrollAnchor = m.moveAnchor;
	cv.edgeScrollWindowed = m.edgeWindowed;
	cv.edgeScrollFullscreen = m.edgeFullscreen;
	cv.captureWindowedGame = m.captureWindowedGame;
	cv.captureWindowedMenu = m.captureWindowedMenu;
	cv.captureFullscreenGame = m.captureFullscreenGame;
	cv.captureFullscreenMenu = m.captureFullscreenMenu;
	ApplyControls(pref, cv, m_scrollLoaded);

	ApplyMusicVolumePercent(pref, m.music);
	ApplySFXVolumePercent(pref, m.sfx);
	ApplyVoiceVolumePercent(pref, m.voice);
	ApplyMoneyVolumePercent(pref, m.money);
	ApplyBrightnessPercent(pref, m.brightness);

	// The renderer. TAA does not run under MSAA, so one choice sets both keys.
	char value[32];
	const bool taa = m.antiAliasing == 1;
	const int msaa = m.antiAliasing >= 2 ? (1 << (m.antiAliasing - 1)) : 0;
	const bool msaaChanged = DX8Wrapper::Get_Anti_Aliasing_Level() != msaa;
	TheWritableGlobalData->m_taaEnabled = taa;
	R3DTaa::setEnabled(taa);
	pref["DX9TAA"] = yesNo(taa);
	TheWritableGlobalData->m_antiAliasLevel = msaa;
	_snprintf_s(value, sizeof(value), _TRUNCATE, "%d", msaa);
	pref["AntiAliasing"] = value;

	const float sharpness = clampInt(m.taaSharpness, 0, 100) / 100.0f;
	TheWritableGlobalData->m_taaSharpness = sharpness;
	R3DTaa::setSharpen(sharpness);
	_snprintf_s(value, sizeof(value), _TRUNCATE, "%.2f", sharpness);
	pref["DX9TAASharpness"] = value;
	const float mipBias = clampInt(m.taaMipBias, -20, 0) / 10.0f;
	TheWritableGlobalData->m_taaMipBias = mipBias;
	R3DTaa::setMipBias(mipBias);
	_snprintf_s(value, sizeof(value), _TRUNCATE, "%.2f", mipBias);
	pref["DX9TAAMipBias"] = value;

	const int shadows = clampInt(m.shadowQuality, 0, 3);
	pref["DX9ShadowQuality"] = kQualityNames[shadows];
	if (shadows != R3DShadowMap::getQuality())
	{
		TheWritableGlobalData->m_shadowMapQuality = shadows;
		R3DShadowMap::applyQuality();	// rebuilds the maps; 0 gives the stencil shadows back
	}
	const int ssao = clampInt(m.ssaoQuality, 0, 3);
	TheWritableGlobalData->m_ssaoQuality = ssao;	// read every frame
	pref["DX9SSAOQuality"] = kQualityNames[ssao];

	TheWritableGlobalData->m_lighting = m.lighting;
	R3DLocalLights::setEnabled(m.lighting);
	pref["DX9Lighting"] = yesNo(m.lighting);
	TheWritableGlobalData->m_lightEffects = m.lightEffects;
	R3DLocalLights::setEffects(m.lightEffects);
	pref["DX9LightEffects"] = yesNo(m.lightEffects);
	TheWritableGlobalData->m_hdr = m.hdr;
	R3DTaa::setFloatScene(m.hdr);
	pref["DX9HDR"] = yesNo(m.hdr);
	TheWritableGlobalData->m_waterReflections = m.waterReflections;
	TheWaterSeaGlobal.mirror = m.waterReflections;
	pref["DX9WaterReflections"] = yesNo(m.waterReflections);
	TheWritableGlobalData->m_shroudInterpolation = m.shroudInterpolation;
	pref["ShroudInterpolation"] = yesNo(m.shroudInterpolation);
	TheWritableGlobalData->m_useS20PerMaterialSplat = TRUE;	// always (see readCurrent)
	pref["DX9UseS20PerMaterialSplat"] = "yes";
	if (m.improvedWater != (TheGlobalData->m_waterType == 2))	// off leaves a GameData type 1 or 3 alone
	{
		TheWritableGlobalData->m_waterType = m.improvedWater ? 2 : 0;
		if (TheWaterRenderObj != nullptr)
			TheWaterRenderObj->setWaterType((WaterRenderObjClass::WaterType)TheGlobalData->m_waterType);
	}
	pref["DX9Water"] = yesNo(m.improvedWater);
	TheWritableGlobalData->m_useTerrainPOM = m.pom;
	pref["DX9UseTerrainPOM"] = yesNo(m.pom);
	pref["ModernUI"] = yesNo(m.modernUI);	// read once at start

	Bool resolutionChanged = FALSE;
	if (!m.restricted)
		resolutionChanged = ApplyDisplayMode(pref, m.resolution);
	if (msaaChanged && !resolutionChanged)	// a mode change resets the device with the new level already
		TheDisplay->setDisplayMode(TheDisplay->getWidth(), TheDisplay->getHeight(), TheDisplay->getBitDepth(), TheDisplay->getWindowed());

	pref.write();
	close();
	if (!TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		destroyQuitMenu();	// a new resolution, then the same kind of game: the menu must be gone (the .wnd's note)
	const bool overlay = GameSpyIsOverlayOpen(GSOVERLAY_OPTIONS) != FALSE;
	finish();
	if (resolutionChanged && !overlay)
		DoResolutionDialog();
}

void RmlOptionsScreen::act(const Rml::String &what, const Rml::String &arg)
{
	if (what == "tab")
	{
		m.tab = arg;
		m_model.DirtyVariable("tab");
	}
	else if (what == "view")
	{
		m.graphicsView = arg;
		m_model.DirtyVariable("graphics_view");
	}
	else if (what == "accept" && m_pref && !m_onlineTesting)	// Ronin @feature 21/11/2026 DX9: not while GO's test dialog is up
		accept();
	else if (what == "cancel")
		back();
	else if (what == "defaults")	// what the .wnd's setDefaults() resets; display, detail and the renderer stay
	{
		m.languageFilter = true;
		ControlValues cv;
		cv.scrollSpeed = m.scroll;
		SetDefaultControls(cv);
		m.scroll = cv.scrollSpeed;
		m.alternateMouse = cv.alternateMouse != FALSE;
		m.retaliation = cv.retaliation != FALSE;
		m.doubleClickAttackMove = cv.doubleClickAttackMove != FALSE;
		m.music = GetDefaultMusicVolumePercent();
		m.sfx = GetDefaultSFXVolumePercent();
		m.voice = GetDefaultVoiceVolumePercent();
		m.brightness = 50;
		if (m_onlineRows)	// Ronin @feature 21/11/2026 DX9: GenOnlineOptionsMenuDefaults(): Automatic, the first LAN IP
		{
			showOnline(ServiceConnectionSettings());
			m.lanIP = 0;
		}
		m_model.DirtyAllVariables();
	}
	else if (what == "firewall" && m_pref && !m.restricted)
		RefreshFirewallBehavior(*m_pref);
}
