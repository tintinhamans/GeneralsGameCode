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

//----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright(C) 2001 - All Rights Reserved
//
//----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: GameText.cpp
//
// Created:   11/07/01
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes
//----------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/GameText.h"
#include "Common/Language.h"
#include "Common/Registry.h"
#include "GameClient/LanguageFilter.h"
#include "Common/Debug.h"
#include "Common/UnicodeString.h"
#include "Common/AsciiString.h"
#include "Common/GlobalData.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/RtlText.h"
#include "Common/UnicodeUtf8.h"
#include "Common/version.h"

#include <unordered_map>




//----------------------------------------------------------------------------
//         Externals
//----------------------------------------------------------------------------

#if defined(RTS_DEBUG)
Bool g_useStringFile = TRUE;
#endif


//----------------------------------------------------------------------------
//         Defines
//----------------------------------------------------------------------------

#define CSF_ID ( ('C'<<24) | ('S'<<16) | ('F'<<8) | (' ') )
#define CSF_LABEL ( ('L'<<24) | ('B'<<16) | ('L'<<8) | (' ') )
#define CSF_STRING ( ('S'<<24) | ('T'<<16) | ('R'<<8) | (' ') )
#define CSF_STRINGWITHWAVE ( ('S'<<24) | ('T'<<16) | ('R'<<8) | ('W') )
#define CSF_VERSION 3

#define MAX_UITEXT_LENGTH (10*1024)
//----------------------------------------------------------------------------
//         Private Types
//----------------------------------------------------------------------------

//===============================
// StringInfo
//===============================

struct StringInfo
{
	AsciiString			label;
	UnicodeString		text;
	AsciiString			speech;
};

struct StringLookUp
{
	AsciiString		*label;
	StringInfo		*info;
};

// Maps a label to its index in the merged StringInfo vector while layering overlay files.
typedef std::map<AsciiString, Int, rts::less_than_nocase<AsciiString> > LabelIndexMap;

//===============================
// CSFHeader
//===============================

struct CSFHeader
{
	Int id;
	Int version;
	Int num_labels;
	Int num_strings;
	Int skip;
	Int langid;

};

//===============================
// struct NoString
//===============================

struct NoString
{
	struct NoString *next;
	UnicodeString text;
};


//===============================
// GameTextManager
//===============================

class GameTextManager : public GameTextInterface
{
	public:

		GameTextManager();
		virtual ~GameTextManager() override;

		virtual void					init() override;						///< Initializes the text system
		virtual void					deinit();					///< Shuts down the text system
		virtual void					update() override {};			///< update text manager
		virtual void					reset() override;					///< Resets the text system

		virtual UnicodeString fetch( const Char *label, Bool *exists = nullptr ) override;		///< Returns the associated labeled unicode text
		virtual UnicodeString fetch( AsciiString label, Bool *exists = nullptr ) override;		///< Returns the associated labeled unicode text
		virtual UnicodeString fetchFormat( const Char *label, ... ) override;
		virtual UnicodeString fetchOrSubstitute( const Char *label, const WideChar *substituteText ) override;
		virtual UnicodeString fetchOrSubstituteFormat( const Char *label, const WideChar *substituteFormat, ... ) override;
		virtual UnicodeString fetchOrSubstituteFormatVA( const Char *label, const WideChar *substituteFormat, va_list args ) override;

		virtual AsciiStringVec& getStringsWithLabelPrefix(AsciiString label) override;

		virtual void					initMapStringFile( const AsciiString& filename ) override;

		virtual UnicodeString toLegacyDisplay( const UnicodeString &text ) override;

	protected:

		Int							m_textCount;
		Int							m_maxLabelLen;
		Char						m_buffer[MAX_UITEXT_LENGTH];
		Char						m_buffer2[MAX_UITEXT_LENGTH];
		Char						m_buffer3[MAX_UITEXT_LENGTH];
		WideChar				m_tbuffer[MAX_UITEXT_LENGTH*2];

		StringInfo			*m_stringInfo;
		StringLookUp		*m_stringLUT;
		Bool						m_initialized;
#if defined(RTS_DEBUG)
		Bool						m_jabberWockie;
		Bool						m_munkee;
#endif
		NoString				*m_noStringList;
		Int							m_useStringFile;
		LanguageID			m_language;
		UnicodeString		m_failed;

		StringInfo			*m_mapStringInfo;
		StringLookUp		*m_mapStringLUT;
		Int							m_mapTextCount;

		/// m_asciiStringVec will be altered every time that getStringsWithLabelPrefix is called,
		/// so don't simply store a pointer to it.
		AsciiStringVec			m_asciiStringVec;

		/// Logical text -> the legacy visual-order original it was converted from (see toLegacyDisplay()).
		std::unordered_map<std::wstring, std::wstring> m_legacyVisual;

		void						stripSpaces ( WideChar *string );
		void						removeLeadingAndTrailing ( Char *m_buffer );
		void						readToEndOfQuote( File *file, Char *in, Char *out, Char *wavefile, Int maxBufLen );
		void						reverseWord ( Char *file, Char *lp );
		void						translateCopy( WideChar *outbuf, Char *inbuf );
		Bool						getStringCount( const Char *filename, Int& textCount );
		Bool						getCSFInfo ( const Char *filename );
		Bool						parseCSF(  const Char *filename, StringInfo *dest, Int& outCount );
		Bool						parseStringFile( const char *filename, StringInfo *dest, Int& outCount );
		Bool						parseMapStringFile( const char *filename );
		Bool						readLine( char *buffer, Int max, File *file );
		Char						readChar( File *file );

		Bool						determineBaseFile( const AsciiString& csfFile, AsciiString& baseFile, Bool& baseIsStr );
		void						collectStringFiles( FilenameList& files );
		Bool						mergeStringFile( const AsciiString& filename, std::vector<StringInfo>& merged, LabelIndexMap& labelIndex );
		void						mergeEntries( const StringInfo *entries, Int count, const char *source, std::vector<StringInfo>& merged, LabelIndexMap& labelIndex );
		Bool						parseMultiLanguageStringFile( const char *filename, const char *column, std::vector<StringInfo>& out );
		void						mergeTextLanguage( std::vector<StringInfo>& merged, LabelIndexMap& labelIndex );
		void						convertLegacyVisualText( std::vector<StringInfo>& merged );
};

static int __cdecl			compareLUT ( const void *,  const void*);
static const char*			getFileBaseName( const AsciiString& path );
static bool						basenameLess( const AsciiString& a, const AsciiString& b );
//----------------------------------------------------------------------------
//         Private Data
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Data
//----------------------------------------------------------------------------

GameTextInterface *TheGameText = nullptr;

// Patch104p's column codes; the native names are escaped so the source stays ASCII.
const GameTextLanguage GameTextLanguages[] =
{
	{ "us", L"English", FALSE },
	{ "de", L"Deutsch", FALSE },
	{ "fr", L"Fran\x00E7" L"ais", FALSE },
	{ "es", L"Espa\x00F1" L"ol", FALSE },
	{ "it", L"Italiano", FALSE },
	{ "ko", L"\xD55C\xAD6D\xC5B4", FALSE },
	{ "zh", L"\x7E41\x9AD4\x4E2D\x6587", FALSE },
	{ "bp", L"Portugu\x00EAs (Brasil)", FALSE },
	{ "pl", L"Polski", FALSE },
	{ "ru", L"\x0420\x0443\x0441\x0441\x043A\x0438\x0439", FALSE },
	{ "uk", L"\x0423\x043A\x0440\x0430\x0457\x043D\x0441\x044C\x043A\x0430", FALSE },
	{ "ar", L"\x0627\x0644\x0639\x0631\x0628\x064A\x0629", TRUE },
};
const Int GameTextLanguageCount = ARRAY_SIZE( GameTextLanguages );

static AsciiString s_textLanguageCode;
static AsciiString s_textLanguagesDir;
static Bool s_textLogicalRtl = FALSE;
static AsciiString s_loadedTextLanguage;

void SetGameTextOptions( const AsciiString &languageCode, const AsciiString &languagesDir, Bool logicalRtl )
{
	s_textLanguageCode = languageCode;
	s_textLanguageCode.toLower();
	s_textLanguagesDir = languagesDir;
	s_textLogicalRtl = logicalRtl;
}

AsciiString GetGameTextLanguage()
{
	return s_loadedTextLanguage;
}

AsciiString GetGameTextLanguagesDir()
{
	return s_textLanguagesDir;
}

Bool IsGameTextRightToLeft()
{
	for ( Int i = 0; i < GameTextLanguageCount; ++i )
	{
		if ( s_loadedTextLanguage.compareNoCase( GameTextLanguages[i].code ) == 0 )
			return GameTextLanguages[i].rightToLeft;
	}
	return FALSE;
}

//----------------------------------------------------------------------------
//         Private Prototypes
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Functions
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Functions
//----------------------------------------------------------------------------

//============================================================================
// CreateGameTextInterface
//============================================================================

GameTextInterface* CreateGameTextInterface()
{
	return NEW GameTextManager;
}


//============================================================================
// GameTextManager::GameTextManager
//============================================================================

GameTextManager::GameTextManager()
:	m_textCount(0),
	m_maxLabelLen(0),
	m_stringInfo(nullptr),
	m_stringLUT(nullptr),
	m_initialized(FALSE),
	m_noStringList(nullptr),
#if defined(RTS_DEBUG)
	m_jabberWockie(FALSE),
	m_munkee(FALSE),
	m_useStringFile(g_useStringFile),
#else
	m_useStringFile(TRUE),
#endif
	m_mapStringInfo(nullptr),
	m_mapStringLUT(nullptr),
	m_failed(L"***FATAL*** String Manager failed to initialize properly")
{
	for(Int i=0; i < MAX_UITEXT_LENGTH; i++)
	{
		m_buffer[i] = 0;
		m_buffer2[i] = 0;
		m_buffer3[i] = 0;
	}
}

//============================================================================
// GameTextManager::~GameTextManager
//============================================================================

GameTextManager::~GameTextManager()
{
	deinit();
}

//============================================================================
// GameTextManager::init
//============================================================================

extern const Char *g_strFile;
extern const Char *g_csfFile;

void GameTextManager::init()
{
	AsciiString csfFile;
	csfFile.format(g_csfFile, GetRegistryLanguage().str());

	if ( m_initialized )
	{
		return;
	}

	m_initialized = TRUE;

	m_maxLabelLen = 0;
#if defined(RTS_DEBUG)
	if(TheGlobalData)
	{
		m_jabberWockie = TheGlobalData->m_jabberOn;
		m_munkee = 	TheGlobalData->m_munkeeOn;
	}
#endif

	AsciiString baseFile;
	Bool baseIsStr;

	if ( !determineBaseFile( csfFile, baseFile, baseIsStr ) )
	{
		return;
	}

	// Gather every *.str/*.csf sitting next to the base file (loose, .big, embedded archive).
	FilenameList files;
	collectStringFiles( files );

	// The chosen base format replaces its sibling entirely; drop it so it can't compete for priority.
	if ( baseIsStr )
	{
		files.erase( csfFile );
	}

	std::vector<AsciiString> sortedFiles( files.begin(), files.end() );
	std::stable_sort( sortedFiles.begin(), sortedFiles.end(), basenameLess );

	// .big-style layering: files sorted case-insensitively by name, alphabetically FIRST wins.
	// Apply lowest priority (last alphabetically) first, so higher priority files overwrite it last.
	std::vector<StringInfo> merged;
	LabelIndexMap labelIndex;

	for ( Int i = (Int)sortedFiles.size() - 1; i >= 0; i-- )
	{
		mergeStringFile( sortedFiles[i], merged, labelIndex );
	}

	// The chosen text language goes over everything the installed language provided.
	mergeTextLanguage( merged, labelIndex );

	if ( s_textLogicalRtl )
	{
		convertLegacyVisualText( merged );
	}

	m_textCount = (Int)merged.size();

	if( m_textCount == 0 )
	{
		return;
	}

	//Allocate StringInfo Array

	m_stringInfo = NEW StringInfo[m_textCount];

	if( m_stringInfo == nullptr )
	{
		deinit();
		return;
	}

	for ( Int i = 0; i < m_textCount; i++ )
	{
		m_stringInfo[i] = merged[i];
	}

	m_stringLUT = NEW StringLookUp[m_textCount];

	StringLookUp *lut = m_stringLUT;
	StringInfo *info = m_stringInfo;

	for ( Int i = 0; i < m_textCount; i++ )
	{
		lut->info = info;
		lut->label = &info->label;
		lut++;
		info++;
	}

	qsort( m_stringLUT, m_textCount, sizeof(StringLookUp), compareLUT  );

}

//============================================================================
// GameTextManager::determineBaseFile
//============================================================================
// Decides which file provides the base table, mirroring the original
// m_useStringFile-then-CSF precedence: Generals.str, if enabled and present,
// replaces Generals.csf entirely.

Bool GameTextManager::determineBaseFile( const AsciiString& csfFile, AsciiString& baseFile, Bool& baseIsStr )
{
	Int count;

	if ( m_useStringFile && getStringCount( g_strFile, count ) )
	{
		baseFile = g_strFile;
		baseIsStr = TRUE;
		return TRUE;
	}

	if ( getCSFInfo( csfFile.str() ) )
	{
		baseFile = csfFile;
		baseIsStr = FALSE;
		return TRUE;
	}

	return FALSE;
}

//============================================================================
// GameTextManager::collectStringFiles
//============================================================================
// Scans data\ and data\<Language>\ (non-recursive, loose + every .big + embedded
// archive) for *.str and *.csf files that may layer over the base table.

void GameTextManager::collectStringFiles( FilenameList& files )
{
	AsciiString neutralDir( "data\\" );
	AsciiString languageDir;
	languageDir.format( "data\\%s\\", GetRegistryLanguage().str() );

	TheFileSystem->getFileListInDirectory( neutralDir, "*.str", files, FALSE );
	TheFileSystem->getFileListInDirectory( neutralDir, "*.csf", files, FALSE );
	TheFileSystem->getFileListInDirectory( languageDir, "*.str", files, FALSE );
	TheFileSystem->getFileListInDirectory( languageDir, "*.csf", files, FALSE );
}

//============================================================================
// GameTextManager::mergeStringFile
//============================================================================
// Parses one overlay (or the base) file and layers it into the merged table:
// new labels are added, labels that already exist are overwritten.

Bool GameTextManager::mergeStringFile( const AsciiString& filename, std::vector<StringInfo>& merged, LabelIndexMap& labelIndex )
{
	const char *ext = filename.reverseFind('.');
	Bool isStr = ext && stricmp( ext, ".str" ) == 0;

	Int capacity = 0;
	if ( isStr )
	{
		if ( !getStringCount( filename.str(), capacity ) || capacity == 0 )
			return FALSE;
	}
	else
	{
		if ( !getCSFInfo( filename.str() ) || m_textCount == 0 )
			return FALSE;
		capacity = m_textCount;
	}

	StringInfo *tempInfo = NEW StringInfo[capacity];
	Int actualCount = 0;
	Bool ok = isStr ? parseStringFile( filename.str(), tempInfo, actualCount )
					: parseCSF( filename.str(), tempInfo, actualCount );

	if ( !ok )
	{
		DEBUG_LOG(("GameText: Failed to parse string file '%s', skipping", filename.str()));
		delete [] tempInfo;
		return FALSE;
	}

	mergeEntries( tempInfo, actualCount, filename.str(), merged, labelIndex );

	delete [] tempInfo;
	return TRUE;
}

//============================================================================
// GameTextManager::mergeEntries
//============================================================================
// Layers parsed entries into the merged table: new labels are added, existing ones overwritten.

void GameTextManager::mergeEntries( const StringInfo *entries, Int count, const char *source, std::vector<StringInfo>& merged, LabelIndexMap& labelIndex )
{
	Int overrideCount = 0;
	for ( Int i = 0; i < count; i++ )
	{
		LabelIndexMap::iterator it = labelIndex.find( entries[i].label );
		if ( it != labelIndex.end() )
		{
			merged[it->second] = entries[i];
			overrideCount++;
		}
		else
		{
			labelIndex[entries[i].label] = (Int)merged.size();
			merged.push_back( entries[i] );
		}
	}

	DEBUG_LOG(("GameText: Loaded string file '%s' (%d entries, %d overridden)", source, count, overrideCount));
}

//============================================================================
// GameTextManager::mergeTextLanguage
//============================================================================
// Layers the chosen text language (SetGameTextOptions()) over the installed one. The base game's text
// comes from the first of these that has the language:
//   1. <languagesDir><code>\generals.csf   a user override: a compiled table for that language, else
//      <languagesDir>generals.str          a multi-language table (Patch104p's format), reading the
//                                          <CODE>: lines and falling back to US: for labels without one
//   2. data\Languages\<code>\generals.csf  the Generals Game Patch text bundled in the embedded archive
//   3. the installed language               (the language then counts as not loaded)
// The game's own strings then come from the other files in data\Languages\<code>\ (the embedded
// archive, loose files or a .big), over whichever base was used.

void GameTextManager::mergeTextLanguage( std::vector<StringInfo>& merged, LabelIndexMap& labelIndex )
{
	s_loadedTextLanguage.clear();
	if ( s_textLanguageCode.isEmpty() )
		return;

	Bool known = FALSE;
	for ( Int i = 0; i < GameTextLanguageCount; ++i )
		known = known || s_textLanguageCode.compare( GameTextLanguages[i].code ) == 0;
	if ( !known )
	{
		DEBUG_LOG(("GameText: Unknown text language '%s', keeping the installed language", s_textLanguageCode.str()));
		return;
	}

	Bool loaded = FALSE;
	if ( !s_textLanguagesDir.isEmpty() )
	{
		AsciiString csfFile;
		csfFile.format( "%s%s\\generals.csf", s_textLanguagesDir.str(), s_textLanguageCode.str() );
		loaded = mergeStringFile( csfFile, merged, labelIndex );

		if ( !loaded )
		{
			AsciiString strFile;
			strFile.format( "%sgenerals.str", s_textLanguagesDir.str() );
			AsciiString column = s_textLanguageCode;
			column.toUpper();
			std::vector<StringInfo> entries;
			if ( parseMultiLanguageStringFile( strFile.str(), column.str(), entries ) && !entries.empty() )
			{
				mergeEntries( &entries[0], (Int)entries.size(), strFile.str(), merged, labelIndex );
				loaded = TRUE;
			}
		}
	}

	AsciiString gameDir;
	gameDir.format( "data\\Languages\\%s\\", s_textLanguageCode.str() );

	if ( !loaded )
	{
		AsciiString bundledFile;
		bundledFile.format( "%sgenerals.csf", gameDir.str() );
		loaded = mergeStringFile( bundledFile, merged, labelIndex );
		DEBUG_LOG(("GameText: %s bundled '%s'", loaded ? "Using" : "No", bundledFile.str()));
	}

	if ( !loaded )
	{
		DEBUG_LOG(("GameText: No '%s' text in '%s' or the bundled files, keeping the installed language", s_textLanguageCode.str(), s_textLanguagesDir.str()));
		return;
	}

	FilenameList files;
	TheFileSystem->getFileListInDirectory( gameDir, "*.csf", files, FALSE );
	TheFileSystem->getFileListInDirectory( gameDir, "*.str", files, FALSE );
	std::vector<AsciiString> sortedFiles( files.begin(), files.end() );
	std::stable_sort( sortedFiles.begin(), sortedFiles.end(), basenameLess );
	for ( Int i = (Int)sortedFiles.size() - 1; i >= 0; i-- )
	{
		// The bundled base table was either merged above or is shadowed by the user's override.
		if ( stricmp( getFileBaseName( sortedFiles[i] ), "generals.csf" ) == 0 )
			continue;
		mergeStringFile( sortedFiles[i], merged, labelIndex );
	}

	s_loadedTextLanguage = s_textLanguageCode;
}

//============================================================================
// GameTextManager::convertLegacyVisualText
//============================================================================
// Right-to-left text that a table stores for GameFont (pre-shaped, in visual order) goes to logical
// order for RmlUi; toLegacyDisplay() turns it back, exactly, through m_legacyVisual.

void GameTextManager::convertLegacyVisualText( std::vector<StringInfo>& merged )
{
	m_legacyVisual.clear();
	for ( size_t i = 0; i < merged.size(); ++i )
	{
		const std::wstring visual( merged[i].text.str() );
		if ( !RtlText::looksLegacyVisual( visual ) )
			continue;
		const std::wstring logical = RtlText::legacyVisualToLogical( visual );
		m_legacyVisual[logical] = visual;
		merged[i].text = logical.c_str();
	}
	DEBUG_LOG(("GameText: Converted %d legacy visual-order strings to logical order", (Int)m_legacyVisual.size()));
}

//============================================================================
// GameTextManager::toLegacyDisplay
//============================================================================

UnicodeString GameTextManager::toLegacyDisplay( const UnicodeString &text )
{
	if ( !s_textLogicalRtl || text.isEmpty() )
		return text;

	Bool rtl = FALSE;
	for ( const WideChar *c = text.str(); *c && !rtl; ++c )
		rtl = RtlText::isRtl( *c );
	if ( !rtl )
		return text;

	const std::wstring logical( text.str() );
	std::unordered_map<std::wstring, std::wstring>::const_iterator it = m_legacyVisual.find( logical );
	if ( it != m_legacyVisual.end() )
		return UnicodeString( it->second.c_str() );

	return UnicodeString( RtlText::logicalToLegacyVisual( logical, IsGameTextRightToLeft() ).c_str() );
}

//============================================================================
// GameTextManager::parseMultiLanguageStringFile
//============================================================================
// Reads a UTF-8 multi-language .str (Patch104p's generals.str, the game's Assets/Localization/English/450_450_GeneralsOnline.str):
//   LABEL
//   US: "text"
//   DE: "Text"
//   END
// For each label it keeps the given column's line, else the US: one (or a plain "text" line). Escapes
// and whitespace follow the classic .str rules (readToEndOfQuote/translateCopy/stripSpaces).

Bool GameTextManager::parseMultiLanguageStringFile( const char *filename, const char *column, std::vector<StringInfo>& out )
{
	File *file = TheFileSystem->openFile( filename, File::READ | File::BINARY );
	if ( file == nullptr )
		return FALSE;

	const Int size = file->size();
	std::string data;
	if ( size > 0 )
	{
		data.resize( size );
		if ( file->read( &data[0], size ) != size )
			data.clear();
	}
	file->close();
	file = nullptr;

	if ( data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF )
		data.erase( 0, 3 );

	AsciiString label;
	std::string chosen;
	std::string fallback;
	Bool haveChosen = FALSE;
	Bool haveFallback = FALSE;
	Bool inEntry = FALSE;

	size_t pos = 0;
	while ( pos < data.size() )
	{
		size_t end = data.find( '\n', pos );
		if ( end == std::string::npos )
			end = data.size();
		std::string line = data.substr( pos, end - pos );
		pos = end + 1;

		const size_t first = line.find_first_not_of( " \t\r" );
		if ( first == std::string::npos )
			continue;
		const size_t last = line.find_last_not_of( " \t\r" );
		line = line.substr( first, last - first + 1 );
		if ( line.compare( 0, 2, "//" ) == 0 )
			continue;

		if ( !inEntry )
		{
			label = line.c_str();
			chosen.clear();
			fallback.clear();
			haveChosen = FALSE;
			haveFallback = FALSE;
			inEntry = TRUE;
			continue;
		}

		if ( stricmp( line.c_str(), "END" ) == 0 )
		{
			if ( haveChosen || haveFallback )
			{
				const std::string &raw = haveChosen ? chosen : fallback;
				// translateCopy()'s escapes, on UTF-8 rather than single bytes.
				std::string unescaped;
				unescaped.reserve( raw.size() );
				for ( size_t k = 0; k < raw.size(); ++k )
				{
					const char c = raw[k];
					if ( c == '\\' && k + 1 < raw.size() )
					{
						const char e = raw[++k];
						if ( e == 'n' )
							unescaped.push_back( '\n' );
						else if ( e == 't' )
							unescaped.push_back( '\t' );
						else
							unescaped.push_back( e );
					}
					else
					{
						unescaped.push_back( (c == '\t' || c == '\r') ? ' ' : c );
					}
				}

				const UnicodeString text = utf8ToUnicode( unescaped );
				std::vector<WideChar> buffer( text.getLength() + 1, 0 );
				if ( text.getLength() > 0 )
					memcpy( &buffer[0], text.str(), text.getLength() * sizeof( WideChar ) );
				stripSpaces( &buffer[0] );

				StringInfo info;
				info.label = label;
				info.text = &buffer[0];
				out.push_back( info );
			}
			inEntry = FALSE;
			continue;
		}

		// Either 'XX: "text"' or a plain '"text"', which counts as US.
		std::string key;
		size_t quote = std::string::npos;
		if ( line[0] == '"' )
		{
			key = "US";
			quote = 0;
		}
		else if ( line.size() > 3 && isalpha( (unsigned char)line[0] ) && isalpha( (unsigned char)line[1] ) && line[2] == ':' )
		{
			key = line.substr( 0, 2 );
			key[0] = (char)toupper( (unsigned char)key[0] );
			key[1] = (char)toupper( (unsigned char)key[1] );
			quote = line.find( '"', 3 );
		}
		if ( quote == std::string::npos )
			continue;

		// The text runs to the next unescaped quote, over more lines if need be (a line break is a space).
		std::string text;
		std::string rest = line.substr( quote + 1 );
		Bool closed = FALSE;
		for ( ;; )
		{
			Bool slash = FALSE;
			for ( size_t k = 0; k < rest.size(); ++k )
			{
				const char c = rest[k];
				if ( c == '"' && !slash )
				{
					closed = TRUE;
					break;
				}
				slash = ( c == '\\' && !slash );
				text.push_back( c );
			}
			if ( closed || pos >= data.size() )
				break;
			size_t next = data.find( '\n', pos );
			if ( next == std::string::npos )
				next = data.size();
			rest = data.substr( pos, next - pos );
			pos = next + 1;
			text.push_back( ' ' );
		}

		if ( key == column )
		{
			chosen = text;
			haveChosen = TRUE;
		}
		else if ( key == "US" )
		{
			fallback = text;
			haveFallback = TRUE;
		}
	}

	return TRUE;
}

//============================================================================
// GameTextManager::deinit
//============================================================================

void GameTextManager::deinit()
{

	delete [] m_stringInfo;
	m_stringInfo = nullptr;

	delete [] m_stringLUT;
	m_stringLUT = nullptr;

	m_textCount = 0;

	NoString *noString = m_noStringList;

	DEBUG_LOG_RAW(("\n"));
	DEBUG_LOG(("*** Missing strings ***"));
	while ( noString )
	{
		DEBUG_LOG(("*** %ls ***", noString->text.str()));
		NoString *next = noString->next;
		delete noString;
		noString = next;
	}
	DEBUG_LOG(("*** End missing strings ***"));
	DEBUG_LOG_RAW(("\n"));

	m_noStringList = nullptr;

	m_initialized = FALSE;
}

//============================================================================
// GameTextManager::reset
//============================================================================

void GameTextManager::reset()
{
	delete [] m_mapStringInfo;
	m_mapStringInfo = nullptr;

	delete [] m_mapStringLUT;
	m_mapStringLUT = nullptr;
}


//============================================================================
// GameTextManager::stripSpaces
//============================================================================

void GameTextManager::stripSpaces ( WideChar *string )
{
	WideChar *str, *ptr;
	WideChar ch, last = 0;
	Int skipall = TRUE;

	str = ptr = string;

	while ( (ch = *ptr++) != 0 )
	{
		if ( ch == ' '  )
		{
			if ( last == ' ' || skipall )
			{
				continue;
			}
		}

		if ( ch == '\n' || ch == '\t' )
		{
				// remove last space
				if ( last == ' ' )
				{
					str--;
				}

				skipall = TRUE;		// skip all spaces
				last = *str++ = ch;
				continue;
		}

		last = *str++ = ch;
		skipall = FALSE;
	}

	if ( last == ' ' )
	{
		str--;
	}

	*str = 0;
}

//============================================================================
// GameTextManager::removeLeadingAndTrailing
//============================================================================

void GameTextManager::removeLeadingAndTrailing ( Char *buffer )
{
	Char *first, *ptr;
	Char ch;

	ptr = first = buffer;

	while ( (ch = *first) != 0 && iswspace ( ch ))
	{
			first++;
	}

	while ( (*ptr++ = *first++) != 0 );

	ptr -= 2;

	while ( (ptr > buffer) && (ch = *ptr) != 0 && iswspace ( ch ) )
	{
		ptr--;
	}

	ptr++;
	*ptr = 0;
}

//============================================================================
// GameTextManager::readToEndOfQuote
//============================================================================

void GameTextManager::readToEndOfQuote( File *file, Char *in, Char *out, Char *wavefile, Int maxBufLen )
{
	Int slash = FALSE;
	Int state = 0;
	Int line_start = FALSE;
	Char ch;
	Int ccount = 0;
	Int len = 0;
	Int done = FALSE;

	while ( maxBufLen )
	{
		// get next Char

		if ( in )
		{
			if ( (ch = *in++) == 0 )
			{
				in = nullptr; // have exhausted the input m_buffer
				ch = readChar ( file );
			}
		}
		else
		{
			ch = readChar ( file );
		}

		if ( ch == EOF )
		{
			return ;
		}

		if ( ch == '\n' )
		{
			line_start = TRUE;
			slash = FALSE;
			ccount = 0;
			ch = ' ';
		}
		else if ( ch == '\\' && !slash)
		{
			slash = TRUE;
		}
		else if ( ch == '\\' && slash)
		{
			slash = FALSE;
		}
		else if ( ch == '"' && !slash )
		{
			break; // done
		}
		else
		{
			slash = FALSE;
		}

		if ( iswspace ( ch ))
		{
			ch = ' ';
		}

		*out++ = ch;
		maxBufLen--;
	}

	*out = 0;

	while ( !done )
	{
		// get next Char

		if ( in )
		{
			if ( (ch = *in++) == 0 )
			{
				in = nullptr; // have exhausted the input m_buffer
				ch = readChar ( file );
			}
		}
		else
		{
			ch = readChar ( file );
		}

		if ( ch == '\n' || ch == EOF )
		{
			break;
		}

		switch ( state )
		{

			case 0:
				if ( iswspace ( ch ) || ch == '=' )
				{
					break;
				}

				state = 1;
				FALLTHROUGH;
			case 1:
				if ( ( ch >= 'a' && ch <= 'z') || ( ch >= 'A' && ch <='Z') || (ch >= '0' && ch <= '9') || ch == '_' )
				{
					*wavefile++ = ch;
					len++;
					break;
				}
				state = 2;
				FALLTHROUGH;
			case 2:
				break;
		}
	}

	*wavefile = 0;

	if ( len )
	{
		if ( ( ch = *(wavefile-1)) >= '0' && ch <= '9' )
		{
			*wavefile++ = 'e';
			*wavefile = 0;
		}
	}

}


//============================================================================
// GameTextManager::reverseWord
//============================================================================

void GameTextManager::reverseWord ( Char *file, Char *lp )
{
	Int first = TRUE;
	Char f, l;
	Int ok = TRUE	;

	while ( ok )
	{
		if ( file >= lp )
		{
			return;
		}

		f = *file;
		l = *lp;

		if ( first )
		{
			if ( f >= 'A' && f <= 'Z' )
			{
				if ( l >= 'a' && l <= 'z' )
				{
					f = (f - 'A') + 'a';
					l = (l - 'a') + 'A';
				}
			}

			first = FALSE;
		}

		*lp-- = f;
		*file++ = l;

	}

}

//============================================================================
// GameTextManager::translateCopy
//============================================================================

void GameTextManager::translateCopy( WideChar *outbuf, Char *inbuf )
{
	Int slash = FALSE;

#if defined(RTS_DEBUG)
	if ( m_jabberWockie )
	{
		static Char buffer[MAX_UITEXT_LENGTH*2];
		Char *firstLetter = nullptr, *lastLetter;
		Char *b = buffer;
		Int formatWord = FALSE;
		Char ch;

		while ( (ch = *inbuf++) != 0 )
		{
			if ( ! (( ch >= 'a' && ch <= 'z') || ( ch >= 'A' && ch <= 'Z' )))
			{
				if ( firstLetter )
				{
					if ( !formatWord )
					{
						lastLetter = b-1;
						reverseWord ( firstLetter, lastLetter );
					}
					firstLetter = nullptr;
					formatWord = FALSE;
				}
				*b++ = ch;
				if ( ch == '\\' )
				{
					*b++ = *inbuf++;
				}
				if ( ch == '%' )
				{
					while ( (ch = *inbuf++) != 0 && !( (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')))
					{
						*b++ = ch;
					}
					*b++ = ch;
				}
			}
			else
			{
				if ( !firstLetter )
				{
					firstLetter = b;
				}

				*b++ = ch;

			}
		}

		if ( firstLetter )
		{
			lastLetter = b-1;
			reverseWord ( firstLetter, lastLetter );
		}

		*b++ = 0;
		inbuf = buffer;
	}
	else if( m_munkee )
	{
		wcscpy(outbuf, L"Munkee");
		return;
	}
#endif

	while( *inbuf != '\0' )
	{
		if( slash == TRUE )
		{
			slash = FALSE;

			switch( *inbuf )
			{
				// in case end of string is reached
				// should never happen!!!
				case '\0':
					return;

				case '\\':
					*outbuf++ = '\\';
					break;

				case '\'':
					*outbuf++ = '\'';
					break;

				case '\"':
					*outbuf++ = '\"';
					break;

				case '\?':
					*outbuf++ = '\?';
					break;

				case 't':
					*outbuf++ = '\t';
					break;

				case 'n':
					*outbuf++ = '\n';
					break;

				default:
					*outbuf++ = *inbuf & 0x00FF;
					break;
			}
		}
		else if( *inbuf != '\\' )
		{
			*outbuf++ = *inbuf & 0x00FF;
		}
		else
			slash = TRUE;

		inbuf++;
	}
	*outbuf= 0;
}

//============================================================================
// GameTextManager::getStringCount
//============================================================================

Bool GameTextManager::getStringCount( const char *filename, Int& textCount )
{
	Int ok = TRUE;

	textCount = 0;

	File *file;
	file = TheFileSystem->openFile(filename, File::READ | File::TEXT);
	DEBUG_LOG(("Looking in %s for string file", filename));

	if ( file == nullptr )
	{
		return FALSE;
	}

	while(ok)
	{
		if( !readLine( m_buffer, sizeof( m_buffer) -1, file ) )
			break;
		removeLeadingAndTrailing ( m_buffer );

		if( m_buffer[0] == '"' )
		{
				Int len = strlen(m_buffer);
				m_buffer[ len ] = '\n';
				m_buffer[ len+1] = 0;
			readToEndOfQuote( file, &m_buffer[1], m_buffer2, m_buffer3, MAX_UITEXT_LENGTH );
		}
		else if( stricmp( m_buffer, "END") == 0 )
		{
			textCount++;
		}
	}

	textCount += 500;
	file->close();
	file = nullptr;
	return TRUE;
}

//============================================================================
// GameTextManager::getCSFInfo
//============================================================================

Bool GameTextManager::getCSFInfo ( const Char *filename )
{
	CSFHeader header;
	Int ok = FALSE;
	File *file = TheFileSystem->openFile(filename, File::READ | File::BINARY);
	DEBUG_LOG(("Looking in %s for compiled string file", filename));

	if ( file != nullptr )
	{
		if ( file->read( &header, sizeof ( header )) == sizeof ( header ) )
		{
			if ( header.id == CSF_ID )
			{
				m_textCount = header.num_labels;

				if ( header.version >= 2 )
				{
					m_language = (LanguageID) header.langid;
				}
				else
				{
					m_language = LANGUAGE_ID_US;
				}

				ok = TRUE;
			}
		}

		file->close();
		file = nullptr;
	}

	return ok;
}

//============================================================================
// GameTextManager::parseCSF
//============================================================================

Bool GameTextManager::parseCSF( const Char *filename, StringInfo *dest, Int& outCount )
{
	File *file;
	Int id;
	Int len;
	Int listCount = 0;
	Bool ok = FALSE;
	CSFHeader header;

	file = TheFileSystem->openFile(filename, File::READ | File::BINARY);

	if ( file == nullptr )
	{
		return FALSE;
	}

	if (  file->read ( &header, sizeof ( CSFHeader)) != sizeof ( CSFHeader) )
	{
		return FALSE;
	}

	while( file->read ( &id, sizeof (id)) == sizeof ( id) )
	{
		Int num;
		Int num_strings;

		if ( id != CSF_LABEL )
		{
			goto quit;
		}

		file->read ( &num_strings, sizeof ( Int ));

		file->read ( &len, sizeof ( Int ) );

		if ( len )
		{
			file->read ( m_buffer, len );
		}

		m_buffer[len] = 0;

		dest[listCount].label = m_buffer;


		if ( len > m_maxLabelLen )
		{
			m_maxLabelLen = len;
		}

		num = 0;

		while ( num < num_strings )
		{
		 	file->read ( &id, sizeof ( Int ) );

			if ( id != CSF_STRING && id != CSF_STRINGWITHWAVE )
			{
				goto quit;
			}

		 	file->read ( &len, sizeof ( Int ) );

			if ( len )
			{
				file->read ( m_tbuffer, len*sizeof(WideChar) );
			}

			if ( num == 0 )
			{
				// only use the first string found
				m_tbuffer[len] = 0;

				{
					WideChar *ptr;

					ptr = m_tbuffer;

					while ( *ptr )
					{
						*ptr = ~*ptr;
						ptr++;
					}
				}

				stripSpaces ( m_tbuffer );
				dest[listCount].text = m_tbuffer;
			}

			if ( id == CSF_STRINGWITHWAVE )
			{
			 	file->read ( &len, sizeof ( Int ) );
				if ( len )
				{
					file->read ( m_buffer, len );
				}
				m_buffer[len] = 0;

				if ( num == 0 && len )
				{
					// only use the first string found
					dest[listCount].speech = m_buffer;
				}

			}

			num++;
		}

		listCount++;
	}

	ok = TRUE;

quit:

	file->close();
	file = nullptr;

	outCount = listCount;
	return ok;
}


//============================================================================
// GameTextManager::parseStringFile
//============================================================================

Bool GameTextManager::parseStringFile( const char *filename, StringInfo *dest, Int& outCount )
{
	Int listCount = 0;
	Int ok = TRUE;

	File *file = TheFileSystem->openFile(filename, File::READ | File::TEXT);

	if ( file == nullptr )
	{
		return FALSE;
	}

	while( ok )
	{
		Int len;
		if( !readLine( m_buffer, MAX_UITEXT_LENGTH, file ))
		{
			break;
		}

		removeLeadingAndTrailing ( m_buffer );

		if( ( *(unsigned short *)m_buffer == 0x2F2F) || !m_buffer[0])			//	0x2F2F is Hex for //
			continue;

		// make sure label is unique

		for ( Int i = 0; i < listCount; i++ )
		{
			if ( stricmp ( dest[i].label.str(), m_buffer ) == 0)
			{
				DEBUG_CRASH ( ("String label '%s' multiply defined!", m_buffer ));
			}
		}

		dest[listCount].label = m_buffer;
		len = strlen ( m_buffer );


		if ( len > m_maxLabelLen )
		{
			m_maxLabelLen = len;
		}

		Bool readString = FALSE;
		while( ok )
		{
			if (!readLine ( m_buffer, sizeof(m_buffer)-1, file ))
			{
				DEBUG_CRASH (("Unexpected end of string file"));
				ok = FALSE;
				goto quit;
			}

			removeLeadingAndTrailing ( m_buffer );

			if( m_buffer[0] == '"' )
			{
				len = strlen(m_buffer);
				m_buffer[ len ] = '\n';
				m_buffer[ len+1] = 0;
				readToEndOfQuote( file, &m_buffer[1], m_buffer2, m_buffer3, MAX_UITEXT_LENGTH );


				if ( readString )
				{
					// only one string per label allows
						DEBUG_CRASH ( ("String label '%s' has more than one string defined!", dest[listCount].label.str()));
				}
				else
				{
					// Copy string into new home
					translateCopy( m_tbuffer, m_buffer2 );
					stripSpaces ( m_tbuffer );

					dest[listCount].text = m_tbuffer ;
					dest[listCount].speech = m_buffer3;
					readString = TRUE;
				}
			}
			else if ( stricmp ( m_buffer, "END" ) == 0)
			{
				break;
			}
		}

		listCount++;
	}

quit:

	file->close();
	file = nullptr;

	outCount = listCount;
	return ok;
}

//============================================================================
// GameTextManager::initMapStringFile
//============================================================================

void GameTextManager::initMapStringFile( const AsciiString& filename )
{
	m_mapTextCount = 0;
	getStringCount( filename.str(), m_mapTextCount );

	m_mapStringInfo = NEW StringInfo[m_mapTextCount];

	parseMapStringFile( filename.str() );

	m_mapStringLUT = NEW StringLookUp[m_mapTextCount];

	StringLookUp *lut = m_mapStringLUT;
	StringInfo *info = m_mapStringInfo;

	for ( Int i = 0; i < m_mapTextCount; i++ )
	{
		lut->info = info;
		lut->label = &info->label;
		lut++;
		info++;
	}

	qsort( m_mapStringLUT, m_mapTextCount, sizeof(StringLookUp), compareLUT  );
}

//============================================================================
// GameTextManager::parseMapStringFile
//============================================================================

Bool GameTextManager::parseMapStringFile( const char *filename )
{
	Int listCount = 0;
	Int ok = TRUE;

	File *file;

	file = TheFileSystem->openFile(filename, File::READ | File::TEXT);
	if ( file == nullptr )
	{
		return FALSE;
	}

	while( ok )
	{
		Int len;
		if( !readLine( m_buffer, MAX_UITEXT_LENGTH, file ))
		{
			break;
		}

		removeLeadingAndTrailing ( m_buffer );

		if( ( *(unsigned short *)m_buffer == 0x2F2F) || !m_buffer[0])			//	0x2F2F is Hex for //
			continue;

		// make sure label is unique

		for ( Int i = 0; i < listCount; i++ )
		{
			if ( stricmp ( m_mapStringInfo[i].label.str(), m_buffer ) == 0)
			{
				DEBUG_CRASH ( ("String label '%s' multiply defined!", m_buffer ));
			}
		}

		m_mapStringInfo[listCount].label = m_buffer;
		len = strlen ( m_buffer );


		if ( len > m_maxLabelLen )
		{
			m_maxLabelLen = len;
		}

		Bool readString = FALSE;
		while( ok )
		{
			if (!readLine ( m_buffer, sizeof(m_buffer)-1, file ))
			{
				DEBUG_CRASH (("Unexpected end of string file"));
				ok = FALSE;
				goto quit;
			}

			removeLeadingAndTrailing ( m_buffer );

			if( m_buffer[0] == '"' )
			{
				len = strlen(m_buffer);
				m_buffer[ len ] = '\n';
				m_buffer[ len+1] = 0;
				readToEndOfQuote( file, &m_buffer[1], m_buffer2, m_buffer3, MAX_UITEXT_LENGTH );


				if ( readString )
				{
					// only one string per label allowed
						DEBUG_CRASH ( ("String label '%s' has more than one string defined!", m_stringInfo[listCount].label.str()));
				}
				else
				{
					// Copy string into new home
					translateCopy( m_tbuffer, m_buffer2 );
					stripSpaces ( m_tbuffer );

					UnicodeString text = UnicodeString(m_tbuffer);
					if (TheLanguageFilter)
						TheLanguageFilter->filterLine(text);

					m_mapStringInfo[listCount].text = text;
					m_mapStringInfo[listCount].speech = m_buffer3;
					readString = TRUE;
				}
			}
			else if ( stricmp ( m_buffer, "END" ) == 0)
			{
				break;
			}
		}

		listCount++;
	}

quit:

	file->close();
	file = nullptr;

	return ok;
}

//============================================================================
// *GameTextManager::fetch
//============================================================================

UnicodeString GameTextManager::fetch( const Char *label, Bool *exists )
{
	DEBUG_ASSERTCRASH ( m_initialized, ("String Manager has not been m_initialized") );

	if( m_stringInfo == nullptr )
	{
		if( exists )
			*exists = FALSE;
		return m_failed;
	}

	StringLookUp *lookUp;
	StringLookUp key;
	AsciiString lb;
	lb = label;
	key.info = nullptr;
	key.label = &lb;

	lookUp = (StringLookUp *) bsearch( &key, (void*) m_stringLUT, m_textCount, sizeof(StringLookUp), compareLUT );

	if ( lookUp == nullptr && m_mapStringLUT && m_mapTextCount )
	{
		lookUp = (StringLookUp *) bsearch( &key, (void*) m_mapStringLUT, m_mapTextCount, sizeof(StringLookUp), compareLUT );
	}

	if( lookUp == nullptr )
	{

		// string not found
		if( exists )
			*exists = FALSE;

		// See if we already have the missing string
		UnicodeString missingString;
		missingString.format(L"MISSING: '%hs'", label);

		NoString *noString = m_noStringList;

		while ( noString )
		{
			if (noString->text == missingString)
				return missingString;

			noString = noString->next;
		}

		//DEBUG_LOG(("*** MISSING:'%s' ***", label));
		// Remember file could have been altered at this point.
		noString = NEW NoString;
		noString->text = missingString;
		noString->next = m_noStringList;
		m_noStringList = noString;
		return noString->text;
	}
	if( exists )
		*exists = TRUE;
	return lookUp->info->text;
}

//============================================================================
// *GameTextManager::fetch
//============================================================================

UnicodeString GameTextManager::fetch( AsciiString label, Bool *exists )
{
	return fetch(label.str(), exists);
}

//============================================================================
// *GameTextManager::fetchFormat
//============================================================================

UnicodeString GameTextManager::fetchFormat( const Char *label, ... )
{
	Bool exists;
	UnicodeString str = fetch(label, &exists);
	if (exists)
	{
		UnicodeString strFormat;

		va_list args;
		va_start(args, label);
		strFormat.format_va(str.str(), args);
		va_end(args);

		str = strFormat;
	}
	return str;
}

//============================================================================
// GameTextManager::fetchOrSubstitute
//============================================================================

UnicodeString GameTextManager::fetchOrSubstitute( const Char *label, const WideChar *substituteText )
{
	Bool exists;
	UnicodeString str = fetch(label, &exists);
	if (!exists)
		str = substituteText;
	return str;
}

//============================================================================
// GameTextManager::fetchOrSubstituteFormat
//============================================================================

UnicodeString GameTextManager::fetchOrSubstituteFormat( const Char *label, const WideChar *substituteFormat, ... )
{
	va_list args;
	va_start(args, substituteFormat);
	UnicodeString str = fetchOrSubstituteFormatVA(label, substituteFormat, args);
	va_end(args);

	return str;
}

//============================================================================
// GameTextManager::fetchOrSubstituteFormatVA
//============================================================================

UnicodeString GameTextManager::fetchOrSubstituteFormatVA( const Char *label, const WideChar *substituteFormat, va_list args )
{
	Bool exists;
	UnicodeString str = fetch(label, &exists);
	if (exists)
	{
		UnicodeString strFormat;
		strFormat.format_va(strFormat.str(), args);
		str = strFormat;
	}
	else
	{
		str.format_va(substituteFormat, args);
	}

	return str;
}

//============================================================================
// GameTextManager::getStringsWithLabelPrefix
//============================================================================

AsciiStringVec& GameTextManager::getStringsWithLabelPrefix(AsciiString label)
{
	m_asciiStringVec.clear();
	if (m_stringLUT) {
		for (int i = 0; i < m_textCount; ++i) {
			if (strstr(m_stringLUT[i].label->str(), label.str()) == m_stringLUT[i].label->str()) {
				m_asciiStringVec.push_back(*m_stringLUT[i].label);
			}
		}
	}
	if (m_mapStringLUT) {
		for (int i = 0; i < m_mapTextCount; ++i) {
			if (strstr(m_mapStringLUT[i].label->str(), label.str()) == m_mapStringLUT[i].label->str()) {
				m_asciiStringVec.push_back(*m_mapStringLUT[i].label);
			}
		}
	}
	return m_asciiStringVec;
}

//============================================================================
// GameTextManager::readLine
//============================================================================

Bool	GameTextManager::readLine( char *buffer, Int max, File *file )
{
	Int ok = FALSE;

	while ( max && file->read( buffer, 1 ) == 1 )
	{
		ok = TRUE;

		if ( *buffer == '\n' )
		{
			break;
		}

		buffer++;
		max--;
	}

	*buffer = 0;

	return ok;
}

//============================================================================
// GameTextManager::readChar
//============================================================================

Char	GameTextManager::readChar( File *file )
{
	Char ch;

	if ( file->read( &ch, 1 ) == 1 )
	{
		return ch;
	}

	return 0;
}

//============================================================================
// compareLUT
//============================================================================

static int __cdecl compareLUT ( const void *i1,  const void*i2)
{
	StringLookUp *lut1 = (StringLookUp*) i1;
	StringLookUp *lut2 = (StringLookUp*) i2;

	return stricmp( lut1->label->str(), lut2->label->str());
}

//============================================================================
// getFileBaseName
//============================================================================

static const char* getFileBaseName( const AsciiString& path )
{
	const char *slash = path.reverseFind('\\');
	const char *fslash = path.reverseFind('/');
	if ( fslash && (!slash || fslash > slash) )
		slash = fslash;
	return slash ? slash + 1 : path.str();
}

//============================================================================
// basenameLess
//============================================================================

static bool basenameLess( const AsciiString& a, const AsciiString& b )
{
	return stricmp( getFileBaseName(a), getFileBaseName(b) ) < 0;
}
