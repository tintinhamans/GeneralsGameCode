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

// Ronin @feature 21/11/2026 DX9: the Generals Online hub for GenOnlineWelcomeMenu.wnd, the first screen after sign-in (from
// tintinhamans' arctic/go/feat/rmlui). Filled straight from GO, as GenOnlineWelcomeMenu.cpp fills the .wnd; its Init/Update never run.
#pragma once

#include "W3DDevice/GameClient/RmlUi/RmlGrowOnlyList.h"
#include "W3DDevice/GameClient/RmlUi/RmlHqStatus.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }
class PSPlayerStats;

//-------------------------------------------------------------------------------------------------
class RmlOnlineWelcomeScreen
{
public:
	static RmlOnlineWelcomeScreen &instance();
	// RmlUiManager::shutdown(): Rml::Shutdown() frees the document and context, and this outlives them.
	void releaseRml() { m_document = nullptr; m_context = nullptr; m_modelHandle = Rml::DataModelHandle(); }

	void load(Rml::Context *context);
	void show();
	void hide();
	bool isVisible() const;
	void onBack();		// the way back: a silent logout, back to the main menu; asks first while a search runs
	void update();		// GenOnlineWelcomeMenuUpdate() and GenOnlineQuickMatchMenuUpdate(): teardown, buddies, the search's timers

private:
	RmlOnlineWelcomeScreen()
		: m_motdIntroRows(m_model.motdIntro), m_motdLeftRows(m_model.motdLeft), m_motdRightRows(m_model.motdRight),
		m_factionRows(m_model.factionStats), m_playlistRows(m_model.qmPlaylists), m_sideRows(m_model.qmSides),
		m_colorRows(m_model.qmColors), m_disconnectRows(m_model.qmDisconnects), m_mapRows(m_model.qmMaps),
		m_honorRows(m_model.honors) {}

	void fillMotd();
	void fillFactionStats();
	void requestFactionStats();
	void requestPlayerCard();
	void setBuddiesCount(int numNotifications);
	void leaveToMainMenu();

	// Ronin @feature 22/11/2026 DX9: My Persona as the hub's left panel: what GO's PopupPlayerInfo.wnd shows the player beyond the
	// card - the battle honors, and Log Out (its Delete Account button, relabelled for the local player).
	void fillHonors(const PSPlayerStats &stats);
	static void confirmedLogOut();

	// Ronin @feature 22/11/2026 DX9: Quick Match as a tab of the hub, GenOnlineQuickMatchMenu.cpp's search without its screen.
	// The search lives as long as the hub: Home and Quick Match share it; Custom Match and Main Menu ask to cancel it first.
	enum QmState { QM_IDLE, QM_STARTING, QM_SEARCHING, QM_MATCH_FOUND };
	void qmReset();					// the log's opening lines, the playlists and maps (on show, when no search runs)
	void qmFillMaps();
	void qmFillOptions();			// army, colour, max disconnects: GO's populateQMSideComboBox() and friends
	void qmUpdateArmy();			// the playlist decides whether the army is the player's to pick
	void qmMapsChanged();
	void qmUpdateMapsText();
	void qmSavePrefs();
	void qmLog(const Rml::String &text, int color);
	void qmSetState(QmState state);
	void qmCancelSearch();
	bool qmBusy() const { return m_qmState != QM_IDLE; }
	void qmRegisterCallbacks();
	void qmDeregisterCallbacks();
	static void qmConfirmedToLobby();	// message box answers
	static void qmConfirmedToMainMenu();

	void onBackPressed(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onHome(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onQuickMatch(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onMyInfo(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onCustomMatch(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBuddies(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onLogOut(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onQmStart(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onQmStop(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onQmWiden(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onQmPlaylistChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onQmSideChanged(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onQmMapToggle(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onQmAllMaps(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onQmNoMaps(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	RmlHqStatus m_hq;			// .hq-header status
	unsigned m_opening = 0;		// a stats reply for an earlier opening is dropped
	bool m_raiseBoxes = false;	// GenOnlineWelcomeMenuUpdate()'s raiseMessageBoxes

	QmState m_qmState = QM_IDLE;
	bool m_qmWidenAvailable = false;	// once per search; the matchmaker's requeue gives it back
	bool m_qmEloPending = false;		// the log's Elo line comes with the player card's stats
	int m_qmLogCount = 0;
	int m_qmScrollFrames = 0;			// frames left to hold the log at its newest line
	unsigned m_qmTimeoutStart = 0;		// GenOnlineQuickMatchMenu.cpp's match-found timeout and start countdown
	int m_qmTimeoutMs = 0;
	int m_qmCountdownMs = 0;
	int m_qmCountdownLastSecond = 0;

	// One message-of-the-day line, coloured as the .wnd's listbox colours it. Ronin @feature 22/11/2026 DX9: isSection is a
	// server "===== Title =====" line, shown as a heading.
	struct MotdLineModel
	{
		Rml::String text;
		Rml::String colorHex;
		bool isHeading = false;
		bool isSection = false;
		bool used = true;	// grow-only storage, see RmlGrowOnlyList.h; hidden via data-if when false
	};

	// One army's daily win rate: the .wnd's PercentXxx checkbox (its silver medallion and SIDE: name).
	struct FactionStatModel
	{
		Rml::String icon;
		Rml::String tooltipText;
		Rml::String percentText;
		bool used = true;
	};

	// Ronin @feature 22/11/2026 DX9: one entry of a Quick Match setup list (playlist, army, colour, disconnects), its map list
	// (position = the map's place in the playlist, what StartMatchmaking() takes) and its progress log.
	struct QmOptionModel
	{
		Rml::String label;
		Rml::String colorHex;
		int value = 0;
		bool used = true;
	};

	struct QmMapModel
	{
		Rml::String file;
		Rml::String name;
		Rml::String players;
		bool selected = false;
		int index = 0;
		int position = 0;
		bool used = true;
	};

	struct QmLogLineModel
	{
		Rml::String text;
		Rml::String colorHex;
		bool used = true;
	};

	// One battle honor: GO's populateBattleHonors() medal, its tooltip split into the title and the line under it.
	struct HonorModel
	{
		Rml::String image;
		Rml::String title;
		Rml::String text;
		bool gained = false;
		bool used = true;
	};

	// Ronin @feature 22/11/2026 DX9: the hub's model - the MOTD in three lists, the record as separate numbers, the badge count,
	// the open tab and the Quick Match panel.
	struct Model
	{
		Rml::String tab = "home";			// home, qm, or persona (My Persona's panel)
		int buddiesCount = 0;
		Rml::Vector<HonorModel> honors;
		Rml::String honorsText;
		Rml::Vector<MotdLineModel> motdIntro;	// before the first section
		Rml::Vector<MotdLineModel> motdLeft;	// the sections, in two columns
		Rml::Vector<MotdLineModel> motdRight;
		Rml::Vector<FactionStatModel> factionStats;	// a row per side: the side, then its three generals

		// The player card: the .wnd's RankBorder cluster plus the career record PopupPlayerInfo.wnd shows.
		bool rankAtMax = false;
		Rml::String rankProgressWidthStyle = "0%";	// data-style-width can't bind a raw number
		Rml::String rankImageName;
		bool showFactionImage = false;
		Rml::String factionImageName;
		Rml::String rankText;
		Rml::String nextRankText;
		bool hasStats = false;
		Rml::String gamesText;
		Rml::String quickMatchText;
		Rml::String eloText;
		Rml::String monthlyEloText;
		Rml::String winsText;
		Rml::String lossesText;
		Rml::String winPercentText;
		Rml::String bestStreakText;
		Rml::String streakLabelText;
		Rml::String streakValueText;

		// Quick Match. GO's setup panel as GO sets it: ladder, max ping, max disconnects and colour stay off ("not supported in
		// GO"); the army opens for a playlist that lets players choose it.
		Rml::Vector<QmOptionModel> qmPlaylists;
		int qmPlaylist = 0;
		Rml::Vector<QmOptionModel> qmSides;
		int qmSide = -1;
		bool qmArmyEnabled = false;
		Rml::Vector<QmOptionModel> qmColors;
		int qmColor = 0;
		Rml::Vector<QmOptionModel> qmDisconnects;
		int qmMaxDisconnects = 0;
		Rml::Vector<QmMapModel> qmMaps;
		Rml::String qmMapsText;
		Rml::Vector<QmLogLineModel> qmLog;
		bool qmSearching = false;		// Cancel in Start's place
		bool qmStartEnabled = false;
		bool qmStopEnabled = true;
		bool qmWidenEnabled = false;
		bool qmSetupEnabled = true;
		bool qmLocked = false;			// a match is being set up: no way out until it starts, requeues or times out
	} m_model;

	RmlGrowOnlyList<MotdLineModel> m_motdIntroRows;
	RmlGrowOnlyList<MotdLineModel> m_motdLeftRows;
	RmlGrowOnlyList<MotdLineModel> m_motdRightRows;
	RmlGrowOnlyList<FactionStatModel> m_factionRows;
	RmlGrowOnlyList<QmOptionModel> m_playlistRows;
	RmlGrowOnlyList<QmOptionModel> m_sideRows;
	RmlGrowOnlyList<QmOptionModel> m_colorRows;
	RmlGrowOnlyList<QmOptionModel> m_disconnectRows;
	RmlGrowOnlyList<QmMapModel> m_mapRows;
	RmlGrowOnlyList<HonorModel> m_honorRows;
};

// Registry entry point (see RmlUiManager::registerScreens()).
void OpenRmlOnlineWelcomeScreen();
void CloseRmlOnlineWelcomeScreen();
