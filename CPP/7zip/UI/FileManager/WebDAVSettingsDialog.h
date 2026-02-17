// WebDAVSettingsDialog.h

#ifndef ZIP7_INC_WEBDAV_SETTINGS_DIALOG_H
#define ZIP7_INC_WEBDAV_SETTINGS_DIALOG_H

#include "../../../Windows/Control/ComboBox.h"
#include "../../../Windows/Control/Dialog.h"
#include "../../../Windows/Control/Edit.h"

#include "WebDAVSettingsDialogRes.h"

class CWebDAVSettingsDialog : public NWindows::NControl::CModalDialog
{
  NWindows::NControl::CComboBox _providerCombo;
  NWindows::NControl::CEdit _urlEdit;
  NWindows::NControl::CEdit _userEdit;
  NWindows::NControl::CEdit _passEdit;
  NWindows::NControl::CEdit _basePathEdit;
  NWindows::NControl::CEdit _delayEdit;
  NWindows::NControl::CEdit _timeoutEdit;
  NWindows::NControl::CEdit _encryptPassEdit;

  virtual bool OnInit() Z7_override;
  virtual bool OnCommand(unsigned code, unsigned itemID, LPARAM lParam) Z7_override;
  virtual void OnOK() Z7_override;

  void LoadFromIni();
  void ApplyPreset(int index);

public:
  INT_PTR Create(HWND parentWindow = NULL) { return CModalDialog::Create(IDD_WEBDAV_SETTINGS, parentWindow); }
};

#endif

