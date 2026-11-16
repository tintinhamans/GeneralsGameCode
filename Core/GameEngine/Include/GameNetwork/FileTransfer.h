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
// FILE: FileTransfer.h
// Author: Matthew D. Campbell, December 2002
// Description: File Transfer wrapper using TheNetwork
///////////////////////////////////////////////////////////////////////////////////////

#pragma once

class GameInfo;

// Convenience functions
AsciiString GetBasePathFromPath( AsciiString path );
AsciiString GetFileFromPath( AsciiString path );
AsciiString GetExtensionFromFile( AsciiString fname );
AsciiString GetBaseFileFromFile( AsciiString fname );
AsciiString GetPreviewFromMap( AsciiString path );
AsciiString GetINIFromMap( AsciiString path );
AsciiString GetStrFileFromMap( AsciiString path );
AsciiString GetSoloINIFromMap( AsciiString path );
AsciiString GetAssetUsageFromMap( AsciiString path );
AsciiString GetReadmeFromMap( AsciiString path );

// The meat of file (map) transfers
Bool DoAnyMapTransfers(GameInfo *game);

enum TransferFileProblem CPP_11(: Int)
{
	TRANSFER_FILE_OK,
	TRANSFER_FILE_MISSING,			///< Not on the local file system, which is the only place the sender reads from.
	TRANSFER_FILE_EMPTY,
	TRANSFER_FILE_UNREADABLE,
	TRANSFER_FILE_WRONG_TYPE,
	TRANSFER_FILE_TOO_LARGE,
	TRANSFER_FILE_INVALID,
};

// Applies the checks a receiver runs before it writes a transferred file
TransferFileProblem CheckTransferFile(const AsciiString& path);

UnicodeString GetTransferFileProblemText(TransferFileProblem problem, const AsciiString& path);

// TRUE when no player needs the map or every file of it passes CheckTransferFile. Only the host holds the files.
Bool CanTransferMap(const GameInfo *game, UnicodeString *problemText = nullptr);

// Clears the contents bit of a side file that cannot be sent, unless the game data depends on it
Int RemoveUntransferableMapContents(const AsciiString& mapPath, Int contentsMask);
