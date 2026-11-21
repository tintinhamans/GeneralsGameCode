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

// Ronin @feature 29/10/2026 DX9: Options (Data/UI/Options.rml) in place of OptionsMenu.wnd: the game's settings through
// OptionsValues, and the DX9 renderer's on their own page. Nothing applies before Accept; after tintinhamans' (arctic/go/feat/rmlui).
#pragma once

#include "GameClient/GUI/GUICallbacks/Menus/OptionsValues.h"
#include "W3DDevice/GameClient/RmlUi/RmlHqStatus.h"
#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <memory>
#include <string>

class OptionPreferences;
namespace Rml { class Event; }
struct ServiceConnectionSettings;						// Ronin @feature 21/11/2026 DX9: Generals Online's
namespace ServiceConnection { struct ProxyResolution; }

class RmlOptionsScreen : public RmlScreen
{
public:
	static RmlOptionsScreen &instance();

	virtual void open() override;
	virtual void close() override;
	virtual bool isVisible() const override;
	virtual void back() override;			// Cancel
	virtual void update() override;			// an edited detail switch turns the preset to Custom; a preset picked shows its values
	virtual void reload() override;
	virtual void release() override;
	virtual Rml::ElementDocument *document() const override { return m_document; }

private:
	RmlOptionsScreen() {}

	void load();
	void readCurrent();
	void fillChoices();						// the resolution and IP lists
	void accept();
	void finish();							// as the .wnd's Accept and Back leave: the overlay or the shell's layout goes
	void showDetail(const OptionsValues::DetailPresetValues &values);
	OptionsValues::DetailPresetValues shownDetail() const;
	void act(const Rml::String &what, const Rml::String &arg);	// what his document's events ask for

	// Ronin @feature 21/11/2026 DX9: Generals Online's connection rows, in its build (GenOnlineOptionsMenu.cpp's steps)
	void loadOnlineRows();
	void showOnline(const ServiceConnectionSettings &settings);
	void startOnlineLookup();
	bool readOnline(ServiceConnectionSettings &settings, std::string &error) const;
	bool acceptOnline();					// false keeps Options open: an address to fix, or GO's test dialog is up

	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_model;
	OptionPreferences *m_pref = nullptr;		// Options.ini as it was on opening; written on Accept
	OptionsValues::DetailPresetValues m_detailShown;	// what this screen last put in the detail switches
	int m_detailLevelShown = 0;
	int m_scrollLoaded = 0;

	RmlHqStatus m_hq;	// the header's pill and clock

	bool m_onlineRows = false;			// Ronin @feature 21/11/2026 DX9: the GO build put its rows in
	bool m_onlineTesting = false, m_onlineTested = false, m_onlineDiscard = false;	// GO's Accept test: up, passed, discarded
	std::shared_ptr<ServiceConnection::ProxyResolution> m_systemProxy;	// the Windows proxy lookup Automatic shows

	struct Model
	{
		Rml::String tab = "video";			// his pages: video, graphics, audio, controls, network; ours: interface
		Rml::String graphicsView = "legacy";	// Graphics' switch: legacy (the 2003 detail) or enhanced (the DX9 renderer)
		int textLanguage = 0, interfaceWidth = 0;	// Generals Online's rows: kept, off until that merge
		bool restricted = false;			// in a match or online: no resolution, preset or network changes
		bool detailLocked = false;			// restricted and not Custom: the switches cannot change

		int resolution = 0;
		int brightness = 50;

		int detailLevel = 0;
		int textureResolution = 2;
		int particleCap = 3000;
		bool shadow3D = false, shadow2D = false, cloudShadows = false, groundLighting = false, smoothWater = false;
		bool extraAnimations = false, noDynamicLod = false, heatEffects = false, buildingOcclusion = false, props = false;

		int antiAliasing = 0;				// 0 off, 1 TAA, 2..4 MSAA 2x 4x 8x
		int taaSharpness = 50;				// percent
		int taaMipBias = 0;					// tenths, -20..0: DX9TAAMipBias -2..0
		int shadowQuality = 2;				// 0 off .. 3 ultra
		int ssaoQuality = 1;
		bool lighting = true, lightEffects = true, hdr = false, waterReflections = true, shroudInterpolation = false;
		bool splat = true, pom = false;
		bool improvedWater = true;			// Options.ini DX9Water: WaterType 2 against the stock 0, switched on Accept
		bool modernUI = true;

		int music = 100, sfx = 100, voice = 100, money = 0;

		int scroll = 50;
		bool alternateMouse = false, rightMouseScroll = false, retaliation = true, doubleClickAttackMove = false;
		bool drawAnchor = false, moveAnchor = false, edgeWindowed = false, edgeFullscreen = true;
		bool captureWindowedGame = false, captureWindowedMenu = false, captureFullscreenGame = true, captureFullscreenMenu = true;

		bool languageFilter = true, moneyPerMinute = false, playerObserver = true;
		bool archiveReplays = false, saveCamera = false, useCamera = false;
		bool showFps = true, showClock = true, showGameTime = true, showLatency = true, showPlayerList = true;

		int lanIP = 0, onlineIP = 0;
		Rml::String firewallPort;

		// Ronin @feature 21/11/2026 DX9: Generals Online's connection (OptionsOnline.rml), its combos' positions
		int proxyMode = 0, serverMode = 0;			// Automatic, Disabled, Custom / Automatic, Custom
		Rml::String proxyAddress, serverAddress;	// what is typed for Custom
		Rml::String proxySystem, serverHost;		// what Automatic resolves to
	} m;

	OptionsValues::InterfaceValues m_interfaceLoaded;	// the overlays' font sizes, kept when they stay shown
};
