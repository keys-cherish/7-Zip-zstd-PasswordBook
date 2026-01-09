// PasswordBookDialog.h

#ifndef ZIP7_INC_PASSWORD_BOOK_DIALOG_H
#define ZIP7_INC_PASSWORD_BOOK_DIALOG_H

#include "../../../Windows/Control/Dialog.h"
#include "../../../Windows/Control/Edit.h"
#include "../../../Windows/Control/ListView.h"

#include "PasswordBookDialogRes.h"

// Password Entry Dialog (for Add/Edit)
class CPasswordEntryDialog : public NWindows::NControl::CModalDialog
{
    NWindows::NControl::CEdit _passwordEdit;
    NWindows::NControl::CEdit _nameEdit;

    virtual bool OnInit() Z7_override;
    virtual bool OnButtonClicked(unsigned buttonID, HWND buttonHWND) Z7_override;
    void OnOK();

public:
    UString Password;
    UString Name;
    bool IsEdit; // true = Edit mode, false = Add mode

    CPasswordEntryDialog() : IsEdit(false) {}

    INT_PTR Create(unsigned resID, HWND parentWindow = NULL) { return CModalDialog::Create(resID, parentWindow); }
};

// Main Password Book Dialog
class CPasswordBookDialog : public NWindows::NControl::CModalDialog
{
    NWindows::NControl::CListView _list;

    virtual bool OnInit() Z7_override;
    virtual bool OnNotify(UINT controlID, LPNMHDR header) Z7_override;
    virtual bool OnButtonClicked(unsigned buttonID, HWND buttonHWND) Z7_override;

    void RefreshList();
    int GetSelectedIndex();
    void OnSelect();
    void OnAdd();
    void OnDelete();
    void OnEdit();
    void OnMoveUp();
    void OnMoveDown();
    void OnImport();
    void OnExport();

public:
    UString SelectedPassword; // Password selected by user
    bool PasswordSelected;    // True if user clicked Select or double-clicked

    CPasswordBookDialog() : PasswordSelected(false) {}

    INT_PTR Create(HWND parentWindow = NULL) { return CModalDialog::Create(IDD_PASSWORD_BOOK, parentWindow); }
};

// Helper function to show password dropdown menu
void ShowPasswordDropdownMenu(HWND hwndParent, HWND hwndButton, NWindows::NControl::CEdit &passwordEdit);

#endif
