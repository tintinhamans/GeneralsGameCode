/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GenOnline/Provider/GenOnlineNetworkOverlay.h"

#include "Common/AdaptiveQuality.h"
#include "GameClient/Color.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameNetwork/ConnectionStats.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/GenOnline/ConnectionRating.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include "GameNetwork/GenOnline/GenOnlineGame.h"
#include "GameNetwork/GenOnline/NetworkMesh.h"

#if defined(GENONLINE)
namespace
{
	enum OverlayColumn
	{
		COL_SLOT, COL_NAME, COL_BARS, COL_PATH, COL_PROBE, COL_CMD, COL_JIT, COL_LOST, COL_LATE,
		COL_CUSHION, COL_SAMPLES, COL_STALLS, COL_DEPTH, COL_OUT, COL_IN,
		COL_COUNT
	};

	const Int k_playerViewTop = 500;
	const Real k_devViewTopFraction = 0.10f;	// the dev table sits higher to clear the control bar
	const Int k_changeShownMs = 10000;
	const Int k_nameMaxChars = 14;
	const Int k_devViewHeaderLines = 2;
	const Int k_devViewFooterLines = 3;

	const wchar_t *const s_overlayHeaders[COL_COUNT] =
	{
		L"Slot", L"Player", L"Bars", L"Path", L"Probe", L"Cmd", L"Jit", L"Lost%", L"Late%",
		L"Cush5/50", L"n", L"Stalls", L"Depth", L"OutL%", L"InL%"
	};

	const wchar_t *const s_overlayChangeNames[] = { L"", L"start", L"lag spike", L"stall", L"clean" };
	const wchar_t *const s_overlayControllerNames[] = { L"n/a", L"seed", L"hold", L"baseline", L"cover", L"uncover", L"congestion", L"outage", L"formula" };

	struct OverlayRow
	{
		OverlayRow() : used(FALSE), bars(-1), slowPc(FALSE), direct(FALSE), pingMs(0), lossPercent(0.0f), latePercent(0.0f)
		{
			for (Int c = 0; c < COL_COUNT; ++c)
			{
				severity[c] = ConnectionRating::SEVERITY_OK;
			}
		}

		Bool used;
		UnicodeString name;
		Int bars;
		Bool slowPc;
		Bool direct;
		Int pingMs;
		Real lossPercent;
		Real latePercent;
		NetworkOverlayInfo info;
		UnicodeString cell[COL_COUNT];
		Int severity[COL_COUNT];
	};

	Int overlayPercentSeverity(Real percent)
	{
		if (percent >= 5.0f)
		{
			return ConnectionRating::SEVERITY_BAD;
		}
		if (percent >= 0.5f)
		{
			return ConnectionRating::SEVERITY_WARN;
		}
		return ConnectionRating::SEVERITY_OK;
	}

	Color overlayColor(Int severity)
	{
		if (severity >= ConnectionRating::SEVERITY_BAD)
		{
			return GameMakeColor(255, 90, 80, 255);
		}
		if (severity == ConnectionRating::SEVERITY_WARN)
		{
			return GameMakeColor(255, 195, 50, 255);
		}
		return GameMakeColor(235, 235, 235, 255);
	}

	Color overlayDim()
	{
		return GameMakeColor(165, 165, 165, 255);
	}

	// the overlay owns one DisplayString per text slot, so a slot whose text is unchanged keeps its built sentence and
	// quads. a shared string would rebuild a sentence surface and texture on every draw, which is very slow on some drivers
	const Int k_overlayRefreshMs = 250;
	const Int k_strMeasure = 0;
	const Int k_strHeaderBase = 1;
	const Int k_strHeaderCount = 8;
	const Int k_strRowBase = k_strHeaderBase + k_strHeaderCount;
	const Int k_strRowCount = 8;
	const Int k_strDevHeadBase = k_strRowBase + MAX_SLOTS * k_strRowCount;
	const Int k_strDevCellBase = k_strDevHeadBase + COL_COUNT;
	const Int k_strFooterBase = k_strDevCellBase + MAX_SLOTS * COL_COUNT;
	const Int k_strFooterCount = 8;
	const Int k_strAqBase = k_strFooterBase + k_strFooterCount;
	const Int k_strAqCount = 8;
	const Int k_strTotal = k_strAqBase + k_strAqCount;
	const Int k_aqDevLines = 3;

	const wchar_t *overlayAqReason(const AdaptiveQualityOverlay &aq)
	{
		if (aq.level <= 0)
		{
			return L"";
		}
		if (aq.lastTo > aq.lastFrom)
		{
			return aq.lastReason == AdaptiveQualityModel::REASON_OVER ? L"late frames" : L"slow frames";
		}
		return L"recovering";
	}

	const wchar_t *overlayAqDeadlineName(Int kind)
	{
		return kind == AdaptiveQualityTuning::DEADLINE_VSYNC ? L"vsync" : (kind == AdaptiveQualityTuning::DEADLINE_CAP ? L"fps cap" : L"floor");
	}

	struct OverlayCache
	{
		OverlayCache() : valid(FALSE), mode(0), refreshedMs(0), lineH(12), logicRate(0), rowCount(0), aqValid(FALSE), fpsAverage(0), fpsLow(0)
		{
			for (Int i = 0; i < k_strTotal; ++i)
			{
				strings[i] = nullptr;
			}
			for (Int c = 0; c < COL_COUNT; ++c)
			{
				headerCell[c] = s_overlayHeaders[c];
			}
		}

		Bool valid;
		Int mode;
		UnsignedInt refreshedMs;
		Int lineH;
		UnsignedInt logicRate;
		Int rowCount;
		NetworkOverlayInfo header;
		Bool aqValid;
		AdaptiveQualityOverlay aq;
		UnsignedInt fpsAverage;
		UnsignedInt fpsLow;
		UnicodeString waitingOn;
		UnicodeString headerCell[COL_COUNT];
		OverlayRow rows[MAX_SLOTS];
		DisplayString *strings[k_strTotal];
	};

	OverlayCache *s_overlayCache = nullptr;
	GameFont *s_overlayFont = nullptr;

	void overlayReleaseCache()
	{
		if (s_overlayCache == nullptr)
		{
			return;
		}
		for (Int i = 0; i < k_strTotal; ++i)
		{
			if (s_overlayCache->strings[i] != nullptr && TheDisplayStringManager != nullptr)
			{
				TheDisplayStringManager->freeDisplayString(s_overlayCache->strings[i]);
			}
		}
		delete s_overlayCache;
		s_overlayCache = nullptr;
	}

	DisplayString *overlayString(Int index)
	{
		DisplayString *&pString = s_overlayCache->strings[index];
		if (pString == nullptr)
		{
			pString = TheDisplayStringManager->newDisplayString();
		}
		pString->setFont(s_overlayFont);
		return pString;
	}

	void overlaySetText(DisplayString *pString, const UnicodeString &text)
	{
		if (!(text == pString->getText()))
		{
			pString->setText(text);
		}
	}

	Int overlayTextWidth(Int index, const UnicodeString &text)
	{
		DisplayString *pString = overlayString(index);
		overlaySetText(pString, text);
		return pString->getWidth();
	}

	Int overlayDrawText(Int index, Int x, Int y, const UnicodeString &text, Color color, Color dropColor)
	{
		DisplayString *pString = overlayString(index);
		overlaySetText(pString, text);
		pString->draw(x, y, color, dropColor);
		return pString->getWidth();
	}

	Int overlayTableWidth(const Int *colWidth, const Bool *colShown, Int gap)
	{
		Int total = 0;
		Int shown = 0;
		for (Int c = 0; c < COL_COUNT; ++c)
		{
			if (colShown[c])
			{
				total += colWidth[c];
				++shown;
			}
		}
		return total + (shown > 1 ? (shown - 1) * gap : 0);
	}
}
#endif

void GenOnlineDrawNetworkOverlay(Int mode, DisplayString *fontSource, Color gameTimeDropColor)
{
#if defined(GENONLINE)
	if (TheGenOnlineGame == nullptr || TheNetwork == nullptr)
	{
		return;
	}

	NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
	GenOnlineLobbyInterface* pLobbyInterface = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
	if (pMesh == nullptr || pLobbyInterface == nullptr)
	{
		return;
	}

	if (s_overlayCache == nullptr)
	{
		s_overlayCache = new OverlayCache();
	}
	OverlayCache& cache = *s_overlayCache;
	s_overlayFont = fontSource->getFont();

	const Color dropColor = gameTimeDropColor;
	const Color neutral = overlayColor(ConnectionRating::SEVERITY_OK);
	const Color dim = overlayDim();

	const UnsignedInt nowMs = timeGetTime();
	const Bool refresh = !cache.valid || cache.mode != mode || nowMs - cache.refreshedMs >= (UnsignedInt)k_overlayRefreshMs;
	if (refresh)
	{
		cache.valid = TRUE;
		cache.mode = mode;
		cache.refreshedMs = nowMs;

		Int lineW, lineH;
		DisplayString *pMeasure = overlayString(k_strMeasure);
		overlaySetText(pMeasure, UnicodeString(L"Wg"));
		pMeasure->getSize(&lineW, &lineH);
		cache.lineH = (std::max)(lineH, 12);
		cache.logicRate = TheNetwork->getMeasuredLogicRate();
		TheNetwork->getOverlayInfo(-1, cache.header);
		cache.aqValid = AdaptiveQuality::get().getOverlay(cache.aq);
		cache.fpsAverage = (UnsignedInt)(TheDisplay->getAverageFPS() + 0.5f);
		cache.fpsLow = (UnsignedInt)(TheDisplay->getLow1PercentFPS() + 0.5f);

		for (Int slot = 0; slot < MAX_SLOTS; ++slot)
		{
			cache.rows[slot] = OverlayRow();
		}
		cache.rowCount = 0;
		cache.waitingOn.clear();
		OverlayRow (&rows)[MAX_SLOTS] = cache.rows;
		Int& rowCount = cache.rowCount;
		UnicodeString& waitingOn = cache.waitingOn;

		for (auto& connection : pMesh->GetAllConnections())
		{
			PlayerConnection& conn = connection.second;
			if (!conn.IsValid())
			{
				continue;
			}

			LobbyMemberEntry member = pLobbyInterface->GetRoomMemberFromID(connection.first);
			if (member.display_name.empty() || member.m_SlotIndex < 0 || member.m_SlotIndex >= MAX_SLOTS)
			{
				continue;
			}

			const Int slot = member.m_SlotIndex;
			OverlayRow& row = rows[slot];
			row.used = TRUE;
			row.name.set(from_utf8(member.display_name).c_str());
			row.bars = conn.ComputeConnectionRating();
			row.slowPc = conn.m_rating.slowPc;
			row.direct = conn.IsDirect();
			row.pingMs = conn.GetLatency();
			++rowCount;

			const ConnectionStats* pCmdStats = TheNetwork->getConnectionStats(slot);
			const ConnectionStats& probeStats = conn.m_probeStats;
			const Bool hasCmd = (pCmdStats != nullptr && pCmdStats->getSampleCount() > 0);
			row.lossPercent = hasCmd ? pCmdStats->getLossPercent() : probeStats.getLossPercent();
			row.latePercent = hasCmd ? pCmdStats->getLatePercent() : 0.0f;
			TheNetwork->getOverlayInfo(slot, row.info);

			if (row.info.waitingOnSlot)
			{
				if (!waitingOn.isEmpty())
				{
					waitingOn.concat(L", ");
				}
				waitingOn.concat(row.name);
			}

			if (mode != 2)
			{
				continue;
			}

			UnicodeString* cell = row.cell;
			Int* sev = row.severity;
			cell[COL_SLOT].format(L"%d", slot);
			cell[COL_NAME] = row.name;
			cell[COL_NAME].truncateTo(k_nameMaxChars);
			if (row.bars > 0)
			{
				cell[COL_BARS].format(L"%d", row.bars);
				sev[COL_BARS] = ConnectionRating::Severity(row.bars);
			}
			else
			{
				cell[COL_BARS] = L"-";
			}
			cell[COL_PATH].format(L"%hs %hs", conn.IsIPV4() ? "IPv4" : "IPv6", row.direct ? "direct" : "relay");

			if (probeStats.getSampleCount() > 0)
			{
				cell[COL_PROBE].format(L"%.0f", probeStats.getLatency());
			}
			else
			{
				cell[COL_PROBE] = L"-";
			}

			if (hasCmd)
			{
				const Real jitter = pCmdStats->getJitter();
				cell[COL_CMD].format(L"%.0f", pCmdStats->getLatency());
				cell[COL_JIT].format(L"%.1f", jitter);
				sev[COL_JIT] = (jitter >= 60.0f) ? ConnectionRating::SEVERITY_BAD : ((jitter >= 30.0f) ? ConnectionRating::SEVERITY_WARN : ConnectionRating::SEVERITY_OK);
				cell[COL_LOST].format(L"%.0f", pCmdStats->getLossPercent());
				sev[COL_LOST] = overlayPercentSeverity(pCmdStats->getLossPercent());
				cell[COL_LATE].format(L"%.0f", pCmdStats->getLatePercent());
				sev[COL_LATE] = overlayPercentSeverity(pCmdStats->getLatePercent());
				cell[COL_DEPTH].format(L"%d/%d", pCmdStats->getRedundancyDepth(), pCmdStats->getRedundancyCap());
				sev[COL_DEPTH] = (pCmdStats->getRedundancyCap() > 0 && pCmdStats->getRedundancyDepth() >= pCmdStats->getRedundancyCap()) ? ConnectionRating::SEVERITY_WARN : ConnectionRating::SEVERITY_OK;
			}
			else
			{
				cell[COL_CMD] = L"-";
				cell[COL_JIT] = L"-";
				cell[COL_LOST] = L"-";
				cell[COL_LATE] = L"-";
				cell[COL_DEPTH] = L"-";
			}

			if (row.info.cushionSamples > 0)
			{
				cell[COL_CUSHION].format(L"%d/%d", row.info.cushionP5, row.info.cushionP50);
				sev[COL_CUSHION] = (row.info.cushionP5 <= 0) ? ConnectionRating::SEVERITY_BAD : ((row.info.cushionP5 <= row.info.marginFrames) ? ConnectionRating::SEVERITY_WARN : ConnectionRating::SEVERITY_OK);
			}
			else
			{
				cell[COL_CUSHION] = L"-";
			}
			cell[COL_SAMPLES].format(L"%d", row.info.cushionSamples);
			cell[COL_STALLS].format(L"%d", row.info.stallsWindow);
			sev[COL_STALLS] = (row.info.stallsWindow >= 3 && !row.slowPc) ? ConnectionRating::SEVERITY_BAD : ((row.info.stallsWindow > 0) ? ConnectionRating::SEVERITY_WARN : ConnectionRating::SEVERITY_OK);

			// out is how packets to the peer fare, in is how packets from the peer fare
			PacketFeedback::Summary feedbackOut;
			PacketFeedback::Summary feedbackIn;
			if (conn.GetOutgoingLoss(NETWORK_STATS_WINDOW_MS, feedbackOut))
			{
				cell[COL_OUT].format(L"%.1f", feedbackOut.lossPercent);
				sev[COL_OUT] = overlayPercentSeverity(feedbackOut.lossPercent);
			}
			else
			{
				cell[COL_OUT] = L"-";
			}
			if (conn.GetIncomingLoss(NETWORK_STATS_WINDOW_MS, feedbackIn))
			{
				cell[COL_IN].format(L"%.1f", feedbackIn.lossPercent);
				sev[COL_IN] = overlayPercentSeverity(feedbackIn.lossPercent);
			}
			else
			{
				cell[COL_IN] = L"-";
			}
		}
	}

	const Int lineH = cache.lineH;
	const Int segmentGap = (lineH * 3) / 4;
	const Int displayWidth = TheDisplay->getWidth();
	const Int displayHeight = TheDisplay->getHeight();

	const Int left = 0;

	OverlayRow (&rows)[MAX_SLOTS] = cache.rows;
	const Int rowCount = cache.rowCount;
	const UnicodeString& waitingOn = cache.waitingOn;
	const NetworkOverlayInfo& header = cache.header;

	const Int tableLines = (mode == 2) ? (k_devViewHeaderLines + rowCount + k_devViewFooterLines + (cache.aqValid ? k_aqDevLines : 0)) : (1 + rowCount + (cache.aqValid ? 1 : 0));
	Int y = (mode == 2) ? (Int)(displayHeight * k_devViewTopFraction) : k_playerViewTop;
	y = (std::min)(y, displayHeight - 4 - tableLines * lineH);
	y = (std::max)(y, 0);

	const Int delayMs = header.runAhead * 1000 / LOGICFRAMES_PER_SECOND;
	{
		Int x = left;
		UnicodeString text;
		text.format(L"Input delay %d ms, logic rate %u Hz", delayMs, cache.logicRate);
		x += overlayDrawText(k_strHeaderBase + 0, x, y, text, header.stalledNow ? overlayColor(ConnectionRating::SEVERITY_BAD) : neutral, dropColor) + segmentGap;

		const Bool moving = (header.changeAgoMs >= 0 && header.changeAgoMs < k_changeShownMs);
		if (moving && header.changeDir > 0)
		{
			text = L"rising";
			x += overlayDrawText(k_strHeaderBase + 1, x, y, text, overlayColor(ConnectionRating::SEVERITY_WARN), dropColor) + segmentGap / 2;
		}
		else if (moving && header.changeDir < 0)
		{
			text = L"lowering";
			x += overlayDrawText(k_strHeaderBase + 1, x, y, text, GameMakeColor(120, 220, 120, 255), dropColor) + segmentGap / 2;
		}
		else
		{
			text = L"steady";
			x += overlayDrawText(k_strHeaderBase + 1, x, y, text, dim, dropColor) + segmentGap / 2;
		}
		if (header.changeReason > NetworkOverlayInfo::CHANGE_NONE)
		{
			text.format(L"(%ls)", s_overlayChangeNames[header.changeReason]);
			x += overlayDrawText(k_strHeaderBase + 2, x, y, text, dim, dropColor) + segmentGap;
		}

		if (header.stalledNow && !waitingOn.isEmpty())
		{
			text.format(L"waiting on: %ls", waitingOn.str());
			overlayDrawText(k_strHeaderBase + 3, x, y, text, overlayColor(ConnectionRating::SEVERITY_BAD), dropColor);
		}
		y += lineH;
	}

	if (mode != 2)
	{
		Int nameWidth = 0;
		for (Int slot = 0; slot < MAX_SLOTS; ++slot)
		{
			if (rows[slot].used)
			{
				nameWidth = (std::max)(nameWidth, overlayTextWidth(k_strRowBase + slot * k_strRowCount, rows[slot].name));
			}
		}

		Int barRowY[MAX_SLOTS];
		Int barRowBars[MAX_SLOTS];
		Int barRowCount = 0;
		for (Int slot = 0; slot < MAX_SLOTS; ++slot)
		{
			const OverlayRow& row = rows[slot];
			if (!row.used)
			{
				continue;
			}

			// the indicators of all rows are drawn in one batch afterwards, text draws must not sit inside it
			barRowY[barRowCount] = y;
			barRowBars[barRowCount] = row.bars;
			++barRowCount;

			const Int rowStr = k_strRowBase + slot * k_strRowCount;
			Int x = left + lineH + 8;
			UnicodeString text;
			overlayDrawText(rowStr, x, y, row.name, neutral, dropColor);
			x += nameWidth + segmentGap;
			text.format(L"ping %d ms", row.pingMs);
			x += overlayDrawText(rowStr + 1, x, y, text, neutral, dropColor) + segmentGap;
			text = row.direct ? L"direct" : L"relay";
			x += overlayDrawText(rowStr + 2, x, y, text, dim, dropColor) + segmentGap;
			if (row.lossPercent >= 0.5f)
			{
				text.format(L"loss %.0f%%", row.lossPercent);
				x += overlayDrawText(rowStr + 3, x, y, text, overlayColor(overlayPercentSeverity(row.lossPercent)), dropColor) + segmentGap;
			}
			if (row.latePercent >= 0.5f)
			{
				text.format(L"late %.0f%%", row.latePercent);
				x += overlayDrawText(rowStr + 4, x, y, text, overlayColor(overlayPercentSeverity(row.latePercent)), dropColor) + segmentGap;
			}
			if (row.slowPc)
			{
				text = L"slow PC";
				x += overlayDrawText(rowStr + 5, x, y, text, overlayColor(ConnectionRating::SEVERITY_WARN), dropColor) + segmentGap;
			}
			else if (row.info.stallsWindow > 0)
			{
				text.format(L"stalls %d", row.info.stallsWindow);
				x += overlayDrawText(rowStr + 5, x, y, text, overlayColor(row.info.stallsWindow >= 3 ? ConnectionRating::SEVERITY_BAD : ConnectionRating::SEVERITY_WARN), dropColor) + segmentGap;
			}
			y += lineH;
		}

		if (cache.aqValid)
		{
			Int x = left;
			UnicodeString text;
			text.format(L"Quality level %d", cache.aq.level);
			x += overlayDrawText(k_strAqBase + 0, x, y, text, cache.aq.level > 0 ? overlayColor(ConnectionRating::SEVERITY_WARN) : neutral, dropColor) + segmentGap;
			if (cache.aq.level > 0)
			{
				text.format(L"(%ls)", overlayAqReason(cache.aq));
				overlayDrawText(k_strAqBase + 1, x, y, text, dim, dropColor);
			}
			y += lineH;
		}

		if (barRowCount > 0)
		{
			ScopedDisplayBatch batch;
			for (Int barRow = 0; barRow < barRowCount; ++barRow)
			{
				ConnectionRating::DrawIndicator(left, barRowY[barRow], lineH, barRowBars[barRow]);
			}
		}
		return;
	}

	// dev view: one row per peer, every column drawn at its own x as the font is proportional
	Int colWidth[COL_COUNT];
	Bool colShown[COL_COUNT];
	Int colX[COL_COUNT];
	for (Int c = 0; c < COL_COUNT; ++c)
	{
		colShown[c] = TRUE;
		colWidth[c] = overlayTextWidth(k_strDevHeadBase + c, cache.headerCell[c]);
		for (Int slot = 0; slot < MAX_SLOTS; ++slot)
		{
			if (rows[slot].used)
			{
				colWidth[c] = (std::max)(colWidth[c], overlayTextWidth(k_strDevCellBase + slot * COL_COUNT + c, rows[slot].cell[c]));
			}
		}
	}

	Int tableGap = segmentGap;
	if (overlayTableWidth(colWidth, colShown, tableGap) > displayWidth - 8)
	{
		tableGap = (std::max)(4, lineH / 3);
	}
	const Int optionalColumns[3] = { COL_OUT, COL_IN, COL_BARS };
	for (Int o = 0; o < 3 && overlayTableWidth(colWidth, colShown, tableGap) > displayWidth - 8; ++o)
	{
		colShown[optionalColumns[o]] = FALSE;
	}

	{
		Int x = left;
		for (Int c = 0; c < COL_COUNT; ++c)
		{
			colX[c] = x;
			if (colShown[c])
			{
				x += colWidth[c] + tableGap;
			}
		}
	}

	for (Int c = 0; c < COL_COUNT; ++c)
	{
		if (colShown[c])
		{
			overlayDrawText(k_strDevHeadBase + c, colX[c], y, cache.headerCell[c], dim, dropColor);
		}
	}
	y += lineH;

	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		const OverlayRow& row = rows[slot];
		if (!row.used)
		{
			continue;
		}
		for (Int c = 0; c < COL_COUNT; ++c)
		{
			if (colShown[c])
			{
				overlayDrawText(k_strDevCellBase + slot * COL_COUNT + c, colX[c], y, row.cell[c], overlayColor(row.severity[c]), dropColor);
			}
		}
		y += lineH;
	}
	y += lineH / 2;

	{
		Int x = left;
		UnicodeString text;
		text.format(L"R %d frames (%d ms)  send interval %d ms", header.runAhead, delayMs, header.frameGroupingMs);
		x += overlayDrawText(k_strFooterBase + 0, x, y, text, neutral, dropColor) + segmentGap;

		if (header.isRouter && header.controllerOn)
		{
			const Int reasonSeverity = (header.ctlReason == NetworkOverlayInfo::CTL_CONGESTION || header.ctlReason == NetworkOverlayInfo::CTL_OUTAGE) ? ConnectionRating::SEVERITY_BAD : ((header.ctlReason == NetworkOverlayInfo::CTL_COVER || header.ctlInSpike) ? ConnectionRating::SEVERITY_WARN : ConnectionRating::SEVERITY_OK);
			text.format(L"reason %ls", s_overlayControllerNames[header.ctlReason]);
			x += overlayDrawText(k_strFooterBase + 1, x, y, text, overlayColor(reasonSeverity), dropColor) + segmentGap;

			const Int minSumNeeded = 2 * header.marginFrames;
			if (header.ctlMinSum < NetworkOverlayInfo::NO_PAIR_SUM)
			{
				text.format(L"minS %d of %d (pair %d-%d)", header.ctlMinSum, minSumNeeded, header.ctlPairA, header.ctlPairB);
				x += overlayDrawText(k_strFooterBase + 2, x, y, text, overlayColor(header.ctlMinSum <= 0 ? ConnectionRating::SEVERITY_BAD : (header.ctlMinSum < minSumNeeded ? ConnectionRating::SEVERITY_WARN : ConnectionRating::SEVERITY_OK)), dropColor) + segmentGap;
			}
			else
			{
				text = L"minS - (no pair)";
				x += overlayDrawText(k_strFooterBase + 2, x, y, text, dim, dropColor) + segmentGap;
			}
			if (header.ctlReason == NetworkOverlayInfo::CTL_NONE)
			{
				text = L"base n/a  cover n/a";
			}
			else
			{
				text.format(L"base %d  cover %d%ls", header.ctlBase, header.ctlCover, header.ctlInSpike ? L"  in spike" : L"");
			}
			overlayDrawText(k_strFooterBase + 3, x, y, text, neutral, dropColor);
			y += lineH;

			text.format(L"spikes %d.%d/min  size %d ms  since_change %d ms", header.ctlSpikesPerMin10 / 10, header.ctlSpikesPerMin10 % 10, header.ctlSpikeSizeMs, header.ctlSinceChangeMs);
			overlayDrawText(k_strFooterBase + 4, left, y, text, neutral, dropColor);
			y += lineH;
		}
		else
		{
			if (header.changeReason > NetworkOverlayInfo::CHANGE_NONE)
			{
				text.format(L"last change %ls %d s ago", s_overlayChangeNames[header.changeReason], header.changeAgoMs / 1000);
				x += overlayDrawText(k_strFooterBase + 5, x, y, text, neutral, dropColor) + segmentGap;
			}
			text = header.isRouter ? L"controller off" : L"host decides";
			overlayDrawText(k_strFooterBase + 6, x, y, text, dim, dropColor);
			y += lineH;
		}
	}

	if (cache.aqValid)
	{
		const AdaptiveQualityOverlay &aq = cache.aq;
		UnicodeString text;
		const Color costColor = overlayColor((aq.p50Ms > aq.stepDownP50Ms) ? ConnectionRating::SEVERITY_BAD : ((aq.p95Ms > aq.stepUpP95Ms) ? ConnectionRating::SEVERITY_WARN : ConnectionRating::SEVERITY_OK));
		text.format(L"Quality L%d  fps %u (1%% low %u)  own p50 %.1f / p95 %.1f ms (cpu %.1f, present %.1f)  budget p50 %.1f / p95 %.1f  deadline %.1f ms (%ls, %.0f Hz)  catch-up x%d %d/s %.1f ms", aq.level, cache.fpsAverage, cache.fpsLow, aq.p50Ms, aq.p95Ms, aq.cpuMs, aq.presentMs, aq.stepDownP50Ms, aq.stepUpP95Ms, aq.deadlineMs, overlayAqDeadlineName(aq.deadlineKind), aq.refreshHz, aq.catchUpFrames, aq.catchUpPerSec, aq.catchUpMs);
		overlayDrawText(k_strAqBase + 2, left, y, text, costColor, dropColor);
		y += lineH;

		text = L"saving ms";
		for (Int l = 1; l < aq.levelCount; ++l)
		{
			UnicodeString part;
			part.format(L"  L%d %.1f%ls", l, aq.savingMs[l], aq.savingLearned[l] ? L"" : L"*");
			text.concat(part);
		}
		text.concat(L"  (* default)");
		UnicodeString down;
		UnicodeString up;
		if (aq.downInSec >= 0.0f)
		{
			down.format(L"%d s", (Int)(aq.downInSec + 0.5f));
		}
		else
		{
			down = L"-";
		}
		if (aq.upInSec >= 0.0f)
		{
			up.format(L"%d s", (Int)(aq.upInSec + 0.5f));
		}
		else
		{
			up = L"-";
		}
		UnicodeString next;
		next.format(L"  next step down %ls, up %ls", down.str(), up.str());
		text.concat(next);
		overlayDrawText(k_strAqBase + 3, left, y, text, neutral, dropColor);
		y += lineH;

		if (aq.lastFrom >= 0)
		{
			text.format(L"last step L%d -> L%d (%ls) %d s ago", aq.lastFrom, aq.lastTo, aq.lastTo > aq.lastFrom ? (aq.lastReason == AdaptiveQualityModel::REASON_OVER ? L"late frames" : L"slow frames") : L"calm", (Int)(aq.lastAgoSec + 0.5f));
		}
		else
		{
			text = L"no step this match";
		}
		overlayDrawText(k_strAqBase + 4, left, y, text, dim, dropColor);
		y += lineH;
	}
#endif
}

void GenOnlineReleaseNetworkOverlay()
{
#if defined(GENONLINE)
	overlayReleaseCache();
#endif
}
