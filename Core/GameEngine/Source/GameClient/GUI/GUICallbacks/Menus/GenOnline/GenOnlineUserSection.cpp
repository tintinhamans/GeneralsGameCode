#include "PreRTS.h"

#include "GameClient/GenOnlineUserSection.h"
#include "GameClient/GenOnlineChatWrap.h"
#include "GameClient/GenOnlineDockChat.h"

#include "Common/GOUIColors.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GenOnlineMenus.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/Image.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"
#include "GameNetwork/GenOnline/GenOnlineAuth.h"
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineSocialInterface.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <vector>

namespace
{

// 800x600 layout units, right aligned to the content frame and sitting on the bottom ruler strip.
constexpr Int SECTION_RIGHT = 759;
constexpr Int SECTION_BOTTOM = 592;
constexpr Int SECTION_MAX_WIDTH = 190;
constexpr Int SECTION_HEIGHT = 28;
constexpr Int CHAT_RESERVE_WIDTH = 300;
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

constexpr Int TAB_WIDTH = 100;
constexpr Int OVERFLOW_WIDTH = 40;
constexpr Int OVERFLOW_LIST_WIDTH = 150;
constexpr Int OVERFLOW_ROW = 18;
constexpr Int OVERFLOW_MAX_ROWS = 8;
constexpr int64_t OVERFLOW_TAB = -1;
constexpr Int CHAT_WIDTH = 420;
constexpr Int CHAT_HEIGHT = 240;
constexpr Int CHAT_TITLE = 20;
constexpr Int CHAT_ENTRY = 22;
constexpr Int CHAT_PAD = 4;
constexpr Int CHAT_LINES = 500;

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
GameWindow *s_tabs = nullptr;
GameWindow *s_chat = nullptr;
GameWindow *s_chatLog = nullptr;
GameWindow *s_chatEntry = nullptr;
GameWindow *s_overflow = nullptr;
GameWindow *s_overflowCatcher = nullptr;
int64_t s_chatPeer = -1;
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
Int s_pillRight = 0;
Int s_communicatorLeft = 0;
std::wstring s_displayName;
std::string s_localStatus;
std::vector<FriendRow> s_onlineRows;
std::vector<FriendRow> s_requestRows;
std::vector<PanelHit> s_hits;

struct ConversationLine
{
	UnicodeString text;
	Color color = 0;
	uint32_t key = 0;
};

// One private conversation of the dock. The lines mirror the cache of the social interface, the tab count is the lines that came in while the window was closed.
struct Conversation
{
	int64_t peer = -1;
	UnicodeString name;
	UnicodeString status;
	std::string presence = "offline";
	std::deque<ConversationLine> lines;
	Int unread = 0;
};

struct TabRect
{
	int64_t peer;
	Int left, top, right, bottom;
};

std::vector<Conversation> s_conversations;
std::vector<TabRect> s_tabRects;
std::vector<TabRect> s_overflowHits;
PanelHit s_closeHit = {};
PanelHit s_endHit = {};
PanelHit s_profileHit = {};
Int s_visibleTabs = 0;
std::chrono::steady_clock::time_point s_lastRefresh;

void resetConversations();
void refreshConversationPeers();
void closeChat();
void closeOverflow();
void onDockMessage(int64_t peer, uint32_t key, UnicodeString text);
void onDockReloaded(int64_t peer);
void onDockLineRemoved(int64_t peer, uint32_t key);

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
		refreshConversationPeers();
		s_lastRefresh = now;
	}
	const Int friendsOnline = (social != nullptr) ? (Int)s_onlineRows.size() : 0;

	const std::string &localStatus = GenOnlineServicesManager::Settings.Social_PresenceStatus();
	if (name == s_displayName && friendsOnline == s_friendsOnline && notifications == s_notifications && localStatus == s_localStatus)
	{
		return;
	}

	// Signing out ends every conversation tab.
	if (name.empty() && !s_displayName.empty())
	{
		resetConversations();
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
	const Int left = (std::max<Int>)(0, s_pillRight - width);

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
	// The panel opens upwards from the pill and stays on screen.
	const Int top = (std::max<Int>)(0, s_sectionBottom - scaleY(SECTION_HEIGHT) - scaleY(2) - height);
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

	closeChat();
	closeOverflow();
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

// The dropdown opens upwards, so the arrow points up.
void drawArrow(Int left, Int top, Int height, Bool lit)
{
	const Image *image = TheMappedImageCollection->findImageByName(lit ? "VSliderUpButtonHilite" : "VSliderUpButtonEnabled");
	if (image != nullptr)
	{
		TheWindowManager->winDrawImage(image, left, top, left + arrowWidth(height), top + height);
	}
}

Bool chatDockReady()
{
	return s_window != nullptr && !s_displayName.empty();
}

Conversation *findConversation(int64_t peer)
{
	for (Conversation &conversation : s_conversations)
	{
		if (conversation.peer == peer)
		{
			return &conversation;
		}
	}
	return nullptr;
}

void closeOverflow()
{
	if (s_overflow != nullptr)
	{
		TheWindowManager->winDestroy(s_overflow);
		s_overflow = nullptr;
	}
	if (s_overflowCatcher != nullptr)
	{
		TheWindowManager->winDestroy(s_overflowCatcher);
		s_overflowCatcher = nullptr;
	}
	s_overflowHits.clear();
}

void closeChat()
{
	if (s_chat != nullptr)
	{
		TheWindowManager->winDestroy(s_chat);
	}
	s_chat = nullptr;
	s_chatLog = nullptr;
	s_chatEntry = nullptr;
	s_chatPeer = -1;
}

void addLogLine(const ConversationLine &line)
{
	const Int row = GadgetListBoxAddEntryText(s_chatLog, GenOnlineBreakLongWords(s_chatLog, line.text), line.color, -1, -1);
	if (row >= 0 && line.key != 0)
	{
		GadgetListBoxSetItemData(s_chatLog, reinterpret_cast<void *>(static_cast<uintptr_t>(line.key)), row, 0);
	}
}

Bool isOwnLine(int64_t peer, uint32_t key)
{
	GenOnlineSocialInterface *social = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	GenOnlineAuthInterface *auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
	if (key == 0 || social == nullptr || auth == nullptr)
	{
		return FALSE;
	}
	const GenOnlineRealtime::ChatMessage *message = social->FindConversationMessage(peer, key);
	return message != nullptr && message->author.id == auth->GetUserID();
}

// Whispers use the private chat colour, the own lines the self colour and the notes of the service the muted one.
Color lineColor(int64_t peer, uint32_t key)
{
	if (key == 0)
	{
		return GOUIColor(GOUICOLOR_MUTED, GameMakeColor(154, 163, 199, 255));
	}
	return GameSpyColor[isOwnLine(peer, key) ? GSCOLOR_CHAT_SELF : GSCOLOR_CHAT_PRIVATE];
}

void refreshPeer(Conversation &conversation)
{
	UnicodeString name;
	GenOnlineGetConversationPeer(conversation.peer, name, conversation.presence, conversation.status);
	if (!name.isEmpty())
	{
		conversation.name = name;
	}
	else if (conversation.name.isEmpty())
	{
		conversation.name.format(L"Player %I64d", conversation.peer);
	}
}

void refreshConversationPeers()
{
	GenOnlineSocialInterface *social = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	for (Conversation &conversation : s_conversations)
	{
		refreshPeer(conversation);
		// The communicator and the service also mark a conversation read, so the tab count follows them.
		if (social == nullptr || social->GetNumberUnreadChatMessagesForUser(conversation.peer) == 0)
		{
			conversation.unread = 0;
		}
	}
}

void resetConversations()
{
	closeChat();
	closeOverflow();
	s_conversations.clear();
}

// The lines of a conversation come from the cache of the social interface, the same one the communicator shows.
void loadLines(Conversation &conversation)
{
	conversation.lines.clear();
	GenOnlineSocialInterface *social = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (social == nullptr)
	{
		return;
	}
	for (const GenOnlineRealtime::DirectChatLine &line : social->GetChatMessagesForUser(conversation.peer))
	{
		ConversationLine entry;
		entry.text = GenOnlineSocialInterface::GetChatLineText(line);
		entry.key = line.key;
		entry.color = lineColor(conversation.peer, line.key);
		conversation.lines.push_back(entry);
		if (static_cast<Int>(conversation.lines.size()) > CHAT_LINES)
		{
			conversation.lines.pop_front();
		}
	}
}

void fillLog(const Conversation &conversation)
{
	if (s_chatLog == nullptr)
	{
		return;
	}
	GadgetListBoxReset(s_chatLog);
	GadgetListBoxBatchAddScope batch(s_chatLog);
	for (const ConversationLine &line : conversation.lines)
	{
		addLogLine(line);
	}
}

Conversation &ensureConversation(int64_t peer, Bool &created)
{
	created = FALSE;
	Conversation *existing = findConversation(peer);
	if (existing != nullptr)
	{
		return *existing;
	}
	Conversation conversation;
	conversation.peer = peer;
	refreshPeer(conversation);
	loadLines(conversation);
	s_conversations.push_back(conversation);
	created = TRUE;
	return s_conversations.back();
}

void addConversationLine(Conversation &conversation, const UnicodeString &text, Color color, uint32_t key, Bool incoming)
{
	ConversationLine line;
	line.text = text;
	line.color = color;
	line.key = key;
	conversation.lines.push_back(line);
	if (static_cast<Int>(conversation.lines.size()) > CHAT_LINES)
	{
		conversation.lines.pop_front();
	}
	if (conversation.peer == s_chatPeer && s_chatLog != nullptr)
	{
		addLogLine(line);
	}
	else if (incoming)
	{
		++conversation.unread;
	}
}

// Runs for every line of every conversation, also while no dock is on screen, so nothing here may hold a window.
void onDockMessage(int64_t peer, uint32_t key, UnicodeString text)
{
	if (peer < 0 || s_displayName.empty())
	{
		return;
	}
	GenOnlineSocialInterface *social = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	Bool created;
	Conversation &conversation = ensureConversation(peer, created);
	const Bool incoming = key != 0 && !isOwnLine(peer, key) && !GameSpyIsOverlayOpen(GSOVERLAY_BUDDY);
	if (created)
	{
		// The new conversation was loaded from the cache that already holds this line.
		conversation.unread = incoming ? 1 : 0;
	}
	else
	{
		addConversationLine(conversation, text, lineColor(peer, key), key, incoming);
	}
	if (incoming && peer == s_chatPeer && social != nullptr)
	{
		social->ViewConversation(peer);
	}
}

void onDockReloaded(int64_t peer)
{
	for (Conversation &conversation : s_conversations)
	{
		if (peer == -1 || conversation.peer == peer)
		{
			loadLines(conversation);
			if (conversation.peer == s_chatPeer)
			{
				fillLog(conversation);
			}
		}
	}
}

void onDockLineRemoved(int64_t peer, uint32_t key)
{
	Conversation *conversation = findConversation(peer);
	if (conversation == nullptr)
	{
		return;
	}
	for (auto it = conversation->lines.begin(); it != conversation->lines.end(); ++it)
	{
		if (it->key == key)
		{
			conversation->lines.erase(it);
			break;
		}
	}
	if (peer != s_chatPeer || s_chatLog == nullptr)
	{
		return;
	}
	const Int numEntries = GadgetListBoxGetNumEntries(s_chatLog);
	for (Int row = 0; row < numEntries; ++row)
	{
		if (reinterpret_cast<uintptr_t>(GadgetListBoxGetItemData(s_chatLog, row, 0)) == key)
		{
			TheWindowManager->winSendSystemMsg(s_chatLog, GLM_DEL_ENTRY, static_cast<WindowMsgData>(row), 0);
			return;
		}
	}
}

// The chat list and the entry line are gadgets with the kit colours, the window frame and the entry fill are drawn by the window behind them.
void createChatGadgets(const Conversation &conversation, Int width, Int height)
{
	GameFont *font = TheHeaderTemplateManager->getFontFromTemplate("GOLabelSmall");
	const Color white = GameMakeColor(255, 255, 255, 255);
	const Color black = GameMakeColor(0, 0, 0, 255);
	const Int pad = scaleX(CHAT_PAD);
	const Int title = scaleY(CHAT_TITLE);
	const Int entryHeight = scaleY(CHAT_ENTRY);
	const Int logHeight = height - title - entryHeight - 2 * pad;

	WinInstanceData logInstance;
	logInstance.init();
	logInstance.m_style = GWS_SCROLL_LISTBOX;
	ListboxData logData;
	memset(&logData, 0, sizeof(logData));
	logData.listLength = CHAT_LINES;
	logData.columns = 1;
	logData.autoPurge = TRUE;
	logData.scrollIfAtEnd = TRUE;
	logData.scrollBar = TRUE;
	s_chatLog = TheWindowManager->gogoGadgetListBox(s_chat, WIN_STATUS_ENABLED, 0, title, width, logHeight, &logInstance, &logData, font, FALSE);
	if (s_chatLog != nullptr)
	{
		if (font != nullptr)
		{
			GadgetListBoxSetFont(s_chatLog, font);
		}
		GadgetListBoxSetEnabledColor(s_chatLog, WIN_COLOR_UNDEFINED);
		GadgetListBoxSetEnabledBorderColor(s_chatLog, WIN_COLOR_UNDEFINED);
		GadgetListBoxSetHiliteColor(s_chatLog, WIN_COLOR_UNDEFINED);
		GadgetListBoxSetHiliteBorderColor(s_chatLog, WIN_COLOR_UNDEFINED);
		GadgetListBoxSetDisabledColor(s_chatLog, WIN_COLOR_UNDEFINED);
		GadgetListBoxSetDisabledBorderColor(s_chatLog, WIN_COLOR_UNDEFINED);
		s_chatLog->winSetEnabledTextColors(white, black);
		s_chatLog->winSetHiliteTextColors(white, black);
		s_chatLog->winSetDisabledTextColors(white, black);
		fillLog(conversation);
	}

	const Color line = GOUIColor(GOUICOLOR_LINE, GameMakeColor(47, 75, 255, 255));
	const Color lineHilite = GOUIColor(GOUICOLOR_LINE_HILITE, GameMakeColor(47, 75, 255, 255));
	WinInstanceData entryInstance;
	entryInstance.init();
	entryInstance.m_style = GWS_ENTRY_FIELD;
	EntryData entryData;
	memset(&entryData, 0, sizeof(entryData));
	entryData.maxTextLen = 100;
	s_chatEntry = TheWindowManager->gogoGadgetTextEntry(s_chat, WIN_STATUS_ENABLED, pad, height - entryHeight - pad, width - 2 * pad, entryHeight, &entryInstance, &entryData, font, FALSE);
	if (s_chatEntry != nullptr)
	{
		if (font != nullptr)
		{
			GadgetTextEntrySetFont(s_chatEntry, font);
		}
		GadgetTextEntrySetEnabledColor(s_chatEntry, WIN_COLOR_UNDEFINED);
		GadgetTextEntrySetEnabledBorderColor(s_chatEntry, line);
		s_chatEntry->winSetHiliteColor(0, WIN_COLOR_UNDEFINED);
		s_chatEntry->winSetHiliteBorderColor(0, lineHilite);
		s_chatEntry->winSetEnabledTextColors(white, black);
		s_chatEntry->winSetHiliteTextColors(white, black);
		s_chatEntry->winSetDisabledTextColors(white, black);
	}
}

WindowMsgHandledType systemChat(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	if (msg != GEM_EDIT_DONE)
	{
		return MSG_IGNORED;
	}
	GameWindow *entry = s_chatEntry;
	const int64_t peer = s_chatPeer;
	if (entry == nullptr || peer < 0 || reinterpret_cast<GameWindow *>(mData1) != entry)
	{
		return MSG_HANDLED;
	}
	UnicodeString text = GadgetTextEntryGetText(entry);
	text.trim();
	if (text.isEmpty())
	{
		GadgetTextEntrySetText(entry, UnicodeString::TheEmptyString);
		return MSG_HANDLED;
	}
	// The line goes out through the send function of the communicator, and the sent line comes back as a message of the conversation.
	UnicodeString failure;
	if (GenOnlineSendConversationLine(peer, text, &failure))
	{
		GadgetTextEntrySetText(entry, UnicodeString::TheEmptyString);
	}
	else if (!failure.isEmpty())
	{
		Conversation *conversation = findConversation(peer);
		if (conversation != nullptr)
		{
			addConversationLine(*conversation, failure, GOUIColor(GOUICOLOR_ERROR, GameMakeColor(255, 0, 0, 255)), 0, FALSE);
		}
	}
	return MSG_HANDLED;
}

void drawChat(GameWindow *window, WinInstanceData *)
{
	Conversation *conversation = findConversation(s_chatPeer);
	if (conversation == nullptr || s_textRegular == nullptr || s_textSmall == nullptr)
	{
		return;
	}

	const Int width = scaleX(CHAT_WIDTH);
	const Int height = scaleY(CHAT_HEIGHT);
	Int right = s_communicatorLeft - scaleX(COMMUNICATOR_GAP);
	for (const TabRect &tab : s_tabRects)
	{
		if (tab.peer == s_chatPeer)
		{
			right = tab.right;
		}
	}
	const Int dockTop = s_sectionBottom - scaleY(SECTION_HEIGHT);
	const Int left = (std::max<Int>)(0, right - width);
	const Int top = (std::max<Int>)(0, dockTop - height);
	window->winSetPosition(left, top);
	window->winSetSize(width, height);

	const MouseIO *mouse = TheMouse->getMouseStatus();
	const Color white = GameMakeColor(255, 255, 255, 255);
	const Color muted = GOUIColor(GOUICOLOR_MUTED, GameMakeColor(154, 163, 199, 255));
	const Color line = GOUIColor(GOUICOLOR_LINE, GameMakeColor(47, 75, 255, 255));
	const Int pad = scaleX(CHAT_PAD);
	const Int titleHeight = scaleY(CHAT_TITLE);
	const Int regularHeight = textHeight(s_textRegular);
	const Int smallHeight = textHeight(s_textSmall);
	const Int dot = scaleY(7);

	TheWindowManager->winFillRect(GameMakeColor(5, 7, 15, 242), 1.0f, left, top, left + width, top + height);
	GadgetListBoxDrawKitFrame(line, left, top, width, height);
	TheWindowManager->winDrawLine(line, 1.0f, left, top + titleHeight, left + width, top + titleHeight);

	// The buttons of the title row sit right aligned: Profile, End and the cross that sinks the window.
	s_closeHit.left = left + width - titleHeight;
	s_closeHit.right = left + width;
	s_closeHit.top = top;
	s_closeHit.bottom = top + titleHeight;
	const UnicodeString cross(L"x");
	drawText(s_textRegular, cross, s_closeHit.left + (titleHeight - textWidth(s_textRegular, cross)) / 2, top + (titleHeight - regularHeight) / 2, s_closeHit.right, inside(s_closeHit, mouse->pos.x, mouse->pos.y) ? white : muted);
	const UnicodeString endLabel = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GODockChatEnd", L"End");
	const UnicodeString profileLabel = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GODockChatProfile", L"Profile");
	PanelHit *buttons[2] = { &s_endHit, &s_profileHit };
	const UnicodeString *labels[2] = { &endLabel, &profileLabel };
	Int buttonRight = s_closeHit.left;
	for (Int i = 0; i < 2; ++i)
	{
		buttons[i]->right = buttonRight;
		buttons[i]->left = buttonRight - textWidth(s_textSmall, *labels[i]) - 4 * pad;
		buttons[i]->top = top;
		buttons[i]->bottom = top + titleHeight;
		drawText(s_textSmall, *labels[i], buttons[i]->left + 2 * pad, top + (titleHeight - smallHeight) / 2, buttons[i]->right, inside(*buttons[i], mouse->pos.x, mouse->pos.y) ? white : muted);
		buttonRight = buttons[i]->left;
	}

	Int x = left + pad * 2;
	drawStatusDot(conversation->presence, x, top + (titleHeight - dot) / 2, dot);
	x += dot + scaleX(5);
	drawText(s_textRegular, conversation->name, x, top + (titleHeight - regularHeight) / 2, buttonRight - pad, white);
	const Int statusLeft = x + textWidth(s_textRegular, conversation->name) + scaleX(8);
	if (statusLeft < buttonRight - pad)
	{
		drawText(s_textSmall, conversation->status, statusLeft, top + (titleHeight - smallHeight) / 2, buttonRight - pad, muted);
	}

	if (s_chatEntry != nullptr)
	{
		const Int entryHeight = scaleY(CHAT_ENTRY);
		const Int entryLeft = left + pad;
		const Int entryTop = top + height - entryHeight - pad;
		TheWindowManager->winFillRect(GOUIColor(GOUICOLOR_FIELD_FILL, GameMakeColor(5, 7, 15, 255)), 1.0f, entryLeft, entryTop, entryLeft + width - 2 * pad, entryTop + entryHeight);
		if (GadgetTextEntryGetText(s_chatEntry).isEmpty())
		{
			const Int textLeft = entryLeft + GameWindowManager::winScaleLayoutSize(7);
			drawText(s_textSmall, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GODockChatHint", L"Press Enter to chat, / for commands"), textLeft, entryTop + (entryHeight - smallHeight) / 2, entryLeft + width - 2 * pad, muted);
		}
	}
}

void openChat(int64_t peer);

// Ends the conversation: the window sinks and the tab goes. The lines stay in the cache of the social interface.
void endConversation(int64_t peer)
{
	if (s_chatPeer == peer)
	{
		closeChat();
	}
	s_conversations.erase(std::remove_if(s_conversations.begin(), s_conversations.end(), [peer](const Conversation &conversation) { return conversation.peer == peer; }), s_conversations.end());
}

void openProfile(int64_t peer)
{
	const Conversation *conversation = findConversation(peer);
	if (conversation == nullptr)
	{
		return;
	}
	const UnicodeString name = conversation->name;
	closeChat();
	GenOnlineSetLookAtPlayer(peer, name);
	GameSpyOpenOverlay(GSOVERLAY_PLAYERINFO);
}

WindowMsgHandledType inputChat(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	const MouseIO *mouse = TheMouse->getMouseStatus();
	switch (msg)
	{
	case GWM_CHAR:
		if (mData1 == KEY_ESC)
		{
			if (BitIsSet(mData2, KEY_STATE_UP))
			{
				closeChat();
			}
			return MSG_HANDLED;
		}
		return MSG_IGNORED;
	case GWM_LEFT_DOWN:
	case GWM_RIGHT_DOWN:
		return MSG_HANDLED;
	case GWM_LEFT_UP:
		if (inside(s_closeHit, mouse->pos.x, mouse->pos.y))
		{
			closeChat();
		}
		else if (inside(s_endHit, mouse->pos.x, mouse->pos.y))
		{
			endConversation(s_chatPeer);
		}
		else if (inside(s_profileHit, mouse->pos.x, mouse->pos.y))
		{
			openProfile(s_chatPeer);
		}
		return MSG_HANDLED;
	}
	return MSG_IGNORED;
}

void openChat(int64_t peer)
{
	closeChat();
	closeOverflow();
	closeDropdown();
	Conversation *conversation = findConversation(peer);
	if (!chatDockReady() || conversation == nullptr)
	{
		return;
	}

	GameWindow *parent = s_window->winGetParent();
	const Int width = scaleX(CHAT_WIDTH);
	const Int height = scaleY(CHAT_HEIGHT);
	s_chat = TheWindowManager->winCreate(parent, WIN_STATUS_ENABLED | WIN_STATUS_NO_FOCUS, 0, 0, width, height, systemChat, nullptr);
	if (s_chat == nullptr)
	{
		return;
	}
	s_chatPeer = peer;
	conversation->unread = 0;
	s_chat->winSetInputFunc(inputChat);
	s_chat->winSetDrawFunc(drawChat);
	createChatGadgets(*conversation, width, height);
	if (s_chatEntry != nullptr)
	{
		TheWindowManager->winSetFocus(s_chatEntry);
	}
	// Viewing loads the history once and marks the conversation read, as the communicator does.
	GenOnlineSocialInterface *social = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (social != nullptr)
	{
		social->ViewConversation(peer);
	}
}

// Tabs that do not fit collapse into one "+N" tab, and the open conversation is always moved into a visible slot.
void layoutTabs()
{
	s_tabRects.clear();
	s_visibleTabs = 0;
	if (s_tabs == nullptr)
	{
		return;
	}
	const Int count = static_cast<Int>(s_conversations.size());
	if (count == 0 || s_displayName.empty())
	{
		s_tabs->winHide(TRUE);
		closeOverflow();
		return;
	}

	Int left, top, right, bottom;
	GenOnlineDockGetChatArea(&left, &top, &right, &bottom);
	const Int gap = scaleX(COMMUNICATOR_GAP);
	const Int tabWidth = scaleX(TAB_WIDTH);
	const Int overflowWidth = scaleX(OVERFLOW_WIDTH);
	Int visible = count;
	Int total = count * tabWidth + (count - 1) * gap;
	if (total > right - left)
	{
		visible = (std::max<Int>)(0, (right - left - overflowWidth) / (tabWidth + gap));
		total = visible * (tabWidth + gap) + overflowWidth;
	}
	s_visibleTabs = visible;
	if (visible > 0 && visible < count)
	{
		for (Int i = visible; i < count; ++i)
		{
			if (s_conversations[i].peer == s_chatPeer)
			{
				std::swap(s_conversations[i], s_conversations[visible - 1]);
			}
		}
	}

	Int x = right - total;
	for (Int i = 0; i < visible; ++i)
	{
		TabRect tab = { s_conversations[i].peer, x, top, x + tabWidth, bottom };
		s_tabRects.push_back(tab);
		x += tabWidth + gap;
	}
	if (visible < count)
	{
		TabRect tab = { OVERFLOW_TAB, x, top, x + overflowWidth, bottom };
		s_tabRects.push_back(tab);
	}
	s_tabs->winHide(FALSE);
	s_tabs->winSetPosition(s_tabRects.front().left, top);
	s_tabs->winSetSize(right - s_tabRects.front().left, bottom - top);
}

void drawBadge(Int right, Int tabTop, Int tabHeight, Int unread)
{
	UnicodeString count;
	count.format(unread > 99 ? L"99+" : L"%d", unread);
	const Int badgeHeight = textHeight(s_textSmall) + scaleY(2);
	const Int badgeWidth = (std::max)(badgeHeight, textWidth(s_textSmall, count) + 2 * scaleX(MARKER_PADDING));
	const Int badgeLeft = right - badgeWidth;
	const Int badgeTop = tabTop + (tabHeight - badgeHeight) / 2;
	const Color blue = GameMakeColor(47, 75, 255, 255);
	TheWindowManager->winFillRect(blue, 1.0f, badgeLeft + 1, badgeTop, badgeLeft + badgeWidth - 1, badgeTop + badgeHeight);
	TheWindowManager->winFillRect(blue, 1.0f, badgeLeft, badgeTop + 1, badgeLeft + badgeWidth, badgeTop + badgeHeight - 1);
	drawText(s_textSmall, count, badgeLeft + (badgeWidth - textWidth(s_textSmall, count)) / 2, badgeTop + (badgeHeight - textHeight(s_textSmall)) / 2, badgeLeft + badgeWidth, GameMakeColor(255, 255, 255, 255));
}

void drawTabs(GameWindow *window, WinInstanceData *)
{
	if (s_textSmall == nullptr)
	{
		return;
	}
	const MouseIO *mouse = TheMouse->getMouseStatus();
	const Color muted = GOUIColor(GOUICOLOR_MUTED, GameMakeColor(154, 163, 199, 255));
	const Color white = GameMakeColor(255, 255, 255, 255);
	const Int padding = scaleX(SECTION_PADDING);
	const Int badgeWidth = textHeight(s_textSmall) + scaleY(2) + scaleX(SECTION_GAP);
	const Int dot = scaleY(7);
	for (const TabRect &tab : s_tabRects)
	{
		const PanelHit rect = { tab.left, tab.top, tab.right, tab.bottom, ACTION_PROFILE, 0 };
		const Bool overflow = tab.peer == OVERFLOW_TAB;
		const Conversation *conversation = overflow ? nullptr : findConversation(tab.peer);
		const Bool open = !overflow && tab.peer == s_chatPeer;
		const Bool lit = open || (overflow && s_overflow != nullptr) || inside(rect, mouse->pos.x, mouse->pos.y);
		const Int width = tab.right - tab.left;
		const Int height = tab.bottom - tab.top;
		TheWindowManager->winFillRect(lit ? GameMakeColor(30, 40, 90, 235) : GameMakeColor(5, 7, 15, 209), 1.0f, tab.left, tab.top, tab.right, tab.bottom);
		GadgetListBoxDrawKitFrame(GOUIColor(lit ? GOUICOLOR_LINE_HILITE : GOUICOLOR_LINE, GameMakeColor(47, 75, 255, 255)), tab.left, tab.top, width, height);

		if (overflow)
		{
			Int hiddenUnread = 0;
			for (Int i = s_visibleTabs; i < static_cast<Int>(s_conversations.size()); ++i)
			{
				hiddenUnread += s_conversations[i].unread > 0 ? 1 : 0;
			}
			UnicodeString label;
			label.format(L"+%d", static_cast<Int>(s_conversations.size()) - s_visibleTabs);
			Int textRight = tab.right - padding;
			if (hiddenUnread > 0)
			{
				drawBadge(tab.right - padding / 2, tab.top, height, hiddenUnread);
				textRight -= badgeWidth;
			}
			drawText(s_textSmall, label, tab.left + padding, tab.top + (height - textHeight(s_textSmall)) / 2, textRight, lit ? white : muted);
			continue;
		}
		if (conversation == nullptr)
		{
			continue;
		}

		drawStatusDot(conversation->presence, tab.left + padding, tab.top + (height - dot) / 2, dot);
		Int textRight = tab.right - padding;
		if (conversation->unread > 0)
		{
			drawBadge(tab.right - padding, tab.top, height, conversation->unread);
			textRight -= badgeWidth;
		}
		drawText(s_textSmall, conversation->name, tab.left + padding + dot + scaleX(SECTION_GAP), tab.top + (height - textHeight(s_textSmall)) / 2, textRight, lit ? white : muted);
	}
}

void drawOverflow(GameWindow *window, WinInstanceData *)
{
	if (s_textSmall == nullptr)
	{
		return;
	}
	s_overflowHits.clear();
	Int tabRight = 0;
	for (const TabRect &tab : s_tabRects)
	{
		if (tab.peer == OVERFLOW_TAB)
		{
			tabRight = tab.right;
		}
	}
	const Int rows = (std::min<Int>)(OVERFLOW_MAX_ROWS, static_cast<Int>(s_conversations.size()) - s_visibleTabs);
	if (rows <= 0 || tabRight == 0)
	{
		return;
	}
	const MouseIO *mouse = TheMouse->getMouseStatus();
	const Color muted = GOUIColor(GOUICOLOR_MUTED, GameMakeColor(154, 163, 199, 255));
	const Int pad = scaleX(CHAT_PAD);
	const Int row = scaleY(OVERFLOW_ROW);
	const Int dot = scaleY(7);
	const Int width = scaleX(OVERFLOW_LIST_WIDTH);
	const Int height = rows * row + 2 * pad;
	const Int left = (std::max<Int>)(0, tabRight - width);
	const Int top = (std::max<Int>)(0, s_sectionBottom - scaleY(SECTION_HEIGHT) - scaleY(2) - height);
	window->winSetPosition(left, top);
	window->winSetSize(width, height);
	TheWindowManager->winFillRect(GameMakeColor(5, 7, 15, 242), 1.0f, left, top, left + width, top + height);
	GadgetListBoxDrawKitFrame(GOUIColor(GOUICOLOR_LINE, GameMakeColor(47, 75, 255, 255)), left, top, width, height);
	for (Int i = 0; i < rows; ++i)
	{
		const Conversation &conversation = s_conversations[s_visibleTabs + i];
		TabRect hit = { conversation.peer, left + 1, top + pad + i * row, left + width - 1, top + pad + (i + 1) * row };
		s_overflowHits.push_back(hit);
		const PanelHit rect = { hit.left, hit.top, hit.right, hit.bottom, ACTION_PROFILE, 0 };
		const Bool hot = inside(rect, mouse->pos.x, mouse->pos.y);
		if (hot)
		{
			TheWindowManager->winFillRect(GameMakeColor(30, 40, 90, 255), 1.0f, hit.left, hit.top, hit.right, hit.bottom);
		}
		drawStatusDot(conversation.presence, hit.left + pad * 2, hit.top + (row - dot) / 2, dot);
		Int textRight = hit.right - pad;
		if (conversation.unread > 0)
		{
			drawBadge(hit.right - pad, hit.top, row, conversation.unread);
			textRight -= textHeight(s_textSmall) + scaleY(2) + pad;
		}
		drawText(s_textSmall, conversation.name, hit.left + pad * 2 + dot + scaleX(5), hit.top + (row - textHeight(s_textSmall)) / 2, textRight, hot ? GameMakeColor(255, 255, 255, 255) : muted);
	}
}

WindowMsgHandledType inputOverflow(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
	case GWM_LEFT_DOWN:
		return MSG_HANDLED;
	case GWM_LEFT_UP:
	{
		const MouseIO *mouse = TheMouse->getMouseStatus();
		for (const TabRect &hit : s_overflowHits)
		{
			const PanelHit rect = { hit.left, hit.top, hit.right, hit.bottom, ACTION_PROFILE, 0 };
			if (inside(rect, mouse->pos.x, mouse->pos.y))
			{
				// The picked conversation takes a visible slot when its window opens, see layoutTabs.
				openChat(hit.peer);
				break;
			}
		}
		return MSG_HANDLED;
	}
	}
	return MSG_IGNORED;
}

WindowMsgHandledType inputOverflowCatcher(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
	case GWM_LEFT_DOWN:
	case GWM_RIGHT_DOWN:
		return MSG_HANDLED;
	case GWM_LEFT_UP:
	case GWM_RIGHT_UP:
		closeOverflow();
		return MSG_HANDLED;
	}
	return MSG_IGNORED;
}

void openOverflow()
{
	if (s_window == nullptr || s_overflow != nullptr)
	{
		return;
	}
	closeChat();
	closeDropdown();
	GameWindow *parent = s_window->winGetParent();
	s_overflowCatcher = TheWindowManager->winCreate(parent, WIN_STATUS_ENABLED | WIN_STATUS_NO_FOCUS, 0, 0, TheDisplay->getWidth(), TheDisplay->getHeight(), nullptr, nullptr);
	s_overflow = TheWindowManager->winCreate(parent, WIN_STATUS_ENABLED | WIN_STATUS_NO_FOCUS, 0, 0, scaleX(OVERFLOW_LIST_WIDTH), scaleY(OVERFLOW_ROW), nullptr, nullptr);
	if (s_overflowCatcher != nullptr)
	{
		s_overflowCatcher->winSetInputFunc(inputOverflowCatcher);
	}
	if (s_overflow != nullptr)
	{
		s_overflow->winSetInputFunc(inputOverflow);
		s_overflow->winSetDrawFunc(drawOverflow);
	}
}

WindowMsgHandledType inputTabs(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
	case GWM_LEFT_DOWN:
		return MSG_HANDLED;
	case GWM_LEFT_UP:
	{
		const MouseIO *mouse = TheMouse->getMouseStatus();
		for (const TabRect &tab : s_tabRects)
		{
			const PanelHit rect = { tab.left, tab.top, tab.right, tab.bottom, ACTION_PROFILE, 0 };
			if (inside(rect, mouse->pos.x, mouse->pos.y))
			{
				const int64_t peer = tab.peer;
				closeDropdown();
				if (peer == OVERFLOW_TAB)
				{
					openOverflow();
				}
				else if (peer == s_chatPeer)
				{
					closeChat();
				}
				else
				{
					openChat(peer);
				}
				break;
			}
		}
		return MSG_HANDLED;
	}
	}
	return MSG_IGNORED;
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

	s_pillRight = s_sectionRight;
	window->winSetSize(width, height);
	window->winSetPosition(s_pillRight - width, s_sectionBottom - height);
	s_sectionLeft = s_pillRight - width;
	s_communicatorLeft = s_sectionLeft - scaleX(COMMUNICATOR_GAP) - height;
	if (s_communicator != nullptr)
	{
		s_communicator->winSetPosition(s_communicatorLeft, s_sectionBottom - height);
	}
	layoutTabs();

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
			closeChat();
			closeOverflow();
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

void GenOnlineDockGetChatArea(Int *left, Int *top, Int *right, Int *bottom)
{
	*right = s_communicatorLeft - scaleX(COMMUNICATOR_GAP);
	*left = *right - scaleX(CHAT_RESERVE_WIDTH);
	*bottom = s_sectionBottom;
	*top = s_sectionBottom - scaleY(SECTION_HEIGHT);
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
	s_tabs = nullptr;
	s_chat = nullptr;
	s_chatLog = nullptr;
	s_chatEntry = nullptr;
	s_chatPeer = -1;
	s_overflow = nullptr;
	s_overflowCatcher = nullptr;
	s_hits.clear();
	s_overflowHits.clear();
	s_tabRects.clear();
	freeString(s_name);
	freeString(s_marker);
	freeString(s_textRegular);
	freeString(s_textSmall);

	s_sectionRight = scaleX(SECTION_RIGHT);
	s_sectionBottom = scaleY(SECTION_BOTTOM);
	s_pillRight = s_sectionRight;
	s_communicatorLeft = 0;
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

	s_tabs = TheWindowManager->winCreate(parent, WIN_STATUS_ENABLED | WIN_STATUS_NO_FOCUS | WIN_STATUS_HIDDEN, 0, 0, height, height, nullptr, nullptr);
	if (s_tabs != nullptr)
	{
		s_tabs->winSetInputFunc(inputTabs);
		s_tabs->winSetDrawFunc(drawTabs);
	}

	// The callbacks only touch the conversation state, so they stay valid when the screen and its windows go.
	GenOnlineSocialInterface *social = GenOnlineServicesManager::GetInterface<GenOnlineSocialInterface>();
	if (social != nullptr)
	{
		social->RegisterForDockCallbacks(onDockMessage, onDockReloaded, onDockLineRemoved);
	}
}

void GenOnlineUserSectionHide()
{
	closeDropdown();
	closeChat();
	closeOverflow();
	if (s_tabs != nullptr)
	{
		TheWindowManager->winDestroy(s_tabs);
		s_tabs = nullptr;
	}
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

void GenOnlineDockOpenConversation(int64_t peerUserID, bool showWindow)
{
	if (peerUserID < 0 || s_displayName.empty())
	{
		return;
	}
	Bool created;
	ensureConversation(peerUserID, created);
	if (showWindow)
	{
		openChat(peerUserID);
	}
}
