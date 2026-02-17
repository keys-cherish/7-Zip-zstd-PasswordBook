// WebDAVSettingsDialog.cpp

#include "StdAfx.h"

#include "WebDAVSettingsDialog.h"

#include "WebDAVAutoBackup.h"

#include "../../../Windows/FileDir.h"

#include <shlobj.h>
#include <wincrypt.h>

#include <vector>

#pragma comment(lib, "crypt32.lib")

using namespace NWindows;
using namespace NWindows::NFile;

static UString NormalizeBasePath_KeepRoot(const UString &base)
{
  UString p = base;
  if (p.IsEmpty())
    p = L"/";
  if (p.Ptr()[0] != L'/')
    p.InsertAtFront(L'/');
  if (p.Back() != L'/')
    p += L"/";
  return p;
}

static bool EndsWith_Ascii_NoCase(const UString &s, const wchar_t *suffix)
{
  const unsigned len = s.Len();
  const unsigned suffixLen = (unsigned)wcslen(suffix);
  if (len < suffixLen)
    return false;

  const wchar_t *p = s.Ptr() + (len - suffixLen);
  for (unsigned i = 0; i < suffixLen; ++i)
  {
    wchar_t a = p[i];
    wchar_t b = suffix[i];
    if (a >= L'A' && a <= L'Z') a = (wchar_t)(a + 0x20);
    if (b >= L'A' && b <= L'Z') b = (wchar_t)(b + 0x20);
    if (a != b)
      return false;
  }

  return true;
}

static UString StripBackupFolderSuffix(const UString &normalizedBase)
{
  UString base = normalizedBase;
  const wchar_t *kTail = L"/7z_zs_pb/";
  const unsigned kTailLen = (unsigned)wcslen(kTail);

  if (EndsWith_Ascii_NoCase(base, kTail))
  {
    const unsigned len = base.Len();
    if (len <= kTailLen)
      return UString(L"/");

    base = base.Left(len - kTailLen + 1);
    if (base.IsEmpty())
      base = L"/";
    return NormalizeBasePath_KeepRoot(base);
  }

  return base;
}

static FString GetAppDataSubDirForIni(const wchar_t *name)
{
  WCHAR appDataPath[MAX_PATH] = {};
  if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath)))
    return FString();

  FString path = appDataPath;
  path += FCHAR_PATH_SEPARATOR;
  path += name;
  if (!NDir::CreateComplexDir(path))
    return FString();
  return path;
}

static UString GetWebDavIniPathForDialog()
{
  FString dir = GetAppDataSubDirForIni(L"7-Zip-zstd");
  if (dir.IsEmpty())
    return UString();
  dir += FCHAR_PATH_SEPARATOR;
  dir += FTEXT("webdav.ini");
  return dir;
}

static UString IniReadStrForDialog(const wchar_t *sec, const wchar_t *key, const wchar_t *def)
{
  UString ini = GetWebDavIniPathForDialog();
  wchar_t buf[4096] = {};
  GetPrivateProfileStringW(sec, key, def, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])), ini);
  return buf;
}

static int IniReadIntForDialog(const wchar_t *sec, const wchar_t *key, int def)
{
  UString ini = GetWebDavIniPathForDialog();
  return (int)GetPrivateProfileIntW(sec, key, def, ini);
}

static bool IniWriteStrForDialog(const wchar_t *sec, const wchar_t *key, const UString &val)
{
  UString ini = GetWebDavIniPathForDialog();
  return WritePrivateProfileStringW(sec, key, val, ini) != 0;
}

static UString EncryptDpapiBase64ForDialog(const UString &plain)
{
  if (plain.IsEmpty())
    return UString();

  DATA_BLOB inBlob;
  inBlob.pbData = (BYTE *)(const wchar_t *)plain;
  inBlob.cbData = (DWORD)((plain.Len() + 1) * sizeof(wchar_t));

  DATA_BLOB outBlob;
  if (!CryptProtectData(&inBlob, NULL, NULL, NULL, NULL, 0, &outBlob))
    return plain;

  DWORD base64Len = 0;
  CryptBinaryToStringW(outBlob.pbData, outBlob.cbData,
      CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
      NULL, &base64Len);

  UString out;
  wchar_t *buf = out.GetBuf(base64Len + 4);
  DWORD actual = base64Len;
  if (!CryptBinaryToStringW(outBlob.pbData, outBlob.cbData,
      CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
      buf, &actual))
  {
    LocalFree(outBlob.pbData);
    return plain;
  }

  out.ReleaseBuf_SetLen(actual);
  while (!out.IsEmpty() && out.Back() == 0)
    out.DeleteBack();

  LocalFree(outBlob.pbData);
  return out;
}

static UString DecryptDpapiBase64ForDialog(const UString &enc)
{
  if (enc.IsEmpty())
    return UString();

  DWORD binaryLen = 0;
  if (!CryptStringToBinaryW(enc, 0, CRYPT_STRING_BASE64, NULL, &binaryLen, NULL, NULL))
    return enc;

  std::vector<BYTE> bin(binaryLen);
  if (!CryptStringToBinaryW(enc, 0, CRYPT_STRING_BASE64,
      bin.empty() ? NULL : bin.data(), &binaryLen, NULL, NULL))
    return enc;

  DATA_BLOB inBlob;
  inBlob.pbData = bin.empty() ? (BYTE *)"" : bin.data();
  inBlob.cbData = binaryLen;

  DATA_BLOB outBlob;
  if (!CryptUnprotectData(&inBlob, NULL, NULL, NULL, NULL, 0, &outBlob))
    return enc;

  UString result((wchar_t *)(void *)outBlob.pbData);
  LocalFree(outBlob.pbData);
  return result;
}

bool CWebDAVSettingsDialog::OnInit()
{
  _providerCombo.Attach(GetItem(IDC_WEBDAV_PROVIDER));
  _urlEdit.Attach(GetItem(IDE_WEBDAV_URL));
  _userEdit.Attach(GetItem(IDE_WEBDAV_USERNAME));
  _passEdit.Attach(GetItem(IDE_WEBDAV_PASSWORD));
  _basePathEdit.Attach(GetItem(IDE_WEBDAV_BASEPATH));
  _delayEdit.Attach(GetItem(IDE_WEBDAV_DELAY));
  _timeoutEdit.Attach(GetItem(IDE_WEBDAV_TIMEOUT));
  _encryptPassEdit.Attach(GetItem(IDE_WEBDAV_ENCRYPT_PASSWORD));

  _providerCombo.AddString(L"\x81EA\x5B9A\x4E49");
  _providerCombo.AddString(L"\x575A\x679C\x4E91");
  _providerCombo.AddString(L"123\x7F51\x76D8");
  _providerCombo.SetCurSel(0);

  SetItemText(IDOK, L"\x786E\x5B9A");
  SetItemText(IDCANCEL, L"\x53D6\x6D88");

  LoadFromIni();
  NormalizeSize();
  return CModalDialog::OnInit();
}

void CWebDAVSettingsDialog::LoadFromIni()
{
  UString url = IniReadStrForDialog(L"Server", L"URL", L"");
  UString user = IniReadStrForDialog(L"Server", L"Username", L"");
  UString pass = DecryptDpapiBase64ForDialog(IniReadStrForDialog(L"Server", L"Password", L""));
  UString basePath = IniReadStrForDialog(L"Server", L"BasePath", L"/7z_ZS_PB/");

  const int delay = IniReadIntForDialog(L"AutoBackup", L"DelaySeconds", 30);
  const int timeout = IniReadIntForDialog(L"Server", L"Timeout", 30);
  const bool autoEnabled = IniReadIntForDialog(L"AutoBackup", L"Enabled", 0) != 0;

  UString encryptPwd = DecryptDpapiBase64ForDialog(IniReadStrForDialog(L"Backup", L"EncryptPassword", L""));

  _urlEdit.SetText(url);
  _userEdit.SetText(user);
  _passEdit.SetText(pass);
  _basePathEdit.SetText(basePath);
  CheckButton(IDX_WEBDAV_AUTO_ENABLE, autoEnabled);

  {
    UString s;
    if (delay < 0)
      s = L"0";
    else
      s.Add_UInt32((UInt32)delay);
    _delayEdit.SetText(s);
  }
  {
    UString s;
    if (timeout <= 0)
      s = L"30";
    else
      s.Add_UInt32((UInt32)timeout);
    _timeoutEdit.SetText(s);
  }

  _encryptPassEdit.SetText(encryptPwd);

  UString lowerUrl = url;
  lowerUrl.MakeLower_Ascii();
  if (lowerUrl.Find(L"jianguoyun") >= 0)
    _providerCombo.SetCurSel(1);
  else if (lowerUrl.Find(L"123pan") >= 0)
    _providerCombo.SetCurSel(2);
  else
    _providerCombo.SetCurSel(0);
}

void CWebDAVSettingsDialog::ApplyPreset(int index)
{
  if (index == 1)
  {
    _urlEdit.SetText(L"https://dav.jianguoyun.com");
    _basePathEdit.SetText(L"/dav/");
  }
  else if (index == 2)
  {
    _urlEdit.SetText(L"https://dav.123pan.com");
    _basePathEdit.SetText(L"/");
  }
}

bool CWebDAVSettingsDialog::OnCommand(unsigned code, unsigned itemID, LPARAM lParam)
{
  if (code == CBN_SELCHANGE && itemID == IDC_WEBDAV_PROVIDER)
  {
    const int index = _providerCombo.GetCurSel();
    if (index >= 0)
      ApplyPreset(index);
    return true;
  }

  return CModalDialog::OnCommand(code, itemID, lParam);
}

void CWebDAVSettingsDialog::OnOK()
{
  UString url;
  UString user;
  UString pass;
  UString basePath;
  UString delayText;
  UString timeoutText;
  UString encryptPwd;

  _urlEdit.GetText(url);
  _userEdit.GetText(user);
  _passEdit.GetText(pass);
  _basePathEdit.GetText(basePath);
  _delayEdit.GetText(delayText);
  _timeoutEdit.GetText(timeoutText);
  _encryptPassEdit.GetText(encryptPwd);

  url.Trim();
  user.Trim();
  pass.Trim();
  basePath.Trim();
  delayText.Trim();
  timeoutText.Trim();

  if (basePath.IsEmpty())
    basePath = L"/7z_ZS_PB/";

  basePath = NormalizeBasePath_KeepRoot(basePath);
  basePath = StripBackupFolderSuffix(basePath);

  int delay = _wtoi(delayText);
  if (delay < 0)
    delay = 0;
  int timeout = _wtoi(timeoutText);
  if (timeout <= 0)
    timeout = 30;

  UString delayOut;
  delayOut.Add_UInt32((UInt32)delay);

  UString timeoutOut;
  timeoutOut.Add_UInt32((UInt32)timeout);

  IniWriteStrForDialog(L"Server", L"URL", url);
  IniWriteStrForDialog(L"Server", L"Username", user);
  IniWriteStrForDialog(L"Server", L"Password", EncryptDpapiBase64ForDialog(pass));
  IniWriteStrForDialog(L"Server", L"BasePath", basePath);
  IniWriteStrForDialog(L"Server", L"Timeout", timeoutOut);

  IniWriteStrForDialog(L"Backup", L"EncryptPassword", EncryptDpapiBase64ForDialog(encryptPwd));
  IniWriteStrForDialog(L"Backup", L"KeepVersions", L"10");

  IniWriteStrForDialog(L"AutoBackup", L"Enabled", IsButtonCheckedBool(IDX_WEBDAV_AUTO_ENABLE) ? L"1" : L"0");
  IniWriteStrForDialog(L"AutoBackup", L"DelaySeconds", delayOut);

  NWebDAVBackup::NormalizeWebDavConfigBasePath();

  CModalDialog::OnOK();
}
