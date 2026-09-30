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
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Core/SystemInterface.h>

#include <cstdlib>
#include <cstring>
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

//-------------------------------------------------------------------------------------------------
// Style sheet mirroring (mirrorStyleSheet()).

std::string trim(const std::string &s)
{
	const size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos)
		return std::string();
	const size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

std::string lower(std::string s)
{
	for (char &c : s)
		c = (char)tolower((unsigned char)c);
	return s;
}

// Splits at `separator` outside parentheses and quotes.
std::vector<std::string> splitTopLevel(const std::string &s, char separator)
{
	std::vector<std::string> parts;
	std::string current;
	int depth = 0;
	char quote = 0;
	for (char c : s)
	{
		if (quote)
		{
			if (c == quote)
				quote = 0;
		}
		else if (c == '"' || c == '\'')
			quote = c;
		else if (c == '(')
			++depth;
		else if (c == ')')
			--depth;
		else if (depth == 0 && (separator == ' ' ? (c == ' ' || c == '\t' || c == '\n' || c == '\r') : c == separator))
		{
			if (separator != ' ' || !current.empty())
				parts.push_back(current);
			current.clear();
			continue;
		}
		current.push_back(c);
	}
	if (!current.empty() || separator != ' ')
		parts.push_back(current);
	return parts;
}

std::string join(const std::vector<std::string> &parts, const char *separator)
{
	std::string out;
	for (size_t i = 0; i < parts.size(); ++i)
	{
		if (i)
			out += separator;
		out += parts[i];
	}
	return out;
}

std::string swapWords(const std::string &value, const char *a, const char *b)
{
	std::vector<std::string> words = splitTopLevel(value, ' ');
	for (std::string &word : words)
	{
		const std::string w = lower(word);
		if (w == a)
			word = b;
		else if (w == b)
			word = a;
	}
	return join(words, " ");
}

std::string mirrorProperty(const std::string &property)
{
	static const char *const pairs[][2] = {
		{ "left", "right" },
		{ "margin-left", "margin-right" },
		{ "padding-left", "padding-right" },
		{ "border-left", "border-right" },
		{ "border-left-width", "border-right-width" },
		{ "border-left-color", "border-right-color" },
		{ "border-top-left-radius", "border-top-right-radius" },
		{ "border-bottom-left-radius", "border-bottom-right-radius" },
	};
	for (const auto &pair : pairs)
	{
		if (property == pair[0])
			return pair[1];
		if (property == pair[1])
			return pair[0];
	}
	return property;
}

std::string mirrorGradients(std::string value)
{
	// horizontal-gradient(from to) runs left to right: swap its colours.
	for (size_t at = 0; (at = lower(value).find("horizontal-gradient(", at)) != std::string::npos;)
	{
		const size_t open = at + strlen("horizontal-gradient(");
		const size_t close = value.find(')', open);
		if (close == std::string::npos)
			break;
		std::vector<std::string> colours = splitTopLevel(value.substr(open, close - open), ' ');
		if (colours.size() == 2)
			value.replace(open, close - open, colours[1] + " " + colours[0]);
		at = close;
	}
	// linear-gradient(to left/right ...) and angles.
	for (size_t at = 0; (at = lower(value).find("linear-gradient(", at)) != std::string::npos;)
	{
		const size_t open = at + strlen("linear-gradient(");
		std::vector<std::string> args = splitTopLevel(value.substr(open), ',');
		if (args.empty())
			break;
		std::string first = trim(args[0]);
		const std::string firstLower = lower(first);
		std::string mirrored = first;
		if (firstLower.rfind("to ", 0) == 0)
			mirrored = swapWords(first, "left", "right");
		else if (firstLower.size() > 3 && firstLower.compare(firstLower.size() - 3, 3, "deg") == 0)
			mirrored = std::to_string((360 - atoi(first.c_str()) % 360) % 360) + "deg";
		const size_t firstPos = value.find(args[0], open);
		if (firstPos != std::string::npos)
			value.replace(firstPos, args[0].size(), std::string(args[0].size() - trim(args[0]).size() ? " " : "") + mirrored);
		at = open;
	}
	return value;
}

std::string mirrorValue(const std::string &property, const std::string &value)
{
	if (property == "text-align" || property == "float" || property == "clear")
		return swapWords(value, "left", "right");
	if (property == "flex-direction")
	{
		const std::string v = lower(trim(value));
		if (v == "row")
			return "row-reverse";
		if (v == "row-reverse")
			return "row";
		return value;
	}
	if (property == "margin" || property == "padding" || property == "border-width" || property == "border-color")
	{
		std::vector<std::string> sides = splitTopLevel(value, ' ');
		if (sides.size() == 4)
			return sides[0] + " " + sides[3] + " " + sides[2] + " " + sides[1];
		return value;
	}
	if (property == "border-radius")
	{
		std::vector<std::string> corners = splitTopLevel(value, ' ');
		if (corners.size() == 4)
			return corners[1] + " " + corners[0] + " " + corners[3] + " " + corners[2];
		if (corners.size() == 3)
			return corners[1] + " " + corners[0] + " " + corners[1] + " " + corners[2];
		return value;
	}
	if (property == "decorator" || property == "background")
		return mirrorGradients(value);
	return value;
}

std::string mirrorDeclarations(const std::string &body)
{
	std::vector<std::pair<std::string, std::string>> declarations;
	bool placed = false;
	bool leftOrRight = false;
	for (const std::string &declaration : splitTopLevel(body, ';'))
	{
		const std::string d = trim(declaration);
		const size_t colon = d.find(':');
		if (d.empty() || colon == std::string::npos)
			continue;
		declarations.push_back({ lower(trim(d.substr(0, colon))), trim(d.substr(colon + 1)) });
		const std::string &property = declarations.back().first;
		const std::string value = lower(declarations.back().second);
		placed = placed || (property == "position" && (value == "absolute" || value == "fixed"));
		leftOrRight = leftOrRight || property == "left" || property == "right";
	}

	// An absolutely placed box whose rule sets no left or right sits where its inline style (a map
	// marker, a pin) or its static position puts it, and neither is mirrored: its horizontal
	// margins centre it on that spot, so they stay as they are.
	const bool physicalMargins = placed && !leftOrRight;

	std::string out;
	for (const auto &declaration : declarations)
	{
		const std::string &property = declaration.first;
		const std::string &value = declaration.second;
		const bool margin = property == "margin" || property == "margin-left" || property == "margin-right";
		if (physicalMargins && margin)
			out += "\n\t" + property + ": " + value + ";";
		else
			out += "\n\t" + mirrorProperty(property) + ": " + mirrorValue(property, value) + ";";
	}
	return out + "\n";
}

// Index just past the '}' matching the '{' at `open`, skipping comments and strings.
size_t matchingBrace(const std::string &s, size_t open)
{
	int depth = 0;
	for (size_t i = open; i < s.size(); ++i)
	{
		const char c = s[i];
		if (c == '/' && i + 1 < s.size() && s[i + 1] == '*')
		{
			const size_t end = s.find("*/", i + 2);
			if (end == std::string::npos)
				return s.size();
			i = end + 1;
		}
		else if (c == '"' || c == '\'')
		{
			const size_t end = s.find(c, i + 1);
			if (end == std::string::npos)
				return s.size();
			i = end;
		}
		else if (c == '{')
			++depth;
		else if (c == '}' && --depth == 0)
			return i + 1;
	}
	return s.size();
}

std::string stripComments(const std::string &s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*')
		{
			const size_t end = s.find("*/", i + 2);
			if (end == std::string::npos)
				break;
			i = end + 1;
			continue;
		}
		out.push_back(s[i]);
	}
	return out;
}

//-------------------------------------------------------------------------------------------------
// Documents.

// The style sheets a document links and embeds, in order, read from its RML (as RmlUi's
// DocumentHeader collects them); inline sheets get the document's path.
struct SheetSource
{
	std::string path;
	std::string text;
};

bool readText(const std::string &path, std::string &out)
{
	Rml::FileInterface *files = Rml::GetFileInterface();
	Rml::FileHandle handle = files ? files->Open(path) : 0;
	if (!handle)
		return false;
	out.resize(files->Length(handle));
	const size_t read = out.empty() ? 0 : files->Read(&out[0], out.size(), handle);
	files->Close(handle);
	out.resize(read);
	return true;
}

std::vector<SheetSource> documentSheets(const std::string &documentPath)
{
	std::vector<SheetSource> sheets;
	std::string rml;
	if (!readText(documentPath, rml))
		return sheets;
	const size_t headEnd = rml.find("</head>");
	const std::string head = rml.substr(0, headEnd == std::string::npos ? rml.size() : headEnd);

	for (size_t at = 0; at < head.size();)
	{
		const size_t link = head.find("<link", at);
		const size_t style = head.find("<style", at);
		if (link == std::string::npos && style == std::string::npos)
			break;
		if (link != std::string::npos && (style == std::string::npos || link < style))
		{
			const size_t end = head.find('>', link);
			const std::string tag = head.substr(link, end - link);
			at = end == std::string::npos ? head.size() : end;
			if (tag.find("rcss") == std::string::npos)
				continue;
			const size_t href = tag.find("href=\"");
			if (href == std::string::npos)
				continue;
			const size_t hrefEnd = tag.find('"', href + 6);
			Rml::String path;
			Rml::GetSystemInterface()->JoinPath(path, documentPath, tag.substr(href + 6, hrefEnd - href - 6));
			SheetSource sheet;
			sheet.path = path;
			if (readText(path, sheet.text))
				sheets.push_back(sheet);
		}
		else
		{
			const size_t open = head.find('>', style);
			const size_t close = head.find("</style>", open);
			if (open == std::string::npos || close == std::string::npos)
				break;
			sheets.push_back(SheetSource{ documentPath, head.substr(open + 1, close - open - 1) });
			at = close + 8;
		}
	}
	return sheets;
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
		{
			document->SetAttribute("dir", "rtl");
			mirror(document);
		}
	}

private:
	// Right-to-left documents get their style sheets replaced by mirrored ones (mirrorStyleSheet()).
	void mirror(Rml::ElementDocument *document)
	{
		Rml::SharedPtr<Rml::StyleSheetContainer> combined;
		for (const SheetSource &sheet : documentSheets(document->GetSourceURL()))
		{
			auto container = Rml::MakeShared<Rml::StyleSheetContainer>();
			const std::string mirrored = mirrorStyleSheet(sheet.text);
			Rml::StreamMemory stream((const Rml::byte *)mirrored.data(), mirrored.size());
			stream.SetSourceURL(sheet.path);
			if (!container->LoadStyleSheetContainer(&stream))
				continue;
			if (combined)
				combined->MergeStyleSheetContainer(*container);
			else
				combined = container;
		}
		if (combined)
			document->SetStyleSheetContainer(combined);
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

	// SIL OFL-licensed Barlow (see Data/UI/Fonts/OFL.txt), the family the style sheets name. The UI
	// draws it as the faces substituted below; it stays loaded as a fallback and for mods' sheets.
	// LoadFontFace reads family/style/weight straight from the font, so both weights register under
	// the "Barlow" family.
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
		// Latin and Cyrillic: Noto Sans body text under Exo 2 headings (SIL OFL, see
		// Data/UI/Fonts/OFL-NotoSans.txt and OFL-Exo2.txt). Barlow has no Cyrillic, so Russian
		// headings with the Barlow pairing use Noto Sans Bold.
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

std::string mirrorStyleSheet(const std::string &rcss)
{
	const std::string s = stripComments(rcss);
	std::string out;
	out.reserve(s.size() + s.size() / 4);
	size_t i = 0;
	while (i < s.size())
	{
		const size_t open = s.find('{', i);
		if (open == std::string::npos)
		{
			out += s.substr(i);
			break;
		}
		const std::string head = s.substr(i, open - i);
		const size_t close = matchingBrace(s, open);
		const std::string body = s.substr(open + 1, close > open + 1 ? close - open - 2 : 0);
		const std::string selector = lower(trim(head));

		if (!selector.empty() && selector[0] == '@')
		{
			if (selector.rfind("@media", 0) == 0)
				out += head + "{" + mirrorStyleSheet(body) + "}";
			else
				out += head + "{" + body + "}";
		}
		else if (selector.find("[dir=rtl]") != std::string::npos || selector.find("[dir=\"rtl\"]") != std::string::npos)
		{
			out += head + "{" + body + "}";
		}
		else
		{
			out += head + "{" + mirrorDeclarations(body) + "}";
		}
		i = close;
	}
	return out;
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
