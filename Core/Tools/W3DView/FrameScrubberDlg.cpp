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
// for seeking to a specific animation frame.

#include "StdAfx.h"
#include "FrameScrubberDlg.h"
#include "resource.h"
#include "W3DViewDoc.h"
#include "GraphicView.h"
#include "MainFrm.h"


BEGIN_MESSAGE_MAP(CFrameScrubberDlg, CDialog)
	ON_WM_HSCROLL()
	ON_EN_CHANGE(IDC_FRAME_GOTO_EDIT, OnGoToFrame)
	ON_BN_CLICKED(IDC_FRAME_REVERSE_BTN, OnReversePlay)
	ON_WM_TIMER()
END_MESSAGE_MAP()


CFrameScrubberDlg::CFrameScrubberDlg (CWnd *pParent)
	: CDialog (IDD_FRAME_SCRUBBER, pParent),
	  m_iTotalFrames (0),
	  m_bReversePlaying (false)
{
}


BOOL
CFrameScrubberDlg::OnInitDialog ()
{
	CDialog::OnInitDialog ();

	m_slider.SubclassDlgItem (IDC_FRAME_SLIDER, this);
	m_label.SubclassDlgItem (IDC_FRAME_LABEL, this);
	m_frameEdit.SubclassDlgItem (IDC_FRAME_GOTO_EDIT, this);
	m_slider.SetRange (0, 0);
	m_slider.SetPos (0);
	m_frameEdit.SetWindowText ("0");

	return TRUE;
}


void
CFrameScrubberDlg::OnCancel ()
{
	// Stop reverse playback if active
	if (m_bReversePlaying) {
		KillTimer (REVERSE_TIMER_ID);
		m_bReversePlaying = false;
	}
	// Hide instead of destroy so we can reuse it
	ShowWindow (SW_HIDE);
}


void
CFrameScrubberDlg::UpdateSlider (int iCurrentFrame, int iTotalFrames)
{
	if (m_hWnd == nullptr || !IsWindowVisible ())
		return;

	m_iTotalFrames = iTotalFrames;

	if (m_slider.GetRangeMax () != iTotalFrames)
		m_slider.SetRange (0, iTotalFrames);

	m_slider.SetPos (iCurrentFrame);

	CString label;
	label.Format ("Frame %d / %d", iCurrentFrame, iTotalFrames);
	m_label.SetWindowText (label);
}


void
CFrameScrubberDlg::SeekToFrame (int iFrame)
{
	CW3DViewDoc *doc = (CW3DViewDoc *)((CMainFrame *)::AfxGetMainWnd ())->GetActiveDocument ();
	if (doc == nullptr || doc->GetCurrentAnimation () == nullptr)
		return;

	int iTotalFrames = doc->GetCurrentAnimation ()->Get_Num_Frames ();
	if (iFrame < 0) iFrame = 0;
	if (iFrame >= iTotalFrames) iFrame = iTotalFrames - 1;

	int iDelta = iFrame - doc->GetCurrentFrame ();
	if (iDelta != 0)
		doc->StepAnimation (iDelta);
}


void
CFrameScrubberDlg::OnHScroll (UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	if (pScrollBar != nullptr && pScrollBar->GetDlgCtrlID () == IDC_FRAME_SLIDER)
	{
		int iFrame = m_slider.GetPos ();

		// Pause the animation while scrubbing
		CMainFrame *pMainFrame = (CMainFrame *)::AfxGetMainWnd ();
		CGraphicView *pCGraphicView = (CGraphicView *)pMainFrame->GetPane (0, 1);
		if (pCGraphicView != nullptr && pCGraphicView->GetAnimationState () == CGraphicView::AnimPlaying)
			pCGraphicView->SetAnimationState (CGraphicView::AnimPaused);

		// Stop reverse if active
		if (m_bReversePlaying) {
			KillTimer (REVERSE_TIMER_ID);
			m_bReversePlaying = false;
		}

		SeekToFrame (iFrame);
		return;
	}

	CDialog::OnHScroll (nSBCode, nPos, pScrollBar);
}


void
CFrameScrubberDlg::OnGoToFrame ()
{
	if (m_frameEdit.m_hWnd == nullptr)
		return;

	CString text;
	m_frameEdit.GetWindowText (text);
	if (text.IsEmpty ())
		return;

	Go_To_Frame (::atoi ((LPCTSTR)text));
}


bool
CFrameScrubberDlg::Go_To_Frame (int iFrame)
{
	CMainFrame *pMainFrame = (CMainFrame *)::AfxGetMainWnd ();
	CW3DViewDoc *doc = (CW3DViewDoc *)pMainFrame->GetActiveDocument ();
	if (doc == nullptr || doc->GetCurrentAnimation () == nullptr)
		return false;

	// Pause normal playback
	CGraphicView *pCGraphicView = (CGraphicView *)pMainFrame->GetPane (0, 1);
	if (pCGraphicView != nullptr && pCGraphicView->GetAnimationState () == CGraphicView::AnimPlaying)
		pCGraphicView->SetAnimationState (CGraphicView::AnimPaused);

	// Stop reverse if active
	if (m_bReversePlaying) {
		KillTimer (REVERSE_TIMER_ID);
		m_bReversePlaying = false;
	}

	SeekToFrame (iFrame);
	return true;
}


void
CFrameScrubberDlg::OnReversePlay ()
{
	if (m_bReversePlaying) {
		// Stop reverse playback
		KillTimer (REVERSE_TIMER_ID);
		m_bReversePlaying = false;
		return;
	}

	CW3DViewDoc *doc = (CW3DViewDoc *)((CMainFrame *)::AfxGetMainWnd ())->GetActiveDocument ();
	if (doc == nullptr || doc->GetCurrentAnimation () == nullptr)
		return;

	// Pause normal forward playback
	CMainFrame *pMainFrame = (CMainFrame *)::AfxGetMainWnd ();
	CGraphicView *pCGraphicView = (CGraphicView *)pMainFrame->GetPane (0, 1);
	if (pCGraphicView != nullptr && pCGraphicView->GetAnimationState () == CGraphicView::AnimPlaying)
		pCGraphicView->SetAnimationState (CGraphicView::AnimPaused);

	// Start reverse playback using a timer at the animation's frame rate
	float frameRate = doc->GetCurrentAnimation ()->Get_Frame_Rate ();
	if (frameRate <= 0.0f) frameRate = 30.0f;
	UINT interval = (UINT)(1000.0f / frameRate);
	if (interval < 1) interval = 1;

	m_bReversePlaying = true;
	SetTimer (REVERSE_TIMER_ID, interval, nullptr);
}


void
CFrameScrubberDlg::OnTimer (UINT_PTR nIDEvent)
{
	if (nIDEvent == REVERSE_TIMER_ID) {
		CW3DViewDoc *doc = (CW3DViewDoc *)((CMainFrame *)::AfxGetMainWnd ())->GetActiveDocument ();
		if (doc != nullptr && doc->GetCurrentAnimation () != nullptr) {
			doc->StepAnimation (-1);
		} else {
			KillTimer (REVERSE_TIMER_ID);
			m_bReversePlaying = false;
		}
		return;
	}

	CDialog::OnTimer (nIDEvent);
}
