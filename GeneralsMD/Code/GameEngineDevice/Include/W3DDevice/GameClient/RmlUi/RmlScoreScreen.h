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

// FILE: RmlScoreScreen.h ///////////////////////////////////////////////////
// RmlScreen for Data/UI/ScoreScreen.rml. Registered for Menus/ScoreScreen.wnd
// (pushed by GameLogicDispatch), so unlike the .wnd version this never gets a
// ScoreScreenInit() WIN_CREATE callback -- open() replicates that function's
// isInReplayGame()/isInInternetGame()/isInLanGame()/isInSkirmishGame() mode
// dispatch itself, then builds ScoreScreenData + ScoreScreenLayout exactly the
// way ScoreScreen.cpp's initX() functions do and binds them into the model.
//
// Scope: fully covers skirmish/LAN/internet/replay (the modes Generals Online
// actually exercises after a match). Single player renders the same score
// table/background but does not replicate ScoreScreen.cpp's
// finishSinglePlayerInit() (challenge win/loss splash, campaign-completion
// movie playback, auto-save, "Retry"/"End Campaign" caption switch) --
// that is a separate, much larger port left for a future pass; the continue
// button here just calls ScoreScreenActions::pressContinue() with
// buttonIsFinishCampaign always FALSE.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/GUI/GUICallbacks/Menus/ScoreScreenData.h"
#include "W3DDevice/GameClient/RmlUi/RmlScreen.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

namespace Rml { class Context; class ElementDocument; class Event; }

//-------------------------------------------------------------------------------------------------
class RmlScoreScreen : public RmlScreen
{
public:
	static RmlScoreScreen &instance();

	// RmlScreen ----------------------------------------------------------------------------
	virtual void load(Rml::Context *context) override;
	virtual void show() override;
	virtual void hide() override;
	virtual bool isVisible() const override;
	virtual void onBack() override; // Escape: same as ButtonOk (see ScoreScreen.cpp's GWM_CHAR/KEY_ESC)

	// Appends a locally-sent chat/emote line to the chat log. Called by the same onChatSend/
	// onEmote handlers that call ScoreScreenActions::sendChat() -- there is no general "chat
	// received" hook in ScoreScreen.cpp to share (its listbox is instead populated by name
	// lookups from WOLGameSetupMenu.cpp/WOLQuickMatchMenu.cpp for disconnect notices only), so
	// this only guarantees the sender sees their own line, same as before this screen existed.
	void appendChatLine(const Rml::String &line);

private:
	RmlScoreScreen() {}

	void refreshFromGameState(); // mode dispatch + ScoreScreenData/ScoreScreenLayout -> m_model

	void onOk(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onContinue(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onBuddies(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSaveReplay(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSendChat(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);
	void onSendEmote(Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &);

	Rml::Context *m_context = nullptr;
	Rml::ElementDocument *m_document = nullptr;
	Rml::DataModelHandle m_modelHandle;
	ScoreScreenModeType m_mode = SCORESCREENMODE_SINGLEPLAYER;

	// One row's worth of fields for the data-for-bound player table (data-for/RegisterStruct/
	// RegisterArray -- see load()). Field names/types mirror ScoreScreenPlayerRow.
	struct RowModel
	{
		Rml::String displayName;
		bool isObserver = false;
		int moneyEarned = 0;
		int unitsBuilt = 0;
		int unitsLost = 0;
		int unitsDestroyed = 0;
		int buildingsBuilt = 0;
		int buildingsLost = 0;
		int buildingsDestroyed = 0;
		Rml::String sideIconImage; // mapped image name; empty means no icon
		bool showSideIcon = false;
	};

	struct Model
	{
		Rml::Vector<RowModel> rows;
		Rml::String backgroundImage;
		bool hasBackgroundImage = false;

		// ScoreScreenLayout fields, same meaning as ScoreScreen.cpp's applyScoreScreenLayout().
		bool showChatEntry = false;
		bool showEmoteButton = false;
		bool showChatBoxBorder = false;
		bool showChatLog = false;
		bool showBuddiesButton = false;
		bool showContinueButton = false;
		bool showDefaultContinueLabel = true; // continueButtonCaption is empty: show the localized default label instead
		Rml::String continueButtonCaption;
		bool showAcademyPanel = false;
		Rml::Vector<Rml::String> academyAdvice;

		Rml::String chatEntryText;
		Rml::Vector<Rml::String> chatLines;
	} m_model;
};

// Registry entry point (see RmlUiManager::init()).
void OpenRmlScoreScreen();
void CloseRmlScoreScreen();
