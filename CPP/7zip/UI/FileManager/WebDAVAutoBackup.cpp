// WebDAVAutoBackup.cpp

#include "StdAfx.h"

#include "WebDAVAutoBackup.h"

#include "../../../Common/UTFConvert.h"
#include "../../../Windows/DLL.h"
#include "../../../Windows/FileDir.h"
#include "../../../Windows/FileFind.h"
#include "../../../Windows/FileIO.h"
#include "../../../Windows/Registry.h"
#include "../../../Windows/System.h"

#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <objbase.h>
#include <oleauto.h>

#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

using namespace NWindows;
using namespace NWindows::NFile;

namespace NWebDAVBackup
{

// -------------------------
// util
// -------------------------

static FString GetAppDataSubDir(const wchar_t *name);

static bool WriteBufferToFile(const FString &path, const void *data, UInt32 size)
{
  NIO::COutFile file;
  if (!file.Create_ALWAYS(path))
    return false;

  UInt32 processed = 0;
  if (!file.Write(data, size, processed))
    return false;
  return processed == size;
}

static bool CopyFileToPath(const FString &srcPath, const FString &dstPath)
{
  NIO::CInFile inFile;
  if (!inFile.Open(srcPath))
    return false;

  NIO::COutFile outFile;
  if (!outFile.Create_ALWAYS(dstPath))
    return false;

  BYTE buf[1 << 15];
  for (;;)
  {
    UInt32 processedIn = 0;
    if (!inFile.Read(buf, sizeof(buf), processedIn))
      return false;
    if (processedIn == 0)
      break;

    UInt32 processedOut = 0;
    if (!outFile.Write(buf, processedIn, processedOut) || processedOut != processedIn)
      return false;
  }

  return true;
}

static void SecureWipeVector(std::vector<BYTE> &buf)
{
  if (!buf.empty())
  {
    SecureZeroMemory(buf.data(), buf.size());
    buf.clear();
  }
}

class CScopedSensitiveBuffer
{
  std::vector<BYTE> &_buf;

public:
  CScopedSensitiveBuffer(std::vector<BYTE> &buf): _buf(buf) {}
  ~CScopedSensitiveBuffer()
  {
    SecureWipeVector(_buf);
  }
};

class CScopedDirCleanup
{
  FString _path;

public:
  CScopedDirCleanup(const FString &path): _path(path) {}
  ~CScopedDirCleanup()
  {
    if (!_path.IsEmpty())
      NDir::RemoveDirWithSubItems(_path);
  }
};

static bool CreateUniqueWorkDir(CFSTR prefix, FString &path)
{
  path = GetAppDataSubDir(FTEXT("7-Zip-ZS-PB"));
  if (path.IsEmpty())
    return false;

  path += FCHAR_PATH_SEPARATOR;
  path += prefix;
  path += FTEXT("_");

  FString idPart;
  idPart.Add_UInt32((UInt32)GetCurrentProcessId());
  idPart += FTEXT("_");
  idPart.Add_UInt32((UInt32)GetCurrentThreadId());
  idPart += FTEXT("_");
  idPart.Add_UInt32((UInt32)GetTickCount());
  path += idPart;

  return NDir::CreateComplexDir(path);
}

static bool ReadFileToBuffer(const FString &path, std::vector<BYTE> &buf)
{
  buf.clear();

  NIO::CInFile file;
  if (!file.Open(path))
    return false;

  UInt64 len = 0;
  if (!file.GetLength(len))
    return false;

  if (len > (1ull << 30))
    return false;

  if ((size_t)len == 0)
    return true;

  buf.resize((size_t)len);

  UInt32 processed = 0;
  if (!file.Read(buf.data(), (UInt32)len, processed))
    return false;
  return processed == (UInt32)len;
}

static bool GetFileSha256Hex(const FString &path, UString &hex)
{
  hex.Empty();

  std::vector<BYTE> data;
  if (!ReadFileToBuffer(path, data))
    return false;

  HCRYPTPROV hProv = 0;
  HCRYPTHASH hHash = 0;

  if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    return false;

  bool ok = false;
  if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
  {
    if (CryptHashData(hHash, data.empty() ? (BYTE *)"" : data.data(), (DWORD)data.size(), 0))
    {
      BYTE digest[32] = { 0 };
      DWORD digestLen = sizeof(digest);
      if (CryptGetHashParam(hHash, HP_HASHVAL, digest, &digestLen, 0) && digestLen == 32)
      {
        static const wchar_t *kHex = L"0123456789abcdef";
        for (DWORD i = 0; i < digestLen; ++i)
        {
          const unsigned b = digest[i];
          hex += kHex[(b >> 4) & 0xF];
          hex += kHex[b & 0xF];
        }
        ok = true;
      }
    }
  }

  if (hHash)
    CryptDestroyHash(hHash);
  if (hProv)
    CryptReleaseContext(hProv, 0);
  return ok;
}

static FString GetAppDataSubDir(const wchar_t *name)
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

static FString GetSettingsRegExportPath()
{
  FString dir = GetAppDataSubDir(L"7-Zip-ZS-PB");
  if (dir.IsEmpty())
    return FString();
  dir += FCHAR_PATH_SEPARATOR;
  dir += FTEXT("settings_reg.txt");
  return dir;
}

static bool SaveLocalSettingsSnapshot(const FString &outPath)
{
  NRegistry::CKey key;
  if (key.Open(HKEY_CURRENT_USER, TEXT("Software\\7-Zip-Zstandard\\FM"), KEY_READ) != ERROR_SUCCESS)
  {
    static const char kEmpty[] = "# No FM settings found\r\n";
    return WriteBufferToFile(outPath, kEmpty, (UInt32)strlen(kEmpty));
  }

  FString text;
  text += FTEXT("# 7-Zip-Zstandard FM settings snapshot\r\n");

  struct KeyName
  {
    const TCHAR *name;
  };

  static const KeyName keys[] = {
    { TEXT("ShowDots") },
    { TEXT("ShowRealFileIcons") },
    { TEXT("FullRow") },
    { TEXT("ShowGrid") },
    { TEXT("SingleClick") },
    { TEXT("AlternativeSelection") },
    { TEXT("WantArcHistory") },
    { TEXT("WantPathHistory") },
    { TEXT("WantCopyHistory") },
    { TEXT("WantFolderHistory") },
    { TEXT("LowercaseHashes") },
    { TEXT("ShowSystemMenu") }
  };

  for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
  {
    bool v = false;
    if (key.GetValue_bool_IfOk(keys[i].name, v) == ERROR_SUCCESS)
    {
      text += keys[i].name;
      text += L"=";
      text += (v ? L"1" : L"0");
      text += L"\r\n";
    }
  }

  AString utf8;
  ConvertUnicodeToUTF8(text, utf8);
  return WriteBufferToFile(outPath, (const char *)utf8, utf8.Len());
}

static UString NormalizeBasePath(const UString &base)
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
    return NormalizeBasePath(base);
  }

  return base;
}

static UString JoinPath(const UString &a, const UString &b)
{
  UString x = a;
  UString y = b;
  while (!x.IsEmpty() && x.Back() == L'/')
    x.DeleteBack();
  while (!y.IsEmpty() && y.Ptr()[0] == L'/')
    y.DeleteFrontal(1);
  return x + L"/" + y;
}

static UString GetDateYYYYMMDD()
{
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t s[32] = {};
  wsprintfW(s, L"%04u%02u%02u", (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay);
  return s;
}

static UString GetTimeHHMMSS()
{
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t s[32] = {};
  wsprintfW(s, L"%02u%02u%02u", (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
  return s;
}

static bool DecryptAes256(const std::vector<BYTE> &enc, const UString &password, std::vector<BYTE> &out)
{
  out.clear();

  HCRYPTPROV hProv = 0;
  HCRYPTHASH hHash = 0;
  HCRYPTKEY hKey = 0;

  if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    return false;

  bool ok = false;
  do
  {
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
      break;

    const BYTE *pwdData = (const BYTE *)(const wchar_t *)password;
    const DWORD pwdLen = (DWORD)(password.Len() * sizeof(wchar_t));

    if (!CryptHashData(hHash, pwdData, pwdLen, 0))
      break;

    if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey))
      break;

    DWORD mode = CRYPT_MODE_CBC;
    CryptSetKeyParam(hKey, KP_MODE, (BYTE *)&mode, 0);

    BYTE iv[16] = { 0 };
    for (int i = 0; i < 16; ++i)
      iv[i] = (BYTE)(i * 13 + 7);
    CryptSetKeyParam(hKey, KP_IV, iv, 0);

    out = enc;
    DWORD dataLen = (DWORD)out.size();
    if (dataLen == 0)
    {
      ok = true;
      break;
    }

    if (!CryptDecrypt(hKey, 0, TRUE, 0, out.data(), &dataLen))
      break;

    out.resize(dataLen);
    ok = true;
  } while (false);

  if (hKey)
    CryptDestroyKey(hKey);
  if (hHash)
    CryptDestroyHash(hHash);
  if (hProv)
    CryptReleaseContext(hProv, 0);

  return ok;
}

// -------------------------
// config
// -------------------------

struct SConfig
{
  UString url;
  UString user;
  UString pass;
  UString basePath;
  UString encryptPassword;
  bool autoEnabled;
  int timeoutSeconds;

  SConfig(): autoEnabled(false), timeoutSeconds(30) {}
};

static bool LoadConfig(SConfig &cfg);

static void SetError(UString *errorMessage, const wchar_t *text)
{
  if (!errorMessage)
    return;

  errorMessage->Empty();
  if (text)
    *errorMessage = text;
}

static const wchar_t *const kErrConfigNotFound = L"未找到 WebDAV 配置。";
static const wchar_t *const kErrAutoDisabled = L"WebDAV 自动备份未启用。";
static const wchar_t *const kErrConfigIncomplete = L"WebDAV 配置不完整（URL/用户名/密码）。";
static const wchar_t *const kErrPasswordBookMissing = L"未找到密码本文件 password_book.dat。";
static const wchar_t *const kErrSettingsPath = L"无法创建设置快照路径。";
static const wchar_t *const kErrSettingsExport = L"无法导出设置快照。";
static const wchar_t *const kErrHashSettings = L"无法计算设置快照哈希。";
static const wchar_t *const kErrHashPassword = L"无法计算密码本哈希。";
static const wchar_t *const kErrConnect = L"无法连接 WebDAV 服务器。";
static const wchar_t *const kErrEncryptPassword = L"备份加密密码为空，请先在 WebDAV 设置中填写。";
static const wchar_t *const kErrTempDir = L"无法创建本地临时目录。";
static const wchar_t *const kErrZipPack = L"无法生成备份压缩包。";
static const wchar_t *const kErrReadZip = L"无法读取备份压缩包。";
static const wchar_t *const kErrEncryptFile = L"无法加密备份文件。";
static const wchar_t *const kErrUploadPack = L"无法上传备份包到 WebDAV。";
static const wchar_t *const kErrUploadManifest = L"无法上传 manifest 文件。";
static const wchar_t *const kErrManualUnexpected = L"手动备份失败（发生未预期错误）。";
static const wchar_t *const kErrRestoreRemoteIndex = L"未找到远程增量索引 password_latest.txt。";
static const wchar_t *const kErrRestoreRemotePath = L"远程增量索引内容无效。";
static const wchar_t *const kErrRestoreDownload = L"无法下载远程备份文件。";
static const wchar_t *const kErrRestoreRead = L"无法读取备份文件。";
static const wchar_t *const kErrRestoreDecrypt = L"无法解密备份文件，请确认加密密码正确。";
static const wchar_t *const kErrRestoreExtract = L"无法解包备份压缩包。";
static const wchar_t *const kErrRestoreNoPasswordFile = L"备份包中未找到 password_book.dat。";
static const wchar_t *const kErrRestoreWriteLocal = L"无法写入本地密码本文件。";
static const wchar_t *const kErrRestoreUnexpected = L"导入备份失败（发生未预期错误）。";
static const wchar_t *const kErrManualUnexpectedCode = L"手动备份失败（异常代码）。";
static const wchar_t *const kErrRestoreUnexpectedCode = L"导入备份失败（异常代码）。";

static CRITICAL_SECTION g_AutoBackupLock;
static volatile LONG g_AutoBackupLockState = 0;
static volatile LONG g_AutoBackupPending = 0;
static volatile LONG g_AutoBackupRunning = 0;
static FString g_AutoBackupPath;

static bool RunBackupCore(const FString &passwordBookPath, bool requireAutoEnabled, UString *errorMessage);
static bool GetDefaultPasswordBookPath(FString &path);
void QueueAutoBackup(const FString &passwordBookPath);

static void SetErrorWithCode(UString *errorMessage, const wchar_t *prefix, DWORD code)
{
  if (!errorMessage)
    return;
  errorMessage->Empty();
  if (prefix)
    *errorMessage = prefix;
  *errorMessage += L"\ncode=";
  errorMessage->Add_UInt32(code);
}

static UString GetWebDavIniPath()
{
  FString dir = GetAppDataSubDir(L"7-Zip-zstd");
  if (dir.IsEmpty())
    return UString();
  dir += FCHAR_PATH_SEPARATOR;
  dir += FTEXT("webdav.ini");
  return dir;
}

static UString IniReadStr(const wchar_t *sec, const wchar_t *key, const wchar_t *def)
{
  UString ini = GetWebDavIniPath();
  wchar_t buf[4096] = {};
  GetPrivateProfileStringW(sec, key, def, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])), ini);
  return buf;
}

static int IniReadInt(const wchar_t *sec, const wchar_t *key, int def)
{
  UString ini = GetWebDavIniPath();
  return (int)GetPrivateProfileIntW(sec, key, def, ini);
}

static bool IniWriteStr(const wchar_t *sec, const wchar_t *key, const UString &val)
{
  UString ini = GetWebDavIniPath();
  return WritePrivateProfileStringW(sec, key, val, ini) != 0;
}

static UString FixCommonProviderPath(const UString &url, const UString &path)
{
  UString lowerUrl = url;
  lowerUrl.MakeLower_Ascii();

  UString normalized = NormalizeBasePath(path);
  normalized = StripBackupFolderSuffix(normalized);

  if (lowerUrl.Find(L"jianguoyun") >= 0)
  {
    if (!normalized.IsPrefixedBy(L"/dav/"))
      normalized = JoinPath(L"/dav", normalized);
  }

  if (lowerUrl.Find(L"123pan") >= 0 || lowerUrl.Find(L"123pan.com") >= 0)
  {
    if (normalized.IsPrefixedBy(L"/dav/"))
      normalized = UString(L"/") + normalized.Ptr(5);
  }

  return NormalizeBasePath(normalized);
}

bool NormalizeWebDavConfigBasePath()
{
  SConfig cfg;
  if (!LoadConfig(cfg))
    return false;

  UString fixed = FixCommonProviderPath(cfg.url, cfg.basePath);
  if (fixed == cfg.basePath)
    return true;

  return IniWriteStr(L"Server", L"BasePath", fixed);
}

static UString DecryptDpapiBase64(const UString &enc)
{
  if (enc.IsEmpty())
    return UString();

  DWORD binaryLen = 0;
  if (!CryptStringToBinaryW(enc, 0, CRYPT_STRING_BASE64, NULL, &binaryLen, NULL, NULL))
    return enc;

  std::vector<BYTE> bin(binaryLen);
  if (!CryptStringToBinaryW(enc, 0, CRYPT_STRING_BASE64, bin.data(), &binaryLen, NULL, NULL))
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

static bool LoadConfig(SConfig &cfg)
{
  cfg = SConfig();

  cfg.url = IniReadStr(L"Server", L"URL", L"");
  cfg.user = IniReadStr(L"Server", L"Username", L"");
  cfg.pass = DecryptDpapiBase64(IniReadStr(L"Server", L"Password", L""));
  cfg.basePath = IniReadStr(L"Server", L"BasePath", L"/7z_ZS_PB/");
  cfg.timeoutSeconds = IniReadInt(L"Server", L"Timeout", 30);
  cfg.basePath = FixCommonProviderPath(cfg.url, cfg.basePath);

  cfg.encryptPassword = DecryptDpapiBase64(IniReadStr(L"Backup", L"EncryptPassword", L""));
  cfg.autoEnabled = IniReadInt(L"AutoBackup", L"Enabled", 0) != 0;

  return !cfg.url.IsEmpty();
}

static bool EnsureEncryptPassword(const SConfig &cfg, UString *errorMessage)
{
  if (!cfg.encryptPassword.IsEmpty())
    return true;

  SetError(errorMessage, kErrEncryptPassword);
  return false;
}

static bool EnsureAutoBackupLock()
{
  if (InterlockedCompareExchange(&g_AutoBackupLockState, 1, 0) == 0)
  {
    InitializeCriticalSection(&g_AutoBackupLock);
    InterlockedExchange(&g_AutoBackupLockState, 2);
    return true;
  }

  while (InterlockedCompareExchange(&g_AutoBackupLockState, 2, 2) != 2)
    Sleep(1);
  return true;
}

static DWORD WINAPI AutoBackupWorkerProc(LPVOID)
{
  for (;;)
  {
    FString path;
    {
      EnsureAutoBackupLock();
      EnterCriticalSection(&g_AutoBackupLock);
      path = g_AutoBackupPath;
      InterlockedExchange(&g_AutoBackupPending, 0);
      LeaveCriticalSection(&g_AutoBackupLock);
    }

    if (!path.IsEmpty())
    {
      try
      {
        RunBackupCore(path, true, NULL);
      }
      catch (...)
      {
      }
    }

    if (InterlockedCompareExchange(&g_AutoBackupPending, 0, 0) == 0)
      break;
  }

  InterlockedExchange(&g_AutoBackupRunning, 0);
  if (InterlockedCompareExchange(&g_AutoBackupPending, 0, 0) != 0)
  {
    QueueAutoBackup(FString());
  }
  return 0;
}

static UString BuildServerPath(const UString &urlPath, const UString &basePath, const UString &relative)
{
  UString url = urlPath;
  if (url.IsEmpty())
    url = L"/";

  UString base = basePath;
  if (base.IsEmpty())
    base = L"/";

  UString rel = relative;
  while (!rel.IsEmpty() && rel.Ptr()[0] == L'/')
    rel.DeleteFrontal(1);

  if (rel.IsEmpty())
    return url;

  if (url.Back() != L'/')
    url += L"/";
  while (!base.IsEmpty() && base.Ptr()[0] == L'/')
    base.DeleteFrontal(1);
  url += base;
  if (url.Back() != L'/')
    url += L"/";
  url += rel;
  return url;
}

// -------------------------
// webdav client (minimal)
// -------------------------

class CClient
{
  HINTERNET _session;
  HINTERNET _conn;
  UString _host;
  INTERNET_PORT _port;
  bool _ssl;
  UString _urlPath;
  UString _basePath;
  UString _user;
  UString _pass;

public:
  CClient(): _session(NULL), _conn(NULL), _port(INTERNET_DEFAULT_HTTPS_PORT), _ssl(true) {}
  ~CClient()
  {
    if (_conn)
      WinHttpCloseHandle(_conn);
    if (_session)
      WinHttpCloseHandle(_session);
  }

  bool Connect(const SConfig &cfg)
  {
    _user = cfg.user;
    _pass = cfg.pass;
    _basePath = cfg.basePath;

    if (!ParseUrl(cfg.url))
      return false;

    _session = WinHttpOpen(L"7z-zstd-PB-backup/1.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS,
                           0);
    if (!_session)
      return false;

    int t = cfg.timeoutSeconds * 1000;
    WinHttpSetTimeouts(_session, t, t, t, t);

    _conn = WinHttpConnect(_session, _host, _port, 0);
    return _conn != NULL;
  }

  bool MkcolRecursive(const UString &relativePath)
  {
    UString path = relativePath;
    if (path.IsEmpty() || path.Ptr()[0] != L'/')
      return false;

    UString cur;
    unsigned i = 0;
    const wchar_t *p = path;
    const unsigned pathLen = path.Len();

    while (i < pathLen)
    {
      while (i < pathLen && p[i] == L'/')
      {
        cur += L'/';
        ++i;
      }

      unsigned j = i;
      while (j < pathLen && p[j] != L'/')
        ++j;

      if (j > i)
      {
        cur += path.Mid(i, j - i);
        UString reqPath = BuildServerPath(_urlPath, _basePath, cur.Ptr(1));
        DWORD sc = 0;
        if (!Request(L"MKCOL", reqPath, NULL, 0, NULL, &sc))
        {
          if (sc != 405 && sc != 301 && sc != 302)
            return false;
        }
      }
      i = j;
    }
    return true;
  }

  bool Put(const UString &fullPath, const void *data, size_t size)
  {
    UString rel = fullPath;
    if (!rel.IsEmpty() && rel.Ptr()[0] == L'/')
      rel.DeleteFrontal(1);
    UString reqPath = BuildServerPath(_urlPath, _basePath, rel);
    DWORD sc = 0;
    if (!Request(L"PUT", reqPath, data, size, NULL, &sc))
      return false;
    return sc == 200 || sc == 201 || sc == 204;
  }

  bool Get(const UString &fullPath, std::vector<BYTE> &out)
  {
    std::vector<BYTE> temp;
    UString rel = fullPath;
    if (!rel.IsEmpty() && rel.Ptr()[0] == L'/')
      rel.DeleteFrontal(1);
    UString reqPath = BuildServerPath(_urlPath, _basePath, rel);
    DWORD sc = 0;
    if (!Request(L"GET", reqPath, NULL, 0, &temp, &sc))
      return false;
    if (sc != 200)
      return false;
    out.swap(temp);
    return true;
  }

  bool Exists(const UString &fullPath)
  {
    UString rel = fullPath;
    if (!rel.IsEmpty() && rel.Ptr()[0] == L'/')
      rel.DeleteFrontal(1);
    UString reqPath = BuildServerPath(_urlPath, _basePath, rel);
    DWORD sc = 0;
    Request(L"HEAD", reqPath, NULL, 0, NULL, &sc);
    return sc == 200;
  }

  bool Delete(const UString &fullPath)
  {
    UString rel = fullPath;
    if (!rel.IsEmpty() && rel.Ptr()[0] == L'/')
      rel.DeleteFrontal(1);
    UString reqPath = BuildServerPath(_urlPath, _basePath, rel);
    DWORD sc = 0;
    if (!Request(L"DELETE", reqPath, NULL, 0, NULL, &sc))
      return false;
    return sc == 200 || sc == 204 || sc == 404;
  }

private:
  bool ParseUrl(const UString &url)
  {
    UString u = url;
    _ssl = true;
    _port = INTERNET_DEFAULT_HTTPS_PORT;

    if (u.Find(L"https://") == 0)
    {
      u.Delete(0, 8);
      _ssl = true;
      _port = INTERNET_DEFAULT_HTTPS_PORT;
    }
    else if (u.Find(L"http://") == 0)
    {
      u.Delete(0, 7);
      _ssl = false;
      _port = INTERNET_DEFAULT_HTTP_PORT;
    }

    int pathPos = u.Find(L'/');
    if (pathPos >= 0)
    {
      _host = u.Left(pathPos);
      _urlPath = u.Ptr(pathPos);
    }
    else
    {
      _host = u;
      _urlPath = L"/";
    }

    int portPos = _host.Find(L':');
    if (portPos >= 0)
    {
      UString portStr = _host.Ptr(portPos + 1);
      _host = _host.Left(portPos);
      _port = (INTERNET_PORT)_wtoi(portStr);
    }

    return !_host.IsEmpty();
  }

  bool Request(const wchar_t *method,
               const UString &path,
               const void *data,
               size_t size,
               std::vector<BYTE> *response,
               DWORD *statusCode)
  {
    if (statusCode)
      *statusCode = 0;

    const DWORD flags = _ssl ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET req = WinHttpOpenRequest(_conn,
                                       method,
                                       path,
                                       NULL,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       flags);
    if (!req)
      return false;

    bool ok = false;
    do
    {
      if (!WinHttpSetCredentials(req,
                                 WINHTTP_AUTH_TARGET_SERVER,
                                 WINHTTP_AUTH_SCHEME_BASIC,
                                 _user,
                                 _pass,
                                 NULL))
      {
        // continue anyway; server may not need auth for this request.
      }

      if (_ssl)
      {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
      }

      DWORD totalLen = (DWORD)size;
      if (!WinHttpSendRequest(req,
                              WINHTTP_NO_ADDITIONAL_HEADERS,
                              0,
                              (LPVOID)data,
                              (DWORD)size,
                              totalLen,
                              0))
        break;

      if (!WinHttpReceiveResponse(req, NULL))
        break;

      DWORD sc = 0;
      DWORD scSize = sizeof(sc);
      WinHttpQueryHeaders(req,
                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX,
                          &sc,
                          &scSize,
                          WINHTTP_NO_HEADER_INDEX);
      if (statusCode)
        *statusCode = sc;

      if (response)
      {
        response->clear();
        for (;;)
        {
          DWORD avail = 0;
          if (!WinHttpQueryDataAvailable(req, &avail))
            break;
          if (avail == 0)
            break;

          size_t old = response->size();
          response->resize(old + avail);

          DWORD got = 0;
          if (!WinHttpReadData(req, response->data() + old, avail, &got))
            break;

          if (got < avail)
            response->resize(old + got);

          if (got == 0)
            break;
        }
      }

      ok = true;
    } while (false);

    WinHttpCloseHandle(req);
    return ok;
  }
};

// -------------------------
// pack + encrypt
// -------------------------

static bool CreateZipWithShell(const FString &zipPath, const std::vector<FString> &inputFiles)
{
  if (inputFiles.empty())
    return false;

  if (NDir::DeleteFileAlways(zipPath))
  {
    // deleted
  }

  HRESULT hr = CoInitialize(NULL);
  const bool needUninit = SUCCEEDED(hr);

  bool ok = false;
  do
  {
    IDispatch *shell = NULL;
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(L"Shell.Application", &clsid)))
      break;

    if (FAILED(CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_IDispatch, (void **)&shell)))
      break;

    DISPID dispNameSpace = 0;
    {
      OLECHAR *name = L"NameSpace";
      if (FAILED(shell->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispNameSpace)))
      {
        shell->Release();
        break;
      }
    }

    DISPID dispCopyHere = 0;
    IDispatch *zipFolder = NULL;

    {
      VARIANT arg;
      VariantInit(&arg);
      arg.vt = VT_BSTR;
      arg.bstrVal = SysAllocString(zipPath);

      DISPPARAMS dp;
      dp.rgvarg = &arg;
      dp.rgdispidNamedArgs = NULL;
      dp.cArgs = 1;
      dp.cNamedArgs = 0;

      VARIANT res;
      VariantInit(&res);
      if (FAILED(shell->Invoke(dispNameSpace, IID_NULL, LOCALE_USER_DEFAULT,
                               DISPATCH_METHOD, &dp, &res, NULL, NULL)))
      {
        VariantClear(&arg);
        shell->Release();
        break;
      }
      VariantClear(&arg);
      VariantClear(&res);

      static const BYTE kEmptyZip[] = {
        0x50,0x4B,0x05,0x06,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
      };
      if (!WriteBufferToFile(zipPath, kEmptyZip, sizeof(kEmptyZip)))
      {
        shell->Release();
        break;
      }

      VariantInit(&arg);
      arg.vt = VT_BSTR;
      arg.bstrVal = SysAllocString(zipPath);
      VariantInit(&res);
      if (FAILED(shell->Invoke(dispNameSpace, IID_NULL, LOCALE_USER_DEFAULT,
                               DISPATCH_METHOD, &dp, &res, NULL, NULL)))
      {
        VariantClear(&arg);
        shell->Release();
        break;
      }
      if (res.vt != VT_DISPATCH || !res.pdispVal)
      {
        VariantClear(&res);
        VariantClear(&arg);
        shell->Release();
        break;
      }
      zipFolder = res.pdispVal;
      zipFolder->AddRef();
      VariantClear(&arg);
      VariantClear(&res);
    }

    {
      OLECHAR *name = L"CopyHere";
      if (FAILED(zipFolder->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispCopyHere)))
      {
        zipFolder->Release();
        shell->Release();
        break;
      }
    }

    for (size_t i = 0; i < inputFiles.size(); ++i)
    {
      VARIANT args[2];
      VariantInit(&args[0]);
      VariantInit(&args[1]);

      args[1].vt = VT_BSTR;
      args[1].bstrVal = SysAllocString(inputFiles[i]);
      args[0].vt = VT_I4;
      args[0].lVal = 4 | 16 | 1024; // no progress UI, no confirmations

      DISPPARAMS dp;
      dp.rgvarg = args;
      dp.rgdispidNamedArgs = NULL;
      dp.cArgs = 2;
      dp.cNamedArgs = 0;

      VARIANT res;
      VariantInit(&res);
      HRESULT hr2 = zipFolder->Invoke(dispCopyHere, IID_NULL, LOCALE_USER_DEFAULT,
                                      DISPATCH_METHOD, &dp, &res, NULL, NULL);

      VariantClear(&res);
      VariantClear(&args[0]);
      VariantClear(&args[1]);

      if (FAILED(hr2))
      {
        zipFolder->Release();
        shell->Release();
        return false;
      }

      Sleep(600);
    }

    Sleep(1200);

    zipFolder->Release();
    shell->Release();

    NFind::CFileInfo fi;
    if (!fi.Find(zipPath) || fi.Size <= 22)
      break;

    ok = true;
  } while (false);

  if (needUninit)
    CoUninitialize();

  return ok;
}

static bool ExtractZipWithShell(const FString &zipPath, const FString &destDir)
{
  HRESULT hr = CoInitialize(NULL);
  const bool needUninit = SUCCEEDED(hr);

  bool ok = false;
  do
  {
    IDispatch *shell = NULL;
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(L"Shell.Application", &clsid)))
      break;

    if (FAILED(CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_IDispatch, (void **)&shell)))
      break;

    DISPID dispNameSpace = 0;
    {
      OLECHAR *name = L"NameSpace";
      if (FAILED(shell->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispNameSpace)))
      {
        shell->Release();
        break;
      }
    }

    IDispatch *srcZip = NULL;
    IDispatch *dstDir = NULL;
    DISPID dispCopyHere = 0;

    auto OpenNameSpace = [&](const FString &path, IDispatch **outDisp) -> bool
    {
      *outDisp = NULL;
      VARIANT arg;
      VariantInit(&arg);
      arg.vt = VT_BSTR;
      arg.bstrVal = SysAllocString(path);

      DISPPARAMS dp;
      dp.rgvarg = &arg;
      dp.rgdispidNamedArgs = NULL;
      dp.cArgs = 1;
      dp.cNamedArgs = 0;

      VARIANT res;
      VariantInit(&res);
      const HRESULT h = shell->Invoke(dispNameSpace, IID_NULL, LOCALE_USER_DEFAULT,
                                      DISPATCH_METHOD, &dp, &res, NULL, NULL);

      VariantClear(&arg);
      if (FAILED(h) || res.vt != VT_DISPATCH || !res.pdispVal)
      {
        VariantClear(&res);
        return false;
      }

      *outDisp = res.pdispVal;
      (*outDisp)->AddRef();
      VariantClear(&res);
      return true;
    };

    if (!OpenNameSpace(zipPath, &srcZip))
    {
      shell->Release();
      break;
    }
    if (!OpenNameSpace(destDir, &dstDir))
    {
      srcZip->Release();
      shell->Release();
      break;
    }

    {
      OLECHAR *name = L"CopyHere";
      if (FAILED(dstDir->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispCopyHere)))
      {
        dstDir->Release();
        srcZip->Release();
        shell->Release();
        break;
      }
    }

    DISPID dispItems = 0;
    {
      OLECHAR *name = L"Items";
      if (FAILED(srcZip->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispItems)))
      {
        dstDir->Release();
        srcZip->Release();
        shell->Release();
        break;
      }
    }

    VARIANT itemsRes;
    VariantInit(&itemsRes);
    {
      DISPPARAMS dpItems;
      dpItems.rgvarg = NULL;
      dpItems.rgdispidNamedArgs = NULL;
      dpItems.cArgs = 0;
      dpItems.cNamedArgs = 0;
      if (FAILED(srcZip->Invoke(dispItems, IID_NULL, LOCALE_USER_DEFAULT,
                                DISPATCH_METHOD, &dpItems, &itemsRes, NULL, NULL)) ||
          itemsRes.vt != VT_DISPATCH || !itemsRes.pdispVal)
      {
        VariantClear(&itemsRes);
        dstDir->Release();
        srcZip->Release();
        shell->Release();
        break;
      }
    }

    VARIANT args[2];
    VariantInit(&args[0]);
    VariantInit(&args[1]);

    args[1].vt = VT_DISPATCH;
    args[1].pdispVal = itemsRes.pdispVal;
    itemsRes.pdispVal->AddRef();

    args[0].vt = VT_I4;
    args[0].lVal = 4 | 16 | 1024;

    DISPPARAMS dp;
    dp.rgvarg = args;
    dp.rgdispidNamedArgs = NULL;
    dp.cArgs = 2;
    dp.cNamedArgs = 0;

    VARIANT res;
    VariantInit(&res);
    const HRESULT hr2 = dstDir->Invoke(dispCopyHere, IID_NULL, LOCALE_USER_DEFAULT,
                                       DISPATCH_METHOD, &dp, &res, NULL, NULL);

    VariantClear(&res);
    VariantClear(&args[0]);
    VariantClear(&args[1]);
    VariantClear(&itemsRes);

    if (FAILED(hr2))
    {
      dstDir->Release();
      srcZip->Release();
      shell->Release();
      break;
    }

    Sleep(1200);

    dstDir->Release();
    srcZip->Release();
    shell->Release();
    ok = true;
  } while (false);

  if (needUninit)
    CoUninitialize();

  return ok;
}

static bool EncryptAes256(const std::vector<BYTE> &plain, const UString &password, std::vector<BYTE> &out)
{
  out.clear();

  HCRYPTPROV hProv = 0;
  HCRYPTHASH hHash = 0;
  HCRYPTKEY hKey = 0;

  if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    return false;

  bool ok = false;
  do
  {
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
      break;

    const BYTE *pwdData = (const BYTE *)(const wchar_t *)password;
    const DWORD pwdLen = (DWORD)(password.Len() * sizeof(wchar_t));

    if (!CryptHashData(hHash, pwdData, pwdLen, 0))
      break;

    if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey))
      break;

    DWORD mode = CRYPT_MODE_CBC;
    CryptSetKeyParam(hKey, KP_MODE, (BYTE *)&mode, 0);

    BYTE iv[16] = { 0 };
    for (int i = 0; i < 16; ++i)
      iv[i] = (BYTE)(i * 13 + 7);
    CryptSetKeyParam(hKey, KP_IV, iv, 0);

    const DWORD maxExtra = 32;
    out = plain;
    out.resize(plain.size() + maxExtra);
    DWORD dataLen = (DWORD)plain.size();

    if (!CryptEncrypt(hKey, 0, TRUE, 0, out.data(), &dataLen, (DWORD)out.size()))
      break;

    out.resize(dataLen);
    ok = true;
  } while (false);

  if (hKey)
    CryptDestroyKey(hKey);
  if (hHash)
    CryptDestroyHash(hHash);
  if (hProv)
    CryptReleaseContext(hProv, 0);

  return ok;
}

// -------------------------
// manifest
// -------------------------

struct SManifest
{
  UString settingsHash;
  UString passwordHash;
};

static void ParseManifest(const std::vector<BYTE> &buf, SManifest &m)
{
  m = SManifest();

  if (buf.empty())
    return;

  AString a;
  if (!buf.empty())
    a.SetFrom((const char *)buf.data(), (unsigned)buf.size());
  UString u;
  ConvertUTF8ToUnicode(a, u);

  int pos = 0;
  while (pos < (int)u.Len())
  {
    int end = u.Find(L'\n', (unsigned)pos);
    if (end < 0)
      end = (int)u.Len();

    UString line = u.Mid((unsigned)pos, (unsigned)(end - pos));
    if (!line.IsEmpty() && line.Back() == L'\r')
      line.DeleteBack();

    int eq = line.Find(L'=');
    if (eq > 0)
    {
      UString k = line.Left((unsigned)eq);
      UString v = line.Ptr(eq + 1);
      if (k == L"settings_hash")
        m.settingsHash = v;
      else if (k == L"password_hash")
        m.passwordHash = v;
    }

    pos = end + 1;
  }
}

static void BuildManifestText(const SManifest &m, AString &out)
{
  UString txt;
  txt += L"version=1\r\n";
  txt += L"settings_hash=";
  txt += m.settingsHash;
  txt += L"\r\n";
  txt += L"password_hash=";
  txt += m.passwordHash;
  txt += L"\r\n";
  ConvertUnicodeToUTF8(txt, out);
}

// -------------------------
// backup core
// -------------------------

static bool GetDefaultPasswordBookPath(FString &path)
{
  path.Empty();

  FString primary = NDLL::GetModuleDirPrefix();
  primary += FTEXT("data");
  primary += FCHAR_PATH_SEPARATOR;
  primary += FTEXT("password_book.dat");

  NFind::CFileInfo fi;
  if (fi.Find(primary) && !fi.IsDir())
  {
    path = primary;
    return true;
  }

  WCHAR appDataPath[MAX_PATH] = {};
  if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath)))
    return false;

  FString backup = appDataPath;
  backup += FCHAR_PATH_SEPARATOR;
  backup += FTEXT("7-Zip-ZS-PB");
  backup += FCHAR_PATH_SEPARATOR;
  backup += FTEXT("password_book.dat");

  if (fi.Find(backup) && !fi.IsDir())
  {
    path = backup;
    return true;
  }

  return false;
}

static bool RunBackupCore(const FString &passwordBookPath, bool requireAutoEnabled, UString *errorMessage)
{
  SetError(errorMessage, NULL);

  SConfig cfg;
  if (!LoadConfig(cfg))
  {
    SetError(errorMessage, kErrConfigNotFound);
    return false;
  }

  if (requireAutoEnabled && !cfg.autoEnabled)
  {
    SetError(errorMessage, kErrAutoDisabled);
    return false;
  }

  if (cfg.url.IsEmpty() || cfg.user.IsEmpty() || cfg.pass.IsEmpty())
  {
    SetError(errorMessage, kErrConfigIncomplete);
    return false;
  }

  NFind::CFileInfo passInfo;
  if (!passInfo.Find(passwordBookPath) || passInfo.IsDir())
  {
    SetError(errorMessage, kErrPasswordBookMissing);
    return false;
  }

  FString settingsPath = GetSettingsRegExportPath();
  if (settingsPath.IsEmpty())
  {
    SetError(errorMessage, kErrSettingsPath);
    return false;
  }
  if (!SaveLocalSettingsSnapshot(settingsPath))
  {
    SetError(errorMessage, kErrSettingsExport);
    return false;
  }

  UString settingsHash;
  UString passwordHash;
  if (!GetFileSha256Hex(settingsPath, settingsHash))
  {
    SetError(errorMessage, kErrHashSettings);
    return false;
  }
  if (!GetFileSha256Hex(passwordBookPath, passwordHash))
  {
    SetError(errorMessage, kErrHashPassword);
    return false;
  }

  CClient client;
  if (!client.Connect(cfg))
  {
    SetError(errorMessage, kErrConnect);
    return false;
  }

  const UString rootPath = UString(L"/7z_ZS_PB");
  const UString manifestRemote = JoinPath(rootPath, L"manifest.txt");

  client.MkcolRecursive(rootPath);

  SManifest oldManifest;
  {
    std::vector<BYTE> mf;
    if (client.Get(manifestRemote, mf))
      ParseManifest(mf, oldManifest);
  }

  const bool settingsChanged = oldManifest.settingsHash != settingsHash;
  const bool passwordChanged = oldManifest.passwordHash != passwordHash;
  if (!settingsChanged && !passwordChanged)
    return true;

  if (!EnsureEncryptPassword(cfg, errorMessage))
    return false;

  const UString date = GetDateYYYYMMDD();
  const UString time = GetTimeHHMMSS();
  const UString dayDir = JoinPath(rootPath, date);
  client.MkcolRecursive(dayDir);

  std::vector<FString> files;
  if (settingsChanged)
    files.push_back(settingsPath);
  if (passwordChanged)
    files.push_back(passwordBookPath);

  FString tmpDir = GetAppDataSubDir(L"7-Zip-ZS-PB");
  if (tmpDir.IsEmpty())
  {
    SetError(errorMessage, kErrTempDir);
    return false;
  }
  tmpDir += FCHAR_PATH_SEPARATOR;
  tmpDir += FTEXT("wb_tmp");
  if (!NDir::CreateComplexDir(tmpDir))
  {
    SetError(errorMessage, kErrTempDir);
    return false;
  }

  FString zipLocal = tmpDir;
  zipLocal += FCHAR_PATH_SEPARATOR;
  zipLocal += FTEXT("pack.zip");

  if (!CreateZipWithShell(zipLocal, files))
  {
    SetError(errorMessage, kErrZipPack);
    return false;
  }

  std::vector<BYTE> zipData;
  CScopedSensitiveBuffer zipDataGuard(zipData);
  if (!ReadFileToBuffer(zipLocal, zipData))
  {
    SetError(errorMessage, kErrReadZip);
    return false;
  }

  std::vector<BYTE> encData;
  CScopedSensitiveBuffer encDataGuard(encData);
  if (!EncryptAes256(zipData, cfg.encryptPassword, encData))
  {
    SetError(errorMessage, kErrEncryptFile);
    return false;
  }

  UString archiveName = UString(L"backup_") + time + (cfg.encryptPassword.IsEmpty() ? L".zip" : L".bin");
  const UString archiveRemote = JoinPath(dayDir, archiveName);

  if (!client.Put(archiveRemote, encData.empty() ? "" : (const void *)encData.data(), encData.size()))
  {
    SetError(errorMessage, kErrUploadPack);
    return false;
  }

  SManifest newManifest;
  newManifest.settingsHash = settingsHash;
  newManifest.passwordHash = passwordHash;
  AString manifestTxt;
  BuildManifestText(newManifest, manifestTxt);
  if (!client.Put(manifestRemote, (const char *)manifestTxt, manifestTxt.Len()))
  {
    SetError(errorMessage, kErrUploadManifest);
    return false;
  }

  // password incremental pointer (latest file only)
  if (passwordChanged)
  {
    AString ptr;
    UString ptrTxt = archiveRemote + L"\r\n";
    ConvertUnicodeToUTF8(ptrTxt, ptr);
    client.Put(JoinPath(rootPath, L"password_latest.txt"), (const char *)ptr, ptr.Len());
  }

  return true;
}

void QueueAutoBackup(const FString &passwordBookPath)
{
  FString effectivePath = passwordBookPath;
  if (effectivePath.IsEmpty())
  {
    GetDefaultPasswordBookPath(effectivePath);
  }

  if (effectivePath.IsEmpty())
    return;

  EnsureAutoBackupLock();
  EnterCriticalSection(&g_AutoBackupLock);
  g_AutoBackupPath = effectivePath;
  LeaveCriticalSection(&g_AutoBackupLock);

  InterlockedExchange(&g_AutoBackupPending, 1);

  if (InterlockedCompareExchange(&g_AutoBackupRunning, 1, 0) != 0)
    return;

  DWORD tid = 0;
  HANDLE hThread = ::CreateThread(NULL, 0, AutoBackupWorkerProc, NULL, 0, &tid);
  if (hThread)
  {
    CloseHandle(hThread);
  }
  else
  {
    InterlockedExchange(&g_AutoBackupRunning, 0);
  }
}

void TryAutoBackup(const FString &passwordBookPath)
{
  QueueAutoBackup(passwordBookPath);
}

bool RunManualBackupNow(UString *errorMessage)
{
  try
  {
    FString passwordBookPath;
    if (!GetDefaultPasswordBookPath(passwordBookPath))
    {
      SetError(errorMessage, kErrPasswordBookMissing);
      return false;
    }

    return RunBackupCore(passwordBookPath, false, errorMessage);
  }
  catch (const std::bad_alloc &)
  {
    SetError(errorMessage, L"手动备份失败（内存不足）。");
    return false;
  }
  catch (const std::exception &e)
  {
    UString msg = L"手动备份失败（std::exception）。";
    AString whatA(e.what());
    if (!whatA.IsEmpty())
    {
      UString whatU;
      ConvertUTF8ToUnicode(whatA, whatU);
      if (!whatU.IsEmpty())
      {
        msg += L"\n";
        msg += whatU;
      }
    }
    SetError(errorMessage, msg);
    return false;
  }
  catch (...)
  {
    SetErrorWithCode(errorMessage, kErrManualUnexpectedCode, GetLastError());
    return false;
  }
}

bool ImportPasswordBookBackupFile(const FString &backupFilePath, UString *errorMessage)
{
  SetError(errorMessage, NULL);

  try
  {
    SConfig cfg;
    if (!LoadConfig(cfg))
    {
      SetError(errorMessage, kErrConfigNotFound);
      return false;
    }

    if (!EnsureEncryptPassword(cfg, errorMessage))
      return false;

    NFind::CFileInfo fi;
    if (!fi.Find(backupFilePath) || fi.IsDir())
    {
      SetError(errorMessage, kErrRestoreRead);
      return false;
    }

    std::vector<BYTE> enc;
    CScopedSensitiveBuffer encGuard(enc);
    if (!ReadFileToBuffer(backupFilePath, enc))
    {
      SetError(errorMessage, kErrRestoreRead);
      return false;
    }

    std::vector<BYTE> zip;
    CScopedSensitiveBuffer zipGuard(zip);
    if (!DecryptAes256(enc, cfg.encryptPassword, zip))
    {
      SetError(errorMessage, kErrRestoreDecrypt);
      return false;
    }

    FString workDir;
    if (!CreateUniqueWorkDir(FTEXT("wb_restore"), workDir))
    {
      SetError(errorMessage, kErrTempDir);
      return false;
    }
    CScopedDirCleanup workDirCleanup(workDir);

    FString zipPath = workDir;
    zipPath += FCHAR_PATH_SEPARATOR;
    zipPath += FTEXT("restore.zip");
    if (!WriteBufferToFile(zipPath, zip.empty() ? "" : (const void *)zip.data(), (UInt32)zip.size()))
    {
      SetError(errorMessage, kErrRestoreExtract);
      return false;
    }

    FString unpackDir = workDir;
    unpackDir += FCHAR_PATH_SEPARATOR;
    unpackDir += FTEXT("unpack");
    if (!NDir::CreateComplexDir(unpackDir))
    {
      SetError(errorMessage, kErrTempDir);
      return false;
    }

    if (!ExtractZipWithShell(zipPath, unpackDir))
    {
      SetError(errorMessage, kErrRestoreExtract);
      return false;
    }

    // fallback verify by presence
    FString restoredPass = unpackDir;
    restoredPass += FCHAR_PATH_SEPARATOR;
    restoredPass += FTEXT("password_book.dat");

    if (!fi.Find(restoredPass) || fi.IsDir())
    {
      // try upper-case variant
      restoredPass = unpackDir;
      restoredPass += FCHAR_PATH_SEPARATOR;
      restoredPass += FTEXT("PASSWORD_BOOK.DAT");
      if (!fi.Find(restoredPass) || fi.IsDir())
      {
        SetError(errorMessage, kErrRestoreNoPasswordFile);
        return false;
      }
    }

    FString localPath;
    if (!GetDefaultPasswordBookPath(localPath))
    {
      localPath = NDLL::GetModuleDirPrefix();
      localPath += FTEXT("data");
      NDir::CreateComplexDir(localPath);
      localPath += FCHAR_PATH_SEPARATOR;
      localPath += FTEXT("password_book.dat");
    }

    if (!CopyFileToPath(restoredPass, localPath))
    {
      SetError(errorMessage, kErrRestoreWriteLocal);
      return false;
    }

    return true;
  }
  catch (const std::bad_alloc &)
  {
    SetError(errorMessage, L"导入备份失败（内存不足）。");
    return false;
  }
  catch (const std::exception &e)
  {
    UString msg = L"导入备份失败（std::exception）。";
    AString whatA(e.what());
    if (!whatA.IsEmpty())
    {
      UString whatU;
      ConvertUTF8ToUnicode(whatA, whatU);
      if (!whatU.IsEmpty())
      {
        msg += L"\n";
        msg += whatU;
      }
    }
    SetError(errorMessage, msg);
    return false;
  }
  catch (...)
  {
    SetErrorWithCode(errorMessage, kErrRestoreUnexpectedCode, GetLastError());
    return false;
  }
}

bool RunManualRestoreNow(UString *errorMessage)
{
  SetError(errorMessage, NULL);

  try
  {
    SConfig cfg;
    if (!LoadConfig(cfg))
    {
      SetError(errorMessage, kErrConfigNotFound);
      return false;
    }

    if (!EnsureEncryptPassword(cfg, errorMessage))
      return false;

    if (cfg.url.IsEmpty() || cfg.user.IsEmpty() || cfg.pass.IsEmpty())
    {
      SetError(errorMessage, kErrConfigIncomplete);
      return false;
    }

    CClient client;
    if (!client.Connect(cfg))
    {
      SetError(errorMessage, kErrConnect);
      return false;
    }

    const UString rootPath = UString(L"/7z_ZS_PB");
    std::vector<BYTE> ptr;
    if (!client.Get(JoinPath(rootPath, L"password_latest.txt"), ptr))
    {
      SetError(errorMessage, kErrRestoreRemoteIndex);
      return false;
    }

    AString ptrA;
    ptrA.SetFrom((const char *)ptr.data(), (unsigned)ptr.size());
    UString ptrU;
    ConvertUTF8ToUnicode(ptrA, ptrU);
    ptrU.Trim();
    if (ptrU.IsEmpty())
    {
      SetError(errorMessage, kErrRestoreRemotePath);
      return false;
    }

    std::vector<BYTE> enc;
    CScopedSensitiveBuffer encGuard(enc);
    if (!client.Get(ptrU, enc))
    {
      SetError(errorMessage, kErrRestoreDownload);
      return false;
    }

    FString localEnc = GetAppDataSubDir(L"7-Zip-ZS-PB");
    if (localEnc.IsEmpty())
    {
      SetError(errorMessage, kErrTempDir);
      return false;
    }
    localEnc += FCHAR_PATH_SEPARATOR;
    localEnc += FTEXT("wb_restore_remote.bin");
    if (!WriteBufferToFile(localEnc, enc.empty() ? "" : (const void *)enc.data(), (UInt32)enc.size()))
    {
      SetError(errorMessage, kErrTempDir);
      return false;
    }

    const bool importOk = ImportPasswordBookBackupFile(localEnc, errorMessage);
    NDir::DeleteFileAlways(localEnc);
    return importOk;
  }
  catch (const std::bad_alloc &)
  {
    SetError(errorMessage, L"导入备份失败（内存不足）。");
    return false;
  }
  catch (const std::exception &e)
  {
    UString msg = L"导入备份失败（std::exception）。";
    AString whatA(e.what());
    if (!whatA.IsEmpty())
    {
      UString whatU;
      ConvertUTF8ToUnicode(whatA, whatU);
      if (!whatU.IsEmpty())
      {
        msg += L"\n";
        msg += whatU;
      }
    }
    SetError(errorMessage, msg);
    return false;
  }
  catch (...)
  {
    SetErrorWithCode(errorMessage, kErrRestoreUnexpectedCode, GetLastError());
    return false;
  }
}

} // namespace NWebDAVBackup



