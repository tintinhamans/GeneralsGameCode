/*
**	Command & Conquer Generals Zero Hour(tm)
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

#pragma once

// teamsdialog.h : header file
//

#include "GameLogic/SidesList.h"

/////////////////////////////////////////////////////////////////////////////
// CTeamsDialog dialog

class CTeamsDialog : public CDialog
{
// Construction
public:
	CTeamsDialog(CWnd* pParent = nullptr);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CTeamsDialog)
	enum { IDD = IDD_TEAMS_DIALOG };
		// NOTE: the ClassWizard will add data members here
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CTeamsDialog)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX) override;    // DDX/DDV support
	//}}AFX_VIRTUAL

#ifdef RTS_HAS_QT
// Qt seam (Tier 3b-2): the Qt Teams dialog drives THIS dialog created hidden -- the
// working-copy m_sides model and every handler (new/copy/delete/move/import/export, the
// team property sheet, the fix-owner validation) are reused verbatim. All definitions live
// in src/WBQtTeamsBridge.cpp (member functions may be defined in any TU).
public:
	static CTeamsDialog *qtOpen(void);
	static void qtClose(int accepted);
	static CTeamsDialog *qtInstance(void);
	int  qtPlayerCount(void);
	void qtPlayerName(int i, char *buf, int cap);
	int  qtPlayerCurSel(void);
	void qtSelectPlayer(int i);
	int  qtTeamRowCount(void);
	void qtTeamRowText(int row, int col, char *buf, int cap);
	int  qtTeamRowSelected(int row);
	void qtSelectTeamRow(int row);
	int  qtIsCtrlEnabled(int ctrlId);
	void qtNewTeam(void);
	void qtDeleteTeam(void);
	void qtCopyTeam(void);
	void qtEditTeam(void);
	void qtSelectTeamMembers(void);
	void qtMoveUpTeam(void);
	void qtMoveDownTeam(void);
	void qtExportTeams(void);
	void qtImportTeams(void);
	void qtCommit(void);
	// Undo history over the working copy: every action is bracketed by Begin and End.
	void qtBeginEdit(void);
	void qtEndEdit(void);
	int  qtStepHistory(int redo);
	void qtSaveNow(void);	// commit and save the map, dialog stays open
	// Tier 3b-3: the Qt team property sheet binds hidden Team* pages to the current team.
	void *qtCurTeamDict(void);
	void *qtSides(void);
	int  qtCurTeamIsDefault(void);
	// De-bridged (windowless) refresh of the bridge-side view model from m_sides (== updateUI
	// minus the controls; rebuildRows == the REBUILD_TEAMS bit). Defined in WBQtTeamsBridge.cpp.
	void qtMRefresh(int rebuildRows);
#endif

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CTeamsDialog)
	virtual BOOL OnInitDialog() override;
	virtual void OnOK() override;
	virtual void OnCancel() override;
	afx_msg void OnNewteam();
	afx_msg void OnDeleteteam();
	afx_msg void OnEditTemplate();
	afx_msg void OnSelchangePlayerList();
	afx_msg void OnClickTeamsList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDblclkTeamsList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnCopyteam();
	afx_msg void OnSelectTeamMembers();
	afx_msg void OnMoveDownTeam();
	afx_msg void OnMoveUpTeam();
	// afx_msg void OnSizing(UINT fwSide, LPRECT pRect);
	afx_msg void OnExpandOrShrink();

	static Bool ParseTeamsDataChunk(DataChunkInput &file, DataChunkInfo *info, void *userData);
	
	afx_msg void OnExportTeams();
	afx_msg void OnImportTeams();

	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

	Int								m_updating;
	SidesList						m_sides;
	Int								m_curTeam;
	AsciiString                     m_importTargetPlayer;

	enum
	{
		REBUILD_TEAMS = 0x01,
		REBUILD_OWNERS = 0x02,
		REBUILD_ALLIES = 0x04,

		REBUILD_NONE = 0x00,
		REBUILD_ALL = 0xff
	};

	Bool m_expanded;

	void updateUI(Int whatToRebuild);
	void validateTeamOwners();
	Bool isValidTeamOwner( AsciiString ownerName );
	void doCorrectTeamOwnerDialog( TeamsInfo *ti );

private:
	void UpdateTeamsList();

	// Required for bug fix for the move and down button - Adriane [Deathscythe]
	int findNextTeamIndex(int curIndex); 
	int findPrevTeamIndex(int curIndex);
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.
