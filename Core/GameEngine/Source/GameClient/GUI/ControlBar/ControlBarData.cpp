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

// FILE: ControlBarData.cpp ///////////////////////////////////////////////////
// ControlBar::fillData(): the headless .wnd windows read back into a ControlBarData for a view. Reads
// exactly what the .wnd draw callbacks read (W3DControlBar.cpp, W3DPushButton.cpp), including consuming a
// button's clock the way W3DGadgetPushButtonImageDrawOne() does, so the logic keeps setting it every frame.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/ControlBarData.h"

#include "Common/GameUtility.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/HotKey.h"
#include "GameClient/InGameUI.h"
#include "GameClient/WindowLayout.h"

#include <math.h>

//-------------------------------------------------------------------------------------------------
void ControlBarButtonData::clear()
{
	shown = FALSE;
	enabled = FALSE;
	checked = FALSE;
	notReady = FALSE;
	alwaysColor = FALSE;
	flashing = FALSE;
	image = nullptr;
	overlay = nullptr;
	clockPercent = -1;
	clockInverse = FALSE;
	text.clear();
	hotkey.clear();
	border = COMMAND_BUTTON_BORDER_NONE;
	command = nullptr;
}

Bool ControlBarButtonData::operator==( const ControlBarButtonData &other ) const
{
	return shown == other.shown && enabled == other.enabled && checked == other.checked && notReady == other.notReady
		&& alwaysColor == other.alwaysColor && flashing == other.flashing && image == other.image && overlay == other.overlay
		&& clockPercent == other.clockPercent && clockInverse == other.clockInverse && text == other.text && hotkey == other.hotkey
		&& border == other.border && command == other.command;
}

void ControlBarData::clear()
{
	visible = FALSE;
	stage = CONTROL_BAR_STAGE_DEFAULT;
	context = CB_CONTEXT_NONE;
	observer = FALSE;
	faction.clear();
	moneyShown = FALSE;
	money.clear();
	powerShown = FALSE;
	powerProduction = 0;
	powerConsumption = 0;
	powerState = 0;
	powerFill = 0.0f;
	powerNeedle = 0.0f;
	for( Int i = 0; i < CONTROL_BAR_VISIBLE_COMMANDS; ++i )
		commands[ i ].clear();
	commandsShown = FALSE;
	queueShown = FALSE;
	for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; ++i )
		queue[ i ].clear();
	portraitShown = FALSE;
	portrait = nullptr;
	portraitOverlay = nullptr;
	name.clear();
	for( Int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; ++i )
	{
		upgrades[ i ].shown = FALSE;
		upgrades[ i ].owned = FALSE;
		upgrades[ i ].image = nullptr;
	}
	selectCount = 0;
	contextText.clear();
	contextPercent = -1;
	for( Int i = 0; i < CB_CTX_COUNT; ++i )
		contextButtons[ i ].clear();
	for( Int i = 0; i < CB_SIDE_COUNT; ++i )
		sideButtons[ i ].clear();
	hasRadar = FALSE;
	radarAlert = FALSE;
	cameoMovie = FALSE;
	tooltipShown = FALSE;
	tooltipName.clear();
	tooltipCost.clear();
	tooltipDescription.clear();
	generalLit = FALSE;
	rank = 0;
	sciencePoints = 0;
	experience = 0.0f;
}

//-------------------------------------------------------------------------------------------------
namespace
{
	const char *const kSideButtonNames[ CB_SIDE_COUNT ] =
	{
		"ControlBar.wnd:ButtonOptions",
		"ControlBar.wnd:ButtonIdleWorker",
		"ControlBar.wnd:ButtonPlaceBeacon",
		"ControlBar.wnd:PopupCommunicator",
		"ControlBar.wnd:ButtonGeneral",
		"ControlBar.wnd:ButtonLarge",
	};

	const char *const kInfoNames[ CB_INFO_COUNT ] =
	{
		"ControlBar.wnd:MoneyDisplay",
		"ControlBar.wnd:PowerWindow",
		"ControlBar.wnd:GeneralsExp",
	};

	const char *const kContextButtonNames[ CB_CTX_COUNT ] =
	{
		"ControlBar.wnd:ButtonCancelConstruction",
		"ControlBar.wnd:OCLTimerSellButton",
	};

	// name keys of the buttons a view addresses, looked up once
	struct ButtonKeys
	{
		NameKeyType queue[ MAX_BUILD_QUEUE_BUTTONS ];
		NameKeyType side[ CB_SIDE_COUNT ];
		NameKeyType context[ CB_CTX_COUNT ];
		NameKeyType info[ CB_INFO_COUNT ];

		ButtonKeys()
		{
			AsciiString name;
			for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; ++i )
			{
				name.format( "ControlBar.wnd:ButtonQueue%02d", i + 1 );
				queue[ i ] = TheNameKeyGenerator->nameToKey( name );
			}
			for( Int i = 0; i < CB_SIDE_COUNT; ++i )
				side[ i ] = TheNameKeyGenerator->nameToKey( kSideButtonNames[ i ] );
			for( Int i = 0; i < CB_CTX_COUNT; ++i )
				context[ i ] = TheNameKeyGenerator->nameToKey( kContextButtonNames[ i ] );
			for( Int i = 0; i < CB_INFO_COUNT; ++i )
				info[ i ] = TheNameKeyGenerator->nameToKey( kInfoNames[ i ] );
		}
	};

	const ButtonKeys &buttonKeys()
	{
		static const ButtonKeys s_keys;
		return s_keys;
	}

	// What W3DGadgetPushButtonImageDrawOne() draws for a push button, from its status, state and PushButtonData.
	void readButton( GameWindow *win, Bool shown, ControlBarButtonData &data )
	{
		data.clear();
		if( win == nullptr || !BitIsSet( win->winGetStyle(), GWS_PUSH_BUTTON ) )
			return;

		const UnsignedInt status = win->winGetStatus();
		data.shown = shown && !BitIsSet( status, WIN_STATUS_HIDDEN );
		data.enabled = BitIsSet( status, WIN_STATUS_ENABLED );
		data.checked = BitIsSet( win->winGetInstanceData()->getState(), WIN_STATE_SELECTED );
		data.notReady = BitIsSet( status, WIN_STATUS_NOT_READY );
		data.alwaysColor = BitIsSet( status, WIN_STATUS_ALWAYS_COLOR );
		data.flashing = BitIsSet( status, WIN_STATUS_FLASHING );
		data.image = GadgetButtonGetEnabledImage( win );
		data.text = win->winGetText();

		PushButtonData *pData = (PushButtonData *)win->winGetUserData();
		if( pData )
		{
			data.overlay = pData->overlayImage;
			if( pData->drawClock != NO_CLOCK )
			{
				data.clockPercent = pData->percentClock;
				data.clockInverse = pData->drawClock == INVERSE_CLOCK;
				pData->drawClock = NO_CLOCK; // the logic sets it again every frame it applies
			}
			data.command = (const CommandButton *)pData->userData;
		}

		if( data.command )
		{
			data.border = data.command->getCommandButtonMappedBorderType();
			if( TheHotKeyManager && data.command->getTextLabel().isNotEmpty() )
			{
				data.hotkey = TheHotKeyManager->searchHotKey( data.command->getTextLabel() );
				data.hotkey.toLower();
			}
		}
	}

	Real powerScale( Real value )
	{
		if( value <= 0.0f || TheGlobalData->m_powerBarBase <= 1 || TheGlobalData->m_powerBarIntervals <= 0.0f )
			return 0.0f;
		const Real scaled = (Real)( log10( value ) / log10( (Real)TheGlobalData->m_powerBarBase ) ) / TheGlobalData->m_powerBarIntervals;
		return scaled < 0.0f ? 0.0f : ( scaled > 1.0f ? 1.0f : scaled );
	}
}

//-------------------------------------------------------------------------------------------------
GameWindow *ControlBar::getButtonWindow( const ControlBarButtonId &id )
{
	if( id.index < 0 )
		return nullptr;

	switch( id.group )
	{
		case CBB_COMMAND:
			return id.index < CONTROL_BAR_VISIBLE_COMMANDS ? m_commandWindows[ id.index ] : nullptr;

		case CBB_QUEUE:
			return id.index < MAX_BUILD_QUEUE_BUTTONS ? TheWindowManager->winGetWindowFromId( m_contextParent[ CP_BUILD_QUEUE ], buttonKeys().queue[ id.index ] ) : nullptr;

		case CBB_SIDE:
			return id.index < CB_SIDE_COUNT ? TheWindowManager->winGetWindowFromId( m_contextParent[ CP_MASTER ], buttonKeys().side[ id.index ] ) : nullptr;

		case CBB_CONTEXT:
			return id.index < CB_CTX_COUNT ? TheWindowManager->winGetWindowFromId( m_contextParent[ CP_MASTER ], buttonKeys().context[ id.index ] ) : nullptr;

		case CBB_INFO:
			return id.index < CB_INFO_COUNT ? TheWindowManager->winGetWindowFromId( m_contextParent[ CP_MASTER ], buttonKeys().info[ id.index ] ) : nullptr;

		default:
			return nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
GameWindow *ControlBar::getRadarWindow()
{
	static const NameKeyType radarID = NAMEKEY( "ControlBar.wnd:LeftHUD" );
	return m_contextParent[ CP_MASTER ] ? TheWindowManager->winGetWindowFromId( m_contextParent[ CP_MASTER ], radarID ) : nullptr;
}

//-------------------------------------------------------------------------------------------------
void ControlBar::fillData( ControlBarData &data )
{
	data.clear();
	GameWindow *master = m_contextParent[ CP_MASTER ];
	if( master == nullptr )
		return;

	data.visible = !master->winIsHidden();
	data.stage = m_currentControlBarStage;
	data.context = m_currContext;
	data.observer = m_isObserverCommandBar;
	data.selectCount = TheInGameUI ? TheInGameUI->getSelectCount() : 0;

	Player *player = m_isObserverCommandBar ? m_observerLookAtPlayer : ThePlayerList->getLocalPlayer();
	if( player && player->getPlayerTemplate() )
		data.faction = player->getPlayerTemplate()->getBaseSide();

	// money and power: InGameUI::update() fills and shows them for getCurrentlyViewedPlayer()
	static const NameKeyType moneyID = NAMEKEY( "ControlBar.wnd:MoneyDisplay" );
	static const NameKeyType powerID = NAMEKEY( "ControlBar.wnd:PowerWindow" );
	GameWindow *moneyWin = TheWindowManager->winGetWindowFromId( master, moneyID );
	GameWindow *powerWin = TheWindowManager->winGetWindowFromId( master, powerID );
	if( moneyWin && !moneyWin->winIsHidden() )
	{
		data.moneyShown = TRUE;
		data.money = GadgetStaticTextGetText( moneyWin );
	}
	if( powerWin && !powerWin->winIsHidden() && player && player->getEnergy() )
	{
		// W3DPowerDraw's scale and colours
		const Energy *energy = player->getEnergy();
		data.powerShown = TRUE;
		data.powerProduction = energy->getProduction();
		data.powerConsumption = energy->getConsumption();
		if( data.powerConsumption > data.powerProduction - TheGlobalData->m_powerBarYellowRange && data.powerConsumption <= data.powerProduction )
			data.powerState = 1;
		else if( data.powerConsumption > data.powerProduction )
			data.powerState = 2;
		data.powerFill = powerScale( (Real)data.powerProduction );
		data.powerNeedle = powerScale( data.powerConsumption == 1 ? 1.5f : (Real)data.powerConsumption );
	}

	// command slots
	data.commandsShown = !m_contextParent[ CP_COMMAND ]->winIsHidden();
	for( Int i = 0; i < CONTROL_BAR_VISIBLE_COMMANDS; ++i )
		readButton( m_commandWindows[ i ], data.commandsShown, data.commands[ i ] );

	// build queue, or the portrait
	data.queueShown = !m_contextParent[ CP_BUILD_QUEUE ]->winIsHidden();
	for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; ++i )
	{
		const ControlBarButtonId id = { CBB_QUEUE, i };
		readButton( getButtonWindow( id ), data.queueShown, data.queue[ i ] );
	}

	data.portraitShown = !m_rightHUDUnitSelectParent->winIsHidden() && BitIsSet( m_rightHUDCameoWindow->winGetStatus(), WIN_STATUS_IMAGE );
	if( data.portraitShown )
	{
		data.portrait = m_rightHUDCameoWindow->winGetEnabledImage( 0 );
		const PushButtonData *cameo = (const PushButtonData *)m_rightHUDCameoWindow->winGetUserData();
		data.portraitOverlay = cameo ? cameo->overlayImage : nullptr;
		if( m_portraitThing )
			data.name = m_portraitThing->getDisplayName();
		for( Int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; ++i )
		{
			GameWindow *win = m_rightHUDUpgradeCameos[ i ];
			data.upgrades[ i ].shown = win && !win->winIsHidden();
			data.upgrades[ i ].owned = win && BitIsSet( win->winGetStatus(), WIN_STATUS_ENABLED );
			data.upgrades[ i ].image = win ? win->winGetEnabledImage( 0 ) : nullptr;
		}
	}

	// the producer's name over its queue
	if( !data.portraitShown && data.queueShown && m_currentSelectedDrawable )
		data.name = m_currentSelectedDrawable->getTemplate()->getDisplayName();

	// under construction and OCL timer: their text, progress and one button
	if( m_currContext == CB_CONTEXT_UNDER_CONSTRUCTION )
	{
		static const NameKeyType descID = NAMEKEY( "ControlBar.wnd:UnderConstructionDesc" );
		GameWindow *desc = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_UNDER_CONSTRUCTION ], descID );
		if( desc )
			data.contextText = GadgetStaticTextGetText( desc );
		data.contextPercent = m_displayedConstructPercent < 0.0f ? 0 : REAL_TO_INT( m_displayedConstructPercent );
	}
	else if( m_currContext == CB_CONTEXT_OCL_TIMER )
	{
		static const NameKeyType descID = NAMEKEY( "ControlBar.wnd:OCLTimerStaticText" );
		static const NameKeyType barID = NAMEKEY( "ControlBar.wnd:OCLTimerProgressBar" );
		GameWindow *desc = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_OCL_TIMER ], descID );
		GameWindow *bar = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_OCL_TIMER ], barID );
		if( desc )
			data.contextText = GadgetStaticTextGetText( desc );
		if( bar )
			data.contextPercent = (Int)(size_t)bar->winGetUserData(); // GadgetProgressBarSetProgress() keeps it there
	}
	const Bool contextParentShown[ CB_CTX_COUNT ] =
	{
		!m_contextParent[ CP_UNDER_CONSTRUCTION ]->winIsHidden(),
		!m_contextParent[ CP_OCL_TIMER ]->winIsHidden(),
	};
	for( Int i = 0; i < CB_CTX_COUNT; ++i )
	{
		const ControlBarButtonId id = { CBB_CONTEXT, i };
		readButton( getButtonWindow( id ), contextParentShown[ i ], data.contextButtons[ i ] );
	}

	// the buttons round the bar
	for( Int i = 0; i < CB_SIDE_COUNT; ++i )
	{
		const ControlBarButtonId id = { CBB_SIDE, i };
		GameWindow *win = getButtonWindow( id );
		readButton( win, TRUE, data.sideButtons[ i ] );
		data.sideButtons[ i ].image = nullptr; // scheme art for the .wnd; the view draws its own
	}
	data.sideButtons[ CB_SIDE_MIN_MAX ].checked = m_currentControlBarStage == CONTROL_BAR_STAGE_LOW;

	// the radar, its under-attack light (updateRadarAttackGlow() blinks WinUAttack by disabling it) and the portrait movie
	data.hasRadar = rts::localPlayerHasRadar();
	data.radarAlert = m_radarAttackGlowOn && m_radarAttackGlowWindow && !BitIsSet( m_radarAttackGlowWindow->winGetStatus(), WIN_STATUS_ENABLED );
	data.cameoMovie = TheInGameUI && TheInGameUI->cameoVideoBuffer() != nullptr;

	// the build tooltip's texts, as populateBuildTooltipLayout() left them
	if( m_buildToolTipLayout && !m_buildToolTipLayout->isHidden() )
	{
		static const NameKeyType nameID = NAMEKEY( "ControlBarPopupDescription.wnd:StaticTextName" );
		static const NameKeyType costID = NAMEKEY( "ControlBarPopupDescription.wnd:StaticTextCost" );
		static const NameKeyType descID = NAMEKEY( "ControlBarPopupDescription.wnd:StaticTextDescription" );
		GameWindow *parent = m_buildToolTipLayout->getFirstWindow();
		GameWindow *name = TheWindowManager->winGetWindowFromId( parent, nameID );
		GameWindow *cost = TheWindowManager->winGetWindowFromId( parent, costID );
		GameWindow *desc = TheWindowManager->winGetWindowFromId( parent, descID );
		data.tooltipShown = TRUE;
		if( name )
			data.tooltipName = GadgetStaticTextGetText( name );
		if( cost && !cost->winIsHidden() )
			data.tooltipCost = GadgetStaticTextGetText( cost );
		if( desc )
			data.tooltipDescription = GadgetStaticTextGetText( desc );
	}

	// the general's rank, points and blinking button
	Player *local = ThePlayerList->getLocalPlayer();
	if( local )
	{
		data.rank = local->getRankLevel();
		data.sciencePoints = local->getSciencePurchasePoints();
		const Int span = local->getSkillPointsLevelUp() - local->getSkillPointsLevelDown();
		if( span > 0 )
		{
			const Real progress = (Real)( local->getSkillPoints() - local->getSkillPointsLevelDown() ) / (Real)span;
			data.experience = progress < 0.0f ? 0.0f : ( progress > 1.0f ? 1.0f : progress );
		}
	}
	const ControlBarButtonId generalId = { CBB_SIDE, CB_SIDE_GENERAL };
	GameWindow *general = getButtonWindow( generalId );
	data.generalLit = m_genStarFlash && general && m_generalButtonHighlight && GadgetButtonGetEnabledImage( general ) == m_generalButtonHighlight;
}
