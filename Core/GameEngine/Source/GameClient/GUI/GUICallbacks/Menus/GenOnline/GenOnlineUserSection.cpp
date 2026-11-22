#include "PreRTS.h"

#include "GameClient/GenOnlineUserSection.h"

#include "Common/GOUIColors.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GenOnlineMenus.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"
#include "GameNetwork/GenOnline/GenOnlineAuth.h"
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineSocialInterface.h"

#include <algorithm>
#include <chrono>
#include <vector>

namespace
{

// 800x600 layout units, right aligned to the content frame and centred in the ruler strip above it.
constexpr Int SECTION_RIGHT = 759;
constexpr Int SECTION_BOTTOM = 30;
constexpr Int SECTION_MAX_WIDTH = 190;
constexpr Int SECTION_HEIGHT = 20;
constexpr Int SECTION_PADDING = 8;
constexpr Int SECTION_GAP = 6;
constexpr Int MARKER_PADDING = 5;
constexpr Int CHEVRON_WIDTH = 8;
constexpr Int FRIENDS_REFRESH_MS = 500;

constexpr Int PANEL_WIDTH = 200;
constexpr Int PANEL_PADDING = 8;
constexpr Int PANEL_ROW = 16;
constexpr Int PANEL_BUTTON_HEIGHT = 20;
constexpr Int PANEL_MAX_ONLINE = 8;
constexpr Int PANEL_MAX_REQUESTS = 4;

enum PanelAction
{
	ACTION_ACCEPT,
	ACTION_IGNORE,
	ACTION_MESSAGES,
	ACTION_FRIENDS,
	ACTION_PROFILE,
	ACTION_SIGN_OUT,
	ACTION_STATUS
};

struct StatusSpec
{
	const char *value;
	const char *key;
	const WideChar *text;
};

const StatusSpec STATUSES[] = {
	{ "online", "GUI:GOPresenceOnline", L"Online" },
	{ "idle", "GUI:GOPresenceIdle", L"Idle" },
	{ "do_not_disturb", "GUI:GOPresenceDoNotDisturb", L"Do not disturb" },
	{ "invisible", "GUI:GOPresenceInvisible", L"Invisible" },
};
constexpr Int STATUS_COUNT = 4;

struct PanelHit
{
	Int left, top, right, bottom;
	PanelAction action;
	int64_t userID;
};

struct FriendRow
{
	int64_t userID;
	std::wstring name;
	std::wstring status;
	std::string mode;
};

GameWindow *s_window = nullptr;
GameWindow *s_catcher = nullptr;
GameWindow *s_panel = nullptr;
DisplayString *s_name = nullptr;
DisplayString *s_marker = nullptr;
DisplayString *s_textRegular = nullptr;
DisplayString *s_textSmall = nullptr;
Bool s_hovered = FALSE;
Int s_friendsOnline = -1;
Int s_friendsTotal = 0;
Int s_notifications = -1;
Int s_sectionRight = 0;
Int s_sectionBottom = 0;
std::wstring s_displayName;
std::string s_localStatus;
std::vector<FriendRow> s_onlineRows;
std::vector<FriendRow> s_requestRows;
std::vector<PanelHit> s_hits;
std::chrono::steady_clock::time_point s_lastRefresh;

Int scaleX(Int units)
{
	return GameWindowManager::winScaleRounded(units, TheDisplay->getWidth(), 800);
}

Int scaleY(Int units)
{
	return GameWindowManager::winScaleRounded(units, TheDisplay->getHeight(), 600);
}

DisplayString *newString(const char *headerTemplate)
{
	DisplayString *string = TheDisplayStringManager->newDisplayString();
	GameFont *font = TheHeaderTemplateManager->getFontFromTemplate(headerTemplate);
	if (font != nullptr)
	{
		string->setFont(font);
	}
	string->setWordWrap(0);
	return string;
}

void freeString(DisplayString *&string)
{
	if (string != nullptr)
	{
		TheDisplayStringManager->freeDisplayString(string);
		string = nullptr;
	}
}

std::wstring lowered(std::wstring text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
	return text;
}

FriendRow makeRow(const FriendsEntry &entry)
{
	FriendRow row;
	row.userID = entry.user_id;
	row.name = from_utf8(entry.display_name);
	row.status = from_utf8(entry.presence);
	row.mode = entry.status;
	return row;
}

void sortRows(std::vector<FriendRow> &rows)
{
	std::sort(rows.begin(), rows.end(), [](const FriendRow &a, const FriendRow &b) { return lowered(a.name) < lowered(b.name); });
}

// The friends and the requests are copied under a lock, so the snapshot is taken at most twice a second.
void snapshotFriends(GenOnlineSocialInterface *social)
{
	s_onlineRows.clear();
	s_requestRows.clear();
	s_friendsTotal = 0;
	for (const auto &entry : social->GetCachedFriendsList())
	{
		++s_friendsTotal;
		if (entry.second.online)
		{
			s_onlineRows.push_back(makeRow(entry.second));
		}
	}
	for (const auto &entry : social->GetCachedRequestsList())
	{
		s_requestRows.push_back(makeRow(entry.second));
	}
	sortRows(s_onlineRows);
	sortRows(s_requestRows);
}

void refresh()
{
	GenOnlineAuthInterface *auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	GenOnlineSocialInterface *social = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	const std::wstring name = (auth != nullptr && auth->HasDisplayName()) ? auth->GetDisplayNameW() : std::wstring();
	const Int notifications = (social != nullptr) ? social->GetNumTotalNotifications() : 0;

	const auto now = std::chrono::steady_clock::now();
	if (social != nullptr && (s_friendsOnline < 0 || now - s_lastRefresh >= std::chrono::milliseconds(FRIENDS_REFRESH_MS)))
	{
		snapshotFriends(social);
		s_lastRefresh = now;
	}
	const Int friendsOnline = (social != nullptr) ? (Int)s_onlineRows.size() : 0;

	const std::string &localStatus = GenOnlineServicesManager::Settings.Social_PresenceStatus();
	if (name == s_displayName && friendsOnline == s_friendsOnline && notifications == s_notifications && localStatus == s_localStatus)
	{
		return;
	}

	s_displayName = name;
	s_friendsOnline = friendsOnline;
	s_notifications = notifications;
	s_localStatus = localStatus;
	s_name->setText(UnicodeString(name.c_str()));
	UnicodeString marker;
	marker.format(L"%d", notifications);
	s_marker->setText(marker);
}

void closeDropdown()
{
	if (s_panel != nullptr)
	{
		TheWindowManager->winDestroy(s_panel);
		s_panel = nullptr;
	}
	if (s_catcher != nullptr)
	{
		TheWindowManager->winDestroy(s_catcher);
		s_catcher = nullptr;
	}
	s_hits.clear();
}

void signOutConfirmed()
{
	GenOnlineAuthInterface *auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (auth != nullptr)
	{
		// The teardown waits for the logout, because it cancels the requests that end the session.
		auth->LogoutOfMyAccount([]()
			{
				if (GenOnlineServicesManager::GetInstance() != nullptr)
				{
					GenOnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::USER_LOGOUT);
				}
			});
	}
}

void runAction(const PanelHit &hit)
{
	GenOnlineSocialInterface *social = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	switch (hit.action)
	{
	case ACTION_STATUS:
		if (social != nullptr)
		{
			social->SetLocalPresenceStatus(STATUSES[hit.userID].value);
		}
		return;
	case ACTION_ACCEPT:
		if (social != nullptr)
		{
			social->AcceptPendingRequest(hit.userID);
		}
		return;
	case ACTION_IGNORE:
		if (social != nullptr)
		{
			social->RejectPendingRequest(hit.userID);
		}
		return;
	case ACTION_MESSAGES:
	case ACTION_FRIENDS:
		closeDropdown();
		GameSpyOpenOverlay(GSOVERLAY_BUDDY);
		return;
	case ACTION_PROFILE:
	{
		GenOnlineAuthInterface *auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
		closeDropdown();
		if (auth != nullptr)
		{
			GenOnlineSetLookAtPlayer(auth->GetUserID(), UnicodeString(auth->GetDisplayNameW().c_str()));
			GameSpyOpenOverlay(GSOVERLAY_PLAYERINFO);
		}
		return;
	}
	case ACTION_SIGN_OUT:
		closeDropdown();
		MessageBoxYesNo(UnicodeString(L"Log Out"), UnicodeString(L"Are you sure you want to log out?"), signOutConfirmed, nullptr);
		return;
	}
}

Bool inside(const PanelHit &hit, Int x, Int y)
{
	return x >= hit.left && x < hit.right && y >= hit.top && y < hit.bottom;
}

Int textWidth(DisplayString *string, const UnicodeString &text)
{
	Int width, height;
	string->setText(text);
	string->getSize(&width, &height);
	return width;
}

Int textHeight(DisplayString *string)
{
	Int width, height;
	string->getSize(&width, &height);
	return height;
}

void drawText(DisplayString *string, const UnicodeString &text, Int x, Int y, Int limit, Color color)
{
	Int width, height;
	string->setText(text);
	string->getSize(&width, &height);
	IRegion2D clip;
	clip.lo.x = x;
	clip.lo.y = y - height;
	clip.hi.x = limit;
	clip.hi.y = y + 2 * height;
	string->setClipRegion(&clip);
	string->draw(x, y, color, GameMakeColor(0, 0, 0, 255));
}

Color statusColor(const std::string &status)
{
	if (status == "idle")
	{
		return GameMakeColor(255, 176, 32, 255);
	}
	if (status == "do_not_disturb")
	{
		return GameMakeColor(255, 74, 74, 255);
	}
	if (status == "invisible")
	{
		return GameMakeColor(140, 148, 170, 255);
	}
	return GameMakeColor(56, 224, 122, 255);
}

// Corners are cut by one pixel to look round, the invisible status is a hollow ring.
void drawStatusDot(const std::string &status, Int x, Int y, Int size)
{
	const Color color = statusColor(status);
	if (status != "invisible")
	{
		TheWindowManager->winFillRect(color, 1.0f, x + 1, y, x + size - 1, y + size);
		TheWindowManager->winFillRect(color, 1.0f, x, y + 1, x + size, y + size - 1);
		return;
	}
	TheWindowManager->winFillRect(color, 1.0f, x + 1, y, x + size - 1, y + 1);
	TheWindowManager->winFillRect(color, 1.0f, x + 1, y + size - 1, x + size - 1, y + size);
	TheWindowManager->winFillRect(color, 1.0f, x, y + 1, x + 1, y + size - 1);
	TheWindowManager->winFillRect(color, 1.0f, x + size - 1, y + 1, x + size, y + size - 1);
}

void drawButton(const PanelHit &hit, const UnicodeString &label, Int mouseX, Int mouseY)
{
	const Bool hot = inside(hit, mouseX, mouseY);
	const Color line = GOUIColor(hot ? GOUICOLOR_LINE_HILITE : GOUICOLOR_LINE, GameMakeColor(47, 75, 255, 255));
	TheWindowManager->winFillRect(hot ? GameMakeColor(30, 40, 90, 255) : GameMakeColor(14, 18, 40, 255), 1.0f, hit.left, hit.top, hit.right, hit.bottom);
	GadgetListBoxDrawKitFrame(line, hit.left, hit.top, hit.right - hit.left, hit.bottom - hit.top);
	const Int width = textWidth(s_textSmall, label);
	const Int height = textHeight(s_textSmall);
	drawText(s_textSmall, label, hit.left + (hit.right - hit.left - width) / 2, hit.top + (hit.bottom - hit.top - height) / 2, hit.right, GameMakeColor(255, 255, 255, 255));
}

void drawPanel(GameWindow *window, WinInstanceData *)
{
	if (s_textRegular == nullptr || s_textSmall == nullptr)
	{
		return;
	}

	const MouseIO *mouse = TheMouse->getMouseStatus();
	const Color muted = GOUIColor(GOUICOLOR_MUTED, GameMakeColor(154, 163, 199, 255));
	const Color white = GameMakeColor(255, 255, 255, 255);
	const Int padding = scaleX(PANEL_PADDING);
	const Int row = scaleY(PANEL_ROW);
	const Int gap = scaleY(4);
	const Int width = scaleX(PANEL_WIDTH);
	const Int left = s_sectionRight - width;
	const Int top = s_sectionBottom + scaleY(2);

	// The window grows and shrinks with the lists, so its height comes first.
	const Int onlineCount = (std::min<Int>)((Int)s_onlineRows.size(), PANEL_MAX_ONLINE);
	const Int requestCount = (std::min<Int>)((Int)s_requestRows.size(), PANEL_MAX_REQUESTS);
	const Int buttonHeight = scaleY(PANEL_BUTTON_HEIGHT);
	Int height = padding + STATUS_COUNT * row + gap + row + (std::max<Int>)(onlineCount, 1) * row + gap;
	if (requestCount > 0)
	{
		height += row + requestCount * row + gap;
	}
	height += 2 * buttonHeight + padding;
	window->winSetPosition(left, top);
	window->winSetSize(width, height);

	TheWindowManager->winFillRect(GameMakeColor(5, 7, 15, 242), 1.0f, left, top, left + width, top + height);
	GadgetListBoxDrawKitFrame(GOUIColor(GOUICOLOR_LINE, GameMakeColor(47, 75, 255, 255)), left, top, width, height);

	s_hits.clear();
	const Int textLeft = left + padding;
	const Int textRight = left + width - padding;
	const Int smallHeight = textHeight(s_textSmall);
	const Int regularHeight = textHeight(s_textRegular);
	Int y = top + padding;
	const Int dot = scaleY(7);
	for (Int i = 0; i < STATUS_COUNT; ++i)
	{
		PanelHit hit;
		hit.left = left + scaleX(2);
		hit.right = left + width - scaleX(2);
		hit.top = y;
		hit.bottom = y + row;
		hit.action = ACTION_STATUS;
		hit.userID = i;
		s_hits.push_back(hit);
		const Bool current = s_localStatus == STATUSES[i].value;
		if (current || inside(hit, mouse->pos.x, mouse->pos.y))
		{
			TheWindowManager->winFillRect(current ? GameMakeColor(30, 40, 90, 255) : GameMakeColor(18, 24, 56, 255), 1.0f, hit.left, hit.top, hit.right, hit.bottom);
		}
		drawStatusDot(STATUSES[i].value, textLeft, y + (row - dot) / 2, dot);
		drawText(s_textRegular, TheGameText->FETCH_OR_SUBSTITUTE(STATUSES[i].key, STATUSES[i].text), textLeft + dot + scaleX(6), y + (row - regularHeight) / 2, textRight, current ? GameMakeColor(255, 194, 15, 255) : white);
		y += row;
	}
	y += gap;
	drawText(s_textSmall, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOUserFriendsOnline", L"FRIENDS ONLINE"), textLeft, y + (row - smallHeight) / 2, textRight, muted);
	UnicodeString count;
	count.format(L"%d / %d", (Int)s_onlineRows.size(), s_friendsTotal);
	drawText(s_textSmall, count, textRight - textWidth(s_textSmall, count), y + (row - smallHeight) / 2, textRight, muted);
	y += row;

	if (onlineCount == 0)
	{
		drawText(s_textSmall, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOUserNoFriendsOnline", L"No friends online"), textLeft, y + (row - smallHeight) / 2, textRight, muted);
		y += row;
	}
	for (Int i = 0; i < onlineCount; ++i)
	{
		const FriendRow &entry = s_onlineRows[i];
		drawStatusDot(entry.mode, textLeft, y + (row - dot) / 2, dot);
		Int nameLimit = textRight;
		if (!entry.status.empty())
		{
			const UnicodeString status(entry.status.c_str());
			const Int statusWidth = textWidth(s_textSmall, status);
			nameLimit = textRight - statusWidth - scaleX(6);
			drawText(s_textSmall, status, textRight - statusWidth, y + (row - smallHeight) / 2, textRight, muted);
		}
		drawText(s_textRegular, UnicodeString(entry.name.c_str()), textLeft + dot + scaleX(6), y + (row - regularHeight) / 2, nameLimit, white);
		y += row;
	}
	y += gap;

	const Int buttonGap = scaleX(4);
	if (requestCount > 0)
	{
		drawText(s_textSmall, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOUserRequests", L"REQUESTS"), textLeft, y + (row - smallHeight) / 2, textRight, muted);
		y += row;
		const Int actionWidth = scaleX(44);
		for (Int i = 0; i < requestCount; ++i)
		{
			const FriendRow &entry = s_requestRows[i];
			PanelHit ignore;
			ignore.right = textRight;
			ignore.left = textRight - actionWidth;
			ignore.top = y + scaleY(1);
			ignore.bottom = y + row - scaleY(1);
			ignore.action = ACTION_IGNORE;
			ignore.userID = entry.userID;
			PanelHit accept = ignore;
			accept.right = ignore.left - buttonGap;
			accept.left = accept.right - actionWidth;
			accept.action = ACTION_ACCEPT;
			s_hits.push_back(accept);
			s_hits.push_back(ignore);
			drawButton(accept, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOUserAccept", L"Accept"), mouse->pos.x, mouse->pos.y);
			drawButton(ignore, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOUserIgnore", L"Ignore"), mouse->pos.x, mouse->pos.y);
			drawText(s_textRegular, UnicodeString(entry.name.c_str()), textLeft, y + (row - regularHeight) / 2, accept.left - buttonGap, white);
			y += row;
		}
		y += gap;
	}

	struct ButtonSpec
	{
		PanelAction action;
		const char *key;
		const WideChar *text;
	};
	const ButtonSpec specs[] = {
		{ ACTION_MESSAGES, "GUI:GOUserMessages", L"Messages" },
		{ ACTION_FRIENDS, "GUI:GOUserAllFriends", L"All friends" },
		{ ACTION_PROFILE, "GUI:GOUserMyProfile", L"My profile" },
		{ ACTION_SIGN_OUT, "GUI:GOUserSignOut", L"Sign out" },
	};
	const Int cell = (textRight - textLeft - buttonGap) / 2;
	for (Int i = 0; i < 4; ++i)
	{
		PanelHit hit;
		hit.left = textLeft + (i % 2) * (cell + buttonGap);
		hit.right = hit.left + cell;
		hit.top = y + (i / 2) * buttonHeight;
		hit.bottom = hit.top + buttonHeight - scaleY(2);
		hit.action = specs[i].action;
		hit.userID = 0;
		s_hits.push_back(hit);
		drawButton(hit, TheGameText->FETCH_OR_SUBSTITUTE(specs[i].key, specs[i].text), mouse->pos.x, mouse->pos.y);
	}
}

WindowMsgHandledType inputPanel(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
	case GWM_LEFT_DOWN:
		return MSG_HANDLED;
	case GWM_LEFT_UP:
	{
		const MouseIO *mouse = TheMouse->getMouseStatus();
		for (const PanelHit &hit : s_hits)
		{
			if (inside(hit, mouse->pos.x, mouse->pos.y))
			{
				const PanelHit chosen = hit;
				runAction(chosen);
				break;
			}
		}
		return MSG_HANDLED;
	}
	}
	return MSG_IGNORED;
}

// A full screen window under the panel, a click anywhere else closes the dropdown.
WindowMsgHandledType inputCatcher(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
	case GWM_LEFT_DOWN:
	case GWM_RIGHT_DOWN:
		return MSG_HANDLED;
	case GWM_LEFT_UP:
	case GWM_RIGHT_UP:
		closeDropdown();
		return MSG_HANDLED;
	}
	return MSG_IGNORED;
}

void openDropdown()
{
	if (s_window == nullptr || s_panel != nullptr)
	{
		return;
	}

	GameWindow *parent = s_window->winGetParent();
	s_catcher = TheWindowManager->winCreate(parent, WIN_STATUS_ENABLED | WIN_STATUS_NO_FOCUS, 0, 0, TheDisplay->getWidth(), TheDisplay->getHeight(), nullptr, nullptr);
	s_panel = TheWindowManager->winCreate(parent, WIN_STATUS_ENABLED | WIN_STATUS_NO_FOCUS, 0, 0, scaleX(PANEL_WIDTH), scaleY(PANEL_ROW), nullptr, nullptr);
	if (s_catcher != nullptr)
	{
		s_catcher->winSetInputFunc(inputCatcher);
	}
	if (s_panel != nullptr)
	{
		s_panel->winSetInputFunc(inputPanel);
		s_panel->winSetDrawFunc(drawPanel);
	}
}

void drawChevron(Int centerX, Int centerY, Int halfWidth, Color color)
{
	const Int halfHeight = halfWidth / 2;
	TheWindowManager->winDrawLine(color, 1.0f, centerX - halfWidth, centerY - halfHeight, centerX, centerY + halfHeight);
	TheWindowManager->winDrawLine(color, 1.0f, centerX, centerY + halfHeight, centerX + halfWidth, centerY - halfHeight);
}

void drawUserSection(GameWindow *window, WinInstanceData *instData)
{
	if (s_name == nullptr)
	{
		return;
	}

	refresh();
	if (s_displayName.empty())
	{
		return;
	}

	const Int padding = scaleX(SECTION_PADDING);
	const Int gap = scaleX(SECTION_GAP);
	const Int chevron = scaleX(CHEVRON_WIDTH);
	Int nameWidth, nameHeight, markerTextWidth, markerTextHeight;
	s_name->getSize(&nameWidth, &nameHeight);
	const Int dot = scaleY(7);
	s_marker->getSize(&markerTextWidth, &markerTextHeight);
	const Int markerPadding = scaleX(MARKER_PADDING);
	const Int markerWidth = markerTextWidth + 2 * markerPadding;
	const Int fixed = 2 * padding + dot + gap + gap + chevron + (s_notifications > 0 ? gap + markerWidth : 0);
	const Int width = (std::min)(scaleX(SECTION_MAX_WIDTH), fixed + nameWidth);
	const Int height = scaleY(SECTION_HEIGHT);
	window->winSetSize(width, height);
	window->winSetPosition(s_sectionRight - width, s_sectionBottom - height);

	Int x, y;
	window->winGetScreenPosition(&x, &y);
	const Color line = GOUIColor((s_hovered || s_panel != nullptr) ? GOUICOLOR_LINE_HILITE : GOUICOLOR_LINE, GameMakeColor(47, 75, 255, 255));
	TheWindowManager->winFillRect(GameMakeColor(5, 7, 15, 209), 1.0f, x, y, x + width, y + height);
	GadgetListBoxDrawKitFrame(line, x, y, width, height);

	const Color shadow = GameMakeColor(0, 0, 0, 255);
	const Color muted = GOUIColor(GOUICOLOR_MUTED, GameMakeColor(154, 163, 199, 255));
	Int right = x + width - padding;

	drawChevron(right - chevron / 2, y + height / 2, chevron / 2, muted);
	right -= chevron + gap;

	if (s_notifications > 0)
	{
		const Int markerHeight = markerTextHeight + scaleY(2);
		const Int markerX = right - markerWidth;
		const Int markerY = y + (height - markerHeight) / 2;
		TheWindowManager->winFillRect(GameMakeColor(255, 74, 74, 255), 1.0f, markerX, markerY, markerX + markerWidth, markerY + markerHeight);
		s_marker->draw(markerX + markerPadding, markerY + (markerHeight - markerTextHeight) / 2, GameMakeColor(255, 255, 255, 255), shadow);
		right = markerX - gap;
	}

	drawStatusDot(s_localStatus, x + padding, y + (height - dot) / 2, dot);
	const Int nameX = x + padding + dot + gap;
	IRegion2D clip;
	clip.lo.x = nameX;
	clip.lo.y = y;
	clip.hi.x = right;
	clip.hi.y = y + height;
	s_name->setClipRegion(&clip);
	s_name->draw(nameX, y + (height - nameHeight) / 2, GameMakeColor(255, 194, 15, 255), shadow);
}

WindowMsgHandledType inputUserSection(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
	case GWM_MOUSE_ENTERING:
		s_hovered = TRUE;
		return MSG_HANDLED;
	case GWM_MOUSE_LEAVING:
		s_hovered = FALSE;
		return MSG_HANDLED;
	case GWM_LEFT_DOWN:
		return MSG_HANDLED;
	case GWM_LEFT_UP:
		if (!s_displayName.empty())
		{
			openDropdown();
		}
		return MSG_HANDLED;
	}
	return MSG_IGNORED;
}

} // namespace

void GenOnlineUserSectionShow(GameWindow *parent)
{
	if (parent == nullptr)
	{
		return;
	}

	// Windows left from an earlier screen went down with its layout, so only the strings are reused.
	s_window = nullptr;
	s_panel = nullptr;
	s_catcher = nullptr;
	s_hits.clear();
	freeString(s_name);
	freeString(s_marker);
	freeString(s_textRegular);
	freeString(s_textSmall);

	s_sectionRight = scaleX(SECTION_RIGHT);
	s_sectionBottom = scaleY(SECTION_BOTTOM);
	const Int width = scaleX(SECTION_MAX_WIDTH);
	const Int height = scaleY(SECTION_HEIGHT);
	s_window = TheWindowManager->winCreate(parent, WIN_STATUS_ENABLED | WIN_STATUS_NO_FOCUS, s_sectionRight - width, s_sectionBottom - height, width, height, nullptr, nullptr);
	if (s_window == nullptr)
	{
		return;
	}

	s_name = newString("GOLabelRegular");
	s_marker = newString("GOLabelSmall");
	s_textRegular = newString("GOLabelRegular");
	s_textSmall = newString("GOLabelSmall");
	s_hovered = FALSE;
	s_friendsOnline = -1;
	s_notifications = -1;
	s_displayName.clear();
	s_localStatus.clear();
	s_window->winSetInputFunc(inputUserSection);
	s_window->winSetDrawFunc(drawUserSection);
}

void GenOnlineUserSectionHide()
{
	closeDropdown();
	if (s_window != nullptr)
	{
		TheWindowManager->winDestroy(s_window);
		s_window = nullptr;
	}
	freeString(s_name);
	freeString(s_marker);
	freeString(s_textRegular);
	freeString(s_textSmall);
	s_hovered = FALSE;
}
