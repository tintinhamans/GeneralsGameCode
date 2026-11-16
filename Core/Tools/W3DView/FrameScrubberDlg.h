/*
**	Command & Conquer Renegade(tm)
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

// TheSuperHackers @feature Tria 18/04/2026 Modeless frame scrubber dialog
// for seeking to a specific animation frame. Opened by clicking the Frame
// pane in the status bar.

#pragma once

class CFrameScrubberDlg : public CDialog
{
public:
	CFrameScrubberDlg (CWnd *pParent = nullptr);

	void UpdateSlider (int iCurrentFrame, int iTotalFrames);

	// Pauses playback and seeks, as typing a frame in the box does. False without an animation.
	bool Go_To_Frame (int iFrame);

protected:
	virtual BOOL OnInitDialog ();
	virtual void OnCancel ();
	afx_msg void OnHScroll (UINT nSBCode, UINT nPos, CScrollBar *pScrollBar);
	afx_msg void OnGoToFrame ();
	afx_msg void OnReversePlay ();
	afx_msg void OnTimer (UINT_PTR nIDEvent);
	DECLARE_MESSAGE_MAP()

private:
	void SeekToFrame (int iFrame);

	CSliderCtrl m_slider;
	CStatic m_label;
	CEdit m_frameEdit;
	int m_iTotalFrames;
	bool m_bReversePlaying;

	static const UINT_PTR REVERSE_TIMER_ID = 1;
};
