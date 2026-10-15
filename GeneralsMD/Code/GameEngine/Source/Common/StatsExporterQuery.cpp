/*
**	Command & Conquer Generals Zero Hour(tm)
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

// Protocol reference: docs/stats-exporter.md
#include "Common/StatsExporterQuery.h"
#include "Common/StatsExporterPipe.h"

#include "Common/KindOf.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Money.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/Radar.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Image.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"	// to_utf8

#include <algorithm>
#include <map>
#include <stdio.h>
#include <string>
#include <string.h>
#include <vector>

#include "StatsExporter.pb.h"

namespace
{
	// Requests answered per tick; the rest wait, so a burst of queries never stalls a frame.
	const Int kMaxRequestsPerPump = 4;

	const UnsignedInt kCatalogDefaultPage = 100;
	const UnsignedInt kObjectsDefaultPage = 100;
	const size_t kMaxContained = 64;
	const size_t kMaxObjectUpgrades = 64;
	const size_t kMaxPlayerUpgrades = 64;
	const size_t kMaxBuildable = 400;
	const size_t kMaxPowers = 64;
	const size_t kMaxWaypoints = 512;

	// Response field numbers (StatsExporter.proto).
	const UnsignedInt kResultCatalog = 10;
	const UnsignedInt kResultObject = 11;
	const UnsignedInt kResultObjects = 12;
	const UnsignedInt kResultPlayer = 13;
	const UnsignedInt kResultMap = 14;
	const UnsignedInt kResultResync = 15;
	const UnsignedInt kResultFeedConfig = 16;

	typedef statsexporter::ResponseStatus Status;

	std::string toUtf8(const UnicodeString& text)
	{
		return to_utf8(std::wstring(text.str()));
	}

	template <typename T>
	T& arenaResult(T*& slot)
	{
		if (slot == NULL)
		{
			slot = google::protobuf::Arena::Create<T>(&StatsExporterArena());
		}
		slot->Clear();
		return *slot;
	}

	UnsignedInt pageSizeOf(const statsexporter::PageRequest& page, UnsignedInt defaultSize)
	{
		UnsignedInt size = page.page_size();
		if (size == 0)
		{
			size = defaultSize;
		}
		return (size > STATS_EXPORTER_MAX_PAGE_SIZE) ? STATS_EXPORTER_MAX_PAGE_SIZE : size;
	}

	Bool matchesIgnoreCase(const AsciiString& a, const std::string& b)
	{
		return _stricmp(a.str(), b.c_str()) == 0;
	}

	// The KindOf with this name, -1 if there is none.
	Int kindOfFromName(const std::string& name)
	{
		for (Int i = 0; i < KINDOF_COUNT; ++i)
		{
			const char* kindName = KindOfMaskType::getNameFromSingleBit(i);
			if (kindName != NULL && _stricmp(kindName, name.c_str()) == 0)
			{
				return i;
			}
		}
		return -1;
	}

	// The KindOf flags that matter to tooling; the rest is engine plumbing.
	const KindOfType kReportedKinds[] =
	{
		KINDOF_STRUCTURE, KINDOF_INFANTRY, KINDOF_VEHICLE, KINDOF_AIRCRAFT, KINDOF_HERO, KINDOF_DOZER, KINDOF_HARVESTER,
		KINDOF_SALVAGER, KINDOF_TRANSPORT, KINDOF_COMMANDCENTER, KINDOF_SUPPLY_SOURCE, KINDOF_TECH_BUILDING, KINDOF_CAN_ATTACK,
		KINDOF_IMMOBILE, KINDOF_PROJECTILE, KINDOF_MINE, KINDOF_FS_POWER, KINDOF_FS_BASE_DEFENSE, KINDOF_FS_SUPERWEAPON,
		KINDOF_FS_SUPPLY_CENTER, KINDOF_FS_STRATEGY_CENTER, KINDOF_FS_BARRACKS, KINDOF_FS_AIRFIELD, KINDOF_FS_FAKE
	};

	Int ownerIndex(const Object* obj)
	{
		const Player* owner = obj->getControllingPlayer();
		if (owner == NULL || ThePlayerList == NULL || owner == ThePlayerList->getNeutralPlayer())
		{
			return -1;
		}
		return owner->getPlayerIndex();
	}

	// The final override is what live objects use.
	const ThingTemplate* finalTemplate(const ThingTemplate* tmpl)
	{
		return static_cast<const ThingTemplate*>(tmpl->friend_getFinalOverride());
	}

	Bool gameIsRunning()
	{
		return TheGameLogic != NULL && ThePlayerList != NULL && TheGameLogic->isInInteractiveGame();
	}

	// -- Catalog -------------------------------------------------------------------------------------------------------

	// Build-button icons by template name, from the control bar's command buttons.
	typedef std::map<std::string, const Image*> IconMap;

	const IconMap& buttonIcons()
	{
		static IconMap s_icons;
		static Bool s_built = FALSE;

		if (!s_built && TheControlBar != NULL)
		{
			for (const CommandButton* button = TheControlBar->getCommandButtons(); button != NULL; button = button->getNext())
			{
				if (button->getCommandType() != GUI_COMMAND_UNIT_BUILD && button->getCommandType() != GUI_COMMAND_DOZER_CONSTRUCT)
				{
					continue;
				}
				const ThingTemplate* tmpl = button->getThingTemplate();
				if (tmpl != NULL && button->getButtonImage() != NULL)
				{
					s_icons[tmpl->getName().str()] = button->getButtonImage();
				}
			}
			s_built = TRUE;
		}
		return s_icons;
	}

	void fillIcon(statsexporter::ButtonIcon* icon, const Image* image)
	{
		icon->set_image_name(image->getName().str());
		icon->set_texture_file(image->getFilename().str());
		const Region2D* uv = image->getUV();
		icon->set_u0(uv->lo.x);
		icon->set_v0(uv->lo.y);
		icon->set_u1(uv->hi.x);
		icon->set_v1(uv->hi.y);
		icon->set_width((uint32_t)image->getImageWidth());
		icon->set_height((uint32_t)image->getImageHeight());
		icon->set_texture_width((uint32_t)image->getTextureSize()->x);
		icon->set_texture_height((uint32_t)image->getTextureSize()->y);
	}

	Status answerCatalog(const statsexporter::CatalogQuery& query, std::string& result, std::string& message)
	{
		static statsexporter::CatalogResult* s_result = NULL;

		if (TheThingFactory == NULL)
		{
			message = "no template database";
			return statsexporter::RESPONSE_STATUS_UNAVAILABLE;
		}

		Int kindOf = -1;
		if (!query.kind_of().empty())
		{
			kindOf = kindOfFromName(query.kind_of());
			if (kindOf < 0)
			{
				message = "unknown kind_of";
				return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
			}
		}

		const UnsignedInt cursor = query.page().cursor();
		const UnsignedInt pageSize = pageSizeOf(query.page(), kCatalogDefaultPage);
		const IconMap& icons = buttonIcons();
		statsexporter::CatalogResult& out = arenaResult(s_result);
		UnsignedInt matched = 0;
		UnsignedInt emitted = 0;

		for (const ThingTemplate* head = TheThingFactory->firstTemplate(); head != NULL; head = head->friend_getNextTemplate())
		{
			const ThingTemplate* tmpl = finalTemplate(head);
			if (!query.name().empty() && !matchesIgnoreCase(tmpl->getName(), query.name()))
			{
				continue;
			}
			if (!query.side().empty() && !matchesIgnoreCase(tmpl->getDefaultOwningSide(), query.side()))
			{
				continue;
			}
			if (kindOf >= 0 && !tmpl->isKindOf((KindOfType)kindOf))
			{
				continue;
			}
			if (query.buildable_only() && !tmpl->isBuildableItem())
			{
				continue;
			}

			if (matched >= cursor && emitted < pageSize)
			{
				statsexporter::CatalogEntry* entry = out.add_entries();
				entry->set_template_id(tmpl->getTemplateID());
				entry->set_name(tmpl->getName().str());
				entry->set_display_name(toUtf8(tmpl->getDisplayName()));
				entry->set_side(tmpl->getDefaultOwningSide().str());
				entry->set_cost((uint32_t)tmpl->friend_getBuildCost());
				entry->set_build_time_seconds(tmpl->friend_getBuildTime());
				entry->set_buildable(tmpl->isBuildableItem());
				entry->set_energy(tmpl->getEnergyProduction());
				for (size_t k = 0; k < sizeof(kReportedKinds) / sizeof(kReportedKinds[0]); ++k)
				{
					if (tmpl->isKindOf(kReportedKinds[k]))
					{
						entry->add_kind_of(KindOfMaskType::getNameFromSingleBit(kReportedKinds[k]));
					}
				}
				const IconMap::const_iterator icon = icons.find(tmpl->getName().str());
				if (icon != icons.end())
				{
					fillIcon(entry->mutable_icon(), icon->second);
				}
				++emitted;
			}
			++matched;
		}

		if (!query.name().empty() && matched == 0)
		{
			message = "no such template";
			return statsexporter::RESPONSE_STATUS_NOT_FOUND;
		}

		statsexporter::PageInfo* page = out.mutable_page();
		page->set_total(matched);
		page->set_has_more(cursor + emitted < matched);
		page->set_next_cursor(cursor + emitted);
		out.SerializeToString(&result);
		return statsexporter::RESPONSE_STATUS_OK;
	}

	// -- Objects -------------------------------------------------------------------------------------------------------

	Status answerObject(const statsexporter::ObjectQuery& query, std::string& result, std::string& message)
	{
		static statsexporter::ObjectDetail* s_result = NULL;

		if (!gameIsRunning())
		{
			message = "no match is running";
			return statsexporter::RESPONSE_STATUS_UNAVAILABLE;
		}

		Object* obj = TheGameLogic->findObjectByID((ObjectID)query.object_id());
		if (obj == NULL || obj->getTemplate() == NULL)
		{
			message = "no such object";
			return statsexporter::RESPONSE_STATUS_NOT_FOUND;
		}

		statsexporter::ObjectDetail& out = arenaResult(s_result);
		const ThingTemplate* tmpl = obj->getTemplate();
		const Coord3D* position = obj->getPosition();

		out.set_object_id(obj->getID());
		out.set_template_name(tmpl->getName().str());
		out.set_display_name(toUtf8(tmpl->getDisplayName()));
		out.set_owner_player_index(ownerIndex(obj));
		out.set_x(position->x);
		out.set_y(position->y);
		out.set_z(position->z);
		out.set_angle(obj->getOrientation());
		const BodyModuleInterface* body = obj->getBodyModule();
		if (body != NULL)
		{
			out.set_health(body->getHealth());
			out.set_max_health(body->getMaxHealth());
		}
		out.set_veterancy((int32_t)obj->getVeterancyLevel());

		const ObjectStatusMaskType status = obj->getStatusBits();
		for (Int i = 0; i < OBJECT_STATUS_COUNT; ++i)
		{
			const char* statusName = ObjectStatusMaskType::getNameFromSingleBit(i);
			if (statusName != NULL && status.test(i))
			{
				out.add_status(statusName);
			}
		}
		out.set_under_construction(obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION));
		out.set_construction_percent(obj->getConstructionPercent());
		out.set_is_effectively_dead(obj->isEffectivelyDead());

		ContainModuleInterface* contain = obj->getContain();
		const ContainedItemsList* contained = (contain != NULL) ? contain->getContainedItemsList() : NULL;
		if (contained != NULL)
		{
			for (ContainedItemsList::const_iterator it = contained->begin(); it != contained->end() && (size_t)out.contained_ids_size() < kMaxContained; ++it)
			{
				out.add_contained_ids((*it)->getID());
			}
		}
		const Object* containedBy = obj->getContainedBy();
		out.set_contained_by_id((containedBy != NULL) ? containedBy->getID() : 0);
		out.set_producer_id(obj->getProducerID());

		if (TheUpgradeCenter != NULL)
		{
			for (UpgradeTemplate* upgrade = TheUpgradeCenter->firstUpgradeTemplate(); upgrade != NULL && (size_t)out.upgrades_size() < kMaxObjectUpgrades; upgrade = upgrade->friend_getNext())
			{
				if (upgrade->getUpgradeType() == UPGRADE_TYPE_OBJECT && obj->hasUpgrade(upgrade))
				{
					out.add_upgrades(upgrade->getUpgradeName().str());
				}
			}
		}

		const Weapon* weapon = obj->getCurrentWeapon();
		if (weapon != NULL)
		{
			out.set_current_weapon(weapon->getName().str());
		}

		AIUpdateInterface* ai = obj->getAIUpdateInterface();
		if (ai != NULL)
		{
			const Object* victim = ai->getCurrentVictim();
			if (victim != NULL)
			{
				out.set_target_object_id(victim->getID());
			}
			else
			{
				const Coord3D* targetPosition = ai->getCurrentVictimPos();
				if (targetPosition != NULL)
				{
					out.set_has_target_position(true);
					out.set_target_x(targetPosition->x);
					out.set_target_y(targetPosition->y);
				}
			}
		}

		out.SerializeToString(&result);
		return statsexporter::RESPONSE_STATUS_OK;
	}

	bool objectIdLess(const Object* a, const Object* b)
	{
		return a->getID() < b->getID();
	}

	Status answerObjects(const statsexporter::ObjectsQuery& query, std::string& result, std::string& message)
	{
		static statsexporter::ObjectsResult* s_result = NULL;
		static std::vector<Object*> s_matches;

		if (!gameIsRunning())
		{
			message = "no match is running";
			return statsexporter::RESPONSE_STATUS_UNAVAILABLE;
		}

		Int kindOf = -1;
		if (!query.kind_of().empty())
		{
			kindOf = kindOfFromName(query.kind_of());
			if (kindOf < 0)
			{
				message = "unknown kind_of";
				return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
			}
		}
		if (query.has_area() && (query.min_x() > query.max_x() || query.min_y() > query.max_y()))
		{
			message = "empty area";
			return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
		}

		std::vector<Object*>& matches = s_matches;
		matches.clear();
		for (Object* obj = TheGameLogic->getFirstObject(); obj != NULL; obj = obj->getNextObject())
		{
			if (obj->getTemplate() == NULL)
			{
				continue;
			}
			if (!query.include_dead() && obj->isEffectivelyDead())
			{
				continue;
			}
			if (query.filter_player() && ownerIndex(obj) != query.player_index())
			{
				continue;
			}
			if (query.has_area())
			{
				const Coord3D* position = obj->getPosition();
				if (position->x < query.min_x() || position->x > query.max_x() || position->y < query.min_y() || position->y > query.max_y())
				{
					continue;
				}
			}
			if (kindOf >= 0 && !obj->isKindOf((KindOfType)kindOf))
			{
				continue;
			}
			matches.push_back(obj);
		}
		std::sort(matches.begin(), matches.end(), objectIdLess);

		const UnsignedInt cursor = query.page().cursor();
		const UnsignedInt pageSize = pageSizeOf(query.page(), kObjectsDefaultPage);
		statsexporter::ObjectsResult& out = arenaResult(s_result);

		size_t first = 0;
		while (first < matches.size() && matches[first]->getID() < (ObjectID)cursor)
		{
			++first;
		}
		size_t end = first;
		std::vector<UnsignedInt> templateIds;
		for (; end < matches.size() && end - first < pageSize; ++end)
		{
			const Object* obj = matches[end];
			const ThingTemplate* tmpl = obj->getTemplate();
			statsexporter::ObjectRow* row = out.add_rows();
			row->set_object_id(obj->getID());
			row->set_template_id(tmpl->getTemplateID());
			row->set_owner_player_index(ownerIndex(obj));
			row->set_x(obj->getPosition()->x);
			row->set_y(obj->getPosition()->y);
			const BodyModuleInterface* body = obj->getBodyModule();
			if (body != NULL && body->getMaxHealth() > 0.0f)
			{
				Real percent = body->getHealth() * 100.0f / body->getMaxHealth();
				percent = (percent < 0.0f) ? 0.0f : ((percent > 100.0f) ? 100.0f : percent);
				row->set_health_percent((uint32_t)(percent + 0.5f));
			}
			row->set_veterancy((int32_t)obj->getVeterancyLevel());
			row->set_under_construction(obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION));
			row->set_is_dead(obj->isEffectivelyDead());

			if (std::find(templateIds.begin(), templateIds.end(), (UnsignedInt)tmpl->getTemplateID()) == templateIds.end())
			{
				templateIds.push_back(tmpl->getTemplateID());
				statsexporter::TemplateRef* ref = out.add_templates();
				ref->set_template_id(tmpl->getTemplateID());
				ref->set_name(tmpl->getName().str());
			}
		}

		statsexporter::PageInfo* page = out.mutable_page();
		page->set_total((uint32_t)matches.size());
		page->set_has_more(end < matches.size());
		page->set_next_cursor((end < matches.size()) ? matches[end]->getID() : 0);
		out.SerializeToString(&result);
		matches.clear();
		return statsexporter::RESPONSE_STATUS_OK;
	}

	// -- Players -------------------------------------------------------------------------------------------------------

	void fillScienceOptions(google::protobuf::RepeatedPtrField<statsexporter::ScienceOption>* target, const ScienceVec& sciences)
	{
		for (size_t i = 0; i < sciences.size(); ++i)
		{
			statsexporter::ScienceOption* option = target->Add();
			option->set_name(TheScienceStore->getInternalNameForScience(sciences[i]).str());
			UnicodeString name;
			UnicodeString description;
			if (TheScienceStore->getNameAndDescription(sciences[i], name, description))
			{
				option->set_display_name(toUtf8(name));
			}
			option->set_cost((uint32_t)TheScienceStore->getSciencePurchaseCost(sciences[i]));
		}
	}

	Status answerPlayer(const statsexporter::PlayerQuery& query, std::string& result, std::string& message)
	{
		static statsexporter::PlayerDetail* s_result = NULL;

		if (!gameIsRunning())
		{
			message = "no match is running";
			return statsexporter::RESPONSE_STATUS_UNAVAILABLE;
		}
		if (query.player_index() < 0)
		{
			message = "player_index must not be negative";
			return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
		}
		if (query.player_index() >= ThePlayerList->getPlayerCount())
		{
			message = "no such player";
			return statsexporter::RESPONSE_STATUS_NOT_FOUND;
		}

		Player* player = ThePlayerList->getNthPlayer(query.player_index());
		if (player == NULL)
		{
			message = "no such player";
			return statsexporter::RESPONSE_STATUS_NOT_FOUND;
		}

		statsexporter::PlayerDetail& out = arenaResult(s_result);
		out.set_player_index(player->getPlayerIndex());
		out.set_name(toUtf8(player->getPlayerDisplayName()));
		out.set_side(player->getSide().str());
		out.set_money(player->getMoney()->countMoney());
		out.set_rank_level(player->getRankLevel());
		out.set_rank_points(player->getSkillPoints());
		out.set_rank_points_next(player->getSkillPointsLevelUp());
		out.set_science_points(player->getSciencePurchasePoints());

		const ScienceVec& owned = player->getSciences();
		for (size_t i = 0; i < owned.size(); ++i)
		{
			out.add_sciences(TheScienceStore->getInternalNameForScience(owned[i]).str());
		}
		ScienceVec purchasable;
		ScienceVec locked;
		TheScienceStore->getPurchasableSciences(player, purchasable, locked);
		fillScienceOptions(out.mutable_purchasable(), purchasable);
		fillScienceOptions(out.mutable_locked(), locked);

		size_t upgradeCount = 0;
		for (UpgradeTemplate* upgrade = TheUpgradeCenter->firstUpgradeTemplate(); upgrade != NULL && upgradeCount < kMaxPlayerUpgrades; upgrade = upgrade->friend_getNext())
		{
			if (upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER && player->hasUpgradeComplete(upgrade))
			{
				out.add_upgrades(upgrade->getUpgradeName().str());
				++upgradeCount;
			}
		}

		// Special powers the player can fire: the same test as the snapshot's readiness list.
		for (Object* obj = TheGameLogic->getFirstObject(); obj != NULL && (size_t)out.powers_size() < kMaxPowers; obj = obj->getNextObject())
		{
			if (obj->getControllingPlayer() != player || obj->isEffectivelyDead() || obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
			{
				continue;
			}
			for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
			{
				SpecialPowerModuleInterface* spmi = (*m)->getSpecialPower();
				if (spmi == NULL)
				{
					continue;
				}
				const ScienceType requiredScience = spmi->getRequiredScience();
				if (requiredScience != SCIENCE_INVALID && !player->hasScience(requiredScience))
				{
					continue;
				}
				statsexporter::PowerStatus* power = out.add_powers();
				power->set_power_name(spmi->getPowerName().str());
				power->set_object_id(obj->getID());
				power->set_template_name(obj->getTemplate()->getName().str());
				power->set_ready_frame(spmi->getReadyFrame());
				power->set_percent_ready(spmi->getPercentReady());
				power->set_ready(spmi->getPercentReady() >= 1.0f);
			}
		}

		// What the player can build right now: its own side's buildable templates whose prerequisites hold.
		const PlayerTemplate* playerTemplate = player->getPlayerTemplate();
		if (playerTemplate != NULL && TheThingFactory != NULL)
		{
			for (const ThingTemplate* head = TheThingFactory->firstTemplate(); head != NULL && (size_t)out.buildable_size() < kMaxBuildable; head = head->friend_getNextTemplate())
			{
				const ThingTemplate* tmpl = finalTemplate(head);
				if (!tmpl->isBuildableItem() || _stricmp(tmpl->getDefaultOwningSide().str(), playerTemplate->getBaseSide().str()) != 0)
				{
					continue;
				}
				if (player->canBuild(tmpl))
				{
					out.add_buildable(tmpl->getName().str());
				}
			}
		}

		out.SerializeToString(&result);
		return statsexporter::RESPONSE_STATUS_OK;
	}

	// -- Map -----------------------------------------------------------------------------------------------------------

	Status answerMap(const statsexporter::MapQuery& query, std::string& result, std::string& message)
	{
		static statsexporter::MapDetail* s_result = NULL;
		static std::vector<UnsignedByte> s_pixels;

		if (TheTerrainLogic == NULL || TheGameLogic == NULL || TheGameLogic->isLoadingMap() || !TheGameLogic->isInInteractiveGame())
		{
			message = "no map is loaded";
			return statsexporter::RESPONSE_STATUS_UNAVAILABLE;
		}

		statsexporter::MapDetail& out = arenaResult(s_result);
		out.set_map_name(StatsExporterCurrentMapName());

		Region3D extent;
		TheTerrainLogic->getExtent(&extent);
		if (extent.width() > 0.0f && extent.height() > 0.0f)
		{
			out.set_has_extent(true);
			out.set_min_x(extent.lo.x);
			out.set_min_y(extent.lo.y);
			out.set_max_x(extent.hi.x);
			out.set_max_y(extent.hi.y);
		}

		size_t waypointCount = 0;
		for (Waypoint* waypoint = TheTerrainLogic->getFirstWaypoint(); waypoint != NULL && waypointCount < kMaxWaypoints; waypoint = waypoint->getNext())
		{
			statsexporter::WaypointInfo* info = out.add_waypoints();
			info->set_id(waypoint->getID());
			info->set_name(waypoint->getName().str());
			info->set_x(waypoint->getLocation()->x);
			info->set_y(waypoint->getLocation()->y);
			info->set_z(waypoint->getLocation()->z);
			++waypointCount;

			// Player_<n>_Start waypoints are the start positions; the lobby numbers them from 0.
			int number = 0;
			char suffix[16] = "";
			if (sscanf(waypoint->getName().str(), "Player_%d_%15s", &number, suffix) == 2 && _stricmp(suffix, "Start") == 0 && number >= 1)
			{
				statsexporter::StartPositionInfo* start = out.add_start_positions();
				start->set_start_position(number - 1);
				start->set_x(waypoint->getLocation()->x);
				start->set_y(waypoint->getLocation()->y);
			}
		}

		if (query.include_minimap() && TheRadar != NULL)
		{
			s_pixels.resize((size_t)RADAR_CELL_WIDTH * RADAR_CELL_HEIGHT * 4);
			Int width = 0;
			Int height = 0;
			if (TheRadar->getTerrainImageForStats(&s_pixels[0], (UnsignedInt)s_pixels.size(), &width, &height) && width > 0 && height > 0)
			{
				statsexporter::Minimap* minimap = out.mutable_minimap();
				minimap->set_width((uint32_t)width);
				minimap->set_height((uint32_t)height);
				minimap->set_rgba((const char*)&s_pixels[0], (size_t)width * (size_t)height * 4);

				// Pixel (px, py) is the world cell the radar's own conversion maps it to.
				ICoord2D origin;
				ICoord2D next;
				Coord3D originWorld;
				Coord3D nextWorld;
				origin.x = 0;
				origin.y = 0;
				next.x = 1;
				next.y = 1;
				TheRadar->radarToWorld2D(&origin, &originWorld);
				TheRadar->radarToWorld2D(&next, &nextWorld);
				minimap->set_origin_x(originWorld.x);
				minimap->set_origin_y(originWorld.y);
				minimap->set_cell_width(nextWorld.x - originWorld.x);
				minimap->set_cell_height(nextWorld.y - originWorld.y);
			}
		}

		out.SerializeToString(&result);
		return statsexporter::RESPONSE_STATUS_OK;
	}

	// -- Feed configuration --------------------------------------------------------------------------------------------

	void fillFeedConfig(statsexporter::FeedConfig& out, const StatsExporterFeedConfig& config)
	{
		out.set_light_interval_ms(config.lightIntervalMs);
		out.set_full_interval_ms(config.fullIntervalMs);
		out.set_events(config.events != FALSE);
		out.set_camera(config.camera != FALSE);
		out.set_chat(config.chat != FALSE);
		out.set_grids(config.grids != FALSE);
		out.set_supply(config.supply != FALSE);
		out.set_tech(config.tech != FALSE);
		out.set_production(config.production != FALSE);
		out.set_grid_resolution(config.gridResolution);
	}

	Status answerFeedConfig(StatsExporterSink* sink, Int slot, UnsignedInt epoch, const statsexporter::FeedConfigQuery& query, std::string& result, std::string& message)
	{
		static statsexporter::FeedConfig* s_result = NULL;

		StatsExporterFeedConfig config;
		sink->getFeedConfig(slot, config);
		if (query.reset())
		{
			config.setDefaults();
		}
		if (query.has_light_interval_ms())
		{
			config.lightIntervalMs = query.light_interval_ms();
		}
		if (query.has_full_interval_ms())
		{
			config.fullIntervalMs = query.full_interval_ms();
		}
		if (query.has_events())
		{
			config.events = query.events();
		}
		if (query.has_camera())
		{
			config.camera = query.camera();
		}
		if (query.has_chat())
		{
			config.chat = query.chat();
		}
		if (query.has_grids())
		{
			config.grids = query.grids();
		}
		if (query.has_supply())
		{
			config.supply = query.supply();
		}
		if (query.has_tech())
		{
			config.tech = query.tech();
		}
		if (query.has_production())
		{
			config.production = query.production();
		}
		if (query.has_grid_resolution())
		{
			config.gridResolution = query.grid_resolution();
		}

		if (config.lightIntervalMs != 0 && (config.lightIntervalMs < 50 || config.lightIntervalMs > 60000))
		{
			message = "light_interval_ms must be 0 or 50..60000";
			return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
		}
		if (config.fullIntervalMs != 0 && (config.fullIntervalMs < 100 || config.fullIntervalMs > 60000))
		{
			message = "full_interval_ms must be 0 or 100..60000";
			return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
		}
		if (config.gridResolution != 8 && config.gridResolution != 16 && config.gridResolution != 32 && config.gridResolution != 64)
		{
			message = "grid_resolution must be 8, 16, 32 or 64";
			return statsexporter::RESPONSE_STATUS_BAD_REQUEST;
		}

		sink->setFeedConfig(slot, epoch, config);
		statsexporter::FeedConfig& out = arenaResult(s_result);
		fillFeedConfig(out, config);
		out.SerializeToString(&result);
		return statsexporter::RESPONSE_STATUS_OK;
	}

	// -- Response framing ----------------------------------------------------------------------------------------------

	void appendVarint(std::string& out, UnsignedInt64 value)
	{
		while (value >= 0x80)
		{
			out.push_back((char)((value & 0x7F) | 0x80));
			value >>= 7;
		}
		out.push_back((char)value);
	}

	void appendLengthDelimited(std::string& out, UnsignedInt field, const std::string& bytes)
	{
		appendVarint(out, ((UnsignedInt64)field << 3) | 2);
		appendVarint(out, bytes.size());
		out.append(bytes);
	}

	// A Response by hand: id, status, message and the already-encoded result under its field. Encoding it from the generated Response
	// would copy the result into a message that has to live on the arena for every request.
	void buildResponse(std::string& out, UnsignedInt id, Status status, const std::string& message, UnsignedInt resultField, const std::string* result)
	{
		out.clear();
		if (id != 0)
		{
			appendVarint(out, (1 << 3) | 0);
			appendVarint(out, id);
		}
		if (status != statsexporter::RESPONSE_STATUS_OK)
		{
			appendVarint(out, (2 << 3) | 0);
			appendVarint(out, (UnsignedInt64)status);
		}
		if (!message.empty())
		{
			appendLengthDelimited(out, 3, message);
		}
		if (result != NULL)
		{
			appendLengthDelimited(out, resultField, *result);	// present even when empty: that is how the oneof is marked
		}
	}

	void answerRequest(StatsExporterSink* sink, const StatsExporterQueuedRequest& queued)
	{
		static std::string s_result;
		static std::string s_response;

		statsexporter::Request request;	// scalars, strings and sub-messages only: nothing here needs the arena
		UnsignedInt id = 0;
		Status status = statsexporter::RESPONSE_STATUS_OK;
		std::string message;
		UnsignedInt resultField = 0;
		Bool resync = FALSE;
		Int slot = -1;
		UnsignedInt epoch = 0;

		for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
		{
			if (queued.from.has(i))
			{
				slot = i;
				epoch = queued.from.epoch(i);
			}
		}

		s_result.clear();
		if (slot < 0 || !request.ParseFromArray(queued.body.data(), (int)queued.body.size()))
		{
			status = statsexporter::RESPONSE_STATUS_BAD_REQUEST;
			message = "undecodable request";
		}
		else
		{
			id = request.id();
			switch (request.query_case())
			{
				case statsexporter::Request::kCatalog:
					resultField = kResultCatalog;
					status = answerCatalog(request.catalog(), s_result, message);
					break;
				case statsexporter::Request::kObject:
					resultField = kResultObject;
					status = answerObject(request.object(), s_result, message);
					break;
				case statsexporter::Request::kObjects:
					resultField = kResultObjects;
					status = answerObjects(request.objects(), s_result, message);
					break;
				case statsexporter::Request::kPlayer:
					resultField = kResultPlayer;
					status = answerPlayer(request.player(), s_result, message);
					break;
				case statsexporter::Request::kMap:
					resultField = kResultMap;
					status = answerMap(request.map(), s_result, message);
					break;
				case statsexporter::Request::kResync:
					resultField = kResultResync;
					resync = TRUE;
					break;
				case statsexporter::Request::kFeedConfig:
					resultField = kResultFeedConfig;
					status = answerFeedConfig(sink, slot, epoch, request.feed_config(), s_result, message);
					break;
				default:
					status = statsexporter::RESPONSE_STATUS_BAD_REQUEST;
					message = "no query set";
					break;
			}
		}

		if (status == statsexporter::RESPONSE_STATUS_OK)
		{
			buildResponse(s_response, id, status, message, resultField, &s_result);
			if (s_response.size() > STATS_EXPORTER_MAX_BODY_BYTES)
			{
				status = statsexporter::RESPONSE_STATUS_TOO_LARGE;
				message = "result does not fit a frame, ask for a smaller page";
				resync = FALSE;
			}
		}
		if (status != statsexporter::RESPONSE_STATUS_OK)
		{
			buildResponse(s_response, id, status, message, 0, NULL);
		}

		sink->enqueueFrame(STATS_EXPORTER_FRAME_RESPONSE, (const UnsignedByte*)s_response.data(), (UnsignedInt)s_response.size(), queued.from);

		// The answer is queued first; the resync then gives the reader a new epoch, which makes every sender repeat its state.
		if (resync)
		{
			sink->bumpEpoch(slot, epoch);
		}
	}
}

void StatsExporterQueryPump(StatsExporterSink* sink)
{
	StatsExporterQueuedRequest queued;

	if (!sink->isConnected())
	{
		return;
	}
	for (Int handled = 0; handled < kMaxRequestsPerPump && sink->takeRequest(queued); ++handled)
	{
		answerRequest(sink, queued);
	}
}
