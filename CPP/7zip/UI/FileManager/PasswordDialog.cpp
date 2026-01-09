// PasswordDialog.cpp

#include "StdAfx.h"

#include "PasswordDialog.h"

#ifndef Z7_SFX
#include "PasswordBookDialog.h"
#include "PasswordBookDialogRes.h"
#include "PasswordManager.h"
#endif

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

#ifndef Z7_SFX
  // Set Marlett font for dropdown button to display arrow
  HWND hDropdown = GetItem(IDB_PASSWORD_DROPDOWN);
  if (hDropdown)
  {
    HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Marlett");
    if (hFont)
    {
      SendMessageW(hDropdown, WM_SETFONT, (WPARAM)hFont, TRUE);
      SetWindowTextW(hDropdown, L"u"); // Marlett 'u' = down arrow
    }
  }
#endif

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
#ifndef Z7_SFX
  if (buttonID == IDB_PASSWORD_BOOK)
  {
    OnPasswordBook();
    return true;
  }
  if (buttonID == IDB_PASSWORD_DROPDOWN)
  {
    OnPasswordDropdown();
    return true;
  }
#endif
  return CDialog::OnButtonClicked(buttonID, buttonHWND);
}

#ifndef Z7_SFX
void CPasswordDialog::OnPasswordDropdown()
{
  // Get password list
  g_PasswordManager.Load();
  const CObjectVector<CPasswordEntry> &entries = g_PasswordManager.GetEntries();

  if (entries.IsEmpty())
  {
    // If no passwords, open password book to add
    OnPasswordBook();
    return;
  }

  // Create popup menu
  HMENU hMenu = CreatePopupMenu();
  if (!hMenu)
    return;

  // Add passwords to menu (show display name or masked password)
  for (unsigned i = 0; i < entries.Size(); i++)
  {
    const CPasswordEntry &entry = entries[i];
    UString displayText;
    if (!entry.Name.IsEmpty())
      displayText = entry.Name;
    else
    {
      // Mask password for display
      displayText = L"******** (";
      if (entry.Password.Len() > 2)
      {
        displayText += entry.Password[0];
        displayText += L"...";
        displayText += entry.Password[entry.Password.Len() - 1];
      }
      else
        displayText += entry.Password;
      displayText += L")";
    }
    AppendMenuW(hMenu, MF_STRING, 1000 + i, displayText);
  }

  // Add separator and "Manage..." option
  AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
  UString menuText = LangString(IDS_PASSWORD_BOOK);
  if (menuText.IsEmpty())
    menuText = L"Password Book...";
  else
    menuText += L"...";
  AppendMenuW(hMenu, MF_STRING, 999, menuText);

  // Get button position
  HWND hButton = GetItem(IDB_PASSWORD_DROPDOWN);
  RECT rc;
  ::GetWindowRect(hButton, &rc);

  // Show popup menu
  UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                            rc.left, rc.bottom, 0, *this, NULL);

  DestroyMenu(hMenu);

  if (cmd == 999)
  {
    // Open password book
    OnPasswordBook();
  }
  else if (cmd >= 1000 && cmd < 1000 + entries.Size())
  {
    // Select password
    Password = entries[cmd - 1000].Password;
    SetTextSpec();
  }
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
#endif

void CPasswordDialog::OnOK()
{
  ReadControls();
  CModalDialog::OnOK();
}
