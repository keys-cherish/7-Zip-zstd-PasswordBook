// PasswordDialog.cpp

#include "StdAfx.h"

#include "PasswordDialog.h"
#include "PasswordBookDialog.h"
#include "PasswordBookDialogRes.h"

#ifdef Z7_LANG
#include "LangUtils.h"
#endif

#ifdef Z7_LANG
static const UInt32 kLangIDs[] =
    {
        IDT_PASSWORD_ENTER,
        IDX_PASSWORD_SHOW};
#endif

void CPasswordDialog::ReadControls()
{
  _passwordEdit.GetText(Password);
  ShowPassword = IsButtonCheckedBool(IDX_PASSWORD_SHOW);
}

void CPasswordDialog::SetTextSpec()
{
  _passwordEdit.SetPasswordChar(ShowPassword ? 0 : TEXT('*'));
  _passwordEdit.SetText(Password);
}

bool CPasswordDialog::OnInit()
{
#ifdef Z7_LANG
  LangSetWindowText(*this, IDD_PASSWORD);
  LangSetDlgItems(*this, kLangIDs, Z7_ARRAY_SIZE(kLangIDs));
#endif
  _passwordEdit.Attach(GetItem(IDE_PASSWORD_PASSWORD));
  CheckButton(IDX_PASSWORD_SHOW, ShowPassword);
  SetTextSpec();
  return CModalDialog::OnInit();
}

bool CPasswordDialog::OnButtonClicked(unsigned buttonID, HWND buttonHWND)
{
  if (buttonID == IDX_PASSWORD_SHOW)
  {
    ReadControls();
    SetTextSpec();
    return true;
  }
  if (buttonID == IDB_PASSWORD_BOOK)
  {
    OnPasswordBook();
    return true;
  }
  return CDialog::OnButtonClicked(buttonID, buttonHWND);
}

void CPasswordDialog::OnPasswordBook()
{
  // Get current password from edit control
  ReadControls();

  CPasswordBookDialog dialog;

  if (dialog.Create(*this) == IDOK && dialog.PasswordSelected)
  {
    Password = dialog.SelectedPassword;
    SetTextSpec();
  }
}

void CPasswordDialog::OnOK()
{
  ReadControls();
  CModalDialog::OnOK();
}
