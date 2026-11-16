#pragma once

#include "resource.h"

// Generic resizable-text dialog with a Copy button.
// Usage: LogDialog dlg(IDD_MISSING_TEXTURES_LOG, text); dlg.DoModal();
class LogDialog : public CDialog
{
public:
	LogDialog(UINT nIDD, const CString& text, CWnd* pParent = nullptr)
		: CDialog(nIDD, pParent), m_Text(text) {}

protected:
	BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		SetDlgItemText(IDC_LOG_EDIT, m_Text);
		return TRUE;
	}

	void OnCopy()
	{
		CString text;
		GetDlgItemText(IDC_LOG_EDIT, text);
		if (!OpenClipboard())
			return;
		EmptyClipboard();
		const int len = (text.GetLength() + 1) * sizeof(TCHAR);
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
		if (hMem)
		{
			memcpy(GlobalLock(hMem), (LPCTSTR)text, len);
			GlobalUnlock(hMem);
			SetClipboardData(CF_TEXT, hMem);
		}
		CloseClipboard();
	}

	DECLARE_MESSAGE_MAP()

private:
	CString m_Text;
};
