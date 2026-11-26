/*
**	Command & Conquer Generals(tm)
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

// Registry.cpp
// Simple interface for storing/retrieving registry values
// Author: Matthew D. Campbell, December 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Registry.h"
#if defined(GENONLINE)

#endif
#if defined(GENONLINE)
#include <filesystem>
#include <map>
#include <string>
#endif


Bool  getStringFromRegistry(HKEY root, AsciiString path, AsciiString key, AsciiString& val)
{
	HKEY handle;
	unsigned char buffer[256];
	unsigned long size = 256;
	unsigned long type;
	int returnValue;

	if ((returnValue = RegOpenKeyEx( root, path.str(), 0, KEY_READ, &handle )) == ERROR_SUCCESS)
	{
		returnValue = RegQueryValueEx(handle, key.str(), nullptr, &type, (unsigned char *) &buffer, &size);
		RegCloseKey( handle );
	}

	if (returnValue == ERROR_SUCCESS)
	{
		val = (char *)buffer;
		return TRUE;
	}

	return FALSE;
}

Bool getUnsignedIntFromRegistry(HKEY root, AsciiString path, AsciiString key, UnsignedInt& val)
{
	HKEY handle;
	unsigned char buffer[4];
	unsigned long size = 4;
	unsigned long type;
	int returnValue;

	if ((returnValue = RegOpenKeyEx( root, path.str(), 0, KEY_READ, &handle )) == ERROR_SUCCESS)
	{
		returnValue = RegQueryValueEx(handle, key.str(), nullptr, &type, (unsigned char *) &buffer, &size);
		RegCloseKey( handle );
	}

	if (returnValue == ERROR_SUCCESS)
	{
		val = *(UnsignedInt *)buffer;
		return TRUE;
	}

	return FALSE;
}

Bool setStringInRegistry( HKEY root, AsciiString path, AsciiString key, AsciiString val)
{
	HKEY handle;
	unsigned long type;
	unsigned long returnValue;
	int size;
	char lpClass[] = "REG_NONE";

	if ((returnValue = RegCreateKeyEx( root, path.str(), 0, lpClass, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &handle, nullptr )) == ERROR_SUCCESS)
	{
		type = REG_SZ;
		size = val.getLength()+1;
		returnValue = RegSetValueEx(handle, key.str(), 0, type, (unsigned char *)val.str(), size);
		RegCloseKey( handle );
	}

	return (returnValue == ERROR_SUCCESS);
}

Bool setUnsignedIntInRegistry( HKEY root, AsciiString path, AsciiString key, UnsignedInt val)
{
	HKEY handle;
	unsigned long type;
	unsigned long returnValue;
	int size;
	char lpClass[] = "REG_NONE";

	if ((returnValue = RegCreateKeyEx( root, path.str(), 0, lpClass, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &handle, nullptr )) == ERROR_SUCCESS)
	{
		type = REG_DWORD;
		size = 4;
		returnValue = RegSetValueEx(handle, key.str(), 0, type, (unsigned char *)&val, size);
		RegCloseKey( handle );
	}

	return (returnValue == ERROR_SUCCESS);
}

Bool GetStringFromGeneralsRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";

	fullPath.concat(path);
	DEBUG_LOG(("GetStringFromRegistry - looking in %s for key %s", fullPath.str(), key.str()));
	if (getStringFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
	{
		return TRUE;
	}

	return getStringFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
}

Bool GetStringFromRegistry(AsciiString path, AsciiString key, AsciiString& val)
{
#if RTS_GENERALS
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";
#elif RTS_ZEROHOUR
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";
#endif

	fullPath.concat(path);
	DEBUG_LOG(("GetStringFromRegistry - looking in %s for key %s", fullPath.str(), key.str()));
	if (getStringFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
	{
		return TRUE;
	}

	return getStringFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
}

Bool GetUnsignedIntFromRegistry(AsciiString path, AsciiString key, UnsignedInt& val)
{
#if RTS_GENERALS
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";
#elif RTS_ZEROHOUR
	AsciiString fullPath = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";
#endif

	fullPath.concat(path);
	DEBUG_LOG(("GetUnsignedIntFromRegistry - looking in %s for key %s", fullPath.str(), key.str()));
	if (getUnsignedIntFromRegistry(HKEY_CURRENT_USER, fullPath.str(), key.str(), val))
	{
		return TRUE;
	}

	return getUnsignedIntFromRegistry(HKEY_LOCAL_MACHINE, fullPath.str(), key.str(), val);
}

AsciiString GetRegistryLanguage()
{
	static Bool cached = FALSE;
	// NOTE: static causes a memory leak, but we have to keep it because the value is cached.
	static AsciiString val = "english";
	if (cached) {
		return val;
	} else {
		cached = TRUE;
	}

#if defined(GENONLINE)
	// GenOnline @bugfix arcticdolphin 21/11/2026 Steam changes the language after install without updating the registry, so the archives on disk decide when the value is missing.
	if (!GetStringFromRegistry("", "Language", val))
	{
		// the working directory is the retail folder, also when the client runs from another folder with -setCwd
		char szProcessDir[MAX_PATH] = { 0 };
		DWORD length = GetCurrentDirectoryA(MAX_PATH, szProcessDir);
		if (length > 0 && length < MAX_PATH)
		{
			std::map<std::string, AsciiString> languageFiles;
			languageFiles["German"] = AsciiString("german");
			languageFiles["French"] = AsciiString("french");
			languageFiles["Korean"] = AsciiString("korean");
			languageFiles["Italian"] = AsciiString("italian");
			languageFiles["Spanish"] = AsciiString("spanish");
			languageFiles["Chinese"] = AsciiString("chinese");
			languageFiles["Polish"] = AsciiString("polish");
			languageFiles["Brazilian"] = AsciiString("brazilian");
			languageFiles["English"] = AsciiString("english");

			for (std::map<std::string, AsciiString>::const_iterator it = languageFiles.begin(); it != languageFiles.end(); ++it)
			{
				const std::string filePath = std::string(szProcessDir) + "/" + it->first + ".big";
				if (std::filesystem::exists(filePath))
				{
					val = it->second;
					break;
				}
			}
		}
	}
#else
	GetStringFromRegistry("", "Language", val);
#endif
	return val;
}

AsciiString GetRegistryGameName()
{
	AsciiString val = "GeneralsMPTest";
	GetStringFromRegistry("", "SKU", val);
	return val;
}

UnsignedInt GetRegistryVersion()
{
	UnsignedInt val = 65536;
	GetUnsignedIntFromRegistry("", "Version", val);
	return val;
}

UnsignedInt GetRegistryMapPackVersion()
{
	UnsignedInt val = 65536;
	GetUnsignedIntFromRegistry("", "MapPackVersion", val);
	return val;
}
