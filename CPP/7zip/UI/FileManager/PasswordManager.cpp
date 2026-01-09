// PasswordManager.cpp

#include "StdAfx.h"

#include "../../../Common/UTFConvert.h"
#include "../../../Windows/DLL.h"
#include "../../../Windows/FileIO.h"

#include "PasswordManager.h"

using namespace NWindows;

// Global instance
CPasswordManager g_PasswordManager;

static const char *const kPasswordFileName = "password_book.dat";

CPasswordManager::CPasswordManager()
{
    GetPasswordFilePath();
}

void CPasswordManager::GetPasswordFilePath()
{
    _filePath = NDLL::GetModuleDirPrefix();
    _filePath += kPasswordFileName;
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

bool CPasswordManager::Load()
{
    _entries.Clear();

    NFile::NIO::CInFile file;
    if (!file.Open(_filePath))
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

    // Parse line by line
    AString line;
    for (unsigned i = 0; i < data.Len();)
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

bool CPasswordManager::Save()
{
    NFile::NIO::COutFile file;
    if (!file.Create_ALWAYS(_filePath))
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

        // Format: Password|||DisplayName
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

    // Parse line by line
    AString line;
    for (unsigned i = 0; i < data.Len();)
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
