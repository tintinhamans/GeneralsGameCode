// str2csf: standalone converter between Generals/Zero Hour .str string tables
// and compiled .csf files.
//
// This mirrors, byte for byte, the parsing and writing rules implemented by
// the game engine itself in Core/GameEngine/Source/GameClient/GameText.cpp:
//   - parseStringFile / readToEndOfQuote / translateCopy / stripSpaces for
//     reading .str text (str -> csf direction).
//   - parseCSF / getCSFInfo for the binary .csf layout (both directions).
//
// Deliberately has zero engine dependency (no AsciiString/UnicodeString/File):
// plain C++17 and the standard library only, so it builds as a small host
// tool with no engine include paths or libraries.
//
// Usage:
//   str2csf <input.str> <output.csf> --lang <LanguageName>
//   str2csf --csf2str <input.csf> <output.str>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// CSF binary layout (matches GameText.cpp's CSFHeader and the CSF_* chunk ids)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct CsfHeader
{
	int32_t id;
	int32_t version;
	int32_t numLabels;
	int32_t numStrings;
	int32_t skip;
	int32_t langId;
};
#pragma pack(pop)

constexpr int32_t kCsfId = (int32_t)(('C' << 24) | ('S' << 16) | ('F' << 8) | (' '));
constexpr int32_t kCsfLabel = (int32_t)(('L' << 24) | ('B' << 16) | ('L' << 8) | (' '));
constexpr int32_t kCsfString = (int32_t)(('S' << 24) | ('T' << 16) | ('R' << 8) | (' '));
constexpr int32_t kCsfStringWithWave = (int32_t)(('S' << 24) | ('T' << 16) | ('R' << 8) | ('W'));
constexpr int32_t kCsfVersion = 3;

// ---------------------------------------------------------------------------
// Language folder name <-> CSF language id (matches LanguageID in Language.h)
// ---------------------------------------------------------------------------

struct LanguageEntry
{
	const char *name;
	int32_t id;
};

const LanguageEntry kLanguages[] = {
	{ "English", 0 },	// LANGUAGE_ID_US
	{ "UK", 1 },		// LANGUAGE_ID_UK
	{ "German", 2 },	// LANGUAGE_ID_GERMAN
	{ "French", 3 },	// LANGUAGE_ID_FRENCH
	{ "Spanish", 4 },	// LANGUAGE_ID_SPANISH
	{ "Italian", 5 },	// LANGUAGE_ID_ITALIAN
	{ "Japanese", 6 },	// LANGUAGE_ID_JAPANESE
	{ "Jabber", 7 },	// LANGUAGE_ID_JABBER
	{ "Korean", 8 },	// LANGUAGE_ID_KOREAN
};

bool stricmpAscii(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
			return false;
	}
	return true;
}

bool languageIdForName(const std::string &name, int32_t &outId)
{
	for (const auto &entry : kLanguages)
	{
		if (stricmpAscii(name, entry.name))
		{
			outId = entry.id;
			return true;
		}
	}
	return false;
}

std::string languageNameForId(int32_t id)
{
	for (const auto &entry : kLanguages)
	{
		if (entry.id == id)
			return entry.name;
	}
	return "Unknown";
}

// ---------------------------------------------------------------------------
// StringEntry: one LABEL + text + optional wave name
// ---------------------------------------------------------------------------

struct StringEntry
{
	std::string label;
	std::u16string text;	// logical text (already un-inverted, decoded)
	std::string wave;		// empty if none
};

// ---------------------------------------------------------------------------
// .str parsing -- ports GameText.cpp's readLine/removeLeadingAndTrailing/
// readToEndOfQuote/translateCopy/stripSpaces exactly.
// ---------------------------------------------------------------------------

std::string normalizeLineEndings(const std::string &raw)
{
	// The engine opens .str files in text mode; on Windows that transparently
	// folds CRLF to LF at the CRT level before GameText.cpp ever sees a byte,
	// and any stray '\r' left over on other platforms is whitespace that
	// removeLeadingAndTrailing/stripSpaces trim or collapse anyway. Folding
	// CRLF/CR to LF up front reproduces that outcome exactly and keeps the
	// rest of this parser platform-agnostic.
	std::string out;
	out.reserve(raw.size());
	for (size_t i = 0; i < raw.size(); ++i)
	{
		char c = raw[i];
		if (c == '\r')
		{
			out.push_back('\n');
			if (i + 1 < raw.size() && raw[i + 1] == '\n')
				++i;
		}
		else
		{
			out.push_back(c);
		}
	}
	return out;
}

void trimAscii(std::string &s)
{
	size_t start = 0;
	while (start < s.size() && std::isspace((unsigned char)s[start]))
		++start;
	size_t end = s.size();
	while (end > start && std::isspace((unsigned char)s[end - 1]))
		--end;
	s = s.substr(start, end - start);
}

// Cursor over the whole normalized document, used both for line-by-line
// scanning and for the raw continuation reads inside a multi-line quote.
struct DocReader
{
	const std::string &data;
	size_t pos = 0;

	int nextChar()
	{
		if (pos >= data.size())
			return -1;
		return (unsigned char)data[pos++];
	}
};

// Mirrors GameTextManager::readLine: consumes bytes up to and including '\n'
// (or EOF), returns false only when nothing at all could be read.
bool readLine(DocReader &r, std::string &out)
{
	out.clear();
	bool any = false;
	while (r.pos < r.data.size())
	{
		any = true;
		char c = r.data[r.pos++];
		if (c == '\n')
			break;
		out.push_back(c);
	}
	return any;
}

// Mirrors GameTextManager::readToEndOfQuote. `firstPart` is the already-
// trimmed remainder of the opening line (after the opening quote, with a
// synthetic trailing '\n' the way GameText.cpp appends one); once exhausted,
// further characters come from the raw document via `r` for multi-line quotes.
std::string readToEndOfQuote(DocReader &r, const std::string &firstPart, std::string &outWave)
{
	std::string out;
	bool slash = false;
	size_t ip = 0;
	bool usingFirst = true;

	auto nextCh = [&]() -> int {
		if (usingFirst)
		{
			if (ip < firstPart.size())
				return (unsigned char)firstPart[ip++];
			usingFirst = false;
		}
		return r.nextChar();
	};

	for (;;)
	{
		int ci = nextCh();
		if (ci < 0)
			throw std::runtime_error("unterminated quoted string (missing closing '\"')");

		char ch = (char)ci;
		if (ch == '\n')
		{
			slash = false;
			ch = ' ';
		}
		else if (ch == '\\' && !slash)
		{
			slash = true;
		}
		else if (ch == '\\' && slash)
		{
			slash = false;
		}
		else if (ch == '"' && !slash)
		{
			break;
		}
		else
		{
			slash = false;
		}

		if (std::isspace((unsigned char)ch))
			ch = ' ';

		out.push_back(ch);
	}

	// Wave name state machine (readToEndOfQuote's second loop).
	outWave.clear();
	int state = 0;
	for (;;)
	{
		int ci = nextCh();
		if (ci < 0)
			break;
		char ch = (char)ci;
		if (ch == '\n')
			break;

		if (state == 0)
		{
			if (std::isspace((unsigned char)ch) || ch == '=')
				continue;
			state = 1;
		}
		if (state == 1)
		{
			if (std::isalnum((unsigned char)ch) || ch == '_')
			{
				outWave.push_back(ch);
				continue;
			}
			state = 2;
		}
		// state 2: consume and discard until '\n'/EOF.
	}

	if (!outWave.empty() && std::isdigit((unsigned char)outWave.back()))
		outWave.push_back('e');

	return out;
}

// Mirrors GameTextManager::translateCopy: resolves backslash escapes,
// otherwise promotes each raw byte directly to a UTF-16 code unit (the
// engine does not UTF-8-decode .str text -- each byte is its own WideChar).
std::u16string translateCopy(const std::string &in)
{
	std::u16string out;
	bool slash = false;
	for (unsigned char c : in)
	{
		if (slash)
		{
			slash = false;
			switch (c)
			{
				case '\\': out.push_back(u'\\'); break;
				case '\'': out.push_back(u'\''); break;
				case '"': out.push_back(u'"'); break;
				case '?': out.push_back(u'?'); break;
				case 't': out.push_back(u'\t'); break;
				case 'n': out.push_back(u'\n'); break;
				default: out.push_back((char16_t)c); break;
			}
		}
		else if (c != '\\')
		{
			out.push_back((char16_t)c);
		}
		else
		{
			slash = true;
		}
	}
	return out;
}

// Mirrors GameTextManager::stripSpaces.
std::u16string stripSpaces(const std::u16string &s)
{
	std::u16string out;
	char16_t last = 0;
	bool skipAll = true;
	for (char16_t ch : s)
	{
		if (ch == u' ')
		{
			if (last == u' ' || skipAll)
				continue;
		}

		if (ch == u'\n' || ch == u'\t')
		{
			if (last == u' ' && !out.empty())
				out.pop_back();
			skipAll = true;
			last = ch;
			out.push_back(ch);
			continue;
		}

		last = ch;
		out.push_back(ch);
		skipAll = false;
	}
	if (last == u' ' && !out.empty())
		out.pop_back();
	return out;
}

bool isCommentOrBlank(const std::string &trimmedLine)
{
	if (trimmedLine.empty())
		return true;
	return trimmedLine.size() >= 2 && trimmedLine[0] == '/' && trimmedLine[1] == '/';
}

// Mirrors GameTextManager::parseStringFile.
std::vector<StringEntry> parseStrFile(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		throw std::runtime_error("cannot open input .str file: " + path);
	std::ostringstream ss;
	ss << in.rdbuf();
	std::string doc = normalizeLineEndings(ss.str());

	DocReader r{ doc };
	std::vector<StringEntry> entries;
	std::string line;

	while (readLine(r, line))
	{
		trimAscii(line);
		if (isCommentOrBlank(line))
			continue;

		StringEntry entry;
		entry.label = line;

		for (const auto &existing : entries)
		{
			if (stricmpAscii(existing.label, entry.label))
			{
				std::cerr << "warning: string label '" << entry.label << "' multiply defined\n";
				break;
			}
		}

		bool readString = false;
		bool sawEnd = false;
		while (readLine(r, line))
		{
			trimAscii(line);
			if (!line.empty() && line[0] == '"')
			{
				std::string firstPart = line.substr(1) + "\n";
				std::string wave;
				std::string raw = readToEndOfQuote(r, firstPart, wave);

				if (readString)
				{
					std::cerr << "warning: string label '" << entry.label << "' has more than one string defined\n";
				}
				else
				{
					entry.text = stripSpaces(translateCopy(raw));
					entry.wave = wave;
					readString = true;
				}
			}
			else if (stricmpAscii(line, "END"))
			{
				sawEnd = true;
				break;
			}
		}

		if (!sawEnd)
			throw std::runtime_error("unexpected end of string file (missing END for label '" + entry.label + "')");

		entries.push_back(std::move(entry));
	}

	return entries;
}

// ---------------------------------------------------------------------------
// CSF binary read/write -- ports GameText.cpp's parseCSF/getCSFInfo and the
// inverse writer.
// ---------------------------------------------------------------------------

void writeCsf(const std::string &path, const std::vector<StringEntry> &entries, int32_t langId)
{
	std::ofstream f(path, std::ios::binary);
	if (!f)
		throw std::runtime_error("cannot open output .csf file: " + path);

	CsfHeader hdr{};
	hdr.id = kCsfId;
	hdr.version = kCsfVersion;
	hdr.numLabels = (int32_t)entries.size();
	hdr.numStrings = (int32_t)entries.size();	// one STR/STRW per label
	hdr.skip = 0;
	hdr.langId = langId;
	f.write((const char *)&hdr, sizeof(hdr));

	for (const auto &e : entries)
	{
		int32_t id = kCsfLabel;
		f.write((const char *)&id, 4);
		int32_t numStrings = 1;
		f.write((const char *)&numStrings, 4);
		int32_t labelLen = (int32_t)e.label.size();
		f.write((const char *)&labelLen, 4);
		if (labelLen)
			f.write(e.label.data(), labelLen);

		int32_t strId = e.wave.empty() ? kCsfString : kCsfStringWithWave;
		f.write((const char *)&strId, 4);
		int32_t textLen = (int32_t)e.text.size();
		f.write((const char *)&textLen, 4);
		for (char16_t c : e.text)
		{
			uint16_t inverted = (uint16_t)(~(uint16_t)c);
			f.write((const char *)&inverted, 2);
		}

		if (!e.wave.empty())
		{
			int32_t waveLen = (int32_t)e.wave.size();
			f.write((const char *)&waveLen, 4);
			f.write(e.wave.data(), waveLen);
		}
	}

	if (!f)
		throw std::runtime_error("write failed for .csf file: " + path);
}

struct CsfDoc
{
	int32_t langId = 0;
	std::vector<StringEntry> entries;
};

void readExact(std::ifstream &f, void *dst, size_t n, const char *what)
{
	f.read((char *)dst, (std::streamsize)n);
	if (!f || (size_t)f.gcount() != n)
		throw std::runtime_error(std::string("malformed CSF: truncated reading ") + what);
}

CsfDoc readCsf(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		throw std::runtime_error("cannot open input .csf file: " + path);

	CsfHeader hdr{};
	readExact(f, &hdr, sizeof(hdr), "header");
	if (hdr.id != kCsfId)
		throw std::runtime_error("not a valid CSF file (bad id): " + path);

	CsfDoc doc;
	doc.langId = (hdr.version >= 2) ? hdr.langId : 0;

	for (int32_t i = 0; i < hdr.numLabels; ++i)
	{
		int32_t id;
		readExact(f, &id, 4, "label chunk id");
		if (id != kCsfLabel)
			throw std::runtime_error("malformed CSF: expected LBL chunk");

		int32_t numStrings;
		readExact(f, &numStrings, 4, "label string count");
		int32_t labelLen;
		readExact(f, &labelLen, 4, "label length");
		std::string label(labelLen, '\0');
		if (labelLen)
			readExact(f, &label[0], labelLen, "label text");

		StringEntry entry;
		entry.label = label;

		for (int32_t n = 0; n < numStrings; ++n)
		{
			int32_t sid;
			readExact(f, &sid, 4, "string chunk id");
			if (sid != kCsfString && sid != kCsfStringWithWave)
				throw std::runtime_error("malformed CSF: expected STR/STRW chunk");

			int32_t textLen;
			readExact(f, &textLen, 4, "string length");
			std::u16string text(textLen, u'\0');
			for (int32_t k = 0; k < textLen; ++k)
			{
				uint16_t raw;
				readExact(f, &raw, 2, "string text");
				text[k] = (char16_t)(~raw);
			}

			std::string wave;
			if (sid == kCsfStringWithWave)
			{
				int32_t waveLen;
				readExact(f, &waveLen, 4, "wave length");
				wave.resize(waveLen);
				if (waveLen)
					readExact(f, &wave[0], waveLen, "wave text");
			}

			if (n == 0)
			{
				// Matches parseCSF: only the first string per label is used,
				// and it is re-run through stripSpaces just like a freshly
				// decoded .str string would be.
				entry.text = stripSpaces(text);
				entry.wave = wave;
			}
		}

		doc.entries.push_back(std::move(entry));
	}

	return doc;
}

// ---------------------------------------------------------------------------
// .str writing (--csf2str direction) -- best-effort inverse of the above for
// tooling/round-trip inspection. Not exercised by the game engine.
// ---------------------------------------------------------------------------

void appendUtf8(std::string &out, char32_t cp)
{
	if (cp <= 0x7F)
	{
		out.push_back((char)cp);
	}
	else if (cp <= 0x7FF)
	{
		out.push_back((char)(0xC0 | (cp >> 6)));
		out.push_back((char)(0x80 | (cp & 0x3F)));
	}
	else if (cp <= 0xFFFF)
	{
		out.push_back((char)(0xE0 | (cp >> 12)));
		out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back((char)(0x80 | (cp & 0x3F)));
	}
	else
	{
		out.push_back((char)(0xF0 | (cp >> 18)));
		out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back((char)(0x80 | (cp & 0x3F)));
	}
}

std::string escapeForStr(const std::u16string &text)
{
	std::string out;
	for (size_t i = 0; i < text.size(); ++i)
	{
		char16_t c = text[i];
		switch (c)
		{
			case u'\\': out += "\\\\"; break;
			case u'"': out += "\\\""; break;
			case u'\n': out += "\\n"; break;
			case u'\t': out += "\\t"; break;
			default:
				if (c <= 0xFF)
				{
					// Byte values 0x80-0xFF here are the raw bytes the forward
					// direction promotes 1:1 into WideChar; only values above
					// 0xFF (true Unicode text from a non-str2csf CSF) need
					// UTF-8 encoding, and won't round-trip through the .str
					// byte-per-char format -- this path is for inspection only.
					out.push_back((char)c);
				}
				else if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() &&
						 text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
				{
					char32_t hi = c - 0xD800, lo = text[i + 1] - 0xDC00;
					appendUtf8(out, 0x10000 + (hi << 10) + lo);
					++i;
				}
				else
				{
					appendUtf8(out, c);
				}
				break;
		}
	}
	return out;
}

void writeStrFile(const std::string &path, const CsfDoc &doc, const std::string &sourceName)
{
	std::ofstream f(path, std::ios::binary);
	if (!f)
		throw std::runtime_error("cannot open output .str file: " + path);

	f << "// Generated by str2csf --csf2str from " << sourceName << ".\n";
	f << "// Language id " << doc.langId << " (" << languageNameForId(doc.langId) << ").\n";
	f << "// Tooling/inspection output; not guaranteed byte-identical if recompiled.\n\n";

	for (const auto &e : doc.entries)
	{
		f << e.label << "\n";
		f << "  \"" << escapeForStr(e.text) << "\"";
		if (!e.wave.empty())
			f << " " << e.wave;
		f << "\n";
		f << "END\n\n";
	}

	if (!f)
		throw std::runtime_error("write failed for .str file: " + path);
}

int usage(const char *argv0)
{
	std::cerr << "usage:\n"
			  << "  " << argv0 << " <input.str> <output.csf> --lang <LanguageName>\n"
			  << "  " << argv0 << " --csf2str <input.csf> <output.str>\n";
	return 2;
}

} // namespace

int main(int argc, char **argv)
{
	std::vector<std::string> args(argv + 1, argv + argc);

	try
	{
		if (!args.empty() && args[0] == "--csf2str")
		{
			if (args.size() != 3)
				return usage(argv[0]);
			CsfDoc doc = readCsf(args[1]);
			writeStrFile(args[2], doc, args[1]);
			std::cout << "str2csf: wrote " << doc.entries.size() << " label(s) to " << args[2] << "\n";
			return 0;
		}

		if (args.size() != 4 || args[2] != "--lang")
			return usage(argv[0]);

		int32_t langId;
		if (!languageIdForName(args[3], langId))
		{
			std::cerr << "error: unknown language '" << args[3] << "'\n";
			return 1;
		}

		std::vector<StringEntry> entries = parseStrFile(args[0]);
		writeCsf(args[1], entries, langId);
		std::cout << "str2csf: wrote " << entries.size() << " label(s) to " << args[1]
				  << " (lang=" << args[3] << ", id=" << langId << ")\n";
		return 0;
	}
	catch (const std::exception &ex)
	{
		std::cerr << "error: " << ex.what() << "\n";
		return 1;
	}
}
