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

#include "PreRTS.h"

#include "Common/GOUIColors.h"
#include "Common/file.h"
#include "Common/FileSystem.h"

// The table has the INI block syntax:
//   GOUIColors
//     Text = R:255 G:255 B:255 A:255
//   End
// It is read with its own small reader rather than the INI class: the first query can come from inside another INI load
// (OnlineChatColors of multiplayer.ini), and a nested INI load would reset and feed that load's INI CRC.

static const char *const s_tablePath = "Data\\INI\\GOUIColors.ini";
static const char *const s_separators = " \t\r\n=:";

// the key of each role, in GOUIColorRole order
static const char *const s_roleNames[] =
{
	"Text",
	"Muted",
	"Disabled",
	"Help",
	"Hilite",
	"Line",
	"Notice",
	"Error",
	"Success",
	"Admin",
	"Offline",
	"TooltipFill",
	"TooltipBorder",
	"TooltipText",
	"TooltipSweep",
	"FieldFill",
	"LobbyJoinable",
	"LobbyNotJoinable",
	"LobbyAltRow",
	"LobbyAlmostFull",
	"ConnectionSpinner",
	"ChatDefault",
	"PlayerNormal",
	"LobbyRow",
	"Buddy",
};
static_assert( ARRAY_SIZE( s_roleNames ) == GOUICOLOR_COUNT, "s_roleNames needs a key for every GOUIColorRole" );

static Color s_colors[ GOUICOLOR_COUNT ];
static Bool s_present[ GOUICOLOR_COUNT ];
static Bool s_loaded = FALSE;

// The next token of a line, split at spaces, '=' and ':'; FALSE at the end of the line
static Bool nextToken( const char **cursor, char *token, Int size )
{
	const char *p = *cursor;
	while( *p != 0 && strchr( s_separators, *p ) != nullptr )
		++p;
	if( *p == 0 )
		return FALSE;

	Int length = 0;
	while( *p != 0 && strchr( s_separators, *p ) == nullptr )
	{
		if( length < size - 1 )
			token[ length++ ] = *p;
		++p;
	}
	token[ length ] = 0;
	*cursor = p;
	return TRUE;
}

// R:<0-255> G:<0-255> B:<0-255>, then an optional A:<0-255>
static Bool parseColor( const char *cursor, Color *color )
{
	static const char *const names[ 4 ] = { "R", "G", "B", "A" };
	Int values[ 4 ] = { 0, 0, 0, 255 };
	char token[ 32 ];
	for( Int i = 0; i < 4; ++i )
	{
		if( nextToken( &cursor, token, sizeof( token ) ) == FALSE )
		{
			if( i == 3 )
				break;
			return FALSE;
		}
		if( stricmp( token, names[ i ] ) != 0 || nextToken( &cursor, token, sizeof( token ) ) == FALSE )
			return FALSE;

		char *end = nullptr;
		long value = strtol( token, &end, 10 );
		if( end == token || *end != 0 || value < 0 || value > 255 )
			return FALSE;
		values[ i ] = (Int)value;
	}

	*color = GameMakeColor( (UnsignedByte)values[ 0 ], (UnsignedByte)values[ 1 ], (UnsignedByte)values[ 2 ], (UnsignedByte)values[ 3 ] );
	return TRUE;
}

static Int findRole( const char *name )
{
	for( Int role = 0; role < GOUICOLOR_COUNT; ++role )
	{
		if( stricmp( name, s_roleNames[ role ] ) == 0 )
			return role;
	}
	return -1;
}

// Read the table once, on the first query after the file system is up; without the file every role stays unset
static void loadTable( void )
{
	if( s_loaded || TheFileSystem == nullptr )
		return;
	s_loaded = TRUE;

	if( TheFileSystem->doesFileExist( s_tablePath ) == FALSE )
		return;
	File *file = TheFileSystem->openFile( s_tablePath, File::READ );
	if( file == nullptr )
		return;
	const Int size = file->size();
	char *buffer = file->readEntireAndClose();
	if( buffer == nullptr )
		return;

	Bool inside = FALSE;
	Int lineStart = 0;
	while( lineStart < size )
	{
		Int lineEnd = lineStart;
		while( lineEnd < size && buffer[ lineEnd ] != '\n' )
			++lineEnd;

		char line[ 256 ];
		Int length = lineEnd - lineStart;
		if( length > (Int)sizeof( line ) - 1 )
			length = (Int)sizeof( line ) - 1;
		memcpy( line, buffer + lineStart, length );
		line[ length ] = 0;
		lineStart = lineEnd + 1;

		char *comment = strchr( line, ';' );
		if( comment != nullptr )
			*comment = 0;

		const char *cursor = line;
		char token[ 64 ];
		if( nextToken( &cursor, token, sizeof( token ) ) == FALSE )
			continue;

		if( inside == FALSE )
		{
			inside = stricmp( token, "GOUIColors" ) == 0;
			continue;
		}
		if( stricmp( token, "End" ) == 0 )
		{
			inside = FALSE;
			continue;
		}

		const Int role = findRole( token );
		Color color;
		if( role < 0 || parseColor( cursor, &color ) == FALSE )
		{
			DEBUG_LOG(( "GOUIColors: skipped the line '%s' of %s, not a role colour", line, s_tablePath ));
			continue;
		}
		s_colors[ role ] = color;
		s_present[ role ] = TRUE;
	}

	delete[] buffer;
}

Bool GOUIColorFind( GOUIColorRole role, Color *color )
{
	loadTable();
	if( role < 0 || role >= GOUICOLOR_COUNT || s_present[ role ] == FALSE )
		return FALSE;

	*color = s_colors[ role ];
	return TRUE;
}

Color GOUIColor( GOUIColorRole role, Color fallback )
{
	Color color;
	if( GOUIColorFind( role, &color ) )
		return color;
	return fallback;
}
