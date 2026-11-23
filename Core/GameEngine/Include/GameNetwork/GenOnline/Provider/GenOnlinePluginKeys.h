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

#pragma once

// Each slot holds one minisign public key for the purpose plugins. A maintainer creates a key with
// minisign -G and pastes the second line of the public key file that it writes, the base64 line that
// starts with RW, between the quotes. The first line of that file names the key id, which goes into the
// comment above the slot. The secret key never enters the repository.
// The current key signs the manifests that ship now. The next key is the key that replaces it, so a
// release that carries both accepts manifests from either key during the change.
// An empty slot holds no key. With both slots empty, every build refuses every external plugin, unless it
// defines GENONLINE_ALLOW_UNSIGNED_PLUGINS, which loads unsigned plugins with a warning. With a key in a slot,
// every build refuses a plugin that fails the check, debug builds included.
namespace GenOnlinePluginKeys
{
// Key id: 3715FA3A98849F81
inline constexpr const char* kPluginsCurrent = "RWSBn4SYOvoVN/7IyKeITXnFjqN1CJ8wCBmQdNaG34qEh9Tk4gj+ieJF";

// Key id: E1CDD5B1BBA7B9F7
inline constexpr const char* kPluginsNext = "RWT3uae7sdXN4X2w/Eb+CjWDVK5kxTsf2movvJj5tX/yVAUi1MkgDTOw";
} // namespace GenOnlinePluginKeys
