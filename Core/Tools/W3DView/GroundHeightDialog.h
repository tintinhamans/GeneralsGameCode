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

// TheSuperHackers @feature Tria Ground > Height... dialog: a single slider that
// live-updates the main viewport's ground-plane height while dragging (the
// viewport keeps rendering during the modal loop via its posted repaint
// messages). Cancel restores the height the dialog opened with.

#pragma once

#include "resource.h"

class CGroundHeightDialog : public CDialog
{
public:
	CGroundHeightDialog(CWnd *parent = nullptr);

protected:
	virtual BOOL OnInitDialog();
	virtual void OnCancel();
	afx_msg void OnHScroll(UINT sb_code, UINT pos, CScrollBar *scrollbar);
	DECLARE_MESSAGE_MAP()

private:
	float	m_InitialZ;
	float	m_MinZ;
	float	m_MaxZ;
};
