// UpdateGUI.cpp

#include "StdAfx.h"

#include "../../../Common/IntToString.h"
#include "../../../Common/StringConvert.h"
#include "../../../Common/StringToInt.h"
#include "../../../Common/UTFConvert.h"

#include "../../../Windows/DLL.h"
#include "../../../Windows/FileDir.h"
#include "../../../Windows/FileName.h"
#include "../../../Windows/Thread.h"

#include "../Common/WorkDir.h"

#include "../Explorer/MyMessages.h"

#include "../FileManager/LangUtils.h"
#include "../FileManager/StringUtils.h"
#include "../FileManager/resourceGui.h"

#include "CompressDialog.h"
#include "UpdateGUI.h"

#include "resource2.h"

using namespace NWindows;
using namespace NFile;
using namespace NDir;

static const char * const kDefaultSfxModule = "7z.sfx";
static const char * const kSFXExtension = "exe";

extern void AddMessageToString(UString &dest, const UString &src);

UString HResultToMessage(HRESULT errorCode);

class CThreadUpdating: public CProgressThreadVirt
{
  HRESULT ProcessVirt() Z7_override;
public:
  CCodecs *codecs;
  const CObjectVector<COpenType> *formatIndices;
  const UString *cmdArcPath;
  CUpdateCallbackGUI *UpdateCallbackGUI;
  NWildcard::CCensor *WildcardCensor;
  CUpdateOptions *Options;
  bool needSetPath;
};
 
HRESULT CThreadUpdating::ProcessVirt()
{
  CUpdateErrorInfo ei;
  HRESULT res = UpdateArchive(codecs, *formatIndices, *cmdArcPath,
      *WildcardCensor, *Options,
      ei, UpdateCallbackGUI, UpdateCallbackGUI, needSetPath);
  FinalMessage.ErrorMessage.Message = ei.Message.Ptr();
  ErrorPaths = ei.FileNames;
  if (res != S_OK)
    return res;
  return HRESULT_FROM_WIN32(ei.SystemError);
}


// parse command line properties

static bool ParseProp_Time_BoolPair(const CProperty &prop, const char *name, CBoolPair &bp)
{
  if (!prop.Name.IsPrefixedBy_Ascii_NoCase(name))
    return false;
  const UString rem = prop.Name.Ptr((unsigned)strlen(name));
  UString val = prop.Value;
  if (!rem.IsEmpty())
  {
    if (!val.IsEmpty())
      return true;
    val = rem;
  }
  bool res;
  if (StringToBool(val, res))
  {
    bp.Val = res;
    bp.Def = true;
  }
  return true;
}

static void ParseProp(
    const CProperty &prop,
    NCompressDialog::CInfo &di)
{
  if (ParseProp_Time_BoolPair(prop, "tm", di.MTime)) return;
  if (ParseProp_Time_BoolPair(prop, "tc", di.CTime)) return;
  if (ParseProp_Time_BoolPair(prop, "ta", di.ATime)) return;
}

static void ParseProperties(
    const CObjectVector<CProperty> &properties,
    NCompressDialog::CInfo &di)
{
  FOR_VECTOR (i, properties)
  {
    ParseProp(properties[i], di);
  }
}





static void AddProp_UString(CObjectVector<CProperty> &properties, const char *name, const UString &value)
{
  CProperty prop;
  prop.Name = name;
  prop.Value = value;
  properties.Add(prop);
}

static void AddProp_UInt32(CObjectVector<CProperty> &properties, const char *name, UInt32 value)
{
  UString s;
  s.Add_UInt32(value);
  AddProp_UString(properties, name, s);
}

static void AddProp_bool(CObjectVector<CProperty> &properties, const char *name, bool value)
{
  AddProp_UString(properties, name, UString(value ? "on": "off"));
}


static void AddProp_BoolPair(CObjectVector<CProperty> &properties,
    const char *name, const CBoolPair &bp)
{
  if (bp.Def)
    AddProp_bool(properties, name, bp.Val);
}



static void SplitOptionsToStrings(const UString &src, UStringVector &strings)
{
  SplitString(src, strings);
  FOR_VECTOR (i, strings)
  {
    UString &s = strings[i];
    if (s.Len() > 2
        && s[0] == '-'
        && MyCharLower_Ascii(s[1]) == 'm')
      s.DeleteFrontal(2);
  }
}

static bool IsThereMethodOverride(bool is7z, const UStringVector &strings)
{
  FOR_VECTOR (i, strings)
  {
    const UString &s = strings[i];
    if (is7z)
    {
      const wchar_t *end;
      UInt64 n = ConvertStringToUInt64(s, &end);
      if (n == 0 && *end == L'=')
        return true;
    }
    else
    {
      if (s.Len() > 0)
        if (s[0] == L'm' && s[1] == L'=')
          return true;
    }
  }
  return false;
}

static void ParseAndAddPropertires(CObjectVector<CProperty> &properties,
    const UStringVector &strings)
{
  FOR_VECTOR (i, strings)
  {
    const UString &s = strings[i];
    CProperty property;
    const int index = s.Find(L'=');
    if (index < 0)
      property.Name = s;
    else
    {
      property.Name.SetFrom(s, (unsigned)index);
      property.Value = s.Ptr(index + 1);
    }
    properties.Add(property);
  }
}


static void AddProp_Size(CObjectVector<CProperty> &properties, const char *name, const UInt64 size)
{
  UString s;
  s.Add_UInt64(size);
  s.Add_Char('b');
  AddProp_UString(properties, name, s);
}


static void SetOutProperties(
    CObjectVector<CProperty> &properties,
    const NCompressDialog::CInfo &di,
    bool is7z,
    bool setMethod)
{
  if (di.Level != (UInt32)(Int32)-1)
    AddProp_UInt32(properties, "x", (UInt32)di.Level);
  if (setMethod)
  {
    if (!di.Method.IsEmpty())
      AddProp_UString(properties, is7z ? "0": "m", di.Method);
    if (di.Dict64 != (UInt64)(Int64)-1)
    {
      AString name;
      if (is7z)
        name = "0";
      name += (di.OrderMode ? "mem" : "d");
      AddProp_Size(properties, name, di.Dict64);
    }
    /*
    if (di.Dict64_Chain != (UInt64)(Int64)-1)
    {
      AString name;
      if (is7z)
        name = "0";
      name += "dc";
      AddProp_Size(properties, name, di.Dict64_Chain);
    }
    */
    if (di.Order != (UInt32)(Int32)-1)
    {
      AString name;
      if (is7z)
        name = "0";
      name += (di.OrderMode ? "o" : "fb");
      AddProp_UInt32(properties, name, (UInt32)di.Order);
    }
  }
    
  if (!di.EncryptionMethod.IsEmpty())
    AddProp_UString(properties, "em", di.EncryptionMethod);

  if (di.EncryptHeadersIsAllowed)
    AddProp_bool(properties, "he", di.EncryptHeaders);

  if (di.SolidIsSpecified)
    AddProp_Size(properties, "s", di.SolidBlockSize);
  
  if (
      // di.MultiThreadIsAllowed &&
      di.NumThreads != (UInt32)(Int32)-1)
    AddProp_UInt32(properties, "mt", di.NumThreads);
  
  const NCompression::CMemUse &memUse = di.MemUsage;
  if (memUse.IsDefined)
  {
    const char *kMemUse = "memuse";
    if (memUse.IsPercent)
    {
      UString s;
      // s += 'p'; // for debug: alternate percent method
      s.Add_UInt64(memUse.Val);
      s.Add_Char('%');
      AddProp_UString(properties, kMemUse, s);
    }
    else
      AddProp_Size(properties, kMemUse, memUse.Val);
  }

  AddProp_BoolPair(properties, "tm", di.MTime);
  AddProp_BoolPair(properties, "tc", di.CTime);
  AddProp_BoolPair(properties, "ta", di.ATime);

  if (di.TimePrec != (UInt32)(Int32)-1)
    AddProp_UInt32(properties, "tp", di.TimePrec);
}


struct C_UpdateMode_ToAction_Pair
{
  NCompressDialog::NUpdateMode::EEnum UpdateMode;
  const NUpdateArchive::CActionSet *ActionSet;
};

static const C_UpdateMode_ToAction_Pair g_UpdateMode_Pairs[] =
{
  { NCompressDialog::NUpdateMode::kAdd,    &NUpdateArchive::k_ActionSet_Add },
  { NCompressDialog::NUpdateMode::kUpdate, &NUpdateArchive::k_ActionSet_Update },
  { NCompressDialog::NUpdateMode::kFresh,  &NUpdateArchive::k_ActionSet_Fresh },
  { NCompressDialog::NUpdateMode::kSync,   &NUpdateArchive::k_ActionSet_Sync }
};

static int FindActionSet(const NUpdateArchive::CActionSet &actionSet)
{
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_UpdateMode_Pairs); i++)
    if (actionSet.IsEqualTo(*g_UpdateMode_Pairs[i].ActionSet))
      return (int)i;
  return -1;
}

static int FindUpdateMode(NCompressDialog::NUpdateMode::EEnum mode)
{
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_UpdateMode_Pairs); i++)
    if (mode == g_UpdateMode_Pairs[i].UpdateMode)
      return (int)i;
  return -1;
}


static HRESULT ShowDialog(
    CCodecs *codecs,
    const CObjectVector<NWildcard::CCensorPath> &censor,
    CUpdateOptions &options,
    bool &compressSeparately,
    UString &serialStart,
    CUpdateCallbackGUI *callback, HWND hwndParent)
{
  if (options.Commands.Size() != 1)
    throw "It must be one command";
  /*
  FString currentDirPrefix;
  #ifndef UNDER_CE
  {
    if (!MyGetCurrentDirectory(currentDirPrefix))
      return E_FAIL;
    NName::NormalizeDirPathPrefix(currentDirPrefix);
  }
  #endif
  */

  bool oneFile = false;
  NFind::CFileInfo fileInfo;
  UString name;
  
  /*
  if (censor.Pairs.Size() > 0)
  {
    const NWildcard::CPair &pair = censor.Pairs[0];
    if (pair.Head.IncludeItems.Size() > 0)
    {
      const NWildcard::CItem &item = pair.Head.IncludeItems[0];
      if (item.ForFile)
      {
        name = pair.Prefix;
        FOR_VECTOR (i, item.PathParts)
        {
          if (i > 0)
            name.Add_PathSepar();
          name += item.PathParts[i];
        }
        if (fileInfo.Find(us2fs(name)))
        {
          if (censor.Pairs.Size() == 1 && pair.Head.IncludeItems.Size() == 1)
            oneFile = !fileInfo.IsDir();
        }
      }
    }
  }
  */
  if (censor.Size() > 0)
  {
    const NWildcard::CCensorPath &cp = censor[0];
    if (cp.Include)
    {
      {
        if (fileInfo.Find(us2fs(cp.Path)))
        {
          if (censor.Size() == 1)
            oneFile = !fileInfo.IsDir();
        }
      }
    }
  }


  /*
  // v23: we restore current dir in dialog code
  #if defined(_WIN32) && !defined(UNDER_CE)
  CCurrentDirRestorer curDirRestorer;
  #endif
  */

  CCompressDialog dialog;
  NCompressDialog::CInfo &di = dialog.Info;
  dialog.ArcFormats = &codecs->Formats;
  {
    CObjectVector<CCodecInfoUser> userCodecs;
    codecs->Get_CodecsInfoUser_Vector(userCodecs);
    dialog.SetMethods(userCodecs);
  }

  if (options.MethodMode.Type_Defined)
    di.FormatIndex = options.MethodMode.Type.FormatIndex;
  
  FOR_VECTOR (i, codecs->Formats)
  {
    const CArcInfoEx &ai = codecs->Formats[i];
    if (!ai.UpdateEnabled)
      continue;
    if (!oneFile && ai.Flags_KeepName())
      continue;
    if ((int)i != di.FormatIndex)
    {
      if (ai.Flags_HashHandler())
        continue;
      if (ai.Name.IsEqualTo_Ascii_NoCase("swfc"))
        if (!oneFile || name.Len() < 4 || !StringsAreEqualNoCase_Ascii(name.RightPtr(4), ".swf"))
          continue;
    }
    dialog.ArcIndices.Add(i);
  }
  if (dialog.ArcIndices.IsEmpty())
  {
    ShowErrorMessage(L"No Update Engines");
    return E_FAIL;
  }

  // di.ArchiveName = options.ArchivePath.GetFinalPath();
  di.ArcPath = options.ArchivePath.GetPathWithoutExt();
  dialog.OriginalFileName = fs2us(fileInfo.Name);

  di.PathMode = options.PathMode;
    
  // di.CurrentDirPrefix = currentDirPrefix;
  di.SFXMode = options.SfxMode;
  di.OpenShareForWrite = options.OpenShareForWrite;
  di.DeleteAfterCompressing = options.DeleteAfterCompressing;

  di.SymLinks = options.SymLinks;
  di.HardLinks = options.HardLinks;
  di.AltStreams = options.AltStreams;
  di.NtSecurity = options.NtSecurity;
  if (options.SetArcMTime)
    di.SetArcMTime.SetTrueTrue();
  if (options.PreserveATime)
    di.PreserveATime.SetTrueTrue();
  
  if (callback->PasswordIsDefined)
    di.Password = callback->Password;
    
  di.KeepName = !oneFile;

  NUpdateArchive::CActionSet &actionSet = options.Commands.Front().ActionSet;
 
  {
    int index = FindActionSet(actionSet);
    if (index < 0)
      return E_NOTIMPL;
    di.UpdateMode = g_UpdateMode_Pairs[(unsigned)index].UpdateMode;
  }

  ParseProperties(options.MethodMode.Properties, di);

  if (dialog.Create(hwndParent) != IDOK)
    return E_ABORT;

  options.DeleteAfterCompressing = di.DeleteAfterCompressing;
  compressSeparately = di.CompressSeparately;
  serialStart = di.SerialStart;

  options.SymLinks = di.SymLinks;
  options.HardLinks = di.HardLinks;
  options.AltStreams = di.AltStreams;
  options.NtSecurity = di.NtSecurity;
  options.SetArcMTime = di.SetArcMTime.Val;
  if (di.PreserveATime.Def)
    options.PreserveATime = di.PreserveATime.Val;
 
  /*
  #if defined(_WIN32) && !defined(UNDER_CE)
  curDirRestorer.NeedRestore = dialog.CurrentDirWasChanged;
  #endif
  */
  
  options.VolumesSizes = di.VolumeSizes;
  /*
  if (di.VolumeSizeIsDefined)
  {
    MyMessageBox(L"Splitting to volumes is not supported");
    return E_FAIL;
  }
  */

 
  {
    int index = FindUpdateMode(di.UpdateMode);
    if (index < 0)
      return E_FAIL;
    actionSet = *g_UpdateMode_Pairs[index].ActionSet;
  }

  options.PathMode = di.PathMode;

  const CArcInfoEx &archiverInfo = codecs->Formats[di.FormatIndex];
  callback->PasswordIsDefined = (!di.Password.IsEmpty());
  if (callback->PasswordIsDefined)
    callback->Password = di.Password;

  // we clear command line options, and fill options form Dialog
  options.MethodMode.Properties.Clear();

  const bool is7z = archiverInfo.Is_7z();

  UStringVector optionStrings;
  SplitOptionsToStrings(di.Options, optionStrings);
  const bool methodOverride = IsThereMethodOverride(is7z, optionStrings);

  SetOutProperties(options.MethodMode.Properties, di,
      is7z,
      !methodOverride); // setMethod
  
  options.OpenShareForWrite = di.OpenShareForWrite;
  ParseAndAddPropertires(options.MethodMode.Properties, optionStrings);

  if (di.SFXMode)
    options.SfxMode = true;
  options.MethodMode.Type = COpenType();
  options.MethodMode.Type_Defined = true;
  options.MethodMode.Type.FormatIndex = di.FormatIndex;

  options.ArchivePath.VolExtension = archiverInfo.GetMainExt();
  if (di.SFXMode)
    options.ArchivePath.BaseExtension = kSFXExtension;
  else
    options.ArchivePath.BaseExtension = options.ArchivePath.VolExtension;
  options.ArchivePath.ParseFromPath(di.ArcPath, k_ArcNameMode_Smart);

  NWorkDir::CInfo workDirInfo;
  workDirInfo.Load();
  options.WorkingDir.Empty();
  if (workDirInfo.Mode != NWorkDir::NMode::kCurrent)
  {
    FString fullPath;
    MyGetFullPathName(us2fs(di.ArcPath), fullPath);
    FString namePart;
    options.WorkingDir = GetWorkDir(workDirInfo, fullPath, namePart);
    CreateComplexDir(options.WorkingDir);
  }
  return S_OK;
}


// --- Serial number generation for "Compress each item separately" ---

enum ESerialType
{
  kSerial_None,
  kSerial_Numeric,    // 001, 02, 1, etc.
  kSerial_AlphaUpper, // A, B, C...Z, AA, AB...
  kSerial_AlphaLower, // a, b, c...z, aa, ab...
  kSerial_Roman       // I, II, III, IV...
};

static bool IsAllDigits(const UString &s)
{
  for (unsigned i = 0; i < s.Len(); i++)
    if (s[i] < L'0' || s[i] > L'9')
      return false;
  return s.Len() > 0;
}

static bool IsAllUpperAlpha(const UString &s)
{
  for (unsigned i = 0; i < s.Len(); i++)
    if (s[i] < L'A' || s[i] > L'Z')
      return false;
  return s.Len() > 0;
}

static bool IsAllLowerAlpha(const UString &s)
{
  for (unsigned i = 0; i < s.Len(); i++)
    if (s[i] < L'a' || s[i] > L'z')
      return false;
  return s.Len() > 0;
}

static const wchar_t * const kRomanOnes[]    = { L"", L"I", L"II", L"III", L"IV", L"V", L"VI", L"VII", L"VIII", L"IX" };
static const wchar_t * const kRomanTens[]    = { L"", L"X", L"XX", L"XXX", L"XL", L"L", L"LX", L"LXX", L"LXXX", L"XC" };
static const wchar_t * const kRomanHundreds[]= { L"", L"C", L"CC", L"CCC", L"CD", L"D", L"DC", L"DCC", L"DCCC", L"CM" };
static const wchar_t * const kRomanThousands[]={ L"", L"M", L"MM", L"MMM" };

static UString IntToRoman(unsigned val)
{
  if (val == 0 || val > 3999)
  {
    UString s;
    s.Add_UInt32(val);
    return s;
  }
  UString r;
  r += kRomanThousands[val / 1000]; val %= 1000;
  r += kRomanHundreds[val / 100];   val %= 100;
  r += kRomanTens[val / 10];        val %= 10;
  r += kRomanOnes[val];
  return r;
}

static int RomanToInt(const UString &s)
{
  int total = 0;
  int prev = 0;
  for (int i = (int)s.Len() - 1; i >= 0; i--)
  {
    int v = 0;
    switch (s[i])
    {
      case L'I': case L'i': v = 1; break;
      case L'V': case L'v': v = 5; break;
      case L'X': case L'x': v = 10; break;
      case L'L': case L'l': v = 50; break;
      case L'C': case L'c': v = 100; break;
      case L'D': case L'd': v = 500; break;
      case L'M': case L'm': v = 1000; break;
      default: return -1;
    }
    if (v < prev)
      total -= v;
    else
      total += v;
    prev = v;
  }
  return total;
}

static bool IsRoman(const UString &s)
{
  if (s.IsEmpty()) return false;
  int v = RomanToInt(s);
  if (v <= 0 || v > 3999) return false;
  // Verify round-trip
  UString check = IntToRoman((unsigned)v);
  // case-insensitive compare
  if (check.Len() != s.Len()) return false;
  for (unsigned i = 0; i < check.Len(); i++)
  {
    wchar_t a = check[i];
    wchar_t b = s[i];
    if (b >= L'a' && b <= L'z') b -= 32;
    if (a != b) return false;
  }
  return true;
}

static ESerialType DetectSerialType(const UString &start)
{
  if (start.IsEmpty()) return kSerial_None;
  if (IsAllDigits(start)) return kSerial_Numeric;
  if (IsRoman(start)) return kSerial_Roman;
  if (IsAllUpperAlpha(start)) return kSerial_AlphaUpper;
  if (IsAllLowerAlpha(start)) return kSerial_AlphaLower;
  return kSerial_None;
}

static unsigned AlphaToInt(const UString &s)
{
  unsigned val = 0;
  for (unsigned i = 0; i < s.Len(); i++)
  {
    wchar_t c = s[i];
    unsigned digit;
    if (c >= L'A' && c <= L'Z') digit = c - L'A';
    else if (c >= L'a' && c <= L'z') digit = c - L'a';
    else digit = 0;
    val = val * 26 + digit;
  }
  return val;
}

static UString IntToAlpha(unsigned val, unsigned minLen, bool upper)
{
  UString r;
  do
  {
    wchar_t c = (wchar_t)((val % 26) + (upper ? L'A' : L'a'));
    r.InsertAtFront(c);
    val /= 26;
  }
  while (val > 0);
  while (r.Len() < minLen)
    r.InsertAtFront(upper ? L'A' : L'a');
  return r;
}

static UString GenerateSerial(const UString &start, ESerialType type, unsigned offset)
{
  switch (type)
  {
    case kSerial_Numeric:
    {
      // Parse start as integer, add offset, pad to same width
      UInt64 startVal = 0;
      for (unsigned i = 0; i < start.Len(); i++)
        startVal = startVal * 10 + (start[i] - L'0');
      UInt64 val = startVal + offset;
      wchar_t buf[32];
      int pos = 31;
      buf[pos] = 0;
      do
      {
        buf[--pos] = (wchar_t)(L'0' + (val % 10));
        val /= 10;
      }
      while (val > 0);
      // Pad to at least start.Len()
      while ((unsigned)(31 - pos) < start.Len())
        buf[--pos] = L'0';
      return UString(buf + pos);
    }
    case kSerial_AlphaUpper:
      return IntToAlpha(AlphaToInt(start) + offset, start.Len(), true);
    case kSerial_AlphaLower:
      return IntToAlpha(AlphaToInt(start) + offset, start.Len(), false);
    case kSerial_Roman:
    {
      int startVal = RomanToInt(start);
      if (startVal <= 0) startVal = 1;
      return IntToRoman((unsigned)(startVal + (int)offset));
    }
    default:
      return UString();
  }
}


HRESULT UpdateGUI(
    CCodecs *codecs,
    const CObjectVector<COpenType> &formatIndices,
    const UString &cmdArcPath,
    NWildcard::CCensor &censor,
    CUpdateOptions &options,
    bool showDialog,
    bool &messageWasDisplayed,
    CUpdateCallbackGUI *callback,
    HWND hwndParent)
{
  messageWasDisplayed = false;
  bool needSetPath  = true;
  bool compressSeparately = false;
  UString serialStart;
  if (showDialog)
  {
    RINOK(ShowDialog(codecs, censor.CensorPaths, options, compressSeparately, serialStart, callback, hwndParent))
    needSetPath = false;
  }
  if (options.SfxMode && options.SfxModule.IsEmpty())
  {
    options.SfxModule = NWindows::NDLL::GetModuleDirPrefix();
    options.SfxModule += kDefaultSfxModule;
  }

  // "Compress each item separately" mode:
  // Try CensorPaths first (CompressCall2/FileManager path: CensorPaths has data, Pairs empty)
  // Fallback to Pairs (command-line GUI.cpp path: CensorPaths consumed by Parse2, Pairs has data)
  UStringVector separateItems;
  if (compressSeparately)
  {
    FOR_VECTOR (ci, censor.CensorPaths)
    {
      if (censor.CensorPaths[ci].Include)
        separateItems.Add(censor.CensorPaths[ci].Path);
    }
    if (separateItems.IsEmpty())
    {
      FOR_VECTOR (pi, censor.Pairs)
      {
        const NWildcard::CPair &pair = censor.Pairs[pi];
        FOR_VECTOR (ii, pair.Head.IncludeItems)
        {
          const NWildcard::CItem &item = pair.Head.IncludeItems[ii];
          UString path = pair.Prefix;
          FOR_VECTOR (pp, item.PathParts)
          {
            if (pp > 0)
              path.Add_PathSepar();
            path += item.PathParts[pp];
          }
          separateItems.Add(path);
        }
      }
    }
  }

  if (compressSeparately && separateItems.Size() >= 1)
  {
    const CArchivePath savedArchivePath = options.ArchivePath;
    const ESerialType serialType = DetectSerialType(serialStart);

    for (unsigned idx = 0; idx < separateItems.Size(); idx++)
    {
      const UString &itemPath = separateItems[idx];

      NWildcard::CCensor singleCensor;
      {
        NWildcard::CCensorPath cp;
        cp.Path = itemPath;
        cp.Include = true;
        singleCensor.CensorPaths.Add(cp);
        singleCensor.AddPathsToCensor(NWildcard::k_AbsPath);
      }
      UString itemName;
      int slashPos = itemPath.ReverseFind_PathSepar();
      if (slashPos >= 0)
        itemName = itemPath.Ptr((unsigned)(slashPos + 1));
      else
        itemName = itemPath;
      // Remove trailing separator if any
      if (itemName.Len() > 0 && IS_PATH_SEPAR(itemName.Back()))
        itemName.DeleteBack();

      // Prepend serial number if configured
      UString arcName = itemName;
      if (serialType != kSerial_None)
      {
        UString serial = GenerateSerial(serialStart, serialType, idx);
        arcName = serial;
        arcName += L"-";
        arcName += itemName;
      }

      // Set archive path: same directory as original, with new name + original extension
      options.ArchivePath = savedArchivePath;
      options.ArchivePath.Name = arcName;

      CThreadUpdating tu;
      tu.needSetPath = needSetPath;
      tu.codecs = codecs;
      tu.formatIndices = &formatIndices;
      tu.cmdArcPath = &cmdArcPath;
      tu.UpdateCallbackGUI = callback;
      tu.UpdateCallbackGUI->ProgressDialog = &tu;
      tu.UpdateCallbackGUI->Init();

      UString title = LangString(IDS_PROGRESS_COMPRESSING);
      title += L" (";
      title += itemName;
      title += L")";

      tu.WildcardCensor = &singleCensor;
      tu.Options = &options;
      tu.IconID = IDI_ICON;

      RINOK(tu.Create(title, hwndParent))

      if (tu.Result != S_OK)
      {
        messageWasDisplayed = tu.ThreadFinishedOK && tu.MessagesDisplayed;
        return tu.Result;
      }
    }
    messageWasDisplayed = true;
    return S_OK;
  }

  CThreadUpdating tu;

  tu.needSetPath = needSetPath;

  tu.codecs = codecs;
  tu.formatIndices = &formatIndices;
  tu.cmdArcPath = &cmdArcPath;

  tu.UpdateCallbackGUI = callback;
  tu.UpdateCallbackGUI->ProgressDialog = &tu;
  tu.UpdateCallbackGUI->Init();

  UString title = LangString(IDS_PROGRESS_COMPRESSING);
  if (!formatIndices.IsEmpty())
  {
    const int fin = formatIndices[0].FormatIndex;
    if (fin >= 0)
      if (codecs->Formats[fin].Flags_HashHandler())
        title = LangString(IDS_CHECKSUM_CALCULATING);
  }

  /*
  if (hwndParent != 0)
  {
    tu.ProgressDialog.MainWindow = hwndParent;
    // tu.ProgressDialog.MainTitle = fileName;
    tu.ProgressDialog.MainAddTitle = title + L' ';
  }
  */

  tu.WildcardCensor = &censor;
  tu.Options = &options;
  tu.IconID = IDI_ICON;

  RINOK(tu.Create(title, hwndParent))

  messageWasDisplayed = tu.ThreadFinishedOK && tu.MessagesDisplayed;
  return tu.Result;
}
