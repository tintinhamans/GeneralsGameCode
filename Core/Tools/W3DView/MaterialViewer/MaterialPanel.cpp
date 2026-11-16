/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

// TheSuperHackers @feature W3DView material viewer Qt panel.
// Read-only replica of the 3ds Max W3D material editor layout (max2w3d plugin):
// Surface Type / Pass Count rollups, then one rollup per pass with Vertex
// Material / Shader / Textures tabs. Fed from raw chunk data (W3dMaterialData.h).
//
// This translation unit is compiled into the core_w3dview_qtpanel static
// library: no MFC, no WW3D, no tool PCH. Everything stays moc-free (no
// Q_OBJECT); the one interactive control connects through a lambda.

#include "MaterialPanel.h"

#include <cctype>
#include <utility>
#include <vector>

#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QFileInfo>
#include <QtCore/QMap>
#include <QtCore/QPointF>
#include <QtCore/QSize>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QColor>
#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtGui/QKeySequence>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPen>
#include <QtGui/QPixmap>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QUndoCommand>
#include <QtWidgets/QUndoStack>
#include <QtWidgets/QVBoxLayout>

#include <dwmapi.h>
#pragma comment(lib, "dwmapi")
#include <shlobj.h>

#include <functional>

namespace W3dMaterialViewer
{

namespace
{

// Notifies the host (MFC frame) when the mesh combo selection changes.
void (*g_MeshSelectedCallback)(const char *) = nullptr;

// Edit-mode host callbacks (see MaterialPanel.h).
bool (*g_EditGateCallback)() = nullptr;
bool (*g_SaveCallback)(const MaterialDocument &) = nullptr;
void (*g_RevertCallback)() = nullptr;
void (*g_DirtyChangedCallback)(bool) = nullptr;
void (*g_ResolveTextureCallback)(TextureData &) = nullptr;
void (*g_LivePreviewCallback)(const MaterialDocument &) = nullptr;

// Sticky Raw/Table choice for the mapper-args editor, so the mode survives a
// page rebuild (e.g. undo/redo, which re-runs Show_Mesh). false = Raw.
bool g_ArgsTableMode = false;

// File tab bar host callbacks (see MaterialPanel.h). Fired only for USER
// interaction; g_TabBarUpdating suppresses them during programmatic updates.
void (*g_TabChangedCallback)(int) = nullptr;
void (*g_TabCloseRequestedCallback)(int) = nullptr;
bool g_TabBarUpdating = false;

//////////////////////////////////////////////////////////////////////////////
//	Field help -> status strip
//////////////////////////////////////////////////////////////////////////////

// The one-line help strip at the bottom of the panel. Populated on hover/focus
// from each control's status tip (set via Set_Help). Long field descriptions
// read far better here than in a hover tooltip.
QLabel *g_StatusLabel = nullptr;

void Show_Help(const QString &text)
{
	if (g_StatusLabel != nullptr) {
		g_StatusLabel->setText(text.isEmpty() ? QStringLiteral(" ") : text);
	}
}

// Attaches help text to a control. Uses the status-tip slot (so it is not a
// hover tooltip) and the shared event filter surfaces it in the status strip.
void Set_Help(QWidget *widget, const QString &text)
{
	if (widget != nullptr) {
		widget->setStatusTip(text);
	}
}

// Single filter installed on the panel; on Enter/FocusIn of a descendant it
// pushes that widget's status tip to the strip, and clears on Leave. No moc:
// eventFilter is a plain virtual override.
class HelpEventFilter : public QObject
{
public:
	explicit HelpEventFilter(QObject *parent) : QObject(parent) {}

protected:
	bool eventFilter(QObject *object, QEvent *event) override
	{
		if (event->type() == QEvent::Enter || event->type() == QEvent::FocusIn) {
			QWidget *widget = qobject_cast<QWidget *>(object);
			while (widget != nullptr && widget->statusTip().isEmpty()) {
				widget = widget->parentWidget();
			}
			if (widget != nullptr) {
				Show_Help(widget->statusTip());
			}
		}
		return QObject::eventFilter(object, event);
	}
};

// Restores the status strip to a combo's own help when its popup closes, so a
// hover-highlighted option description does not outlive the popup (Esc close,
// click-away). Installed on the popup view by Wire_Option_Hover_Help. No moc:
// eventFilter is a plain virtual override.
class ComboPopupHelpFilter : public QObject
{
public:
	explicit ComboPopupHelpFilter(QComboBox *combo) : QObject(combo), m_Combo(combo) {}

protected:
	bool eventFilter(QObject *object, QEvent *event) override
	{
		if (event->type() == QEvent::Hide) {
			Show_Help(m_Combo->statusTip());
		}
		return QObject::eventFilter(object, event);
	}

private:
	QComboBox *m_Combo;
};

// Shows each option's description in the status strip while the popup is open
// and the mouse (or arrow keys) highlights it. `help_for` maps an option index
// to its help text.
void Wire_Option_Hover_Help(QComboBox *combo, std::function<QString(int)> help_for)
{
	QObject::connect(combo, QOverload<int>::of(&QComboBox::highlighted),
		[help_for](int value) { Show_Help(help_for(value)); });
	combo->view()->installEventFilter(new ComboPopupHelpFilter(combo));
}

//////////////////////////////////////////////////////////////////////////////
//	Painted glyph icons (Qt5's SP_Dialog* standard icons are empty pixmaps on
//	the Windows style, so the +/x buttons would render blank). These paint a
//	crisp, DPI-aware glyph in the requested color and follow the theme.
//////////////////////////////////////////////////////////////////////////////

// Paints a QIcon at the given logical size for the current device pixel ratio.
// `glyph`: 0 = plus (+), 1 = cross (x). `color` is the stroke color.
QIcon Make_Glyph_Icon(int glyph, const QColor &color, int size = 12)
{
	const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
	QPixmap pm(QSize(size, size) * dpr);
	pm.setDevicePixelRatio(dpr);
	pm.fill(Qt::transparent);

	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing, true);
	QPen pen(color);
	pen.setWidthF(size / 8.0);				// scales with icon size
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);

	const qreal m = size * 0.22;				// inset margin
	const qreal lo = m, hi = size - m, mid = size / 2.0;
	if (glyph == 0) {						// plus
		p.drawLine(QPointF(mid, lo), QPointF(mid, hi));
		p.drawLine(QPointF(lo, mid), QPointF(hi, mid));
	} else {								// cross
		p.drawLine(QPointF(lo, lo), QPointF(hi, hi));
		p.drawLine(QPointF(hi, lo), QPointF(lo, hi));
	}
	p.end();
	return QIcon(pm);
}

//////////////////////////////////////////////////////////////////////////////
//	Mapper arg-key templates (per mapping type), from the OpenW3D docs
//////////////////////////////////////////////////////////////////////////////

// Index matches MAPPING_TYPES below. Each entry is the default key=value
// template inserted into an EMPTY Args box when that mapping type is chosen;
// an empty string means the mapper reads no args. Keys/defaults follow the
// per-mapper table documented in docs README (OpenW3D spec).
const char *const MAPPER_ARG_TEMPLATES[] = {
	"",																// 0  UV
	"",																// 1  Environment
	"",																// 2  Classic Environment
	"UPerSec=0.0\nVPerSec=0.0\nUScale=1.0\nVScale=1.0",				// 3  Screen
	"UPerSec=0.0\nVPerSec=0.0\nUScale=1.0\nVScale=1.0",				// 4  Linear Offset
	"",																// 5  Silhouette (no parser)
	"UScale=1.0\nVScale=1.0",										// 6  Scale
	"FPS=1.0\nLog2Width=0\nLast=0",									// 7  Grid
	"Speed=0.1\nUCenter=0.0\nVCenter=0.0\nUScale=1.0\nVScale=1.0",	// 8  Rotate
	"UAmp=1.0\nUFreq=1.0\nUPhase=0.0\nVAmp=1.0\nVFreq=1.0\nVPhase=0.0",	// 9  Sine
	"UStep=0.0\nVStep=0.0\nSPS=0.0",								// 10 Step
	"UPerSec=0.0\nVPerSec=0.0\nPeriod=0.0",							// 11 ZigZag
	"",																// 12 WS Classic Env
	"",																// 13 WS Environment
	"FPS=1.0\nLog2Width=0\nLast=0",									// 14 Grid Classic Env
	"FPS=1.0\nLog2Width=0\nLast=0",									// 15 Grid Environment
	"FPS=1.0\nUPerSec=0.0\nVPerSec=0.0",							// 16 Random
	"VPerSec=0.0\nVStart=0.0\nUseReflect=false",					// 17 Edge
	"BumpRotation=0.0\nBumpScale=1.0",								// 18 Bump Env
	"",																// 19 Grid WS Classic Env (no parser)
	"",																// 20 Grid WS Env (no parser)
};

// Returns the template "Key=Value" lines for `mappingType` whose keys are NOT
// already present in `currentArgs`. Used to show dimmed/italic ghost guidance of
// the args a freshly chosen mapper type expects, without clobbering real args.
static std::vector<std::pair<std::string, std::string> >
Missing_Template_Args(int mappingType, const std::string &currentArgs)
{
	std::vector<std::pair<std::string, std::string> > missing;
	if (mappingType < 0
			|| mappingType >= (int)(sizeof(MAPPER_ARG_TEMPLATES) / sizeof(MAPPER_ARG_TEMPLATES[0]))) {
		return missing;
	}
	const char *tmpl = MAPPER_ARG_TEMPLATES[mappingType];
	if (tmpl[0] == '\0') {
		return missing;
	}

	// Collect existing keys (lower-cased) from the current args string.
	std::vector<std::string> have;
	{
		std::string line;
		std::string src = currentArgs;
		size_t start = 0;
		while (start <= src.size()) {
			size_t nl = src.find('\n', start);
			line = (nl == std::string::npos) ? src.substr(start) : src.substr(start, nl - start);
			size_t eq = line.find('=');
			std::string key = (eq == std::string::npos) ? line : line.substr(0, eq);
			// trim
			while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(key.begin());
			while (!key.empty() && (key.back() == ' ' || key.back() == '\t' || key.back() == '\r')) key.pop_back();
			for (char &c : key) c = (char)tolower((unsigned char)c);
			if (!key.empty()) have.push_back(key);
			if (nl == std::string::npos) break;
			start = nl + 1;
		}
	}

	// Walk template lines; keep those whose key is not already present.
	std::string t = tmpl;
	size_t start = 0;
	while (start <= t.size()) {
		size_t nl = t.find('\n', start);
		std::string line = (nl == std::string::npos) ? t.substr(start) : t.substr(start, nl - start);
		size_t eq = line.find('=');
		if (eq != std::string::npos) {
			std::string key = line.substr(0, eq);
			std::string val = line.substr(eq + 1);
			std::string lkey = key;
			for (char &c : lkey) c = (char)tolower((unsigned char)c);
			bool present = false;
			for (const std::string &h : have) { if (h == lkey) { present = true; break; } }
			if (!present) missing.push_back(std::make_pair(key, val));
		}
		if (nl == std::string::npos) break;
		start = nl + 1;
	}
	return missing;
}

// Per-key help for mapper args, surfaced in the status strip when a Table-mode
// row is hovered/focused. Text is sourced from the engine mapper spec,
// Core/Libraries/Source/WWVegas/WW3D2/MAPPERS.TXT. Match is case-insensitive;
// returns nullptr for unknown keys (leaves the status strip on the generic Args
// help). Keys shared across mappers (UPerSec, UScale, FPS...) describe once.
static const char *Mapper_Arg_Help(const std::string &key)
{
	std::string k = key;
	while (!k.empty() && (k.front() == ' ' || k.front() == '\t')) k.erase(k.begin());
	while (!k.empty() && (k.back() == ' ' || k.back() == '\t' || k.back() == '\r')) k.pop_back();
	for (char &c : k) c = (char)tolower((unsigned char)c);

	struct Entry { const char *key; const char *help; };
	static const Entry TABLE[] = {
		{ "upersec",      "UPerSec: U-coordinate scroll speed in units per second." },
		{ "vpersec",      "VPerSec: V-coordinate scroll speed in units per second." },
		{ "uscale",       "UScale: scale factor applied to the U texture coordinate." },
		{ "vscale",       "VScale: scale factor applied to the V texture coordinate." },
		{ "fps",          "FPS: animation rate in frames per second." },
		{ "log2width",    "Log2Width: grid subdivision as a power of two (0 = 1 wide, 1 = 2, 2 = 4...); default divides the texture into quarters." },
		{ "last",         "Last: last grid frame to use (default = GridWidth * GridWidth)." },
		{ "speed",        "Speed: rotation rate in Hertz (1 = one rotation per second)." },
		{ "ucenter",      "UCenter: U coordinate of the center of rotation." },
		{ "vcenter",      "VCenter: V coordinate of the center of rotation." },
		{ "uamp",         "UAmp: amplitude of the U sine offset (Lissajous figure)." },
		{ "ufreq",        "UFreq: frequency of the U sine offset." },
		{ "uphase",       "UPhase: phase of the U sine offset." },
		{ "vamp",         "VAmp: amplitude of the V sine offset (Lissajous figure)." },
		{ "vfreq",        "VFreq: frequency of the V sine offset." },
		{ "vphase",       "VPhase: phase of the V sine offset." },
		{ "ustep",        "UStep: discrete U offset applied per step." },
		{ "vstep",        "VStep: discrete V offset applied per step." },
		{ "sps",          "SPS: steps per second." },
		{ "period",       "Period: time in seconds to complete one zigzag." },
		{ "usereflect",   "UseReflect: TRUE uses the reflection vector, FALSE the normal, to access the U coordinate." },
		{ "vstart",       "VStart: starting V coordinate for the Edge mapper." },
		{ "bumprotation", "BumpRotation: bump-matrix rotation rate in Hertz (1 = one rotation per second)." },
		{ "bumpscale",    "BumpScale: scale factor applied to the bumps." },
	};
	for (const Entry &e : TABLE) {
		if (k == e.key) return e.help;
	}
	return nullptr;
}

// Per-type description for the Mapping Type combo, surfaced in the status strip
// when a type is selected. Index matches MAPPING_TYPES / MAPPER_ARG_TEMPLATES.
// Text is sourced from the engine mapper spec, MAPPERS.TXT. An empty string
// falls back to the generic Mapping Type help.
const char *const MAPPING_TYPE_DESCS[] = {
	"Uses the mesh's own UV coordinates directly, with no transformation.",	// 0  UV
	"Uses the reflection direction to look up the environment map.",	// 1  Environment
	"Uses the surface normals to look up the environment map.",		// 2  Classic Environment
	"Uses the screen coordinate as the UV coordinate.",				// 3  Screen
	"Scrolls the texture at the speed specified.",					// 4  Linear Offset
	"Obsolete, not supported.",										// 5  Silhouette
	"Scales the UV coordinates. Useful for detail mapping.",		// 6  Scale
	"Animates a grid-divided texture left-to-right, top-to-bottom (like reading text). The grid must divide evenly.",	// 7  Grid
	"Rotates the texture counterclockwise about a center, then scales it.",	// 8  Rotate
	"Moves the texture in the shape of a Lissajous figure.",		// 9  Sine
	"Like Linear Offset, but moves in discrete steps.",				// 10 Step
	"Like Linear Offset, but reverses direction periodically.",		// 11 ZigZag
	"World-space normal environment map.",							// 12 WS Classic Env
	"World-space reflection environment map.",						// 13 WS Environment
	"Animated normal environment map.",								// 14 Grid Classic Env
	"Animated reflection environment map.",							// 15 Grid Environment
	"Randomly rotates and translates a texture with linear offset.",	// 16 Random
	"Uses the Z of the reflection/normal vector for the U coordinate; V is linear offset.",	// 17 Edge
	"Sets up (and can animate) the bump matrix; also has Linear Offset features. Use this even for a static bump matrix so it initializes to identity.",	// 18 Bump Env
	"Grid-animated world-space normal environment map.",			// 19 Grid WS Classic Env
	"Grid-animated world-space reflection environment map.",		// 20 Grid WS Env
};


//////////////////////////////////////////////////////////////////////////////
//	Field help text (source: W3D Hub 3ds Max tools documentation)
//////////////////////////////////////////////////////////////////////////////

namespace Help
{
	const char *const SURFACE_TYPE =
		"Surface type: used with an INI file to decide which effects (decals, "
		"emitters, sounds, geometry, animations) occur when something collides "
		"with this surface.";
	const char *const STATIC_SORT =
		"Static Sorting: tells the engine not to sort these polygons; the level "
		"picks which batch to render them in.";
	const char *const PASS_COUNT =
		"Pass count: number of layers (passes) the material uses; each pass has "
		"its own mapper, textures and colors. Max 4; all materials in a mesh "
		"must share the same count.";

	const char *const AMBIENT      = "Ambient: the color of the object's shaded portion.";
	const char *const DIFFUSE      = "Diffuse: the color the object reflects when lit; the object's base color.";
	const char *const SPECULAR     = "Specular: the color of the specular highlights.";
	const char *const EMISSIVE     = "Emissive: self-illumination base color, not affected by scene light.";
	const char *const SPEC_TO_DIFF = "Specular To Diffuse: makes the material a lamp that lights its surroundings with its emissive color times its texture.";
	const char *const OPACITY      = "Opacity: mesh opacity; 1 = fully opaque, 0 = fully transparent.";
	const char *const TRANSLUCENCY = "Translucency: how much light passes through the material.";
	const char *const SHININESS    = "Shininess: controls the tightness of the specular highlights.";
	const char *const MAPPING_TYPE = "Mapping type: how this stage's texture coordinates are generated.";
	const char *const MAPPER_ARGS  = "Args: mapper-specific key=value arguments (case sensitive) for the selected mapping type.";
	const char *const UV_CHANNEL   = "UV Channel: which UV channel this stage samples (derived from the args).";

	const char *const BLEND_MODE   = "Blend Mode: how this pass blends against the scene (a preset that sets Src/Dest/Alpha Test).";
	const char *const SRC_BLEND    = "Custom Src: the source blend factor.";
	const char *const DEST_BLEND   = "Custom Dest: the destination blend factor.";
	const char *const WRITE_ZBUF   = "Write Z Buffer: whether this pass writes to the depth buffer.";
	const char *const ALPHA_TEST   = "Alpha Test: whether alpha testing is enabled for this pass.";
	const char *const DEPTH_CMP    = "Depth Cmp: how the pixel's depth is compared against the Z buffer to decide whether to draw.";
	const char *const PRI_GRAD     = "Pri Gradient: what to do with the diffuse lighting.";
	const char *const SEC_GRAD     = "Sec Gradient: controls the specular lighting.";
	const char *const DETAIL_CLR   = "Detail Colour: how the stage-1 texture color combines with stage 0.";
	const char *const DETAIL_ALPHA = "Detail Alpha: how the stage-1 texture alpha combines with stage 0.";

	const char *const TEX_ENABLED  = "Enabled: whether this texture stage uses a bitmap.";
	const char *const TEX_NAME     = "Texture filename, resolved through the texture search paths.";
	const char *const TEX_CLAMP_U  = "Clamp U: clamp the map across the U coordinate to reduce seams.";
	const char *const TEX_CLAMP_V  = "Clamp V: clamp the map across the V coordinate to reduce seams.";
	const char *const TEX_NO_LOD   = "No LOD: no level-of-detail (mipmaps) for this texture.";
	const char *const TEX_PUBLISH  = "Publish: mark this a runtime-swappable texture.";
	const char *const TEX_RESIZE   = "Resize: obsolete.";
	const char *const TEX_ALPHA    = "Alpha Bitmap: reduce the alpha channel to 1-bit (a compression hint).";
	const char *const TEX_FRAMES   = "Frames: number of frames for an animated (TGA-sequence) texture.";
	const char *const TEX_FPS      = "FPS: frame rate for the animated frames.";
	const char *const TEX_ANIM     = "Anim Mode: playback of the animated frames (Loop / Ping-Pong / Once / Manual).";
	const char *const TEX_HINT     = "Pass Hint: what the texture is for (Base / Emissive Light Map / Environment Map / Shinyness Map).";
}

// Returns the status-tip help for a mapping type: the per-type description when
// one exists (what the selected mapper does), else the generic help. The two are
// not concatenated — the specific description already implies the generic point.
static QString Mapping_Type_Help(int mappingType)
{
	QString generic = QString::fromLatin1(Help::MAPPING_TYPE);
	if (mappingType < 0
			|| mappingType >= (int)(sizeof(MAPPING_TYPE_DESCS) / sizeof(MAPPING_TYPE_DESCS[0]))) {
		return generic;
	}
	const char *desc = MAPPING_TYPE_DESCS[mappingType];
	if (desc[0] == '\0') {
		return generic;
	}
	return QString::fromLatin1(desc);
}

// Same idea for any combo with a parallel per-value description table: the
// selected value's description when one exists, else the field's generic help.
static QString Option_Help(const char *generic, const char *const *descs, int count, int value)
{
	if (value < 0 || value >= count || descs[value][0] == '\0') {
		return QString::fromLatin1(generic);
	}
	return QString::fromLatin1(descs[value]);
}

// Opens the file's folder in Explorer with the file selected
void Show_In_Explorer(const std::string &path)
{
	if (path.empty()) {
		return;
	}

	QString native = QDir::toNativeSeparators(QFileInfo(QString::fromLocal8Bit(path.c_str())).absoluteFilePath());
	PIDLIST_ABSOLUTE item = ::ILCreateFromPathW((const wchar_t *)native.utf16());
	if (item != nullptr) {
		::SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
		::ILFree(item);
	}
}

// Tracks the active theme so popups opened from the panel (the swatch color
// dialog) can request a dark titlebar via DWM.
bool g_DarkTheme = false;

void Apply_Dark_Title_Bar(HWND hwnd)
{
	if (!g_DarkTheme || hwnd == nullptr) {
		return;
	}
	BOOL dark = TRUE;
	// 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (19 on pre-20H1 builds).
	if (FAILED(::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
		::DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
	}
}

//////////////////////////////////////////////////////////////////////////////
//	Option label tables
//////////////////////////////////////////////////////////////////////////////

// SURFACE_TYPE_STRINGS order from w3d_file.h — the authority for this engine.
const char *const SURFACE_TYPES[] = {
	"Light Metal", "Heavy Metal", "Water", "Sand", "Dirt", "Mud", "Grass",
	"Wood", "Concrete", "Flesh", "Rock", "Snow", "Ice", "Default", "Glass",
	"Cloth", "Tiberium Field", "Foliage Permeable", "Glass Permeable",
	"Ice Permeable", "Cloth Permeable", "Electrical", "Flammable", "Steam",
	"Electrical Permeable", "Flammable Permeable", "Steam Permeable",
	"Water Permeable", "Tiberium Water", "Tiberium Water Permeable",
	"Underwater Dirt", "Underwater Tiberium Dirt",
};

// W3DVERTMAT_STAGEn_MAPPING_* order; labels match the Max plugin dropdown.
const char *const MAPPING_TYPES[] = {
	"UV", "Environment", "Classic Environment", "Screen", "Linear Offset",
	"Silhouette", "Scale", "Grid", "Rotate", "Sine", "Step", "ZigZag",
	"WS Classic Env", "WS Environment", "Grid Classic Env", "Grid Environment",
	"Random", "Edge", "Bump Env", "Grid WS Classic Env", "Grid WS Env",
};

const char *const DEPTH_COMPARE[] = {
	"Pass Never", "Pass Less", "Pass Equal", "Pass LEqual",
	"Pass Greater", "Pass NEqual", "Pass GEqual", "Pass Always",
};

const char *const SRC_BLEND[] = {
	"Zero", "One", "Src Alpha", "One Minus Src Alpha",
};

const char *const DEST_BLEND[] = {
	"Zero", "One", "Src Colour", "One Minus Src Colour",
	"Src Alpha", "One Minus Src Alpha", "Src Colour Pre-Fog",
};

const char *const PRI_GRADIENT[] = {
	"Disable", "Modulate", "Add", "Bump-Environment", "Bump-Env Luminance", "Modulate 2X",
};

const char *const SEC_GRADIENT[] = { "Disabled", "Enabled" };

const char *const DETAIL_COLOR[] = {
	"Disable", "Detail", "Scale", "InvScale", "Add", "Sub", "SubR", "Blend",
	"DetailBlend", "AddSigned", "AddSigned2X", "Scale2X", "ModAlphaAddClr",
};

const char *const DETAIL_ALPHA[] = { "Disable", "Detail", "Scale", "InvScale" };

// Per-value descriptions for the Shader tab's Advanced combos, surfaced in the
// status strip when a value is selected (same pattern as MAPPING_TYPE_DESCS).
// Semantics from ShaderClass in shader.h; "detail" is the stage-1 texture.
const char *const DEPTH_COMPARE_DESCS[] = {
	"Never draws the pixel.",
	"Draws the pixel only when it is closer than the Z buffer.",
	"Draws the pixel only when its depth equals the Z buffer.",
	"Draws the pixel when it is closer than or equal to the Z buffer (default).",
	"Draws the pixel only when it is farther than the Z buffer.",
	"Draws the pixel only when its depth differs from the Z buffer.",
	"Draws the pixel when it is farther than or equal to the Z buffer.",
	"Always draws the pixel, ignoring the Z buffer.",
};

const char *const PRI_GRADIENT_DESCS[] = {
	"Ignores lighting: the texture is drawn unlit (like a decal).",
	"Multiplies the pixel by the lighting color and alpha (default).",
	"Adds the lighting RGB to the pixel; the lighting alpha replaces the pixel alpha.",
	"Environment-mapped bump mapping.",
	"Environment-mapped bump mapping with luminance control.",
	"Multiplies the pixel by the lighting, then doubles the RGB (over-brightens).",
};

const char *const SEC_GRADIENT_DESCS[] = {
	"No specular highlights are drawn (default).",
	"Adds the specular lighting RGB on top of the pixel.",
};

const char *const DETAIL_COLOR_DESCS[] = {
	"Detail texture color is ignored; the base color is used as is (default).",
	"Replaces the base color with the detail texture's color.",
	"Multiplies the base color by the detail color.",
	"Inverse multiply: base + (1 - base) * detail; brightens without clipping.",
	"Adds the detail color to the base color.",
	"Subtracts the detail color from the base color.",
	"Subtracts the base color from the detail color.",
	"Blends base and detail using the base alpha: base*A + detail*(1-A).",
	"Blends base and detail using the detail alpha: base*A + detail*(1-A).",
	"Adds the colors and subtracts 0.5 (signed add).",
	"Adds the colors, subtracts 0.5, then doubles the result.",
	"Multiplies the colors, then doubles the result.",
	"Base + base alpha * detail (modulate alpha, add color).",
};

const char *const DETAIL_ALPHA_DESCS[] = {
	"Detail texture alpha is ignored; the base alpha is used as is (default).",
	"Replaces the base alpha with the detail texture's alpha.",
	"Multiplies the base alpha by the detail alpha.",
	"Inverse multiply: base + (1 - base) * detail; raises alpha without clipping.",
};

const char *const BLEND_MODES[] = {
	"Opaque", "Add", "Multiply", "Multiply and Add", "Screen",
	"Alpha Blend", "Alpha Test", "Alpha Test and Blend", "Custom",
};

const char *const ANIM_MODES[] = { "Loop", "Ping-Pong", "Once", "Manual" };

const char *const PASS_HINTS[] = {
	"Base Texture", "Emissive Light Map", "Environment Map", "Shinyness Map",
};

// W3DTEXTURE_* attribute bits (w3d_file.h; duplicated here to stay WW3D-free).
const uint16_t TEX_PUBLISH = 0x0001;
const uint16_t TEX_RESIZE_OBSOLETE = 0x0002;
const uint16_t TEX_NO_LOD = 0x0004;
const uint16_t TEX_CLAMP_U = 0x0008;
const uint16_t TEX_CLAMP_V = 0x0010;
const uint16_t TEX_ALPHA_BITMAP = 0x0020;
const uint16_t TEX_HINT_MASK = 0xff00;
const int TEX_HINT_SHIFT = 8;

// W3DVERTMAT_* bits.
const uint32_t VERTMAT_COPY_SPECULAR_TO_DIFFUSE = 0x00000004;
const uint32_t VERTMAT_STAGE0_MAPPING_MASK = 0x00FF0000;
const int VERTMAT_STAGE0_MAPPING_SHIFT = 16;
const uint32_t VERTMAT_STAGE1_MAPPING_MASK = 0x0000FF00;
const int VERTMAT_STAGE1_MAPPING_SHIFT = 8;

template<int N>
QString Label_For(const char *const (&table)[N], int index)
{
	if (index >= 0 && index < N) {
		return QString::fromLatin1(table[index]);
	}
	return QStringLiteral("Unknown (%1)").arg(index);
}

// Derives the Max "Blend Mode" preset from (src, dest, alphaTest); index into
// BLEND_MODES, 8 = Custom. Reverse of the max2w3d preset table.
int Derive_Blend_Mode(int src, int dest, int alpha_test)
{
	struct { int src, dest, alphaTest; } const presets[] = {
		{ 1, 0, 0 },	// Opaque:              One / Zero
		{ 1, 1, 0 },	// Add:                 One / One
		{ 0, 2, 0 },	// Multiply:            Zero / Src Colour
		{ 1, 2, 0 },	// Multiply and Add:    One / Src Colour
		{ 1, 3, 0 },	// Screen:              One / One Minus Src Colour
		{ 2, 5, 0 },	// Alpha Blend:         Src Alpha / One Minus Src Alpha
		{ 1, 0, 1 },	// Alpha Test:          One / Zero + test
		{ 2, 5, 1 },	// Alpha Test and Blend
	};
	for (int i = 0; i < 8; i++) {
		if (presets[i].src == src && presets[i].dest == dest && presets[i].alphaTest == (alpha_test ? 1 : 0)) {
			return i;
		}
	}
	return 8;	// Custom
}

//////////////////////////////////////////////////////////////////////////////
//	Control factory helpers
//////////////////////////////////////////////////////////////////////////////
//
// Each factory takes an optional change callback. When it is null the control
// is built read-only (View mode, unchanged look). When it is set the control
// is interactive (Edit mode) and reports edits through the callback, which
// writes the new value into the in-memory document and marks it dirty.

// Blocks interaction while keeping the normal (non-grayed) look.
void Make_Read_Only(QWidget *widget)
{
	widget->setFocusPolicy(Qt::NoFocus);
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);
}

// Passed by value into the change lambdas; carried through every builder.
using ChangeFn = std::function<void()>;

// `on_change(index)` is called with the selected combo index in Edit mode.
template<int N>
QComboBox *Make_Combo(const char *const (&table)[N], int index,
	std::function<void(int)> on_change = nullptr)
{
	QComboBox *combo = new QComboBox;
	for (int i = 0; i < N; i++) {
		combo->addItem(QString::fromLatin1(table[i]));
	}
	if (index >= 0 && index < N) {
		combo->setCurrentIndex(index);
	} else {
		combo->addItem(QStringLiteral("Unknown (%1)").arg(index));
		combo->setCurrentIndex(N);
	}
	if (on_change) {
		QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
			[on_change](int value) { on_change(value); });
	} else {
		Make_Read_Only(combo);
	}
	return combo;
}

// A Make_Combo whose status-strip help tracks the selected value's description
// (from a parallel `descs` table, falling back to `generic`). In edit mode the
// selection is stored into `field` and the strip refreshes immediately — the
// combo keeps focus, so the hover/focus filter would not re-fire on its own.
// The combo pointer lives in a heap holder captured by value because the
// change lambda runs long after this function returns.
template<int N>
QComboBox *Make_Described_Combo(const char *const (&table)[N],
	const char *const (&descs)[N], const char *generic,
	int index, uint8_t *field, bool edit, ChangeFn dirty)
{
	const char *const *desc_table = descs;
	auto holder = std::make_shared<QComboBox *>(nullptr);

	std::function<void(int)> on_change;
	if (edit) {
		on_change = [field, dirty, desc_table, generic, holder](int value) {
			*field = (uint8_t)value;
			QString tip = Option_Help(generic, desc_table, N, value);
			if (*holder != nullptr) {
				Set_Help(*holder, tip);
			}
			Show_Help(tip);
			if (dirty) dirty();
		};
	}

	QComboBox *combo = Make_Combo(table, index, on_change);
	*holder = combo;
	Set_Help(combo, Option_Help(generic, desc_table, N, index));
	Wire_Option_Hover_Help(combo, [desc_table, generic](int value) {
		return Option_Help(generic, desc_table, N, value);
	});
	return combo;
}

QCheckBox *Make_Check(const char *label, bool checked,
	std::function<void(bool)> on_change = nullptr)
{
	QCheckBox *check = new QCheckBox(QString::fromLatin1(label));
	check->setChecked(checked);
	if (on_change) {
		QObject::connect(check, &QCheckBox::toggled,
			[on_change](bool value) { on_change(value); });
	} else {
		Make_Read_Only(check);
	}
	return check;
}

QDoubleSpinBox *Make_Float_Spin(double value, double min_value, double max_value,
	int decimals = 3, std::function<void(double)> on_change = nullptr)
{
	QDoubleSpinBox *spin = new QDoubleSpinBox;
	spin->setRange(min_value, max_value);
	spin->setDecimals(decimals);
	spin->setValue(value);
	if (on_change) {
		QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			[on_change](double v) { on_change(v); });
	} else {
		Make_Read_Only(spin);
	}
	return spin;
}

QSpinBox *Make_Int_Spin(int value, int min_value, int max_value,
	std::function<void(int)> on_change = nullptr)
{
	QSpinBox *spin = new QSpinBox;
	spin->setRange(min_value, max_value);
	spin->setValue(value);
	if (on_change) {
		QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
			[on_change](int v) { on_change(v); });
	} else {
		Make_Read_Only(spin);
	}
	return spin;
}

// Small color swatch in the Max style: a flat filled rectangle. Clicking it
// opens a (non-native, theme-following) color dialog.
// View mode: the dialog has no OK/Cancel and nothing is written back (inspect
// / copy only). Edit mode: OK/Cancel; on accept the new color is stored, the
// swatch and its sibling label repaint, and on_change reports the RGB.
class ColorSwatchWidget : public QFrame
{
public:
	ColorSwatchWidget(const QColor &color, std::function<void(const QColor &)> on_change)
		: m_Color(color), m_OnChange(std::move(on_change)) {}

	void Set_Sibling_Label(QLabel *label) { m_Label = label; }

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton) {
			bool editable = (bool)m_OnChange;
			QColorDialog dialog(m_Color, this);
			QColorDialog::ColorDialogOptions options = QColorDialog::DontUseNativeDialog;
			if (!editable) {
				options |= QColorDialog::NoButtons;
			}
			dialog.setOptions(options);
			dialog.setWindowTitle(editable ? QStringLiteral("Color")
				: QStringLiteral("Color (read-only)"));
			Apply_Dark_Title_Bar((HWND)dialog.winId());

			if (dialog.exec() == QDialog::Accepted && editable) {
				m_Color = dialog.currentColor();
				QPalette palette = this->palette();
				palette.setColor(QPalette::Window, m_Color);
				setPalette(palette);
				if (m_Label != nullptr) {
					m_Label->setText(QStringLiteral("%1, %2, %3")
						.arg(m_Color.red()).arg(m_Color.green()).arg(m_Color.blue()));
				}
				m_OnChange(m_Color);
			}
		}
		QFrame::mousePressEvent(event);
	}

private:
	QColor m_Color;
	std::function<void(const QColor &)> m_OnChange;
	QLabel *m_Label = nullptr;
};

// on_change (Edit mode) receives the new RGB as a QColor.
ColorSwatchWidget *Make_Color_Swatch(const uint8_t rgb[3],
	std::function<void(const QColor &)> on_change = nullptr)
{
	ColorSwatchWidget *swatch = new ColorSwatchWidget(QColor(rgb[0], rgb[1], rgb[2]), std::move(on_change));
	swatch->setFrameShape(QFrame::StyledPanel);
	swatch->setFixedSize(48, 18);
	swatch->setAutoFillBackground(true);
	QPalette palette = swatch->palette();
	palette.setColor(QPalette::Window, QColor(rgb[0], rgb[1], rgb[2]));
	swatch->setPalette(palette);
	swatch->setCursor(Qt::PointingHandCursor);
	QString tip = QStringLiteral("RGB (%1, %2, %3) %4 click to inspect/copy")
		.arg(rgb[0]).arg(rgb[1]).arg(rgb[2]).arg(QChar(0x2014));
	swatch->setToolTip(tip);
	return swatch;
}

// `store` (Edit mode) writes the picked RGB back into the document's uint8[3].
QWidget *Make_Color_Row(const uint8_t rgb[3], std::function<void(const uint8_t[3])> store = nullptr)
{
	QWidget *row = new QWidget;
	QHBoxLayout *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	QLabel *value = new QLabel(QStringLiteral("%1, %2, %3").arg(rgb[0]).arg(rgb[1]).arg(rgb[2]));
	value->setTextInteractionFlags(Qt::TextSelectableByMouse);
	value->setCursor(Qt::IBeamCursor);

	std::function<void(const QColor &)> on_change;
	if (store) {
		on_change = [store](const QColor &color) {
			uint8_t out[3] = { (uint8_t)color.red(), (uint8_t)color.green(), (uint8_t)color.blue() };
			store(out);
		};
	}
	ColorSwatchWidget *swatch = Make_Color_Swatch(rgb, std::move(on_change));
	swatch->Set_Sibling_Label(value);

	layout->addWidget(swatch);
	layout->addWidget(value);
	layout->addStretch(1);
	return row;
}

// Very small flat button that puts `text` on the clipboard when clicked.
QToolButton *Make_Copy_Button(const QString &text, const char *tooltip)
{
	QToolButton *copy = new QToolButton;
	copy->setText(QString(QChar(0x29C9)));	// "two joined squares" copy glyph
	copy->setFixedSize(18, 18);
	copy->setAutoRaise(true);
	copy->setToolTip(QString::fromLatin1(tooltip));
	QObject::connect(copy, &QToolButton::clicked, [text]() {
		QApplication::clipboard()->setText(text);
	});
	return copy;
}

//////////////////////////////////////////////////////////////////////////////
//	Section copy/paste
//////////////////////////////////////////////////////////////////////////////
//
// Each settings group (vertex material, stage mapping, blend, advanced) can be
// copied to the clipboard as a tagged "[W3DView.X]" key=value text block and
// pasted onto the same group of any mesh / pass / file. Plain text, so it also
// travels between two W3DView instances (and can be kept in a notes file).

// Mapper args are multi-line; keep the block one-line-per-key by escaping.
QString Escape_Section_Value(const std::string &value)
{
	QString out = QString::fromLatin1(value.c_str());
	out.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
	out.replace(QStringLiteral("\r"), QStringLiteral("\\r"));
	out.replace(QStringLiteral("\n"), QStringLiteral("\\n"));
	return out;
}

std::string Unescape_Section_Value(const QString &value)
{
	QString out;
	out.reserve(value.size());
	for (int i = 0; i < value.size(); i++) {
		QChar ch = value.at(i);
		if (ch == QChar('\\') && i + 1 < value.size()) {
			QChar next = value.at(++i);
			if (next == QChar('n'))       out += QChar('\n');
			else if (next == QChar('r'))  out += QChar('\r');
			else                          out += next;
		} else {
			out += ch;
		}
	}
	return out.toLatin1().constData();
}

QString Serialize_Section(const char *tag, const QList<QPair<QString, QString>> &fields)
{
	QString out = QStringLiteral("[") + QString::fromLatin1(tag) + QStringLiteral("]\n");
	for (const QPair<QString, QString> &field : fields) {
		out += field.first + QStringLiteral("=") + field.second + QStringLiteral("\n");
	}
	return out;
}

// Reads the clipboard; true only when it holds a "[tag]" block. Fills `out`
// with its key=value lines.
bool Parse_Section(const char *tag, QMap<QString, QString> &out)
{
	QStringList lines = QApplication::clipboard()->text().split(QChar('\n'));
	bool tag_seen = false;
	QString wanted = QStringLiteral("[") + QString::fromLatin1(tag) + QStringLiteral("]");
	for (QString line : lines) {
		line.remove(QChar('\r'));
		if (!tag_seen) {
			if (line.trimmed().isEmpty()) {
				continue;
			}
			if (line.trimmed() != wanted) {
				return false;
			}
			tag_seen = true;
			continue;
		}
		int eq = line.indexOf(QChar('='));
		if (eq > 0) {
			out.insert(line.left(eq).trimmed(), line.mid(eq + 1));
		}
	}
	return tag_seen;
}

// Field readers: apply only when the key is present and parses; out-of-range
// numbers are clamped so a hand-edited block cannot corrupt the document.
bool Parse_Int_Field(const QMap<QString, QString> &kv, const char *key,
	int min_value, int max_value, int &out)
{
	QMap<QString, QString>::const_iterator it = kv.find(QString::fromLatin1(key));
	if (it == kv.end()) {
		return false;
	}
	bool ok = false;
	int value = it.value().trimmed().toInt(&ok);
	if (!ok) {
		return false;
	}
	out = (value < min_value) ? min_value : ((value > max_value) ? max_value : value);
	return true;
}

bool Parse_Float_Field(const QMap<QString, QString> &kv, const char *key,
	float min_value, float max_value, float &out)
{
	QMap<QString, QString>::const_iterator it = kv.find(QString::fromLatin1(key));
	if (it == kv.end()) {
		return false;
	}
	bool ok = false;
	float value = it.value().trimmed().toFloat(&ok);
	if (!ok) {
		return false;
	}
	out = (value < min_value) ? min_value : ((value > max_value) ? max_value : value);
	return true;
}

QString Color_To_String(const uint8_t rgb[3])
{
	return QStringLiteral("%1,%2,%3").arg(rgb[0]).arg(rgb[1]).arg(rgb[2]);
}

bool Parse_Color_Field(const QMap<QString, QString> &kv, const char *key, uint8_t rgb[3])
{
	QMap<QString, QString>::const_iterator it = kv.find(QString::fromLatin1(key));
	if (it == kv.end()) {
		return false;
	}
	QStringList parts = it.value().split(QChar(','));
	if (parts.size() != 3) {
		return false;
	}
	uint8_t out[3];
	for (int i = 0; i < 3; i++) {
		bool ok = false;
		int value = parts[i].trimmed().toInt(&ok);
		if (!ok) {
			return false;
		}
		out[i] = (uint8_t)((value < 0) ? 0 : ((value > 255) ? 255 : value));
	}
	memcpy(rgb, out, 3);
	return true;
}

// Right-aligned [Copy][Paste] pair for one settings group. `make_text`
// serializes the group's CURRENT values at click time; `paste` applies the
// clipboard onto the group and returns false when it does not hold this
// group's block (pass null outside edit mode — the button disables).
// Feedback lands in the status strip.
QWidget *Make_Copy_Paste_Row(const QString &what,
	std::function<QString()> make_text, std::function<bool()> paste)
{
	QWidget *row = new QWidget;
	QHBoxLayout *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	layout->addStretch(1);

	QToolButton *copy_button = new QToolButton;
	copy_button->setText(QStringLiteral("Copy"));
	copy_button->setAutoRaise(true);
	copy_button->setFocusPolicy(Qt::NoFocus);
	copy_button->setFixedHeight(18);
	Set_Help(copy_button, QStringLiteral("Copy the %1 settings to the clipboard.").arg(what));
	QObject::connect(copy_button, &QToolButton::clicked, [what, make_text]() {
		QApplication::clipboard()->setText(make_text());
		Show_Help(QStringLiteral("Copied the %1 settings to the clipboard.").arg(what));
	});
	layout->addWidget(copy_button);

	QToolButton *paste_button = new QToolButton;
	paste_button->setText(QStringLiteral("Paste"));
	paste_button->setAutoRaise(true);
	paste_button->setFocusPolicy(Qt::NoFocus);
	paste_button->setFixedHeight(18);
	if (paste) {
		Set_Help(paste_button,
			QStringLiteral("Paste %1 settings from the clipboard onto this group.").arg(what));
		QObject::connect(paste_button, &QToolButton::clicked, [what, paste]() {
			if (paste()) {
				Show_Help(QStringLiteral("Pasted the %1 settings.").arg(what));
			} else {
				Show_Help(QStringLiteral("The clipboard does not contain %1 settings.").arg(what));
			}
		});
	} else {
		paste_button->setEnabled(false);
		paste_button->setToolTip(QStringLiteral("Switch to Edit mode to paste."));
		Set_Help(paste_button, QStringLiteral("Switch to Edit mode to paste %1 settings.").arg(what));
	}
	layout->addWidget(paste_button);

	return row;
}

// Applies the segmented-control stylesheet to a pair of buttons named
// "segLeft"/"segRight": flatten the seam corners into one divider, highlight
// the checked half, hover-tint the inactive half. Palette-driven for light/dark.
void Style_Segment_Pair(QWidget *left, QWidget *right)
{
	// Use the application palette (which the panel sets to the active theme in
	// Apply_Theme) rather than the button's own — a freshly created / not-yet-
	// parented button still carries the default light palette, which would make
	// the segments render light/white in dark mode.
	QPalette pal = QApplication::palette();
	QColor border = pal.color(QPalette::Mid);
	QColor face = pal.color(QPalette::Button);
	QColor text = pal.color(QPalette::ButtonText);
	QColor sel = pal.color(QPalette::Highlight);
	QColor selText = pal.color(QPalette::HighlightedText);
	QColor hover = g_DarkTheme ? face.lighter(130) : face.darker(108);

	QString css = QStringLiteral(
		"QPushButton#segLeft, QPushButton#segRight {"
		"  border: 1px solid %1; padding: 3px 12px; color: %2;"
		"  background: %3; }"
		"QPushButton#segLeft { border-top-left-radius: 4px;"
		"  border-bottom-left-radius: 4px; border-right: none; }"
		"QPushButton#segRight { border-top-right-radius: 4px;"
		"  border-bottom-right-radius: 4px; }"
		"QPushButton#segLeft:checked, QPushButton#segRight:checked {"
		"  background: %4; color: %5; }"
		"QPushButton#segLeft:hover:!checked, QPushButton#segRight:hover:!checked {"
		"  background: %6; }")
		.arg(border.name(), text.name(), face.name(),
			 sel.name(), selText.name(), hover.name());

	left->setStyleSheet(css);
	right->setStyleSheet(css);
}

// Builds a glued, mutually-exclusive two-button segmented toggle (like the
// View/Edit control). `left`/`right` receive the two checkable buttons; the
// left one starts checked. The caller connects toggled() for behavior.
QWidget *Make_Segmented_Toggle(const QString &leftText, const QString &rightText,
	QPushButton *&left, QPushButton *&right)
{
	left = new QPushButton(leftText);
	left->setCheckable(true);
	left->setChecked(true);
	left->setObjectName(QStringLiteral("segLeft"));
	right = new QPushButton(rightText);
	right->setCheckable(true);
	right->setObjectName(QStringLiteral("segRight"));

	QButtonGroup *group = new QButtonGroup(left);	// parented so it lives with the pair
	group->setExclusive(true);
	group->addButton(left);
	group->addButton(right);

	QWidget *row = new QWidget;
	QHBoxLayout *hl = new QHBoxLayout(row);
	hl->setContentsMargins(0, 0, 0, 0);
	hl->setSpacing(0);
	hl->addWidget(left);
	hl->addWidget(right);

	Style_Segment_Pair(left, right);
	return row;
}

// Parses "UVSourceChannel=<n>" out of a mapper args string; 1 when absent.
int Parse_UV_Channel(const std::string &args)
{
	const char KEY[] = "UVSourceChannel";
	size_t pos = args.find(KEY);
	if (pos == std::string::npos) {
		return 1;
	}
	pos = args.find('=', pos);
	if (pos == std::string::npos) {
		return 1;
	}
	return atoi(args.c_str() + pos + 1);
}

//////////////////////////////////////////////////////////////////////////////
//	Tab builders (one mesh, one pass)
//////////////////////////////////////////////////////////////////////////////
//
// Every builder takes an EditCtx. In View mode (edit == false) it holds no
// mutable pointers and all controls are built read-only, exactly as before.
// In Edit mode it carries a pointer to the mesh being shown (owned by the
// panel's document copy) plus a markDirty callback; controls mutate the
// document structs in place and mark it dirty. Values-only editing never
// resizes the document vectors, so the raw pointers stay valid for the
// lifetime of the built page.

struct EditCtx
{
	bool				edit = false;
	MeshMaterialData	*mesh = nullptr;	// mutable when edit; else null
	ChangeFn			markDirty;			// commit one undoable edit (discrete controls)
	ChangeFn			markTyping;			// commit a coalescing edit (text boxes)
	ChangeFn			refreshPage;		// rebuild the page (deferred) after a multi-field paste
};

QWidget *Build_Stage_Mapping_Group(const EditCtx &ctx, VertexMaterialData &material, int stage)
{
	QGroupBox *group = new QGroupBox(stage == 0
		? QStringLiteral("Stage 0 Mapping") : QStringLiteral("Stage 1 Mapping"));
	QFormLayout *form = new QFormLayout(group);

	uint32_t mask = (stage == 0) ? VERTMAT_STAGE0_MAPPING_MASK : VERTMAT_STAGE1_MAPPING_MASK;
	int shift = (stage == 0) ? VERTMAT_STAGE0_MAPPING_SHIFT : VERTMAT_STAGE1_MAPPING_SHIFT;
	std::string *args = (stage == 0) ? &material.mapperArgs0 : &material.mapperArgs1;
	int mapping = (int)((material.attributes & mask) >> shift);

	VertexMaterialData *material_ptr = &material;
	ChangeFn dirty = ctx.markDirty;
	ChangeFn typing = ctx.markTyping;

	// Copy/Paste for this stage's mapping (Type + Args). The block format is
	// shared between stages, so stage 0 can be pasted onto stage 1 and back.
	{
		std::string *args_for_copy = args;
		std::function<QString()> make_text = [material_ptr, mask, shift, args_for_copy]() {
			QList<QPair<QString, QString>> fields;
			fields << qMakePair(QStringLiteral("type"),
				QString::number((int)((material_ptr->attributes & mask) >> shift)));
			fields << qMakePair(QStringLiteral("args"), Escape_Section_Value(*args_for_copy));
			return Serialize_Section("W3DView.StageMapping", fields);
		};

		std::function<bool()> paste;
		if (ctx.edit) {
			ChangeFn refresh = ctx.refreshPage;
			paste = [material_ptr, mask, shift, args_for_copy, dirty, refresh]() -> bool {
				QMap<QString, QString> kv;
				if (!Parse_Section("W3DView.StageMapping", kv)) {
					return false;
				}
				int type = 0;
				if (Parse_Int_Field(kv, "type", 0,
						(int)(sizeof(MAPPING_TYPES) / sizeof(MAPPING_TYPES[0])) - 1, type)) {
					material_ptr->attributes =
						(material_ptr->attributes & ~mask) | (((uint32_t)type << shift) & mask);
				}
				QMap<QString, QString>::const_iterator it = kv.find(QStringLiteral("args"));
				if (it != kv.end()) {
					*args_for_copy = Unescape_Section_Value(it.value());
				}
				if (dirty) dirty();
				if (refresh) refresh();
				return true;
			};
		}
		form->addRow(QString(), Make_Copy_Paste_Row(QStringLiteral("stage mapping"), make_text, paste));
	}

	// Current mapping type, shared so the Type combo, the Table ghost rows, and
	// the Raw ghost hint all track the latest selection.
	auto cur_type = std::make_shared<int>(mapping);

	// Build the args editor first so the Type combo can prefill it on change.
	// It has two views over the same key=value string: a raw multi-line text box
	// and a "pretty" grid of editable Key / Value fields. A toggle swaps between
	// them; whichever is active writes back to *args and rebuilds the other.
	std::string *args_ptr = args;
	QPlainTextEdit *args_edit = new QPlainTextEdit(QString::fromStdString(*args));
	args_edit->setReadOnly(!ctx.edit);
	args_edit->setFixedHeight(56);
	Set_Help(args_edit, QString::fromLatin1(Help::MAPPER_ARGS));

	// Dimmed italic hint under the Raw box: the template keys the selected type
	// expects that aren't present yet ("Suggested: Key=Value, ..."). Refreshed
	// when the type changes; hidden when nothing is missing.
	QLabel *raw_hint = new QLabel;
	{
		QFont f = raw_hint->font();
		f.setItalic(true);
		raw_hint->setFont(f);
		raw_hint->setWordWrap(true);
		QPalette pal = raw_hint->palette();
		QColor c = pal.color(QPalette::WindowText);
		c.setAlpha(120);	// lower opacity
		pal.setColor(QPalette::WindowText, c);
		raw_hint->setPalette(pal);
	}
	auto refresh_raw_hint = [raw_hint, args_ptr, cur_type]() {
		auto miss = Missing_Template_Args(*cur_type, *args_ptr);
		if (miss.empty()) {
			raw_hint->clear();
			raw_hint->setVisible(false);
			return;
		}
		QStringList parts;
		for (const auto &kv : miss) {
			parts << (QString::fromStdString(kv.first) + QStringLiteral("=")
				+ QString::fromStdString(kv.second));
		}
		raw_hint->setText(QStringLiteral("Suggested: ") + parts.join(QStringLiteral(", ")));
		raw_hint->setVisible(true);
	};

	// Pretty (key/value grid) view.
	QWidget *pretty = new QWidget;
	QVBoxLayout *pretty_layout = new QVBoxLayout(pretty);
	pretty_layout->setContentsMargins(0, 0, 0, 0);
	pretty_layout->setSpacing(2);

	// Raw page = the text box plus the dimmed suggestion hint beneath it.
	QWidget *raw_page = new QWidget;
	QVBoxLayout *raw_page_layout = new QVBoxLayout(raw_page);
	raw_page_layout->setContentsMargins(0, 0, 0, 0);
	raw_page_layout->setSpacing(2);
	raw_page_layout->addWidget(args_edit);
	raw_page_layout->addWidget(raw_hint);

	QStackedWidget *args_stack = new QStackedWidget;
	args_stack->addWidget(raw_page);	// index 0 = raw
	args_stack->addWidget(pretty);		// index 1 = pretty

	// A QStackedWidget sizes to the LARGEST page, so a tall Table page would keep
	// the stack tall after switching back to Raw. Make only the current page
	// contribute to the vertical size hint; hidden pages are Ignored and collapse.
	QObject::connect(args_stack, &QStackedWidget::currentChanged,
		[args_stack](int index) {
			for (int i = 0; i < args_stack->count(); i++) {
				QWidget *page = args_stack->widget(i);
				QSizePolicy sp = page->sizePolicy();
				sp.setVerticalPolicy(i == index ? QSizePolicy::Preferred
												: QSizePolicy::Ignored);
				page->setSizePolicy(sp);
			}
			args_stack->updateGeometry();
		});
	// Apply the initial policy (Raw is page 0): collapse the Table page's height.
	{
		QSizePolicy sp = pretty->sizePolicy();
		sp.setVerticalPolicy(QSizePolicy::Ignored);
		pretty->setSizePolicy(sp);
	}

	// Raw | Pretty segmented toggle (same design as the View/Edit control):
	// Raw = multi-line text, Pretty = editable Key/Value grid. Declared here so
	// mapping_change can reference the Pretty button; wired up below.
	QPushButton *raw_btn = nullptr;
	QPushButton *pretty_toggle = nullptr;
	QWidget *args_mode_row = Make_Segmented_Toggle(
		QStringLiteral("Raw"), QStringLiteral("Table"), raw_btn, pretty_toggle);
	raw_btn->setToolTip(QStringLiteral("Edit mapper args as raw Key=Value text"));
	pretty_toggle->setToolTip(QStringLiteral("Edit mapper args as a Key / Value table"));

	// Rebuilds the pretty grid from the current *args string. Each non-empty
	// "Key=Value" line becomes a row of two line edits; edits rewrite *args.
	auto rebuild_pretty = [pretty, pretty_layout, args_ptr, args_edit, typing,
			cur_type, edit = ctx.edit]() {
		// Clear existing rows.
		QLayoutItem *item;
		while ((item = pretty_layout->takeAt(0)) != nullptr) {
			if (item->widget()) item->widget()->deleteLater();
			delete item;
		}

		// Serialize the current field rows back into *args and the raw box.
		// Ghost (suggestion) rows use objectNames gk/gv and are skipped until
		// the user edits them (which promotes them to real k/v fields).
		auto serialize = [pretty_layout, args_ptr, args_edit, typing]() {
			QString out;
			for (int i = 0; i < pretty_layout->count(); i++) {
				QWidget *row = pretty_layout->itemAt(i)->widget();
				if (row == nullptr) continue;
				QLineEdit *k = row->findChild<QLineEdit *>(QStringLiteral("k"));
				QLineEdit *v = row->findChild<QLineEdit *>(QStringLiteral("v"));
				if (k == nullptr) continue;
				QString key = k->text().trimmed();
				if (key.isEmpty()) continue;
				out += key + QStringLiteral("=") + (v ? v->text().trimmed() : QString());
				out += QStringLiteral("\n");
			}
			if (out.endsWith('\n')) out.chop(1);
			*args_ptr = out.toStdString();
			{ QSignalBlocker b(args_edit); args_edit->setPlainText(out); }
			if (typing) typing();
		};

		// Points a Key / Value cell pair's status tip at the per-key mapper help
		// (from MAPPERS.TXT), so hovering or focusing the row surfaces it in the
		// status strip. Re-called on key edits so the help tracks the typed key.
		auto apply_key_help = [](QLineEdit *k, QLineEdit *v) {
			const char *help = Mapper_Arg_Help(k->text().toStdString());
			QString tip = help ? QString::fromLatin1(help) : QString::fromLatin1(Help::MAPPER_ARGS);
			k->setStatusTip(tip);
			if (v) v->setStatusTip(tip);
		};

		// One editable Key / Value row, with a delete (x) button in edit mode.
		auto add_row = [pretty, pretty_layout, edit, serialize, apply_key_help](const QString &key, const QString &value) {
			QWidget *row = new QWidget;
			QHBoxLayout *hl = new QHBoxLayout(row);
			hl->setContentsMargins(0, 0, 0, 0);
			hl->setSpacing(2);
			// Left spacer the width of a ghost row's "+" button, so real rows'
			// Key/Value columns line up with the suggestion rows below them.
			if (edit) {
				QWidget *spacer = new QWidget;
				spacer->setFixedSize(18, 18);
				hl->addWidget(spacer, 0);
			}
			QLineEdit *k = new QLineEdit(key);
			k->setObjectName(QStringLiteral("k"));
			k->setPlaceholderText(QStringLiteral("Key"));
			QLineEdit *v = new QLineEdit(value);
			v->setObjectName(QStringLiteral("v"));
			v->setPlaceholderText(QStringLiteral("Value"));
			k->setReadOnly(!edit);
			v->setReadOnly(!edit);
			apply_key_help(k, v);
			if (edit) {
				QObject::connect(k, &QLineEdit::textChanged, [serialize]() { serialize(); });
				QObject::connect(v, &QLineEdit::textChanged, [serialize]() { serialize(); });
				QObject::connect(k, &QLineEdit::textChanged, [k, v, apply_key_help]() { apply_key_help(k, v); });
			}
			hl->addWidget(k, 1);
			hl->addWidget(v, 1);
			if (edit) {
				QToolButton *del = new QToolButton;
				del->setIcon(Make_Glyph_Icon(1,	// cross
					QApplication::palette().color(QPalette::WindowText)));
				del->setIconSize(QSize(12, 12));
				del->setObjectName(QStringLiteral("del"));
				del->setFixedSize(18, 18);
				del->setAutoRaise(true);
				del->setToolTip(QStringLiteral("Remove this key/value"));
				// Delete the row, then re-serialize (removed row is skipped).
				QObject::connect(del, &QToolButton::clicked, [row, serialize]() {
					row->setParent(nullptr);
					row->deleteLater();
					serialize();
				});
				hl->addWidget(del, 0);
			}
			pretty_layout->addWidget(row);
		};

		// Parse the raw string into key/value rows.
		QString raw = QString::fromStdString(*args_ptr);
		const QStringList lines = raw.split('\n', Qt::SkipEmptyParts);
		for (const QString &line : lines) {
			int eq = line.indexOf('=');
			if (eq < 0) {
				add_row(line.trimmed(), QString());
			} else {
				add_row(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
			}
		}
		if (edit) {
			// A trailing blank row lets the user add a new key/value pair.
			add_row(QString(), QString());

			// Ghost (suggestion) rows: the selected type's template keys that are
			// not present yet, shown italic + dimmed. Their fields use gk/gv so
			// serialize ignores them; editing one promotes it to a real k/v pair.
			auto missing = Missing_Template_Args(*cur_type, *args_ptr);
			for (const auto &kv : missing) {
				QWidget *row = new QWidget;
				QHBoxLayout *hl = new QHBoxLayout(row);
				hl->setContentsMargins(0, 0, 0, 0);
				hl->setSpacing(2);
				// "+" button on the left promotes this dimmed suggestion into a
				// real key/value row (added to the args). Declared first so it can
				// be captured by commit_ghost and hidden once promoted.
				QToolButton *add = new QToolButton;
				add->setIcon(Make_Glyph_Icon(0,	// plus
					QApplication::palette().color(QPalette::WindowText)));
				add->setIconSize(QSize(12, 12));
				add->setObjectName(QStringLiteral("add"));
				add->setFixedSize(18, 18);
				add->setAutoRaise(true);
				add->setToolTip(QStringLiteral("Add this suggested key/value"));

				QLineEdit *gk = new QLineEdit(QString::fromStdString(kv.first));
				gk->setObjectName(QStringLiteral("gk"));
				QLineEdit *gv = new QLineEdit(QString::fromStdString(kv.second));
				gv->setObjectName(QStringLiteral("gv"));

				// Italic + lower-opacity styling to read as a suggestion.
				for (QLineEdit *le : { gk, gv }) {
					QFont f = le->font();
					f.setItalic(true);
					le->setFont(f);
					QPalette pal = le->palette();
					QColor c = pal.color(QPalette::Text);
					c.setAlpha(120);
					pal.setColor(QPalette::Text, c);
					le->setPalette(pal);
				}
				gk->setToolTip(QStringLiteral("Suggested key for this mapper type %1 use + or edit to add").arg(QChar(0x2014)));
				gv->setToolTip(QStringLiteral("Suggested value %1 use + or edit to add").arg(QChar(0x2014)));
				apply_key_help(gk, gv);

				// Turns the dimmed suggestion into a real row: solid style, k/v
				// object names (so serialize() picks it up), hide the "+" button,
				// then serialize. Idempotent — re-running on a promoted row no-ops.
				auto commit_ghost = [row, gk, gv, add, serialize]() {
					if (gk->objectName() == QStringLiteral("k")) return;	// already promoted
					gk->setObjectName(QStringLiteral("k"));
					gv->setObjectName(QStringLiteral("v"));
					for (QLineEdit *le : { gk, gv }) {
						QFont f = le->font();
						f.setItalic(false);
						le->setFont(f);
						// Restore full-opacity text from the inherited palette.
						le->setPalette(QApplication::palette());
					}
					add->hide();	// no longer a suggestion; a real row is deleted via 'x' logic
					serialize();
				};

				// Edit-driven promotion: only when the user actually changed a field
				// and then left it (Tab/Enter). A per-field "touched" flag guards
				// against promoting a ghost merely tabbed through. Typing alone does
				// not write the suggestion; the "+" button commits it unconditionally.
				auto touched = std::make_shared<bool>(false);
				auto promote = [commit_ghost, touched]() {
					if (!*touched) return;			// tabbed through, unchanged
					commit_ghost();
				};
				QObject::connect(add, &QToolButton::clicked, [commit_ghost]() { commit_ghost(); });
				QObject::connect(gk, &QLineEdit::textEdited, [touched]() { *touched = true; });
				QObject::connect(gv, &QLineEdit::textEdited, [touched]() { *touched = true; });
				QObject::connect(gk, &QLineEdit::textChanged, [gk, gv, apply_key_help]() { apply_key_help(gk, gv); });
				QObject::connect(gk, &QLineEdit::editingFinished, [promote]() { promote(); });
				QObject::connect(gv, &QLineEdit::editingFinished, [promote]() { promote(); });
				// Once promoted (objectName == k), edits serialize live like any
				// real row; before that (still a ghost), typing only sets touched.
				QObject::connect(gk, &QLineEdit::textChanged, [gk, serialize]() {
					if (gk->objectName() == QStringLiteral("k")) serialize();
				});
				QObject::connect(gv, &QLineEdit::textChanged, [gk, serialize]() {
					if (gk->objectName() == QStringLiteral("k")) serialize();
				});

				hl->addWidget(add, 0);
				hl->addWidget(gk, 1);
				hl->addWidget(gv, 1);
				// Right spacer matching a real row's delete (x) button width, so the
				// Value column ends at the same place as the real rows above.
				QWidget *rspacer = new QWidget;
				rspacer->setFixedSize(18, 18);
				hl->addWidget(rspacer, 0);
				pretty_layout->addWidget(row);
			}
		}
	};

	if (ctx.edit) {
		QObject::connect(args_edit, &QPlainTextEdit::textChanged,
			[args_edit, args_ptr, typing, refresh_raw_hint]() {
				*args_ptr = args_edit->toPlainText().toStdString();
				refresh_raw_hint();		// a newly typed key drops out of "missing"
				if (typing) typing();	// coalesced: one undo step per typing burst
			});
	}
	refresh_raw_hint();	// initial suggestion state

	// Holder so mapping_change (defined before the combo exists) can refresh the
	// combo's status tip with the newly selected type's description.
	auto type_combo_holder = std::make_shared<QComboBox *>(nullptr);

	std::function<void(int)> mapping_change;
	if (ctx.edit) {
		mapping_change = [material_ptr, mask, shift, dirty, args_edit, args_ptr,
				rebuild_pretty, pretty_toggle, cur_type, refresh_raw_hint,
				type_combo_holder](int value) {
			material_ptr->attributes = (material_ptr->attributes & ~mask)
				| (((uint32_t)value << shift) & mask);
			*cur_type = value;

			// Update the combo's status help to the new type's description, and
			// push it to the strip now (the combo already has focus, so the
			// hover/focus filter won't re-fire on its own).
			if (*type_combo_holder != nullptr) {
				QString tip = Mapping_Type_Help(value);
				Set_Help(*type_combo_holder, tip);
				Show_Help(tip);
			}

			// Never auto-write args on a type change: the suggested keys for the
			// new type are surfaced as DIMMED guidance only (the Raw hint line and,
			// in Table mode, the ghost rows). The user opts in per key via the
			// Table-mode "+" button (or by editing a ghost row). This avoids the
			// bug where switching away from UV (empty template) into a mapper with
			// a template silently injected real lines into an empty Args box.
			refresh_raw_hint();
			if (pretty_toggle->isChecked()) rebuild_pretty();
			if (dirty) dirty();
		};
	}
	QComboBox *type_combo = Make_Combo(MAPPING_TYPES, mapping, mapping_change);
	*type_combo_holder = type_combo;
	Set_Help(type_combo, Mapping_Type_Help(mapping));
	Wire_Option_Hover_Help(type_combo, [](int value) { return Mapping_Type_Help(value); });
	form->addRow(QStringLiteral("Type:"), type_combo);

	// Wire the Pretty button (Raw is its exclusive twin). Selecting Pretty
	// rebuilds the grid from the latest text and shows it; Raw shows the text.
	// The choice is remembered globally so it survives a page rebuild (undo/redo
	// re-runs Show_Mesh, which recreates this editor).
	QObject::connect(pretty_toggle, &QPushButton::toggled,
		[args_stack, rebuild_pretty](bool on) {
			g_ArgsTableMode = on;
			if (on) rebuild_pretty();		// (re)build grid from the latest text
			args_stack->setCurrentIndex(on ? 1 : 0);
		});

	QWidget *args_row = new QWidget;
	QVBoxLayout *args_col = new QVBoxLayout(args_row);
	args_col->setContentsMargins(0, 0, 0, 0);
	args_col->setSpacing(2);

	// Raw | Table toggle sits above the editor.
	args_col->addWidget(args_mode_row, 0, Qt::AlignLeft);

	QHBoxLayout *args_top = new QHBoxLayout;
	args_top->setContentsMargins(0, 0, 0, 0);
	args_top->setSpacing(2);
	args_top->addWidget(args_stack, 1);
	args_top->addWidget(Make_Copy_Button(QString::fromStdString(*args), "Copy mapper args"),
		0, Qt::AlignTop);
	args_col->addLayout(args_top);

	form->addRow(QStringLiteral("Args:"), args_row);

	// Restore the sticky Raw/Table choice (default Raw). Only meaningful in edit
	// mode; the toggled handler builds/shows the matching view.
	if (ctx.edit && g_ArgsTableMode) {
		pretty_toggle->setChecked(true);
	}

	// UV Channel stays derived (read-only) even in edit mode; it lives inside
	// the args text, which the user edits directly.
	QSpinBox *uv_spin = Make_Int_Spin(Parse_UV_Channel(*args), 1, 99);
	Set_Help(uv_spin, QString::fromLatin1(Help::UV_CHANNEL));
	form->addRow(QStringLiteral("UV Channel:"), uv_spin);
	return group;
}

QWidget *Build_Vertex_Material_Tab(const EditCtx &ctx, MeshMaterialData &mesh, const PassData &pass)
{
	QWidget *tab = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(tab);

	static VertexMaterialData DEFAULT_MATERIAL;
	bool has_material = (pass.vertexMaterialIndex >= 0
		&& pass.vertexMaterialIndex < (int)mesh.vertexMaterials.size());
	VertexMaterialData &material =
		has_material ? mesh.vertexMaterials[pass.vertexMaterialIndex] : DEFAULT_MATERIAL;

	// Only edit real materials (never the shared default placeholder).
	EditCtx mat_ctx = ctx;
	mat_ctx.edit = ctx.edit && has_material;
	ChangeFn dirty = mat_ctx.edit ? ctx.markDirty : ChangeFn();

	if (!material.name.empty()) {
		QLabel *name = new QLabel(QString::fromStdString(material.name));
		// Bold via QFont, not a stylesheet — a stylesheet detaches the label
		// from the palette and its text goes black in dark mode.
		QFont font = name->font();
		font.setBold(true);
		name->setFont(font);
		layout->addWidget(name);
	}

	VertexMaterialData *material_ptr = &material;

	// Copy/Paste for the vertex-material settings: colors, Specular To
	// Diffuse, opacity, translucency, shininess. The stage mappings are a
	// separate block with their own buttons below.
	{
		std::function<QString()> make_text = [material_ptr]() {
			QList<QPair<QString, QString>> fields;
			fields << qMakePair(QStringLiteral("ambient"), Color_To_String(material_ptr->ambient));
			fields << qMakePair(QStringLiteral("diffuse"), Color_To_String(material_ptr->diffuse));
			fields << qMakePair(QStringLiteral("specular"), Color_To_String(material_ptr->specular));
			fields << qMakePair(QStringLiteral("emissive"), Color_To_String(material_ptr->emissive));
			fields << qMakePair(QStringLiteral("specToDiffuse"), QString::number(
				(material_ptr->attributes & VERTMAT_COPY_SPECULAR_TO_DIFFUSE) != 0 ? 1 : 0));
			fields << qMakePair(QStringLiteral("opacity"), QString::number(material_ptr->opacity, 'f', 6));
			fields << qMakePair(QStringLiteral("translucency"), QString::number(material_ptr->translucency, 'f', 6));
			fields << qMakePair(QStringLiteral("shininess"), QString::number(material_ptr->shininess, 'f', 6));
			return Serialize_Section("W3DView.VertexMaterial", fields);
		};

		std::function<bool()> paste;
		if (mat_ctx.edit) {
			ChangeFn refresh = mat_ctx.refreshPage;
			paste = [material_ptr, dirty, refresh]() -> bool {
				QMap<QString, QString> kv;
				if (!Parse_Section("W3DView.VertexMaterial", kv)) {
					return false;
				}
				Parse_Color_Field(kv, "ambient", material_ptr->ambient);
				Parse_Color_Field(kv, "diffuse", material_ptr->diffuse);
				Parse_Color_Field(kv, "specular", material_ptr->specular);
				Parse_Color_Field(kv, "emissive", material_ptr->emissive);
				int flag = 0;
				if (Parse_Int_Field(kv, "specToDiffuse", 0, 1, flag)) {
					if (flag) material_ptr->attributes |= VERTMAT_COPY_SPECULAR_TO_DIFFUSE;
					else      material_ptr->attributes &= ~VERTMAT_COPY_SPECULAR_TO_DIFFUSE;
				}
				Parse_Float_Field(kv, "opacity", 0.0F, 1.0F, material_ptr->opacity);
				Parse_Float_Field(kv, "translucency", 0.0F, 1.0F, material_ptr->translucency);
				Parse_Float_Field(kv, "shininess", 0.0F, 1.0F, material_ptr->shininess);
				if (dirty) dirty();
				if (refresh) refresh();
				return true;
			};
		}
		layout->addWidget(Make_Copy_Paste_Row(QStringLiteral("vertex material"), make_text, paste));
	}

	QFormLayout *form = new QFormLayout;
	QWidget *ambient_row = Make_Color_Row(material.ambient,
		mat_ctx.edit ? [material_ptr, dirty](const uint8_t c[3]) {
			memcpy(material_ptr->ambient, c, 3); if (dirty) dirty();
		} : std::function<void(const uint8_t[3])>());
	Set_Help(ambient_row, QString::fromLatin1(Help::AMBIENT));
	form->addRow(QStringLiteral("Ambient:"), ambient_row);
	QWidget *diffuse_row = Make_Color_Row(material.diffuse,
		mat_ctx.edit ? [material_ptr, dirty](const uint8_t c[3]) {
			memcpy(material_ptr->diffuse, c, 3); if (dirty) dirty();
		} : std::function<void(const uint8_t[3])>());
	Set_Help(diffuse_row, QString::fromLatin1(Help::DIFFUSE));
	form->addRow(QStringLiteral("Diffuse:"), diffuse_row);
	QWidget *specular_row = Make_Color_Row(material.specular,
		mat_ctx.edit ? [material_ptr, dirty](const uint8_t c[3]) {
			memcpy(material_ptr->specular, c, 3); if (dirty) dirty();
		} : std::function<void(const uint8_t[3])>());
	Set_Help(specular_row, QString::fromLatin1(Help::SPECULAR));
	form->addRow(QStringLiteral("Specular:"), specular_row);
	QWidget *emissive_row = Make_Color_Row(material.emissive,
		mat_ctx.edit ? [material_ptr, dirty](const uint8_t c[3]) {
			memcpy(material_ptr->emissive, c, 3); if (dirty) dirty();
		} : std::function<void(const uint8_t[3])>());
	Set_Help(emissive_row, QString::fromLatin1(Help::EMISSIVE));
	form->addRow(QStringLiteral("Emissive:"), emissive_row);
	layout->addLayout(form);

	QCheckBox *spec_to_diff = Make_Check("Specular To Diffuse",
		(material.attributes & VERTMAT_COPY_SPECULAR_TO_DIFFUSE) != 0,
		mat_ctx.edit ? [material_ptr, dirty](bool on) {
			if (on) material_ptr->attributes |= VERTMAT_COPY_SPECULAR_TO_DIFFUSE;
			else    material_ptr->attributes &= ~VERTMAT_COPY_SPECULAR_TO_DIFFUSE;
			if (dirty) dirty();
		} : std::function<void(bool)>());
	Set_Help(spec_to_diff, QString::fromLatin1(Help::SPEC_TO_DIFF));
	layout->addWidget(spec_to_diff);

	QFormLayout *value_form = new QFormLayout;
	QDoubleSpinBox *opacity_spin = Make_Float_Spin(material.opacity, 0.0, 1.0, 3,
		mat_ctx.edit ? [material_ptr, dirty](double v) {
			material_ptr->opacity = (float)v; if (dirty) dirty();
		} : std::function<void(double)>());
	Set_Help(opacity_spin, QString::fromLatin1(Help::OPACITY));
	value_form->addRow(QStringLiteral("Opacity:"), opacity_spin);
	QDoubleSpinBox *translucency_spin = Make_Float_Spin(material.translucency, 0.0, 1.0, 3,
		mat_ctx.edit ? [material_ptr, dirty](double v) {
			material_ptr->translucency = (float)v; if (dirty) dirty();
		} : std::function<void(double)>());
	Set_Help(translucency_spin, QString::fromLatin1(Help::TRANSLUCENCY));
	value_form->addRow(QStringLiteral("Translucency:"), translucency_spin);
	QDoubleSpinBox *shininess_spin = Make_Float_Spin(material.shininess, 0.0, 1.0, 3,
		mat_ctx.edit ? [material_ptr, dirty](double v) {
			material_ptr->shininess = (float)v; if (dirty) dirty();
		} : std::function<void(double)>());
	Set_Help(shininess_spin, QString::fromLatin1(Help::SHININESS));
	value_form->addRow(QStringLiteral("Shininess:"), shininess_spin);
	layout->addLayout(value_form);

	layout->addWidget(Build_Stage_Mapping_Group(mat_ctx, material, 0));
	layout->addWidget(Build_Stage_Mapping_Group(mat_ctx, material, 1));
	layout->addStretch(1);
	return tab;
}

QWidget *Build_Shader_Tab(const EditCtx &ctx, MeshMaterialData &mesh, const PassData &pass)
{
	QWidget *tab = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(tab);

	static ShaderData DEFAULT_SHADER;
	bool has_shader = (pass.shaderIndex >= 0 && pass.shaderIndex < (int)mesh.shaders.size());
	ShaderData &shader = has_shader ? mesh.shaders[pass.shaderIndex] : DEFAULT_SHADER;

	bool edit = ctx.edit && has_shader;
	ShaderData *shader_ptr = &shader;
	ChangeFn dirty = ctx.markDirty;

	QGroupBox *blend_group = new QGroupBox(QStringLiteral("Blend"));
	QFormLayout *blend_form = new QFormLayout(blend_group);

	// Copy/Paste for the blend settings (Src/Dest factors, Alpha Test, Write Z
	// Buffer — the Blend Mode preset is derived from these).
	{
		std::function<QString()> make_text = [shader_ptr]() {
			QList<QPair<QString, QString>> fields;
			fields << qMakePair(QStringLiteral("srcBlend"), QString::number(shader_ptr->srcBlend));
			fields << qMakePair(QStringLiteral("destBlend"), QString::number(shader_ptr->destBlend));
			fields << qMakePair(QStringLiteral("alphaTest"), QString::number(shader_ptr->alphaTest));
			fields << qMakePair(QStringLiteral("depthMask"), QString::number(shader_ptr->depthMask));
			return Serialize_Section("W3DView.Blend", fields);
		};

		std::function<bool()> paste;
		if (edit) {
			ChangeFn refresh = ctx.refreshPage;
			paste = [shader_ptr, dirty, refresh]() -> bool {
				QMap<QString, QString> kv;
				if (!Parse_Section("W3DView.Blend", kv)) {
					return false;
				}
				int value = 0;
				if (Parse_Int_Field(kv, "srcBlend", 0,
						(int)(sizeof(SRC_BLEND) / sizeof(SRC_BLEND[0])) - 1, value)) {
					shader_ptr->srcBlend = (uint8_t)value;
				}
				if (Parse_Int_Field(kv, "destBlend", 0,
						(int)(sizeof(DEST_BLEND) / sizeof(DEST_BLEND[0])) - 1, value)) {
					shader_ptr->destBlend = (uint8_t)value;
				}
				if (Parse_Int_Field(kv, "alphaTest", 0, 1, value)) {
					shader_ptr->alphaTest = (uint8_t)value;
				}
				if (Parse_Int_Field(kv, "depthMask", 0, 1, value)) {
					shader_ptr->depthMask = (uint8_t)value;
				}
				if (dirty) dirty();
				if (refresh) refresh();
				return true;
			};
		}
		blend_form->addRow(QString(), Make_Copy_Paste_Row(QStringLiteral("blend"), make_text, paste));
	}

	// The preset combo and the three raw controls update each other. The
	// controls are created before they can all be cross-referenced, so the
	// pointers live in a heap holder captured by value in every lambda (a
	// reference to the function's stack locals would dangle — the lambdas run
	// after this function returns).
	struct BlendRefs { QComboBox *preset = nullptr, *src = nullptr, *dest = nullptr; QCheckBox *alpha = nullptr; };
	auto refs = std::make_shared<BlendRefs>();

	int blend_mode = Derive_Blend_Mode(shader.srcBlend, shader.destBlend, shader.alphaTest);

	// Preset -> (src, dest, alphaTest); mirrors Derive_Blend_Mode's table.
	auto apply_preset = [](int preset, int &src, int &dest, int &at) {
		static const struct { int src, dest, at; } presets[] = {
			{ 1, 0, 0 }, { 1, 1, 0 }, { 0, 2, 0 }, { 1, 2, 0 },
			{ 1, 3, 0 }, { 2, 5, 0 }, { 1, 0, 1 }, { 2, 5, 1 },
		};
		if (preset >= 0 && preset < 8) {
			src = presets[preset].src;
			dest = presets[preset].dest;
			at = presets[preset].at;
		}
	};

	refs->preset = Make_Combo(BLEND_MODES, blend_mode,
		edit ? std::function<void(int)>([shader_ptr, dirty, apply_preset, refs](int preset) {
			if (preset >= 8) return;	// Custom: no forced values
			int src = shader_ptr->srcBlend, dest = shader_ptr->destBlend, at = shader_ptr->alphaTest;
			apply_preset(preset, src, dest, at);
			shader_ptr->srcBlend = (uint8_t)src;
			shader_ptr->destBlend = (uint8_t)dest;
			shader_ptr->alphaTest = (uint8_t)at;
			if (refs->src)   { QSignalBlocker b(refs->src);   refs->src->setCurrentIndex(src); }
			if (refs->dest)  { QSignalBlocker b(refs->dest);  refs->dest->setCurrentIndex(dest); }
			if (refs->alpha) { QSignalBlocker b(refs->alpha); refs->alpha->setChecked(at != 0); }
			if (dirty) dirty();
		}) : std::function<void(int)>());

	auto rederive_preset = [refs, shader_ptr]() {
		if (!refs->preset) return;
		int mode = Derive_Blend_Mode(shader_ptr->srcBlend, shader_ptr->destBlend, shader_ptr->alphaTest);
		QSignalBlocker b(refs->preset);
		refs->preset->setCurrentIndex(mode);
	};

	refs->src = Make_Combo(SRC_BLEND, shader.srcBlend,
		edit ? std::function<void(int)>([shader_ptr, dirty, rederive_preset](int v) {
			shader_ptr->srcBlend = (uint8_t)v; rederive_preset(); if (dirty) dirty();
		}) : std::function<void(int)>());
	refs->dest = Make_Combo(DEST_BLEND, shader.destBlend,
		edit ? std::function<void(int)>([shader_ptr, dirty, rederive_preset](int v) {
			shader_ptr->destBlend = (uint8_t)v; rederive_preset(); if (dirty) dirty();
		}) : std::function<void(int)>());
	refs->alpha = Make_Check("Alpha Test", shader.alphaTest != 0,
		edit ? std::function<void(bool)>([shader_ptr, dirty, rederive_preset](bool on) {
			shader_ptr->alphaTest = on ? 1 : 0; rederive_preset(); if (dirty) dirty();
		}) : std::function<void(bool)>());

	Set_Help(refs->preset, QString::fromLatin1(Help::BLEND_MODE));
	Set_Help(refs->src, QString::fromLatin1(Help::SRC_BLEND));
	Set_Help(refs->dest, QString::fromLatin1(Help::DEST_BLEND));
	Set_Help(refs->alpha, QString::fromLatin1(Help::ALPHA_TEST));
	blend_form->addRow(QStringLiteral("Blend Mode:"), refs->preset);
	blend_form->addRow(QStringLiteral("Custom Src:"), refs->src);
	blend_form->addRow(QStringLiteral("Custom Dest:"), refs->dest);
	QCheckBox *write_z = Make_Check("Write Z Buffer", shader.depthMask != 0,
		edit ? std::function<void(bool)>([shader_ptr, dirty](bool on) {
			shader_ptr->depthMask = on ? 1 : 0; if (dirty) dirty();
		}) : std::function<void(bool)>());
	Set_Help(write_z, QString::fromLatin1(Help::WRITE_ZBUF));
	blend_form->addRow(QString(), write_z);
	blend_form->addRow(QString(), refs->alpha);
	layout->addWidget(blend_group);

	QGroupBox *advanced_group = new QGroupBox(QStringLiteral("Advanced"));
	QFormLayout *advanced_form = new QFormLayout(advanced_group);

	// Copy/Paste for the Advanced settings (gradients, depth compare, detail
	// colour/alpha functions).
	{
		std::function<QString()> make_text = [shader_ptr]() {
			QList<QPair<QString, QString>> fields;
			fields << qMakePair(QStringLiteral("priGradient"), QString::number(shader_ptr->priGradient));
			fields << qMakePair(QStringLiteral("secGradient"), QString::number(shader_ptr->secGradient));
			fields << qMakePair(QStringLiteral("depthCompare"), QString::number(shader_ptr->depthCompare));
			fields << qMakePair(QStringLiteral("detailColorFunc"), QString::number(shader_ptr->detailColorFunc));
			fields << qMakePair(QStringLiteral("detailAlphaFunc"), QString::number(shader_ptr->detailAlphaFunc));
			return Serialize_Section("W3DView.Advanced", fields);
		};

		std::function<bool()> paste;
		if (edit) {
			ChangeFn refresh = ctx.refreshPage;
			paste = [shader_ptr, dirty, refresh]() -> bool {
				QMap<QString, QString> kv;
				if (!Parse_Section("W3DView.Advanced", kv)) {
					return false;
				}
				int value = 0;
				if (Parse_Int_Field(kv, "priGradient", 0,
						(int)(sizeof(PRI_GRADIENT) / sizeof(PRI_GRADIENT[0])) - 1, value)) {
					shader_ptr->priGradient = (uint8_t)value;
				}
				if (Parse_Int_Field(kv, "secGradient", 0,
						(int)(sizeof(SEC_GRADIENT) / sizeof(SEC_GRADIENT[0])) - 1, value)) {
					shader_ptr->secGradient = (uint8_t)value;
				}
				if (Parse_Int_Field(kv, "depthCompare", 0,
						(int)(sizeof(DEPTH_COMPARE) / sizeof(DEPTH_COMPARE[0])) - 1, value)) {
					shader_ptr->depthCompare = (uint8_t)value;
				}
				if (Parse_Int_Field(kv, "detailColorFunc", 0,
						(int)(sizeof(DETAIL_COLOR) / sizeof(DETAIL_COLOR[0])) - 1, value)) {
					shader_ptr->detailColorFunc = (uint8_t)value;
				}
				if (Parse_Int_Field(kv, "detailAlphaFunc", 0,
						(int)(sizeof(DETAIL_ALPHA) / sizeof(DETAIL_ALPHA[0])) - 1, value)) {
					shader_ptr->detailAlphaFunc = (uint8_t)value;
				}
				if (dirty) dirty();
				if (refresh) refresh();
				return true;
			};
		}
		advanced_form->addRow(QString(), Make_Copy_Paste_Row(QStringLiteral("advanced"), make_text, paste));
	}

	advanced_form->addRow(QStringLiteral("Pri Gradient:"),
		Make_Described_Combo(PRI_GRADIENT, PRI_GRADIENT_DESCS, Help::PRI_GRAD,
			shader.priGradient, &shader_ptr->priGradient, edit, dirty));
	advanced_form->addRow(QStringLiteral("Sec Gradient:"),
		Make_Described_Combo(SEC_GRADIENT, SEC_GRADIENT_DESCS, Help::SEC_GRAD,
			shader.secGradient, &shader_ptr->secGradient, edit, dirty));
	advanced_form->addRow(QStringLiteral("Depth Cmp:"),
		Make_Described_Combo(DEPTH_COMPARE, DEPTH_COMPARE_DESCS, Help::DEPTH_CMP,
			shader.depthCompare, &shader_ptr->depthCompare, edit, dirty));
	advanced_form->addRow(QStringLiteral("Detail Colour:"),
		Make_Described_Combo(DETAIL_COLOR, DETAIL_COLOR_DESCS, Help::DETAIL_CLR,
			shader.detailColorFunc, &shader_ptr->detailColorFunc, edit, dirty));
	advanced_form->addRow(QStringLiteral("Detail Alpha:"),
		Make_Described_Combo(DETAIL_ALPHA, DETAIL_ALPHA_DESCS, Help::DETAIL_ALPHA,
			shader.detailAlphaFunc, &shader_ptr->detailAlphaFunc, edit, dirty));
	layout->addWidget(advanced_group);
	layout->addStretch(1);
	return tab;
}

QWidget *Build_Texture_Stage_Group(const EditCtx &ctx, MeshMaterialData &mesh, const PassData &pass, int stage)
{
	QGroupBox *group = new QGroupBox(QStringLiteral("Stage %1 Texture").arg(stage));
	QVBoxLayout *layout = new QVBoxLayout(group);

	int texture_index = pass.stageTextureIndex[stage];
	bool present = (texture_index >= 0 && texture_index < (int)mesh.textures.size());

	// Enabling/disabling a stage is structural (v1 leaves it read-only).
	QCheckBox *enabled_check = Make_Check("Enabled", present);
	Set_Help(enabled_check, QString::fromLatin1(Help::TEX_ENABLED));
	layout->addWidget(enabled_check);

	static TextureData DEFAULT_TEXTURE;
	TextureData &texture = present ? mesh.textures[texture_index] : DEFAULT_TEXTURE;

	bool edit = ctx.edit && present;
	TextureData *texture_ptr = &texture;
	ChangeFn dirty = ctx.markDirty;
	ChangeFn typing = ctx.markTyping;

	// Texture info bits require the optional TEXTURE_INFO chunk; setting any of
	// them marks the texture as having info so the writer materializes it.
	auto touch_info = [texture_ptr]() { texture_ptr->hasInfo = true; };

	// A fixed-size thumbnail slot that any of the code below can repopulate.
	// Its pixmap comes from the host-decoded BGRA pixels (== little-endian
	// ARGB32); empty pixels show the "not found" text instead.
	QLabel *thumbnail = new QLabel;
	thumbnail->setFrameShape(QFrame::StyledPanel);
	thumbnail->setAlignment(Qt::AlignCenter);

	// A right click shows the texture file in Explorer
	thumbnail->setContextMenuPolicy(Qt::CustomContextMenu);
	QObject::connect(thumbnail, &QWidget::customContextMenuRequested, [texture_ptr](const QPoint &) {
		Show_In_Explorer(texture_ptr->resolvedPath);
	});

	auto paint_thumbnail = [thumbnail](const TextureData &tex) {
		if (tex.previewWidth > 0 && tex.previewHeight > 0 &&
				tex.previewPixels.size() >= (size_t)tex.previewWidth * tex.previewHeight * 4) {
			QImage image(tex.previewPixels.data(),
				tex.previewWidth, tex.previewHeight,
				tex.previewWidth * 4, QImage::Format_ARGB32);
			thumbnail->setPixmap(QPixmap::fromImage(image.copy()));
			thumbnail->setText(QString());
			thumbnail->setFixedSize(tex.previewWidth + 2, tex.previewHeight + 2);
			thumbnail->setToolTip(tex.resolvedPath.empty()
				? QString() : QString::fromStdString(tex.resolvedPath) + QStringLiteral("\nRight-click to show in Explorer"));
		} else {
			thumbnail->setPixmap(QPixmap());
			thumbnail->setText(QStringLiteral("(texture file not found)"));
			// Drop any fixed size left from a previous (found) texture so the
			// label shrinks back to its text.
			thumbnail->setMinimumSize(0, 0);
			thumbnail->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
			thumbnail->setToolTip(QString());
		}
	};

	QHBoxLayout *name_row = new QHBoxLayout;
	if (edit) {
		// Manual edit is allowed; Browse... opens a file picker but stores only
		// the stripped filename (with extension) — .w3d texture references are
		// bare filenames resolved through the texture search paths, never full
		// paths. Editing the name re-decodes the thumbnail through the host.
		QLineEdit *name_edit = new QLineEdit(QString::fromStdString(texture.name));
		QObject::connect(name_edit, &QLineEdit::textChanged,
			[texture_ptr, typing, paint_thumbnail](const QString &t) {
				texture_ptr->name = t.toStdString();
				// Re-resolve the preview for the new filename (host decodes it).
				texture_ptr->resolvedPath.clear();
				texture_ptr->previewWidth = 0;
				texture_ptr->previewHeight = 0;
				texture_ptr->previewPixels.clear();
				if (g_ResolveTextureCallback != nullptr && !texture_ptr->name.empty()) {
					g_ResolveTextureCallback(*texture_ptr);
				}
				paint_thumbnail(*texture_ptr);
				if (typing) typing();	// coalesced: one undo step per typing burst
			});
		name_row->addWidget(name_edit, 1);
		name_row->addWidget(Make_Copy_Button(QString::fromStdString(texture.name),
			"Copy texture filename"));

		QPushButton *browse = new QPushButton(QStringLiteral("Browse..."));
		browse->setToolTip(QStringLiteral("Pick a texture file; only its filename is stored"));
		QString start_dir = QString::fromStdString(texture.resolvedPath);
		QObject::connect(browse, &QPushButton::clicked, [name_edit, start_dir]() {
			QString picked = QFileDialog::getOpenFileName(name_edit,
				QStringLiteral("Select Texture"), start_dir,
				QStringLiteral("Texture Files (*.tga *.dds);;All Files (*.*)"));
			if (!picked.isEmpty()) {
				// Strip the path: store just "name.ext". setText fires
				// textChanged, which updates the document + thumbnail + dirty.
				name_edit->setText(QFileInfo(picked).fileName());
			}
		});
		name_row->addWidget(browse);
	} else {
		QLabel *name = new QLabel(present ? QString::fromStdString(texture.name) : QStringLiteral("None"));
		QFont font = name->font();
		font.setBold(true);
		name->setFont(font);
		name->setTextInteractionFlags(Qt::TextSelectableByMouse);
		if (!texture.resolvedPath.empty()) {
			name->setToolTip(QString::fromStdString(texture.resolvedPath));
		}
		name_row->addWidget(name);
		if (present) {
			name_row->addWidget(Make_Copy_Button(QString::fromStdString(texture.name),
				"Copy texture filename"));
		}
		name_row->addStretch(1);
	}
	layout->addLayout(name_row);

	// Only show the thumbnail slot for a real (present) texture; an empty
	// stage stays as before.
	if (present) {
		paint_thumbnail(texture);
		layout->addWidget(thumbnail);
	} else {
		delete thumbnail;
	}

	// Toggles a single bit of texture.attributes; marks info present + dirty.
	auto make_flag = [&](const char *label, uint16_t bit, const char *help) -> QCheckBox * {
		QCheckBox *check = Make_Check(label, (texture.attributes & bit) != 0,
			edit ? std::function<void(bool)>([texture_ptr, bit, dirty, touch_info](bool on) {
				if (on) texture_ptr->attributes |= bit;
				else    texture_ptr->attributes &= ~bit;
				touch_info();
				if (dirty) dirty();
			}) : std::function<void(bool)>());
		Set_Help(check, QString::fromLatin1(help));
		return check;
	};

	QHBoxLayout *flags_row_1 = new QHBoxLayout;
	flags_row_1->addWidget(make_flag("Clamp U", TEX_CLAMP_U, Help::TEX_CLAMP_U));
	flags_row_1->addWidget(make_flag("Clamp V", TEX_CLAMP_V, Help::TEX_CLAMP_V));
	flags_row_1->addWidget(make_flag("No LOD", TEX_NO_LOD, Help::TEX_NO_LOD));
	flags_row_1->addStretch(1);
	layout->addLayout(flags_row_1);

	QHBoxLayout *flags_row_2 = new QHBoxLayout;
	flags_row_2->addWidget(make_flag("Publish", TEX_PUBLISH, Help::TEX_PUBLISH));
	flags_row_2->addWidget(make_flag("Resize", TEX_RESIZE_OBSOLETE, Help::TEX_RESIZE));
	flags_row_2->addWidget(make_flag("Alpha Bitmap", TEX_ALPHA_BITMAP, Help::TEX_ALPHA));
	flags_row_2->addStretch(1);
	layout->addLayout(flags_row_2);

	QFormLayout *form = new QFormLayout;
	QSpinBox *frames_spin = Make_Int_Spin((int)texture.frameCount, 0, 999,
		edit ? std::function<void(int)>([texture_ptr, dirty, touch_info](int v) {
			texture_ptr->frameCount = (uint32_t)v; touch_info(); if (dirty) dirty();
		}) : std::function<void(int)>());
	Set_Help(frames_spin, QString::fromLatin1(Help::TEX_FRAMES));
	form->addRow(QStringLiteral("Frames:"), frames_spin);
	QDoubleSpinBox *fps_spin = Make_Float_Spin(texture.frameRate, 0.0, 60.0, 1,
		edit ? std::function<void(double)>([texture_ptr, dirty, touch_info](double v) {
			texture_ptr->frameRate = (float)v; touch_info(); if (dirty) dirty();
		}) : std::function<void(double)>());
	Set_Help(fps_spin, QString::fromLatin1(Help::TEX_FPS));
	form->addRow(QStringLiteral("FPS:"), fps_spin);
	QComboBox *anim_combo = Make_Combo(ANIM_MODES, texture.animType,
		edit ? std::function<void(int)>([texture_ptr, dirty, touch_info](int v) {
			texture_ptr->animType = (uint16_t)v; touch_info(); if (dirty) dirty();
		}) : std::function<void(int)>());
	Set_Help(anim_combo, QString::fromLatin1(Help::TEX_ANIM));
	form->addRow(QStringLiteral("Anim Mode:"), anim_combo);
	QComboBox *hint_combo = Make_Combo(PASS_HINTS, (texture.attributes & TEX_HINT_MASK) >> TEX_HINT_SHIFT,
		edit ? std::function<void(int)>([texture_ptr, dirty, touch_info](int v) {
			texture_ptr->attributes = (texture_ptr->attributes & ~TEX_HINT_MASK)
				| (uint16_t)(((uint32_t)v << TEX_HINT_SHIFT) & TEX_HINT_MASK);
			touch_info(); if (dirty) dirty();
		}) : std::function<void(int)>());
	Set_Help(hint_combo, QString::fromLatin1(Help::TEX_HINT));
	form->addRow(QStringLiteral("Pass Hint:"), hint_combo);
	layout->addLayout(form);

	if (!present) {
		// Match the Max plugin: gray the whole group when the stage is unused.
		group->setEnabled(false);
	}
	return group;
}

QWidget *Build_Textures_Tab(const EditCtx &ctx, MeshMaterialData &mesh, const PassData &pass)
{
	QWidget *tab = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(tab);
	layout->addWidget(Build_Texture_Stage_Group(ctx, mesh, pass, 0));
	layout->addWidget(Build_Texture_Stage_Group(ctx, mesh, pass, 1));
	layout->addStretch(1);
	return tab;
}

//////////////////////////////////////////////////////////////////////////////
//	Undo/redo
//////////////////////////////////////////////////////////////////////////////
//
// Every edit mutates one field of one mesh in the panel's document copy. Rather
// than a bespoke command per field type (colors, floats, bitfields, strings,
// coupled blend presets, ...), each edit is captured as a whole-mesh before/after
// snapshot. MeshMaterialData is a small plain-copyable struct, so this is cheap
// and handles coupled edits (blend preset writing src+dest+alphaTest, the
// static-sort toggle rewriting sortLevel, texture-info side effects) atomically.

// Implemented by the panel; lets a command write a snapshot back and refresh UI.
class MeshSnapshotHost
{
public:
	virtual ~MeshSnapshotHost() {}
	virtual void Apply_Mesh_Snapshot(int meshIndex, const MeshMaterialData &snapshot) = 0;
};

class MeshEditCommand : public QUndoCommand
{
public:
	MeshEditCommand(MeshSnapshotHost *host, int meshIndex, const QString &label,
			const MeshMaterialData &before, const MeshMaterialData &after, bool mergeable)
		: m_Host(host), m_MeshIndex(meshIndex),
		  m_Before(before), m_After(after), m_Mergeable(mergeable),
		  m_Pushed(false)
	{
		setText(label);
	}

	void undo() override { m_Host->Apply_Mesh_Snapshot(m_MeshIndex, m_Before); }

	void redo() override
	{
		// QUndoStack::push() calls redo() immediately, but the edit that created
		// this command already mutated the document in place — and it runs inside
		// the edited widget's own signal handler. Re-applying the snapshot there
		// rebuilds the panel page (deleting that live widget) and corrupts the
		// heap. So skip the first redo; only real user-driven redos re-apply.
		if (!m_Pushed) {
			m_Pushed = true;
			return;
		}
		m_Host->Apply_Mesh_Snapshot(m_MeshIndex, m_After);
	}

	// Merge consecutive edits to the same field of the same mesh (e.g. typing
	// into the texture-name / mapper-args box, or dragging a spin box) into one
	// undo step. `id()` != -1 enables Qt's automatic merge attempt.
	int id() const override { return m_Mergeable ? 0x4D455348 /*'MESH'*/ : -1; }

	bool mergeWith(const QUndoCommand *other) override
	{
		const MeshEditCommand *cmd = static_cast<const MeshEditCommand *>(other);
		if (cmd->m_MeshIndex != m_MeshIndex || text() != cmd->text()) {
			return false;
		}
		m_After = cmd->m_After;	// keep the original "before", extend the "after"
		return true;
	}

private:
	MeshSnapshotHost	*m_Host;
	int					m_MeshIndex;
	MeshMaterialData	m_Before;
	MeshMaterialData	m_After;
	bool				m_Mergeable;
	bool				m_Pushed;	// false until the initial push-redo is skipped
};

//////////////////////////////////////////////////////////////////////////////
//	MaterialPanelWidget
//////////////////////////////////////////////////////////////////////////////

class MaterialPanelWidget : public QWidget, public MeshSnapshotHost
{
public:
	MaterialPanelWidget()
		: m_MeshButton(nullptr),
		  m_ViewButton(nullptr),
		  m_EditButton(nullptr),
		  m_UndoButton(nullptr),
		  m_RedoButton(nullptr),
		  m_SaveButton(nullptr),
		  m_RevertButton(nullptr),
		  m_Scroll(nullptr),
		  m_StatusLabel(nullptr),
		  m_HelpFilter(nullptr),
		  m_UndoAction(nullptr),
		  m_RedoAction(nullptr),
		  m_CurrentIndex(-1),
		  m_EditMode(false),
		  m_Dirty(false),
		  m_Building(false)
	{
		QVBoxLayout *layout = new QVBoxLayout(this);
		layout->setContentsMargins(4, 4, 4, 4);

		QHBoxLayout *mesh_row = new QHBoxLayout;
		mesh_row->addWidget(new QLabel(QStringLiteral("Mesh:")));
		m_MeshButton = new QPushButton(QStringLiteral("(no file loaded)"));
		m_MeshButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		m_MeshButton->setToolTip(QStringLiteral("Choose a mesh (filterable list)"));
		mesh_row->addWidget(m_MeshButton, 1);

		// Segmented View | Edit gate: two glued, mutually exclusive buttons.
		// Save/Revert appear only while editing.
		m_ViewButton = new QPushButton(QStringLiteral("View"));
		m_ViewButton->setCheckable(true);
		m_ViewButton->setChecked(true);
		m_ViewButton->setToolTip(QStringLiteral("Read-only View mode"));
		m_EditButton = new QPushButton(QStringLiteral("Edit"));
		m_EditButton->setCheckable(true);
		m_EditButton->setToolTip(QStringLiteral("Editable Edit mode"));

		// Keep the pair exclusive (radio-like) and glue them into one control.
		QButtonGroup *mode_group = new QButtonGroup(this);
		mode_group->setExclusive(true);
		mode_group->addButton(m_ViewButton);
		mode_group->addButton(m_EditButton);

		m_ViewButton->setObjectName(QStringLiteral("segLeft"));
		m_EditButton->setObjectName(QStringLiteral("segRight"));

		QHBoxLayout *mode_row = new QHBoxLayout;
		mode_row->setContentsMargins(0, 0, 0, 0);
		mode_row->setSpacing(0);
		mode_row->addWidget(m_ViewButton);
		mode_row->addWidget(m_EditButton);
		mesh_row->addLayout(mode_row);

		// Glue the pair into one segmented control: flatten the inner corners
		// so the seam reads as a single divider, and let the checked segment
		// stand out. Scoped by objectName so it doesn't touch other buttons.
		Style_Mode_Segment();

		layout->addLayout(mesh_row);

		// Edit-mode action row beneath the mesh row (only shown while editing)
		// so it doesn't crowd the View/Edit segment: Undo | Redo ... Save Revert.
		m_UndoButton = new QPushButton(QStringLiteral("Undo"));
		m_UndoButton->setToolTip(QStringLiteral("Undo the last material edit (Ctrl+Z)"));
		m_UndoButton->setVisible(false);
		m_UndoButton->setEnabled(false);

		m_RedoButton = new QPushButton(QStringLiteral("Redo"));
		m_RedoButton->setToolTip(QStringLiteral("Redo the next material edit (Ctrl+Shift+Z)"));
		m_RedoButton->setVisible(false);
		m_RedoButton->setEnabled(false);

		m_SaveButton = new QPushButton(QStringLiteral("Save"));
		m_SaveButton->setToolTip(QStringLiteral("Write the edited material back to the .w3d file"));
		m_SaveButton->setVisible(false);
		m_SaveButton->setEnabled(false);

		m_RevertButton = new QPushButton(QStringLiteral("Revert"));
		m_RevertButton->setToolTip(QStringLiteral("Discard edits and reload the file from disk"));
		m_RevertButton->setVisible(false);
		m_RevertButton->setEnabled(false);

		QHBoxLayout *edit_row = new QHBoxLayout;
		edit_row->setContentsMargins(0, 0, 0, 0);
		edit_row->addWidget(m_UndoButton);
		edit_row->addWidget(m_RedoButton);
		edit_row->addStretch(1);
		edit_row->addWidget(m_SaveButton);
		edit_row->addWidget(m_RevertButton);
		layout->addLayout(edit_row);

		// Undo/redo: buttons drive the stack.
		QObject::connect(m_UndoButton, &QPushButton::clicked,
			[this]() { m_UndoStack.undo(); });
		QObject::connect(m_RedoButton, &QPushButton::clicked,
			[this]() { m_UndoStack.redo(); });
		QObject::connect(&m_UndoStack, &QUndoStack::canUndoChanged,
			[this](bool can) { m_UndoButton->setEnabled(m_EditMode && can); });
		QObject::connect(&m_UndoStack, &QUndoStack::canRedoChanged,
			[this](bool can) { m_RedoButton->setEnabled(m_EditMode && can); });

		// Keyboard shortcuts: Ctrl+Z = undo, Ctrl+Shift+Z (and Ctrl+Y) = redo.
		// Connect straight to the stack rather than createUndo/RedoAction so we
		// can bind multiple redo chords. WidgetWithChildrenShortcut fires while
		// focus is anywhere inside the panel (the MFC frame forwards these keys
		// to Qt via PreTranslateMessage). guard with m_EditMode so View mode is
		// unaffected.
		m_UndoAction = new QAction(this);
		m_UndoAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
		m_UndoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		QObject::connect(m_UndoAction, &QAction::triggered, [this]() {
			if (m_EditMode && m_UndoStack.canUndo()) m_UndoStack.undo();
		});
		addAction(m_UndoAction);

		m_RedoAction = new QAction(this);
		QList<QKeySequence> redo_keys;
		redo_keys << QKeySequence(QStringLiteral("Ctrl+Shift+Z"))
				  << QKeySequence(QStringLiteral("Ctrl+Y"));
		m_RedoAction->setShortcuts(redo_keys);
		m_RedoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		QObject::connect(m_RedoAction, &QAction::triggered, [this]() {
			if (m_EditMode && m_UndoStack.canRedo()) m_UndoStack.redo();
		});
		addAction(m_RedoAction);

		m_Scroll = new QScrollArea;
		m_Scroll->setWidgetResizable(true);
		m_Scroll->setFrameShape(QFrame::NoFrame);
		layout->addWidget(m_Scroll, 1);

		// One-line help strip: field descriptions (from the W3D Hub tools docs)
		// appear here on hover/focus instead of in hover tooltips, which read
		// poorly for the longer descriptions.
		m_StatusLabel = new QLabel(QStringLiteral(" "));
		m_StatusLabel->setWordWrap(true);
		m_StatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
		m_StatusLabel->setFrameShape(QFrame::StyledPanel);
		m_StatusLabel->setMinimumHeight(30);
		m_StatusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		layout->addWidget(m_StatusLabel);
		g_StatusLabel = m_StatusLabel;

		// Watch every descendant so hover/focus can surface its help text.
		m_HelpFilter = new HelpEventFilter(this);
		QApplication::instance()->installEventFilter(m_HelpFilter);

		// Live controls connect through lambdas; no Q_OBJECT needed.
		QObject::connect(m_MeshButton, &QPushButton::clicked,
			[this]() { Pick_Mesh(); });
		QObject::connect(m_EditButton, &QPushButton::toggled,
			[this](bool on) { Toggle_Edit_Mode(on); });
		// Guard against re-clicking the already-active segment: an exclusive
		// group swallows the second click (no toggled signal), which is fine.
		QObject::connect(m_SaveButton, &QPushButton::clicked,
			[this]() { Do_Save(); });
		QObject::connect(m_RevertButton, &QPushButton::clicked,
			[this]() { Do_Revert(); });

		Show_Empty();
	}

	void Set_Document(const MaterialDocument &document)
	{
		m_Document = document;
		// A freshly (re)loaded document starts with no edit history.
		m_UndoStack.clear();
		m_UndoStack.setClean();
		Set_Dirty(false);

		if (m_Document.meshes.empty()) {
			m_CurrentIndex = -1;
			m_MeshButton->setText(QStringLiteral("(no meshes)"));
			Show_Empty();
		} else {
			Show_Mesh(m_CurrentIndex >= 0 && m_CurrentIndex < (int)m_Document.meshes.size()
				? m_CurrentIndex : 0);
		}
	}

	bool Has_Unsaved_Changes() const { return m_EditMode && m_Dirty; }

	bool Request_Save()
	{
		if (!m_EditMode || !m_Dirty) {
			return true;
		}
		return Do_Save();
	}

	// --- Edit-mode menu bridge (see MaterialPanel.h) ---
	bool Is_Editing() const { return m_EditMode; }
	bool Can_Undo() const { return m_EditMode && m_UndoStack.canUndo(); }
	bool Can_Redo() const { return m_EditMode && m_UndoStack.canRedo(); }
	bool Can_Save_Or_Revert() const { return m_EditMode && m_Dirty; }

	void Menu_Undo() { if (Can_Undo()) m_UndoStack.undo(); }
	void Menu_Redo() { if (Can_Redo()) m_UndoStack.redo(); }
	void Menu_Revert() { if (m_EditMode && m_Dirty) Do_Revert(); }

	void Select_Mesh(const char *mesh_name)
	{
		QString wanted = QString::fromLatin1(mesh_name);

		// Exact full-name match first ("Container.Mesh").
		for (size_t i = 0; i < m_Document.meshes.size(); i++) {
			if (QString::fromStdString(m_Document.meshes[i].meshName)
					.compare(wanted, Qt::CaseInsensitive) == 0) {
				Show_Mesh((int)i);
				return;
			}
		}

		// Fall back to the sub-object name (part after the last '.'), so a
		// selection carried over from another tab/file — whose container prefix
		// differs (USCLAB_NE.MESH02 vs USCLAB_S.MESH02) — still lands on the
		// matching mesh here. This is what makes Batch Select work across files
		// with different container names or mesh counts.
		int dot = wanted.lastIndexOf(QLatin1Char('.'));
		QString wantedSub = (dot >= 0) ? wanted.mid(dot + 1) : wanted;
		for (size_t i = 0; i < m_Document.meshes.size(); i++) {
			QString name = QString::fromStdString(m_Document.meshes[i].meshName);
			int d = name.lastIndexOf(QLatin1Char('.'));
			QString sub = (d >= 0) ? name.mid(d + 1) : name;
			if (sub.compare(wantedSub, Qt::CaseInsensitive) == 0) {
				Show_Mesh((int)i);
				return;
			}
		}
	}

	void Apply_Theme(const PanelTheme &theme)
	{
		g_DarkTheme = theme.dark;

		QPalette palette;
		if (theme.dark) {
			QColor window(GetRValue(theme.background), GetGValue(theme.background), GetBValue(theme.background));
			QColor base(GetRValue(theme.ctrlBackground), GetGValue(theme.ctrlBackground), GetBValue(theme.ctrlBackground));
			QColor text(GetRValue(theme.text), GetGValue(theme.text), GetBValue(theme.text));
			QColor edge(GetRValue(theme.edge), GetGValue(theme.edge), GetBValue(theme.edge));

			palette.setColor(QPalette::Window, window);
			palette.setColor(QPalette::WindowText, text);
			palette.setColor(QPalette::Base, base);
			palette.setColor(QPalette::AlternateBase, window);
			palette.setColor(QPalette::Text, text);
			palette.setColor(QPalette::Button, window);
			palette.setColor(QPalette::ButtonText, text);
			palette.setColor(QPalette::ToolTipBase, base);
			palette.setColor(QPalette::ToolTipText, text);
			palette.setColor(QPalette::Mid, edge);
			palette.setColor(QPalette::Highlight, QColor(0, 120, 215));
			palette.setColor(QPalette::HighlightedText, Qt::white);
		} else {
			palette = QApplication::style()->standardPalette();
		}

		// Read-only controls stay enabled-looking: mirror active colors into the
		// disabled group (used only by the unused-texture-stage groups).
		palette.setColor(QPalette::Disabled, QPalette::WindowText,
			palette.color(QPalette::Active, QPalette::WindowText).darker(theme.dark ? 100 : 130));

		setPalette(palette);
		// New children created by later Set_Document calls inherit this palette.

		// Top-level popups (the swatch QColorDialog) do NOT inherit from the
		// panel — they take the application palette, so set it too or they
		// come up light in dark mode.
		QApplication::setPalette(palette);

		// The segment stylesheet uses fixed hex colors, so restyle it against
		// the new theme.
		Style_Mode_Segment();
	}

private:
	// Paint the View|Edit pair as one segmented control: flatten the seam
	// corners, share a 1px divider, and highlight the checked half. Uses
	// palette-driven hex so it reads correctly in light and dark themes.
	void Style_Mode_Segment()
	{
		if (m_ViewButton == nullptr || m_EditButton == nullptr) {
			return;
		}
		Style_Segment_Pair(m_ViewButton, m_EditButton);
	}

	void Show_Empty()
	{
		QLabel *empty = new QLabel(QStringLiteral("Open a .w3d file to inspect its materials."));
		empty->setAlignment(Qt::AlignCenter);
		m_Scroll->setWidget(empty);
	}

	// Modal mesh picker: full mesh list with a filter box. Double-click or
	// Enter accepts; the filter keeps a visible row selected so Enter always
	// picks the top match.
	void Pick_Mesh()
	{
		if (m_Document.meshes.empty()) {
			return;
		}

		QDialog dialog(this);
		dialog.setWindowTitle(QStringLiteral("Select Mesh"));
		dialog.resize(420, 480);

		QVBoxLayout *layout = new QVBoxLayout(&dialog);

		QLineEdit *filter = new QLineEdit;
		filter->setPlaceholderText(QStringLiteral("Filter meshes..."));
		filter->setClearButtonEnabled(true);
		layout->addWidget(filter);

		QListWidget *list = new QListWidget;
		for (size_t i = 0; i < m_Document.meshes.size(); i++) {
			QListWidgetItem *item =
				new QListWidgetItem(QString::fromStdString(m_Document.meshes[i].meshName));
			item->setData(Qt::UserRole, (int)i);
			list->addItem(item);
		}
		if (m_CurrentIndex >= 0 && m_CurrentIndex < list->count()) {
			list->setCurrentRow(m_CurrentIndex);
		}
		layout->addWidget(list, 1);

		QDialogButtonBox *buttons =
			new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		layout->addWidget(buttons);

		QObject::connect(filter, &QLineEdit::textChanged, [list](const QString &text) {
			int first_visible = -1;
			for (int i = 0; i < list->count(); i++) {
				QListWidgetItem *item = list->item(i);
				bool match = item->text().contains(text, Qt::CaseInsensitive);
				item->setHidden(!match);
				if (match && first_visible < 0) {
					first_visible = i;
				}
			}
			QListWidgetItem *current = list->currentItem();
			if ((current == nullptr || current->isHidden()) && first_visible >= 0) {
				list->setCurrentRow(first_visible);
			}
		});
		QObject::connect(list, &QListWidget::itemDoubleClicked,
			[&dialog](QListWidgetItem *) { dialog.accept(); });
		QObject::connect(filter, &QLineEdit::returnPressed, [&dialog]() { dialog.accept(); });
		QObject::connect(buttons, &QDialogButtonBox::accepted, [&dialog]() { dialog.accept(); });
		QObject::connect(buttons, &QDialogButtonBox::rejected, [&dialog]() { dialog.reject(); });

		// Live preview: while the user scrolls the list (arrow keys, clicks,
		// filtering), show the highlighted mesh in the viewport immediately.
		// The panel page is NOT rebuilt — that happens only on accept
		// (Show_Mesh); a cancel restores the previous mesh below.
		QObject::connect(list, &QListWidget::currentItemChanged,
			[](QListWidgetItem *current, QListWidgetItem *) {
				if (current != nullptr && g_MeshSelectedCallback != nullptr) {
					QByteArray name = current->text().toLatin1();
					g_MeshSelectedCallback(name.constData());
				}
			});

		// Focus the list (not the filter) so arrow keys walk the meshes — and
		// the live viewport preview — the moment the dialog opens.
		list->setFocus();
		Apply_Dark_Title_Bar((HWND)dialog.winId());

		bool accepted = (dialog.exec() == QDialog::Accepted);
		QListWidgetItem *item = accepted ? list->currentItem() : nullptr;
		if (item != nullptr && !item->isHidden()) {
			Show_Mesh(item->data(Qt::UserRole).toInt());
		} else if (g_MeshSelectedCallback != nullptr
				&& m_CurrentIndex >= 0 && m_CurrentIndex < (int)m_Document.meshes.size()) {
			// Cancelled (or accepted with no usable row): the panel still shows
			// the old mesh, so point the live preview back at it.
			g_MeshSelectedCallback(m_Document.meshes[m_CurrentIndex].meshName.c_str());
		}
	}

	void Show_Mesh(int index)
	{
		if (index < 0 || index >= (int)m_Document.meshes.size()) {
			Show_Empty();
			return;
		}

		MeshMaterialData &mesh = m_Document.meshes[index];
		m_CurrentIndex = index;
		m_MeshButton->setText(QString::fromStdString(mesh.meshName));

		if (g_MeshSelectedCallback != nullptr) {
			g_MeshSelectedCallback(mesh.meshName.c_str());
		}

		// Suppress dirty marking while the page is under construction: some
		// controls (e.g. combos whose value is set as the initial index)
		// legitimately fire change signals during setup, and rebuilding the
		// page on a View/Edit toggle must never look like a user edit.
		m_Building = true;

		// Baseline snapshot for undo: each committed edit is a diff against this,
		// and it is refreshed to the post-edit state after every push.
		m_EditBaseline = mesh;

		// Prelit meshes keep several material variants; editing one would desync
		// them, so they stay read-only even in edit mode (v1).
		EditCtx ctx;
		ctx.edit = m_EditMode && !mesh.prelit;
		ctx.mesh = &mesh;
		ctx.markDirty = [this]() { Commit_Edit(false); };	// discrete: one step each
		ctx.markTyping = [this]() { Commit_Edit(true); };	// text: coalesced burst
		// Deferred rebuild: the paste button that asks for it lives on the page
		// being torn down, so never rebuild inside its clicked() handler.
		ctx.refreshPage = [this]() {
			QTimer::singleShot(0, this, [this]() { Show_Mesh(m_CurrentIndex); });
		};

		MeshMaterialData *mesh_ptr = &mesh;
		ChangeFn dirty = ctx.markDirty;

		QWidget *content = new QWidget;
		QVBoxLayout *layout = new QVBoxLayout(content);
		layout->setContentsMargins(4, 4, 4, 4);

		// --- Material Surface Type rollup ---
		QGroupBox *surface_group = new QGroupBox(QStringLiteral("Material Surface Type"));
		QFormLayout *surface_form = new QFormLayout(surface_group);
		QComboBox *surface_combo = Make_Combo(SURFACE_TYPES, (int)mesh.surfaceType,
			ctx.edit ? std::function<void(int)>([mesh_ptr, dirty](int v) {
				mesh_ptr->surfaceType = (uint32_t)v; if (dirty) dirty();
			}) : std::function<void(int)>());
		Set_Help(surface_combo, QString::fromLatin1(Help::SURFACE_TYPE));
		surface_form->addRow(QStringLiteral("Surface Type:"), surface_combo);
		QCheckBox *static_sort = Make_Check("Static Sorting Enabled", mesh.sortLevel != 0,
			ctx.edit ? std::function<void(bool)>([mesh_ptr, dirty](bool on) {
				// Toggle drives sort level: on keeps/repairs a level, off clears it.
				if (on && mesh_ptr->sortLevel == 0) mesh_ptr->sortLevel = 1;
				else if (!on) mesh_ptr->sortLevel = 0;
				if (dirty) dirty();
			}) : std::function<void(bool)>());
		Set_Help(static_sort, QString::fromLatin1(Help::STATIC_SORT));
		surface_form->addRow(QString(), static_sort);
		QSpinBox *sort_spin = Make_Int_Spin(mesh.sortLevel, 0, 32,
			ctx.edit ? std::function<void(int)>([mesh_ptr, dirty](int v) {
				mesh_ptr->sortLevel = v; if (dirty) dirty();
			}) : std::function<void(int)>());
		Set_Help(sort_spin, QString::fromLatin1(Help::STATIC_SORT));
		surface_form->addRow(QStringLiteral("Sort Level:"), sort_spin);
		if (mesh.prelit) {
			surface_form->addRow(QString(), new QLabel(m_EditMode
				? QStringLiteral("(Prelit material %1 read-only)").arg(QChar(0x2014))
				: QStringLiteral("(Prelit material)")));
		}
		layout->addWidget(surface_group);

		// --- Material Pass Count rollup, with a live pass selector ---
		QGroupBox *pass_group = new QGroupBox(QStringLiteral("Material Pass Count"));
		QFormLayout *pass_form = new QFormLayout(pass_group);
		// Pass count is structural — read-only in v1 even while editing.
		QSpinBox *pass_count_spin = Make_Int_Spin((int)mesh.passes.size(), 0, 4);
		Set_Help(pass_count_spin, QString::fromLatin1(Help::PASS_COUNT));
		pass_form->addRow(QStringLiteral("Current Pass Count:"), pass_count_spin);
		layout->addWidget(pass_group);

		if (mesh.passes.empty()) {
			QLabel *none = new QLabel(QStringLiteral("This mesh has no material passes."));
			none->setAlignment(Qt::AlignCenter);
			layout->addWidget(none);
		} else {
			// All passes are built up front as tabs; the Show Pass combo and
			// the tab bar stay in sync both ways (setCurrentIndex no-ops on
			// the same index, so the two connects cannot loop).
			QComboBox *pass_combo = new QComboBox;
			QTabWidget *pass_tabs = new QTabWidget;
			for (size_t i = 0; i < mesh.passes.size(); i++) {
				pass_combo->addItem(QStringLiteral("Pass %1").arg(i + 1));
				pass_tabs->addTab(Build_Pass_Content(ctx, mesh, (int)i),
					QStringLiteral("Pass %1").arg(i + 1));
			}
			pass_form->addRow(QStringLiteral("Show Pass:"), pass_combo);
			layout->addWidget(pass_tabs);

			QObject::connect(pass_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
				[pass_tabs](int pass_index) { pass_tabs->setCurrentIndex(pass_index); });
			QObject::connect(pass_tabs, &QTabWidget::currentChanged,
				[pass_combo](int pass_index) { pass_combo->setCurrentIndex(pass_index); });
		}

		layout->addStretch(1);
		m_Scroll->setWidget(content);

		m_Building = false;
	}

	// One "Pass N" tab: the Vertex Material / Shader / Textures tab widget.
	static QWidget *Build_Pass_Content(const EditCtx &ctx, MeshMaterialData &mesh, int pass_index)
	{
		const PassData &pass = mesh.passes[pass_index];
		QTabWidget *tabs = new QTabWidget;
		tabs->addTab(Build_Vertex_Material_Tab(ctx, mesh, pass), QStringLiteral("Vertex Material"));
		tabs->addTab(Build_Shader_Tab(ctx, mesh, pass), QStringLiteral("Shader"));
		tabs->addTab(Build_Textures_Tab(ctx, mesh, pass), QStringLiteral("Textures"));
		return tabs;
	}

	//////////////////////////////////////////////////////////////////////////
	//	Edit-mode actions
	//////////////////////////////////////////////////////////////////////////

	void Toggle_Edit_Mode(bool on)
	{
		if (on == m_EditMode) {
			return;
		}

		if (on) {
			// Run the host's file-integrity gate before entering edit mode.
			if (g_EditGateCallback != nullptr && !g_EditGateCallback()) {
				Restore_Mode_Buttons(false);	// snap back to View
				return;
			}
			m_EditMode = true;
		} else {
			// Leaving edit mode with unsaved edits: offer to keep editing.
			if (m_Dirty) {
				QMessageBox box(this);
				box.setWindowTitle(QStringLiteral("W3D Material Viewer"));
				box.setText(QStringLiteral("Discard unsaved material changes?"));
				box.setStandardButtons(QMessageBox::Discard | QMessageBox::Cancel);
				box.setIcon(QMessageBox::Warning);
				Apply_Dark_Title_Bar((HWND)box.winId());
				if (box.exec() != QMessageBox::Discard) {
					Restore_Mode_Buttons(true);		// stay in Edit
					return;
				}
				// Discard: reload the pristine document from disk.
				if (g_RevertCallback != nullptr) {
					g_RevertCallback();
				}
			}
			m_EditMode = false;
		}

		Update_Edit_Buttons();
		if (m_CurrentIndex >= 0) {
			Show_Mesh(m_CurrentIndex);
		}
	}

	bool Do_Save()
	{
		if (g_SaveCallback == nullptr) {
			return false;
		}
		if (g_SaveCallback(m_Document)) {
			m_UndoStack.setClean();	// keep history but mark this the saved state
			Set_Dirty(false);
			return true;
		}
		return false;
	}

	void Do_Revert()
	{
		if (g_RevertCallback != nullptr) {
			g_RevertCallback();	// re-parses + calls Set_Document (clears dirty)
		}
	}

	// Push one undoable edit: diff the live mesh against the baseline snapshot,
	// record a before/after command, then advance the baseline. `mergeable`
	// coalesces consecutive same-field edits (text typing) into one step.
	void Commit_Edit(bool mergeable)
	{
		// Signals fired while a page is being (re)built are not user edits.
		if (m_Building || m_CurrentIndex < 0
				|| m_CurrentIndex >= (int)m_Document.meshes.size()) {
			return;
		}

		const MeshMaterialData &live = m_Document.meshes[m_CurrentIndex];
		m_UndoStack.push(new MeshEditCommand(this, m_CurrentIndex,
			QStringLiteral("Edit material"), m_EditBaseline, live, mergeable));
		m_EditBaseline = live;	// next edit diffs against this state
		Set_Dirty(true);
		Notify_Live_Preview();
	}

	// Push the current document to the host's live 3D preview (debounced there).
	// Independent of Save; the file is not written.
	void Notify_Live_Preview()
	{
		if (g_LivePreviewCallback != nullptr) {
			g_LivePreviewCallback(m_Document);
		}
	}

	// MeshSnapshotHost: restore a mesh to `snapshot` (undo/redo) and refresh.
	void Apply_Mesh_Snapshot(int meshIndex, const MeshMaterialData &snapshot) override
	{
		if (meshIndex < 0 || meshIndex >= (int)m_Document.meshes.size()) {
			return;
		}
		m_Document.meshes[meshIndex] = snapshot;

		// Re-resolve texture previews (names may have changed) before redisplay.
		if (g_ResolveTextureCallback != nullptr) {
			for (TextureData &tex : m_Document.meshes[meshIndex].textures) {
				tex.resolvedPath.clear();
				tex.previewWidth = tex.previewHeight = 0;
				tex.previewPixels.clear();
				if (!tex.name.empty()) {
					g_ResolveTextureCallback(tex);
				}
			}
		}

		// Rebuild the page (also refreshes m_EditBaseline for this mesh) and
		// reflect the resulting clean/dirty state.
		Show_Mesh(meshIndex);
		Set_Dirty(!m_UndoStack.isClean());
		Notify_Live_Preview();	// undo/redo also updates the live preview
	}

	void Set_Dirty(bool dirty)
	{
		// Ignore change signals emitted while a page is being built.
		if (dirty && m_Building) {
			return;
		}
		if (dirty == m_Dirty) {
			// Still refresh button state on first document load.
			Update_Edit_Buttons();
			return;
		}
		m_Dirty = dirty;
		Update_Edit_Buttons();
		if (g_DirtyChangedCallback != nullptr) {
			g_DirtyChangedCallback(m_EditMode && m_Dirty);
		}
	}

	// Force the segmented View/Edit pair to reflect `editActive` without
	// re-firing Toggle_Edit_Mode (used when a mode change is rejected).
	void Restore_Mode_Buttons(bool editActive)
	{
		QSignalBlocker block_edit(m_EditButton);
		QSignalBlocker block_view(m_ViewButton);
		m_EditButton->setChecked(editActive);
		m_ViewButton->setChecked(!editActive);
	}

	void Update_Edit_Buttons()
	{
		m_UndoButton->setVisible(m_EditMode);
		m_RedoButton->setVisible(m_EditMode);
		m_SaveButton->setVisible(m_EditMode);
		m_RevertButton->setVisible(m_EditMode);
		m_UndoButton->setEnabled(m_EditMode && m_UndoStack.canUndo());
		m_RedoButton->setEnabled(m_EditMode && m_UndoStack.canRedo());
		m_SaveButton->setEnabled(m_EditMode && m_Dirty);
		m_RevertButton->setEnabled(m_EditMode && m_Dirty);
	}

	QPushButton		*m_MeshButton;
	QPushButton		*m_ViewButton;
	QPushButton		*m_EditButton;
	QPushButton		*m_UndoButton;
	QPushButton		*m_RedoButton;
	QPushButton		*m_SaveButton;
	QPushButton		*m_RevertButton;
	QScrollArea		*m_Scroll;
	QLabel			*m_StatusLabel;
	HelpEventFilter	*m_HelpFilter;
	QAction			*m_UndoAction;
	QAction			*m_RedoAction;
	MaterialDocument m_Document;
	QUndoStack		m_UndoStack;
	MeshMaterialData m_EditBaseline;	// pre-edit state of the shown mesh (for undo diffs)
	int				m_CurrentIndex;
	bool			m_EditMode;
	bool			m_Dirty;
	bool			m_Building;	// true while Show_Mesh builds a page
};

//////////////////////////////////////////////////////////////////////////////
//	Module state
//////////////////////////////////////////////////////////////////////////////

QApplication *g_Application = nullptr;
MaterialPanelWidget *g_Panel = nullptr;
bool g_ApplicationFailed = false;

// File tab bar (see CreateTabBar). Owned separately from the material panel.
QTabBar *g_TabBar = nullptr;

bool Ensure_Application()
{
	if (g_Application != nullptr) {
		return true;
	}
	if (g_ApplicationFailed) {
		return false;
	}

	// argc/argv must outlive the QApplication.
	static int argc = 1;
	static char arg0[] = "W3DView";
	static char *argv[] = { arg0, nullptr };

	g_Application = new QApplication(argc, argv);
	if (g_Application == nullptr) {
		g_ApplicationFailed = true;
		return false;
	}

	// Fusion respects custom palettes (the native style ignores them), which the
	// dark theme needs.
	QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
	return true;
}

} // namespace

//////////////////////////////////////////////////////////////////////////////
//	Public interface (MaterialPanel.h)
//////////////////////////////////////////////////////////////////////////////

HWND CreatePanel(HWND parent)
{
	if (!Ensure_Application()) {
		return nullptr;
	}

	if (g_Panel == nullptr) {
		g_Panel = new MaterialPanelWidget;
	}

	// Qt must consider the window frameless BEFORE the native window is
	// created: otherwise it keeps top-level frame margins (caption + borders)
	// in its geometry math after the re-parent, leaving its logical size a
	// title-bar short of the real client size — the bottom rows then never
	// get painted.
	g_Panel->setWindowFlags(g_Panel->windowFlags() | Qt::FramelessWindowHint);

	// Re-parent the native Qt window into the MFC frame as a WS_CHILD.
	HWND hwnd = (HWND)g_Panel->winId();
	::SetParent(hwnd, parent);
	LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
	style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME);
	style |= WS_CHILD;
	::SetWindowLong(hwnd, GWL_STYLE, style);

	// The MFC host sizes this window natively (MoveWindow), which does not set
	// Qt's WA_Resized flag — without it, Qt's layout engine "auto-sizes" the
	// window to its preferred size whenever the content is rebuilt, leaving a
	// gap of unstyled frame background.
	g_Panel->setAttribute(Qt::WA_Resized, true);

	g_Panel->show();
	return hwnd;
}

void DestroyPanel()
{
	if (g_Panel != nullptr) {
		g_StatusLabel = nullptr;	// owned by the panel; cleared before delete
		delete g_Panel;
		g_Panel = nullptr;
	}
}

HWND GetPanelHwnd()
{
	return (g_Panel != nullptr) ? (HWND)g_Panel->winId() : nullptr;
}

//////////////////////////////////////////////////////////////////////////////
//	File tab bar (MaterialPanel.h)
//////////////////////////////////////////////////////////////////////////////

// The Fusion style's built-in close-tab indicator is a fixed dark glyph that
// all but disappears on a dark tab bar, so each tab carries its own close
// QToolButton with a cross drawn in the current palette's text colour.
static QIcon Make_Tab_Close_Icon()
{
	const QColor color = QApplication::palette().color(QPalette::WindowText);
	QPixmap pixmap(16, 16);
	pixmap.fill(Qt::transparent);
	{
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(QPen(color, 1.4));
		painter.drawLine(QPointF(5.0, 5.0), QPointF(11.0, 11.0));
		painter.drawLine(QPointF(11.0, 5.0), QPointF(5.0, 11.0));
	}
	return QIcon(pixmap);
}

// Re-tints every tab's close cross after a theme switch (ApplyPanelTheme).
static void Refresh_Tab_Close_Icons()
{
	if (g_TabBar == nullptr) {
		return;
	}
	QIcon icon = Make_Tab_Close_Icon();
	for (int i = 0; i < g_TabBar->count(); i++) {
		QToolButton *button = qobject_cast<QToolButton *>(
			g_TabBar->tabButton(i, QTabBar::RightSide));
		if (button != nullptr) {
			button->setIcon(icon);
		}
	}
}

HWND CreateTabBar(HWND parent)
{
	if (!Ensure_Application()) {
		return nullptr;
	}

	if (g_TabBar == nullptr) {
		g_TabBar = new QTabBar;
		g_TabBar->setExpanding(false);
		g_TabBar->setUsesScrollButtons(true);
		g_TabBar->setElideMode(Qt::ElideMiddle);
		// No keyboard focus: the MFC frame's PreTranslateMessage only routes
		// keys to the panel, and the mouse is enough for a tab strip.
		g_TabBar->setFocusPolicy(Qt::NoFocus);
		g_TabBar->setDrawBase(false);
		QObject::connect(g_TabBar, &QTabBar::currentChanged, [](int index) {
			if (!g_TabBarUpdating && g_TabChangedCallback != nullptr) {
				g_TabChangedCallback(index);
			}
		});
	}

	// Same reparenting dance as CreatePanel: Qt must consider the window
	// frameless BEFORE the native window exists, or its geometry math keeps
	// top-level frame margins after the re-parent.
	g_TabBar->setWindowFlags(g_TabBar->windowFlags() | Qt::FramelessWindowHint);

	HWND hwnd = (HWND)g_TabBar->winId();
	::SetParent(hwnd, parent);
	LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
	style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME);
	style |= WS_CHILD;
	::SetWindowLong(hwnd, GWL_STYLE, style);

	g_TabBar->setAttribute(Qt::WA_Resized, true);
	g_TabBar->show();
	return hwnd;
}

void DestroyTabBar()
{
	if (g_TabBar != nullptr) {
		delete g_TabBar;
		g_TabBar = nullptr;
	}
}

void ResizeTabBar(int width, int height)
{
	if (g_TabBar != nullptr) {
		g_TabBar->resize(width, height);
	}
}

int GetTabBarPreferredHeight()
{
	return (g_TabBar != nullptr) ? g_TabBar->sizeHint().height() : 0;
}

void SetTabBarTabs(const char *const *labels, const char *const *tooltips,
	int count, int active)
{
	if (g_TabBar == nullptr) {
		return;
	}

	g_TabBarUpdating = true;
	while (g_TabBar->count() > 0) {
		g_TabBar->removeTab(0);
	}
	QIcon close_icon = Make_Tab_Close_Icon();
	for (int i = 0; i < count; i++) {
		g_TabBar->addTab(QString::fromLocal8Bit(labels[i]));
		if (tooltips != nullptr && tooltips[i] != nullptr && tooltips[i][0] != '\0') {
			g_TabBar->setTabToolTip(i, QString::fromLocal8Bit(tooltips[i]));
		}

		// The captured index stays valid: every tab-list change rebuilds the
		// bar (and with it these buttons) through this function.
		QToolButton *close_button = new QToolButton(g_TabBar);
		close_button->setAutoRaise(true);
		close_button->setFocusPolicy(Qt::NoFocus);
		close_button->setIcon(close_icon);
		close_button->setIconSize(QSize(16, 16));
		close_button->setFixedSize(18, 18);
		close_button->setToolTip(QStringLiteral("Close"));
		QObject::connect(close_button, &QToolButton::clicked, [i]() {
			if (!g_TabBarUpdating && g_TabCloseRequestedCallback != nullptr) {
				g_TabCloseRequestedCallback(i);
			}
		});
		g_TabBar->setTabButton(i, QTabBar::RightSide, close_button);
	}
	if (active >= 0 && active < count) {
		g_TabBar->setCurrentIndex(active);
	}
	g_TabBarUpdating = false;
}

void SetTabBarLabel(int index, const char *label)
{
	if (g_TabBar != nullptr && index >= 0 && index < g_TabBar->count()) {
		g_TabBar->setTabText(index, QString::fromLocal8Bit(label));
	}
}

void SetTabBarCurrent(int index)
{
	if (g_TabBar != nullptr && index >= 0 && index < g_TabBar->count()) {
		g_TabBarUpdating = true;
		g_TabBar->setCurrentIndex(index);
		g_TabBarUpdating = false;
	}
}

//////////////////////////////////////////////////////////////////////////////
//	Ground-height slider

QSlider *g_GroundSlider = nullptr;
bool g_GroundSliderUpdating = false;
void (*g_GroundSliderChangedCallback)(int pos) = nullptr;

HWND CreateGroundSlider(HWND parent, int steps)
{
	if (!Ensure_Application()) {
		return nullptr;
	}

	if (g_GroundSlider == nullptr) {
		g_GroundSlider = new QSlider(Qt::Vertical);
		g_GroundSlider->setRange(0, steps);
		// Position 0 sits at the TOP, matching the host's "top = high" mapping
		// (a vertical QSlider otherwise puts its minimum at the bottom).
		g_GroundSlider->setInvertedAppearance(true);
		g_GroundSlider->setInvertedControls(true);
		g_GroundSlider->setFocusPolicy(Qt::NoFocus);
		g_GroundSlider->setToolTip(QStringLiteral("Ground height"));
		QObject::connect(g_GroundSlider, &QSlider::valueChanged, [](int pos) {
			if (!g_GroundSliderUpdating && g_GroundSliderChangedCallback != nullptr) {
				g_GroundSliderChangedCallback(pos);
			}
		});
	}

	// Same reparenting dance as CreateTabBar.
	g_GroundSlider->setWindowFlags(g_GroundSlider->windowFlags() | Qt::FramelessWindowHint);

	HWND hwnd = (HWND)g_GroundSlider->winId();
	::SetParent(hwnd, parent);
	LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
	style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME);
	style |= WS_CHILD;
	::SetWindowLong(hwnd, GWL_STYLE, style);

	g_GroundSlider->setAttribute(Qt::WA_Resized, true);
	g_GroundSlider->show();
	return hwnd;
}

void DestroyGroundSlider()
{
	if (g_GroundSlider != nullptr) {
		delete g_GroundSlider;
		g_GroundSlider = nullptr;
	}
}

void ResizeGroundSlider(int width, int height)
{
	if (g_GroundSlider != nullptr) {
		g_GroundSlider->resize(width, height);
	}
}

void SetGroundSliderPos(int pos)
{
	if (g_GroundSlider != nullptr) {
		g_GroundSliderUpdating = true;
		g_GroundSlider->setValue(pos);
		g_GroundSliderUpdating = false;
	}
}

void SetGroundSliderChangedCallback(void (*callback)(int pos))
{
	g_GroundSliderChangedCallback = callback;
}

//////////////////////////////////////////////////////////////////////////////
//	Animation bar

// Palette-tinted media glyphs (play / pause / stop). Fusion's SP_Media* icons
// are fixed dark drawings that all but vanish on the dark theme, so paint the
// glyphs with the current text colour instead (same recipe as the tab-bar
// close cross); Refresh_Anim_Bar_Icons re-tints them on a theme switch.
enum { MEDIA_GLYPH_PLAY = 0, MEDIA_GLYPH_PAUSE, MEDIA_GLYPH_STOP };

static QIcon Make_Media_Icon(int glyph)
{
	const QColor color = QApplication::palette().color(QPalette::WindowText);
	QPixmap pixmap(16, 16);
	pixmap.fill(Qt::transparent);
	{
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(Qt::NoPen);
		painter.setBrush(color);
		switch (glyph) {
			case MEDIA_GLYPH_PLAY: {
				const QPointF triangle[3] = {
					QPointF(5.0, 3.5), QPointF(12.5, 8.0), QPointF(5.0, 12.5) };
				painter.drawPolygon(triangle, 3);
				break;
			}
			case MEDIA_GLYPH_PAUSE:
				painter.drawRect(QRectF(4.5, 3.5, 2.5, 9.0));
				painter.drawRect(QRectF(9.0, 3.5, 2.5, 9.0));
				break;
			case MEDIA_GLYPH_STOP:
			default:
				painter.drawRect(QRectF(4.5, 4.5, 7.0, 7.0));
				break;
		}
	}
	return QIcon(pixmap);
}

QWidget *g_AnimBar = nullptr;
QComboBox *g_AnimCombo = nullptr;
QToolButton *g_AnimPlayButton = nullptr;
QToolButton *g_AnimStopButton = nullptr;
QSlider *g_AnimSlider = nullptr;
QLabel *g_AnimFrameLabel = nullptr;
bool g_AnimBarUpdating = false;
bool g_AnimBarPlaying = false;

// Re-tints the media glyphs after a theme switch (called by ApplyPanelTheme).
static void Refresh_Anim_Bar_Icons()
{
	if (g_AnimPlayButton != nullptr) {
		g_AnimPlayButton->setIcon(Make_Media_Icon(
			g_AnimBarPlaying ? MEDIA_GLYPH_PAUSE : MEDIA_GLYPH_PLAY));
	}
	if (g_AnimStopButton != nullptr) {
		g_AnimStopButton->setIcon(Make_Media_Icon(MEDIA_GLYPH_STOP));
	}
}
void (*g_AnimSelectedCallback)(int index) = nullptr;
void (*g_AnimPlayPauseCallback)() = nullptr;
void (*g_AnimStopCallback)() = nullptr;
void (*g_AnimSeekCallback)(int frame) = nullptr;

HWND CreateAnimBar(HWND parent)
{
	if (!Ensure_Application()) {
		return nullptr;
	}

	if (g_AnimBar == nullptr) {
		g_AnimBar = new QWidget;
		QHBoxLayout *layout = new QHBoxLayout(g_AnimBar);
		layout->setContentsMargins(6, 2, 6, 2);
		layout->setSpacing(6);

		g_AnimCombo = new QComboBox;
		g_AnimCombo->setFocusPolicy(Qt::NoFocus);
		g_AnimCombo->setMinimumWidth(130);
		g_AnimCombo->setToolTip(QStringLiteral("Animation"));
		layout->addWidget(g_AnimCombo);

		g_AnimPlayButton = new QToolButton;
		g_AnimPlayButton->setAutoRaise(true);
		g_AnimPlayButton->setFocusPolicy(Qt::NoFocus);
		g_AnimPlayButton->setIcon(Make_Media_Icon(MEDIA_GLYPH_PLAY));
		g_AnimPlayButton->setToolTip(QStringLiteral("Play / Pause"));
		layout->addWidget(g_AnimPlayButton);

		g_AnimStopButton = new QToolButton;
		g_AnimStopButton->setAutoRaise(true);
		g_AnimStopButton->setFocusPolicy(Qt::NoFocus);
		g_AnimStopButton->setIcon(Make_Media_Icon(MEDIA_GLYPH_STOP));
		g_AnimStopButton->setToolTip(QStringLiteral("Stop (back to frame 0)"));
		layout->addWidget(g_AnimStopButton);

		g_AnimSlider = new QSlider(Qt::Horizontal);
		g_AnimSlider->setFocusPolicy(Qt::NoFocus);
		g_AnimSlider->setRange(0, 0);
		layout->addWidget(g_AnimSlider, 1);

		g_AnimFrameLabel = new QLabel(QStringLiteral("0 / 0"));
		g_AnimFrameLabel->setMinimumWidth(56);
		g_AnimFrameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		layout->addWidget(g_AnimFrameLabel);

		QObject::connect(g_AnimCombo, QOverload<int>::of(&QComboBox::activated), [](int index) {
			if (!g_AnimBarUpdating && g_AnimSelectedCallback != nullptr) {
				g_AnimSelectedCallback(index);
			}
		});
		QObject::connect(g_AnimPlayButton, &QToolButton::clicked, []() {
			if (!g_AnimBarUpdating && g_AnimPlayPauseCallback != nullptr) {
				g_AnimPlayPauseCallback();
			}
		});
		QObject::connect(g_AnimStopButton, &QToolButton::clicked, []() {
			if (!g_AnimBarUpdating && g_AnimStopCallback != nullptr) {
				g_AnimStopCallback();
			}
		});
		QObject::connect(g_AnimSlider, &QSlider::valueChanged, [](int frame) {
			if (!g_AnimBarUpdating && g_AnimSeekCallback != nullptr) {
				g_AnimSeekCallback(frame);
			}
		});
	}

	// Same reparenting dance as CreateTabBar.
	g_AnimBar->setWindowFlags(g_AnimBar->windowFlags() | Qt::FramelessWindowHint);

	HWND hwnd = (HWND)g_AnimBar->winId();
	::SetParent(hwnd, parent);
	LONG style_bits = ::GetWindowLong(hwnd, GWL_STYLE);
	style_bits &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME);
	style_bits |= WS_CHILD;
	::SetWindowLong(hwnd, GWL_STYLE, style_bits);

	g_AnimBar->setAttribute(Qt::WA_Resized, true);
	g_AnimBar->show();
	return hwnd;
}

void DestroyAnimBar()
{
	if (g_AnimBar != nullptr) {
		delete g_AnimBar;
		g_AnimBar = nullptr;
		g_AnimCombo = nullptr;
		g_AnimPlayButton = nullptr;
		g_AnimStopButton = nullptr;
		g_AnimSlider = nullptr;
		g_AnimFrameLabel = nullptr;
	}
}

void ResizeAnimBar(int width, int height)
{
	if (g_AnimBar != nullptr) {
		g_AnimBar->resize(width, height);
	}
}

int GetAnimBarPreferredHeight()
{
	return (g_AnimBar != nullptr) ? g_AnimBar->sizeHint().height() : 0;
}

void SetAnimBarAnims(const char *const *names, int count, int current)
{
	if (g_AnimCombo == nullptr) {
		return;
	}
	g_AnimBarUpdating = true;
	g_AnimCombo->clear();
	for (int i = 0; i < count; i++) {
		QString full = QString::fromLocal8Bit(names[i]);
		int dot = full.lastIndexOf(QLatin1Char('.'));
		g_AnimCombo->addItem(dot >= 0 ? full.mid(dot + 1) : full);
		g_AnimCombo->setItemData(i, full, Qt::ToolTipRole);
	}
	if (current >= 0 && current < count) {
		g_AnimCombo->setCurrentIndex(current);
	}
	g_AnimBarUpdating = false;
}

void SetAnimBarState(bool playing, int frame, int frameCount)
{
	if (g_AnimBar == nullptr) {
		return;
	}
	g_AnimBarUpdating = true;
	if (playing != g_AnimBarPlaying && g_AnimPlayButton != nullptr) {
		g_AnimPlayButton->setIcon(Make_Media_Icon(
			playing ? MEDIA_GLYPH_PAUSE : MEDIA_GLYPH_PLAY));
		g_AnimBarPlaying = playing;
	}
	int last = (frameCount > 0) ? frameCount - 1 : 0;
	if (g_AnimSlider != nullptr) {
		if (g_AnimSlider->maximum() != last) {
			g_AnimSlider->setRange(0, last);
		}
		g_AnimSlider->setValue(frame);
	}
	if (g_AnimFrameLabel != nullptr) {
		g_AnimFrameLabel->setText(QStringLiteral("%1 / %2").arg(frame).arg(last));
	}
	g_AnimBarUpdating = false;
}

void SetAnimBarAnimSelectedCallback(void (*callback)(int index))
{
	g_AnimSelectedCallback = callback;
}

void SetAnimBarPlayPauseCallback(void (*callback)())
{
	g_AnimPlayPauseCallback = callback;
}

void SetAnimBarStopCallback(void (*callback)())
{
	g_AnimStopCallback = callback;
}

void SetAnimBarSeekCallback(void (*callback)(int frame))
{
	g_AnimSeekCallback = callback;
}

void SetTabBarChangedCallback(void (*callback)(int index))
{
	g_TabChangedCallback = callback;
}

void SetTabBarCloseRequestedCallback(void (*callback)(int index))
{
	g_TabCloseRequestedCallback = callback;
}

// Qt colour picker bridged behind a Qt-free (COLORREF) signature. Uses the native
// QColorDialog so it matches the rest of the Material Viewer's Qt UI. Parented to
// the panel widget when one exists.
bool PickColor(COLORREF initial, COLORREF &chosen)
{
	// The dialog needs a live QApplication. It exists once the panel has been
	// created, but this can be invoked from the main frame before the Material
	// Viewer was ever opened, so ensure it here.
	if (!Ensure_Application()) {
		return false;
	}

	QColor start(GetRValue(initial), GetGValue(initial), GetBValue(initial));
	QColor picked = QColorDialog::getColor(start, g_Panel,
		QStringLiteral("Back Face Tint Color"));
	if (!picked.isValid()) {
		return false;	// cancelled
	}
	chosen = RGB(picked.red(), picked.green(), picked.blue());
	return true;
}

void ResizePanel(int width, int height)
{
	if (g_Panel != nullptr) {
		g_Panel->resize(width, height);
	}
}

void SetPanelDocument(const MaterialDocument &document)
{
	if (g_Panel != nullptr) {
		g_Panel->Set_Document(document);
	}
}

void SelectPanelMesh(const char *meshName)
{
	if (g_Panel != nullptr) {
		g_Panel->Select_Mesh(meshName);
	}
}

void SetPanelMeshSelectedCallback(void (*callback)(const char *meshName))
{
	g_MeshSelectedCallback = callback;
}

void SetPanelEditGateCallback(bool (*callback)())
{
	g_EditGateCallback = callback;
}

void SetPanelSaveCallback(bool (*callback)(const MaterialDocument &document))
{
	g_SaveCallback = callback;
}

void SetPanelRevertCallback(void (*callback)())
{
	g_RevertCallback = callback;
}

void SetPanelDirtyChangedCallback(void (*callback)(bool dirty))
{
	g_DirtyChangedCallback = callback;
}

void SetPanelLivePreviewCallback(void (*callback)(const MaterialDocument &document))
{
	g_LivePreviewCallback = callback;
}

void SetPanelResolveTextureCallback(void (*callback)(TextureData &texture))
{
	g_ResolveTextureCallback = callback;
}

bool PanelHasUnsavedChanges()
{
	return g_Panel != nullptr && g_Panel->Has_Unsaved_Changes();
}

bool RequestPanelSave()
{
	return g_Panel == nullptr || g_Panel->Request_Save();
}

bool PanelIsEditing()
{
	return g_Panel != nullptr && g_Panel->Is_Editing();
}

// True when the Qt keyboard focus is on a widget that consumes typed
// characters (a line edit, spin box, or editable combo). The host uses this to
// decide whether a plain letter key (e.g. Shift+F) is the user typing into a
// field or a viewer hotkey it should handle. Focus merely being *inside* the
// panel (a tree, a label, a just-switched tab) is not text entry.
bool PanelFocusIsTextEntry()
{
	QWidget *focus = QApplication::focusWidget();
	if (focus == nullptr) {
		return false;
	}
	// A combo only eats text while it is editable (its internal line edit has
	// focus); a read-only combo just navigates.
	if (QComboBox *combo = qobject_cast<QComboBox *>(focus)) {
		return combo->isEditable();
	}
	return qobject_cast<QLineEdit *>(focus) != nullptr
		|| qobject_cast<QAbstractSpinBox *>(focus) != nullptr
		|| qobject_cast<QPlainTextEdit *>(focus) != nullptr;
}

bool PanelCanUndo()
{
	return g_Panel != nullptr && g_Panel->Can_Undo();
}

bool PanelCanRedo()
{
	return g_Panel != nullptr && g_Panel->Can_Redo();
}

bool PanelCanSaveOrRevert()
{
	return g_Panel != nullptr && g_Panel->Can_Save_Or_Revert();
}

void RequestPanelUndo()
{
	if (g_Panel != nullptr) g_Panel->Menu_Undo();
}

void RequestPanelRedo()
{
	if (g_Panel != nullptr) g_Panel->Menu_Redo();
}

void RequestPanelRevert()
{
	if (g_Panel != nullptr) g_Panel->Menu_Revert();
}

void ApplyPanelTheme(const PanelTheme &theme)
{
	if (g_Panel != nullptr) {
		g_Panel->Apply_Theme(theme);
	}
	Refresh_Tab_Close_Icons();
	Refresh_Anim_Bar_Icons();
}

void PumpQtEvents()
{
	if (g_Application != nullptr) {
		// Window messages (input, paint, Qt's timer window) reach the Qt HWNDs
		// through the regular Win32 pump; only Qt-posted events (deleteLater,
		// layout) need explicit delivery. Deliberately NOT processEvents():
		// that would drain the thread's whole message queue and re-enter MFC
		// window procedures mid render tick.
		QCoreApplication::sendPostedEvents();
	}
}

void ShutdownQt()
{
	DestroyPanel();
	DestroyTabBar();
	DestroyGroundSlider();
	DestroyAnimBar();
	if (g_Application != nullptr) {
		delete g_Application;
		g_Application = nullptr;
	}
}

} // namespace W3dMaterialViewer
