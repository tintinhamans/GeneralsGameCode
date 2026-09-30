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

// No PreRTS.h: this file uses the standard library only, so the RmlUi render harness and tools
// can compile it directly (the game's precompiled header is still forced in by CMake).
#include "Common/RtlText.h"

#include <algorithm>
#include <vector>

namespace RtlText
{

namespace
{

struct BidiRange
{
	unsigned short first;
	unsigned short last;
	BidiType type;
};

struct PresentationForm
{
	unsigned short codepoint;
	unsigned short poolOffset;
	unsigned char length;
};

struct ArabicForms
{
	unsigned short base;
	unsigned short isolated;
	unsigned short final;
	unsigned short initial;
	unsigned short medial;
};

// Generated from Unicode 15.0.0 bidi classes (UnicodeData.txt), BMP only: every range not listed is L.
static const BidiRange kBidiRanges[] = {
	{0x0000,0x0008,BIDI_NSM},{0x0009,0x000D,BIDI_N},{0x000E,0x001B,BIDI_NSM},{0x001C,0x0022,BIDI_N},
	{0x0023,0x0025,BIDI_ET},{0x0026,0x002A,BIDI_N},{0x002B,0x002B,BIDI_ES},{0x002C,0x002C,BIDI_CS},{0x002D,0x002D,BIDI_ES},
	{0x002E,0x002F,BIDI_CS},{0x0030,0x0039,BIDI_EN},{0x003A,0x003A,BIDI_CS},{0x003B,0x0040,BIDI_N},{0x005B,0x0060,BIDI_N},
	{0x007B,0x007E,BIDI_N},{0x007F,0x0084,BIDI_NSM},{0x0085,0x0085,BIDI_N},{0x0086,0x009F,BIDI_NSM},
	{0x00A0,0x00A0,BIDI_CS},{0x00A1,0x00A1,BIDI_N},{0x00A2,0x00A5,BIDI_ET},{0x00A6,0x00A9,BIDI_N},{0x00AB,0x00AC,BIDI_N},
	{0x00AD,0x00AD,BIDI_NSM},{0x00AE,0x00AF,BIDI_N},{0x00B0,0x00B1,BIDI_ET},{0x00B2,0x00B3,BIDI_EN},{0x00B4,0x00B4,BIDI_N},
	{0x00B6,0x00B8,BIDI_N},{0x00B9,0x00B9,BIDI_EN},{0x00BB,0x00BF,BIDI_N},{0x00D7,0x00D7,BIDI_N},{0x00F7,0x00F7,BIDI_N},
	{0x02B9,0x02BA,BIDI_N},{0x02C2,0x02CF,BIDI_N},{0x02D2,0x02DF,BIDI_N},{0x02E5,0x02ED,BIDI_N},{0x02EF,0x02FF,BIDI_N},
	{0x0300,0x036F,BIDI_NSM},{0x0374,0x0375,BIDI_N},{0x037E,0x037E,BIDI_N},{0x0384,0x0385,BIDI_N},{0x0387,0x0387,BIDI_N},
	{0x03F6,0x03F6,BIDI_N},{0x0483,0x0489,BIDI_NSM},{0x058A,0x058A,BIDI_N},{0x058D,0x058E,BIDI_N},{0x058F,0x058F,BIDI_ET},
	{0x0590,0x0590,BIDI_R},{0x0591,0x05BD,BIDI_NSM},{0x05BE,0x05BE,BIDI_R},{0x05BF,0x05BF,BIDI_NSM},{0x05C0,0x05C0,BIDI_R},
	{0x05C1,0x05C2,BIDI_NSM},{0x05C3,0x05C3,BIDI_R},{0x05C4,0x05C5,BIDI_NSM},{0x05C6,0x05C6,BIDI_R},
	{0x05C7,0x05C7,BIDI_NSM},{0x05C8,0x05FF,BIDI_R},{0x0600,0x0605,BIDI_EN},{0x0606,0x0607,BIDI_N},{0x0608,0x0608,BIDI_R},
	{0x0609,0x060A,BIDI_ET},{0x060B,0x060B,BIDI_R},{0x060C,0x060C,BIDI_CS},{0x060D,0x060D,BIDI_R},{0x060E,0x060F,BIDI_N},
	{0x0610,0x061A,BIDI_NSM},{0x061B,0x064A,BIDI_R},{0x064B,0x065F,BIDI_NSM},{0x0660,0x0669,BIDI_EN},
	{0x066A,0x066A,BIDI_ET},{0x066B,0x066C,BIDI_EN},{0x066D,0x066F,BIDI_R},{0x0670,0x0670,BIDI_NSM},{0x0671,0x06D5,BIDI_R},
	{0x06D6,0x06DC,BIDI_NSM},{0x06DD,0x06DD,BIDI_EN},{0x06DE,0x06DE,BIDI_N},{0x06DF,0x06E4,BIDI_NSM},
	{0x06E5,0x06E6,BIDI_R},{0x06E7,0x06E8,BIDI_NSM},{0x06E9,0x06E9,BIDI_N},{0x06EA,0x06ED,BIDI_NSM},{0x06EE,0x06EF,BIDI_R},
	{0x06F0,0x06F9,BIDI_EN},{0x06FA,0x0710,BIDI_R},{0x0711,0x0711,BIDI_NSM},{0x0712,0x072F,BIDI_R},
	{0x0730,0x074A,BIDI_NSM},{0x074B,0x07A5,BIDI_R},{0x07A6,0x07B0,BIDI_NSM},{0x07B1,0x07EA,BIDI_R},
	{0x07EB,0x07F3,BIDI_NSM},{0x07F4,0x07F5,BIDI_R},{0x07F6,0x07F9,BIDI_N},{0x07FA,0x07FC,BIDI_R},{0x07FD,0x07FD,BIDI_NSM},
	{0x07FE,0x0815,BIDI_R},{0x0816,0x0819,BIDI_NSM},{0x081A,0x081A,BIDI_R},{0x081B,0x0823,BIDI_NSM},{0x0824,0x0824,BIDI_R},
	{0x0825,0x0827,BIDI_NSM},{0x0828,0x0828,BIDI_R},{0x0829,0x082D,BIDI_NSM},{0x082E,0x0858,BIDI_R},
	{0x0859,0x085B,BIDI_NSM},{0x085C,0x088F,BIDI_R},{0x0890,0x0891,BIDI_EN},{0x0892,0x0897,BIDI_R},
	{0x0898,0x089F,BIDI_NSM},{0x08A0,0x08C9,BIDI_R},{0x08CA,0x08E1,BIDI_NSM},{0x08E2,0x08E2,BIDI_EN},
	{0x08E3,0x0902,BIDI_NSM},{0x093A,0x093A,BIDI_NSM},{0x093C,0x093C,BIDI_NSM},{0x0941,0x0948,BIDI_NSM},
	{0x094D,0x094D,BIDI_NSM},{0x0951,0x0957,BIDI_NSM},{0x0962,0x0963,BIDI_NSM},{0x0981,0x0981,BIDI_NSM},
	{0x09BC,0x09BC,BIDI_NSM},{0x09C1,0x09C4,BIDI_NSM},{0x09CD,0x09CD,BIDI_NSM},{0x09E2,0x09E3,BIDI_NSM},
	{0x09F2,0x09F3,BIDI_ET},{0x09FB,0x09FB,BIDI_ET},{0x09FE,0x09FE,BIDI_NSM},{0x0A01,0x0A02,BIDI_NSM},
	{0x0A3C,0x0A3C,BIDI_NSM},{0x0A41,0x0A42,BIDI_NSM},{0x0A47,0x0A48,BIDI_NSM},{0x0A4B,0x0A4D,BIDI_NSM},
	{0x0A51,0x0A51,BIDI_NSM},{0x0A70,0x0A71,BIDI_NSM},{0x0A75,0x0A75,BIDI_NSM},{0x0A81,0x0A82,BIDI_NSM},
	{0x0ABC,0x0ABC,BIDI_NSM},{0x0AC1,0x0AC5,BIDI_NSM},{0x0AC7,0x0AC8,BIDI_NSM},{0x0ACD,0x0ACD,BIDI_NSM},
	{0x0AE2,0x0AE3,BIDI_NSM},{0x0AF1,0x0AF1,BIDI_ET},{0x0AFA,0x0AFF,BIDI_NSM},{0x0B01,0x0B01,BIDI_NSM},
	{0x0B3C,0x0B3C,BIDI_NSM},{0x0B3F,0x0B3F,BIDI_NSM},{0x0B41,0x0B44,BIDI_NSM},{0x0B4D,0x0B4D,BIDI_NSM},
	{0x0B55,0x0B56,BIDI_NSM},{0x0B62,0x0B63,BIDI_NSM},{0x0B82,0x0B82,BIDI_NSM},{0x0BC0,0x0BC0,BIDI_NSM},
	{0x0BCD,0x0BCD,BIDI_NSM},{0x0BF3,0x0BF8,BIDI_N},{0x0BF9,0x0BF9,BIDI_ET},{0x0BFA,0x0BFA,BIDI_N},
	{0x0C00,0x0C00,BIDI_NSM},{0x0C04,0x0C04,BIDI_NSM},{0x0C3C,0x0C3C,BIDI_NSM},{0x0C3E,0x0C40,BIDI_NSM},
	{0x0C46,0x0C48,BIDI_NSM},{0x0C4A,0x0C4D,BIDI_NSM},{0x0C55,0x0C56,BIDI_NSM},{0x0C62,0x0C63,BIDI_NSM},
	{0x0C78,0x0C7E,BIDI_N},{0x0C81,0x0C81,BIDI_NSM},{0x0CBC,0x0CBC,BIDI_NSM},{0x0CCC,0x0CCD,BIDI_NSM},
	{0x0CE2,0x0CE3,BIDI_NSM},{0x0D00,0x0D01,BIDI_NSM},{0x0D3B,0x0D3C,BIDI_NSM},{0x0D41,0x0D44,BIDI_NSM},
	{0x0D4D,0x0D4D,BIDI_NSM},{0x0D62,0x0D63,BIDI_NSM},{0x0D81,0x0D81,BIDI_NSM},{0x0DCA,0x0DCA,BIDI_NSM},
	{0x0DD2,0x0DD4,BIDI_NSM},{0x0DD6,0x0DD6,BIDI_NSM},{0x0E31,0x0E31,BIDI_NSM},{0x0E34,0x0E3A,BIDI_NSM},
	{0x0E3F,0x0E3F,BIDI_ET},{0x0E47,0x0E4E,BIDI_NSM},{0x0EB1,0x0EB1,BIDI_NSM},{0x0EB4,0x0EBC,BIDI_NSM},
	{0x0EC8,0x0ECE,BIDI_NSM},{0x0F18,0x0F19,BIDI_NSM},{0x0F35,0x0F35,BIDI_NSM},{0x0F37,0x0F37,BIDI_NSM},
	{0x0F39,0x0F39,BIDI_NSM},{0x0F3A,0x0F3D,BIDI_N},{0x0F71,0x0F7E,BIDI_NSM},{0x0F80,0x0F84,BIDI_NSM},
	{0x0F86,0x0F87,BIDI_NSM},{0x0F8D,0x0F97,BIDI_NSM},{0x0F99,0x0FBC,BIDI_NSM},{0x0FC6,0x0FC6,BIDI_NSM},
	{0x102D,0x1030,BIDI_NSM},{0x1032,0x1037,BIDI_NSM},{0x1039,0x103A,BIDI_NSM},{0x103D,0x103E,BIDI_NSM},
	{0x1058,0x1059,BIDI_NSM},{0x105E,0x1060,BIDI_NSM},{0x1071,0x1074,BIDI_NSM},{0x1082,0x1082,BIDI_NSM},
	{0x1085,0x1086,BIDI_NSM},{0x108D,0x108D,BIDI_NSM},{0x109D,0x109D,BIDI_NSM},{0x135D,0x135F,BIDI_NSM},
	{0x1390,0x1399,BIDI_N},{0x1400,0x1400,BIDI_N},{0x1680,0x1680,BIDI_N},{0x169B,0x169C,BIDI_N},{0x1712,0x1714,BIDI_NSM},
	{0x1732,0x1733,BIDI_NSM},{0x1752,0x1753,BIDI_NSM},{0x1772,0x1773,BIDI_NSM},{0x17B4,0x17B5,BIDI_NSM},
	{0x17B7,0x17BD,BIDI_NSM},{0x17C6,0x17C6,BIDI_NSM},{0x17C9,0x17D3,BIDI_NSM},{0x17DB,0x17DB,BIDI_ET},
	{0x17DD,0x17DD,BIDI_NSM},{0x17F0,0x17F9,BIDI_N},{0x1800,0x180A,BIDI_N},{0x180B,0x180F,BIDI_NSM},
	{0x1885,0x1886,BIDI_NSM},{0x18A9,0x18A9,BIDI_NSM},{0x1920,0x1922,BIDI_NSM},{0x1927,0x1928,BIDI_NSM},
	{0x1932,0x1932,BIDI_NSM},{0x1939,0x193B,BIDI_NSM},{0x1940,0x1940,BIDI_N},{0x1944,0x1945,BIDI_N},{0x19DE,0x19FF,BIDI_N},
	{0x1A17,0x1A18,BIDI_NSM},{0x1A1B,0x1A1B,BIDI_NSM},{0x1A56,0x1A56,BIDI_NSM},{0x1A58,0x1A5E,BIDI_NSM},
	{0x1A60,0x1A60,BIDI_NSM},{0x1A62,0x1A62,BIDI_NSM},{0x1A65,0x1A6C,BIDI_NSM},{0x1A73,0x1A7C,BIDI_NSM},
	{0x1A7F,0x1A7F,BIDI_NSM},{0x1AB0,0x1ACE,BIDI_NSM},{0x1B00,0x1B03,BIDI_NSM},{0x1B34,0x1B34,BIDI_NSM},
	{0x1B36,0x1B3A,BIDI_NSM},{0x1B3C,0x1B3C,BIDI_NSM},{0x1B42,0x1B42,BIDI_NSM},{0x1B6B,0x1B73,BIDI_NSM},
	{0x1B80,0x1B81,BIDI_NSM},{0x1BA2,0x1BA5,BIDI_NSM},{0x1BA8,0x1BA9,BIDI_NSM},{0x1BAB,0x1BAD,BIDI_NSM},
	{0x1BE6,0x1BE6,BIDI_NSM},{0x1BE8,0x1BE9,BIDI_NSM},{0x1BED,0x1BED,BIDI_NSM},{0x1BEF,0x1BF1,BIDI_NSM},
	{0x1C2C,0x1C33,BIDI_NSM},{0x1C36,0x1C37,BIDI_NSM},{0x1CD0,0x1CD2,BIDI_NSM},{0x1CD4,0x1CE0,BIDI_NSM},
	{0x1CE2,0x1CE8,BIDI_NSM},{0x1CED,0x1CED,BIDI_NSM},{0x1CF4,0x1CF4,BIDI_NSM},{0x1CF8,0x1CF9,BIDI_NSM},
	{0x1DC0,0x1DFF,BIDI_NSM},{0x1FBD,0x1FBD,BIDI_N},{0x1FBF,0x1FC1,BIDI_N},{0x1FCD,0x1FCF,BIDI_N},{0x1FDD,0x1FDF,BIDI_N},
	{0x1FED,0x1FEF,BIDI_N},{0x1FFD,0x1FFE,BIDI_N},{0x2000,0x200A,BIDI_N},{0x200B,0x200D,BIDI_NSM},{0x200F,0x200F,BIDI_R},
	{0x2010,0x202E,BIDI_N},{0x202F,0x202F,BIDI_CS},{0x2030,0x2034,BIDI_ET},{0x2035,0x2043,BIDI_N},{0x2044,0x2044,BIDI_CS},
	{0x2045,0x205F,BIDI_N},{0x2060,0x2064,BIDI_NSM},{0x2066,0x2069,BIDI_N},{0x206A,0x206F,BIDI_NSM},
	{0x2070,0x2070,BIDI_EN},{0x2074,0x2079,BIDI_EN},{0x207A,0x207B,BIDI_ES},{0x207C,0x207E,BIDI_N},{0x2080,0x2089,BIDI_EN},
	{0x208A,0x208B,BIDI_ES},{0x208C,0x208E,BIDI_N},{0x20A0,0x20C0,BIDI_ET},{0x20D0,0x20F0,BIDI_NSM},{0x2100,0x2101,BIDI_N},
	{0x2103,0x2106,BIDI_N},{0x2108,0x2109,BIDI_N},{0x2114,0x2114,BIDI_N},{0x2116,0x2118,BIDI_N},{0x211E,0x2123,BIDI_N},
	{0x2125,0x2125,BIDI_N},{0x2127,0x2127,BIDI_N},{0x2129,0x2129,BIDI_N},{0x212E,0x212E,BIDI_ET},{0x213A,0x213B,BIDI_N},
	{0x2140,0x2144,BIDI_N},{0x214A,0x214D,BIDI_N},{0x2150,0x215F,BIDI_N},{0x2189,0x218B,BIDI_N},{0x2190,0x2211,BIDI_N},
	{0x2212,0x2212,BIDI_ES},{0x2213,0x2213,BIDI_ET},{0x2214,0x2335,BIDI_N},{0x237B,0x2394,BIDI_N},{0x2396,0x2426,BIDI_N},
	{0x2440,0x244A,BIDI_N},{0x2460,0x2487,BIDI_N},{0x2488,0x249B,BIDI_EN},{0x24EA,0x26AB,BIDI_N},{0x26AD,0x27FF,BIDI_N},
	{0x2900,0x2B73,BIDI_N},{0x2B76,0x2B95,BIDI_N},{0x2B97,0x2BFF,BIDI_N},{0x2CE5,0x2CEA,BIDI_N},{0x2CEF,0x2CF1,BIDI_NSM},
	{0x2CF9,0x2CFF,BIDI_N},{0x2D7F,0x2D7F,BIDI_NSM},{0x2DE0,0x2DFF,BIDI_NSM},{0x2E00,0x2E5D,BIDI_N},{0x2E80,0x2E99,BIDI_N},
	{0x2E9B,0x2EF3,BIDI_N},{0x2F00,0x2FD5,BIDI_N},{0x2FF0,0x2FFB,BIDI_N},{0x3000,0x3004,BIDI_N},{0x3008,0x3020,BIDI_N},
	{0x302A,0x302D,BIDI_NSM},{0x3030,0x3030,BIDI_N},{0x3036,0x3037,BIDI_N},{0x303D,0x303F,BIDI_N},{0x3099,0x309A,BIDI_NSM},
	{0x309B,0x309C,BIDI_N},{0x30A0,0x30A0,BIDI_N},{0x30FB,0x30FB,BIDI_N},{0x31C0,0x31E3,BIDI_N},{0x321D,0x321E,BIDI_N},
	{0x3250,0x325F,BIDI_N},{0x327C,0x327E,BIDI_N},{0x32B1,0x32BF,BIDI_N},{0x32CC,0x32CF,BIDI_N},{0x3377,0x337A,BIDI_N},
	{0x33DE,0x33DF,BIDI_N},{0x33FF,0x33FF,BIDI_N},{0x4DC0,0x4DFF,BIDI_N},{0xA490,0xA4C6,BIDI_N},{0xA60D,0xA60F,BIDI_N},
	{0xA66F,0xA672,BIDI_NSM},{0xA673,0xA673,BIDI_N},{0xA674,0xA67D,BIDI_NSM},{0xA67E,0xA67F,BIDI_N},
	{0xA69E,0xA69F,BIDI_NSM},{0xA6F0,0xA6F1,BIDI_NSM},{0xA700,0xA721,BIDI_N},{0xA788,0xA788,BIDI_N},
	{0xA802,0xA802,BIDI_NSM},{0xA806,0xA806,BIDI_NSM},{0xA80B,0xA80B,BIDI_NSM},{0xA825,0xA826,BIDI_NSM},
	{0xA828,0xA82B,BIDI_N},{0xA82C,0xA82C,BIDI_NSM},{0xA838,0xA839,BIDI_ET},{0xA874,0xA877,BIDI_N},
	{0xA8C4,0xA8C5,BIDI_NSM},{0xA8E0,0xA8F1,BIDI_NSM},{0xA8FF,0xA8FF,BIDI_NSM},{0xA926,0xA92D,BIDI_NSM},
	{0xA947,0xA951,BIDI_NSM},{0xA980,0xA982,BIDI_NSM},{0xA9B3,0xA9B3,BIDI_NSM},{0xA9B6,0xA9B9,BIDI_NSM},
	{0xA9BC,0xA9BD,BIDI_NSM},{0xA9E5,0xA9E5,BIDI_NSM},{0xAA29,0xAA2E,BIDI_NSM},{0xAA31,0xAA32,BIDI_NSM},
	{0xAA35,0xAA36,BIDI_NSM},{0xAA43,0xAA43,BIDI_NSM},{0xAA4C,0xAA4C,BIDI_NSM},{0xAA7C,0xAA7C,BIDI_NSM},
	{0xAAB0,0xAAB0,BIDI_NSM},{0xAAB2,0xAAB4,BIDI_NSM},{0xAAB7,0xAAB8,BIDI_NSM},{0xAABE,0xAABF,BIDI_NSM},
	{0xAAC1,0xAAC1,BIDI_NSM},{0xAAEC,0xAAED,BIDI_NSM},{0xAAF6,0xAAF6,BIDI_NSM},{0xAB6A,0xAB6B,BIDI_N},
	{0xABE5,0xABE5,BIDI_NSM},{0xABE8,0xABE8,BIDI_NSM},{0xABED,0xABED,BIDI_NSM},{0xD800,0xDFFF,BIDI_N},
	{0xFB1D,0xFB1D,BIDI_R},{0xFB1E,0xFB1E,BIDI_NSM},{0xFB1F,0xFB28,BIDI_R},{0xFB29,0xFB29,BIDI_ES},{0xFB2A,0xFD3D,BIDI_R},
	{0xFD3E,0xFD4F,BIDI_N},{0xFD50,0xFDCE,BIDI_R},{0xFDCF,0xFDCF,BIDI_N},{0xFDD0,0xFDFC,BIDI_R},{0xFDFD,0xFDFF,BIDI_N},
	{0xFE00,0xFE0F,BIDI_NSM},{0xFE10,0xFE19,BIDI_N},{0xFE20,0xFE2F,BIDI_NSM},{0xFE30,0xFE4F,BIDI_N},
	{0xFE50,0xFE50,BIDI_CS},{0xFE51,0xFE51,BIDI_N},{0xFE52,0xFE52,BIDI_CS},{0xFE54,0xFE54,BIDI_N},{0xFE55,0xFE55,BIDI_CS},
	{0xFE56,0xFE5E,BIDI_N},{0xFE5F,0xFE5F,BIDI_ET},{0xFE60,0xFE61,BIDI_N},{0xFE62,0xFE63,BIDI_ES},{0xFE64,0xFE66,BIDI_N},
	{0xFE68,0xFE68,BIDI_N},{0xFE69,0xFE6A,BIDI_ET},{0xFE6B,0xFE6B,BIDI_N},{0xFE70,0xFEFE,BIDI_R},{0xFEFF,0xFEFF,BIDI_NSM},
	{0xFF01,0xFF02,BIDI_N},{0xFF03,0xFF05,BIDI_ET},{0xFF06,0xFF0A,BIDI_N},{0xFF0B,0xFF0B,BIDI_ES},{0xFF0C,0xFF0C,BIDI_CS},
	{0xFF0D,0xFF0D,BIDI_ES},{0xFF0E,0xFF0F,BIDI_CS},{0xFF10,0xFF19,BIDI_EN},{0xFF1A,0xFF1A,BIDI_CS},{0xFF1B,0xFF20,BIDI_N},
	{0xFF3B,0xFF40,BIDI_N},{0xFF5B,0xFF65,BIDI_N},{0xFFE0,0xFFE1,BIDI_ET},{0xFFE2,0xFFE4,BIDI_N},{0xFFE5,0xFFE6,BIDI_ET},
	{0xFFE8,0xFFEE,BIDI_N},{0xFFF9,0xFFFD,BIDI_N},
};

// NFKC decompositions of the Arabic Presentation Forms-A/B (U+FB50-U+FDFF, U+FE70-U+FEFF),
// in logical order; the isolated harakat forms lose the space NFKC puts before them, and the Farsi
// yeh and heh doachashmee forms, which legacy Arabic tables use for plain yeh and heh, map to those.
static const wchar_t kFormPool[] =
	L"\x0671\x067B\x067E\x0680\x067A\x067F\x0679\x06A4\x06A6\x0684\x0683\x0686\x0687\x068D\x068C\x068E\x0688\x0698\x0691"
	L"\x06A9\x06AF\x06B3\x06B1\x06BA\x06BB\x06C0\x06C1\x0647\x06D2\x06D3\x06AD\x06C7\x06C6\x06C8\x06C7\x0674\x06CB\x06C5"
	L"\x06C9\x06D0\x0649\x0626\x0627\x0626\x06D5\x0626\x0648\x0626\x06C7\x0626\x06C6\x0626\x06C8\x0626\x06D0\x0626\x0649"
	L"\x064A\x0626\x062C\x0626\x062D\x0626\x0645\x0626\x064A\x0628\x062C\x0628\x062D\x0628\x062E\x0628\x0645\x0628\x0649"
	L"\x0628\x064A\x062A\x062C\x062A\x062D\x062A\x062E\x062A\x0645\x062A\x0649\x062A\x064A\x062B\x062C\x062B\x0645\x062B"
	L"\x0649\x062B\x064A\x062C\x062D\x062C\x0645\x062D\x0645\x062E\x062C\x062E\x062D\x062E\x0645\x0633\x062C\x0633\x062D"
	L"\x0633\x062E\x0633\x0645\x0635\x062D\x0635\x0645\x0636\x062C\x0636\x062D\x0636\x062E\x0636\x0645\x0637\x062D\x0637"
	L"\x0645\x0638\x0645\x0639\x062C\x0639\x0645\x063A\x062C\x063A\x0645\x0641\x062C\x0641\x062D\x0641\x062E\x0641\x0645"
	L"\x0641\x0649\x0641\x064A\x0642\x062D\x0642\x0645\x0642\x0649\x0642\x064A\x0643\x0627\x0643\x062C\x0643\x062D\x0643"
	L"\x062E\x0643\x0644\x0643\x0645\x0643\x0649\x0643\x064A\x0644\x062C\x0644\x062D\x0644\x062E\x0644\x0645\x0644\x0649"
	L"\x0644\x064A\x0645\x062C\x0645\x0645\x0645\x0649\x0645\x064A\x0646\x062C\x0646\x062D\x0646\x062E\x0646\x0645\x0646"
	L"\x0649\x0646\x064A\x0647\x062C\x0647\x0645\x0647\x0649\x0647\x064A\x064A\x062D\x064A\x062E\x064A\x0649\x0630\x0670"
	L"\x0631\x0670\x0649\x0670\x064C\x0651\x064D\x0651\x064E\x0651\x064F\x0651\x0650\x0651\x0651\x0670\x0626\x0631\x0626"
	L"\x0632\x0626\x0646\x0628\x0631\x0628\x0632\x0628\x0646\x062A\x0631\x062A\x0632\x062A\x0646\x062B\x0631\x062B\x0632"
	L"\x062B\x0646\x0645\x0627\x0646\x0631\x0646\x0632\x0646\x0646\x064A\x0631\x064A\x0632\x0626\x062E\x0626\x0647\x0628"
	L"\x0647\x062A\x0647\x0635\x062E\x0644\x0647\x0646\x0647\x0647\x0670\x062B\x0647\x0633\x0647\x0634\x0645\x0634\x0647"
	L"\x0640\x064E\x0651\x0640\x064F\x0651\x0640\x0650\x0651\x0637\x0649\x0637\x064A\x0639\x0649\x0639\x064A\x063A\x0649"
	L"\x063A\x064A\x0633\x0649\x0633\x064A\x0634\x0649\x0634\x064A\x062D\x0649\x062C\x0649\x062C\x064A\x062E\x0649\x0635"
	L"\x0649\x0635\x064A\x0636\x0649\x0636\x064A\x0634\x062C\x0634\x062D\x0634\x062E\x0634\x0631\x0633\x0631\x0635\x0631"
	L"\x0636\x0631\x0627\x064B\x062A\x062C\x0645\x062A\x062D\x062C\x062A\x062D\x0645\x062A\x062E\x0645\x062A\x0645\x062C"
	L"\x062A\x0645\x062D\x062A\x0645\x062E\x062D\x0645\x064A\x062D\x0645\x0649\x0633\x062D\x062C\x0633\x062C\x062D\x0633"
	L"\x062C\x0649\x0633\x0645\x062D\x0633\x0645\x062C\x0633\x0645\x0645\x0635\x062D\x062D\x0635\x0645\x0645\x0634\x062D"
	L"\x0645\x0634\x062C\x064A\x0634\x0645\x062E\x0634\x0645\x0645\x0636\x062D\x0649\x0636\x062E\x0645\x0637\x0645\x062D"
	L"\x0637\x0645\x0645\x0637\x0645\x064A\x0639\x062C\x0645\x0639\x0645\x0645\x0639\x0645\x0649\x063A\x0645\x0645\x063A"
	L"\x0645\x064A\x063A\x0645\x0649\x0641\x062E\x0645\x0642\x0645\x062D\x0642\x0645\x0645\x0644\x062D\x0645\x0644\x062D"
	L"\x064A\x0644\x062D\x0649\x0644\x062C\x062C\x0644\x062E\x0645\x0644\x0645\x062D\x0645\x062D\x062C\x0645\x062D\x064A"
	L"\x0645\x062C\x062D\x0645\x062E\x0645\x0645\x062C\x062E\x0647\x0645\x062C\x0647\x0645\x0645\x0646\x062D\x0645\x0646"
	L"\x062D\x0649\x0646\x062C\x0645\x0646\x062C\x0649\x0646\x0645\x064A\x0646\x0645\x0649\x064A\x0645\x0645\x0628\x062E"
	L"\x064A\x062A\x062C\x064A\x062A\x062C\x0649\x062A\x062E\x064A\x062A\x062E\x0649\x062A\x0645\x064A\x062A\x0645\x0649"
	L"\x062C\x0645\x064A\x062C\x062D\x0649\x062C\x0645\x0649\x0633\x062E\x0649\x0635\x062D\x064A\x0634\x062D\x064A\x0636"
	L"\x062D\x064A\x0644\x062C\x064A\x0644\x0645\x064A\x064A\x062C\x064A\x064A\x0645\x064A\x0645\x0645\x064A\x0642\x0645"
	L"\x064A\x0646\x062D\x064A\x0639\x0645\x064A\x0643\x0645\x064A\x0646\x062C\x062D\x0645\x062E\x064A\x0644\x062C\x0645"
	L"\x0643\x0645\x0645\x062C\x062D\x064A\x062D\x062C\x064A\x0645\x062C\x064A\x0641\x0645\x064A\x0628\x062D\x064A\x0633"
	L"\x062E\x064A\x0646\x062C\x064A\x0635\x0644\x06D2\x0642\x0644\x06D2\x0627\x0644\x0644\x0647\x0627\x0643\x0628\x0631"
	L"\x0645\x062D\x0645\x062F\x0635\x0644\x0639\x0645\x0631\x0633\x0648\x0644\x0639\x0644\x064A\x0647\x0648\x0633\x0644"
	L"\x0645\x0635\x0644\x0649\x0635\x0644\x0649\x0020\x0627\x0644\x0644\x0647\x0020\x0639\x0644\x064A\x0647\x0020\x0648"
	L"\x0633\x0644\x0645\x062C\x0644\x0020\x062C\x0644\x0627\x0644\x0647\x0631\x06CC\x0627\x0644\x0640\x064B\x0640\x0651"
	L"\x0652\x0640\x0652\x0621\x0622\x0623\x0624\x0625\x0629\x0644\x0622\x0644\x0623\x0644\x0625";
static const PresentationForm kForms[] = {
	{0xFB50,0,1},{0xFB51,0,1},{0xFB52,1,1},{0xFB53,1,1},{0xFB54,1,1},{0xFB55,1,1},{0xFB56,2,1},{0xFB57,2,1},{0xFB58,2,1},
	{0xFB59,2,1},{0xFB5A,3,1},{0xFB5B,3,1},{0xFB5C,3,1},{0xFB5D,3,1},{0xFB5E,4,1},{0xFB5F,4,1},{0xFB60,4,1},{0xFB61,4,1},
	{0xFB62,5,1},{0xFB63,5,1},{0xFB64,5,1},{0xFB65,5,1},{0xFB66,6,1},{0xFB67,6,1},{0xFB68,6,1},{0xFB69,6,1},{0xFB6A,7,1},
	{0xFB6B,7,1},{0xFB6C,7,1},{0xFB6D,7,1},{0xFB6E,8,1},{0xFB6F,8,1},{0xFB70,8,1},{0xFB71,8,1},{0xFB72,9,1},{0xFB73,9,1},
	{0xFB74,9,1},{0xFB75,9,1},{0xFB76,10,1},{0xFB77,10,1},{0xFB78,10,1},{0xFB79,10,1},{0xFB7A,11,1},{0xFB7B,11,1},
	{0xFB7C,11,1},{0xFB7D,11,1},{0xFB7E,12,1},{0xFB7F,12,1},{0xFB80,12,1},{0xFB81,12,1},{0xFB82,13,1},{0xFB83,13,1},
	{0xFB84,14,1},{0xFB85,14,1},{0xFB86,15,1},{0xFB87,15,1},{0xFB88,16,1},{0xFB89,16,1},{0xFB8A,17,1},{0xFB8B,17,1},
	{0xFB8C,18,1},{0xFB8D,18,1},{0xFB8E,19,1},{0xFB8F,19,1},{0xFB90,19,1},{0xFB91,19,1},{0xFB92,20,1},{0xFB93,20,1},
	{0xFB94,20,1},{0xFB95,20,1},{0xFB96,21,1},{0xFB97,21,1},{0xFB98,21,1},{0xFB99,21,1},{0xFB9A,22,1},{0xFB9B,22,1},
	{0xFB9C,22,1},{0xFB9D,22,1},{0xFB9E,23,1},{0xFB9F,23,1},{0xFBA0,24,1},{0xFBA1,24,1},{0xFBA2,24,1},{0xFBA3,24,1},
	{0xFBA4,25,1},{0xFBA5,25,1},{0xFBA6,26,1},{0xFBA7,26,1},{0xFBA8,26,1},{0xFBA9,26,1},{0xFBAA,27,1},{0xFBAB,27,1},
	{0xFBAC,27,1},{0xFBAD,27,1},{0xFBAE,28,1},{0xFBAF,28,1},{0xFBB0,29,1},{0xFBB1,29,1},{0xFBD3,30,1},{0xFBD4,30,1},
	{0xFBD5,30,1},{0xFBD6,30,1},{0xFBD7,31,1},{0xFBD8,31,1},{0xFBD9,32,1},{0xFBDA,32,1},{0xFBDB,33,1},{0xFBDC,33,1},
	{0xFBDD,34,2},{0xFBDE,36,1},{0xFBDF,36,1},{0xFBE0,37,1},{0xFBE1,37,1},{0xFBE2,38,1},{0xFBE3,38,1},{0xFBE4,39,1},
	{0xFBE5,39,1},{0xFBE6,39,1},{0xFBE7,39,1},{0xFBE8,40,1},{0xFBE9,40,1},{0xFBEA,41,2},{0xFBEB,41,2},{0xFBEC,43,2},
	{0xFBED,43,2},{0xFBEE,45,2},{0xFBEF,45,2},{0xFBF0,47,2},{0xFBF1,47,2},{0xFBF2,49,2},{0xFBF3,49,2},{0xFBF4,51,2},
	{0xFBF5,51,2},{0xFBF6,53,2},{0xFBF7,53,2},{0xFBF8,53,2},{0xFBF9,55,2},{0xFBFA,55,2},{0xFBFB,55,2},{0xFBFC,57,1},
	{0xFBFD,57,1},{0xFBFE,57,1},{0xFBFF,57,1},{0xFC00,58,2},{0xFC01,60,2},{0xFC02,62,2},{0xFC03,55,2},{0xFC04,64,2},
	{0xFC05,66,2},{0xFC06,68,2},{0xFC07,70,2},{0xFC08,72,2},{0xFC09,74,2},{0xFC0A,76,2},{0xFC0B,78,2},{0xFC0C,80,2},
	{0xFC0D,82,2},{0xFC0E,84,2},{0xFC0F,86,2},{0xFC10,88,2},{0xFC11,90,2},{0xFC12,92,2},{0xFC13,94,2},{0xFC14,96,2},
	{0xFC15,98,2},{0xFC16,100,2},{0xFC17,99,2},{0xFC18,102,2},{0xFC19,104,2},{0xFC1A,106,2},{0xFC1B,108,2},{0xFC1C,110,2},
	{0xFC1D,112,2},{0xFC1E,114,2},{0xFC1F,116,2},{0xFC20,118,2},{0xFC21,120,2},{0xFC22,122,2},{0xFC23,124,2},
	{0xFC24,126,2},{0xFC25,128,2},{0xFC26,130,2},{0xFC27,132,2},{0xFC28,134,2},{0xFC29,136,2},{0xFC2A,138,2},
	{0xFC2B,140,2},{0xFC2C,142,2},{0xFC2D,144,2},{0xFC2E,146,2},{0xFC2F,148,2},{0xFC30,150,2},{0xFC31,152,2},
	{0xFC32,154,2},{0xFC33,156,2},{0xFC34,158,2},{0xFC35,160,2},{0xFC36,162,2},{0xFC37,164,2},{0xFC38,166,2},
	{0xFC39,168,2},{0xFC3A,170,2},{0xFC3B,172,2},{0xFC3C,174,2},{0xFC3D,176,2},{0xFC3E,178,2},{0xFC3F,180,2},
	{0xFC40,182,2},{0xFC41,184,2},{0xFC42,186,2},{0xFC43,188,2},{0xFC44,190,2},{0xFC45,192,2},{0xFC46,101,2},
	{0xFC47,103,2},{0xFC48,194,2},{0xFC49,196,2},{0xFC4A,198,2},{0xFC4B,200,2},{0xFC4C,202,2},{0xFC4D,204,2},
	{0xFC4E,206,2},{0xFC4F,208,2},{0xFC50,210,2},{0xFC51,212,2},{0xFC52,214,2},{0xFC53,216,2},{0xFC54,218,2},{0xFC55,97,2},
	{0xFC56,220,2},{0xFC57,222,2},{0xFC58,191,2},{0xFC59,224,2},{0xFC5A,219,2},{0xFC5B,226,2},{0xFC5C,228,2},
	{0xFC5D,230,2},{0xFC5E,232,2},{0xFC5F,234,2},{0xFC60,236,2},{0xFC61,238,2},{0xFC62,240,2},{0xFC63,242,2},
	{0xFC64,244,2},{0xFC65,246,2},{0xFC66,62,2},{0xFC67,248,2},{0xFC68,55,2},{0xFC69,64,2},{0xFC6A,250,2},{0xFC6B,252,2},
	{0xFC6C,72,2},{0xFC6D,254,2},{0xFC6E,74,2},{0xFC6F,76,2},{0xFC70,256,2},{0xFC71,258,2},{0xFC72,84,2},{0xFC73,260,2},
	{0xFC74,86,2},{0xFC75,88,2},{0xFC76,262,2},{0xFC77,264,2},{0xFC78,92,2},{0xFC79,266,2},{0xFC7A,94,2},{0xFC7B,96,2},
	{0xFC7C,152,2},{0xFC7D,154,2},{0xFC7E,160,2},{0xFC7F,162,2},{0xFC80,164,2},{0xFC81,172,2},{0xFC82,174,2},
	{0xFC83,176,2},{0xFC84,178,2},{0xFC85,186,2},{0xFC86,188,2},{0xFC87,190,2},{0xFC88,268,2},{0xFC89,194,2},
	{0xFC8A,270,2},{0xFC8B,272,2},{0xFC8C,206,2},{0xFC8D,274,2},{0xFC8E,208,2},{0xFC8F,210,2},{0xFC90,230,2},
	{0xFC91,276,2},{0xFC92,278,2},{0xFC93,191,2},{0xFC94,199,2},{0xFC95,224,2},{0xFC96,219,2},{0xFC97,58,2},{0xFC98,60,2},
	{0xFC99,280,2},{0xFC9A,62,2},{0xFC9B,282,2},{0xFC9C,66,2},{0xFC9D,68,2},{0xFC9E,70,2},{0xFC9F,72,2},{0xFCA0,284,2},
	{0xFCA1,78,2},{0xFCA2,80,2},{0xFCA3,82,2},{0xFCA4,84,2},{0xFCA5,286,2},{0xFCA6,92,2},{0xFCA7,98,2},{0xFCA8,100,2},
	{0xFCA9,99,2},{0xFCAA,102,2},{0xFCAB,104,2},{0xFCAC,108,2},{0xFCAD,110,2},{0xFCAE,112,2},{0xFCAF,114,2},{0xFCB0,116,2},
	{0xFCB1,118,2},{0xFCB2,288,2},{0xFCB3,120,2},{0xFCB4,122,2},{0xFCB5,124,2},{0xFCB6,126,2},{0xFCB7,128,2},
	{0xFCB8,130,2},{0xFCB9,134,2},{0xFCBA,136,2},{0xFCBB,138,2},{0xFCBC,140,2},{0xFCBD,142,2},{0xFCBE,144,2},
	{0xFCBF,146,2},{0xFCC0,148,2},{0xFCC1,150,2},{0xFCC2,156,2},{0xFCC3,158,2},{0xFCC4,166,2},{0xFCC5,168,2},
	{0xFCC6,170,2},{0xFCC7,172,2},{0xFCC8,174,2},{0xFCC9,180,2},{0xFCCA,182,2},{0xFCCB,184,2},{0xFCCC,186,2},
	{0xFCCD,290,2},{0xFCCE,192,2},{0xFCCF,101,2},{0xFCD0,103,2},{0xFCD1,194,2},{0xFCD2,200,2},{0xFCD3,202,2},
	{0xFCD4,204,2},{0xFCD5,206,2},{0xFCD6,292,2},{0xFCD7,212,2},{0xFCD8,214,2},{0xFCD9,294,2},{0xFCDA,97,2},{0xFCDB,220,2},
	{0xFCDC,222,2},{0xFCDD,191,2},{0xFCDE,211,2},{0xFCDF,62,2},{0xFCE0,282,2},{0xFCE1,72,2},{0xFCE2,284,2},{0xFCE3,84,2},
	{0xFCE4,286,2},{0xFCE5,92,2},{0xFCE6,296,2},{0xFCE7,116,2},{0xFCE8,298,2},{0xFCE9,300,2},{0xFCEA,302,2},{0xFCEB,172,2},
	{0xFCEC,174,2},{0xFCED,186,2},{0xFCEE,206,2},{0xFCEF,292,2},{0xFCF0,191,2},{0xFCF1,211,2},{0xFCF2,304,3},
	{0xFCF3,307,3},{0xFCF4,310,3},{0xFCF5,313,2},{0xFCF6,315,2},{0xFCF7,317,2},{0xFCF8,319,2},{0xFCF9,321,2},
	{0xFCFA,323,2},{0xFCFB,325,2},{0xFCFC,327,2},{0xFCFD,329,2},{0xFCFE,331,2},{0xFCFF,333,2},{0xFD00,221,2},
	{0xFD01,335,2},{0xFD02,337,2},{0xFD03,339,2},{0xFD04,223,2},{0xFD05,341,2},{0xFD06,343,2},{0xFD07,345,2},
	{0xFD08,347,2},{0xFD09,349,2},{0xFD0A,351,2},{0xFD0B,353,2},{0xFD0C,300,2},{0xFD0D,355,2},{0xFD0E,357,2},
	{0xFD0F,359,2},{0xFD10,361,2},{0xFD11,313,2},{0xFD12,315,2},{0xFD13,317,2},{0xFD14,319,2},{0xFD15,321,2},
	{0xFD16,323,2},{0xFD17,325,2},{0xFD18,327,2},{0xFD19,329,2},{0xFD1A,331,2},{0xFD1B,333,2},{0xFD1C,221,2},
	{0xFD1D,335,2},{0xFD1E,337,2},{0xFD1F,339,2},{0xFD20,223,2},{0xFD21,341,2},{0xFD22,343,2},{0xFD23,345,2},
	{0xFD24,347,2},{0xFD25,349,2},{0xFD26,351,2},{0xFD27,353,2},{0xFD28,300,2},{0xFD29,355,2},{0xFD2A,357,2},
	{0xFD2B,359,2},{0xFD2C,361,2},{0xFD2D,349,2},{0xFD2E,351,2},{0xFD2F,353,2},{0xFD30,300,2},{0xFD31,298,2},
	{0xFD32,302,2},{0xFD33,132,2},{0xFD34,110,2},{0xFD35,112,2},{0xFD36,114,2},{0xFD37,349,2},{0xFD38,351,2},
	{0xFD39,353,2},{0xFD3A,132,2},{0xFD3B,134,2},{0xFD3C,363,2},{0xFD3D,363,2},{0xFD50,365,3},{0xFD51,368,3},
	{0xFD52,368,3},{0xFD53,371,3},{0xFD54,374,3},{0xFD55,377,3},{0xFD56,380,3},{0xFD57,383,3},{0xFD58,100,3},
	{0xFD59,100,3},{0xFD5A,386,3},{0xFD5B,389,3},{0xFD5C,392,3},{0xFD5D,395,3},{0xFD5E,398,3},{0xFD5F,401,3},
	{0xFD60,401,3},{0xFD61,404,3},{0xFD62,407,3},{0xFD63,407,3},{0xFD64,410,3},{0xFD65,410,3},{0xFD66,413,3},
	{0xFD67,416,3},{0xFD68,416,3},{0xFD69,419,3},{0xFD6A,422,3},{0xFD6B,422,3},{0xFD6C,425,3},{0xFD6D,425,3},
	{0xFD6E,428,3},{0xFD6F,431,3},{0xFD70,431,3},{0xFD71,434,3},{0xFD72,434,3},{0xFD73,437,3},{0xFD74,440,3},
	{0xFD75,443,3},{0xFD76,446,3},{0xFD77,446,3},{0xFD78,449,3},{0xFD79,452,3},{0xFD7A,455,3},{0xFD7B,458,3},
	{0xFD7C,461,3},{0xFD7D,461,3},{0xFD7E,464,3},{0xFD7F,467,3},{0xFD80,470,3},{0xFD81,473,3},{0xFD82,476,3},
	{0xFD83,479,3},{0xFD84,479,3},{0xFD85,482,3},{0xFD86,482,3},{0xFD87,485,3},{0xFD88,485,3},{0xFD89,488,3},
	{0xFD8A,101,3},{0xFD8B,491,3},{0xFD8C,494,3},{0xFD8D,192,3},{0xFD8E,103,3},{0xFD8F,497,3},{0xFD92,500,3},
	{0xFD93,503,3},{0xFD94,506,3},{0xFD95,509,3},{0xFD96,512,3},{0xFD97,515,3},{0xFD98,515,3},{0xFD99,518,3},
	{0xFD9A,521,3},{0xFD9B,524,3},{0xFD9C,527,3},{0xFD9D,527,3},{0xFD9E,530,3},{0xFD9F,533,3},{0xFDA0,536,3},
	{0xFDA1,539,3},{0xFDA2,542,3},{0xFDA3,545,3},{0xFDA4,548,3},{0xFDA5,551,3},{0xFDA6,554,3},{0xFDA7,557,3},
	{0xFDA8,560,3},{0xFDA9,563,3},{0xFDAA,566,3},{0xFDAB,569,3},{0xFDAC,572,3},{0xFDAD,575,3},{0xFDAE,220,3},
	{0xFDAF,578,3},{0xFDB0,581,3},{0xFDB1,584,3},{0xFDB2,587,3},{0xFDB3,590,3},{0xFDB4,464,3},{0xFDB5,470,3},
	{0xFDB6,593,3},{0xFDB7,596,3},{0xFDB8,599,3},{0xFDB9,602,3},{0xFDBA,605,3},{0xFDBB,608,3},{0xFDBC,605,3},
	{0xFDBD,599,3},{0xFDBE,611,3},{0xFDBF,614,3},{0xFDC0,617,3},{0xFDC1,620,3},{0xFDC2,623,3},{0xFDC3,608,3},
	{0xFDC4,443,3},{0xFDC5,413,3},{0xFDC6,626,3},{0xFDC7,629,3},{0xFDF0,632,3},{0xFDF1,635,3},{0xFDF2,638,4},
	{0xFDF3,642,4},{0xFDF4,646,4},{0xFDF5,650,4},{0xFDF6,654,4},{0xFDF7,658,4},{0xFDF8,662,4},{0xFDF9,666,3},
	{0xFDFA,669,18},{0xFDFB,687,8},{0xFDFC,695,4},{0xFE70,364,1},{0xFE71,699,2},{0xFE72,232,1},{0xFE74,234,1},
	{0xFE76,236,1},{0xFE77,304,2},{0xFE78,238,1},{0xFE79,307,2},{0xFE7A,240,1},{0xFE7B,310,2},{0xFE7C,233,1},
	{0xFE7D,701,2},{0xFE7E,703,1},{0xFE7F,704,2},{0xFE80,706,1},{0xFE81,707,1},{0xFE82,707,1},{0xFE83,708,1},
	{0xFE84,708,1},{0xFE85,709,1},{0xFE86,709,1},{0xFE87,710,1},{0xFE88,710,1},{0xFE89,41,1},{0xFE8A,41,1},{0xFE8B,41,1},
	{0xFE8C,41,1},{0xFE8D,42,1},{0xFE8E,42,1},{0xFE8F,66,1},{0xFE90,66,1},{0xFE91,66,1},{0xFE92,66,1},{0xFE93,711,1},
	{0xFE94,711,1},{0xFE95,78,1},{0xFE96,78,1},{0xFE97,78,1},{0xFE98,78,1},{0xFE99,90,1},{0xFE9A,90,1},{0xFE9B,90,1},
	{0xFE9C,90,1},{0xFE9D,59,1},{0xFE9E,59,1},{0xFE9F,59,1},{0xFEA0,59,1},{0xFEA1,61,1},{0xFEA2,61,1},{0xFEA3,61,1},
	{0xFEA4,61,1},{0xFEA5,71,1},{0xFEA6,71,1},{0xFEA7,71,1},{0xFEA8,71,1},{0xFEA9,649,1},{0xFEAA,649,1},{0xFEAB,226,1},
	{0xFEAC,226,1},{0xFEAD,228,1},{0xFEAE,228,1},{0xFEAF,247,1},{0xFEB0,247,1},{0xFEB1,110,1},{0xFEB2,110,1},
	{0xFEB3,110,1},{0xFEB4,110,1},{0xFEB5,300,1},{0xFEB6,300,1},{0xFEB7,300,1},{0xFEB8,300,1},{0xFEB9,118,1},
	{0xFEBA,118,1},{0xFEBB,118,1},{0xFEBC,118,1},{0xFEBD,122,1},{0xFEBE,122,1},{0xFEBF,122,1},{0xFEC0,122,1},
	{0xFEC1,130,1},{0xFEC2,130,1},{0xFEC3,130,1},{0xFEC4,130,1},{0xFEC5,134,1},{0xFEC6,134,1},{0xFEC7,134,1},
	{0xFEC8,134,1},{0xFEC9,136,1},{0xFECA,136,1},{0xFECB,136,1},{0xFECC,136,1},{0xFECD,140,1},{0xFECE,140,1},
	{0xFECF,140,1},{0xFED0,140,1},{0xFED1,144,1},{0xFED2,144,1},{0xFED3,144,1},{0xFED4,144,1},{0xFED5,156,1},
	{0xFED6,156,1},{0xFED7,156,1},{0xFED8,156,1},{0xFED9,164,1},{0xFEDA,164,1},{0xFEDB,164,1},{0xFEDC,164,1},
	{0xFEDD,173,1},{0xFEDE,173,1},{0xFEDF,173,1},{0xFEE0,173,1},{0xFEE1,63,1},{0xFEE2,63,1},{0xFEE3,63,1},{0xFEE4,63,1},
	{0xFEE5,200,1},{0xFEE6,200,1},{0xFEE7,200,1},{0xFEE8,200,1},{0xFEE9,27,1},{0xFEEA,27,1},{0xFEEB,27,1},{0xFEEC,27,1},
	{0xFEED,46,1},{0xFEEE,46,1},{0xFEEF,40,1},{0xFEF0,40,1},{0xFEF1,57,1},{0xFEF2,57,1},{0xFEF3,57,1},{0xFEF4,57,1},
	{0xFEF5,712,2},{0xFEF6,712,2},{0xFEF7,714,2},{0xFEF8,714,2},{0xFEF9,716,2},{0xFEFA,716,2},{0xFEFB,691,2},
	{0xFEFC,691,2},
};
// Contextual forms of the Arabic letters (base, isolated, final, initial, medial; 0 where Unicode has none),
// from the presentation-form decompositions; Forms-B first, as the legacy tables use them.
static const ArabicForms kArabicForms[] = {
	{0x0621,0xFE80,0x0000,0x0000,0x0000},{0x0622,0xFE81,0xFE82,0x0000,0x0000},{0x0623,0xFE83,0xFE84,0x0000,0x0000},
	{0x0624,0xFE85,0xFE86,0x0000,0x0000},{0x0625,0xFE87,0xFE88,0x0000,0x0000},{0x0626,0xFE89,0xFE8A,0xFE8B,0xFE8C},
	{0x0627,0xFE8D,0xFE8E,0x0000,0x0000},{0x0628,0xFE8F,0xFE90,0xFE91,0xFE92},{0x0629,0xFE93,0xFE94,0x0000,0x0000},
	{0x062A,0xFE95,0xFE96,0xFE97,0xFE98},{0x062B,0xFE99,0xFE9A,0xFE9B,0xFE9C},{0x062C,0xFE9D,0xFE9E,0xFE9F,0xFEA0},
	{0x062D,0xFEA1,0xFEA2,0xFEA3,0xFEA4},{0x062E,0xFEA5,0xFEA6,0xFEA7,0xFEA8},{0x062F,0xFEA9,0xFEAA,0x0000,0x0000},
	{0x0630,0xFEAB,0xFEAC,0x0000,0x0000},{0x0631,0xFEAD,0xFEAE,0x0000,0x0000},{0x0632,0xFEAF,0xFEB0,0x0000,0x0000},
	{0x0633,0xFEB1,0xFEB2,0xFEB3,0xFEB4},{0x0634,0xFEB5,0xFEB6,0xFEB7,0xFEB8},{0x0635,0xFEB9,0xFEBA,0xFEBB,0xFEBC},
	{0x0636,0xFEBD,0xFEBE,0xFEBF,0xFEC0},{0x0637,0xFEC1,0xFEC2,0xFEC3,0xFEC4},{0x0638,0xFEC5,0xFEC6,0xFEC7,0xFEC8},
	{0x0639,0xFEC9,0xFECA,0xFECB,0xFECC},{0x063A,0xFECD,0xFECE,0xFECF,0xFED0},{0x0641,0xFED1,0xFED2,0xFED3,0xFED4},
	{0x0642,0xFED5,0xFED6,0xFED7,0xFED8},{0x0643,0xFED9,0xFEDA,0xFEDB,0xFEDC},{0x0644,0xFEDD,0xFEDE,0xFEDF,0xFEE0},
	{0x0645,0xFEE1,0xFEE2,0xFEE3,0xFEE4},{0x0646,0xFEE5,0xFEE6,0xFEE7,0xFEE8},{0x0647,0xFEE9,0xFEEA,0xFEEB,0xFEEC},
	{0x0648,0xFEED,0xFEEE,0x0000,0x0000},{0x0649,0xFEEF,0xFEF0,0xFBE8,0xFBE9},{0x064A,0xFEF1,0xFEF2,0xFEF3,0xFEF4},
	{0x0671,0xFB50,0xFB51,0x0000,0x0000},{0x0677,0xFBDD,0x0000,0x0000,0x0000},{0x0679,0xFB66,0xFB67,0xFB68,0xFB69},
	{0x067A,0xFB5E,0xFB5F,0xFB60,0xFB61},{0x067B,0xFB52,0xFB53,0xFB54,0xFB55},{0x067E,0xFB56,0xFB57,0xFB58,0xFB59},
	{0x067F,0xFB62,0xFB63,0xFB64,0xFB65},{0x0680,0xFB5A,0xFB5B,0xFB5C,0xFB5D},{0x0683,0xFB76,0xFB77,0xFB78,0xFB79},
	{0x0684,0xFB72,0xFB73,0xFB74,0xFB75},{0x0686,0xFB7A,0xFB7B,0xFB7C,0xFB7D},{0x0687,0xFB7E,0xFB7F,0xFB80,0xFB81},
	{0x0688,0xFB88,0xFB89,0x0000,0x0000},{0x068C,0xFB84,0xFB85,0x0000,0x0000},{0x068D,0xFB82,0xFB83,0x0000,0x0000},
	{0x068E,0xFB86,0xFB87,0x0000,0x0000},{0x0691,0xFB8C,0xFB8D,0x0000,0x0000},{0x0698,0xFB8A,0xFB8B,0x0000,0x0000},
	{0x06A4,0xFB6A,0xFB6B,0xFB6C,0xFB6D},{0x06A6,0xFB6E,0xFB6F,0xFB70,0xFB71},{0x06A9,0xFB8E,0xFB8F,0xFB90,0xFB91},
	{0x06AD,0xFBD3,0xFBD4,0xFBD5,0xFBD6},{0x06AF,0xFB92,0xFB93,0xFB94,0xFB95},{0x06B1,0xFB9A,0xFB9B,0xFB9C,0xFB9D},
	{0x06B3,0xFB96,0xFB97,0xFB98,0xFB99},{0x06BA,0xFB9E,0xFB9F,0x0000,0x0000},{0x06BB,0xFBA0,0xFBA1,0xFBA2,0xFBA3},
	{0x06BE,0xFBAA,0xFBAB,0xFBAC,0xFBAD},{0x06C0,0xFBA4,0xFBA5,0x0000,0x0000},{0x06C1,0xFBA6,0xFBA7,0xFBA8,0xFBA9},
	{0x06C5,0xFBE0,0xFBE1,0x0000,0x0000},{0x06C6,0xFBD9,0xFBDA,0x0000,0x0000},{0x06C7,0xFBD7,0xFBD8,0x0000,0x0000},
	{0x06C8,0xFBDB,0xFBDC,0x0000,0x0000},{0x06C9,0xFBE2,0xFBE3,0x0000,0x0000},{0x06CB,0xFBDE,0xFBDF,0x0000,0x0000},
	{0x06CC,0xFBFC,0xFBFD,0xFBFE,0xFBFF},{0x06D0,0xFBE4,0xFBE5,0xFBE6,0xFBE7},{0x06D2,0xFBAE,0xFBAF,0x0000,0x0000},
	{0x06D3,0xFBB0,0xFBB1,0x0000,0x0000},
};

bool isPresentationForm(unsigned int c)
{
	return (c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFE);
}

bool isArabicLetter(unsigned int c)
{
	return (c >= 0x0620 && c <= 0x064A) || (c >= 0x066E && c <= 0x06D3) || (c >= 0x06FA && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F);
}

const PresentationForm *findForm(unsigned int c)
{
	const PresentationForm *end = kForms + sizeof(kForms) / sizeof(kForms[0]);
	const PresentationForm *it = std::lower_bound(kForms, end, c,
		[](const PresentationForm &form, unsigned int cp) { return form.codepoint < cp; });
	return (it != end && it->codepoint == c) ? it : nullptr;
}

bool isWhitespace(unsigned int c)
{
	return c == ' ' || c == '\t' || c == 0x00A0 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F || c == 0x3000;
}

// Word-shape test for Arabic lines without presentation forms: reversed Arabic words start with
// the letters words end with (teh marbuta, alef maksura) and end with a reversed "al-" prefix.
bool lineLooksVisual(const wchar_t *text, size_t count)
{
	int visual = 0;
	int logical = 0;
	bool arabic = false;
	size_t i = 0;
	while (i < count)
	{
		while (i < count && !isArabicLetter(text[i]))
		{
			if (isPresentationForm(text[i]))
				return true;
			++i;
		}
		const size_t start = i;
		while (i < count && isArabicLetter(text[i]))
			++i;
		const size_t length = i - start;
		if (length == 0)
			continue;
		arabic = true;
		const wchar_t first = text[start];
		const wchar_t last = text[i - 1];
		if (first == 0x0629 || first == 0x0649)
			++visual;
		if (last == 0x0629 || last == 0x0649)
			++logical;
		if (length > 3 && text[i - 2] == 0x0644 && text[i - 1] == 0x0627)
			++visual;
		if (length > 3 && text[start] == 0x0627 && text[start + 1] == 0x0644)
			++logical;
	}
	return arabic && visual > logical;
}

void appendLogicalLine(std::wstring &out, const wchar_t *text, size_t count)
{
	if (!lineLooksVisual(text, count))
	{
		out.append(text, count);
		return;
	}

	std::vector<unsigned int> codepoints(text, text + count);
	std::vector<unsigned char> levels(count);
	std::vector<int> order(count);
	resolveLevels(codepoints.data(), count, true, levels.data());
	// With levels of 1 and 2 only, the visual reorder is its own inverse: reordering the visual
	// line gives back the logical one that displays as it.
	visualOrder(levels.data(), count, order.data());

	for (size_t i = 0; i < count; ++i)
	{
		const int from = order[i];
		unsigned int c = codepoints[from];
		if (levels[from] & 1)
		{
			if (unsigned int mirrored = mirroredBracket(c))
				c = mirrored;
		}
		// Legacy tables also use the base heh doachashmee and Farsi yeh for plain heh and yeh.
		if (c == 0x06BE)
			c = 0x0647;
		else if (c == 0x06CC)
			c = 0x064A;
		if (const PresentationForm *form = findForm(c))
			out.append(kFormPool + form->poolOffset, form->length);
		else
			out.push_back((wchar_t)c);
	}
}

const ArabicForms *findArabicForms(unsigned int c)
{
	const ArabicForms *end = kArabicForms + sizeof(kArabicForms) / sizeof(kArabicForms[0]);
	const ArabicForms *it = std::lower_bound(kArabicForms, end, c,
		[](const ArabicForms &forms, unsigned int cp) { return forms.base < cp; });
	return (it != end && it->base == c) ? it : nullptr;
}

const unsigned int kTatweel = 0x0640;
const unsigned int kLam = 0x0644;

bool joinsBefore(unsigned int c)
{
	// Can connect to the letter before it (its right side): every joining letter.
	const ArabicForms *forms = findArabicForms(c);
	return c == kTatweel || (forms && forms->final);
}

bool joinsAfter(unsigned int c)
{
	// Can connect to the letter after it (its left side): dual-joining letters only.
	const ArabicForms *forms = findArabicForms(c);
	return c == kTatweel || (forms && forms->initial && forms->medial);
}

bool isTransparent(unsigned int c)
{
	return bidiType(c) == BIDI_NSM && c >= 0x0300;
}

// The lam-alef ligature (isolated, final) for an alef, or 0 if c is no alef.
unsigned int lamAlef(unsigned int c, bool final)
{
	switch (c)
	{
		case 0x0622: return final ? 0xFEF6 : 0xFEF5;
		case 0x0623: return final ? 0xFEF8 : 0xFEF7;
		case 0x0625: return final ? 0xFEFA : 0xFEF9;
		case 0x0627: return final ? 0xFEFC : 0xFEFB;
		default: return 0;
	}
}

void appendLegacyVisualLine(std::wstring &out, const wchar_t *text, size_t count, bool preferRtl)
{
	bool arabic = false;
	for (size_t i = 0; i < count && !arabic; ++i)
		arabic = findArabicForms(text[i]) != nullptr;
	if (!arabic)
	{
		out.append(text, count);
		return;
	}

	// Paragraph direction from the first strong character (UAX #9 P2/P3), unless an Arabic
	// line should read right to left anyway.
	bool rtlBase = preferRtl;
	for (size_t i = 0; i < count && !preferRtl; ++i)
	{
		const BidiType t = bidiType(text[i]);
		if (t == BIDI_L || t == BIDI_R)
		{
			rtlBase = t == BIDI_R;
			break;
		}
	}

	std::vector<unsigned int> logical(text, text + count);
	std::vector<unsigned char> logicalLevels(count);
	resolveLevels(logical.data(), count, rtlBase, logicalLevels.data());

	// Shape in logical order; a lam-alef pair becomes one ligature and keeps the lam's level.
	std::vector<unsigned int> shaped;
	std::vector<unsigned char> levels;
	shaped.reserve(count);
	levels.reserve(count);
	for (size_t i = 0; i < count; ++i)
	{
		const unsigned int c = logical[i];
		const ArabicForms *forms = findArabicForms(c);
		if (!forms)
		{
			shaped.push_back(c);
			levels.push_back(logicalLevels[i]);
			continue;
		}

		size_t prev = i;
		while (prev > 0 && isTransparent(logical[prev - 1]))
			--prev;
		size_t next = i + 1;
		while (next < count && isTransparent(logical[next]))
			++next;
		const bool joinPrev = prev > 0 && joinsAfter(logical[prev - 1]) && joinsBefore(c);

		if (c == kLam && next < count && lamAlef(logical[next], false))
		{
			shaped.push_back(lamAlef(logical[next], joinPrev));
			levels.push_back(logicalLevels[i]);
			// Marks between the lam and the alef stay after the ligature.
			for (size_t k = i + 1; k < next; ++k)
			{
				shaped.push_back(logical[k]);
				levels.push_back(logicalLevels[k]);
			}
			i = next;
			continue;
		}

		const bool joinNext = next < count && joinsAfter(c) && joinsBefore(logical[next]);
		unsigned int form = 0;
		if (joinPrev && joinNext)
			form = forms->medial;
		else if (joinPrev)
			form = forms->final;
		else if (joinNext)
			form = forms->initial;
		if (!form)
			form = forms->isolated ? forms->isolated : c;
		shaped.push_back(c == kTatweel ? c : form);
		levels.push_back(logicalLevels[i]);
	}

	std::vector<int> order(shaped.size());
	visualOrder(levels.data(), shaped.size(), order.data());
	for (size_t i = 0; i < shaped.size(); ++i)
	{
		const int from = order[i];
		unsigned int c = shaped[from];
		if (levels[from] & 1)
		{
			if (unsigned int mirrored = mirroredBracket(c))
				c = mirrored;
		}
		out.push_back((wchar_t)c);
	}
}

} // namespace

BidiType bidiType(unsigned int c)
{
	if (c >= 0x10000)
	{
		if (c >= 0x1F000 && c <= 0x1FFFF)
			return BIDI_N; // emoji and pictographs
		if ((c >= 0x10800 && c <= 0x10FFF) || (c >= 0x1E800 && c <= 0x1EFFF))
			return BIDI_R;
		return BIDI_L;
	}
	const BidiRange *end = kBidiRanges + sizeof(kBidiRanges) / sizeof(kBidiRanges[0]);
	const BidiRange *it = std::upper_bound(kBidiRanges, end, c,
		[](unsigned int cp, const BidiRange &range) { return cp < range.first; });
	if (it == kBidiRanges)
		return BIDI_L;
	--it;
	return c <= it->last ? it->type : BIDI_L;
}

bool isRtl(unsigned int c)
{
	return bidiType(c) == BIDI_R;
}

void resolveLevels(const unsigned int *text, size_t count, bool rtlBase, unsigned char *levels)
{
	if (count == 0)
		return;

	const BidiType base = rtlBase ? BIDI_R : BIDI_L;
	std::vector<BidiType> types(count);

	// W1: a nonspacing mark takes the type of the character before it.
	for (size_t i = 0; i < count; ++i)
	{
		BidiType t = bidiType(text[i]);
		if (t == BIDI_NSM)
			t = i > 0 ? types[i - 1] : BIDI_N;
		types[i] = t;
	}

	// W4: a single separator between two numbers joins them.
	for (size_t i = 1; i + 1 < count; ++i)
	{
		if ((types[i] == BIDI_ES || types[i] == BIDI_CS) && types[i - 1] == BIDI_EN && types[i + 1] == BIDI_EN)
			types[i] = BIDI_EN;
	}

	// W5: terminators next to a number are part of it.
	for (size_t i = 0; i < count;)
	{
		if (types[i] != BIDI_ET)
		{
			++i;
			continue;
		}
		size_t end = i;
		while (end < count && types[end] == BIDI_ET)
			++end;
		const bool touchesNumber = (i > 0 && types[i - 1] == BIDI_EN) || (end < count && types[end] == BIDI_EN);
		if (touchesNumber)
			std::fill(types.begin() + i, types.begin() + end, BIDI_EN);
		i = end;
	}

	// W6: leftover separators and terminators are neutral.
	// W7: a number after left-to-right text (or at the start of a left-to-right line) is L.
	BidiType lastStrong = base;
	for (size_t i = 0; i < count; ++i)
	{
		BidiType &t = types[i];
		if (t == BIDI_ES || t == BIDI_ET || t == BIDI_CS)
			t = BIDI_N;
		if (t == BIDI_L || t == BIDI_R)
			lastStrong = t;
		else if (t == BIDI_EN && lastStrong == BIDI_L)
			t = BIDI_L;
	}

	// N1/N2: a run of neutrals between two sides of the same direction takes it (numbers count
	// as right-to-left here), else it takes the paragraph's.
	for (size_t i = 0; i < count;)
	{
		if (types[i] != BIDI_N)
		{
			++i;
			continue;
		}
		size_t end = i;
		while (end < count && types[end] == BIDI_N)
			++end;
		const BidiType before = i > 0 ? (types[i - 1] == BIDI_L ? BIDI_L : BIDI_R) : base;
		const BidiType after = end < count ? (types[end] == BIDI_L ? BIDI_L : BIDI_R) : base;
		std::fill(types.begin() + i, types.begin() + end, before == after ? before : base);
		i = end;
	}

	// I1/I2.
	for (size_t i = 0; i < count; ++i)
	{
		if (rtlBase)
			levels[i] = types[i] == BIDI_R ? 1 : 2;
		else
			levels[i] = types[i] == BIDI_L ? 0 : (types[i] == BIDI_R ? 1 : 2);
	}

	// L1: trailing whitespace goes back to the paragraph level.
	for (size_t i = count; i > 0 && isWhitespace(text[i - 1]); --i)
		levels[i - 1] = rtlBase ? 1 : 0;
}

void visualOrder(const unsigned char *levels, size_t count, int *order)
{
	unsigned char highest = 0;
	unsigned char lowestOdd = 255;
	for (size_t i = 0; i < count; ++i)
	{
		order[i] = (int)i;
		highest = std::max(highest, levels[i]);
		if (levels[i] & 1)
			lowestOdd = std::min(lowestOdd, levels[i]);
	}

	for (int level = highest; level >= (int)lowestOdd && level > 0; --level)
	{
		for (size_t i = 0; i < count;)
		{
			if (levels[order[i]] < level)
			{
				++i;
				continue;
			}
			size_t end = i;
			while (end < count && levels[order[end]] >= level)
				++end;
			std::reverse(order + i, order + end);
			i = end;
		}
	}
}

unsigned int mirroredBracket(unsigned int c)
{
	switch (c)
	{
		case 0x0028: return 0x0029;
		case 0x0029: return 0x0028;
		case 0x003C: return 0x003E;
		case 0x003E: return 0x003C;
		case 0x005B: return 0x005D;
		case 0x005D: return 0x005B;
		case 0x007B: return 0x007D;
		case 0x007D: return 0x007B;
		case 0x00AB: return 0x00BB;
		case 0x00BB: return 0x00AB;
		case 0x2039: return 0x203A;
		case 0x203A: return 0x2039;
		default: return 0;
	}
}

bool looksLegacyVisual(const std::wstring &text)
{
	size_t start = 0;
	while (start <= text.size())
	{
		size_t end = text.find(L'\n', start);
		if (end == std::wstring::npos)
			end = text.size();
		if (lineLooksVisual(text.data() + start, end - start))
			return true;
		start = end + 1;
	}
	return false;
}

std::wstring legacyVisualToLogical(const std::wstring &text)
{
	bool anyArabic = false;
	for (wchar_t c : text)
	{
		if (isArabicLetter(c) || isPresentationForm(c))
		{
			anyArabic = true;
			break;
		}
	}
	if (!anyArabic)
		return text;

	std::wstring out;
	out.reserve(text.size());
	size_t start = 0;
	while (start <= text.size())
	{
		size_t end = text.find(L'\n', start);
		if (end == std::wstring::npos)
			end = text.size();
		appendLogicalLine(out, text.data() + start, end - start);
		if (end < text.size())
			out.push_back(L'\n');
		start = end + 1;
	}
	return out;
}

std::wstring logicalToLegacyVisual(const std::wstring &text, bool preferRtl)
{
	std::wstring out;
	out.reserve(text.size());
	size_t start = 0;
	while (start <= text.size())
	{
		size_t end = text.find(L'\n', start);
		if (end == std::wstring::npos)
			end = text.size();
		appendLegacyVisualLine(out, text.data() + start, end - start, preferRtl);
		if (end < text.size())
			out.push_back(L'\n');
		start = end + 1;
	}
	return out;
}

} // namespace RtlText
