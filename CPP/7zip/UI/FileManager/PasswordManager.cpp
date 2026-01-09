// PasswordManager.cpp

#include "StdAfx.h"

#include "../../../Common/UTFConvert.h"
#include "../../../Windows/DLL.h"
#include "../../../Windows/FileDir.h"
#include "../../../Windows/FileIO.h"

#include "PasswordManager.h"

#include <ShlObj.h>

using namespace NWindows;

// Global instance
CPasswordManager g_PasswordManager;

static const char *const kPasswordFileName = "password_book.dat";
static const WCHAR *const kDataFolder = L"data";
static const WCHAR *const kBackupAppFolder = L"7-Zip-ZS-PB";

// Simple XOR encryption key
static const unsigned char kXorKey[] = {0x7A, 0x5F, 0x70, 0x62, 0x6B, 0x21, 0x32, 0x30};
static const unsigned kXorKeyLen = sizeof(kXorKey);

// File magic header for encrypted files
static const char kFileMagic[] = "7ZPB01";
static const unsigned kMagicLen = 6;

// XOR encrypt/decrypt (same operation)
static void XorCrypt(char *data, unsigned len)
{
    for (unsigned i = 0; i < len; i++)
        data[i] ^= kXorKey[i % kXorKeyLen];
}

CPasswordManager::CPasswordManager()
{
    GetPasswordFilePath();
}

void CPasswordManager::GetPasswordFilePath()
{
    // Primary: <exe>\data\password_book.dat
    FString moduleDir = NDLL::GetModuleDirPrefix();
    _filePath = moduleDir;
    _filePath += kDataFolder;
    NFile::NDir::CreateComplexDir(_filePath);
    _filePath += FCHAR_PATH_SEPARATOR;
    _filePath += kPasswordFileName;

    // Backup: %APPDATA%\7-Zip-ZS-PB\password_book.dat
    WCHAR appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath)))
    {
        _backupPath = appDataPath;
        _backupPath += FCHAR_PATH_SEPARATOR;
        _backupPath += kBackupAppFolder;
        NFile::NDir::CreateComplexDir(_backupPath);
        _backupPath += FCHAR_PATH_SEPARATOR;
        _backupPath += kPasswordFileName;
    }
}

// Parse a line in format: Password|||DisplayName
static bool ParsePasswordLine(const UString &line, CPasswordEntry &entry)
{
    // Find the delimiter
    int delimPos = line.Find(L"|||");
    if (delimPos < 0)
    {
        // Old format: just password (for backward compatibility)
        entry.Password = line;
        entry.Name = line;
        return true;
    }

    entry.Password = line.Left(delimPos);
    entry.Name = line.Ptr(delimPos + 3); // Skip "|||"

    return !entry.Password.IsEmpty();
}

bool CPasswordManager::LoadFromPath(const FString &path)
{
    NFile::NIO::CInFile file;
    if (!file.Open(path))
        return false;

    UInt64 fileSize;
    if (!file.GetLength(fileSize))
        return false;

    if (fileSize == 0)
        return true;

    if (fileSize > (1 << 20)) // Limit to 1MB
        return false;

    AString data;
    char *buf = data.GetBuf_SetEnd((unsigned)fileSize);

    UInt32 processedSize;
    if (!file.Read(buf, (UInt32)fileSize, processedSize))
        return false;

    if (processedSize != fileSize)
        return false;

    // Check magic header and decrypt
    unsigned dataStart = 0;
    bool isEncrypted = (fileSize > kMagicLen && memcmp(buf, kFileMagic, kMagicLen) == 0);
    if (isEncrypted)
    {
        dataStart = kMagicLen;
        XorCrypt(buf + dataStart, (unsigned)(fileSize - dataStart));
    }

    // Parse line by line
    AString line;
    unsigned dataLen = (unsigned)fileSize;
    for (unsigned i = dataStart; i < dataLen;)
    {
        char c = data[i++];
        if (c == '\r')
            continue;
        if (c == '\n')
        {
            if (!line.IsEmpty() && line[0] != '#') // Skip comments
            {
                UString uline;
                ConvertUTF8ToUnicode(line, uline);
                if (!uline.IsEmpty())
                {
                    CPasswordEntry entry;
                    if (ParsePasswordLine(uline, entry))
                        _entries.Add(entry);
                }
            }
            line.Empty();
            continue;
        }
        line += c;
    }

    // Handle last line without newline
    if (!line.IsEmpty() && line[0] != '#') // Skip comments
    {
        UString uline;
        ConvertUTF8ToUnicode(line, uline);
        if (!uline.IsEmpty())
        {
            CPasswordEntry entry;
            if (ParsePasswordLine(uline, entry))
                _entries.Add(entry);
        }
    }

    return true;
}

bool CPasswordManager::Load()
{
    _entries.Clear();

    // Try primary path first
    if (LoadFromPath(_filePath))
        return true;

    // Try backup path
    if (!_backupPath.IsEmpty() && LoadFromPath(_backupPath))
    {
        Save(); // Restore to primary
        return true;
    }

    return false;
}

bool CPasswordManager::SaveToPath(const FString &path)
{
    NFile::NIO::COutFile file;
    if (!file.Create_ALWAYS(path))
        return false;

    // Build content
    AString content;
    content += "# Password Book\n# Format: Password|||DisplayName\n";

    for (unsigned i = 0; i < _entries.Size(); i++)
    {
        const CPasswordEntry &entry = _entries[i];
        UString line = entry.Password;
        line += L"|||";
        line += entry.Name;

        AString utf8;
        ConvertUnicodeToUTF8(line, utf8);
        content += utf8;
        content += "\r\n";
    }

    // Write magic header
    UInt32 processedSize;
    if (!file.Write(kFileMagic, kMagicLen, processedSize))
        return false;

    // Encrypt and write content
    char *contentBuf = content.GetBuf_SetEnd(content.Len());
    XorCrypt(contentBuf, content.Len());

    if (!file.Write(contentBuf, content.Len(), processedSize))
        return false;

    return processedSize == content.Len();
}

bool CPasswordManager::Save()
{
    bool ok1 = SaveToPath(_filePath);
    bool ok2 = !_backupPath.IsEmpty() ? SaveToPath(_backupPath) : false;
    return ok1 || ok2;
}

bool CPasswordManager::Add(const UString &name, const UString &password)
{
    if (name.IsEmpty() || password.IsEmpty())
        return false;

    // Check for duplicates by name
    for (unsigned i = 0; i < _entries.Size(); i++)
    {
        if (_entries[i].Name == name)
            return false; // Name already exists
    }

    CPasswordEntry entry(name, password);
    _entries.Add(entry);
    return true;
}

bool CPasswordManager::Add(const CPasswordEntry &entry)
{
    if (entry.Password.IsEmpty())
        return false;

    _entries.Add(entry);
    return true;
}

bool CPasswordManager::Remove(unsigned index)
{
    if (index >= _entries.Size())
        return false;

    _entries.Delete(index);
    return true;
}

bool CPasswordManager::Update(unsigned index, const CPasswordEntry &entry)
{
    if (index >= _entries.Size())
        return false;

    _entries[index] = entry;
    return true;
}

bool CPasswordManager::MoveUp(unsigned index)
{
    if (index == 0 || index >= _entries.Size())
        return false;

    CPasswordEntry temp = _entries[index];
    _entries[index] = _entries[index - 1];
    _entries[index - 1] = temp;
    return true;
}

bool CPasswordManager::MoveDown(unsigned index)
{
    if (index >= _entries.Size() - 1)
        return false;

    CPasswordEntry temp = _entries[index];
    _entries[index] = _entries[index + 1];
    _entries[index + 1] = temp;
    return true;
}

bool CPasswordManager::Get(unsigned index, CPasswordEntry &entry) const
{
    if (index >= _entries.Size())
        return false;

    entry = _entries[index];
    return true;
}

bool CPasswordManager::GetPassword(unsigned index, UString &password) const
{
    if (index >= _entries.Size())
        return false;

    password = _entries[index].Password;
    return true;
}

void CPasswordManager::Clear()
{
    _entries.Clear();
}

int CPasswordManager::Import(const FString &importPath)
{
    NFile::NIO::CInFile file;
    if (!file.Open(importPath))
        return -1;

    UInt64 fileSize;
    if (!file.GetLength(fileSize))
        return -1;

    if (fileSize == 0)
        return 0;

    if (fileSize > (1 << 20)) // Limit to 1MB
        return -1;

    AString data;
    char *buf = data.GetBuf_SetEnd((unsigned)fileSize);

    UInt32 processedSize;
    if (!file.Read(buf, (UInt32)fileSize, processedSize))
        return -1;

    if (processedSize != fileSize)
        return -1;

    int importedCount = 0;

    // Check if encrypted (has magic header) and decrypt
    unsigned dataStart = 0;
    if (fileSize > kMagicLen && memcmp(buf, kFileMagic, kMagicLen) == 0)
    {
        dataStart = kMagicLen;
        XorCrypt(buf + dataStart, (unsigned)(fileSize - dataStart));
    }

    // Parse line by line
    AString line;
    unsigned dataLen = (unsigned)fileSize;
    for (unsigned i = dataStart; i < dataLen;)
    {
        char c = buf[i++];
        if (c == '\r')
            continue;
        if (c == '\n')
        {
            if (!line.IsEmpty() && line[0] != '#') // Skip comments
            {
                UString uline;
                ConvertUTF8ToUnicode(line, uline);
                if (!uline.IsEmpty())
                {
                    CPasswordEntry entry;
                    if (ParsePasswordLine(uline, entry))
                    {
                        // Check if password already exists
                        bool exists = false;
                        for (unsigned j = 0; j < _entries.Size(); j++)
                        {
                            if (_entries[j].Password == entry.Password)
                            {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists)
                        {
                            _entries.Add(entry);
                            importedCount++;
                        }
                    }
                }
            }
            line.Empty();
            continue;
        }
        line += c;
    }

    // Handle last line without newline
    if (!line.IsEmpty() && line[0] != '#') // Skip comments
    {
        UString uline;
        ConvertUTF8ToUnicode(line, uline);
        if (!uline.IsEmpty())
        {
            CPasswordEntry entry;
            if (ParsePasswordLine(uline, entry))
            {
                // Check if password already exists
                bool exists = false;
                for (unsigned j = 0; j < _entries.Size(); j++)
                {
                    if (_entries[j].Password == entry.Password)
                    {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                {
                    _entries.Add(entry);
                    importedCount++;
                }
            }
        }
    }

    return importedCount;
}

bool CPasswordManager::Export(const FString &exportPath)
{
    NFile::NIO::COutFile file;
    if (!file.Create_ALWAYS(exportPath))
        return false;

    // Write header comment
    const char *header = "# Password Book\n# Format: Password|||DisplayName\n";
    UInt32 headerLen = (UInt32)strlen(header);
    UInt32 processedSize;
    if (!file.Write(header, headerLen, processedSize))
        return false;

    for (unsigned i = 0; i < _entries.Size(); i++)
    {
        const CPasswordEntry &entry = _entries[i];

        // Format: Password|||DisplayName (same as internal format)
        UString line = entry.Password;
        line += L"|||";
        line += entry.Name;

        AString utf8;
        ConvertUnicodeToUTF8(line, utf8);
        utf8 += "\r\n";

        if (!file.Write((const char *)utf8, utf8.Len(), processedSize))
            return false;
        if (processedSize != utf8.Len())
            return false;
    }

    return true;
}
