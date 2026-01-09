// PasswordBookDialog.cpp
// Password Book Management Dialog - Bandizip Style

#include "StdAfx.h"
#include "PasswordBookDialog.h"
#include "PasswordManager.h"

#include "../../../Windows/Control/Dialog.h"
#include "../../../Windows/Control/ListView.h"
#include "../../../Windows/Menu.h"
#include "../../../Windows/FileFind.h"
#include "../../../Windows/Clipboard.h"

#include <commdlg.h>

#ifdef Z7_LANG
#include "LangUtils.h"
#endif

// Helper function to get localized string with English fallback
static UString GetLangStringWithDefault(UInt32 langID, const wchar_t *defaultStr)
{
#ifdef Z7_LANG
    UString s = LangString(langID);
    if (!s.IsEmpty())
        return s;
#endif
    return defaultStr;
}

// ============================================================================
// CPasswordEntryDialog - Add/Edit single password dialog
// ============================================================================

bool CPasswordEntryDialog::OnInit()
{
#ifdef Z7_LANG
    LangSetDlgItems(*this, NULL, 0);
#endif

    _passwordEdit.Attach(GetItem(IDE_PASSWORD_ENTRY_PASSWORD));
    _nameEdit.Attach(GetItem(IDE_PASSWORD_ENTRY_NAME));

    // Set initial values
    _passwordEdit.SetText(Password);
    _nameEdit.SetText(Name);

    // Set window title based on mode
    if (IsEdit)
        SetText(GetLangStringWithDefault(IDS_PASSWORD_ENTRY_TITLE_EDIT, L"Edit Password"));
    else
        SetText(GetLangStringWithDefault(IDS_PASSWORD_ENTRY_TITLE_ADD, L"Add Password"));

    // Set labels
    SetItemText(IDT_PASSWORD_ENTRY_PASSWORD, GetLangStringWithDefault(IDS_PASSWORD_LABEL, L"Password:"));
    SetItemText(IDT_PASSWORD_ENTRY_NAME, GetLangStringWithDefault(IDS_PASSWORD_DISPLAYNAME_LABEL, L"Display Name (optional):"));

    NormalizePosition();
    return CModalDialog::OnInit();
}

bool CPasswordEntryDialog::OnButtonClicked(unsigned buttonID, HWND buttonHWND)
{
    if (buttonID == IDOK)
    {
        OnOK();
        return true;
    }
    return CModalDialog::OnButtonClicked(buttonID, buttonHWND);
}

void CPasswordEntryDialog::OnOK()
{
    _passwordEdit.GetText(Password);
    _nameEdit.GetText(Name);

    // Password is required
    if (Password.IsEmpty())
    {
        MessageBoxW(*this, GetLangStringWithDefault(IDS_PASSWORD_EMPTY_ERROR, L"Password cannot be empty!"), L"Error", MB_OK | MB_ICONWARNING);
        return;
    }

    // If name is empty, use password as display name (masked)
    if (Name.IsEmpty())
    {
        unsigned len = Password.Len();
        if (len <= 3)
            Name = L"***";
        else
        {
            Name = Password.Left(1);
            Name += L"***";
            Name += Password[len - 1];
        }
    }

    CModalDialog::OnOK();
}

// ============================================================================
// CPasswordBookDialog - Main password book dialog
// ============================================================================

bool CPasswordBookDialog::OnInit()
{
#ifdef Z7_LANG
    LangSetDlgItems(*this, NULL, 0);
#endif

    // Set window title
    SetText(GetLangStringWithDefault(IDS_PASSWORD_MANAGER, L"Password Manager"));

    // Set button texts
    SetItemText(IDB_PASSWORD_BOOK_ADD, GetLangStringWithDefault(IDS_PASSWORD_ADD, L"Add"));
    SetItemText(IDB_PASSWORD_BOOK_DELETE, GetLangStringWithDefault(IDS_PASSWORD_DELETE, L"Delete"));
    SetItemText(IDB_PASSWORD_BOOK_EDIT, GetLangStringWithDefault(IDS_PASSWORD_EDIT, L"Edit"));
    SetItemText(IDB_PASSWORD_BOOK_SELECT, GetLangStringWithDefault(IDS_PASSWORD_SELECT, L"Select"));
    SetItemText(IDB_PASSWORD_BOOK_IMPORT, GetLangStringWithDefault(IDS_PASSWORD_IMPORT, L"Import"));
    SetItemText(IDB_PASSWORD_BOOK_EXPORT, GetLangStringWithDefault(IDS_PASSWORD_EXPORT, L"Export"));
    SetItemText(IDCANCEL, GetLangStringWithDefault(IDS_PASSWORD_CLOSE, L"Close"));
    SetItemText(IDT_PASSWORD_BOOK_INFO, GetLangStringWithDefault(IDS_PASSWORD_INFO_DBLCLICK, L"Double-click to copy password to clipboard"));

    _list.Attach(GetItem(IDL_PASSWORD_BOOK_LIST));

    // Setup ListView columns
    _list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // Add columns: DisplayName first, then Password (masked)
    _list.InsertColumn(0, GetLangStringWithDefault(IDS_PASSWORD_COL_DISPLAYNAME, L"Display Name"), 150);
    _list.InsertColumn(1, GetLangStringWithDefault(IDS_PASSWORD_COL_PASSWORD, L"Password"), 200);

    // Load passwords from file
    g_PasswordManager.Load();

    // Populate list
    RefreshList();

    NormalizePosition();
    return CModalDialog::OnInit();
}

void CPasswordBookDialog::RefreshList()
{
    _list.DeleteAllItems();

    const CObjectVector<CPasswordEntry> &entries = g_PasswordManager.GetEntries();

    FOR_VECTOR(i, entries)
    {
        const CPasswordEntry &entry = entries[i];

        // Mask password for display
        UString maskedPwd;
        unsigned len = entry.Password.Len();
        for (unsigned j = 0; j < len && j < 20; j++)
            maskedPwd += L'*';
        if (len > 20)
            maskedPwd += L"...";

        // Column 0: Display Name, Column 1: Masked Password
        int index = _list.InsertItem(i, entry.Name);
        _list.SetSubItem(index, 1, maskedPwd);
    }

    // Select first item if exists
    if (_list.GetItemCount() > 0)
        _list.SetItemState(0, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
}

int CPasswordBookDialog::GetSelectedIndex()
{
    int count = _list.GetItemCount();
    for (int i = 0; i < count; i++)
    {
        if (_list.GetItemState(i, LVIS_SELECTED) == LVIS_SELECTED)
            return i;
    }
    return -1;
}

bool CPasswordBookDialog::OnButtonClicked(unsigned buttonID, HWND buttonHWND)
{
    switch (buttonID)
    {
    case IDB_PASSWORD_BOOK_ADD:
        OnAdd();
        return true;
    case IDB_PASSWORD_BOOK_DELETE:
        OnDelete();
        return true;
    case IDB_PASSWORD_BOOK_EDIT:
        OnEdit();
        return true;
    case IDB_PASSWORD_BOOK_MOVE_UP:
        OnMoveUp();
        return true;
    case IDB_PASSWORD_BOOK_MOVE_DOWN:
        OnMoveDown();
        return true;
    case IDB_PASSWORD_BOOK_SELECT:
        OnSelect();
        return true;
    case IDB_PASSWORD_BOOK_IMPORT:
        OnImport();
        return true;
    case IDB_PASSWORD_BOOK_EXPORT:
        OnExport();
        return true;
    case IDCANCEL:
        CModalDialog::OnCancel();
        return true;
    }
    return CModalDialog::OnButtonClicked(buttonID, buttonHWND);
}

bool CPasswordBookDialog::OnNotify(UINT controlID, LPNMHDR header)
{
    if (controlID == IDL_PASSWORD_BOOK_LIST)
    {
        if (header->code == NM_DBLCLK)
        {
            // Double-click to copy password to clipboard
            int index = GetSelectedIndex();
            if (index >= 0)
            {
                const CObjectVector<CPasswordEntry> &entries = g_PasswordManager.GetEntries();
                if (index < (int)entries.Size())
                {
                    NWindows::ClipboardSetText(*this, entries[index].Password);
                    MessageBoxW(*this, L"Password copied to clipboard!", L"Info", MB_OK | MB_ICONINFORMATION);
                }
            }
            return true;
        }
    }
    return CModalDialog::OnNotify(controlID, header);
}

void CPasswordBookDialog::OnAdd()
{
    CPasswordEntryDialog dlg;
    dlg.IsEdit = false;
    dlg.Password.Empty();
    dlg.Name.Empty();

    if (dlg.Create(IDD_PASSWORD_ENTRY, *this) == IDOK)
    {
        CPasswordEntry entry;
        entry.Name = dlg.Name;
        entry.Password = dlg.Password;

        g_PasswordManager.Add(entry);
        g_PasswordManager.Save();
        RefreshList();

        // Select newly added item
        int count = _list.GetItemCount();
        if (count > 0)
            _list.SetItemState(count - 1, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
    }
}

void CPasswordBookDialog::OnDelete()
{
    int index = GetSelectedIndex();
    if (index < 0)
    {
        MessageBoxW(*this, GetLangStringWithDefault(IDS_PASSWORD_SELECT_DELETE, L"Please select a password to delete."), L"Info", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (MessageBoxW(*this, GetLangStringWithDefault(IDS_PASSWORD_CONFIRM_DELETE, L"Are you sure you want to delete this password?"),
                    GetLangStringWithDefault(IDS_PASSWORD_DELETE, L"Delete"), MB_YESNO | MB_ICONQUESTION) == IDYES)
    {
        g_PasswordManager.Remove(index);
        g_PasswordManager.Save();
        RefreshList();
    }
}

void CPasswordBookDialog::OnEdit()
{
    int index = GetSelectedIndex();
    if (index < 0)
    {
        MessageBoxW(*this, GetLangStringWithDefault(IDS_PASSWORD_SELECT_EDIT, L"Please select a password to edit."), L"Info", MB_OK | MB_ICONINFORMATION);
        return;
    }

    const CObjectVector<CPasswordEntry> &entries = g_PasswordManager.GetEntries();
    if (index >= (int)entries.Size())
        return;

    const CPasswordEntry &entry = entries[index];

    CPasswordEntryDialog dlg;
    dlg.IsEdit = true;
    dlg.Password = entry.Password;
    dlg.Name = entry.Name;

    if (dlg.Create(IDD_PASSWORD_ENTRY, *this) == IDOK)
    {
        CPasswordEntry newEntry;
        newEntry.Name = dlg.Name;
        newEntry.Password = dlg.Password;

        g_PasswordManager.Update(index, newEntry);
        g_PasswordManager.Save();
        RefreshList();

        // Re-select the edited item
        if (index >= 0 && index < _list.GetItemCount())
            _list.SetItemState(index, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
    }
}

void CPasswordBookDialog::OnMoveUp()
{
    int index = GetSelectedIndex();
    if (index <= 0)
        return;

    g_PasswordManager.MoveUp(index);
    g_PasswordManager.Save();
    RefreshList();
    int newIndex = index - 1;
    if (newIndex >= 0 && newIndex < _list.GetItemCount())
        _list.SetItemState(newIndex, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
}

void CPasswordBookDialog::OnMoveDown()
{
    int index = GetSelectedIndex();
    if (index < 0 || index >= _list.GetItemCount() - 1)
        return;

    g_PasswordManager.MoveDown(index);
    g_PasswordManager.Save();
    RefreshList();
    int newIndex = index + 1;
    if (newIndex >= 0 && newIndex < _list.GetItemCount())
        _list.SetItemState(newIndex, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
}

void CPasswordBookDialog::OnSelect()
{
    int index = GetSelectedIndex();
    if (index < 0)
    {
        MessageBoxW(*this, GetLangStringWithDefault(IDS_PASSWORD_SELECT_ITEM, L"Please select a password."), L"Info", MB_OK | MB_ICONINFORMATION);
        return;
    }

    const CObjectVector<CPasswordEntry> &entries = g_PasswordManager.GetEntries();
    if (index >= (int)entries.Size())
        return;

    SelectedPassword = entries[index].Password;
    PasswordSelected = true;
    CModalDialog::OnOK();
}

// ============================================================================
// ShowPasswordDropdownMenu - Show popup menu with saved passwords
// ============================================================================

void ShowPasswordDropdownMenu(HWND hwndParent, HWND hwndButton, NWindows::NControl::CEdit &passwordEdit)
{
    // Load passwords
    g_PasswordManager.Load();

    const CObjectVector<CPasswordEntry> &entries = g_PasswordManager.GetEntries();
    if (entries.IsEmpty())
        return;

    // Create popup menu
    NWindows::CMenu menu;
    menu.CreatePopup();

    // Add password entries to menu (show display name)
    FOR_VECTOR(i, entries)
    {
        const CPasswordEntry &entry = entries[i];
        menu.AppendItem(MF_STRING, (UINT)(1000 + i), entry.Name);
    }

    // Get button position for menu placement
    RECT rect;
    ::GetWindowRect(hwndButton, &rect);

    // Show popup menu
    UINT cmd = (UINT)menu.Track(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                                rect.left, rect.bottom, hwndParent);

    if (cmd >= 1000 && cmd < 1000 + entries.Size())
    {
        unsigned index = cmd - 1000;
        passwordEdit.SetText(entries[index].Password);
    }
}

void CPasswordBookDialog::OnImport()
{
    OPENFILENAMEW ofn;
    wchar_t szFile[MAX_PATH] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = *this;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn))
    {
        UString filePath = szFile;
        int count = g_PasswordManager.Import(filePath);

        if (count > 0)
        {
            g_PasswordManager.Save();
            RefreshList();

            UString msg = GetLangStringWithDefault(IDS_PASSWORD_IMPORT_SUCCESS, L"Successfully imported passwords.");
            wchar_t numBuf[32];
            _itow_s(count, numBuf, 10);
            msg += L" (";
            msg += numBuf;
            msg += L")";
            MessageBoxW(*this, msg, L"Import", MB_OK | MB_ICONINFORMATION);
        }
        else if (count == 0)
        {
            MessageBoxW(*this, L"No new passwords to import.", L"Import", MB_OK | MB_ICONINFORMATION);
        }
        else
        {
            MessageBoxW(*this, GetLangStringWithDefault(IDS_PASSWORD_IMPORT_FAILED, L"Failed to import passwords. Check file format."), L"Import", MB_OK | MB_ICONWARNING);
        }
    }
}

void CPasswordBookDialog::OnExport()
{
    const CObjectVector<CPasswordEntry> &entries = g_PasswordManager.GetEntries();
    if (entries.IsEmpty())
    {
        MessageBoxW(*this, L"No passwords to export.", L"Export", MB_OK | MB_ICONINFORMATION);
        return;
    }

    OPENFILENAMEW ofn;
    wchar_t szFile[MAX_PATH] = L"passwords.txt";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = *this;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"txt";

    if (GetSaveFileNameW(&ofn))
    {
        UString filePath = szFile;
        bool success = g_PasswordManager.Export(filePath);

        if (success)
        {
            UString msg = GetLangStringWithDefault(IDS_PASSWORD_EXPORT_SUCCESS, L"Successfully exported passwords.");
            wchar_t numBuf[32];
            _itow_s((int)entries.Size(), numBuf, 10);
            msg += L" (";
            msg += numBuf;
            msg += L")";
            MessageBoxW(*this, msg, L"Export", MB_OK | MB_ICONINFORMATION);
        }
        else
        {
            MessageBoxW(*this, GetLangStringWithDefault(IDS_PASSWORD_EXPORT_FAILED, L"Failed to export passwords."), L"Export", MB_OK | MB_ICONERROR);
        }
    }
}
