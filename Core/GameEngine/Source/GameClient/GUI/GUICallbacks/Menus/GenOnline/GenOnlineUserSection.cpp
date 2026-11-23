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
#include "GameClient/Image.h"
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
constexpr Int MARKER_PADDING = 3;
constexpr Int COMMUNICATOR_GAP = 4;
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
GameWindow *s_communicator = nullptr;
GameWindow *s_catcher = nullptr;
GameWindow *s_panel = nullptr;
DisplayString *s_name = nullptr;
DisplayString *s_marker = nullptr;
DisplayString *s_textRegular = nullptr;
DisplayString *s_textSmall = nullptr;
Bool s_hovered = FALSE;
Bool s_communicatorHovered = FALSE;
Int s_friendsOnline = -1;
Int s_friendsTotal = 0;
Int s_notifications = -1;
Int s_unreadConversations = 0;
Int s_pendingRequests = 0;
Int s_sectionLeft = 0;
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
	// The badge counts what still waits for the player, so it clears once the requests are answered and the conversations read.
	s_unreadConversations = (social != nullptr) ? social->GetNumUnreadConversations() : 0;
	s_pendingRequests = (social != nullptr) ? (Int)social->GetCachedRequestsList().size() : 0;
	const Int notifications = s_unreadConversations + s_pendingRequests;

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
	marker.format(notifications > 9 ? L"9+" : L"%d", notifications);
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
	if (status == "offline")
	{
		return GameMakeColor(93, 99, 130, 255);
	}
	return GameMakeColor(56, 224, 122, 255);
}

// Corners are cut by one pixel to look round, the invisible and offline statuses are a hollow ring.
void drawStatusDot(const std::string &status, Int x, Int y, Int size)
{
	// The dot is a disc drawn row by row, and an invisible or offline status leaves a ring so the dot never reads as a checkbox.
	const Color color = statusColor(status);
	const Bool ring = status == "invisible" || status == "offline";
	const Real radius = size * 0.5f;
	const Real inner = radius - (std::max)(1.5f, size * 0.2f);
	for (Int row = 0; row < size; ++row)
	{
		const Real dy = row + 0.5f - radius;
		const Int outerHalf = static_cast<Int>(sqrtf((std::max)(0.0f, radius * radius - dy * dy)) + 0.5f);
		if (outerHalf <= 0)
		{
			continue;
		}
		const Int center = x + size / 2;
		const Int left = x + (size - 2 * outerHalf) / 2;
		const Int right = left + 2 * outerHalf;
		const Int innerHalf = ring && inner > 0.0f && fabsf(dy) < inner ? static_cast<Int>(sqrtf(inner * inner - dy * dy) + 0.5f) : 0;
		if (innerHalf <= 0)
		{
			TheWindowManager->winFillRect(color, 1.0f, left, y + row, right, y + row + 1);
			continue;
		}
		TheWindowManager->winFillRect(color, 1.0f, left, y + row, center - innerHalf, y + row + 1);
		TheWindowManager->winFillRect(color, 1.0f, center + innerHalf, y + row, right, y + row + 1);
	}
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
	height += buttonHeight + padding;
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
		{ ACTION_PROFILE, "GUI:GOUserMyProfile", L"My profile" },
		{ ACTION_SIGN_OUT, "GUI:GOUserSignOut", L"Sign out" },
	};
	const Int cell = (textRight - textLeft - buttonGap) / 2;
	for (Int i = 0; i < 2; ++i)
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

// The arrow is the drop-down button art of the kit combo boxes, at their 21 by 22 proportion over the full height.
Int arrowWidth(Int height)
{
	return (height * 21 + 11) / 22;
}

void drawArrow(Int left, Int top, Int height, Bool lit)
{
	const Image *image = TheMappedImageCollection->findImageByName(lit ? "VSliderDownButtonHilite" : "VSliderDownButtonEnabled");
	if (image != nullptr)
	{
		TheWindowManager->winDrawImage(image, left, top, left + arrowWidth(height), top + height);
	}
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
	const Int height = scaleY(SECTION_HEIGHT);
	const Int arrow = arrowWidth(height);
	Int nameWidth, nameHeight;
	s_name->getSize(&nameWidth, &nameHeight);
	const Int dot = scaleY(7);
	const Int fixed = padding + dot + gap + gap + arrow;
	const Int width = (std::min)(scaleX(SECTION_MAX_WIDTH), fixed + nameWidth);
	window->winSetSize(width, height);
	window->winSetPosition(s_sectionRight - width, s_sectionBottom - height);
	s_sectionLeft = s_sectionRight - width;
	if (s_communicator != nullptr)
	{
		s_communicator->winSetPosition(s_sectionLeft - scaleX(COMMUNICATOR_GAP) - height, s_sectionBottom - height);
	}

	Int x, y;
	window->winGetScreenPosition(&x, &y);
	const Color line = GOUIColor((s_hovered || s_panel != nullptr) ? GOUICOLOR_LINE_HILITE : GOUICOLOR_LINE, GameMakeColor(47, 75, 255, 255));
	TheWindowManager->winFillRect(GameMakeColor(5, 7, 15, 209), 1.0f, x, y, x + width, y + height);
	GadgetListBoxDrawKitFrame(line, x, y, width, height);

	const Color shadow = GameMakeColor(0, 0, 0, 255);
	drawArrow(x + width - arrow, y, height, s_hovered || s_panel != nullptr);
	Int right = x + width - arrow - gap;

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

// A speech bubble with the count of unread conversations and friend requests in a red badge on its top right corner.
void drawCommunicatorButton(GameWindow *window, WinInstanceData *)
{
	if (s_marker == nullptr || s_displayName.empty() || s_sectionLeft == 0)
	{
		return;
	}
	Int x, y, size, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&size, &height);
	const Color line = GOUIColor((s_communicatorHovered || GameSpyIsOverlayOpen(GSOVERLAY_BUDDY)) ? GOUICOLOR_LINE_HILITE : GOUICOLOR_LINE, GameMakeColor(47, 75, 255, 255));
	TheWindowManager->winFillRect(GameMakeColor(5, 7, 15, 209), 1.0f, x, y, x + size, y + height);
	GadgetListBoxDrawKitFrame(line, x, y, size, height);

	const Color icon = s_notifications > 0 ? GameMakeColor(255, 255, 255, 255) : GOUIColor(GOUICOLOR_MUTED, GameMakeColor(154, 163, 199, 255));
	const Int left = x + size / 4;
	const Int right = x + size - size / 4;
	const Int top = y + height / 4;
	const Int bottom = y + height - height / 3;
	TheWindowManager->winOpenRect(icon, 1.0f, left, top, right, bottom);
	TheWindowManager->winDrawLine(icon, 1.0f, left + size / 8, bottom, left + size / 8, bottom + height / 6);
	TheWindowManager->winDrawLine(icon, 1.0f, left + size / 8, bottom + height / 6, left + size / 4, bottom);

	if (s_notifications <= 0)
	{
		return;
	}
	Int textWidth, textHeight;
	s_marker->getSize(&textWidth, &textHeight);
	const Int badgeHeight = textHeight + scaleY(1);
	const Int badgeWidth = (std::max)(badgeHeight, textWidth + 2 * scaleX(MARKER_PADDING));
	const Int badgeX = x + size - badgeWidth / 2 - scaleX(1);
	const Int badgeY = y - badgeHeight / 3;
	const Color red = GameMakeColor(255, 74, 74, 255);
	TheWindowManager->winFillRect(red, 1.0f, badgeX + 1, badgeY, badgeX + badgeWidth - 1, badgeY + badgeHeight);
	TheWindowManager->winFillRect(red, 1.0f, badgeX, badgeY + 1, badgeX + badgeWidth, badgeY + badgeHeight - 1);
	s_marker->draw(badgeX + (badgeWidth - textWidth) / 2, badgeY + (badgeHeight - textHeight) / 2, GameMakeColor(255, 255, 255, 255), GameMakeColor(0, 0, 0, 255));
}

void communicatorTooltip(GameWindow *, WinInstanceData *, UnsignedInt)
{
	std::wstring text = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOCommunicator", L"Communicator").str();
	if (s_unreadConversations > 0)
	{
		text += L"\n" + std::to_wstring(s_unreadConversations) + (s_unreadConversations == 1 ? L" unread conversation" : L" unread conversations");
	}
	if (s_pendingRequests > 0)
	{
		text += L"\n" + std::to_wstring(s_pendingRequests) + (s_pendingRequests == 1 ? L" friend request" : L" friend requests");
	}
	TheMouse->setCursorTooltip(UnicodeString(text.c_str()));
}

// Unread messages open the latest conversation, else waiting requests open their tab, else the friends list opens.
void openCommunicator()
{
	GenOnlineSocialInterface *social = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (GameSpyIsOverlayOpen(GSOVERLAY_BUDDY))
	{
		GameSpyCloseOverlay(GSOVERLAY_BUDDY);
		return;
	}
	if (social != nullptr && s_unreadConversations > 0)
	{
		GenOnlineOpenCommunicator(GENONLINE_COMMUNICATOR_FRIENDS, social->GetLastConversationPeer());
	}
	else if (s_pendingRequests > 0)
	{
		GenOnlineOpenCommunicator(GENONLINE_COMMUNICATOR_REQUESTS, -1);
	}
	else
	{
		GenOnlineOpenCommunicator(GENONLINE_COMMUNICATOR_FRIENDS, -1);
	}
}

WindowMsgHandledType inputCommunicatorButton(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
	case GWM_MOUSE_ENTERING:
		s_communicatorHovered = TRUE;
		return MSG_HANDLED;
	case GWM_MOUSE_LEAVING:
		s_communicatorHovered = FALSE;
		return MSG_HANDLED;
	case GWM_LEFT_DOWN:
		return MSG_HANDLED;
	case GWM_LEFT_UP:
		if (!s_displayName.empty())
		{
			closeDropdown();
			openCommunicator();
		}
		return MSG_HANDLED;
	}
	return MSG_IGNORED;
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

void GenOnlineDrawPresenceDot(const char *status, Int x, Int y, Int size)
{
	drawStatusDot(status != nullptr ? status : "offline", x, y, size);
}

void GenOnlineUserSectionShow(GameWindow *parent)
{
	if (parent == nullptr)
	{
		return;
	}

	// Windows left from an earlier screen went down with its layout, so only the strings are reused.
	s_window = nullptr;
	s_communicator = nullptr;
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

	s_sectionLeft = 0;
	s_communicatorHovered = FALSE;
	s_communicator = TheWindowManager->winCreate(parent, WIN_STATUS_ENABLED | WIN_STATUS_NO_FOCUS, s_sectionRight - width - scaleX(COMMUNICATOR_GAP) - height, s_sectionBottom - height, height, height, nullptr, nullptr);
	if (s_communicator != nullptr)
	{
		s_communicator->winSetInputFunc(inputCommunicatorButton);
		s_communicator->winSetDrawFunc(drawCommunicatorButton);
		s_communicator->winSetTooltipFunc(communicatorTooltip);
	}
}

void GenOnlineUserSectionHide()
{
	closeDropdown();
	if (s_window != nullptr)
	{
		TheWindowManager->winDestroy(s_window);
		s_window = nullptr;
	}
	if (s_communicator != nullptr)
	{
		TheWindowManager->winDestroy(s_communicator);
		s_communicator = nullptr;
	}
	freeString(s_name);
	freeString(s_marker);
	freeString(s_textRegular);
	freeString(s_textSmall);
	s_hovered = FALSE;
}
