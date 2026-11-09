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
#include "Common/MessageStream.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Image.h"
#include "GameClient/Keyboard.h"
#include "GameClient/LoadScreen.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/WindowVideoManager.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/RankPointValue.h"
#include "../GenOnlineInit.h"
#include "../GenOnlineStatsInterface.h"

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// PRIVATE TYPES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE DATA ///////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
extern HWND ApplicationHWnd;

// TheSuperHackers @feature arcticdolphin 09/11/2026 The Generals Online credit stays at least this long unless Esc is pressed.
static const UnsignedInt CREDIT_MIN_MS = 3000;
static const Real CREDIT_FADE_IN_MS = 500.0f;
// TheSuperHackers @tweak arcticdolphin 09/11/2026 In 800x600 units: the GO logo in rows 222 to 346, where the title art draws the Zero Hour logo, and the website under it.
static const Real CREDIT_LOGO_TOP = 222.0f;
static const Real CREDIT_LOGO_HEIGHT = 125.0f;
static const Real CREDIT_URL_TOP = 362.0f;

//-----------------------------------------------------------------------------
// PUBLIC DATA ////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
bool g_bHasDoneSOGScreenshot = false;

//-----------------------------------------------------------------------------
// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

// TheSuperHackers @tweak arcticdolphin 09/11/2026 The bronze bar fill of the GO bar art; null in the Retail style, which leaves that art out, or without it.
static const Image *findGoLoadBarImage()
{
	const Image *image = TheMappedImageCollection->findImageByName("LoadingBar_Progress");
	if (image == nullptr || image->getFilename().compareNoCase("GeneralsOnline_LoadingBar.tga") != 0)
	{
		return nullptr;
	}
	return image;
}

// TheSuperHackers @feature arcticdolphin 09/11/2026 True for an image of the GO bar art, which the Retail style leaves out.
Bool isGoLoadBarImage(const Image *image)
{
	return image != nullptr && image->getFilename().compareNoCase("GeneralsOnline_LoadingBar.tga") == 0;
}

// TheSuperHackers @feature arcticdolphin 09/11/2026 The radar van rules in texels of its 256x156 canvas and milliseconds; scripts/go_ui_kit.py VAN_CONSTANTS mirrors them.
static const Int GO_VAN_FRAME_WIDTH = 256;
static const Int GO_VAN_FRAME_HEIGHT = 156;
static const Int GO_VAN_OUTLINE_TOP = 5;
static const Int GO_VAN_OUTLINE_BOTTOM = 152;
static const Int GO_VAN_OUTLINE_LEFT = 14;
static const Int GO_VAN_OUTLINE_RIGHT = 247;
static const Int GO_VAN_TYRE_BOTTOM = 145;
static const Int GO_VAN_REAR_TYRE = 46;
static const Int GO_VAN_BUMPER = 240;
static const Int GO_VAN_MIN_HEIGHT = 20;
static const Int GO_VAN_WHEEL_FRAMES = 4;
static const UnsignedInt GO_VAN_FRAME_MS = 83;
static const UnsignedInt GO_VAN_STALL_MS = 500;
static const UnsignedInt GO_VAN_SETTLE_MS = 300;
static const UnsignedInt GO_VAN_FORGET_MS = 1000;
static const Int GO_VAN_BARS = 16;
static const Int GO_VAN_BOB = 2;
static const UnsignedInt GO_VAN_DISH_LAG_MS = 90;
static const UnsignedInt GO_VAN_WAVE_MS = 10000;
static const Int GO_VAN_PUFFS = 12;
static const Int GO_VAN_PUFF_SPAWN_X = 44;
static const Int GO_VAN_PUFF_SPAWN_Y = 138;
static const Int GO_VAN_PUFF_JITTER = 4;
static const Int GO_VAN_PUFF_DRIFT_X = 24;
static const Int GO_VAN_PUFF_DRIFT_Y = 40;
static const Int GO_VAN_PUFF_START = 16;
static const Int GO_VAN_PUFF_END = 40;
static const UnsignedInt GO_VAN_PUFF_LIFE_MS = 500;
static const UnsignedInt GO_VAN_PUFF_LIFE_SPREAD_MS = 300;
static const UnsignedInt GO_VAN_PUFF_GAP_MS = 40;
static const Int GO_VAN_PUFF_TRAVEL = 50;
static const Int GO_VAN_PUFF_CREDIT = 3;
static const Int GO_VAN_PUFF_ALPHA = 255;
static const Real GO_VAN_BOB_HZ_A = 3.3f;
static const Real GO_VAN_BOB_HZ_B = 5.1f;
static const Real GO_VAN_BOB_PHASE_B = 1.3f;
static const Real GO_VAN_BOB_WEIGHT_A = 0.6f;
static const Int GO_VAN_SPAN = GO_VAN_OUTLINE_BOTTOM - GO_VAN_OUTLINE_TOP;
static const char *const GO_VAN_WHEEL_IMAGES[GO_VAN_WHEEL_FRAMES] = { "GOVanWheels0", "GOVanWheels1", "GOVanWheels2", "GOVanWheels3" };

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
	Int fillEnd;
	UnsignedInt changedAt;
	UnsignedInt drawnAt;
	UnsignedInt spawnedAt;
	Int frame;
	Real credit;
	Int nextPuff;
	GoVanPuff puffs[GO_VAN_PUFFS];
};

// TheSuperHackers @feature arcticdolphin 09/11/2026 One fixed slot per bar on screen, so the van never allocates while it draws.
static GoVanState s_goVans[GO_VAN_BARS];

static Int goVanScale(Int value, Int height)
{
	return (value * height + GO_VAN_SPAN / 2) / GO_VAN_SPAN;
}

static UnsignedInt goVanHash(UnsignedInt value)
{
	return value * 2654435761u;
}

// TheSuperHackers @feature arcticdolphin 09/11/2026 The suspension rhythm in -1..1: two sines at unrelated rates, so the bounce never repeats a beat.
static Real goVanWave(UnsignedInt now)
{
	// wrapped on whole beats of both sines first, so the phase keeps its precision under the 24 bit FPU mode at any uptime
	const double t = (now % GO_VAN_WAVE_MS) / 1000.0;
	const double a = GO_VAN_BOB_WEIGHT_A * sin(TWO_PI * GO_VAN_BOB_HZ_A * t);
	const double b = (1.0 - GO_VAN_BOB_WEIGHT_A) * sin(TWO_PI * GO_VAN_BOB_HZ_B * t + GO_VAN_BOB_PHASE_B);
	return (Real)(a + b);
}

// TheSuperHackers @feature arcticdolphin 09/11/2026 The bar's slot; a bar not drawn for a while takes the stalest slot and starts parked.
static GoVanState *findGoVanState(GameWindow *window, Int progress, Int fillEnd, UnsignedInt now)
{
	GoVanState *oldest = &s_goVans[0];
	for (Int i = 0; i < GO_VAN_BARS; ++i)
	{
		GoVanState *state = &s_goVans[i];
		if (state->window == window && now - state->drawnAt < GO_VAN_FORGET_MS)
		{
			return state;
		}
		if (now - state->drawnAt > now - oldest->drawnAt)
		{
			oldest = state;
		}
	}
	memset(oldest, 0, sizeof(*oldest));
	oldest->window = window;
	oldest->progress = progress;
	oldest->fillEnd = fillEnd;
	oldest->changedAt = now - GO_VAN_STALL_MS - GO_VAN_SETTLE_MS;
	oldest->spawnedAt = now;
	return oldest;
}

// TheSuperHackers @feature arcticdolphin 09/11/2026 True when a shown window that draws, other than the bar and the windows holding it, meets the strip.
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

// TheSuperHackers @feature arcticdolphin 09/11/2026 True when the van fits the strip over the bar: inside the bar's top window and over no other window.
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
	Bool inside;
	Int height;
	Int x0;
	Int y0;
};

// TheSuperHackers @feature arcticdolphin 09/11/2026 Where the van goes: on the road's top edge at the fill end when there is room, else on the track ahead of the fill.
static GoVanPlace placeGoVan(GameWindow *window, const IRegion2D &road, const IRegion2D &fill, Int fillEnd)
{
	GoVanPlace place;
	place.placed = TheMappedImageCollection->findImageByName("GOVanBody") != nullptr && TheMappedImageCollection->findImageByName("GOVanDish") != nullptr;
	// a partial set of van images gives no van and no fill cut
	for (Int wheel = 0; wheel < GO_VAN_WHEEL_FRAMES; ++wheel)
	{
		if (TheMappedImageCollection->findImageByName(GO_VAN_WHEEL_IMAGES[wheel]) == nullptr)
		{
			place.placed = FALSE;
		}
	}
	place.inside = FALSE;

	// above the road: about 1.8 times its height, tyres on its top edge, when nothing else is drawn there
	Int height = (road.hi.y - road.lo.y) * 9 / 5;
	if (height < GO_VAN_MIN_HEIGHT)
	{
		height = GO_VAN_MIN_HEIGHT;
	}
	Int left = road.lo.x;
	Int right = road.hi.x;
	Int tyre = road.lo.y;
	IRegion2D strip;
	strip.lo.x = left + goVanScale(GO_VAN_BUMPER - GO_VAN_REAR_TYRE, height) - goVanScale(GO_VAN_BUMPER, height);
	strip.lo.y = tyre - goVanScale(GO_VAN_TYRE_BOTTOM - GO_VAN_OUTLINE_TOP + GO_VAN_BOB, height);
	strip.hi.x = right;
	strip.hi.y = tyre;
	Int x0;
	if (goVanStripIsClear(window, strip))
	{
		// the front bumper at the fill end, but the rear tyre no further left than the road's start
		Int bumper = left + goVanScale(GO_VAN_BUMPER - GO_VAN_REAR_TYRE, height);
		if (fillEnd > bumper)
		{
			bumper = fillEnd;
		}
		if (bumper > right - goVanScale(GO_VAN_OUTLINE_RIGHT - GO_VAN_BUMPER, height))
		{
			bumper = right - goVanScale(GO_VAN_OUTLINE_RIGHT - GO_VAN_BUMPER, height);
		}
		x0 = bumper - goVanScale(GO_VAN_BUMPER, height);
	}
	else
	{
		// no room: on the dark track ahead of the fill at its height, rear at the fill end, or no van under the minimum height
		place.inside = TRUE;
		height = fill.hi.y - fill.lo.y;
		if (height < GO_VAN_MIN_HEIGHT)
		{
			place.placed = FALSE;
		}
		right = fill.hi.x;
		tyre = fill.lo.y + goVanScale(GO_VAN_TYRE_BOTTOM - GO_VAN_OUTLINE_TOP, height);
		x0 = fillEnd - goVanScale(GO_VAN_OUTLINE_LEFT, height);
		if (x0 > right - goVanScale(GO_VAN_OUTLINE_RIGHT, height))
		{
			x0 = right - goVanScale(GO_VAN_OUTLINE_RIGHT, height);
		}
	}
	place.height = height;
	place.x0 = x0;
	place.y0 = tyre - goVanScale(GO_VAN_TYRE_BOTTOM, height);
	return place;
}

// TheSuperHackers @feature arcticdolphin 09/11/2026 Where a GO bar art fill should end: at the rear of a van driving inside the bar, so the fill never runs under it.
Int goLoadBarVanFillEnd(GameWindow *window, const IRegion2D &road, const IRegion2D &fill, Int fillEnd)
{
	if (window == nullptr)
	{
		return fillEnd;
	}
	const GoVanPlace place = placeGoVan(window, road, fill, fillEnd);
	if (!place.placed || !place.inside)
	{
		return fillEnd;
	}
	const Int rear = place.x0 + goVanScale(GO_VAN_OUTLINE_LEFT, place.height);
	return rear < fillEnd ? rear : fillEnd;
}

// TheSuperHackers @feature arcticdolphin 09/11/2026 The GO radar van at the leading edge of a GO bar art fill: wheels, puffs and bob run while progress moves and settle when it stalls.
void drawGoLoadBarVan(GameWindow *window, Int progress, const IRegion2D &road, const IRegion2D &fill, Int fillEnd)
{
	const Image *body = TheMappedImageCollection->findImageByName("GOVanBody");
	const Image *dish = TheMappedImageCollection->findImageByName("GOVanDish");
	const Image *puffImage = TheMappedImageCollection->findImageByName("GOVanPuff");
	if (window == nullptr || body == nullptr || dish == nullptr)
	{
		return;
	}

	const GoVanPlace place = placeGoVan(window, road, fill, fillEnd);
	const Bool placed = place.placed;
	const Int height = place.height;
	const Int x0 = place.x0;
	const Int y0 = place.y0;
	const Int frameWidth = goVanScale(GO_VAN_FRAME_WIDTH, height);
	const Int frameHeight = goVanScale(GO_VAN_FRAME_HEIGHT, height);

	// the clock: moving until progress stood still GO_VAN_STALL_MS, then the wiggle fades out
	const UnsignedInt now = timeGetTime();
	GoVanState *state = findGoVanState(window, progress, fillEnd, now);
	state->drawnAt = now;
	if (progress != state->progress)
	{
		state->progress = progress;
		state->changedAt = now;
	}
	const UnsignedInt still = now - state->changedAt;
	const Bool moving = still < GO_VAN_STALL_MS;
	Real envelope = 1.0f;
	if (moving)
	{
		state->frame = (Int)((now / GO_VAN_FRAME_MS) % GO_VAN_WHEEL_FRAMES);
	}
	else
	{
		envelope = 1.0f - (Real)(still - GO_VAN_STALL_MS) / GO_VAN_SETTLE_MS;
		if (envelope < 0.0f)
		{
			envelope = 0.0f;
		}
	}

	// a puff behind the rear wheel per stretch of travel, a few at most at once, none while parked
	const Int travel = fillEnd > state->fillEnd ? fillEnd - state->fillEnd : 0;
	state->fillEnd = fillEnd;
	if (!moving || !placed)
	{
		state->credit = 0.0f;
	}
	else
	{
		Int stretch = goVanScale(GO_VAN_PUFF_TRAVEL, height);
		if (stretch < 1)
		{
			stretch = 1;
		}
		state->credit += (Real)travel / stretch;
		if (state->credit > GO_VAN_PUFF_CREDIT)
		{
			state->credit = (Real)GO_VAN_PUFF_CREDIT;
		}
		if (state->credit >= 1.0f && now - state->spawnedAt >= GO_VAN_PUFF_GAP_MS)
		{
			state->credit -= 1.0f;
			state->spawnedAt = now;
			const UnsignedInt hash = goVanHash(now + state->nextPuff);
			const Int spread = 2 * GO_VAN_PUFF_JITTER + 1;
			GoVanPuff &puff = state->puffs[state->nextPuff];
			puff.born = now;
			puff.life = GO_VAN_PUFF_LIFE_MS + (hash >> 20) % GO_VAN_PUFF_LIFE_SPREAD_MS;
			puff.x = x0 + goVanScale(GO_VAN_PUFF_SPAWN_X + (Int)((hash >> 8) % spread) - GO_VAN_PUFF_JITTER, height);
			puff.y = y0 + goVanScale(GO_VAN_PUFF_SPAWN_Y + (Int)((hash >> 16) % spread) - GO_VAN_PUFF_JITTER, height);
			puff.height = height;
			state->nextPuff = (state->nextPuff + 1) % GO_VAN_PUFFS;
		}
	}

	// the puffs under everything: they drift back and up, grow and fade over their life
	if (puffImage != nullptr && puffImage->getImageWidth() > 0)
	{
		const Real aspect = (Real)puffImage->getImageHeight() / puffImage->getImageWidth();
		for (Int p = 0; p < GO_VAN_PUFFS; ++p)
		{
			const GoVanPuff &puff = state->puffs[p];
			if (puff.life == 0 || now - puff.born >= puff.life)
			{
				continue;
			}
			const Real t = (Real)(now - puff.born) / puff.life;
			const Int start = goVanScale(GO_VAN_PUFF_START, puff.height);
			const Real size = start + (goVanScale(GO_VAN_PUFF_END, puff.height) - start) * t;
			const Int cx = (Int)(puff.x - goVanScale(GO_VAN_PUFF_DRIFT_X, puff.height) * t);
			const Int cy = (Int)(puff.y - goVanScale(GO_VAN_PUFF_DRIFT_Y, puff.height) * t);
			const Int w = (Int)size;
			const Int h = (Int)(size * aspect);
			const Int alpha = (Int)(GO_VAN_PUFF_ALPHA * (1.0f - t));
			TheDisplay->drawImage(puffImage, cx - w / 2, cy - h / 2, cx - w / 2 + w, cy - h / 2 + h, GameMakeColor(255, 255, 255, alpha));
		}
	}
	if (!placed)
	{
		return;
	}

	const Image *wheels = TheMappedImageCollection->findImageByName(GO_VAN_WHEEL_IMAGES[state->frame]);
	if (wheels == nullptr)
	{
		return;
	}

	// the body bobs on its suspension and the dish follows a beat later, a pixel sideways; the wheels stay on the road
	const Int bob = REAL_TO_INT_FLOOR(GO_VAN_BOB * height / (Real)GO_VAN_SPAN * envelope * goVanWave(now) + 0.5f);
	const Int sway = REAL_TO_INT_FLOOR(envelope * goVanWave(now - GO_VAN_DISH_LAG_MS) + 0.5f);
	TheDisplay->drawImage(body, x0, y0 + bob, x0 + frameWidth, y0 + bob + frameHeight);
	TheDisplay->drawImage(dish, x0 + sway, y0 + bob, x0 + sway + frameWidth, y0 + bob + frameHeight);
	TheDisplay->drawImage(wheels, x0, y0, x0 + frameWidth, y0 + frameHeight);
}

// TheSuperHackers @tweak arcticdolphin 09/11/2026 The GO shell load bar: the kit frame, then one stretched bronze fill a line inside it on every side.
static void drawGoShellLoadBar( GameWindow *window, WinInstanceData *instData )
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

	// the frame line as winDrawThreePieceImage draws the border rows, and a gap of the same width inside it
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
	const Int drawWidth = fillWidth * progress / 100;

	// TheSuperHackers @feature arcticdolphin 09/11/2026 The radar van drives on the frame at the end of the fill; inside the bar it rides the track ahead of the fill, so the fill stops at its rear.
	const Bool goVan = isGoLoadBarImage(fill);
	IRegion2D road;
	road.lo.x = x;
	road.lo.y = y;
	road.hi.x = x + width;
	road.hi.y = y + height;
	IRegion2D fillArea;
	fillArea.lo.x = x + inset;
	fillArea.lo.y = y + inset;
	fillArea.hi.x = x + inset + fillWidth;
	fillArea.hi.y = y + inset + fillHeight;
	Int fillStop = x + inset + drawWidth;
	if (goVan)
	{
		fillStop = goLoadBarVanFillEnd(window, road, fillArea, fillStop);
	}
	if (fillStop > x + inset)
	{
		TheDisplay->drawImage(fill, x + inset, y + inset, fillStop, y + inset + fillHeight);
	}

	if (goVan)
	{
		drawGoLoadBarVan(window, progress, road, fillArea, x + inset + drawWidth);
	}
}

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
void positionStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void updateMapStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], Bool onLoadScreen = FALSE );
void positionAdditionalImages( MapMetaData *mmd, GameWindow *mapWindow, Bool force);

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
}

LoadScreen::~LoadScreen()
{
	if(m_loadScreen)
		TheWindowManager->winDestroy( m_loadScreen );
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
	m_progressBar = nullptr;
	m_percent = nullptr;
	m_videoStream = nullptr;
	m_videoBuffer = nullptr;
	m_objectiveWin = nullptr;
	for(Int i = 0; i < MAX_OBJECTIVE_LINES; ++i)
		m_objectiveLines[i] = nullptr;

}

SinglePlayerLoadScreen::~SinglePlayerLoadScreen()
{
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

	if( frame == STATE_BEGIN_BRIEFING)
	{
		// add sound support here
		TheAudio->friend_forcePlayAudioEventRTS(&TheCampaignManager->getCurrentMission()->m_briefingVoice);
	}

	if( frame == STATE_BEGIN_ANIMATING_TEXT)
	{
		m_objectiveWin->winHide(FALSE);
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
			UnicodeString text = GadgetStaticTextGetText(m_objectiveLines[m_currentObjectiveLine]);
			text.concat(wChar);
			GadgetStaticTextSetText(m_objectiveLines[m_currentObjectiveLine], text);

		}
		m_currentObjectiveLineCharacter++;
	}
	switch (frame) {

	case STATE_SHOW_LOCATION:
		m_location->winHide(FALSE);
		break;
	case STATE_SHOW_CAMEO_1:
		m_unitDesc[0]->winHide(FALSE);
		break;
	case STATE_HIDE_CAMEO_1:
		m_unitDesc[0]->winHide(TRUE);
		break;
	case STATE_SHOW_CAMEO_2:
		m_unitDesc[1]->winHide(FALSE);
		break;
	case STATE_HIDE_CAMEO_2:
		m_unitDesc[1]->winHide(TRUE);
		break;
	case STATE_SHOW_CAMEO_3:
		m_unitDesc[2]->winHide(FALSE);
		break;
	case STATE_HIDE_CAMEO_3:
		m_unitDesc[2]->winHide(TRUE);
		break;
	}

}
/*
	static Bool on = FALSE;
	static ICoord2D startPos, endPos;
	enum{
		STATE_BEGIN = 275,
		STATE_BEGIN_ANIM = 290,
		STATE_ANIM_CAMEO1 = 300,
		STATE_ANIM_CAMEO1_TRANSITION_CAMEO2 = 350,
		STATE_ANIM_CAMEO2 = 400,
		STATE_ANIM_CAMEO2_TRANSITION_CAMEO3 = 450,
		STATE_ANIM_CAMEO3 = 500,
		STATED_END_ANIM = 550,
		STATE_END = 800
	};
	if(frame < STATE_BEGIN)
		return;
	else if(frame == STATE_BEGIN )
	{
		m_cameoWindow1->winHide(FALSE);
		m_cameoWindow2->winHide(FALSE);
		m_cameoWindow3->winHide(FALSE);
		m_cameoFrame->winHide(FALSE);
	}
	else if( frame == STATE_ANIM_CAMEO1)
	{
		m_cameoWindow1->winEnable(TRUE);
		GadgetStaticTextSetText(m_cameoText, TheGameText->fetch(TheCampaignManager->getCurrentMission()->m_cameoImageName[0]));
		//save of positions
	}
	else if( frame == STATE_ANIM_CAMEO1_TRANSITION_CAMEO2)
	{
		m_cameoWindow1->winEnable(FALSE);
		GadgetStaticTextSetText(m_cameoText, UnicodeString::TheEmptyString);
		ICoord2D tempPos;
		Int xOffset;
		m_cameoFrame->winGetPosition(&startPos.x, &startPos.y);
		m_cameoWindow1->winGetPosition(&tempPos.x, &tempPos.y);
		xOffset = tempPos.x - startPos.x;
		m_cameoWindow2->winGetPosition(&endPos.x, &endPos.y);
		endPos.x = endPos.x - xOffset;
		endPos.y = startPos.y;

	}
	else if( frame > STATE_ANIM_CAMEO1_TRANSITION_CAMEO2 && frame < STATE_ANIM_CAMEO2)
	{

		//extrapolate between start and end pos
		Real percent = INT_TO_REAL((frame - STATE_ANIM_CAMEO1_TRANSITION_CAMEO2)) / (STATE_ANIM_CAMEO2 - STATE_ANIM_CAMEO1_TRANSITION_CAMEO2);
		m_cameoFrame->winSetPosition(startPos.x + (endPos.x - startPos.x) * percent, endPos.y);
	}
	else if( frame == STATE_ANIM_CAMEO2 )
	{
		m_cameoWindow2->winEnable(TRUE);
		m_cameoFrame->winSetPosition(endPos.x, endPos.y);
		GadgetStaticTextSetText(m_cameoText, TheGameText->fetch(TheCampaignManager->getCurrentMission()->m_cameoImageName[1]));
	}
	else if( frame == STATE_ANIM_CAMEO2_TRANSITION_CAMEO3)
	{
		m_cameoWindow2->winEnable(FALSE);
		GadgetStaticTextSetText(m_cameoText, UnicodeString::TheEmptyString);
		ICoord2D tempPos;
		Int xOffset;
		m_cameoFrame->winGetPosition(&startPos.x, &startPos.y);
		m_cameoWindow2->winGetPosition(&tempPos.x, &tempPos.y);
		xOffset = tempPos.x - startPos.x;
		m_cameoWindow3->winGetPosition(&endPos.x, &endPos.y);
		endPos.x = endPos.x - xOffset;
		endPos.y = startPos.y;

	}
	else if( frame > STATE_ANIM_CAMEO2_TRANSITION_CAMEO3 && frame < STATE_ANIM_CAMEO3)
	{

		//extrapolate between start and end pos
		Real percent = INT_TO_REAL((frame - STATE_ANIM_CAMEO2_TRANSITION_CAMEO3)) / (STATE_ANIM_CAMEO3 - STATE_ANIM_CAMEO2_TRANSITION_CAMEO3);
		m_cameoFrame->winSetPosition(startPos.x + (endPos.x - startPos.x) * percent, endPos.y);
	}
	else if( frame == STATE_ANIM_CAMEO3 )
	{
		m_cameoFrame->winSetPosition(endPos.x, endPos.y);
		m_cameoWindow3->winEnable(TRUE);
		GadgetStaticTextSetText(m_cameoText, TheGameText->fetch(TheCampaignManager->getCurrentMission()->m_cameoImageName[2]));
	}
	else if( frame ==STATED_END_ANIM)
	{
		m_cameoWindow3->winEnable(FALSE);
		GadgetStaticTextSetText(m_cameoText, UnicodeString::TheEmptyString);
		m_cameoFrame->winHide(TRUE);

	}
}*/

void SinglePlayerLoadScreen::init( GameInfo *game )
{
	//No music in SinglePlayerLoadScreen

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( "Menus/SinglePlayerLoadScreen.wnd" );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the single player loadscreen"));
	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();
//	Mission *mission = TheCampaignManager->getCurrentMission();
	// Store the pointer to the progress bar on the loadscreen
	m_progressBar = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "SinglePlayerLoadScreen.wnd:ProgressLoad" ));
	DEBUG_ASSERTCRASH(m_progressBar, ("Can't initialize the progressbar for the single player loadscreen"));
	GadgetProgressBarSetProgress(m_progressBar, 0 );

	m_percent = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "SinglePlayerLoadScreen.wnd:Percent" ));
	DEBUG_ASSERTCRASH(m_percent, ("Can't initialize the m_percent for the single player loadscreen"));
	GadgetStaticTextSetText(m_percent,L"0%");
	m_percent->winHide(TRUE);

	m_objectiveWin = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "SinglePlayerLoadScreen.wnd:ObjectivesWin" ));
	DEBUG_ASSERTCRASH(m_objectiveWin, ("Can't initialize the m_objectiveWin for the single player loadscreen"));
	m_objectiveWin->winHide(TRUE);


	Mission *mission = TheCampaignManager->getCurrentMission();
	AsciiString lineName;
	Int i = 0;
	for(; i < MAX_OBJECTIVE_LINES; ++i)
	{
		lineName.format("SinglePlayerLoadScreen.wnd:StaticTextLine%d",i);
		m_objectiveLines[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( lineName ));
		DEBUG_ASSERTCRASH(m_objectiveLines[i], ("Can't initialize the m_objectiveLines[%d] for the single player loadscreen", i));
		GadgetStaticTextSetText(m_objectiveLines[i],UnicodeString::TheEmptyString);

		// translate the objective lines
		if(mission->m_missionObjectivesLabel[i].isNotEmpty())
			m_unicodeObjectiveLines[i] = TheGameText->fetch(mission->m_missionObjectivesLabel[i]);
	}

	for(i = 0; i < MAX_DISPLAYED_UNITS; ++i)
	{
		lineName.format("SinglePlayerLoadScreen.wnd:StaticTextCameoText%d",i);
		m_unitDesc[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( lineName ));
		DEBUG_ASSERTCRASH(m_unitDesc[i], ("Can't initialize the m_objectiveLines[%d] for the single player loadscreen", i));
		GadgetStaticTextSetText(m_unitDesc[i],TheGameText->fetch(mission->m_unitNames[i]));
		m_unitDesc[i]->winHide(TRUE);
	}
	m_location = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "SinglePlayerLoadScreen.wnd:StaticTextCameoText3" ));
	DEBUG_ASSERTCRASH(m_location, ("Can't initialize the m_objectiveWin for the single player loadscreen"));
	m_location->winHide(TRUE);
	GadgetStaticTextSetText(m_location, TheGameText->fetch(mission->m_locationNameLabel));



	m_currentObjectiveLine = 0;
	m_currentObjectiveWidthOffset = 0;
	m_currentObjectiveLineCharacter = 0;
	m_finishedObjectiveText = FALSE;
/*
	m_cameoWindow1 = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "SinglePlayerLoadScreen.wnd:WindowCameo1" ));
	DEBUG_ASSERTCRASH(m_cameoWindow1, ("Can't initialize the m_cameoWindow1 for the single player loadscreen"));
	m_cameoWindow1->winHide(TRUE);
	m_cameoWindow1->winEnable(FALSE);
	m_cameoWindow1->winSetEnabledImage(0, mission->m_cameoImage[0]);
	m_cameoWindow1->winSetDisabledImage(0, mission->m_cameoDisabledImage[0]);

	m_cameoWindow2 = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "SinglePlayerLoadScreen.wnd:WindowCameo2" ));
	DEBUG_ASSERTCRASH(m_cameoWindow2, ("Can't initialize the m_cameoWindow2 for the single player loadscreen"));
	m_cameoWindow2->winHide(TRUE);
	m_cameoWindow2->winEnable(FALSE);
	m_cameoWindow2->winSetEnabledImage(0, mission->m_cameoImage[1]);
	m_cameoWindow2->winSetDisabledImage(0, mission->m_cameoDisabledImage[1]);

	m_cameoWindow3 = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "SinglePlayerLoadScreen.wnd:WindowCameo3" ));
	DEBUG_ASSERTCRASH(m_cameoWindow3, ("Can't initialize the m_cameoWindow3 for the single player loadscreen"));
	m_cameoWindow3->winHide(TRUE);
	m_cameoWindow3->winEnable(FALSE);
	m_cameoWindow3->winSetEnabledImage(0, mission->m_cameoImage[2]);
	m_cameoWindow3->winSetDisabledImage(0, mission->m_cameoDisabledImage[2]);

	m_headMovie = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "SinglePlayerLoadScreen.wnd:WindowHead" ));
	DEBUG_ASSERTCRASH(m_headMovie, ("Can't initialize the m_headMovie for the single player loadscreen"));
	m_headMovie->winHide(TRUE);
	m_cameoFrame = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "SinglePlayerLoadScreen.wnd:WindowHiliteCameo" ));
	DEBUG_ASSERTCRASH(m_cameoFrame, ("Can't initialize the m_cameoFrame for the single player loadscreen"));
	m_cameoFrame->winHide(TRUE);
	m_cameoText = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "SinglePlayerLoadScreen.wnd:StaticTextCameoText" ));
	DEBUG_ASSERTCRASH(m_cameoText, ("Can't initialize the m_cameoText for the single player loadscreen"));

*/
	m_ambientLoop.setEventName("LoadScreenAmbient");
	// create the new stream
	m_videoStream = TheVideoPlayer->open( TheCampaignManager->getCurrentMission()->m_movieLabel );
	if ( m_videoStream == nullptr )
	{
		m_percent->winHide(TRUE);
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
	GameWindow *backgroundWin = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "SinglePlayerLoadScreen.wnd:ParentSinglePlayerLoadScreen" ));
	if (campaignName.compareNoCase("USA") == 0)
	{
		if (const Image *image = TheMappedImageCollection->findImageByName("MissionLoad_USA"))
		{
			backgroundWin->winSetEnabledImage( 0, image);
		}
		if (const Image *image = TheMappedImageCollection->findImageByName("LoadingBar_ProgressCenter2"))
		{
			m_progressBar->winSetEnabledImage( 6, image );
		}
	}
	else if (campaignName.compareNoCase("GLA") == 0)
	{
		if (const Image *image = TheMappedImageCollection->findImageByName("MissionLoad_GLA"))
		{
			backgroundWin->winSetEnabledImage( 0, image );
		}
		if (const Image *image = TheMappedImageCollection->findImageByName("LoadingBar_ProgressCenter3"))
		{
			m_progressBar->winSetEnabledImage( 6, image );
		}
	}
	else if (campaignName.compareNoCase("China") == 0)
	{
		if (const Image *image = TheMappedImageCollection->findImageByName("MissionLoad_China"))
		{
			backgroundWin->winSetEnabledImage( 0, image );
		}
		if (const Image *image = TheMappedImageCollection->findImageByName("LoadingBar_ProgressCenter1"))
		{
			m_progressBar->winSetEnabledImage( 6, image );
		}
	}
	// else leave the default background screen
	// TheSuperHackers @tweak arcticdolphin 09/11/2026 With the GO bar art the campaign bar stays bronze instead of taking the faction colour.
	if (const Image *goBarImage = findGoLoadBarImage())
	{
		m_progressBar->winSetEnabledImage( 6, goBarImage );
	}


	if(TheGameLODManager && TheGameLODManager->didMemPass())
	{
		// TheSuperHackers @bugfix Originally this movie render loop stopped rendering when the game window was inactive.
		// This either skipped the movie or caused decompression artifacts. Now the video just keeps playing until it done.

		Int progressUpdateCount = m_videoStream->frameCount() / FRAME_FUDGE_ADD;
		Int shiftedPercent = -FRAME_FUDGE_ADD + 1;
		while (!m_videoStream->isPlaybackComplete())
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

			if(m_videoBuffer)
				m_loadScreen->winGetInstanceData()->setVideoBuffer(m_videoBuffer);
			if(m_videoStream->frameIndex() % progressUpdateCount == 0)
			{
				shiftedPercent++;
				if(shiftedPercent >0)
					shiftedPercent = 0;
				Int percent = (shiftedPercent + FRAME_FUDGE_ADD)/1.3;
				UnicodeString per;
				per.format(L"%d%%",percent);
				TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
				GadgetProgressBarSetProgress(m_progressBar, percent);
				GadgetStaticTextSetText(m_percent, per);

			}
			TheWindowManager->update();

			// redraw all views, update the GUI
			TheDisplay->draw();
		}

#if !RTS_GENERALS
		// let the background image show through
		m_videoStream->close();
		m_videoStream = nullptr;
		m_loadScreen->winGetInstanceData()->setVideoBuffer( nullptr );
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
				m_loadScreen->winGetInstanceData()->setVideoBuffer(m_videoBuffer);

		m_objectiveWin->winHide(FALSE);
		for(i = 0; i < MAX_DISPLAYED_UNITS; ++i)
			m_unitDesc[i]->winHide(FALSE);
		m_location->winHide(FALSE);

		// Audio was choppy so, I chopped it out!
		TheAudio->friend_forcePlayAudioEventRTS(&TheCampaignManager->getCurrentMission()->m_briefingVoice);

		for(Int i = 0; i < MAX_OBJECTIVE_LINES; ++i)
		{
			GadgetStaticTextSetText(m_objectiveLines[i], m_unicodeObjectiveLines[i]);
		}
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
			GadgetProgressBarSetProgress(m_progressBar, fudgeFactor);

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
	m_percent->winHide(TRUE);
	m_ambientLoopHandle = TheAudio->addAudioEvent(&m_ambientLoop);

}

void SinglePlayerLoadScreen::reset()
{
 setLoadScreen(nullptr);
 m_progressBar = nullptr;
}

void SinglePlayerLoadScreen::update( Int percent )
{
	percent = (percent + FRAME_FUDGE_ADD)/1.3;
	UnicodeString per;
	per.format(L"%d%%",percent);
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
	GadgetProgressBarSetProgress(m_progressBar, percent);
	GadgetStaticTextSetText(m_percent, per);

	// Do this last!
	LoadScreen::update( percent );
}

void SinglePlayerLoadScreen::setProgressRange( Int min, Int max )
{

}

// ChallengeLoadScreen Class ///////////////////////////////////////////////
//-----------------------------------------------------------------------------
ChallengeLoadScreen::ChallengeLoadScreen()
{
	m_progressBar = nullptr;
	m_videoStream = nullptr;
	m_videoBuffer = nullptr;

	m_bioNameLeft = nullptr;
	m_bioAgeLeft = nullptr;
	m_bioBirthplaceLeft = nullptr;
	m_bioStrategyLeft = nullptr;
	m_bioBigNameEntryLeft = nullptr;
	m_bioNameEntryLeft = nullptr;
	m_bioAgeEntryLeft = nullptr;
	m_bioBirthplaceEntryLeft = nullptr;
	m_bioStrategyEntryLeft = nullptr;
	m_bioBigNameEntryRight = nullptr;
	m_bioNameRight = nullptr;
	m_bioAgeRight = nullptr;
	m_bioBirthplaceRight = nullptr;
	m_bioStrategyRight = nullptr;
	m_bioNameEntryRight = nullptr;
	m_bioAgeEntryRight = nullptr;
	m_bioBirthplaceEntryRight = nullptr;
	m_bioStrategyEntryRight = nullptr;

	m_portraitLeft = nullptr;
	m_portraitRight = nullptr;
	m_portraitMovieLeft = nullptr;
	m_portraitMovieRight = nullptr;

//	m_overlayReticleCrosshairs = nullptr;
//	m_overlayReticleCircleLineOuter = nullptr;
//	m_overlayReticleCircleLineInner = nullptr;
	m_overlayReticleCircleAlphaOuter = nullptr;
	m_overlayReticleCircleAlphaInner = nullptr;
	m_overlayVsBackdrop = nullptr;
	m_overlayVs = nullptr;
	m_wndVideoManager = nullptr;
}

ChallengeLoadScreen::~ChallengeLoadScreen()
{
	delete m_videoBuffer;

	if ( m_videoStream )
	{
		m_videoStream->close();
	}

	delete m_wndVideoManager;

	TheAudio->removeAudioEvent( m_ambientLoopHandle );
}

// accepts the number of chars to advance, the window we're concerned with, the total text for final display, and the current position of the readout
// returns the updated position of the readout
Int updateTeletypeText( Int num_chars, GameWindow* window, UnicodeString full_text, Int current_text_pos )
{
	DEBUG_ASSERTCRASH(window, ("No window for teletype text update"));
	UnicodeString currentText = GadgetStaticTextGetText(window);
	WideChar wChar;
	for (Int i = 0; i < num_chars; i++)
	{
		if (current_text_pos < full_text.getLength())
		{
			wChar = full_text.getCharAt(current_text_pos);
			currentText.concat(wChar);
			current_text_pos++;
		}
	}
	GadgetStaticTextSetText(window, currentText);
	return current_text_pos;
}

void ChallengeLoadScreen::activatePieces( Int frame, const GeneralPersona *generalPlayer, const GeneralPersona *generalOpponent )
{
	static Int textPosBigNameRight = 0;
	static Int textPosNameRight = 0;
	static Int textPosAgeRight = 0;
	static Int textPosBirthplaceRight = 0;
	static Int textPosStrategyRight = 0;
	static Int textPosBigNameLeft = 0;
	static Int textPosNameLeft = 0;
	static Int textPosAgeLeft = 0;
	static Int textPosBirthplaceLeft = 0;
	static Int textPosStrategyLeft = 0;

	AudioEventRTS eventLeftGeneral( generalPlayer->getNameSound() );
	AudioEventRTS eventVS("Taunts_GCAnnouncer12");
	AudioEventRTS eventRightGeneral( generalOpponent->getNameSound() );

	switch (frame)
	{
		case FRAME_TITLES_START:
			m_bioNameLeft->winHide(FALSE);
//			m_bioAgeLeft->winHide(FALSE);
			m_bioBirthplaceLeft->winHide(FALSE);
			m_bioStrategyLeft->winHide(FALSE);
			m_bioNameRight->winHide(FALSE);
//			m_bioAgeRight->winHide(FALSE);
			m_bioBirthplaceRight->winHide(FALSE);
			m_bioStrategyRight->winHide(FALSE);

			break;
		case FRAME_TELETYPE_START:
			// reinit the statics for each new load screen
			textPosBigNameRight = 0;
			textPosNameRight = 0;
			textPosAgeRight = 0;
			textPosBirthplaceRight = 0;
			textPosStrategyRight = 0;
			textPosBigNameLeft = 0;
			textPosNameLeft = 0;
			textPosAgeLeft = 0;
			textPosBirthplaceLeft = 0;
			textPosStrategyLeft = 0;

			m_bioBigNameEntryLeft->winHide(FALSE);
			m_bioNameEntryLeft->winHide(FALSE);
//			m_bioAgeEntryLeft->winHide(FALSE);
			m_bioBirthplaceEntryLeft->winHide(FALSE);
			m_bioStrategyEntryLeft->winHide(FALSE);
			GadgetStaticTextSetText( m_bioBigNameEntryLeft, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioNameEntryLeft, UnicodeString::TheEmptyString );
//			GadgetStaticTextSetText( m_bioAgeEntryLeft, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioBirthplaceEntryLeft, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioStrategyEntryLeft, UnicodeString::TheEmptyString );

			m_bioBigNameEntryRight->winHide(FALSE);
			m_bioNameEntryRight->winHide(FALSE);
//			m_bioAgeEntryRight->winHide(FALSE);
			m_bioBirthplaceEntryRight->winHide(FALSE);
			m_bioStrategyEntryRight->winHide(FALSE);
			GadgetStaticTextSetText( m_bioBigNameEntryRight, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioNameEntryRight, UnicodeString::TheEmptyString );
//			GadgetStaticTextSetText( m_bioAgeEntryRight, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioBirthplaceEntryRight, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioStrategyEntryRight, UnicodeString::TheEmptyString );
			break;
		case FRAME_PORTRAITS_START:

			m_wndVideoManager->playMovie( m_portraitMovieLeft, generalPlayer->getPortraitMovieLeftName(), WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);
			m_wndVideoManager->playMovie( m_portraitMovieRight, generalOpponent->getPortraitMovieRightName(), WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);
			m_portraitMovieLeft->winHide(FALSE);
			m_portraitMovieRight->winHide(FALSE);

			TheAudio->addAudioEvent( &eventLeftGeneral );

			break;
		case FRAME_OUTER_CIRCLE_LINE_SHOW:
//			m_overlayReticleCircleLineOuter->winHide(FALSE);
			break;
		case FRAME_INNER_CIRCLE_LINE_SHOW:
//			m_overlayReticleCircleLineInner->winHide(FALSE);
			break;
		case FRAME_OUTER_CIRCLE_ALPHA_SHOW:
			m_overlayReticleCircleAlphaOuter->winHide(FALSE);
			break;
		case FRAME_INNER_CIRCLE_ALPHA_SHOW:
			m_overlayReticleCircleAlphaInner->winHide(FALSE);
			break;
		case FRAME_OUTER_CIRCLE_LINE_HIDE:
//			m_overlayReticleCircleLineOuter->winHide(TRUE);
			break;
		case FRAME_INNER_BACKDROP_ALPHA_SHOW:
			m_overlayVsBackdrop->winHide(FALSE);
			break;
		case FRAME_INNER_CIRCLE_LINE_HIDE:
//			m_overlayReticleCircleLineInner->winHide(TRUE);
			break;
		case FRAME_VS_ANIM_START:
			// it's time to start the overlay movie
//					m_overlayVsBackdrop->winSetEnabledImage( 0, TheMappedImageCollection->findImageByFilename("))
			m_overlayVsBackdrop->winHide(FALSE);
			m_overlayVs->winHide(FALSE);
			m_wndVideoManager->playMovie( m_overlayVs, "VSSmall", WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);

			// "Verses"
			TheAudio->addAudioEvent( &eventVS );

			break;
		case FRAME_RIGHT_VOICE:
			TheAudio->addAudioEvent( &eventRightGeneral );

			break;
	}

	// update the teletype readout
	if (frame > FRAME_TELETYPE_START && (frame % TELETYPE_UPDATE_FREQ) == 0)
	{
		textPosNameLeft = updateTeletypeText( 1, m_bioNameEntryLeft, TheGameText->fetch(generalPlayer->getBioName()), textPosNameLeft);
		textPosBigNameLeft = updateTeletypeText( 1, m_bioBigNameEntryLeft, TheGameText->fetch(generalPlayer->getBioName()), textPosBigNameLeft);
//		textPosAgeLeft = updateTeletypeText( 1, m_bioAgeEntryLeft, TheGameText->fetch(generalPlayer->getBioDOB()), textPosAgeLeft);
		textPosBirthplaceLeft = updateTeletypeText( 1, m_bioBirthplaceEntryLeft, TheGameText->fetch(generalPlayer->getBioRank()), textPosBirthplaceLeft);
		textPosStrategyLeft = updateTeletypeText( 1, m_bioStrategyEntryLeft, TheGameText->fetch(generalPlayer->getBioStrategy()), textPosStrategyLeft);

		textPosNameRight = updateTeletypeText( 1, m_bioNameEntryRight, TheGameText->fetch(generalOpponent->getBioName()), textPosNameRight);
		textPosBigNameRight = updateTeletypeText( 1, m_bioBigNameEntryRight, TheGameText->fetch(generalOpponent->getBioName()), textPosBigNameRight);
//		textPosAgeRight = updateTeletypeText( 1, m_bioAgeEntryRight, TheGameText->fetch(generalOpponent->getBioDOB()), textPosAgeRight);
		textPosBirthplaceRight = updateTeletypeText( 1, m_bioBirthplaceEntryRight, TheGameText->fetch(generalOpponent->getBioRank()), textPosBirthplaceRight);
		textPosStrategyRight = updateTeletypeText( 1, m_bioStrategyEntryRight, TheGameText->fetch(generalOpponent->getBioStrategy()), textPosStrategyRight);
	}
}

void ChallengeLoadScreen::activatePiecesMinSpec(const GeneralPersona *generalPlayer, const GeneralPersona *generalOpponent)
{
	m_bioNameLeft->winHide(FALSE);
//	m_bioAgeLeft->winHide(FALSE);
	m_bioBirthplaceLeft->winHide(FALSE);
	m_bioStrategyLeft->winHide(FALSE);
	m_bioNameRight->winHide(FALSE);
//	m_bioAgeRight->winHide(FALSE);
	m_bioBirthplaceRight->winHide(FALSE);
	m_bioStrategyRight->winHide(FALSE);
	m_bioBigNameEntryLeft->winHide(FALSE);
	m_bioNameEntryLeft->winHide(FALSE);
//	m_bioAgeEntryLeft->winHide(FALSE);
	m_bioBirthplaceEntryLeft->winHide(FALSE);
	m_bioStrategyEntryLeft->winHide(FALSE);
	GadgetStaticTextSetText( m_bioBigNameEntryLeft, TheGameText->fetch(generalPlayer->getBioName()) );
	GadgetStaticTextSetText( m_bioNameEntryLeft, TheGameText->fetch(generalPlayer->getBioName()) );
//	GadgetStaticTextSetText( m_bioAgeEntryLeft, TheGameText->fetch(generalPlayer->getBioDOB()) );
	GadgetStaticTextSetText( m_bioBirthplaceEntryLeft, TheGameText->fetch(generalPlayer->getBioRank()) );
	GadgetStaticTextSetText( m_bioStrategyEntryLeft, TheGameText->fetch(generalPlayer->getBioStrategy()) );
	m_bioBigNameEntryRight->winHide(FALSE);
	m_bioNameEntryRight->winHide(FALSE);
//	m_bioAgeEntryRight->winHide(FALSE);
	m_bioBirthplaceEntryRight->winHide(FALSE);
	m_bioStrategyEntryRight->winHide(FALSE);
	GadgetStaticTextSetText( m_bioBigNameEntryRight, TheGameText->fetch(generalOpponent->getBioName()) );
	GadgetStaticTextSetText( m_bioNameEntryRight, TheGameText->fetch(generalOpponent->getBioName()) );
//	GadgetStaticTextSetText( m_bioAgeEntryRight, TheGameText->fetch(generalOpponent->getBioDOB()) );
	GadgetStaticTextSetText( m_bioBirthplaceEntryRight, TheGameText->fetch(generalOpponent->getBioRank()) );
	GadgetStaticTextSetText( m_bioStrategyEntryRight, TheGameText->fetch(generalOpponent->getBioStrategy()) );
	m_portraitLeft->winSetEnabledImage(0, generalPlayer->getBioPortraitLarge() );
	m_portraitRight->winSetEnabledImage(0, generalOpponent->getBioPortraitLarge() );
	m_portraitLeft->winHide(FALSE);
	m_portraitRight->winHide(FALSE);
	m_overlayReticleCircleAlphaOuter->winHide(FALSE);
	m_overlayReticleCircleAlphaInner->winHide(FALSE);
	m_overlayVsBackdrop->winHide(FALSE);
	m_overlayVsBackdrop->winHide(FALSE);
	m_overlayVs->winHide(FALSE);
	m_wndVideoManager->playMovie( m_overlayVs, "VSSmall", WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);
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

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( "Menus/ChallengeLoadScreen.wnd" );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the single player loadscreen"));
	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();

	// Store the pointer to the progress bar on the loadscreen
	m_progressBar = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:ProgressLoad" ));
	DEBUG_ASSERTCRASH(m_progressBar, ("Can't initialize the progressbar for the single player loadscreen"));
	GadgetProgressBarSetProgress(m_progressBar, 0 );

	m_ambientLoop.setEventName("LoadScreenAmbient");

	// create the new background video stream
	m_videoStream = TheVideoPlayer->open( TheCampaignManager->getCurrentMission()->m_movieLabel );

	// Create the new buffer
	m_videoBuffer = TheDisplay->createVideoBuffer();
	if (m_videoBuffer == nullptr || !m_videoBuffer->allocate(	m_videoStream->width(), m_videoStream->height() ))
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

	// init overlays
	NameKeyType namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:PortraitLeft");
	m_portraitLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:PortraitRight");
	m_portraitRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );

	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:PortraitMovieLeft");
	m_portraitMovieLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:PortraitMovieRight");
	m_portraitMovieRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );

//	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:ReticleCrosshairs");
//	m_overlayReticleCrosshairs = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
/*
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:OuterCircleLine");
	m_overlayReticleCircleLineOuter = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:InnerCircleLine");
	m_overlayReticleCircleLineInner = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
*/
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:CircleAlphaOuter");
	m_overlayReticleCircleAlphaOuter = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:CircleAlphaInner");
	m_overlayReticleCircleAlphaInner = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:VersusBackdrop");
	m_overlayVsBackdrop = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:OverlayVs");
	m_overlayVs = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );

	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioNameLeft");
	m_bioNameLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
//	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioDOBLeft");
//	m_bioAgeLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioBirthplaceLeft");
	m_bioBirthplaceLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioStrategyLeft");
	m_bioStrategyLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BigNameEntryLeft");
	m_bioBigNameEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioNameEntryLeft");
	m_bioNameEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
//	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioDOBEntryLeft");
//	m_bioAgeEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioBirthplaceEntryLeft");
	m_bioBirthplaceEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioStrategyEntryLeft");
	m_bioStrategyEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioNameRight");
	m_bioNameRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
//	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioDOBRight");
//	m_bioAgeRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioBirthplaceRight");
	m_bioBirthplaceRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioStrategyRight");
	m_bioStrategyRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BigNameEntryRight");
	m_bioBigNameEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioNameEntryRight");
	m_bioNameEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
//	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioDOBEntryRight");
//	m_bioAgeEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioBirthplaceEntryRight");
	m_bioBirthplaceEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( "ChallengeLoadScreen.wnd:BioStrategyEntryRight");
	m_bioStrategyEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );


	// make sure reticle stuff starts out hidden
//	m_overlayReticleCircleLineOuter->winHide(TRUE);
//	m_overlayReticleCircleLineInner->winHide(TRUE);
	m_overlayReticleCircleAlphaOuter->winHide(TRUE);
	m_overlayReticleCircleAlphaInner->winHide(TRUE);
	m_overlayVsBackdrop->winHide(TRUE);
	m_overlayVs->winHide(TRUE);

	m_wndVideoManager = NEW WindowVideoManager;
	m_wndVideoManager->init();

	if(TheGameLODManager && TheGameLODManager->didMemPass())
	{
		// TheSuperHackers @bugfix Originally this movie render loop stopped rendering when the game window was inactive.
		// This either skipped the movie or caused decompression artifacts. Now the video just keeps playing until it done.

		Int progressUpdateCount = m_videoStream->frameCount() / FRAME_FUDGE_ADD;
		Int shiftedPercent = -FRAME_FUDGE_ADD + 1;
		while (!m_videoStream->isPlaybackComplete())
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

			if(m_videoBuffer)
				m_loadScreen->winGetInstanceData()->setVideoBuffer(m_videoBuffer);

			Int frame = m_videoStream->frameIndex();
			if(frame % progressUpdateCount == 0)
			{
				shiftedPercent++;
				if(shiftedPercent >0)
					shiftedPercent = 0;
				Int percent = (shiftedPercent + FRAME_FUDGE_ADD)/1.3;
				UnicodeString per;
				per.format(L"%d%%",percent);
				TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
				GadgetProgressBarSetProgress(m_progressBar, percent);
			}
			TheWindowManager->update();

			activatePieces(frame, generalPlayer, generalOpponent);
			m_wndVideoManager->update();

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
			m_loadScreen->winGetInstanceData()->setVideoBuffer(m_videoBuffer);

		activatePiecesMinSpec(generalPlayer, generalOpponent);

		Int delay = mission->m_voiceLength * 1000;
		Int begin = timeGetTime();
		Int currTime = begin;
		Int fudgeFactor = 0;
		while(begin + delay > currTime )
		{
			fudgeFactor = 30 * ((currTime - begin)/ INT_TO_REAL(delay ));
			GadgetProgressBarSetProgress(m_progressBar, fudgeFactor);

			if (GameClient::isMovieAbortRequested())
			{
				break;
			}

			TheWindowManager->update();
			TheDisplay->draw();
			Sleep(100);
			currTime = timeGetTime();
		}

		m_wndVideoManager->update();
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
 m_progressBar = nullptr;
}

void ChallengeLoadScreen::update( Int percent )
{
	percent = (percent + FRAME_FUDGE_ADD)/1.3;
	UnicodeString per;
	per.format(L"%d%%",percent);
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
	GadgetProgressBarSetProgress(m_progressBar, percent);

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
	m_creditWindow = nullptr;
	m_creditUrl = nullptr;
	m_creditLogo = nullptr;
	m_creditStartMs = 0;
	m_creditEscArmed = FALSE;
	m_creditSkipped = FALSE;
}

ShellGameLoadScreen::~ShellGameLoadScreen()
{
	// TheSuperHackers @feature arcticdolphin 09/11/2026 The credit window goes with the load screen window; keep it from reaching this object.
	if (m_creditWindow != nullptr)
	{
		m_creditWindow->winSetUserData(nullptr);
		m_creditWindow->winHide(TRUE);
	}
	if (m_creditUrl != nullptr)
	{
		TheDisplayStringManager->freeDisplayString(m_creditUrl);
	}
}

void ShellGameLoadScreen::init( GameInfo *game )
{
	static BOOL firstLoad = TRUE;


	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( "Menus/ShellGameLoadScreen.wnd" );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the ShellGame loadscreen"));
	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();

	// Store the pointer to the progress bar on the loadscreen
	m_progressBar = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "ShellGameLoadScreen.wnd:ProgressLoad" ));
	DEBUG_ASSERTCRASH(m_progressBar, ("Can't initialize the progressbar for the single player loadscreen"));
	GadgetProgressBarSetProgress(m_progressBar, 0 );
	m_progressBar->winHide(TRUE);

	if(m_loadScreen && firstLoad && TheGameLODManager && TheGameLODManager->didMemPass())
	{
		m_loadScreen->winSetEnabledImage(0, TheMappedImageCollection->findImageByName("TitleScreen"));
		TheWritableGlobalData->m_breakTheMovie = FALSE;

		GameWindow *win = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "ShellGameLoadScreen.wnd:StaticTextLegal" ));
		if(win)
			win->winHide(FALSE);
		firstLoad = FALSE;
	}
	m_progressBar->winHide(FALSE);
	// TheSuperHackers @tweak arcticdolphin 09/11/2026 With the GO bar art the bar keeps the same padding on every side at any resolution.
	if (findGoLoadBarImage() != nullptr)
	{
		m_progressBar->winSetDrawFunc(drawGoShellLoadBar);
	}

	// TheSuperHackers @feature arcticdolphin 09/11/2026 The first shell load after the intro shows the Generals Online credit; -nologo leaves it out.
	static Bool firstShellLoad = TRUE;
	if (m_loadScreen && TheGameLogic && TheGameLogic->getGameMode() == GAME_SHELL)
	{
		if (firstShellLoad && TheGlobalData->m_playIntro)
		{
			initCredit();
		}
		firstShellLoad = FALSE;
	}
}

void ShellGameLoadScreen::initCredit()
{
	// Please be kind and do not remove the custom logo screen when building a client that includes GeneralsOnline.
	const Real fontScale = GlobalLanguage::getResolutionFontSizeScale(GlobalLanguage::ResolutionFontSizeMethod_Strict);
	const Int screenWidth = TheDisplay->getWidth();
	const Int screenHeight = TheDisplay->getHeight();

	// "Consolas" is Windows Vista native, can be installed in 2000, XP
	m_creditUrl = TheDisplayStringManager->newDisplayString();
	m_creditUrl->setText(UnicodeString(L"www.playgenerals.online"));
	m_creditUrl->setFont(TheFontLibrary->getFont("Consolas", Int(14 * fontScale), FALSE));
	m_creditUrl->setWordWrap(screenWidth);
	m_creditLogo = TheMappedImageCollection->findImageByName("GOLoadLogo");
	// TheSuperHackers @tweak arcticdolphin 09/11/2026 The GO logo replaces the title art; the Retail style leaves the logo out and keeps the title art.
	if (m_creditLogo != nullptr)
	{
		m_loadScreen->winSetEnabledImage(0, nullptr);
	}
	// TheSuperHackers @tweak arcticdolphin 09/11/2026 The GO branded screen leaves out the retail legal text, which also keeps the room over the bar for the radar van.
	if (m_creditLogo != nullptr)
	{
		GameWindow *legal = TheWindowManager->winGetWindowFromId(m_loadScreen, TheNameKeyGenerator->nameToKey("ShellGameLoadScreen.wnd:StaticTextLegal"));
		if (legal != nullptr)
		{
			legal->winHide(TRUE);
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
	// a fresh Esc press only: a key still held from skipping the intro must be released first
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
	// TheSuperHackers @tweak arcticdolphin 09/11/2026 The launcher tan for the website.
	const Color tanColor = GameMakeColor(227, 186, 108, alpha);
	const Color dropColor = GameMakeColor(0, 0, 0, alpha);

	const Int logoTop = REAL_TO_INT(CREDIT_LOGO_TOP * unit);
	const Int logoHeight = REAL_TO_INT(CREDIT_LOGO_HEIGHT * unit);
	if (m_creditLogo != nullptr && m_creditLogo->getImageHeight() > 0)
	{
		const Int logoWidth = logoHeight * m_creditLogo->getImageWidth() / m_creditLogo->getImageHeight();
		const Int logoLeft = centerX - logoWidth / 2;
		TheDisplay->drawImage(m_creditLogo, logoLeft, logoTop, logoLeft + logoWidth, logoTop + logoHeight, GameMakeColor(255, 255, 255, alpha));
	}

	Int width = 0;
	Int height = 0;
	m_creditUrl->getSize(&width, &height);
	m_creditUrl->draw(centerX - width / 2, REAL_TO_INT(CREDIT_URL_TOP * unit), tanColor, dropColor);
}

void ShellGameLoadScreen::reset()
{
 setLoadScreen(nullptr);
 m_progressBar = nullptr;
}

void ShellGameLoadScreen::update( Int percent )
{
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
	// TheSuperHackers @feature arcticdolphin 09/11/2026 With the credit up the bar fills by time, so it reaches 100% when the credit ends.
	if (m_creditWindow != nullptr)
	{
		pollCreditSkip();
		const Int creditProgress = creditPercent();
		if (percent > creditProgress)
		{
			percent = creditProgress;
		}
	}
	GadgetProgressBarSetProgress(m_progressBar, percent);

	// Do this last!
	LoadScreen::update( percent );
}

// MultiPlayerLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
MultiPlayerLoadScreen::MultiPlayerLoadScreen()
{
	m_mapPreview = nullptr;
	m_portraitLocalGeneral = nullptr;
	m_featuresLocalGeneral = nullptr;
	m_nameLocalGeneral = nullptr;

	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_buttonMapStartPosition[i] = nullptr;
		m_progressBars[i] = nullptr;
		m_playerNames[i] = nullptr;
		m_playerSide[i]= nullptr;
		m_playerLookup[i] = -1;
	}
}

MultiPlayerLoadScreen::~MultiPlayerLoadScreen()
{
	if(m_mapPreview)
	{
		m_mapPreview->winSetUserData(nullptr);
	}

	TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );
//	TheAudio->stopAudio( AudioAffect_Music );
}

void MultiPlayerLoadScreen::init( GameInfo *game )
{
	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( "Menus/MultiplayerLoadScreen.wnd" );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the Multiplayer loadscreen"));
	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();
	m_mapPreview = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "MultiplayerLoadScreen.wnd:WinMapPreview"));
	GameSlot *lSlot = game->getSlot(game->getLocalSlotNum());
	const PlayerTemplate* pt;
	if (lSlot->getPlayerTemplate() >= 0)
		pt = ThePlayerTemplateStore->getNthPlayerTemplate(lSlot->getPlayerTemplate());
	else
		pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );

#if RTS_GENERALS
	const Image *loadScreenImage = TheMappedImageCollection->findImageByName(pt->getLoadScreen());
	if(loadScreenImage)
		m_loadScreen->winSetEnabledImage(0, loadScreenImage);
#else
	// add portrait, features, and name for the local player's general
	const GeneralPersona *localGeneral = TheChallengeGenerals->getGeneralByTemplateName( pt->getName() );
	const Image *portrait = nullptr;
	UnicodeString localName;
	if (localGeneral)
	{
		portrait = localGeneral->getBioPortraitLarge();
		localName = TheGameText->fetch( localGeneral->getBioName() );
	}
	else
	{
		// the main original factions don't have associated generals
		AsciiString imageName;
		if (pt->getName() == "FactionAmerica")
			portrait = TheMappedImageCollection->findImageByName("SAFactionLogoLg_US");
		else if (pt->getName() == "FactionGLA")
			portrait = TheMappedImageCollection->findImageByName("SUFactionLogoLg_GLA");
		else if (pt->getName() == "FactionChina")
			portrait = TheMappedImageCollection->findImageByName("SNFactionLogoLg_China");
		else
			DEBUG_CRASH(("Unexpected player template"));

		localName = pt->getDisplayName();
	}
	m_portraitLocalGeneral = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "MultiplayerLoadScreen.wnd:LocalGeneralPortrait"));
	m_portraitLocalGeneral->winSetEnabledImage( 0, portrait);
	m_featuresLocalGeneral = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "MultiplayerLoadScreen.wnd:LocalGeneralFeatures"));
	AsciiString features = pt->getGeneralFeatures();
	GadgetStaticTextSetText( m_featuresLocalGeneral, TheGameText->fetch( features.isEmpty() ? "GUI:PlayerObserver" : pt->getGeneralFeatures() ) );
	m_nameLocalGeneral = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "MultiplayerLoadScreen.wnd:LocalGeneralName"));
	GadgetStaticTextSetText( m_nameLocalGeneral, localName );
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

	GameWindow *teamWin[MAX_SLOTS];
	Int i = 0;
	for (; i < MAX_SLOTS; ++i)
	{
		teamWin[i] = nullptr;
	}

	Int netSlot = 0;
	// Loop through and make the loadscreen look all good.
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		// Load the Progress Bar
		AsciiString winName;
		winName.format( "MultiplayerLoadScreen.wnd:ProgressLoad%d",i);
		m_progressBars[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_progressBars[i], ("Can't initialize the progressbars for the Multiplayer loadscreen"));
		// set the progressbar to zero
		GadgetProgressBarSetProgress(m_progressBars[i], 0 );

		// Load MapStart Positions
		winName.format( "MultiplayerLoadScreen.wnd:ButtonMapStartPosition%d",i);
		m_buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_buttonMapStartPosition[i], ("Can't initialize the MapStart Positions for the MultiplayerLoadScreen loadscreen"));


		// Load the Player's name
		winName.format( "MultiplayerLoadScreen.wnd:StaticTextPlayer%d",i);
		m_playerNames[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerNames[i], ("Can't initialize the Names for the Multiplayer loadscreen"));

		// Load the Player's Side
		winName.format( "MultiplayerLoadScreen.wnd:StaticTextSide%d",i);
		m_playerSide[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerSide[i], ("Can't initialize the Sides for the Multiplayer loadscreen"));

		winName.format( "MultiplayerLoadScreen.wnd:StaticTextTeam%d",i);
		teamWin[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));

		// get the slot man!
		GameSlot *slot = game->getSlot(i);
		if (!slot || !slot->isOccupied())
			continue;

		Color houseColor = TheMultiplayerSettings->getColor(slot->getApparentColor())->getColor();
#if RTS_GENERALS
		GadgetProgressBarSetEnabledBarColor(m_progressBars[netSlot],houseColor );
#else
		// format the progress bar to house colors
		AsciiString imageName;
		imageName.format("LoadingBar_ProgressCenter%d", slot->getApparentColor());
		const Image *houseImage = TheMappedImageCollection->findImageByName(imageName);
		if (! houseImage)
			houseImage = TheMappedImageCollection->findImageByName("LoadingBar_Progress");
		// TheSuperHackers @tweak arcticdolphin 09/11/2026 With the GO bar art every player bar is bronze; the house colour stays on the player name.
		if (const Image *kitImage = findGoLoadBarImage())
		{
			houseImage = kitImage;
		}
		m_progressBars[netSlot]->winSetEnabledImage( 6, houseImage );
#endif

		UnicodeString name = slot->getName();
		GadgetStaticTextSetText(m_playerNames[netSlot], name );
		m_playerNames[netSlot]->winSetEnabledTextColors(houseColor, m_playerNames[netSlot]->winGetEnabledTextBorderColor());

#if defined(GENONLINE_REVEAL_TEAMS)
        const PlayerTemplate* pt = ThePlayerTemplateStore->getNthPlayerTemplate(slot->getPlayerTemplate());
        GadgetStaticTextSetText(m_playerSide[netSlot], pt ? pt->getDisplayName() : slot->getApparentPlayerTemplateDisplayName());
#else
        GadgetStaticTextSetText(m_playerSide[netSlot], slot->getApparentPlayerTemplateDisplayName());
#endif
		
		m_playerSide[netSlot]->winSetEnabledTextColors(houseColor, m_playerSide[netSlot]->winGetEnabledTextBorderColor());

		if (slot->isAI() && m_progressBars[netSlot])
			m_progressBars[netSlot]->winHide(TRUE);

		if (teamWin[netSlot])
		{
			AsciiString teamStr;
			teamStr.format("Team:%d", slot->getTeamNumber() + 1);
			GadgetStaticTextSetText(teamWin[netSlot], TheGameText->fetch(teamStr));
			teamWin[netSlot]->winSetEnabledTextColors(houseColor, m_playerNames[netSlot]->winGetEnabledTextBorderColor());
		}

		m_playerLookup[i] = netSlot; // save our mapping so we can update progress correctly

		netSlot++;
	}

	for(i = netSlot; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i]->winHide(TRUE);
		m_playerNames[i]->winHide(TRUE);
		m_playerSide[i]->winHide(TRUE);
		teamWin[i]->winHide(TRUE);
	}

	if(m_mapPreview)
	{
		const MapMetaData *mmd = TheMapCache->findMap(game->getMap());
		Image *image = getMapPreviewImage(game->getMap());
		m_mapPreview->winSetUserData((void *)mmd);

		positionStartSpots( game, m_buttonMapStartPosition, m_mapPreview);
		updateMapStartSpots( game, m_buttonMapStartPosition, TRUE );
		//positionAdditionalImages((MapMetaData *)mmd, m_mapPreview, TRUE);
		if(image)
		{
			m_mapPreview->winSetStatus(WIN_STATUS_IMAGE);
			m_mapPreview->winSetEnabledImage(0, image);
		}
		else
		{
			m_mapPreview->winClearStatus(WIN_STATUS_IMAGE);
		}
	}


	TheGameLogic->initTimeOutValues();
}

void MultiPlayerLoadScreen::reset()
{
	setLoadScreen(nullptr);
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = nullptr;
		m_playerNames[i] = nullptr;
		m_playerSide[i]= nullptr;
	}
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
	if(m_progressBars[m_playerLookup[playerId]])
		GadgetProgressBarSetProgress(m_progressBars[m_playerLookup[playerId]], percentage );
}

// GameSpyLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
GameSpyLoadScreen::GameSpyLoadScreen()
{

	m_mapPreview = nullptr;
	m_portraitLocalGeneral = nullptr;
	m_featuresLocalGeneral = nullptr;
	m_nameLocalGeneral = nullptr;

	for(Int i = 0; i < MAX_SLOTS; ++i)
	{

		m_buttonMapStartPosition[i] = nullptr;
		m_playerRank[i] = nullptr;

		m_playerOfficerMedal[i] = nullptr;
		m_progressBars[i] = nullptr;
		m_playerNames[i] = nullptr;
		m_playerSide[i]= nullptr;
		m_playerLookup[i] = -1;
		m_playerFavoriteFactions[i]= nullptr;
		m_playerTotalDisconnects[i]= nullptr;
		m_playerWin[i]= nullptr;
		m_playerWinLosses[i]= nullptr;
	}
}

GameSpyLoadScreen::~GameSpyLoadScreen()
{
	if(m_mapPreview)
	{
		m_mapPreview->winSetUserData(nullptr);
	}
}

#if !defined(GENONLINE)
extern Int GetAdditionalDisconnectsFromUserFile(Int playerID);
#endif

void GameSpyLoadScreen::init( GameInfo *game )
{
	g_bHasDoneSOGScreenshot = FALSE;

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( "Menus/GameSpyLoadScreen.wnd" );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the Multiplayer loadscreen"));
	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();
	m_mapPreview = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "GameSpyLoadScreen.wnd:WinMapPreview"));
	DEBUG_ASSERTCRASH(TheNetwork, ("Where the Heck is the Network!!!!"));
	DEBUG_LOG(("NumPlayers %d", TheNetwork->getNumPlayers()));
GameSlot *lSlot = game->getSlot(game->getLocalSlotNum());
	const PlayerTemplate* pt;
	if (lSlot->getPlayerTemplate() >= 0)
		pt = ThePlayerTemplateStore->getNthPlayerTemplate(lSlot->getPlayerTemplate());
	else
		pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );

#if RTS_GENERALS
	const Image *loadScreenImage = TheMappedImageCollection->findImageByName(pt->getLoadScreen());
	if(loadScreenImage)
		m_loadScreen->winSetEnabledImage(0, loadScreenImage);
#else
	// add portrait, features, and name for the local player's general
	const GeneralPersona *localGeneral = TheChallengeGenerals->getGeneralByTemplateName( pt->getName() );
	const Image *portrait = nullptr;
	UnicodeString localName;
	if (localGeneral)
	{
		portrait = localGeneral->getBioPortraitLarge();
		localName = TheGameText->fetch( localGeneral->getBioName() );
	}
	else
	{
		// the main original factions don't have associated generals
		AsciiString imageName;
		if (pt->getName() == "FactionAmerica")
			portrait = TheMappedImageCollection->findImageByName("SAFactionLogo144_US");
		else if (pt->getName() == "FactionGLA")
			portrait = TheMappedImageCollection->findImageByName("SUFactionLogo144_GLA");
		else if (pt->getName() == "FactionChina")
			portrait = TheMappedImageCollection->findImageByName("SNFactionLogo144_China");
		else
			DEBUG_CRASH(("Unexpected player template"));

		localName = pt->getDisplayName();
	}
	m_portraitLocalGeneral = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "GameSpyLoadScreen.wnd:LocalGeneralPortrait"));
	m_portraitLocalGeneral->winSetEnabledImage( 0, portrait);
	m_featuresLocalGeneral = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "GameSpyLoadScreen.wnd:LocalGeneralFeatures"));
	AsciiString features = pt->getGeneralFeatures();
	GadgetStaticTextSetText( m_featuresLocalGeneral, TheGameText->fetch( features.isEmpty() ? "GUI:PlayerObserver" : pt->getGeneralFeatures() ) );
	m_nameLocalGeneral = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "GameSpyLoadScreen.wnd:LocalGeneralName"));
	GadgetStaticTextSetText( m_nameLocalGeneral, localName );
#endif

	GameWindow *teamWin[MAX_SLOTS];
	Int i = 0;
	for (; i < MAX_SLOTS; ++i)
	{
		teamWin[i] = nullptr;
	}

	Int netSlot = 0;
	// Loop through and make the loadscreen look all good.
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		// Load the Progress Bar
		AsciiString winName;
		winName.format( "GameSpyLoadScreen.wnd:ProgressLoad%d",i);
		m_progressBars[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_progressBars[i], ("Can't initialize the progressbars for the GameSpyLoadScreen loadscreen"));
		// set the progressbar to zero
		GadgetProgressBarSetProgress(m_progressBars[i], 0 );

		// Load the Player's name
		winName.format( "GameSpyLoadScreen.wnd:StaticTextPlayer%d",i);
		m_playerNames[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerNames[i], ("Can't initialize the Names for the GameSpyLoadScreen loadscreen"));

		// Load MapStart Positions
		winName.format( "GameSpyLoadScreen.wnd:ButtonMapStartPosition%d",i);
		m_buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_buttonMapStartPosition[i], ("Can't initialize the MapStart Positions for the GameSpyLoadScreen loadscreen"));


		// Load the Player's Side
		winName.format( "GameSpyLoadScreen.wnd:StaticTextSide%d",i);
		m_playerSide[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerSide[i], ("Can't initialize the Sides for the GameSpyLoadScreen loadscreen"));

		// Load the Player's window
		winName.format( "GameSpyLoadScreen.wnd:WinPlayer%d",i);
		m_playerWin[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerWin[i], ("Can't initialize the WinPlayer for the GameSpyLoadScreen loadscreen"));

		// Load the Player's m_playerTotalDisconnects
		winName.format( "GameSpyLoadScreen.wnd:StaticTextTotalDisconnects%d",i);
		m_playerTotalDisconnects[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerTotalDisconnects[i], ("Can't initialize the m_playerTotalDisconnects for the GameSpyLoadScreen loadscreen"));

//		// Load the Player's m_playerFavoriteFactions
//		winName.format( "GameSpyLoadScreen.wnd:StaticTextFavoriteFaction%d",i);
//		m_playerFavoriteFactions[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
//		DEBUG_ASSERTCRASH(m_playerFavoriteFactions[i], ("Can't initialize the StaticTextFavoriteFaction for the GameSpyLoadScreen loadscreen"));

		// Load the Player's m_playerWinLosses
		winName.format( "GameSpyLoadScreen.wnd:StaticTextWinLoss%d",i);
		m_playerWinLosses[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerWinLosses[i], ("Can't initialize the m_playerWinLosses for the GameSpyLoadScreen loadscreen"));

		// Load the Player's m_playerWinLosses
		winName.format( "GameSpyLoadScreen.wnd:WinRank%d",i);
		m_playerRank[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerRank[i], ("Can't initialize the m_playerRank for the GameSpyLoadScreen loadscreen"));

		// Load the Player's m_playerOfficerMedal
		winName.format( "GameSpyLoadScreen.wnd:WinOfficer%d",i);
		m_playerOfficerMedal[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerOfficerMedal[i], ("Can't initialize the m_playerOfficerMedal for the GameSpyLoadScreen loadscreen"));

		winName.format( "MultiplayerLoadScreen.wnd:StaticTextTeam%d",i);
		teamWin[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));

		// get the slot man!
		GameSpyGameSlot *slot = (GameSpyGameSlot *)game->getSlot(i);
		if (!slot || !slot->isOccupied())
			continue;

		Color houseColor = TheMultiplayerSettings->getColor(slot->getApparentColor())->getColor();
#if RTS_GENERALS
		GadgetProgressBarSetEnabledBarColor(m_progressBars[netSlot],houseColor );
#else
		// format the progress bar to house colors
		AsciiString imageName;
		imageName.format("LoadingBar_ProgressCenter%d", slot->getApparentColor());
		const Image *houseImage = TheMappedImageCollection->findImageByName(imageName);
		if (! houseImage)
			houseImage = TheMappedImageCollection->findImageByName("LoadingBar_Progress");
		// TheSuperHackers @tweak arcticdolphin 09/11/2026 With the GO bar art every player bar is bronze; the house colour stays on the player name.
		if (const Image *kitImage = findGoLoadBarImage())
		{
			houseImage = kitImage;
		}
		m_progressBars[netSlot]->winSetEnabledImage( 6, houseImage );
#endif

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

		UnicodeString name = slot->getName();

#if defined(GENONLINE)
		// if QM, show ELO
		GenOnlineGame* pNGMPGame = (GenOnlineGame*)game;
		if (pNGMPGame->isQMGame())
		{
			name.format(L"%s (Elo: %d)", slot->getName().str(), stats.elo_rating);
		}
#endif

		GadgetStaticTextSetText(m_playerNames[netSlot], name );
		m_playerNames[netSlot]->winSetEnabledTextColors(houseColor, m_playerNames[netSlot]->winGetEnabledTextBorderColor());

		DEBUG_LOG(("LoadScreen - populating info for %ls(%d) - stats returned id %d",
			slot->getName().str(), slot->getProfileID(), stats.id));

#if defined(GENONLINE)
		Bool isPreorder = false;
#else
		Bool isPreorder = TheGameSpyInfo->didPlayerPreorder(stats.id);
#endif
		Int rankPoints = CalculateRank(stats);
		Int favSide = GetFavoriteSide(stats);

		const Image *preorderImg = TheMappedImageCollection->findImageByName("OfficersClubsmall");
		if (!isPreorder)
			preorderImg = nullptr;
		const Image *rankImg = LookupSmallRankImage(favSide, rankPoints);
		m_playerOfficerMedal[i]->winSetEnabledImage(0, preorderImg);
		m_playerRank[i]->winSetEnabledImage(0, rankImg);

		UnicodeString formatString;

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
		formatString.format(L"%d/%d", numWins, numLosses);
		GadgetStaticTextSetText(m_playerWinLosses[netSlot], formatString);
		m_playerWinLosses[netSlot]->winSetEnabledTextColors(houseColor, m_playerWinLosses[netSlot]->winGetEnabledTextBorderColor());
		// favoriteFaction
		Int numGames = 0;
		Int favorite = 0;

		for(it =stats.games.begin(); it != stats.games.end(); ++it)
		{
			if(it->second >= numGames)
			{
				numGames = it->second;
				favorite = it->first;
			}
		}

//		if(numGames == 0)
//			GadgetStaticTextSetText(m_playerFavoriteFactions[netSlot], TheGameText->fetch("GUI:None"));
//		else if( stats.gamesAsRandom > numGames )
//			GadgetStaticTextSetText(m_playerFavoriteFactions[netSlot], TheGameText->fetch("GUI:Random"));
//		else
//		{
//			const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(favorite);
//			if (fac)
//			{
//				AsciiString side;
//				side.format("SIDE:%s", fac->getSide().str());
//
//				GadgetStaticTextSetText(m_playerFavoriteFactions[netSlot], TheGameText->fetch(side));
//			}
//		}
//		m_playerFavoriteFactions[netSlot]->winSetEnabledTextColors(houseColor, m_playerFavoriteFactions[netSlot]->winGetEnabledTextBorderColor());
		// disconnects
		numGames = 0;


		for(it =stats.discons.begin(); it != stats.discons.end(); ++it)
		{
			numGames += it->second;
		}
		for(it =stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
		{
			numGames += it->second;
		}
#if !defined(GENONLINE)
		numGames += GetAdditionalDisconnectsFromUserFile(stats.id);
#endif

		formatString.format(L"%d", numGames);
		GadgetStaticTextSetText(m_playerTotalDisconnects[netSlot], formatString);
		m_playerTotalDisconnects[netSlot]->winSetEnabledTextColors(houseColor, m_playerTotalDisconnects[netSlot]->winGetEnabledTextBorderColor());

#if defined(GENONLINE_REVEAL_TEAMS)
		const PlayerTemplate* pt = ThePlayerTemplateStore->getNthPlayerTemplate(slot->getPlayerTemplate());
        GadgetStaticTextSetText(m_playerSide[netSlot], pt ? pt->getDisplayName() : slot->getApparentPlayerTemplateDisplayName());
#else
		GadgetStaticTextSetText(m_playerSide[netSlot], slot->getApparentPlayerTemplateDisplayName());
#endif
		m_playerSide[netSlot]->winSetEnabledTextColors(houseColor, m_playerSide[netSlot]->winGetEnabledTextBorderColor());

		if (slot->isAI())
		{
			if (m_progressBars[netSlot])
				m_progressBars[netSlot]->winHide(TRUE);
			if (m_playerTotalDisconnects[netSlot])
				m_playerTotalDisconnects[netSlot]->winHide(TRUE);
//			if (m_playerFavoriteFactions[netSlot])
//				m_playerFavoriteFactions[netSlot]->winHide(TRUE);
			if (m_playerWinLosses[netSlot])
				m_playerWinLosses[netSlot]->winHide(TRUE);
			if (m_playerRank[netSlot])
				m_playerRank[netSlot]->winHide(TRUE);
			if (m_playerOfficerMedal[netSlot])
				m_playerOfficerMedal[netSlot]->winHide(TRUE);
		}

		if (teamWin[netSlot])
		{
			AsciiString teamStr;
			teamStr.format("Team:%d", slot->getTeamNumber() + 1);
			if (slot->isAI() && slot->getTeamNumber() == -1)
				teamStr = "Team:AI";
			GadgetStaticTextSetText(teamWin[netSlot], TheGameText->fetch(teamStr));
			teamWin[netSlot]->winSetEnabledTextColors(houseColor, m_playerNames[netSlot]->winGetEnabledTextBorderColor());
		}

		m_playerLookup[i] = netSlot; // save our mapping so we can update progress correctly

		netSlot++;
	}

	for(i = netSlot; i < MAX_SLOTS; ++i)
	{
		m_playerWin[i]->winHide(TRUE);
		//m_playerNames[i]->winHide(TRUE);
		//m_playerSide[i]->winHide(TRUE);
	}

	if(m_mapPreview)
	{
		const MapMetaData *mmd = TheMapCache->findMap(game->getMap());
		Image *image = getMapPreviewImage(game->getMap());
		m_mapPreview->winSetUserData((void *)mmd);

		positionStartSpots( game, m_buttonMapStartPosition, m_mapPreview);
		updateMapStartSpots( game, m_buttonMapStartPosition, TRUE );
		//positionAdditionalImages((MapMetaData *)mmd, m_mapPreview, TRUE);
		if(image)
		{
			m_mapPreview->winSetStatus(WIN_STATUS_IMAGE);
			m_mapPreview->winSetEnabledImage(0, image);
		}
		else
		{
			m_mapPreview->winClearStatus(WIN_STATUS_IMAGE);
		}
	}

	TheGameLogic->initTimeOutValues();
}

void GameSpyLoadScreen::reset()
{
	setLoadScreen(nullptr);
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = nullptr;
		m_playerNames[i] = nullptr;
		m_playerSide[i]= nullptr;
	}
}

void GameSpyLoadScreen::update( Int percent )
{
	if(percent <= 100)
		TheNetwork->updateLoadProgress( percent );
	TheNetwork->liteupdate();

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

	//GadgetProgressBarSetProgress(m_progressBars[TheNetwork->getLocalPlayerID()], percent );

	// GENERALS ONLINE: this is ticked in game engine, but game engine doesnt tick for MP loads when the host is complete and remotes arent... do a liteupdate like TheNetwork does
	if (GenOnlineServicesManager::GetInstance() != nullptr)
	{
		GenOnlineServicesManager::GetInstance()->Tick();
	}
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
	if(m_progressBars[m_playerLookup[playerId]])
		GadgetProgressBarSetProgress(m_progressBars[m_playerLookup[playerId]], percentage );
}

// MapTransferLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
MapTransferLoadScreen::MapTransferLoadScreen()
{
	m_oldTimeout = 0;
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = nullptr;
		m_playerNames[i] = nullptr;
		m_progressText[i]= nullptr;
		m_playerLookup[i] = -1;
		m_oldProgress[i] = -1;
	}
	m_fileNameText = nullptr;
	m_timeoutText = nullptr;
}

MapTransferLoadScreen::~MapTransferLoadScreen()
{
}

void MapTransferLoadScreen::init( GameInfo *game )
{
	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( "Menus/MapTransferScreen.wnd" );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the map transfer loadscreen"));
	if (!m_loadScreen)
		return;

	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();

	DEBUG_ASSERTCRASH(TheNetwork, ("Where the Heck is the Network?!!!!"));
	DEBUG_LOG(("NumPlayers %d", TheNetwork->getNumPlayers()));

	AsciiString winName;
	Int i;

	// Load the Filename Text
	m_fileNameText = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "MapTransferScreen.wnd:StaticTextCurrentFile" ));
	DEBUG_ASSERTCRASH(m_fileNameText, ("Can't initialize the filename for the map transfer loadscreen"));

	// Load the Timeout Text
	m_timeoutText = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( "MapTransferScreen.wnd:StaticTextTimeout" ));
	DEBUG_ASSERTCRASH(m_timeoutText, ("Can't initialize the timeout for the map transfer loadscreen"));

	Int netSlot = 0;
	// Loop through and make the loadscreen look all good.
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		// Load the Progress Bar
		winName.format( "MapTransferScreen.wnd:ProgressLoad%d",i);
		m_progressBars[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_progressBars[i], ("Can't initialize the progressbars for the map transfer loadscreen"));
		// set the progressbar to zero
		GadgetProgressBarSetProgress(m_progressBars[i], 0 );

		// Load the Player's name
		winName.format( "MapTransferScreen.wnd:StaticTextPlayer%d",i);
		m_playerNames[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerNames[i], ("Can't initialize the Names for the map transfer loadscreen"));

		// Load the Progress Text
		winName.format( "MapTransferScreen.wnd:StaticTextProgress%d",i);
		m_progressText[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_progressText[i], ("Can't initialize the progress text for the map transfer loadscreen"));

		// get the slot man!
		GameSlot *slot = game->getSlot(i);
		if (!slot || !slot->isHuman())
			continue;
		Color houseColor = TheMultiplayerSettings->getColor(slot->getApparentColor())->getColor();
		GadgetProgressBarSetEnabledBarColor(m_progressBars[netSlot], houseColor );
		// TheSuperHackers @tweak arcticdolphin 09/11/2026 With the GO bar art this colour drawn bar takes its bronze; the house colour stays on the player name.
		if (findGoLoadBarImage() != nullptr)
		{
			GadgetProgressBarSetEnabledBarColor(m_progressBars[netSlot], GameMakeColor(191, 144, 72, 255) );
		}

		UnicodeString name = slot->getName();
		GadgetStaticTextSetText(m_playerNames[netSlot], name );
		m_playerNames[netSlot]->winSetEnabledTextColors(houseColor, m_playerNames[netSlot]->winGetEnabledTextBorderColor());

		GadgetStaticTextSetText(m_progressText[netSlot], UnicodeString::TheEmptyString );
		m_progressText[netSlot]->winSetEnabledTextColors(houseColor, m_progressText[netSlot]->winGetEnabledTextBorderColor());

		const GameSlot *gameInfoSlot = TheGameInfo->getConstSlot(i);
		if ((i == 0 || (gameInfoSlot && gameInfoSlot->isHuman() && gameInfoSlot->hasMap())) && m_progressBars[netSlot])
			m_progressBars[netSlot]->winHide(TRUE);

		m_playerLookup[i] = netSlot; // save our mapping so we can update progress correctly

		netSlot++;
	}

	for(i = netSlot; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i]->winHide(TRUE);
		m_playerNames[i]->winHide(TRUE);
		m_progressText[i]->winHide(TRUE);
	}
}

void MapTransferLoadScreen::reset()
{
	setLoadScreen(nullptr);
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = nullptr;
		m_playerNames[i] = nullptr;
		m_progressText[i]= nullptr;
		m_playerLookup[i] = -1;
		m_oldProgress[i] = -1;
	}
	m_fileNameText = nullptr;
	m_timeoutText = nullptr;
}

void MapTransferLoadScreen::update( Int percent )
{
	if (TheNetwork)
	{
		TheNetwork->liteupdate();
	}

	// GENERALS ONLINE: this is ticked in game engine, but game engine doesnt tick for MP loads and map transfers are while(true)... when the host is complete and remotes arent... do a liteupdate like TheNetwork does
	if (GenOnlineServicesManager::GetInstance() != nullptr)
	{
		GenOnlineServicesManager::GetInstance()->Tick();
	}

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

	Int translatedSlot = m_playerLookup[playerId];
	if(m_progressBars[translatedSlot])
		GadgetProgressBarSetProgress(m_progressBars[translatedSlot], percentage );
	if (m_progressText[translatedSlot])
		GadgetStaticTextSetText(m_progressText[translatedSlot], TheGameText->fetch(stateStr));
}

void MapTransferLoadScreen::processTimeout(Int secondsLeft)
{
	if (m_oldTimeout == secondsLeft)
		return;
	m_oldTimeout = secondsLeft;

	if (m_timeoutText)
	{
		UnicodeString txt;
		txt.format(TheGameText->fetch("MapTransfer:Timeout"), (secondsLeft/60), (secondsLeft%60));
		GadgetStaticTextSetText(m_timeoutText, txt);
	}
}

void MapTransferLoadScreen::setCurrentFilename(AsciiString filename)
{
	if (m_fileNameText)
	{
		UnicodeString txt;
		txt.translate(TheGameState->getMapLeafName(filename));
		txt.format(TheGameText->fetch("MapTransfer:CurrentFile"), txt.str());
		GadgetStaticTextSetText(m_fileNameText, txt);
	}
}

