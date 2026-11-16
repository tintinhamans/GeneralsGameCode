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

// BackgroundColorDialog.cpp : implementation file
//

#include "StdAfx.h"
#include "W3DView.h"
#include "BackgroundColorDialog.h"
#include "MainFrm.h"
#include "W3DViewDoc.h"
#include "Utils.h"


#ifdef RTS_DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CBackgroundColorDialog dialog


CBackgroundColorDialog::CBackgroundColorDialog(CWnd* pParent /*=nullptr*/)
	: CDialog(CBackgroundColorDialog::IDD, pParent)
{
	//{{AFX_DATA_INIT(CBackgroundColorDialog)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


void CBackgroundColorDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CBackgroundColorDialog)
	DDX_Control(pDX, IDC_SLIDER_BLUE, m_blueSlider);
	DDX_Control(pDX, IDC_SLIDER_GREEN, m_greenSlider);
	DDX_Control(pDX, IDC_SLIDER_RED, m_redSlider);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CBackgroundColorDialog, CDialog)
	//{{AFX_MSG_MAP(CBackgroundColorDialog)
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_GRAYSCALE_CHECK, OnGrayscaleCheck)
	ON_BN_CLICKED(IDC_COLOR_PICK_BTN, OnColorPick)
	ON_WM_DRAWITEM()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CBackgroundColorDialog message handlers

/////////////////////////////////////////////////////////////
//
//  OnInitDialog
//
BOOL
CBackgroundColorDialog::OnInitDialog ()
{
	// Allow the base class to process this message
    CDialog::OnInitDialog ();

    // Center the dialog around the data tree view instead
    // of the direct center of the screen
    ::CenterDialogAroundTreeView (m_hWnd);

    m_redSlider.SetRange (0, 100);
    m_greenSlider.SetRange (0, 100);
    m_blueSlider.SetRange (0, 100);

    // Get a pointer to the doc so we can get at the current
    // background color
    CW3DViewDoc *pCDoc = ::GetCurrentDocument ();
    if (pCDoc)
    {
        // Get the background color from the document
        Vector3 colorSettings = pCDoc->GetBackgroundColor ();

        // Remember these initial settings so we can restore them
        // if the user cancels
        m_initialRed = int(colorSettings.X * 100.00F);
        m_initialGreen = int(colorSettings.Y * 100.00F);
        m_initialBlue = int(colorSettings.Z * 100.00F);
    }

    if ((m_initialRed == m_initialGreen) &&
        (m_initialRed == m_initialBlue))
    {
        // Check the grayscale checkbox
        SendDlgItemMessage (IDC_GRAYSCALE_CHECK, BM_SETCHECK, (WPARAM)TRUE);
    }

    // Set the initial slider position
    m_redSlider.SetPos (m_initialRed);
    m_greenSlider.SetPos (m_initialGreen);
    m_blueSlider.SetPos (m_initialBlue);

    // TheSuperHackers @feature Tria 18/04/2026 Enable owner-draw for color swatch.
    HWND hSwatch = ::GetDlgItem (m_hWnd, IDC_COLOR_SWATCH);
    if (hSwatch) {
        ::SetWindowLong (hSwatch, GWL_STYLE, ::GetWindowLong (hSwatch, GWL_STYLE) | SS_OWNERDRAW);
    }
    Update_Color_Swatch ();

	return TRUE;
}


//////////////////////////////////////////////////////////////
//
//  OnHScroll
//
void
CBackgroundColorDialog::OnHScroll
(
    UINT nSBCode,
    UINT nPos,
    CScrollBar* pScrollBar
)
{
    if (SendDlgItemMessage (IDC_GRAYSCALE_CHECK, BM_GETCHECK))
    {
        int iCurrentPos = 0;
        if (pScrollBar == GetDlgItem (IDC_SLIDER_RED))
        {
            iCurrentPos = m_redSlider.GetPos ();
        }
        else if (pScrollBar == GetDlgItem (IDC_SLIDER_GREEN))
        {
            iCurrentPos = m_greenSlider.GetPos ();
        }
        else
        {
            iCurrentPos = m_blueSlider.GetPos ();
        }

        // Make all the sliders the same pos
        m_redSlider.SetPos (iCurrentPos);
        m_greenSlider.SetPos (iCurrentPos);
        m_blueSlider.SetPos (iCurrentPos);
    }

    Vector3 colorSettings;
    colorSettings.X = float(m_redSlider.GetPos ()) / 100.00F;
    colorSettings.Y = float(m_greenSlider.GetPos ()) / 100.00F;
    colorSettings.Z = float(m_blueSlider.GetPos ()) / 100.00F;

    // Get a pointer to the document so we can change the current
    // background color
    CW3DViewDoc *pCDoc = ::GetCurrentDocument ();
    if (pCDoc)
    {
        // Modify the ambient light for this scene
        pCDoc->SetBackgroundColor (colorSettings);
    }

    Update_Color_Swatch ();

	// Allow the base class to process this message
    CDialog::OnHScroll (nSBCode, nPos, pScrollBar);
}

//////////////////////////////////////////////////////////////
//
//  OnGrayscaleCheck
//
void
CBackgroundColorDialog::OnGrayscaleCheck ()
{
    if (SendDlgItemMessage (IDC_GRAYSCALE_CHECK, BM_GETCHECK))
    {
        // Make the green and blue sliders the same as red
        m_greenSlider.SetPos (m_redSlider.GetPos ());
        m_blueSlider.SetPos (m_redSlider.GetPos ());

        Vector3 colorSettings;
        colorSettings.X = float(m_redSlider.GetPos ()) / 100.00F;
        colorSettings.Y = float(m_greenSlider.GetPos ()) / 100.00F;
        colorSettings.Z = float(m_blueSlider.GetPos ()) / 100.00F;

        // Get a pointer to the document so we can change the background
        // color
        CW3DViewDoc *pCDoc = ::GetCurrentDocument ();
        if (pCDoc)
        {
            // Modify the current background color
            pCDoc->SetBackgroundColor (colorSettings);
        }

        Update_Color_Swatch ();
    }
}

//////////////////////////////////////////////////////////////
//
//  OnCancel
//
void
CBackgroundColorDialog::OnCancel ()
{
    Vector3 colorSettings;
    colorSettings.X = float(m_initialRed) / 100.00F;
    colorSettings.Y = float(m_initialGreen) / 100.00F;
    colorSettings.Z = float(m_initialBlue) / 100.00F;

    // Get a pointer to the document so we can change the
    // background color
    CW3DViewDoc *pCDoc = ::GetCurrentDocument ();
    if (pCDoc)
    {
        // Restore the current background color
        pCDoc->SetBackgroundColor (colorSettings);
    }

	// Allow the base class to process this message
    CDialog::OnCancel();
}

//////////////////////////////////////////////////////////////
//
//  WindowProc
//
LRESULT
CBackgroundColorDialog::WindowProc
(
    UINT message,
    WPARAM wParam,
    LPARAM lParam
)
{
    if (message == WM_PAINT)
    {
        // Paint the gradients for each color
        ::Paint_Gradient (::GetDlgItem (m_hWnd, IDC_RED_GRADIENT), 1, 0, 0);
        ::Paint_Gradient (::GetDlgItem (m_hWnd, IDC_GREEN_GRADIENT), 0, 1, 0);
        ::Paint_Gradient (::GetDlgItem (m_hWnd, IDC_BLUE_GRADIENT), 0, 0, 1);
    }

	// Allow the base class to process this message
    return CDialog::WindowProc (message, wParam, lParam);
}

// TheSuperHackers @feature Tria 18/04/2026 Color swatch and picker for background color dialog.

//////////////////////////////////////////////////////////////
//
//  Update_Color_Swatch
//
void
CBackgroundColorDialog::Update_Color_Swatch (void)
{
    HWND hSwatch = ::GetDlgItem (m_hWnd, IDC_COLOR_SWATCH);
    if (hSwatch) {
        ::InvalidateRect (hSwatch, nullptr, TRUE);
    }
}

//////////////////////////////////////////////////////////////
//
//  OnDrawItem
//
void
CBackgroundColorDialog::OnDrawItem (int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (nIDCtl == IDC_COLOR_SWATCH) {
        int r = (m_redSlider.GetPos () * 255) / 100;
        int g = (m_greenSlider.GetPos () * 255) / 100;
        int b = (m_blueSlider.GetPos () * 255) / 100;
        HBRUSH hBrush = ::CreateSolidBrush (RGB (r, g, b));
        ::FillRect (lpDrawItemStruct->hDC, &lpDrawItemStruct->rcItem, hBrush);
        ::DeleteObject (hBrush);
        ::DrawEdge (lpDrawItemStruct->hDC, &lpDrawItemStruct->rcItem, EDGE_SUNKEN, BF_RECT);
        return;
    }
    CDialog::OnDrawItem (nIDCtl, lpDrawItemStruct);
}

//////////////////////////////////////////////////////////////
//
//  OnColorPick
//
void
CBackgroundColorDialog::OnColorPick (void)
{
    int r = (m_redSlider.GetPos () * 255) / 100;
    int g = (m_greenSlider.GetPos () * 255) / 100;
    int b = (m_blueSlider.GetPos () * 255) / 100;

    static COLORREF s_CustomColors[16] = { 0 };

    CHOOSECOLOR cc = { 0 };
    cc.lStructSize = sizeof (cc);
    cc.hwndOwner = m_hWnd;
    cc.rgbResult = RGB (r, g, b);
    cc.lpCustColors = s_CustomColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (::ChooseColor (&cc)) {
        int newR = (GetRValue (cc.rgbResult) * 100) / 255;
        int newG = (GetGValue (cc.rgbResult) * 100) / 255;
        int newB = (GetBValue (cc.rgbResult) * 100) / 255;

        m_redSlider.SetPos (newR);
        m_greenSlider.SetPos (newG);
        m_blueSlider.SetPos (newB);

        if (newR == newG && newR == newB) {
            SendDlgItemMessage (IDC_GRAYSCALE_CHECK, BM_SETCHECK, (WPARAM)TRUE);
        } else {
            SendDlgItemMessage (IDC_GRAYSCALE_CHECK, BM_SETCHECK, (WPARAM)FALSE);
        }

        Vector3 colorSettings;
        colorSettings.X = float(newR) / 100.00F;
        colorSettings.Y = float(newG) / 100.00F;
        colorSettings.Z = float(newB) / 100.00F;

        CW3DViewDoc *pCDoc = ::GetCurrentDocument ();
        if (pCDoc) {
            pCDoc->SetBackgroundColor (colorSettings);
        }

        Update_Color_Swatch ();
    }
}
