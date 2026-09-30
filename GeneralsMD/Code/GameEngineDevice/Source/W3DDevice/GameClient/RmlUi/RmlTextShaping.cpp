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

#include "W3DDevice/GameClient/RmlUi/RmlTextShaping.h"

#include "HarfBuzz/FontEngineInterfaceHarfBuzz.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Plugin.h>

#include <map>
#include <memory>

namespace RmlTextShaping
{

namespace
{

using Engine = RmlHarfBuzz::FontEngineInterfaceHarfBuzz;

Engine *s_engine = nullptr;

// Font files stay in memory for as long as FreeType uses them; a variable font serving several
// weights is read once.
std::map<std::string, std::vector<Rml::byte>> s_fontData;

// A face the UI can use: the family it registers as, the weight, and the files that can provide
// it (a static file first, then a variable font with that weight as a named instance).
struct FontRole
{
	const char *family;
	Rml::Style::FontWeight weight;
	const char *files[3];
};

const Rml::Style::FontWeight REGULAR = Rml::Style::FontWeight::Normal;
const Rml::Style::FontWeight BOLD = Rml::Style::FontWeight::Bold;
const Rml::Style::FontWeight BLACK = (Rml::Style::FontWeight)900;

const FontRole NOTO_SANS = { "Noto Sans", REGULAR, { "NotoSans-Regular.ttf", "NotoSans-VF.ttf", nullptr } };
const FontRole NOTO_SANS_BOLD = { "Noto Sans", BOLD, { "NotoSans-Bold.ttf", "NotoSans-VF.ttf", nullptr } };
const FontRole EXO2_BOLD = { "Exo 2", BOLD, { "Exo2-Bold.ttf", "Exo2-VF.ttf", nullptr } };
const FontRole OSWALD_BOLD = { "Oswald", BOLD, { "Oswald-Bold.ttf", "Oswald-VF.ttf", nullptr } };
const FontRole ARABIC_BODY = { "Noto Sans Arabic", REGULAR, { "NotoSansArabic-Regular.ttf", "NotoSansArabic-VF.ttf", nullptr } };
const FontRole ARABIC_HEADING = { "Noto Kufi Arabic", BOLD, { "NotoKufiArabic-Bold.ttf", "NotoKufiArabic-VF.ttf", nullptr } };
const FontRole KOREAN_BODY = { "Noto Sans KR", REGULAR, { "NotoSansKR-Regular.ttf", "NotoSansKR-VF.ttf", nullptr } };
const FontRole KOREAN_HEADING = { "Noto Sans KR", BLACK, { "NotoSansKR-Black.ttf", "NotoSansKR-VF.ttf", nullptr } };
const FontRole CHINESE_BODY = { "Noto Sans TC", REGULAR, { "NotoSansTC-Regular.ttf", "NotoSansTC-VF.ttf", nullptr } };
const FontRole CHINESE_HEADING = { "Noto Sans TC", BLACK, { "NotoSansTC-Black.ttf", "NotoSansTC-VF.ttf", nullptr } };
// The system's own CJK fonts when the language's font pack is missing.
const FontRole MALGUN = { "Malgun Gothic", REGULAR, { "malgun.ttf", nullptr, nullptr } };
const FontRole MALGUN_BOLD = { "Malgun Gothic", BOLD, { "malgunbd.ttf", nullptr, nullptr } };
const FontRole JHENGHEI = { "Microsoft JhengHei", REGULAR, { "msjh.ttc", nullptr, nullptr } };
const FontRole JHENGHEI_BOLD = { "Microsoft JhengHei", BOLD, { "msjhbd.ttc", nullptr, nullptr } };

bool fileExists(const std::string &path)
{
	Rml::FileInterface *files = Rml::GetFileInterface();
	if (!files)
		return false;
	Rml::FileHandle handle = files->Open(path);
	if (!handle)
		return false;
	files->Close(handle);
	return true;
}

const std::vector<Rml::byte> *readFont(const std::string &path)
{
	auto cached = s_fontData.find(path);
	if (cached != s_fontData.end())
		return &cached->second;

	Rml::FileInterface *files = Rml::GetFileInterface();
	Rml::FileHandle handle = files ? files->Open(path) : 0;
	if (!handle)
		return nullptr;
	std::vector<Rml::byte> data(files->Length(handle));
	const size_t read = data.empty() ? 0 : files->Read(data.data(), data.size(), handle);
	files->Close(handle);
	if (read != data.size() || data.empty())
		return nullptr;
	return &(s_fontData[path] = std::move(data));
}

// Loads the role from the first directory that has one of its files; returns whether it loaded.
bool loadRole(const FontRole &role, const std::vector<std::string> &dirs, bool fallback)
{
	for (const char *file : role.files)
	{
		if (!file)
			break;
		for (const std::string &dir : dirs)
		{
			const std::string path = dir + file;
			if (!fileExists(path))
				continue;
			const std::vector<Rml::byte> *data = readFont(path);
			if (!data)
				continue;
			if (Rml::LoadFontFace(Rml::Span<const Rml::byte>(data->data(), data->size()), role.family, Rml::Style::FontStyle::Normal, role.weight,
					fallback))
				return true;
		}
	}
	return false;
}

void substitute(const char *regularFamily, Rml::Style::FontWeight regularWeight, const char *boldFamily, Rml::Style::FontWeight boldWeight)
{
	s_engine->SetFamilySubstitute("Barlow", Engine::FamilySubstitute{ regularFamily ? regularFamily : "", regularWeight },
		Engine::FamilySubstitute{ boldFamily ? boldFamily : "", boldWeight });
}

class DocumentLanguagePlugin : public Rml::Plugin
{
public:
	std::string lang;
	bool rightToLeft = false;

	virtual int GetEventClasses() override { return EVT_DOCUMENT; }

	virtual void OnDocumentLoad(Rml::ElementDocument *document) override
	{
		if (!document)
			return;
		if (!lang.empty())
			document->SetAttribute("lang", lang);
		if (rightToLeft)
			document->SetAttribute("dir", "rtl");
	}
};

DocumentLanguagePlugin s_documentLanguage;
bool s_pluginRegistered = false;

std::string bcp47(const std::string &language)
{
	if (language == "us")
		return "en";
	if (language == "bp")
		return "pt-BR";
	if (language == "zh")
		return "zh-Hant";
	return language;
}

} // namespace

Rml::FontEngineInterface *createFontEngine()
{
	if (!s_engine)
		s_engine = new Engine();
	return s_engine;
}

void destroyFontEngine()
{
	delete s_engine;
	s_engine = nullptr;
	s_fontData.clear();
}

bool isRightToLeft(const std::string &language)
{
	return language == "ar";
}

std::vector<std::string> fontFilesFor(const std::string &language)
{
	std::vector<const FontRole *> roles = { &NOTO_SANS, &NOTO_SANS_BOLD, &EXO2_BOLD, &OSWALD_BOLD };
	if (language == "ar")
		roles = { &ARABIC_BODY, &ARABIC_HEADING };
	else if (language == "ko")
		roles = { &KOREAN_BODY, &KOREAN_HEADING };
	else if (language == "zh")
		roles = { &CHINESE_BODY, &CHINESE_HEADING };
	std::vector<std::string> files;
	for (const FontRole *role : roles)
		files.push_back(role->files[0]);
	return files;
}

void loadFonts(const FontSetup &setup)
{
	if (!s_engine)
		return;

	const std::vector<std::string> &dirs = setup.fontDirs;
	const std::vector<std::string> systemDirs = { setup.windowsFontsDir };
	const std::string &language = setup.language;

	// Noto Sans first, as a fallback face too: Latin, Cyrillic and Greek that a language's own face
	// lacks (the Arabic faces have no Latin) then match the body text rather than Arial.
	const bool notoSans = loadRole(NOTO_SANS, dirs, true);
	const bool notoSansBold = loadRole(NOTO_SANS_BOLD, dirs, false);

	// SIL OFL-licensed Barlow (see Data/UI/Fonts/OFL.txt), the bundled default UI font.
	// LoadFontFace reads family/style/weight straight from the font, so both weights
	// register under the "Barlow" family; common.rcss picks weight via font-weight.
	Rml::LoadFontFace("UI/Fonts/Barlow-Regular.ttf", true);
	Rml::LoadFontFace("UI/Fonts/Barlow-Bold.ttf");

	// The language's own faces: body text at normal weight, headings (bold) in the heading face.
	if (language == "ar")
	{
		const bool body = loadRole(ARABIC_BODY, dirs, false);
		const bool heading = loadRole(ARABIC_HEADING, dirs, false);
		// Without the pack the Arabic comes from Arial's own Arabic (a fallback face below).
		substitute(body ? ARABIC_BODY.family : nullptr, REGULAR, heading ? ARABIC_HEADING.family : nullptr, BOLD);
	}
	else if (language == "ko" || language == "zh")
	{
		const bool korean = language == "ko";
		const FontRole &body = korean ? KOREAN_BODY : CHINESE_BODY;
		const FontRole &heading = korean ? KOREAN_HEADING : CHINESE_HEADING;
		if (loadRole(body, dirs, false))
		{
			const bool black = loadRole(heading, dirs, false);
			substitute(body.family, REGULAR, body.family, black ? BLACK : BOLD);
		}
		else
		{
			// The font pack is missing: Malgun Gothic / Microsoft JhengHei from Windows.
			const FontRole &system = korean ? MALGUN : JHENGHEI;
			const FontRole &systemBold = korean ? MALGUN_BOLD : JHENGHEI_BOLD;
			if (loadRole(system, systemDirs, false))
			{
				const bool bold = loadRole(systemBold, systemDirs, false);
				substitute(system.family, REGULAR, system.family, bold ? BOLD : REGULAR);
				Rml::Log::Message(Rml::Log::LT_INFO, "No %s font pack; using %s.", language.c_str(), system.family);
			}
		}
	}
	else
	{
		// Latin and Cyrillic: Noto Sans body text under the chosen heading face. Barlow has no
		// Cyrillic, so Russian headings with the Barlow pairing use Noto Sans Bold.
		const char *headingFamily = nullptr;
		if (setup.heading == HEADING_EXO2 && loadRole(EXO2_BOLD, dirs, false))
			headingFamily = EXO2_BOLD.family;
		else if (setup.heading == HEADING_OSWALD && loadRole(OSWALD_BOLD, dirs, false))
			headingFamily = OSWALD_BOLD.family;
		else if (language == "ru" && notoSansBold)
			headingFamily = NOTO_SANS_BOLD.family;
		substitute(notoSans ? NOTO_SANS.family : nullptr, REGULAR, headingFamily, BOLD);
	}

	// SIL OFL-licensed Noto Color Emoji (see Data/UI/Fonts/OFL-NotoColorEmoji.txt), a subset of about
	// 1,400 common emoji (CBDT colour bitmaps), is the last fallback face so chat can show them.
	// The blank face maps the invisible parts of emoji sequences (flag, skin tone, ZWJ family) to a
	// zero-width glyph and has to come before Arial, whose own ZWJ glyph is visible.
	Rml::LoadFontFace("UI/Fonts/NotoColorEmoji-Blank.ttf", true);

	// Arial, if present in the Windows fonts folder, is loaded only as a fallback face (not
	// bundled) so glyphs the faces above lack -- other scripts in player names, Arabic without its
	// font pack -- still render. Missing files are not an error.
	if (!setup.windowsFontsDir.empty())
	{
		const bool regularOk = fileExists(setup.windowsFontsDir + "arial.ttf") && Rml::LoadFontFace(setup.windowsFontsDir + "arial.ttf", true);
		const bool boldOk = fileExists(setup.windowsFontsDir + "arialbd.ttf") && Rml::LoadFontFace(setup.windowsFontsDir + "arialbd.ttf", true);
		if (!regularOk && !boldOk)
			Rml::Log::Message(Rml::Log::LT_INFO, "Arial not found in the Windows fonts folder; falling back to the bundled faces only.");
	}

	Rml::LoadFontFace("UI/Fonts/NotoColorEmoji-Subset.ttf", true);

	// Segoe UI Symbol from the Windows fonts folder (not bundled), for the symbols players put in
	// their names (U+26CA and the like) that none of the faces above have. It also has monochrome
	// emoji, so it comes after the colour subset; a missing file is skipped.
	if (!setup.windowsFontsDir.empty() && fileExists(setup.windowsFontsDir + "seguisym.ttf"))
		Rml::LoadFontFace(setup.windowsFontsDir + "seguisym.ttf", true);
}

void tagDocuments(const std::string &language)
{
	s_documentLanguage.lang = bcp47(language);
	s_documentLanguage.rightToLeft = isRightToLeft(language);
	if (!s_pluginRegistered)
	{
		Rml::RegisterPlugin(&s_documentLanguage);
		s_pluginRegistered = true;
	}
}

} // namespace RmlTextShaping
