#include "PreRTS.h"

#include "GameClient/GenOnlineLobbyGameDetails.h"

#include "Common/GOUIColors.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameText.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameNetwork/GenOnline/GenOnlineAnticheatKind.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineLobbyInterface.h"

namespace
{

GameWindow *s_panel = nullptr;
GameWindow *s_players = nullptr;
GameWindow *s_name = nullptr;
GameWindow *s_hostLine = nullptr;
GameWindow *s_map = nullptr;
GameWindow *s_mapName = nullptr;
GameWindow *s_shield = nullptr;
GameWindow *s_valuePlayers = nullptr;
GameWindow *s_valueAnticheat = nullptr;
GameWindow *s_valueCash = nullptr;
GameWindow *s_valueSuperweapons = nullptr;
GameWindow *s_valueStats = nullptr;
GameWindow *s_valueObservers = nullptr;
Real s_mapAspect = 1.0f;
GameWindow *s_headers = nullptr;
GameWindow *s_chat = nullptr;
DisplayString *s_headerString = nullptr;

// The windows under the map preview move up by the preview height when a game has no preview.
constexpr Int MAX_SHIFTED = 16;
GameWindow *s_shifted[MAX_SHIFTED];
Int s_shiftedX[MAX_SHIFTED];
Int s_shiftedY[MAX_SHIFTED];
Int s_shiftedCount = 0;
Int s_collapse = 0;

// 800x600 layout units, the header rows are the 12 units over the players list and the chat list.
constexpr Int HEADER_HEIGHT = 12;

GameWindow *findWindow(GameWindow *parent, const char *name)
{
	return TheWindowManager->winGetWindowFromId(parent, TheNameKeyGenerator->nameToKey(name));
}

void setText(GameWindow *window, const UnicodeString &text)
{
	if (window != nullptr)
	{
		GadgetStaticTextSetText(window, text);
	}
}

void setLabel(GameWindow *parent, const char *name, const WideChar *text)
{
	setText(findWindow(parent, name), UnicodeString(text));
}

// Single line text is cut with dots so it never wraps into the row below.
void setFittedText(GameWindow *window, UnicodeString text)
{
	if (window == nullptr)
	{
		return;
	}

	Int width, height;
	window->winGetSize(&width, &height);
	const Int limit = width - GameWindowManager::winScaleLayoutSize(10);
	Int textWidth, textHeight;
	TheWindowManager->winGetTextSize(window->winGetFont(), text, &textWidth, &textHeight, 0);
	if (textWidth > limit)
	{
		while (text.getLength() > 1 && textWidth > limit)
		{
			text.removeLastChar();
			UnicodeString cut = text;
			cut.concat(L"...");
			TheWindowManager->winGetTextSize(window->winGetFont(), cut, &textWidth, &textHeight, 0);
		}
		text.concat(L"...");
	}
	GadgetStaticTextSetText(window, text);
}

void drawMapPreview(GameWindow *window, WinInstanceData *)
{
	const Image *image = window->winGetEnabledImage(0);
	if (image == nullptr)
	{
		return;
	}

	Int x, y, width, height;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&width, &height);
	TheWindowManager->winFillRect(GameMakeColor(0, 0, 0, 255), 1.0f, x, y, x + width, y + height);

	Int drawWidth = width;
	Int drawHeight = height;
	if (s_mapAspect > (Real)width / (Real)height)
	{
		drawHeight = (Int)((Real)width / s_mapAspect);
	}
	else
	{
		drawWidth = (Int)((Real)height * s_mapAspect);
	}
	const Int left = x + (width - drawWidth) / 2;
	const Int top = y + (height - drawHeight) / 2;
	TheWindowManager->winDrawImage(image, left, top, left + drawWidth, top + drawHeight);
}

DisplayString *newHeaderString()
{
	DisplayString *string = TheDisplayStringManager->newDisplayString();
	GameFont *font = TheHeaderTemplateManager->getFontFromTemplate("GOLabelSmall");
	if (font != nullptr)
	{
		string->setFont(font);
	}
	string->setWordWrap(0);
	return string;
}

void drawHeader(Int x, Int y, Int height, const UnicodeString &text)
{
	Int textWidth, textHeight;
	s_headerString->setText(text);
	s_headerString->getSize(&textWidth, &textHeight);
	s_headerString->draw(x, y + (height - textHeight) / 2, GOUIColor(GOUICOLOR_MUTED, GameMakeColor(154, 163, 199, 255)), GameMakeColor(0, 0, 0, 255));
}

void drawHeaders(GameWindow *window, WinInstanceData *)
{
	if (s_headerString == nullptr || s_panel == nullptr || s_chat == nullptr)
	{
		return;
	}

	Int x, y;
	window->winGetScreenPosition(&x, &y);
	const Int height = GameWindowManager::winScaleLayoutSize(HEADER_HEIGHT);
	const Int pad = GameWindowManager::winScaleLayoutSize(8);
	UnicodeString text;
	if (!s_panel->winIsHidden())
	{
		text = TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOHeaderSelectedGame", L"SELECTED GAME");
	}
	else
	{
		text.format(TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOHeaderInRoom", L"IN ROOM (%d)").str(), s_players != nullptr ? GadgetListBoxGetNumEntries(s_players) : 0);
	}
	drawHeader(x + pad, y, height, text);
	Int chatX, chatY;
	s_chat->winGetScreenPosition(&chatX, &chatY);
	drawHeader(x + pad, chatY - height - GameWindowManager::winScaleLayoutSize(2), height, TheGameText->FETCH_OR_SUBSTITUTE("GUI:GOHeaderChat", L"CHAT"));
}

Bool findSelectedLobby(LobbyEntry &lobby)
{
	GameWindow *list = GetGameListBox();
	GenOnlineLobbyInterface *lobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (list == nullptr || lobbyInterface == nullptr)
	{
		return FALSE;
	}

	Int selected = -1;
	GadgetListBoxGetSelected(list, &selected);
	if (selected < 0)
	{
		return FALSE;
	}

	const int64_t lobbyID = ResolveGameListLobbyID((Int)(intptr_t)GadgetListBoxGetItemData(list, selected));
	if (lobbyID < 0)
	{
		return FALSE;
	}

	lobby = lobbyInterface->GetLobbyFromID(lobbyID);
	return lobby.lobbyID != -1;
}

UnicodeString hostLine(const LobbyEntry &lobby)
{
	std::wstring owner;
	for (const LobbyMemberEntry &member : lobby.members)
	{
		if (member.user_id == lobby.owner)
		{
			owner = from_utf8(member.display_name);
		}
	}

	UnicodeString line;
	line.format(L"Host %s", owner.c_str());
	UnicodeString ping;
	ping.format(L" \u00B7 %d ms", lobby.latency);
	line.concat(ping);
	return line;
}

// The list carries the map file name or path of the host, the map cache is keyed by the lowercased map path.
std::string mapCachePath(const LobbyEntry &lobby)
{
	std::string path = lobby.map_path;
	if (path.find_first_of("\/") == std::string::npos)
	{
		const size_t dot = path.find_last_of('.');
		path = path.substr(0, dot) + "\\" + path;
	}
	if (lobby.map_official)
	{
		return "maps\\" + path;
	}

	AsciiString userDir = TheMapCache->getUserMapDir(true);
	userDir.toLower();
	return std::string(userDir.str()) + "\\" + path;
}

// Official maps win over a user map with the same display name.
const MapMetaData *findMapForLobby(const LobbyEntry &lobby)
{
	if (!lobby.map_path.empty())
	{
		const MapMetaData *md = TheMapCache->findMap(AsciiString(mapCachePath(lobby).c_str()));
		if (md != nullptr)
		{
			return md;
		}
	}

	const std::wstring wanted = from_utf8(lobby.map_name);
	for (Int pass = 0; pass < 2; ++pass)
	{
		for (const auto &entry : *TheMapCache)
		{
			if ((entry.second.m_isOfficial != FALSE) == (pass == 0) && wcsicmp(entry.second.m_displayName.str(), wanted.c_str()) == 0)
			{
				return &entry.second;
			}
		}
	}
	return nullptr;
}

UnicodeString mapDisplayName(const LobbyEntry &lobby)
{
	const MapMetaData *md = findMapForLobby(lobby);
	if (md != nullptr)
	{
		return md->m_displayName;
	}

	const char *start = lobby.map_name.c_str();
	const char *slash = strrchr(start, '\\');
	if (slash != nullptr)
	{
		start = slash + 1;
	}
	UnicodeString name;
	name.format(L"%s", from_utf8(start).c_str());
	return name;
}

void setCollapse(Bool collapsed)
{
	const Int shift = collapsed ? s_collapse : 0;
	for (Int i = 0; i < s_shiftedCount; ++i)
	{
		s_shifted[i]->winSetPosition(s_shiftedX[i], s_shiftedY[i] - shift);
	}
}

void showMap(const LobbyEntry &lobby)
{
	if (s_map == nullptr)
	{
		return;
	}

	const MapMetaData *md = findMapForLobby(lobby);
	const Image *preview = md != nullptr ? getMapPreviewImage(md->m_fileName) : nullptr;
	if (preview == nullptr)
	{
		s_map->winHide(TRUE);
		setCollapse(TRUE);
		return;
	}
	setCollapse(FALSE);

	const Real width = md->m_extent.width();
	const Real height = md->m_extent.height();
	s_mapAspect = (width > 0.0f && height > 0.0f) ? width / height : 1.0f;
	s_map->winSetStatus(WIN_STATUS_IMAGE);
	s_map->winSetEnabledImage(0, preview);
	s_map->winHide(FALSE);
}

void showAnticheat(const LobbyEntry &lobby)
{
	const EAnticheatKind kind = ParseAnticheatKind(lobby.anticheat);
	const EAnticheatKind local = LocalAnticheatKind();
	const Bool eac = kind == EAnticheatKind::Eac;
	UnicodeString text = eac ? AnticheatKindText(kind) : UnicodeString(L"Not required");
	if (eac)
	{
		text.concat(L" required");
	}
	setText(s_valueAnticheat, text);

	if (s_shield != nullptr)
	{
		const Image *shield = eac ? TheMappedImageCollection->findImageByName(AnticheatKindImageName(kind, local != EAnticheatKind::Eac)) : nullptr;
		s_shield->winSetStatus(WIN_STATUS_IMAGE);
		s_shield->winSetEnabledImage(0, shield);
		s_shield->winHide(shield == nullptr);
	}
}

void fillPanel(const LobbyEntry &lobby)
{
	setFittedText(s_name, UnicodeString(from_utf8(lobby.name).c_str()));
	setFittedText(s_hostLine, hostLine(lobby));
	setFittedText(s_mapName, mapDisplayName(lobby));
	showMap(lobby);
	showAnticheat(lobby);

	UnicodeString text;
	text.format(L"%d / %d", lobby.current_players, lobby.max_players);
	setText(s_valuePlayers, text);
	text.format(L"$%u", (unsigned)lobby.starting_cash);
	setText(s_valueCash, text);
	setText(s_valueSuperweapons, UnicodeString(lobby.limit_superweapons ? L"Limited" : L"Unlimited"));
	setText(s_valueStats, UnicodeString(lobby.track_stats ? L"Recorded" : L"Not recorded"));
	setText(s_valueObservers, UnicodeString(lobby.allow_observers ? L"Allowed" : L"Not allowed"));
}

} // namespace

void GenOnlineLobbyGameDetailsInit(GameWindow *parent)
{
	s_panel = findWindow(parent, "WOLCustomLobby.wnd:ParentGameDetails");
	s_players = findWindow(parent, "WOLCustomLobby.wnd:ListboxPlayers");
	s_name = findWindow(parent, "WOLCustomLobby.wnd:StaticTextDetailsName");
	s_hostLine = findWindow(parent, "WOLCustomLobby.wnd:StaticTextDetailsHost");
	s_map = findWindow(parent, "WOLCustomLobby.wnd:WindowDetailsMap");
	s_mapName = findWindow(parent, "WOLCustomLobby.wnd:StaticTextDetailsMapName");
	s_shield = findWindow(parent, "WOLCustomLobby.wnd:WindowDetailsShield");
	s_valuePlayers = findWindow(parent, "WOLCustomLobby.wnd:StaticTextDetailsValuePlayers");
	s_valueAnticheat = findWindow(parent, "WOLCustomLobby.wnd:StaticTextDetailsValueAnticheat");
	s_valueCash = findWindow(parent, "WOLCustomLobby.wnd:StaticTextDetailsValueCash");
	s_valueSuperweapons = findWindow(parent, "WOLCustomLobby.wnd:StaticTextDetailsValueSuperweapons");
	s_valueStats = findWindow(parent, "WOLCustomLobby.wnd:StaticTextDetailsValueStats");
	s_valueObservers = findWindow(parent, "WOLCustomLobby.wnd:StaticTextDetailsValueObservers");

	setLabel(parent, "WOLCustomLobby.wnd:StaticTextDetailsLabelPlayers", L"Players");
	setLabel(parent, "WOLCustomLobby.wnd:StaticTextDetailsLabelAnticheat", L"Anti-cheat");
	setLabel(parent, "WOLCustomLobby.wnd:StaticTextDetailsLabelCash", L"Starting cash");
	setLabel(parent, "WOLCustomLobby.wnd:StaticTextDetailsLabelSuperweapons", L"Superweapons");
	setLabel(parent, "WOLCustomLobby.wnd:StaticTextDetailsLabelStats", L"Stats");
	setLabel(parent, "WOLCustomLobby.wnd:StaticTextDetailsLabelObservers", L"Observers");

	if (s_map != nullptr)
	{
		s_map->winSetDrawFunc(drawMapPreview);
	}

	// The windows from the map name down follow the preview, so they move up together when it is missing.
	s_shiftedCount = 0;
	s_collapse = 0;
	if (s_map != nullptr && s_mapName != nullptr)
	{
		Int mapX, mapY, nameX, nameY;
		s_map->winGetPosition(&mapX, &mapY);
		s_mapName->winGetPosition(&nameX, &nameY);
		s_collapse = nameY - mapY;
		for (GameWindow *child = s_panel != nullptr ? s_panel->winGetChild() : nullptr; child != nullptr && s_shiftedCount < MAX_SHIFTED; child = child->winGetNext())
		{
			Int childX, childY;
			child->winGetPosition(&childX, &childY);
			if (childY >= nameY)
			{
				s_shifted[s_shiftedCount] = child;
				s_shiftedX[s_shiftedCount] = childX;
				s_shiftedY[s_shiftedCount] = childY;
				++s_shiftedCount;
			}
		}
	}

	// The header strip sits over the sidebar from the top of the details panel down to the chat, it only draws text.
	// It is not enabled because the hit test returns the newest enabled child under the mouse and drops the click when that child takes no input.
	s_chat = findWindow(parent, "WOLCustomLobby.wnd:ListboxChat");
	if (parent != nullptr && s_panel != nullptr && s_chat != nullptr && s_headerString == nullptr)
	{
		Int x, y, width, height, chatX, chatY, chatWidth, chatHeight;
		s_panel->winGetScreenPosition(&x, &y);
		s_panel->winGetSize(&width, &height);
		s_chat->winGetScreenPosition(&chatX, &chatY);
		s_chat->winGetSize(&chatWidth, &chatHeight);
		s_headers = TheWindowManager->winCreate(parent, WIN_STATUS_NO_INPUT, x, y, width, chatY + chatHeight - y, nullptr, nullptr);
		if (s_headers != nullptr)
		{
			s_headerString = newHeaderString();
			s_headers->winSetDrawFunc(drawHeaders);
		}
	}

	GenOnlineLobbyGameDetailsUpdate();
}

void GenOnlineLobbyGameDetailsShutdown()
{
	if (s_headers != nullptr)
	{
		TheWindowManager->winDestroy(s_headers);
		s_headers = nullptr;
	}
	if (s_headerString != nullptr)
	{
		TheDisplayStringManager->freeDisplayString(s_headerString);
		s_headerString = nullptr;
	}
	s_panel = nullptr;
	s_chat = nullptr;
	s_shiftedCount = 0;
	s_players = nullptr;
	s_name = nullptr;
	s_hostLine = nullptr;
	s_map = nullptr;
	s_mapName = nullptr;
	s_shield = nullptr;
	s_valuePlayers = nullptr;
	s_valueAnticheat = nullptr;
	s_valueCash = nullptr;
	s_valueSuperweapons = nullptr;
	s_valueStats = nullptr;
	s_valueObservers = nullptr;
}

void GenOnlineLobbyGameDetailsUpdate()
{
	if (s_panel == nullptr)
	{
		return;
	}

	LobbyEntry lobby;
	const Bool hasLobby = findSelectedLobby(lobby);
	if (hasLobby)
	{
		fillPanel(lobby);
	}
	s_panel->winHide(!hasLobby);
	if (s_players != nullptr)
	{
		s_players->winHide(hasLobby);
	}
}
