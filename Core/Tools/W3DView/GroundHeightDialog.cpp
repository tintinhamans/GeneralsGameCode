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

#include "StdAfx.h"
#include "GroundHeightDialog.h"
#include "GraphicView.h"
#include "Utils.h"

namespace
{
	// Slider resolution across the height range.
	const int GROUND_HEIGHT_STEPS = 1000;
}

BEGIN_MESSAGE_MAP(CGroundHeightDialog, CDialog)
	ON_WM_HSCROLL()
END_MESSAGE_MAP()

CGroundHeightDialog::CGroundHeightDialog(CWnd *parent)
	: CDialog(IDD_GROUND_HEIGHT, parent),
	  m_InitialZ(0.0F),
	  m_MinZ(-10.0F),
	  m_MaxZ(10.0F)
{
}

BOOL
CGroundHeightDialog::OnInitDialog()
{
	CDialog::OnInitDialog();

	CGraphicView *view = ::Get_Graphic_View();
	if (view != nullptr) {
		m_InitialZ = view->Get_Ground_Z();
		// Without a displayed object there is no bounding box to derive a
		// range from; offer a window around the current height instead.
		if (!view->Get_Ground_Z_Range(m_MinZ, m_MaxZ)) {
			m_MinZ = m_InitialZ - 10.0F;
			m_MaxZ = m_InitialZ + 10.0F;
		}
	}

	CSliderCtrl *slider = (CSliderCtrl *)GetDlgItem(IDC_GROUND_HEIGHT_SLIDER);
	if (slider != nullptr) {
		slider->SetRange(0, GROUND_HEIGHT_STEPS);
		float z = m_InitialZ;
		if (z < m_MinZ) {
			z = m_MinZ;
		}
		if (z > m_MaxZ) {
			z = m_MaxZ;
		}
		float frac = (m_MaxZ > m_MinZ) ? (z - m_MinZ) / (m_MaxZ - m_MinZ) : 0.5F;
		slider->SetPos((int)(frac * GROUND_HEIGHT_STEPS + 0.5F));
	}
	return TRUE;
}

// Trackbars report every notch through WM_HSCROLL; GetPos is live during a
// thumb drag, and the viewport renders each change immediately.
void
CGroundHeightDialog::OnHScroll(UINT sb_code, UINT pos, CScrollBar *scrollbar)
{
	CSliderCtrl *slider = (CSliderCtrl *)GetDlgItem(IDC_GROUND_HEIGHT_SLIDER);
	CGraphicView *view = ::Get_Graphic_View();
	if (slider != nullptr && view != nullptr
			&& scrollbar != nullptr && scrollbar->GetSafeHwnd() == slider->GetSafeHwnd()) {
		float frac = (float)slider->GetPos() / (float)GROUND_HEIGHT_STEPS;
		view->Set_Ground_Z(m_MinZ + frac * (m_MaxZ - m_MinZ));
		return;
	}
	CDialog::OnHScroll(sb_code, pos, scrollbar);
}

void
CGroundHeightDialog::OnCancel()
{
	CGraphicView *view = ::Get_Graphic_View();
	if (view != nullptr) {
		view->Set_Ground_Z(m_InitialZ);
	}
	CDialog::OnCancel();
}
