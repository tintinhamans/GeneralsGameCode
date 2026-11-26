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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: LoadScreen.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Electronic Arts Pacific.
//
//                       Confidential Information
//                Copyright (C) 2002 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
//	created:	Mar 2002
//
//	Filename: 	LoadScreen.cpp
//
//	author:		Chris Huybregts
//
//	purpose:	Contains each of the different derived LoadClasses for each of the
//						Different kind of games we can have.
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "Common/NameKeyGenerator.h"
#include "Common/AudioAffect.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/Display.h"
#if defined(GENONLINE)
#include "GameClient/DisplayStringManager.h"
#endif
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameClient.h"
#if defined(GENONLINE)
#include "GameClient/GameFont.h"
#endif
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GUI/GUICallbacks/Menus/GameSetupData.h"
#if defined(GENONLINE)
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Image.h"
#endif
#include "GameClient/Keyboard.h"
#include "GameClient/LoadScreen.h"
#include "GameClient/LoadScreenData.h"
#include "GameClient/LoadScreenView.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/RmlUiScreenRegistry.h"
#include "GameClient/Shell.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/RankPointValue.h"
#if defined(GENONLINE)
#include "../GenOnlineInit.h"
#include "../GenOnlineStatsInterface.h"
#endif

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// PRIVATE TYPES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE DATA ///////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#if defined(GENONLINE)
extern HWND ApplicationHWnd;

// GenOnline @feature arcticdolphin 09/11/2026 The Generals Online credit stays at least this long unless Esc is pressed.
static const UnsignedInt CREDIT_MIN_MS = 3000;
static const Real CREDIT_FADE_IN_MS = 500.0f;
// GenOnline @tweak arcticdolphin 09/11/2026 In 800x600 units: the GO logo in rows 222 to 346, where the title art draws the Zero Hour logo, and the website under it.
static const Real CREDIT_LOGO_TOP = 222.0f;
static const Real CREDIT_LOGO_HEIGHT = 125.0f;
static const Real CREDIT_URL_TOP = 362.0f;

// GenOnline @feature arcticdolphin 09/11/2026 Splash lines the radar van says, translatable by label. GenOnline/tools/genonline_ui_kit.py LOAD_SPLASHES mirrors them.
struct GoLoadSplash
{
	const char *label;
	const WideChar *text;
};
static const GoLoadSplash GENONLINE_LOAD_SPLASHES[] = {
	{ "GUI:GOLoadSplash1", L"Working eyes at your disposal." },
	{ "GUI:GOLoadSplash2", L"Let us see what they are up to." },
	{ "GUI:GOLoadSplash3", L"Nothing is private." },
	{ "GUI:GOLoadSplash4", L"GLA radar service." },
	{ "GUI:GOLoadSplash5", L"What station you need, general?" },
	{ "GUI:GOLoadSplash6", L"Better reception there?" },
	{ "GUI:GOLoadSplash7", L"Keep me out of sight." },
	{ "GUI:GOLoadSplash8", L"Let's go where it is safe." },
	{ "GUI:GOLoadSplash9", L"We get signal everywhere." } };
static const Int GENONLINE_SPLASH_FONT_SIZE = 16;
static const Int GENONLINE_SPLASH_MAX_ROWS = 4;
static Int s_goSplashLine = -1;
static DisplayString *s_goSplashRows[GENONLINE_SPLASH_MAX_ROWS] = { nullptr };
static Int s_goSplashRowCount = 0;

static void freeGoSplashRows()
{
	for (Int freeRow = 0; freeRow < s_goSplashRowCount; ++freeRow)
	{
		TheDisplayStringManager->freeDisplayString(s_goSplashRows[freeRow]);
		s_goSplashRows[freeRow] = nullptr;
	}
	s_goSplashRowCount = 0;
}
#endif

//-----------------------------------------------------------------------------
// PUBLIC DATA ////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#if defined(GENONLINE)
bool g_bHasDoneSOGScreenshot = false;
#endif

//-----------------------------------------------------------------------------
// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

UnsignedInt GetTeamUiColor(Int teamNumber);

// The .wnd load screen gets a view over LoadScreenData only when the registry does not route the path to RmlUi.
static Bool usesLegacyView( const char *wndPath )
{
	return !RmlUiScreenRegistry::routesToRmlUi( AsciiString( wndPath ) );
}

#if !RTS_GENERALS
// Portrait and name shown for the local player's general (or faction, for the original armies).
static const Image *getLocalGeneralPortrait( const PlayerTemplate *pt, Bool large, UnicodeString &localName )
{
	const GeneralPersona *localGeneral = TheChallengeGenerals->getGeneralByTemplateName( pt->getName() );
	const Image *portrait = nullptr;
	if (localGeneral)
	{
		portrait = localGeneral->getBioPortraitLarge();
		localName = TheGameText->fetch( localGeneral->getBioName() );
	}
	else
	{
		// the main original factions don't have associated generals
		if (pt->getName() == "FactionAmerica")
			portrait = TheMappedImageCollection->findImageByName( large ? "SAFactionLogoLg_US" : "SAFactionLogo144_US" );
		else if (pt->getName() == "FactionGLA")
			portrait = TheMappedImageCollection->findImageByName( large ? "SUFactionLogoLg_GLA" : "SUFactionLogo144_GLA" );
		else if (pt->getName() == "FactionChina")
			portrait = TheMappedImageCollection->findImageByName( large ? "SNFactionLogoLg_China" : "SNFactionLogo144_China" );
		else
			DEBUG_CRASH(("Unexpected player template"));

		localName = pt->getDisplayName();
	}
	return portrait;
}

static void fillLocalGeneral( LoadScreenData &view, const PlayerTemplate *pt, Bool large )
{
	const Image *portrait = getLocalGeneralPortrait( pt, large, view.m_localName );
	AsciiString features = pt->getGeneralFeatures();
	view.m_localFeatures = TheGameText->fetch( features.isEmpty() ? "GUI:PlayerObserver" : pt->getGeneralFeatures() );
	view.m_localPortrait = portrait ? portrait->getName() : AsciiString::TheEmptyString;
}
#endif

// Map preview markers, each numbered and tinted by team for the slot starting there, like updateMapStartSpots().
static void fillMapView( LoadScreenData &view, GameInfo *game )
{
	view.m_mapName = game->getMap();
	const MapMetaData *md = TheMapCache ? TheMapCache->findMap( game->getMap() ) : nullptr;
	if (md)
		view.m_mapDisplayName = md->m_displayName;
	else
		view.m_mapDisplayName.translate( game->getMap() );
	std::vector<GameSetupStartPositionMarker> markers = GameSetupData::computeStartPositionMarkers( game->getMap() );
	for (Int i = 0; i < MAX_SLOTS && i < (Int)markers.size(); ++i)
	{
		view.m_markers[i].m_used = markers[i].m_used;
		view.m_markers[i].m_x = markers[i].m_xFraction;
		view.m_markers[i].m_y = markers[i].m_yFraction;
	}
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);
		if (!slot || !slot->isOccupied())
			continue;
		Int startPos = slot->getApparentStartPos();
		if (startPos < 0 || startPos >= MAX_SLOTS)
			continue;
		view.m_markers[startPos].m_slotNumber = i + 1;
		view.m_markers[startPos].m_color = (slot->getTeamNumber() >= 0 ? GetTeamUiColor( slot->getTeamNumber() ) : 0xFFFFFF) & 0xFFFFFF;
	}
}

// The badge for the side getSlotSideName() names: the general's image, else the side icon.
static AsciiString getSlotSideImage( GameSlot *slot )
{
#if defined(GENONLINE_REVEAL_TEAMS)
	Int templateNum = slot->getPlayerTemplate();
#else
	Int templateNum = slot->getApparentPlayerTemplate();
#endif
	const PlayerTemplate *pt = nullptr;
	if (templateNum >= 0)
		pt = ThePlayerTemplateStore->getNthPlayerTemplate( templateNum );
	else if (templateNum == PLAYERTEMPLATE_OBSERVER)
		pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey( "FactionObserver" ) );
	if (!pt)
		return AsciiString::TheEmptyString;
#if RTS_GENERALS
	const Image *image = pt->getSideIconImage();
#else
	const Image *image = pt->getGeneralImage() ? pt->getGeneralImage() : pt->getSideIconImage();
#endif
	return image ? image->getName() : AsciiString::TheEmptyString;
}

static UnicodeString getSlotSideName( GameSlot *slot )
{
#if defined(GENONLINE_REVEAL_TEAMS)
	const PlayerTemplate* pt = ThePlayerTemplateStore->getNthPlayerTemplate(slot->getPlayerTemplate());
	return pt ? pt->getDisplayName() : slot->getApparentPlayerTemplateDisplayName();
#else
	return slot->getApparentPlayerTemplateDisplayName();
#endif
}

#if defined(GENONLINE)
// GenOnline @tweak arcticdolphin 09/11/2026 The bronze bar fill of the GO bar art, null in the Retail style or without it.
static const Image *findGoLoadBarImage()
{
	const Image *image = TheMappedImageCollection->findImageByName("LoadingBar_Progress");
	if (image == nullptr || image->getFilename().compareNoCase("GenOnline_LoadingBar.tga") != 0)
	{
		return nullptr;
	}
	return image;
}

// GenOnline @feature arcticdolphin 09/11/2026 The radar van rules in texels of its 256x156 canvas and milliseconds, mirrored by GenOnline/tools/genonline_ui_kit.py VAN_CONSTANTS.
static const Int GENONLINE_VAN_FRAME_WIDTH = 256;
static const Int GENONLINE_VAN_FRAME_HEIGHT = 156;
static const Int GENONLINE_VAN_OUTLINE_TOP = 5;
static const Int GENONLINE_VAN_OUTLINE_BOTTOM = 152;
static const Int GENONLINE_VAN_OUTLINE_LEFT = 14;
static const Int GENONLINE_VAN_OUTLINE_RIGHT = 247;
static const Int GENONLINE_VAN_TYRE_BOTTOM = 145;
static const Int GENONLINE_VAN_REAR_TYRE = 46;
static const Int GENONLINE_VAN_BUMPER = 240;
static const Int GENONLINE_VAN_MIN_HEIGHT = 20;
static const Int GENONLINE_VAN_WHEEL_FRAMES = 4;
static const UnsignedInt GENONLINE_VAN_FRAME_MS = 83;
static const UnsignedInt GENONLINE_VAN_STALL_MS = 500;
static const UnsignedInt GENONLINE_VAN_SETTLE_MS = 300;
static const UnsignedInt GENONLINE_VAN_FORGET_MS = 1000;
static const Int GENONLINE_VAN_BOB = 2;
static const UnsignedInt GENONLINE_VAN_DISH_LAG_MS = 90;
static const UnsignedInt GENONLINE_VAN_WAVE_MS = 10000;
static const Int GENONLINE_VAN_PUFFS = 12;
static const Int GENONLINE_VAN_PUFF_SPAWN_X = 44;
static const Int GENONLINE_VAN_PUFF_SPAWN_Y = 138;
static const Int GENONLINE_VAN_PUFF_JITTER = 4;
static const Int GENONLINE_VAN_PUFF_DRIFT_X = 24;
static const Int GENONLINE_VAN_PUFF_DRIFT_Y = 40;
static const Int GENONLINE_VAN_PUFF_START = 16;
static const Int GENONLINE_VAN_PUFF_END = 40;
static const UnsignedInt GENONLINE_VAN_PUFF_LIFE_MS = 500;
static const UnsignedInt GENONLINE_VAN_PUFF_LIFE_SPREAD_MS = 300;
static const UnsignedInt GENONLINE_VAN_PUFF_GAP_MS = 40;
static const Int GENONLINE_VAN_PUFF_TRAVEL = 50;
static const Int GENONLINE_VAN_PUFF_CREDIT = 3;
static const Int GENONLINE_VAN_PUFF_ALPHA = 255;
static const Real GENONLINE_VAN_BOB_HZ_A = 3.3f;
static const Real GENONLINE_VAN_BOB_HZ_B = 5.1f;
static const Real GENONLINE_VAN_BOB_PHASE_B = 1.3f;
static const Real GENONLINE_VAN_BOB_WEIGHT_A = 0.6f;
static const Int GENONLINE_VAN_SPAN = GENONLINE_VAN_OUTLINE_BOTTOM - GENONLINE_VAN_OUTLINE_TOP;
static const char *const GENONLINE_VAN_WHEEL_IMAGES[GENONLINE_VAN_WHEEL_FRAMES] = { "GOVanWheels0", "GOVanWheels1", "GOVanWheels2", "GOVanWheels3" };

// GenOnline @feature arcticdolphin 09/11/2026 The shell load road in 800x600 units and half its dash period, mirrored by GenOnline/tools/genonline_ui_kit.py LOAD_ROAD_RECT.
static const Int GENONLINE_ROAD_LEFT = 267;
static const Int GENONLINE_ROAD_TOP = 574;
static const Int GENONLINE_ROAD_RIGHT = 533;
static const Int GENONLINE_ROAD_BOTTOM = 592;
static const Real GENONLINE_ROAD_DASH_UNITS = 6.0f;

struct GoVanPuff
{
	UnsignedInt born;
	UnsignedInt life;
	Int x;
	Int y;
	Int height;
};

struct GoVanState
{
	GameWindow *window;
	Int progress;
	Int position;
	UnsignedInt changedAt;
	UnsignedInt drawnAt;
	UnsignedInt spawnedAt;
	Int frame;
	Real credit;
	Int nextPuff;
	GoVanPuff puffs[GENONLINE_VAN_PUFFS];
};

// GenOnline @feature arcticdolphin 09/11/2026 The van clock and a fixed ring of puffs, so it never allocates while it draws.
static GoVanState s_goVan;

static Int goVanScale(Int value, Int height)
{
	return (value * height + GENONLINE_VAN_SPAN / 2) / GENONLINE_VAN_SPAN;
}

static Int goLayoutScale(Int value, Int numerator, Int denominator)
{
	return (value * numerator + denominator / 2) / denominator;
}

static UnsignedInt goVanHash(UnsignedInt value)
{
	return value * 2654435761u;
}

// GenOnline @feature arcticdolphin 09/11/2026 The suspension rhythm in -1..1: two sines at unrelated rates, so the bounce never repeats a beat.
static Real goVanWave(UnsignedInt now)
{
	// Wrapped on whole beats of both sines first so the phase keeps its precision under the 24 bit FPU mode at any uptime
	const double t = (now % GENONLINE_VAN_WAVE_MS) / 1000.0;
	const double a = GENONLINE_VAN_BOB_WEIGHT_A * sin(TWO_PI * GENONLINE_VAN_BOB_HZ_A * t);
	const double b = (1.0 - GENONLINE_VAN_BOB_WEIGHT_A) * sin(TWO_PI * GENONLINE_VAN_BOB_HZ_B * t + GENONLINE_VAN_BOB_PHASE_B);
	return (Real)(a + b);
}

// GenOnline @feature arcticdolphin 09/11/2026 The van state for this road. A road not drawn for a while starts again parked.
static GoVanState *findGoVanState(GameWindow *window, Int progress, Int position, UnsignedInt now)
{
	GoVanState *state = &s_goVan;
	if (state->window != window || now - state->drawnAt >= GENONLINE_VAN_FORGET_MS)
	{
		memset(state, 0, sizeof(*state));
		state->window = window;
		state->progress = progress;
		state->position = position;
		state->changedAt = now - GENONLINE_VAN_STALL_MS - GENONLINE_VAN_SETTLE_MS;
		state->spawnedAt = now;
	}
	return state;
}

// GenOnline @feature arcticdolphin 09/11/2026 True when a shown window that draws, other than the bar and the windows holding it, meets the strip.
static Bool goVanWindowBlocks(GameWindow *parent, GameWindow *bar, const IRegion2D &barRect, const IRegion2D &strip)
{
	for (GameWindow *child = parent->winGetChild(); child != nullptr; child = child->winGetNext())
	{
		if (child == bar || child->winIsHidden())
		{
			continue;
		}
		IRegion2D rect;
		child->winGetScreenPosition(&rect.lo.x, &rect.lo.y);
		child->winGetSize(&rect.hi.x, &rect.hi.y);
		rect.hi.x += rect.lo.x;
		rect.hi.y += rect.lo.y;
		const Bool holdsBar = rect.lo.x <= barRect.lo.x && rect.lo.y <= barRect.lo.y && rect.hi.x >= barRect.hi.x && rect.hi.y >= barRect.hi.y;
		const Bool meets = rect.lo.x < strip.hi.x && strip.lo.x < rect.hi.x && rect.lo.y < strip.hi.y && strip.lo.y < rect.hi.y;
		if (!BitIsSet(child->winGetStatus(), WIN_STATUS_SEE_THRU) && !holdsBar && meets)
		{
			return TRUE;
		}
		if (goVanWindowBlocks(child, bar, barRect, strip))
		{
			return TRUE;
		}
	}
	return FALSE;
}

// GenOnline @feature arcticdolphin 09/11/2026 True when the van fits the strip over the bar and over no other window.
static Bool goVanStripIsClear(GameWindow *bar, const IRegion2D &strip)
{
	GameWindow *root = bar;
	while (root->winGetParent() != nullptr)
	{
		root = root->winGetParent();
	}
	IRegion2D top;
	root->winGetScreenPosition(&top.lo.x, &top.lo.y);
	root->winGetSize(&top.hi.x, &top.hi.y);
	top.hi.x += top.lo.x;
	top.hi.y += top.lo.y;
	if (strip.lo.x < top.lo.x || strip.lo.y < top.lo.y || strip.hi.x > top.hi.x || strip.hi.y > top.hi.y)
	{
		return FALSE;
	}
	IRegion2D barRect;
	bar->winGetScreenPosition(&barRect.lo.x, &barRect.lo.y);
	bar->winGetSize(&barRect.hi.x, &barRect.hi.y);
	barRect.hi.x += barRect.lo.x;
	barRect.hi.y += barRect.lo.y;
	return !goVanWindowBlocks(root, bar, barRect, strip);
}

struct GoVanPlace
{
	Bool placed;
	Int height;
	Int x0;
	Int y0;
};

// GenOnline @feature arcticdolphin 09/11/2026 Where the van goes, with the tyres on the top edge of the road and none when a window is in the way.
static GoVanPlace placeGoVan(GameWindow *window, const IRegion2D &road, Int progress)
{
	GoVanPlace place;
	place.placed = TheMappedImageCollection->findImageByName("GOVanBody") != nullptr && TheMappedImageCollection->findImageByName("GOVanDish") != nullptr;
	for (Int wheel = 0; wheel < GENONLINE_VAN_WHEEL_FRAMES; ++wheel)
	{
		if (TheMappedImageCollection->findImageByName(GENONLINE_VAN_WHEEL_IMAGES[wheel]) == nullptr)
		{
			place.placed = FALSE;
		}
	}

	Int height = (road.hi.y - road.lo.y) * 9 / 5;
	if (height < GENONLINE_VAN_MIN_HEIGHT)
	{
		height = GENONLINE_VAN_MIN_HEIGHT;
	}
	IRegion2D strip;
	strip.lo.x = road.lo.x + goVanScale(GENONLINE_VAN_BUMPER - GENONLINE_VAN_REAR_TYRE, height) - goVanScale(GENONLINE_VAN_BUMPER, height);
	strip.lo.y = road.lo.y - goVanScale(GENONLINE_VAN_TYRE_BOTTOM - GENONLINE_VAN_OUTLINE_TOP + GENONLINE_VAN_BOB, height);
	strip.hi.x = road.hi.x;
	strip.hi.y = road.lo.y;
	if (!goVanStripIsClear(window, strip))
	{
		place.placed = FALSE;
	}

	const Int first = road.lo.x + goVanScale(GENONLINE_VAN_BUMPER - GENONLINE_VAN_REAR_TYRE, height);
	const Int last = road.hi.x - goVanScale(GENONLINE_VAN_OUTLINE_RIGHT - GENONLINE_VAN_BUMPER, height);
	const Int bumper = first + (last - first) * progress / 100;
	place.height = height;
	place.x0 = bumper - goVanScale(GENONLINE_VAN_BUMPER, height);
	place.y0 = road.lo.y - goVanScale(GENONLINE_VAN_TYRE_BOTTOM, height);
	return place;
}

// GenOnline @feature arcticdolphin 09/11/2026 The splash line as a speech bubble from the logo van, with its constants mirrored by GenOnline/tools/genonline_ui_kit.py BUBBLE_CONSTANTS.
static const Int GENONLINE_LOGO_DISH_X = 306;
static const Int GENONLINE_LOGO_DISH_Y = 0;
static const UnsignedInt GENONLINE_BUBBLE_SHOW_MS = 600;
static const Real GENONLINE_BUBBLE_MARGIN = 8.0f;
static const Real GENONLINE_BUBBLE_TIP_GAP = 2.0f;
// GenOnline @feature arcticdolphin 09/11/2026 The splash line wraps at this width, scaled like its font, so long lines take two lines and short ones one.
static const Real GENONLINE_BUBBLE_WRAP = 260.0f;

// GenOnline @feature arcticdolphin 09/11/2026 The 9-slice bubble grows upward from a bottom fixed over the tip and stays on screen, and its tail keeps pointing at the dish.
static void drawGoLogoBubble(Int tipX, Int tipY, UnsignedInt shownMs)
{
	if (s_goSplashRowCount <= 0 || s_goSplashRows[0]->getFont() == nullptr)
	{
		return;
	}
	static const char *const sliceNames[10] = { "GOBubbleTL", "GOBubbleT", "GOBubbleTR", "GOBubbleL", "GOBubbleC", "GOBubbleR", "GOBubbleBL", "GOBubbleB", "GOBubbleBR", "GOBubbleTail" };
	const Image *slices[10];
	for (Int i = 0; i < 10; ++i)
	{
		slices[i] = TheMappedImageCollection->findImageByName(sliceNames[i]);
		if (slices[i] == nullptr)
		{
			return;
		}
	}
	Int textWidth = 0;
	for (Int sizeRow = 0; sizeRow < s_goSplashRowCount; ++sizeRow)
	{
		Int rowWidth = 0;
		s_goSplashRows[sizeRow]->getSize(&rowWidth, nullptr);
		if (rowWidth > textWidth)
		{
			textWidth = rowWidth;
		}
	}
	const Int lineHeight = s_goSplashRows[0]->getFont()->height;
	const Int textHeight = lineHeight * s_goSplashRowCount;
	if (textWidth <= 0 || lineHeight <= 0)
	{
		return;
	}

	const Int corner = lineHeight * 3 / 5;
	const Int tailWidth = lineHeight * 9 / 10;
	const Int tailHeight = lineHeight * 3 / 4;
	const Int over = lineHeight * 9 / 80;
	Int height = textHeight + lineHeight / 2;
	if (height < 2 * corner)
	{
		height = 2 * corner;
	}
	Int width = textWidth + lineHeight;
	if (width < 2 * corner + tailWidth)
	{
		width = 2 * corner + tailWidth;
	}
	const Int screenWidth = TheDisplay->getWidth();
	const Int margin = REAL_TO_INT(GENONLINE_BUBBLE_MARGIN * TheDisplay->getHeight() / 600.0f);
	Int left = tipX - width * 3 / 4;
	if (left > screenWidth - margin - width)
	{
		left = screenWidth - margin - width;
	}
	if (left < margin)
	{
		left = margin;
	}
	const Int bottom = tipY - tailHeight + over;
	const Int top = bottom - height;
	Int tailCenter = tipX;
	if (tailCenter > left + width - corner - tailWidth / 2)
	{
		tailCenter = left + width - corner - tailWidth / 2;
	}
	if (tailCenter < left + corner + tailWidth / 2)
	{
		tailCenter = left + corner + tailWidth / 2;
	}

	// Pops in whole once the logo has faded in because a fade would show the seams where its pieces overlap
	if (shownMs < GENONLINE_BUBBLE_SHOW_MS)
	{
		return;
	}
	const Int alpha = 255;
	const Color tint = GameMakeColor(255, 255, 255, alpha);
	const Int columns[4] = { left, left + corner, left + width - corner, left + width };
	const Int rows[4] = { top, top + corner, bottom - corner, bottom };
	for (Int row = 0; row < 3; ++row)
	{
		for (Int column = 0; column < 3; ++column)
		{
			TheDisplay->drawImage(slices[row * 3 + column], columns[column], rows[row], columns[column + 1], rows[row + 1], tint);
		}
	}
	const Int tailLeft = tailCenter - tailWidth / 2;
	TheDisplay->drawImage(slices[9], tailLeft, bottom - over, tailLeft + tailWidth, bottom - over + tailHeight, tint);
	const Int textTop = top + (height - textHeight) / 2;
	for (Int drawRow = 0; drawRow < s_goSplashRowCount; ++drawRow)
	{
		Int rowWidth = 0;
		s_goSplashRows[drawRow]->getSize(&rowWidth, nullptr);
		s_goSplashRows[drawRow]->draw(left + (width - rowWidth) / 2, textTop + drawRow * lineHeight, GameMakeColor(13, 21, 27, alpha), GameMakeColor(0, 0, 0, 0));
	}
}

// GenOnline @feature arcticdolphin 09/11/2026 The GO radar van on the shell load road: wheels, puffs and bob run while progress moves and settle when it stalls.
static void drawGoLoadBarVan(GameWindow *window, Int progress, const GoVanPlace &place)
{
	const Image *body = TheMappedImageCollection->findImageByName("GOVanBody");
	const Image *dish = TheMappedImageCollection->findImageByName("GOVanDish");
	const Image *puffImage = TheMappedImageCollection->findImageByName("GOVanPuff");
	if (!place.placed || body == nullptr || dish == nullptr)
	{
		return;
	}
	const Int height = place.height;
	const Int x0 = place.x0;
	const Int y0 = place.y0;
	const Int frameWidth = goVanScale(GENONLINE_VAN_FRAME_WIDTH, height);
	const Int frameHeight = goVanScale(GENONLINE_VAN_FRAME_HEIGHT, height);

	const UnsignedInt now = timeGetTime();
	GoVanState *state = findGoVanState(window, progress, x0, now);
	state->drawnAt = now;
	if (progress != state->progress)
	{
		state->progress = progress;
		state->changedAt = now;
	}
	const UnsignedInt still = now - state->changedAt;
	const Bool moving = still < GENONLINE_VAN_STALL_MS;
	Real envelope = 1.0f;
	if (moving)
	{
		state->frame = (Int)((now / GENONLINE_VAN_FRAME_MS) % GENONLINE_VAN_WHEEL_FRAMES);
	}
	else
	{
		envelope = 1.0f - (Real)(still - GENONLINE_VAN_STALL_MS) / GENONLINE_VAN_SETTLE_MS;
		if (envelope < 0.0f)
		{
			envelope = 0.0f;
		}
	}

	const Int travel = x0 > state->position ? x0 - state->position : 0;
	state->position = x0;
	if (!moving)
	{
		state->credit = 0.0f;
	}
	else
	{
		Int stretch = goVanScale(GENONLINE_VAN_PUFF_TRAVEL, height);
		if (stretch < 1)
		{
			stretch = 1;
		}
		state->credit += (Real)travel / stretch;
		if (state->credit > GENONLINE_VAN_PUFF_CREDIT)
		{
			state->credit = (Real)GENONLINE_VAN_PUFF_CREDIT;
		}
		if (state->credit >= 1.0f && now - state->spawnedAt >= GENONLINE_VAN_PUFF_GAP_MS)
		{
			state->credit -= 1.0f;
			state->spawnedAt = now;
			const UnsignedInt hash = goVanHash(now + state->nextPuff);
			const Int spread = 2 * GENONLINE_VAN_PUFF_JITTER + 1;
			GoVanPuff &puff = state->puffs[state->nextPuff];
			puff.born = now;
			puff.life = GENONLINE_VAN_PUFF_LIFE_MS + (hash >> 20) % GENONLINE_VAN_PUFF_LIFE_SPREAD_MS;
			puff.x = x0 + goVanScale(GENONLINE_VAN_PUFF_SPAWN_X + (Int)((hash >> 8) % spread) - GENONLINE_VAN_PUFF_JITTER, height);
			puff.y = y0 + goVanScale(GENONLINE_VAN_PUFF_SPAWN_Y + (Int)((hash >> 16) % spread) - GENONLINE_VAN_PUFF_JITTER, height);
			puff.height = height;
			state->nextPuff = (state->nextPuff + 1) % GENONLINE_VAN_PUFFS;
		}
	}

	if (puffImage != nullptr && puffImage->getImageWidth() > 0)
	{
		const Real aspect = (Real)puffImage->getImageHeight() / puffImage->getImageWidth();
		for (Int p = 0; p < GENONLINE_VAN_PUFFS; ++p)
		{
			const GoVanPuff &puff = state->puffs[p];
			if (puff.life == 0 || now - puff.born >= puff.life)
			{
				continue;
			}
			const Real t = (Real)(now - puff.born) / puff.life;
			const Int start = goVanScale(GENONLINE_VAN_PUFF_START, puff.height);
			const Real size = start + (goVanScale(GENONLINE_VAN_PUFF_END, puff.height) - start) * t;
			const Int cx = (Int)(puff.x - goVanScale(GENONLINE_VAN_PUFF_DRIFT_X, puff.height) * t);
			const Int cy = (Int)(puff.y - goVanScale(GENONLINE_VAN_PUFF_DRIFT_Y, puff.height) * t);
			const Int w = (Int)size;
			const Int h = (Int)(size * aspect);
			const Int alpha = (Int)(GENONLINE_VAN_PUFF_ALPHA * (1.0f - t));
			TheDisplay->drawImage(puffImage, cx - w / 2, cy - h / 2, cx - w / 2 + w, cy - h / 2 + h, GameMakeColor(255, 255, 255, alpha));
		}
	}

	const Image *wheels = TheMappedImageCollection->findImageByName(GENONLINE_VAN_WHEEL_IMAGES[state->frame]);
	if (wheels == nullptr)
	{
		return;
	}

	const Int bob = REAL_TO_INT_FLOOR(GENONLINE_VAN_BOB * height / (Real)GENONLINE_VAN_SPAN * envelope * goVanWave(now) + 0.5f);
	const Int sway = REAL_TO_INT_FLOOR(envelope * goVanWave(now - GENONLINE_VAN_DISH_LAG_MS) + 0.5f);
	TheDisplay->drawImage(body, x0, y0 + bob, x0 + frameWidth, y0 + bob + frameHeight);
	TheDisplay->drawImage(dish, x0 + sway, y0 + bob, x0 + sway + frameWidth, y0 + bob + frameHeight);
	TheDisplay->drawImage(wheels, x0, y0, x0 + frameWidth, y0 + frameHeight);
}

// GenOnline @tweak arcticdolphin 09/11/2026 The plain GO shell load bar: the kit frame, and the fill inset by the frame line and an equal gap on every side.
static void drawGoShellPlainBar( GameWindow *window, WinInstanceData *instData )
{
	const Image *left = GadgetProgressBarGetEnabledImageLeft(window);
	const Image *right = GadgetProgressBarGetEnabledImageRight(window);
	const Image *center = GadgetProgressBarGetEnabledImageCenter(window);
	const Image *fill = GadgetProgressBarGetEnabledBarImageCenter(window);
	if (left == nullptr || right == nullptr || center == nullptr || fill == nullptr)
	{
		return;
	}

	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);
	TheWindowManager->winDrawThreePieceImage(left, center, nullptr, right, x, y, width, height);

	Int line = REAL_TO_INT_CEIL(left->getBorder() * TheDisplay->getHeight() / 600.0f - 0.01f);
	if (line < 1)
	{
		line = 1;
	}
	const Int inset = 2 * line;
	const Int fillWidth = width - 2 * inset;
	const Int fillHeight = height - 2 * inset;
	if (fillWidth <= 0 || fillHeight <= 0)
	{
		return;
	}

	Int progress = (Int)window->winGetUserData();
	if (progress > 100)
	{
		progress = 100;
	}
	if (progress < 0)
	{
		progress = 0;
	}
	const Int drawWidth = fillWidth * progress / 100;
	if (drawWidth > 0)
	{
		TheDisplay->drawImage(fill, x + inset, y + inset, x + inset + drawWidth, y + inset + fillHeight);
	}
}

// GenOnline @tweak arcticdolphin 09/11/2026 The GO shell load bar as a road: asphalt between kit lines with a dashed centre line, lit behind the radar van that drives it.
static void drawGoShellLoadBar( GameWindow *window, WinInstanceData *instData )
{
	const Image *left = TheMappedImageCollection->findImageByName("GORoadL");
	const Image *center = TheMappedImageCollection->findImageByName("GORoadC");
	const Image *right = TheMappedImageCollection->findImageByName("GORoadR");
	const Image *dash = TheMappedImageCollection->findImageByName("GORoadDash");
	const Image *dashLit = TheMappedImageCollection->findImageByName("GORoadDashLit");
	const Image *tint = TheMappedImageCollection->findImageByName("GORoadTint");
	if (left == nullptr || center == nullptr || right == nullptr || dash == nullptr || dashLit == nullptr || tint == nullptr)
	{
		return;
	}

	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);
	TheWindowManager->winDrawThreePieceImage(left, center, nullptr, right, x, y, width, height);

	Int progress = (Int)window->winGetUserData();
	if (progress < 0)
	{
		progress = 0;
	}
	if (progress > 100)
	{
		progress = 100;
	}

	IRegion2D road;
	road.lo.x = x;
	road.lo.y = y;
	road.hi.x = x + width;
	road.hi.y = y + height;
	const GoVanPlace place = placeGoVan(window, road, progress);
	Int litEnd = x + width * progress / 100;
	if (litEnd < x)
	{
		litEnd = x;
	}
	if (litEnd > x + width)
	{
		litEnd = x + width;
	}

	Int line = REAL_TO_INT_CEIL(left->getBorder() * TheDisplay->getHeight() / 600.0f - 0.01f);
	if (line < 1)
	{
		line = 1;
	}
	if (litEnd > x)
	{
		TheDisplay->drawImage(tint, x, y + line, litEnd, y + height - line);
	}

	const Int thickness = line + ((height - line) & 1);
	Int half = REAL_TO_INT_FLOOR(GENONLINE_ROAD_DASH_UNITS * TheDisplay->getHeight() / 600.0f + 0.5f);
	if (half < 2)
	{
		half = 2;
	}
	const Int period = 2 * half;
	Int length = 2 * (half / 2);
	if (length < 2)
	{
		length = 2;
	}
	const Int span = width - 2 * (height / 2);
	const Int count = span >= length ? (span - length) / period + 1 : 0;
	Int total = count > 0 ? (count - 1) * period + length : 0;
	if (count > 0 && ((width - total) & 1) != 0)
	{
		++length;
		++total;
	}
	const Int pad = (width - total) / 2;
	const Int dashTop = y + (height - thickness) / 2;
	for (Int k = 0; k < count; ++k)
	{
		const Int dashLeft = x + pad + k * period;
		const Int dashRight = dashLeft + length;
		const Int split = litEnd < dashLeft ? dashLeft : (litEnd > dashRight ? dashRight : litEnd);
		if (split > dashLeft)
		{
			TheDisplay->drawImage(dashLit, dashLeft, dashTop, split, dashTop + thickness);
		}
		if (split < dashRight)
		{
			TheDisplay->drawImage(dash, split, dashTop, dashRight, dashTop + thickness);
		}
	}

	drawGoLoadBarVan(window, progress, place);
}

#endif
//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
enum{
FRAME_TITLES_START = 20,
FRAME_TELETYPE_START = 24,
FRAME_FUDGE_ADD = 30,
FRAME_PORTRAITS_START = 35,
FRAME_OUTER_CIRCLE_LINE_SHOW = 50,
FRAME_INNER_CIRCLE_LINE_SHOW = 52,
FRAME_OUTER_CIRCLE_ALPHA_SHOW = 63,
FRAME_INNER_CIRCLE_ALPHA_SHOW = 74,
FRAME_OUTER_CIRCLE_LINE_HIDE = 75,
FRAME_INNER_BACKDROP_ALPHA_SHOW = 80,
FRAME_INNER_CIRCLE_LINE_HIDE = 81,
FRAME_VS_ANIM_START = 98,
FRAME_RIGHT_VOICE = 140,
};

static const Int TELETYPE_UPDATE_FREQ = 2; // how many frames between teletype updates



//-----------------------------------------------------------------------------
// LoadScreen Class
//-----------------------------------------------------------------------------

LoadScreen::LoadScreen()
{
	m_loadScreen = nullptr;
	m_view = nullptr;
}

LoadScreen::~LoadScreen()
{
	delete m_view;
	if(m_loadScreen)
		TheWindowManager->winDestroy( m_loadScreen );
}

void LoadScreen::publishData()
{
	LoadScreenData &data = LoadScreenData::instance();
	data.touch();
	if (m_view)
		m_view->update( data );
}

void LoadScreen::publishProgress( Int percent )
{
	LoadScreenData &data = LoadScreenData::instance();
	if (data.m_progress != percent)
	{
		data.m_progress = percent;
		publishData();
	}
}

void LoadScreen::update( Int percent )
{
	TheGameEngine->serviceWindowsOS();
	if (TheGameEngine->getQuitting() || (TheGameLogic && TheGameLogic->isQuitToDesktopRequested()))
		return;	//don't bother with any of this if the player is exiting game.

	TheWindowManager->update();
	TheDisplay->update();
	// redraw all views, update the GUI
	TheDisplay->draw();

	setFPMode();
}


// SinglePlayerLoadScreen Class ///////////////////////////////////////////////
//-----------------------------------------------------------------------------
SinglePlayerLoadScreen::SinglePlayerLoadScreen()
{
	m_currentObjectiveLine = 0;
	m_currentObjectiveLineCharacter = 0;
	m_finishedObjectiveText = FALSE;
	m_currentObjectiveWidthOffset = 0;
	m_videoStream = nullptr;
	m_videoBuffer = nullptr;
}

SinglePlayerLoadScreen::~SinglePlayerLoadScreen()
{
	// nothing may draw the buffer once it is gone
	LoadScreenData::instance().m_videos[LOAD_VIDEO_BACKGROUND] = nullptr;
	publishData();

	delete m_videoBuffer;

	if ( m_videoStream )
	{
		m_videoStream->close();
	}

	TheAudio->removeAudioEvent( m_ambientLoopHandle );
}

void SinglePlayerLoadScreen::moveWindows( Int frame )
{
	enum{
		STATE_BEGIN = 250,
		STATE_SHOW_LOCATION = 251,
		STATE_BEGIN_BRIEFING = 255,
//		STATE_BEGIN_ANIMATING_TEXT = 250,
		STATE_SHOW_CAMEO_1 = 434,
		STATE_BEGIN_ANIMATING_TEXT = 356,
		STATE_HIDE_CAMEO_1 = 459,
		STATE_SHOW_CAMEO_2 = 464,
		STATE_HIDE_CAMEO_2 = 492,
		STATE_SHOW_CAMEO_3 = 497,
		STATE_HIDE_CAMEO_3 = 524,
//		STATE_END_ANIM_HEAD = 450,
		STATE_END_ANIMATING_TEXT = 730,
		STATE_END = 730
	};
	if(frame < STATE_BEGIN || frame > STATE_END)
		return;

	LoadScreenData &data = LoadScreenData::instance();

	if( frame == STATE_BEGIN_BRIEFING)
	{
		// add sound support here
		TheAudio->friend_forcePlayAudioEventRTS(&TheCampaignManager->getCurrentMission()->m_briefingVoice);
	}

	if( frame == STATE_BEGIN_ANIMATING_TEXT)
	{
		data.m_showObjectives = TRUE;
		// animate the text and stuff
	}

	if( frame > STATE_BEGIN_ANIMATING_TEXT && frame <= STATE_END_ANIMATING_TEXT && !m_finishedObjectiveText)
	{
		if(m_currentObjectiveLineCharacter >= m_unicodeObjectiveLines[m_currentObjectiveLine].getLength() )
		{
			m_currentObjectiveLine++;
			m_currentObjectiveLineCharacter =0;
		}
		if(m_currentObjectiveLine >= MAX_OBJECTIVE_LINES || m_unicodeObjectiveLines[m_currentObjectiveLine].isEmpty())
		{
			m_finishedObjectiveText = TRUE;
		}
		else
		{
			WideChar wChar = m_unicodeObjectiveLines[m_currentObjectiveLine].getCharAt(m_currentObjectiveLineCharacter);
			data.m_objectiveLines[m_currentObjectiveLine].concat(wChar);
		}
		m_currentObjectiveLineCharacter++;
	}
	switch (frame) {

	case STATE_SHOW_LOCATION:
		data.m_showLocation = TRUE;
		break;
	case STATE_SHOW_CAMEO_1:
		data.m_showUnit[0] = TRUE;
		break;
	case STATE_HIDE_CAMEO_1:
		data.m_showUnit[0] = FALSE;
		break;
	case STATE_SHOW_CAMEO_2:
		data.m_showUnit[1] = TRUE;
		break;
	case STATE_HIDE_CAMEO_2:
		data.m_showUnit[1] = FALSE;
		break;
	case STATE_SHOW_CAMEO_3:
		data.m_showUnit[2] = TRUE;
		break;
	case STATE_HIDE_CAMEO_3:
		data.m_showUnit[2] = FALSE;
		break;
	}

	publishData();
}

void SinglePlayerLoadScreen::init( GameInfo *game )
{
	//No music in SinglePlayerLoadScreen

	static const char *wndPath = "Menus/SinglePlayerLoadScreen.wnd";
	LoadScreenData &data = LoadScreenData::instance();
	data.reset();

	Mission *mission = TheCampaignManager->getCurrentMission();
	Int i = 0;
	for(; i < MAX_OBJECTIVE_LINES; ++i)
	{
		// translate the objective lines
		if(mission->m_missionObjectivesLabel[i].isNotEmpty())
			m_unicodeObjectiveLines[i] = TheGameText->fetch(mission->m_missionObjectivesLabel[i]);
	}
	for(i = 0; i < MAX_DISPLAYED_UNITS; ++i)
		data.m_unitNames[i] = TheGameText->fetch(mission->m_unitNames[i]);
	data.m_location = TheGameText->fetch(mission->m_locationNameLabel);

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( wndPath );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the single player loadscreen"));
	if (usesLegacyView( wndPath ))
	{
		m_view = NEW SinglePlayerLoadScreenView;
		m_view->init( m_loadScreen, game, data );
	}
	data.touch();

	m_currentObjectiveLine = 0;
	m_currentObjectiveWidthOffset = 0;
	m_currentObjectiveLineCharacter = 0;
	m_finishedObjectiveText = FALSE;

	m_ambientLoop.setEventName("LoadScreenAmbient");
	// create the new stream
	m_videoStream = TheVideoPlayer->open( mission->m_movieLabel );
	if ( m_videoStream == nullptr )
	{
		return;
	}

	// Create the new buffer
	m_videoBuffer = TheDisplay->createVideoBuffer();
	if (	m_videoBuffer == nullptr ||
				!m_videoBuffer->allocate(	m_videoStream->width(),
													m_videoStream->height())
		)
	{
		delete m_videoBuffer;
		m_videoBuffer = nullptr;

		if ( m_videoStream )
		{
			m_videoStream->close();
			m_videoStream = nullptr;
		}

		return;
	}

	// format the progress bar: USA to blue, GLA to green, China to red
	// and set the background image
	AsciiString campaignName = TheCampaignManager->getCurrentCampaign()->m_name;
	if (campaignName.compareNoCase("USA") == 0)
	{
		data.m_backgroundImage = "MissionLoad_USA";
		data.m_barColorIndex = 2;
	}
	else if (campaignName.compareNoCase("GLA") == 0)
	{
		data.m_backgroundImage = "MissionLoad_GLA";
		data.m_barColorIndex = 3;
	}
	else if (campaignName.compareNoCase("China") == 0)
	{
		data.m_backgroundImage = "MissionLoad_China";
		data.m_barColorIndex = 1;
	}
	// else leave the default background screen
	publishData();


	if(TheGameLODManager && TheGameLODManager->didMemPass())
	{
		// TheSuperHackers @bugfix Originally this movie render loop stopped rendering when the game window was inactive.
		// This either skipped the movie or caused decompression artifacts. Now the video just keeps playing until it done.

		Int progressUpdateCount = m_videoStream->frameCount() / FRAME_FUDGE_ADD;
		Int shiftedPercent = -FRAME_FUDGE_ADD + 1;
#if defined(GENONLINE)
		while (!m_videoStream->isPlaybackComplete())
#else
		while (m_videoStream->frameIndex() < m_videoStream->frameCount() - 1 )
#endif
		{
			if (GameClient::isMovieAbortRequested())
			{
				break;
			}

			if(!m_videoStream->isFrameReady())
			{
				Sleep(1);
				continue;
			}

			m_videoStream->frameDecompress();
			m_videoStream->frameRender(m_videoBuffer);

#if RTS_GENERALS
			moveWindows( m_videoStream->frameIndex());
#endif

			m_videoStream->frameNext();

			if(m_videoBuffer && data.m_videos[LOAD_VIDEO_BACKGROUND] != m_videoBuffer)
			{
				data.m_videos[LOAD_VIDEO_BACKGROUND] = m_videoBuffer;
				publishData();
			}
			if(m_videoStream->frameIndex() % progressUpdateCount == 0)
			{
				shiftedPercent++;
				if(shiftedPercent >0)
					shiftedPercent = 0;
				Int percent = (shiftedPercent + FRAME_FUDGE_ADD)/1.3;
				TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
				publishProgress( percent );

			}
			TheWindowManager->update();

			// redraw all views, update the GUI
			TheDisplay->draw();
		}

#if !RTS_GENERALS
		// let the background image show through
		m_videoStream->close();
		m_videoStream = nullptr;
		data.m_videos[LOAD_VIDEO_BACKGROUND] = nullptr;
		publishData();
		TheDisplay->draw();
#endif
	}
	else
	{
#if RTS_GENERALS
		// if we're min speced
		m_videoStream->frameGoto(m_videoStream->frameCount()); // zero based
		while(!m_videoStream->isFrameReady())
			Sleep(1);
		m_videoStream->frameDecompress();
		m_videoStream->frameRender(m_videoBuffer);
		if(m_videoBuffer)
			data.m_videos[LOAD_VIDEO_BACKGROUND] = m_videoBuffer;

		data.m_showObjectives = TRUE;
		for(i = 0; i < MAX_DISPLAYED_UNITS; ++i)
			data.m_showUnit[i] = TRUE;
		data.m_showLocation = TRUE;

		// Audio was choppy so, I chopped it out!
		TheAudio->friend_forcePlayAudioEventRTS(&TheCampaignManager->getCurrentMission()->m_briefingVoice);

		for(Int i = 0; i < MAX_OBJECTIVE_LINES; ++i)
		{
			data.m_objectiveLines[i] = m_unicodeObjectiveLines[i];
		}
		publishData();
#else
		// if we're min spec'ed don't play a movie
#endif

		Int delay = mission->m_voiceLength * 1000;
		Int begin = timeGetTime();
		Int currTime = begin;
		Int fudgeFactor = 0;
		while(begin + delay > currTime )
		{
			fudgeFactor = 30 * ((currTime - begin)/ INT_TO_REAL(delay ));
			publishProgress( fudgeFactor );

			if (GameClient::isMovieAbortRequested())
			{
				break;
			}

			TheWindowManager->update();
			TheDisplay->draw();
			Sleep(100);
			currTime = timeGetTime();
		}


		TheWindowManager->update();
		TheDisplay->draw();

	}
	setFPMode();
	m_ambientLoopHandle = TheAudio->addAudioEvent(&m_ambientLoop);

}

void SinglePlayerLoadScreen::reset()
{
	setLoadScreen(nullptr);
	if (m_view)
		m_view->reset();
}

void SinglePlayerLoadScreen::update( Int percent )
{
	percent = (percent + FRAME_FUDGE_ADD)/1.3;
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
	publishProgress( percent );

	// Do this last!
	LoadScreen::update( percent );
}

void SinglePlayerLoadScreen::setProgressRange( Int min, Int max )
{

}

// LoadScreenMovie Class //////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
LoadScreenMovie::LoadScreenMovie()
{
	m_stream = nullptr;
	m_buffer = nullptr;
	m_playType = WINDOW_PLAY_MOVIE_ONCE;
	m_state = WINDOW_VIDEO_STATE_STOP;
}

LoadScreenMovie::~LoadScreenMovie()
{
	release();
}

void LoadScreenMovie::release()
{
	delete m_buffer;
	m_buffer = nullptr;

	if ( m_stream )
	{
		m_stream->close();
		m_stream = nullptr;
	}
	m_state = WINDOW_VIDEO_STATE_STOP;
}

void LoadScreenMovie::play( const AsciiString &movieName, WindowVideoPlayType playType )
{
	// if we already have a movie playing, kill it.
	release();

	// create the new stream
	VideoStreamInterface *videoStream = TheVideoPlayer->open( movieName );
	if ( videoStream == nullptr )
	{
		return;
	}

	// Create the new buffer
	VideoBuffer *videoBuffer = TheDisplay->createVideoBuffer();
	if (	videoBuffer == nullptr ||
				!videoBuffer->allocate(	videoStream->width(),
													videoStream->height())
		)
	{
		delete videoBuffer;
		videoStream->close();
		return;
	}

	m_stream = videoStream;
	m_buffer = videoBuffer;
	m_playType = playType;
	m_state = WINDOW_VIDEO_STATE_PLAY;
}

void LoadScreenMovie::update()
{
	// Only advance the frame if we're playing
	if ( m_state != WINDOW_VIDEO_STATE_PLAY || !m_stream || !m_buffer )
		return;

	if ( m_stream->isFrameReady())
	{
		m_stream->frameDecompress();
		m_stream->frameRender( m_buffer );
		m_stream->frameNext();

		// If we reach frame Index of 0, we might have to pause, or loop.
		if ( m_stream->frameIndex() == 0 )
		{
			if(m_playType == WINDOW_PLAY_MOVIE_ONCE)
				m_state = WINDOW_VIDEO_STATE_STOP;
			else if (m_playType == WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME)
				m_state = WINDOW_VIDEO_STATE_PAUSE;
		}
	}
}

VideoBuffer *LoadScreenMovie::buffer() const
{
	return (m_state == WINDOW_VIDEO_STATE_PLAY || m_state == WINDOW_VIDEO_STATE_PAUSE) ? m_buffer : nullptr;
}

// ChallengeLoadScreen Class ///////////////////////////////////////////////
//-----------------------------------------------------------------------------
ChallengeLoadScreen::ChallengeLoadScreen()
{
	m_videoStream = nullptr;
	m_videoBuffer = nullptr;

	for (Int i = 0; i < 2; ++i)
	{
		m_textPosBigName[i] = 0;
		m_textPosName[i] = 0;
		m_textPosRank[i] = 0;
		m_textPosStrategy[i] = 0;
	}
}

ChallengeLoadScreen::~ChallengeLoadScreen()
{
	// nothing may draw the buffers once they are gone
	LoadScreenData &data = LoadScreenData::instance();
	for (Int i = 0; i < LOAD_VIDEO_COUNT; ++i)
		data.m_videos[i] = nullptr;
	publishData();

	delete m_videoBuffer;

	if ( m_videoStream )
	{
		m_videoStream->close();
	}

	TheAudio->removeAudioEvent( m_ambientLoopHandle );
}

// accepts the number of chars to advance, the text shown so far, the total text for final display, and the current position of the readout
// returns the updated position of the readout
static Int updateTeletypeText( Int num_chars, UnicodeString &shownText, const UnicodeString &full_text, Int current_text_pos )
{
	WideChar wChar;
	for (Int i = 0; i < num_chars; i++)
	{
		if (current_text_pos < full_text.getLength())
		{
			wChar = full_text.getCharAt(current_text_pos);
			shownText.concat(wChar);
			current_text_pos++;
		}
	}
	return current_text_pos;
}

void ChallengeLoadScreen::activatePieces( Int frame, const GeneralPersona *generalPlayer, const GeneralPersona *generalOpponent )
{
	LoadScreenData &data = LoadScreenData::instance();
	Bool changed = TRUE;

	AudioEventRTS eventLeftGeneral( generalPlayer->getNameSound() );
	AudioEventRTS eventVS("Taunts_GCAnnouncer12");
	AudioEventRTS eventRightGeneral( generalOpponent->getNameSound() );

	switch (frame)
	{
		case FRAME_TITLES_START:
			data.m_showBioTitles = TRUE;
			break;
		case FRAME_TELETYPE_START:
			// reinit the readouts for each new load screen
			for (Int i = 0; i < 2; ++i)
			{
				m_textPosBigName[i] = 0;
				m_textPosName[i] = 0;
				m_textPosRank[i] = 0;
				m_textPosStrategy[i] = 0;

				data.m_generals[i].m_bigName.clear();
				data.m_generals[i].m_name.clear();
				data.m_generals[i].m_rank.clear();
				data.m_generals[i].m_strategy.clear();
			}
			data.m_showBioEntries = TRUE;
			break;
		case FRAME_PORTRAITS_START:

			m_portraitMovieLeft.play( generalPlayer->getPortraitMovieLeftName(), WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);
			m_portraitMovieRight.play( generalOpponent->getPortraitMovieRightName(), WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);
			data.m_showPortraitMovies = TRUE;

			TheAudio->addAudioEvent( &eventLeftGeneral );

			break;
		case FRAME_OUTER_CIRCLE_ALPHA_SHOW:
			data.m_showOuterCircle = TRUE;
			break;
		case FRAME_INNER_CIRCLE_ALPHA_SHOW:
			data.m_showInnerCircle = TRUE;
			break;
		case FRAME_INNER_BACKDROP_ALPHA_SHOW:
			data.m_showVersusBackdrop = TRUE;
			break;
		case FRAME_VS_ANIM_START:
			// it's time to start the overlay movie
			data.m_showVersusBackdrop = TRUE;
			data.m_showVersus = TRUE;
			// only the .wnd shows the movie; the RmlUi screen animates its own "vs" off m_showVersus
			if (m_view)
				m_versusMovie.play( "VSSmall", WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);

			// "Verses"
			TheAudio->addAudioEvent( &eventVS );

			break;
		case FRAME_RIGHT_VOICE:
			TheAudio->addAudioEvent( &eventRightGeneral );
			changed = FALSE;
			break;
		default:
			changed = FALSE;
			break;
	}

	// update the teletype readout
	if (frame > FRAME_TELETYPE_START && (frame % TELETYPE_UPDATE_FREQ) == 0)
	{
		for (Int i = 0; i < 2; ++i)
		{
			LoadScreenGeneral &general = data.m_generals[i];
			m_textPosName[i] = updateTeletypeText( 1, general.m_name, m_bioName[i], m_textPosName[i]);
			m_textPosBigName[i] = updateTeletypeText( 1, general.m_bigName, m_bioName[i], m_textPosBigName[i]);
			m_textPosRank[i] = updateTeletypeText( 1, general.m_rank, m_bioRank[i], m_textPosRank[i]);
			m_textPosStrategy[i] = updateTeletypeText( 1, general.m_strategy, m_bioStrategy[i], m_textPosStrategy[i]);
		}
		changed = TRUE;
	}

	if (changed)
		publishData();
}

void ChallengeLoadScreen::activatePiecesMinSpec(const GeneralPersona *generalPlayer, const GeneralPersona *generalOpponent)
{
	LoadScreenData &data = LoadScreenData::instance();
	const Image *portraits[2] = { generalPlayer->getBioPortraitLarge(), generalOpponent->getBioPortraitLarge() };

	data.m_showBioTitles = TRUE;
	data.m_showBioEntries = TRUE;
	for (Int i = 0; i < 2; ++i)
	{
		LoadScreenGeneral &general = data.m_generals[i];
		general.m_bigName = m_bioName[i];
		general.m_name = m_bioName[i];
		general.m_rank = m_bioRank[i];
		general.m_strategy = m_bioStrategy[i];
		general.m_portrait = portraits[i] ? portraits[i]->getName() : AsciiString::TheEmptyString;
	}
	data.m_showPortraits = TRUE;
	data.m_showOuterCircle = TRUE;
	data.m_showInnerCircle = TRUE;
	data.m_showVersusBackdrop = TRUE;
	data.m_showVersus = TRUE;
	if (m_view)
		m_versusMovie.play( "VSSmall", WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);
	data.m_videos[LOAD_VIDEO_VERSUS] = m_versusMovie.buffer();
	publishData();
}

void ChallengeLoadScreen::updateMovies()
{
	m_portraitMovieLeft.update();
	m_portraitMovieRight.update();
	m_versusMovie.update();

	LoadScreenData &data = LoadScreenData::instance();
	VideoBuffer *left = m_portraitMovieLeft.buffer();
	VideoBuffer *right = m_portraitMovieRight.buffer();
	VideoBuffer *versus = m_versusMovie.buffer();
	if (data.m_videos[LOAD_VIDEO_PORTRAIT_LEFT] != left || data.m_videos[LOAD_VIDEO_PORTRAIT_RIGHT] != right || data.m_videos[LOAD_VIDEO_VERSUS] != versus)
	{
		data.m_videos[LOAD_VIDEO_PORTRAIT_LEFT] = left;
		data.m_videos[LOAD_VIDEO_PORTRAIT_RIGHT] = right;
		data.m_videos[LOAD_VIDEO_VERSUS] = versus;
		publishData();
	}
}


void ChallengeLoadScreen::init( GameInfo *game )
{
	const Campaign *campaign = TheCampaignManager->getCurrentCampaign();
	const Mission *mission = TheCampaignManager->getCurrentMission();

	// the player general is tied to the campaign
	const GeneralPersona* generalPlayer = TheChallengeGenerals->getPlayerGeneralByCampaignName( campaign->m_name );

	// the opponent general is tied to the mission
	DEBUG_ASSERTCRASH(mission->m_generalName.isNotEmpty(), ("No GeneralName associated with this mission, check Campaign.ini"));
	const GeneralPersona* generalOpponent = TheChallengeGenerals->getGeneralByGeneralName( mission->m_generalName );

	static const char *wndPath = "Menus/ChallengeLoadScreen.wnd";
	LoadScreenData &data = LoadScreenData::instance();
	data.reset();

	const GeneralPersona *generals[2] = { generalPlayer, generalOpponent };
	for (Int i = 0; i < 2; ++i)
	{
		m_bioName[i] = TheGameText->fetch(generals[i]->getBioName());
		m_bioRank[i] = TheGameText->fetch(generals[i]->getBioRank());
		m_bioStrategy[i] = TheGameText->fetch(generals[i]->getBioStrategy());
	}

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( wndPath );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the single player loadscreen"));
	if (usesLegacyView( wndPath ))
	{
		m_view = NEW ChallengeLoadScreenView;
		m_view->init( m_loadScreen, game, data );
	}
	data.touch();

	m_ambientLoop.setEventName("LoadScreenAmbient");

	// create the new background video stream
	m_videoStream = TheVideoPlayer->open( TheCampaignManager->getCurrentMission()->m_movieLabel );

	// Create the new buffer
	m_videoBuffer = TheDisplay->createVideoBuffer();
	if (m_videoBuffer == nullptr || m_videoStream == nullptr || !m_videoBuffer->allocate(	m_videoStream->width(), m_videoStream->height() ))
	{
		delete m_videoBuffer;
		m_videoBuffer = nullptr;

		if ( m_videoStream )
		{
			m_videoStream->close();
			m_videoStream = nullptr;
		}

		return;
	}

	// make sure reticle stuff starts out hidden
	data.m_showOuterCircle = FALSE;
	data.m_showInnerCircle = FALSE;
	data.m_showVersusBackdrop = FALSE;
	data.m_showVersus = FALSE;
	publishData();

	if(TheGameLODManager && TheGameLODManager->didMemPass())
	{
		// TheSuperHackers @bugfix Originally this movie render loop stopped rendering when the game window was inactive.
		// This either skipped the movie or caused decompression artifacts. Now the video just keeps playing until it done.

		Int progressUpdateCount = m_videoStream->frameCount() / FRAME_FUDGE_ADD;
		Int shiftedPercent = -FRAME_FUDGE_ADD + 1;
#if defined(GENONLINE)
		while (!m_videoStream->isPlaybackComplete())
#else
		while (m_videoStream->frameIndex() < m_videoStream->frameCount() - 1 )
#endif
		{
			if (GameClient::isMovieAbortRequested())
			{
				break;
			}

			if(!m_videoStream->isFrameReady())
			{
				Sleep(1);
				continue;
			}

			m_videoStream->frameDecompress();
			m_videoStream->frameRender(m_videoBuffer);
			m_videoStream->frameNext();

			if(m_videoBuffer && data.m_videos[LOAD_VIDEO_BACKGROUND] != m_videoBuffer)
			{
				data.m_videos[LOAD_VIDEO_BACKGROUND] = m_videoBuffer;
				publishData();
			}

			Int frame = m_videoStream->frameIndex();
			if(frame % progressUpdateCount == 0)
			{
				shiftedPercent++;
				if(shiftedPercent >0)
					shiftedPercent = 0;
				Int percent = (shiftedPercent + FRAME_FUDGE_ADD)/1.3;
				TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
				publishProgress( percent );
			}
			TheWindowManager->update();

			activatePieces(frame, generalPlayer, generalOpponent);
			updateMovies();

			// redraw all views, update the GUI
			TheDisplay->draw();

			TheAudio->update();
		}
	}
	else
	{
		// if we're min speced
		m_videoStream->frameGoto(m_videoStream->frameCount()); // zero based
		while(!m_videoStream->isFrameReady())
		{
			if (GameClient::isMovieAbortRequested())
			{
				return;
			}
			Sleep(1);
		}
		m_videoStream->frameDecompress();
		m_videoStream->frameRender(m_videoBuffer);
		if(m_videoBuffer)
			data.m_videos[LOAD_VIDEO_BACKGROUND] = m_videoBuffer;

		activatePiecesMinSpec(generalPlayer, generalOpponent);

		Int delay = mission->m_voiceLength * 1000;
		Int begin = timeGetTime();
		Int currTime = begin;
		Int fudgeFactor = 0;
		while(begin + delay > currTime )
		{
			fudgeFactor = 30 * ((currTime - begin)/ INT_TO_REAL(delay ));
			publishProgress( fudgeFactor );

			if (GameClient::isMovieAbortRequested())
			{
				break;
			}

			TheWindowManager->update();
			TheDisplay->draw();
			Sleep(100);
			currTime = timeGetTime();
		}

		updateMovies();
		TheWindowManager->update();
		TheDisplay->draw();
	}
	setFPMode();


	AudioEventRTS event( generalOpponent->getRandomTauntSound() );
	TheAudio->addAudioEvent( &event );

	m_ambientLoopHandle = TheAudio->addAudioEvent(&m_ambientLoop);
	TheAudio->update();
}

void ChallengeLoadScreen::reset()
{
	setLoadScreen(nullptr);
	if (m_view)
		m_view->reset();
}

void ChallengeLoadScreen::update( Int percent )
{
	percent = (percent + FRAME_FUDGE_ADD)/1.3;
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
	publishProgress( percent );

	// Do this last!
	LoadScreen::update( percent );
}

void ChallengeLoadScreen::setProgressRange( Int min, Int max )
{

}

// ShellGameLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
ShellGameLoadScreen::ShellGameLoadScreen()
{
	m_progressBar = nullptr;
#if defined(GENONLINE)
	m_creditWindow = nullptr;
	m_creditUrl = nullptr;
	m_creditLogo = nullptr;
	m_creditStartMs = 0;
	m_creditEscArmed = FALSE;
	m_creditSkipped = FALSE;
#endif
}

ShellGameLoadScreen::~ShellGameLoadScreen()
{
#if defined(GENONLINE)
	// GenOnline @feature arcticdolphin 09/11/2026 The credit window goes with the load screen window and must not reach this object.
	if (m_creditWindow != nullptr)
	{
		m_creditWindow->winSetUserData(nullptr);
		m_creditWindow->winHide(TRUE);
	}
	if (m_creditUrl != nullptr)
	{
		TheDisplayStringManager->freeDisplayString(m_creditUrl);
	}
	// GenOnline @feature arcticdolphin 09/11/2026 The splash rows go with the credit that drew them.
	if (m_creditWindow != nullptr)
	{
		freeGoSplashRows();
	}
#endif
}

void ShellGameLoadScreen::init( GameInfo *game )
{
	static BOOL firstLoad = TRUE;

	static const char *wndPath = "Menus/ShellGameLoadScreen.wnd";
	LoadScreenData &data = LoadScreenData::instance();
	data.reset();

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( wndPath );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the ShellGame loadscreen"));

	if (firstLoad && TheGameLODManager && TheGameLODManager->didMemPass())
	{
		data.m_titleScreen = TRUE;
		TheWritableGlobalData->m_breakTheMovie = FALSE;
		firstLoad = FALSE;
	}

	if (usesLegacyView( wndPath ))
	{
		m_view = NEW ShellLoadScreenView;
		m_view->init( m_loadScreen, game, data );
	}
	data.touch();

	// Ronin @feature 20/11/2026 DX9: GO's road bar on the .wnd's own progress bar (the view sets its fill)
	m_progressBar = m_loadScreen ? TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "ShellGameLoadScreen.wnd:ProgressLoad" )) : nullptr;
#if defined(GENONLINE)

	// GenOnline @tweak arcticdolphin 21/11/2026 The Generals Online credit is on the intro again, so the load screen stays the retail one.
	// GenOnline @tweak arcticdolphin 09/11/2026 On the GO credit screen the bar is the road the radar van drives, and later shell loads keep the plain bar.
	if (m_progressBar != nullptr && m_creditLogo != nullptr && findGoLoadBarImage() != nullptr)
	{
		m_progressBar->winSetDrawFunc(drawGoShellLoadBar);
		const Int screenWidth = TheDisplay->getWidth();
		const Int screenHeight = TheDisplay->getHeight();
		m_progressBar->winSetPosition(goLayoutScale(GENONLINE_ROAD_LEFT, screenWidth, 800), goLayoutScale(GENONLINE_ROAD_TOP, screenHeight, 600));
		m_progressBar->winSetSize(goLayoutScale(GENONLINE_ROAD_RIGHT - GENONLINE_ROAD_LEFT, screenWidth, 800), goLayoutScale(GENONLINE_ROAD_BOTTOM - GENONLINE_ROAD_TOP, screenHeight, 600));
	}
	else if (m_progressBar != nullptr && findGoLoadBarImage() != nullptr)
	{
		m_progressBar->winSetDrawFunc(drawGoShellPlainBar);
	}
}

void ShellGameLoadScreen::initCredit()
{
	// Please be kind and do not remove the custom logo screen when building a client that includes Generals Online.
	const Real fontScale = GlobalLanguage::getResolutionFontSizeScale(GlobalLanguage::ResolutionFontSizeMethod_Strict);
	const Int screenWidth = TheDisplay->getWidth();
	const Int screenHeight = TheDisplay->getHeight();

	// "Consolas" is Windows Vista native, can be installed in 2000, XP
	m_creditUrl = TheDisplayStringManager->newDisplayString();
	m_creditUrl->setText(UnicodeString(L"www.generals.online"));
	m_creditUrl->setFont(TheFontLibrary->getFont("Consolas", Int(14 * fontScale), FALSE));
	m_creditUrl->setWordWrap(screenWidth);
	m_creditLogo = TheMappedImageCollection->findImageByName("GOLoadLogo");
	// GenOnline @tweak arcticdolphin 09/11/2026 The GO logo replaces the title art, and the Retail style keeps the title art.
	if (m_creditLogo != nullptr)
	{
		m_loadScreen->winSetEnabledImage(0, nullptr);
	}
	// GenOnline @tweak arcticdolphin 09/11/2026 The GO branded screen leaves out the retail legal text, which also keeps the room over the bar for the radar van.
	if (m_creditLogo != nullptr)
	{
		GameWindow *legal = TheWindowManager->winGetWindowFromId(m_loadScreen, TheNameKeyGenerator->nameToKey("ShellGameLoadScreen.wnd:StaticTextLegal"));
		if (legal != nullptr)
		{
			legal->winHide(TRUE);
		}
	}
	// GenOnline @feature arcticdolphin 09/11/2026 With the GO logo a splash line, picked once per launch from the clock, never from a game random stream.
	if (m_creditLogo != nullptr)
	{
		if (s_goSplashLine < 0)
		{
			s_goSplashLine = (Int)(timeGetTime() % (sizeof(GENONLINE_LOAD_SPLASHES) / sizeof(GENONLINE_LOAD_SPLASHES[0])));
		}
		// GenOnline @bugfix arcticdolphin 09/11/2026 Break the line into rows here, each measured as it is drawn, so the bubble fits its text. The centred wrap of the engine counts every space twice.
		freeGoSplashRows();
		GameFont *font = TheFontLibrary->getFont("Consolas", Int(GENONLINE_SPLASH_FONT_SIZE * fontScale), TRUE);
		const Int wrap = REAL_TO_INT(GENONLINE_BUBBLE_WRAP * fontScale);
		UnicodeString rest = TheGameText->FETCH_OR_SUBSTITUTE(GENONLINE_LOAD_SPLASHES[s_goSplashLine].label, GENONLINE_LOAD_SPLASHES[s_goSplashLine].text);
		UnicodeString word;
		while (rest.nextToken(&word, L" "))
		{
			if (s_goSplashRowCount > 0)
			{
				DisplayString *last = s_goSplashRows[s_goSplashRowCount - 1];
				const UnicodeString previous = last->getText();
				UnicodeString candidate = previous;
				candidate.concat(L' ');
				candidate.concat(word);
				last->setText(candidate);
				Int width = 0;
				last->getSize(&width, nullptr);
				if (width < wrap || s_goSplashRowCount == GENONLINE_SPLASH_MAX_ROWS)
				{
					continue;
				}
				last->setText(previous);
			}
			DisplayString *row = TheDisplayStringManager->newDisplayString();
			row->setFont(font);
			row->setText(word);
			s_goSplashRows[s_goSplashRowCount++] = row;
		}
	}

	m_creditWindow = TheWindowManager->winCreate(m_loadScreen, WIN_STATUS_ENABLED | WIN_STATUS_NO_INPUT, 0, 0, screenWidth, screenHeight, nullptr);
	if (m_creditWindow != nullptr)
	{
		m_creditWindow->winSetUserData(this);
		m_creditWindow->winSetDrawFunc(drawCreditWindow);
	}
	m_creditStartMs = timeGetTime();
	m_creditEscArmed = FALSE;
	m_creditSkipped = FALSE;
}

void ShellGameLoadScreen::pollCreditSkip()
{
	// A fresh Esc press only because a key still held from skipping the intro must be released first
	const Bool escDown = GetForegroundWindow() == ApplicationHWnd && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
	if (!escDown)
	{
		m_creditEscArmed = TRUE;
	}
	else if (m_creditEscArmed)
	{
		m_creditSkipped = TRUE;
	}
}

Int ShellGameLoadScreen::creditPercent() const
{
	if (m_creditSkipped)
	{
		return 100;
	}
	const UnsignedInt elapsedMs = timeGetTime() - m_creditStartMs;
	if (elapsedMs >= CREDIT_MIN_MS)
	{
		return 100;
	}
	return (Int)(elapsedMs * 100 / CREDIT_MIN_MS);
}

Bool ShellGameLoadScreen::holdsAfterLoad()
{
	if (m_creditWindow == nullptr)
	{
		return FALSE;
	}
	if (TheGameEngine->getQuitting() || (TheGameLogic && TheGameLogic->isQuitToDesktopRequested()))
	{
		return FALSE;
	}
	pollCreditSkip();
	return creditPercent() < 100;
}

void ShellGameLoadScreen::drawCreditWindow( GameWindow *window, WinInstanceData *instData )
{
	ShellGameLoadScreen *screen = static_cast<ShellGameLoadScreen *>(window->winGetUserData());
	if (screen != nullptr)
	{
		screen->drawCredit();
	}
}

void ShellGameLoadScreen::drawCredit()
{
	const Int screenWidth = TheDisplay->getWidth();
	const Int screenHeight = TheDisplay->getHeight();
	const Real unit = screenHeight / 600.0f;
	const Int centerX = screenWidth / 2;

	Real fade = (timeGetTime() - m_creditStartMs) / CREDIT_FADE_IN_MS;
	if (fade > 1.0f)
	{
		fade = 1.0f;
	}
	const Int alpha = REAL_TO_INT(255.0f * fade);
	// GenOnline @tweak arcticdolphin 09/11/2026 The launcher tan for the website.
	const Color tanColor = GameMakeColor(227, 186, 108, alpha);
	const Color dropColor = GameMakeColor(0, 0, 0, alpha);

	const Int logoTop = REAL_TO_INT(CREDIT_LOGO_TOP * unit);
	const Int logoHeight = REAL_TO_INT(CREDIT_LOGO_HEIGHT * unit);
	if (m_creditLogo != nullptr && m_creditLogo->getImageHeight() > 0)
	{
		const Int logoWidth = logoHeight * m_creditLogo->getImageWidth() / m_creditLogo->getImageHeight();
		const Int logoLeft = centerX - logoWidth / 2;
		TheDisplay->drawImage(m_creditLogo, logoLeft, logoTop, logoLeft + logoWidth, logoTop + logoHeight, GameMakeColor(255, 255, 255, alpha));
		// GenOnline @feature arcticdolphin 09/11/2026 The logo van says the splash line of the launch, with the tail of the bubble on its radar dish.
		if (m_creditLogo->getImageWidth() > 0)
		{
			const Int dishX = logoLeft + GENONLINE_LOGO_DISH_X * logoWidth / m_creditLogo->getImageWidth();
			const Int dishY = logoTop + GENONLINE_LOGO_DISH_Y * logoHeight / m_creditLogo->getImageHeight();
			drawGoLogoBubble(dishX, dishY - REAL_TO_INT(GENONLINE_BUBBLE_TIP_GAP * unit), timeGetTime() - m_creditStartMs);
		}
	}

	Int width = 0;
	Int height = 0;
	m_creditUrl->getSize(&width, &height);
	m_creditUrl->draw(centerX - width / 2, REAL_TO_INT(CREDIT_URL_TOP * unit), tanColor, dropColor);
#endif
}

void ShellGameLoadScreen::reset()
{
	setLoadScreen(nullptr);
	if (m_view)
		m_view->reset();
}

void ShellGameLoadScreen::update( Int percent )
{
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
#if defined(GENONLINE)
	// GenOnline @feature arcticdolphin 09/11/2026 With the credit up the bar fills by time, so it reaches 100% when the credit ends.
	if (m_creditWindow != nullptr)
	{
		pollCreditSkip();
		const Int creditProgress = creditPercent();
		if (percent > creditProgress)
		{
			percent = creditProgress;
		}
	}
#endif
	publishProgress( percent );

	// Do this last!
	LoadScreen::update( percent );
}

// MultiPlayerLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
MultiPlayerLoadScreen::MultiPlayerLoadScreen()
{
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_playerLookup[i] = -1;
	}
}

MultiPlayerLoadScreen::~MultiPlayerLoadScreen()
{
	TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );
//	TheAudio->stopAudio( AudioAffect_Music );
}

void MultiPlayerLoadScreen::init( GameInfo *game )
{
	static const char *wndPath = "Menus/MultiplayerLoadScreen.wnd";
	LoadScreenData &data = LoadScreenData::instance();
	data.reset();

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( wndPath );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the Multiplayer loadscreen"));
	GameSlot *lSlot = game->getSlot(game->getLocalSlotNum());
	const PlayerTemplate* pt;
	if (lSlot->getPlayerTemplate() >= 0)
		pt = ThePlayerTemplateStore->getNthPlayerTemplate(lSlot->getPlayerTemplate());
	else
		pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );

#if RTS_GENERALS
	data.m_backgroundImage = pt->getLoadScreen();
#else
	// portrait, features, and name for the local player's general
	fillLocalGeneral( data, pt, TRUE );
#endif

	AsciiString musicName = pt->getLoadScreenMusic();
	if ( ! musicName.isEmpty() )
	{
		TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );
		AudioEventRTS event( musicName );
		event.setShouldFade( TRUE );

		TheAudio->addAudioEvent( &event );
		TheAudio->update();//Since GameEngine::update() is suspended until after I am gone...

	}

	//DEBUG_ASSERTCRASH(TheNetwork, ("Where the Heck is the Network!!!!"));
	//DEBUG_LOG(("NumPlayers %d", TheNetwork->getNumPlayers()));

	Int netSlot = 0;
	for (Int slotNum = 0; slotNum < MAX_SLOTS; ++slotNum)
	{
		GameSlot *slot = game->getSlot(slotNum);
		if (!slot || !slot->isOccupied())
			continue;

		LoadScreenPlayerRow &row = data.m_rows[netSlot];
		row.m_name = slot->getName();
		row.m_colorIndex = slot->getApparentColor();
		row.m_color = TheMultiplayerSettings->getColor(slot->getApparentColor())->getColor() & 0xFFFFFF;
		row.m_side = getSlotSideName( slot );
		row.m_sideImage = getSlotSideImage( slot );
		row.m_showProgress = !slot->isAI();
		AsciiString teamStr;
		teamStr.format("Team:%d", slot->getTeamNumber() + 1);
		row.m_team = TheGameText->fetch(teamStr);
		m_playerLookup[slotNum] = netSlot; // save our mapping so we can update progress correctly
		netSlot++;
	}
	data.m_rowCount = netSlot;
	fillMapView( data, game );
	data.m_gameMode = TheGameText->fetch( TheGameLogic->getGameMode() == GAME_SKIRMISH ? "GUI:Skirmish" : "GUI:Network" );

	if (usesLegacyView( wndPath ))
	{
		m_view = NEW MultiPlayerLoadScreenView;
		m_view->init( m_loadScreen, game, data );
	}
	data.touch();

	TheGameLogic->initTimeOutValues();
}

void MultiPlayerLoadScreen::reset()
{
	setLoadScreen(nullptr);
	if (m_view)
		m_view->reset();
}

void MultiPlayerLoadScreen::update( Int percent )
{
	if (TheNetwork)
	{
		if(percent <= 100)
			TheNetwork->updateLoadProgress( percent );
		TheNetwork->liteupdate();
	}
	else
	{
		if (percent <= 100)
			TheGameLogic->processProgress( TheGameInfo->getLocalSlotNum(), percent );
	}

	//GadgetProgressBarSetProgress(m_progressBars[TheNetwork->getLocalPlayerID()], percent );

	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);

	// Do this last!
	LoadScreen::update( percent );
}

void MultiPlayerLoadScreen::processProgress(Int playerId, Int percentage)
{

	if( percentage < 0 || percentage > 100 || playerId >= MAX_SLOTS || playerId < 0 || m_playerLookup[playerId] == -1)
	{
		DEBUG_CRASH(("Percentage %d was passed in for Player %d", percentage, playerId));
		return;
	}
	//DEBUG_LOG(("Percentage %d was passed in for Player %d (in loadscreen position %d)", percentage, playerId, m_playerLookup[playerId]));
	LoadScreenData::instance().m_rows[m_playerLookup[playerId]].m_progress = percentage;
	publishData();
}

// GameSpyLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
GameSpyLoadScreen::GameSpyLoadScreen()
{
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_playerLookup[i] = -1;
	}
}

GameSpyLoadScreen::~GameSpyLoadScreen()
{
}

#if !defined(GENONLINE)
extern Int GetAdditionalDisconnectsFromUserFile(Int playerID);
#endif

struct GameSpySlotInfo
{
	UnicodeString m_name;
	const Image *m_rankImage;
	const Image *m_medalImage;
	UnicodeString m_winLoss;
	UnicodeString m_disconnects;
};

// Name (with Elo in quick match), rank, officers club medal, win/loss and disconnect counts for a slot.
static void gatherGameSpySlotInfo( GameInfo *game, GameSpyGameSlot *slot, GameSpySlotInfo &info )
{
	// Get the stats for the player
#if defined(GENONLINE)
	PSPlayerStats stats = PSPlayerStats();
	GenOnlineStatsInterface* pStatsInterface = GenOnlineServicesManager::GetInterface<GenOnlineStatsInterface>();
	if (pStatsInterface != nullptr)
	{
		// Data should be in cache from lobby joins, so we can do this synchronously
		pStatsInterface->getPlayerStatsFromCache(slot->getProfileID(), &stats);
	}
#else
	PSPlayerStats stats = TheGameSpyPSMessageQueue->findPlayerStatsByID(slot->getProfileID());
#endif

	info.m_name = slot->getName();

#if defined(GENONLINE)
	// if QM, show ELO
	GenOnlineGame* pGenOnlineGame = (GenOnlineGame*)game;
	if (pGenOnlineGame->isQMGame())
	{
		info.m_name.format(L"%s (Elo: %d)", slot->getName().str(), stats.elo_rating);
	}
#endif

	DEBUG_LOG(("LoadScreen - populating info for %ls(%d) - stats returned id %d",
		slot->getName().str(), slot->getProfileID(), stats.id));

#if defined(GENONLINE)
	Bool isPreorder = false;
#else
	Bool isPreorder = TheGameSpyInfo->didPlayerPreorder(stats.id);
#endif
	Int rankPoints = CalculateRank(stats);
	Int favSide = GetFavoriteSide(stats);

	info.m_medalImage = TheMappedImageCollection->findImageByName("OfficersClubsmall");
	if (!isPreorder)
		info.m_medalImage = nullptr;
	info.m_rankImage = LookupSmallRankImage(favSide, rankPoints);

	// pop wins and losses
	Int numLosses = 0;

	PerGeneralMap::iterator it;
	for(it = stats.losses.begin(); it != stats.losses.end(); ++it)
	{
		numLosses += it->second;
	}
	Int numWins = 0;
	for(it =stats.wins.begin(); it != stats.wins.end(); ++it)
	{
		numWins += it->second;
	}
	info.m_winLoss.format(L"%d/%d", numWins, numLosses);

	// disconnects
	Int numDisconnects = 0;

	for(it =stats.discons.begin(); it != stats.discons.end(); ++it)
	{
		numDisconnects += it->second;
	}
	for(it =stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
	{
		numDisconnects += it->second;
	}
#if !defined(GENONLINE)
	numDisconnects += GetAdditionalDisconnectsFromUserFile(stats.id);
#endif

	info.m_disconnects.format(L"%d", numDisconnects);
}

void GameSpyLoadScreen::init( GameInfo *game )
{
#if defined(GENONLINE)
	g_bHasDoneSOGScreenshot = FALSE;
#endif

	static const char *wndPath = "Menus/GameSpyLoadScreen.wnd";
	LoadScreenData &data = LoadScreenData::instance();
	data.reset();

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( wndPath );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the Multiplayer loadscreen"));
	DEBUG_ASSERTCRASH(TheNetwork, ("Where the Heck is the Network!!!!"));
	DEBUG_LOG(("NumPlayers %d", TheNetwork->getNumPlayers()));
GameSlot *lSlot = game->getSlot(game->getLocalSlotNum());
	const PlayerTemplate* pt;
	if (lSlot->getPlayerTemplate() >= 0)
		pt = ThePlayerTemplateStore->getNthPlayerTemplate(lSlot->getPlayerTemplate());
	else
		pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );

#if RTS_GENERALS
	data.m_backgroundImage = pt->getLoadScreen();
#else
	// portrait, features, and name for the local player's general
	fillLocalGeneral( data, pt, FALSE );
#endif

	Int netSlot = 0;
	for (Int slotNum = 0; slotNum < MAX_SLOTS; ++slotNum)
	{
		GameSpyGameSlot *slot = (GameSpyGameSlot *)game->getSlot(slotNum);
		if (!slot || !slot->isOccupied())
			continue;

		GameSpySlotInfo info;
		gatherGameSpySlotInfo( game, slot, info );

		LoadScreenPlayerRow &row = data.m_rows[netSlot];
		row.m_name = info.m_name;
		row.m_colorIndex = slot->getApparentColor();
		row.m_color = TheMultiplayerSettings->getColor(slot->getApparentColor())->getColor() & 0xFFFFFF;
		row.m_side = getSlotSideName( slot );
		row.m_sideImage = getSlotSideImage( slot );
		row.m_winLoss = info.m_winLoss;
		row.m_disconnects = info.m_disconnects;
		row.m_rankImage = info.m_rankImage ? info.m_rankImage->getName() : AsciiString::TheEmptyString;
		row.m_medalImage = info.m_medalImage ? info.m_medalImage->getName() : AsciiString::TheEmptyString;
		row.m_showProgress = !slot->isAI();
		row.m_showStats = !slot->isAI();
		AsciiString teamStr;
		teamStr.format("Team:%d", slot->getTeamNumber() + 1);
		if (slot->isAI() && slot->getTeamNumber() == -1)
			teamStr = "Team:AI";
		row.m_team = TheGameText->fetch(teamStr);
		m_playerLookup[slotNum] = netSlot; // save our mapping so we can update progress correctly
		netSlot++;
	}
	data.m_rowCount = netSlot;
	fillMapView( data, game );
	data.m_gameMode = TheGameText->fetch( "GUI:GeneralsOnline" );

	if (usesLegacyView( wndPath ))
	{
		m_view = NEW GameSpyLoadScreenView;
		m_view->init( m_loadScreen, game, data );
	}
	data.touch();

	TheGameLogic->initTimeOutValues();
}

void GameSpyLoadScreen::reset()
{
	setLoadScreen(nullptr);
	if (m_view)
		m_view->reset();
}

void GameSpyLoadScreen::update( Int percent )
{
	if(percent <= 100)
		TheNetwork->updateLoadProgress( percent );
	TheNetwork->liteupdate();

#if defined(GENONLINE)
	if (TheNetwork != nullptr)
	{
		if (percent >= 50)
		{
			if (!g_bHasDoneSOGScreenshot)
			{
				g_bHasDoneSOGScreenshot = true;

				GenOnlineServicesManager::GetInstance()->CaptureScreenshotForProbe(EScreenshotType::SCREENSHOT_TYPE_LOADSCREEN, std::string()); // pass no URI here, wait until we have one received from server
			}
		}
	}

#endif
	//GadgetProgressBarSetProgress(m_progressBars[TheNetwork->getLocalPlayerID()], percent );

#if defined(GENONLINE)
	// GENERALS ONLINE: this is ticked in game engine, but game engine doesnt tick for MP loads when the host is complete and remotes arent... do a liteupdate like TheNetwork does
	if (GenOnlineServicesManager::GetInstance() != nullptr)
	{
		GenOnlineServicesManager::GetInstance()->Tick();
	}
#endif
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);

	// Do this last!
	LoadScreen::update( percent );
}

void GameSpyLoadScreen::processProgress(Int playerId, Int percentage)
{

	if( percentage < 0 || percentage > 100 || playerId >= MAX_SLOTS || playerId < 0 || m_playerLookup[playerId] == -1)
	{
		DEBUG_CRASH(("Percentage %d was passed in for Player %d", percentage, playerId));
		return;
	}
	//DEBUG_LOG(("Percentage %d was passed in for Player %d (in loadscreen position %d)", percentage, playerId, m_playerLookup[playerId]));
	LoadScreenData::instance().m_rows[m_playerLookup[playerId]].m_progress = percentage;
	publishData();
}

// MapTransferLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
MapTransferLoadScreen::MapTransferLoadScreen()
{
	m_oldTimeout = 0;
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_playerLookup[i] = -1;
		m_oldProgress[i] = -1;
	}
}

MapTransferLoadScreen::~MapTransferLoadScreen()
{
}

void MapTransferLoadScreen::init( GameInfo *game )
{
	static const char *wndPath = "Menus/MapTransferScreen.wnd";
	LoadScreenData &data = LoadScreenData::instance();
	data.reset();

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( wndPath );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the map transfer loadscreen"));
	if (!m_loadScreen)
		return;

	DEBUG_ASSERTCRASH(TheNetwork, ("Where the Heck is the Network?!!!!"));
	DEBUG_LOG(("NumPlayers %d", TheNetwork->getNumPlayers()));

	Int netSlot = 0;
	for (Int slotNum = 0; slotNum < MAX_SLOTS; ++slotNum)
	{
		GameSlot *slot = game->getSlot(slotNum);
		if (!slot || !slot->isHuman())
			continue;

		LoadScreenPlayerRow &row = data.m_rows[netSlot];
		row.m_name = slot->getName();
		row.m_color = TheMultiplayerSettings->getColor(slot->getApparentColor())->getColor() & 0xFFFFFF;
		const GameSlot *gameInfoSlot = TheGameInfo->getConstSlot(slotNum);
		row.m_showProgress = !(slotNum == 0 || (gameInfoSlot && gameInfoSlot->isHuman() && gameInfoSlot->hasMap()));
		m_playerLookup[slotNum] = netSlot; // save our mapping so we can update progress correctly
		netSlot++;
	}
	data.m_rowCount = netSlot;

	if (usesLegacyView( wndPath ))
	{
		m_view = NEW MapTransferLoadScreenView;
		m_view->init( m_loadScreen, game, data );
	}
	data.touch();
}

void MapTransferLoadScreen::reset()
{
	setLoadScreen(nullptr);
	if (m_view)
		m_view->reset();
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_playerLookup[i] = -1;
		m_oldProgress[i] = -1;
	}
}

void MapTransferLoadScreen::update( Int percent )
{
	if (TheNetwork)
	{
		TheNetwork->liteupdate();
	}

#if defined(GENONLINE)
	// GENERALS ONLINE: this is ticked in game engine, but game engine doesnt tick for MP loads and map transfers are while(true)... when the host is complete and remotes arent... do a liteupdate like TheNetwork does
	if (GenOnlineServicesManager::GetInstance() != nullptr)
	{
		GenOnlineServicesManager::GetInstance()->Tick();
	}

#endif
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);

	// Do this last!
	LoadScreen::update( percent );
}

void MapTransferLoadScreen::processProgress(Int playerId, Int percentage, AsciiString stateStr)
{

	if( percentage < 0 || percentage > 100 || playerId >= MAX_SLOTS || playerId < 0 || m_playerLookup[playerId] == -1)
	{
		DEBUG_CRASH(("Percentage %d was passed in for Player %d", percentage, playerId));
		return;
	}

	if (m_oldProgress[playerId] == percentage)
		return;
	m_oldProgress[playerId] = percentage;

	LoadScreenPlayerRow &row = LoadScreenData::instance().m_rows[m_playerLookup[playerId]];
	row.m_progress = percentage;
	row.m_status = TheGameText->fetch(stateStr);
	publishData();
}

void MapTransferLoadScreen::processTimeout(Int secondsLeft)
{
	if (m_oldTimeout == secondsLeft)
		return;
	m_oldTimeout = secondsLeft;

	UnicodeString txt;
	txt.format(TheGameText->fetch("MapTransfer:Timeout"), (secondsLeft/60), (secondsLeft%60));
	LoadScreenData::instance().m_timeout = txt;
	publishData();
}

void MapTransferLoadScreen::setCurrentFilename(AsciiString filename)
{
	UnicodeString txt;
	txt.translate(TheGameState->getMapLeafName(filename));
	txt.format(TheGameText->fetch("MapTransfer:CurrentFile"), txt.str());
	LoadScreenData::instance().m_currentFile = txt;
	publishData();
}
