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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: FileTransfer.cpp
// Author: Matthew D. Campbell, December 2002
// Description: File Transfer wrapper using TheNetwork
///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/LoadScreen.h"
#include "GameClient/Shell.h"
#include "GameNetwork/FileTransfer.h"
#include "GameNetwork/networkutil.h"

//-------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------

static Bool doFileTransfer( AsciiString filename, MapTransferLoadScreen *ls, Int mask )
{
	Bool fileTransferDone = FALSE;
	Int fileTransferPercent = 0;
	Int i;

	if (mask)
	{
		ls->setCurrentFilename(filename);
		UnsignedInt lastProgressTime = timeGetTime();
		Int lastProgressSum = 0;
		const UnsignedInt timeoutPeriod = 60*1000;
		ls->processTimeout(timeoutPeriod/1000);

		ls->update(0);
		fileTransferDone = FALSE;
		fileTransferPercent = 0;

		UnsignedShort fileCommandID = 0;
		Bool sentFile = FALSE;
		if (TheGameInfo->amIHost())
		{
			Sleep(500);
			fileCommandID = TheNetwork->sendFileAnnounce(filename, mask);
		}
		else
		{
			sentFile = TRUE;
		}

		DEBUG_LOG(("Starting file transfer loop"));

		while (!fileTransferDone)
		{
			if (!sentFile && TheNetwork->areAllQueuesEmpty())
			{
				TheNetwork->sendFile(filename, mask, fileCommandID);
				sentFile = TRUE;
			}

			// get the progress for each player, and take the min for our overall progress
			fileTransferDone = TRUE;
			fileTransferPercent = 100;
			Int progressSum = 0;
			for (i=1; i<MAX_SLOTS; ++i)
			{
				if (TheGameInfo->getConstSlot(i)->isHuman() && !TheGameInfo->getConstSlot(i)->hasMap())
				{
					Int slotTransferPercent = TheNetwork->getFileTransferProgress(i, filename);
					fileTransferPercent = min(fileTransferPercent, slotTransferPercent);
					progressSum += slotTransferPercent;

					if (slotTransferPercent == 0)
						ls->processProgress(i, slotTransferPercent, "MapTransfer:Preparing");
					else if (slotTransferPercent < 100)
						ls->processProgress(i, slotTransferPercent, "MapTransfer:Recieving");
					else
						ls->processProgress(i, slotTransferPercent, "MapTransfer:Done");
				}
			}
			if (fileTransferPercent < 100)
			{
				fileTransferDone = FALSE;
				if (fileTransferPercent == 0)
					ls->processProgress(0, fileTransferPercent, "MapTransfer:Preparing");
				else
					ls->processProgress(0, fileTransferPercent, "MapTransfer:Sending");
			}
			else
			{
				DEBUG_LOG(("File transfer is 100%%!"));
				ls->processProgress(0, fileTransferPercent, "MapTransfer:Done");
			}

			UnsignedInt now = timeGetTime();

			// Progress by any player restarts the timeout, so a slow link is not cut off while it still delivers
			if (progressSum > lastProgressSum)
			{
				lastProgressSum = progressSum;
				lastProgressTime = now;
			}

			if (now - lastProgressTime > timeoutPeriod)
			{
				DEBUG_LOG(("Timing out file transfer"));
				break;
			}
			else
			{
				ls->processTimeout((timeoutPeriod - (now - lastProgressTime))/1000);
			}

			ls->update(fileTransferPercent);
		}

		if (!fileTransferDone)
		{
			return FALSE;
		}
	}

	return TRUE;
}

//-------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------

AsciiString GetBasePathFromPath( AsciiString path )
{
	const char *s = path.reverseFind('\\');
	if (s)
	{
		Int len = s - path.str();

		AsciiString base;
		char *buf = base.getBufferForRead(len + 1);
		memcpy(buf, path.str(), len);
		buf[len] = 0;
		return buf;
	}
	return AsciiString::TheEmptyString;
}

AsciiString GetFileFromPath( AsciiString path )
{
	const char *s = path.reverseFind('\\');
	if (s)
		return s+1;
	return path;
}

AsciiString GetExtensionFromFile( AsciiString fname )
{
	const char *s = fname.reverseFind('.');
	if (s)
		return s+1;
	return fname;
}

AsciiString GetBaseFileFromFile( AsciiString fname )
{
	const char *s = fname.reverseFind('.');
	if (s)
	{
		Int len = s - fname.str();

		AsciiString base;
		char *buf = base.getBufferForRead(len + 1);
		memcpy(buf, fname.str(), len);
		buf[len] = 0;
		return buf;
	}
	return AsciiString::TheEmptyString;
}

AsciiString GetPreviewFromMap( AsciiString path )
{
	AsciiString fname = GetBaseFileFromFile(GetFileFromPath(path));
	AsciiString base = GetBasePathFromPath(path);

	AsciiString out;
	out.format("%s\\%s.tga", base.str(), fname.str());
	return out;
}

AsciiString GetINIFromMap( AsciiString path )
{
	AsciiString base = GetBasePathFromPath(path);

	AsciiString out;
	out.format("%s\\map.ini", base.str());
	return out;
}

AsciiString GetStrFileFromMap( AsciiString path )
{
	AsciiString base = GetBasePathFromPath(path);

	AsciiString out;
	out.format("%s\\map.str", base.str());
	return out;
}

AsciiString GetSoloINIFromMap( AsciiString path )
{
	AsciiString base = GetBasePathFromPath(path);

	AsciiString out;
	out.format("%s\\solo.ini", base.str());
	return out;
}

AsciiString GetAssetUsageFromMap( AsciiString path )
{
	AsciiString base = GetBasePathFromPath(path);

	AsciiString out;
	out.format("%s\\assetusage.txt", base.str());
	return out;
}

AsciiString GetReadmeFromMap( AsciiString path )
{
	AsciiString base = GetBasePathFromPath(path);

	AsciiString out;
	out.format("%s\\readme.txt", base.str());
	return out;
}

//-------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------

static Int getMapTransferMask(const GameInfo *game)
{
	Int mask = 0;
	for (Int i=1; i<MAX_SLOTS; ++i)
	{
		if (game->getConstSlot(i)->isHuman() && !game->getConstSlot(i)->hasMap())
		{
			DEBUG_LOG(("Adding player %d to transfer mask", i));
			mask |= (1<<i);
		}
	}
	return mask;
}

struct MapSideFile
{
	Int contentsBit;
	AsciiString (*getPath)(AsciiString mapPath);
	Bool changesGameData;	///< A player without this file runs a different simulation than the host.
};

static const MapSideFile s_mapSideFiles[] =
{
	{ 2, GetPreviewFromMap, FALSE },
	{ 4, GetINIFromMap, TRUE },
	{ 8, GetStrFileFromMap, FALSE },
	{ 16, GetSoloINIFromMap, TRUE },
	{ 32, GetAssetUsageFromMap, FALSE },
	{ 64, GetReadmeFromMap, FALSE },
};

static void getMapTransferFiles(const GameInfo *game, std::vector<AsciiString>& files)
{
	for (Int i = 0; i < ARRAY_SIZE(s_mapSideFiles); ++i)
	{
		if (game->getMapContentsMask() & s_mapSideFiles[i].contentsBit)
		{
			files.push_back(s_mapSideFiles[i].getPath(game->getMap()));
		}
	}
	files.push_back(game->getMap());
}

Int RemoveUntransferableMapContents(const AsciiString& mapPath, Int contentsMask)
{
	for (Int i = 0; i < ARRAY_SIZE(s_mapSideFiles); ++i)
	{
		const Int bit = s_mapSideFiles[i].contentsBit;
		if (!(contentsMask & bit))
		{
			continue;
		}

		const AsciiString path = s_mapSideFiles[i].getPath(mapPath);
		const TransferFileProblem problem = CheckTransferFile(path);
		if (problem == TRANSFER_FILE_OK)
		{
			continue;
		}

		// The bit can come from a file inside an archive, which the sender cannot read. A game data file on disk stays listed, so Start reports it.
		if (problem != TRANSFER_FILE_MISSING && s_mapSideFiles[i].changesGameData)
		{
			continue;
		}

		DEBUG_LOG(("Side file '%s' cannot be transferred and is left out of the map contents", path.str()));
		contentsMask &= ~bit;
	}
	return contentsMask;
}

Bool CanTransferMap(const GameInfo *game, UnicodeString *problemText)
{
	if (!getMapTransferMask(game))
	{
		return TRUE;
	}

	std::vector<AsciiString> files;
	getMapTransferFiles(game, files);
	for (size_t i = 0; i < files.size(); ++i)
	{
		const TransferFileProblem problem = CheckTransferFile(files[i]);
		if (problem != TRANSFER_FILE_OK)
		{
			if (problemText != nullptr)
			{
				*problemText = GetTransferFileProblemText(problem, files[i]);
			}
			return FALSE;
		}
	}
	return TRUE;
}

Bool DoAnyMapTransfers(GameInfo *game)
{
	TheGameInfo = game;
	Int mask = getMapTransferMask(game);
	if (!mask)
		return TRUE;

	if (game->amIHost() && !CanTransferMap(game))
	{
		return FALSE;
	}

	TheShell->hideShell();
	MapTransferLoadScreen *ls = NEW MapTransferLoadScreen;
	ls->init(TheGameInfo);
	std::vector<AsciiString> files;
	getMapTransferFiles(game, files);
	Bool ok = TRUE;
	for (size_t i = 0; ok && i < files.size(); ++i)
	{
		ok = doFileTransfer(files[i], ls, mask);
	}
	delete ls;
	ls = nullptr;
	if (!ok)
		TheShell->showShell();
	return ok;
}
