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

// TheSuperHackers @feature W3DView material viewer.
// Qt-free interface to the Qt material panel. The implementation lives in
// MaterialPanel.cpp, which is compiled into the separate core_w3dview_qtpanel
// static library (no MFC, no WW3D, no tool PCH). Everything here is guarded by
// W3DVIEW_HAS_QT so the tool builds without Qt.

#pragma once

#ifdef W3DVIEW_HAS_QT

#include <windows.h>
#include "W3dMaterialData.h"

namespace W3dMaterialViewer
{

struct PanelTheme
{
	bool		dark = false;
	COLORREF	background = 0;
	COLORREF	ctrlBackground = 0;
	COLORREF	text = 0;
	COLORREF	edge = 0;
};

// Creates the (single) Qt material panel as a WS_CHILD of `parent`, lazily
// initializing the QApplication. Returns null if Qt failed to initialize.
HWND CreatePanel(HWND parent);

// Destroys the panel widget. Must be called before the parent window dies.
void DestroyPanel();

HWND GetPanelHwnd();

// Resizes the panel through Qt so its logical geometry and layout stay in
// sync. Sizing the HWND natively (MoveWindow) leaves Qt believing the old
// size and it stops painting short of the real bottom edge.
void ResizePanel(int width, int height);

void SetPanelDocument(const MaterialDocument &document);

// Selects the given mesh ("Container.Mesh") in the panel's mesh combo, if present.
void SelectPanelMesh(const char *meshName);

//////////////////////////////////////////////////////////////////////////////
//	Edit mode
//////////////////////////////////////////////////////////////////////////////

// Invoked when the user toggles Edit on. The host runs the file-integrity gate
// (a load/write round trip of the file must reproduce it byte-identically);
// return false to refuse edit mode (the toggle snaps back to View).
void SetPanelEditGateCallback(bool (*callback)());

// Invoked when the user clicks Save. Receives the edited document; the host
// writes it back to disk and refreshes the preview. Return true on success
// (the panel clears its dirty flag), false to keep the edits dirty.
void SetPanelSaveCallback(bool (*callback)(const MaterialDocument &document));

// Invoked when the user clicks Revert (or confirms "Discard"): the host
// re-parses the file from disk and calls SetPanelDocument.
void SetPanelRevertCallback(void (*callback)());

// Invoked whenever the panel's dirty state changes, so the host can show an
// unsaved-changes marker in its title.
void SetPanelDirtyChangedCallback(void (*callback)(bool dirty));

// Invoked after every committed edit so the host can refresh the live 3D
// preview from the in-memory document (typically debounced). Independent of
// Save: the file is not written.
void SetPanelLivePreviewCallback(void (*callback)(const MaterialDocument &document));

// Decodes a preview thumbnail for a texture filename (needs the D3D device +
// file factory, which live on the host side). Fills `resolvedPath` and the
// preview pixels of `texture` (already carrying the name); leaves them empty
// when the file cannot be found/decoded. Lets the Textures tab refresh its
// thumbnail live when the filename is edited.
void SetPanelResolveTextureCallback(void (*callback)(TextureData &texture));

// True when edit mode has unsaved changes. Lets the host prompt before the
// document is replaced (File->Open) or the window closes.
bool PanelHasUnsavedChanges();

// Asks the panel to run its Save action (as if the Save button was clicked).
// Returns true if the document was clean or the save succeeded.
bool RequestPanelSave();

//////////////////////////////////////////////////////////////////////////////
//	Edit-mode menu bridge
//
// Let the host frame mirror the panel's Undo / Redo / Revert / Save actions on
// its menu bar. The Can* queries drive ON_UPDATE_COMMAND_UI enable state; the
// Request* calls perform the action (as if the matching panel button was
// clicked). All are no-ops / return false when not in edit mode.

bool PanelIsEditing();

// True when Qt keyboard focus is on a text-entry widget (line edit / spin box /
// editable combo). Lets the host tell "typing into a field" apart from "the
// panel merely has focus", so plain-letter viewer hotkeys still work when focus
// is on the panel but not in an editable field (e.g. right after a tab switch).
bool PanelFocusIsTextEntry();

bool PanelCanUndo();
bool PanelCanRedo();
bool PanelCanSaveOrRevert();	// true when editing and dirty

void RequestPanelUndo();
void RequestPanelRedo();
void RequestPanelRevert();

// Invoked (synchronously, on the UI thread) whenever the panel's mesh combo
// selection changes; receives the mesh name. Pass null to clear.
void SetPanelMeshSelectedCallback(void (*callback)(const char *meshName));

//////////////////////////////////////////////////////////////////////////////
//	File tab bar
//
// A QTabBar hosted as a WS_CHILD of the MFC frame (same reparenting dance as
// the panel), so the Material Viewer's file tabs follow the Qt dark theme with
// no owner-draw. One tab per open .w3d file; the host owns the tab list and
// mirrors it in with SetTabBarTabs. Close buttons are shown on every tab; the
// close request is forwarded to the host, which decides (dirty prompt) whether
// the tab actually goes.

HWND CreateTabBar(HWND parent);
void DestroyTabBar();
void ResizeTabBar(int width, int height);

// Preferred strip height for the current style/font (0 when no tab bar).
int GetTabBarPreferredHeight();

// Replaces all tabs. `labels` are shown, `tooltips` (full paths, may be null)
// appear on hover, `active` is the selected index. Emits no changed callback.
void SetTabBarTabs(const char *const *labels, const char *const *tooltips,
	int count, int active);

// Updates one tab's label in place (dirty '*' marker) without rebuilding.
void SetTabBarLabel(int index, const char *label);

// Selection snapped back without going through SetTabBarTabs (e.g. a cancelled
// switch). Emits no changed callback.
void SetTabBarCurrent(int index);

// Invoked (synchronously, on the UI thread) when the USER selects a different
// tab / clicks a tab's close button. Programmatic updates never fire these.
void SetTabBarChangedCallback(void (*callback)(int index));
void SetTabBarCloseRequestedCallback(void (*callback)(int index));

//////////////////////////////////////////////////////////////////////////////
//	Ground-height slider
//
// A vertical QSlider hosted as a WS_CHILD of the MFC frame (same reparenting
// dance as the tab bar), so the ground-height control follows the Qt dark
// theme instead of the classic Win32 trackbar look. Positions run 0..steps
// with 0 at the TOP (the host maps top = high).

HWND CreateGroundSlider(HWND parent, int steps);
void DestroyGroundSlider();
void ResizeGroundSlider(int width, int height);

// Programmatic thumb move; emits no changed callback.
void SetGroundSliderPos(int pos);

// Invoked (synchronously, on the UI thread) when the USER moves the slider.
void SetGroundSliderChangedCallback(void (*callback)(int pos));

//////////////////////////////////////////////////////////////////////////////
//	Animation bar
//
// A thin Qt playback strip hosted under the 3D preview (same reparenting dance
// as the tab bar): animation combo, play/pause, stop, frame slider and a frame
// counter. The bar is pure UI — the host owns playback (the preview pane
// advances the frames). Programmatic updates never fire the callbacks.

HWND CreateAnimBar(HWND parent);
void DestroyAnimBar();
void ResizeAnimBar(int width, int height);

// Preferred strip height for the current style/font (0 when no bar).
int GetAnimBarPreferredHeight();

// Replaces the animation list. `names` are the full asset names
// ("HTREE.ANIMNAME"); the combo displays the part after the last '.'.
// Selects index `current`. Emits no callbacks.
void SetAnimBarAnims(const char *const *names, int count, int current);

// Pushes playback state into the bar: play/pause glyph, slider range/position,
// and the "frame / last" counter. Emits no callbacks.
void SetAnimBarState(bool playing, int frame, int frameCount);

// User interaction callbacks (synchronous, on the UI thread).
void SetAnimBarAnimSelectedCallback(void (*callback)(int index));
void SetAnimBarPlayPauseCallback(void (*callback)());
void SetAnimBarStopCallback(void (*callback)());
void SetAnimBarSeekCallback(void (*callback)(int frame));

void ApplyPanelTheme(const PanelTheme &theme);

// Delivers Qt posted events/timers. Qt's native child windows receive input
// through the regular Win32 message pump; this handles the rest. Cheap no-op
// when the QApplication does not exist.
void PumpQtEvents();

// Deletes the QApplication. Call at app exit, after DestroyPanel.
void ShutdownQt();

// Opens the Qt colour picker (QColorDialog) seeded with `initial`, parented off
// the panel if it exists. On OK, writes the chosen colour to `chosen` and returns
// true; returns false if the user cancelled. Qt-free signature (COLORREF) so MFC
// callers need no Qt headers.
bool PickColor(COLORREF initial, COLORREF &chosen);

} // namespace W3dMaterialViewer

#endif // W3DVIEW_HAS_QT
