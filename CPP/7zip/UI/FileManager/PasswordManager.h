// PasswordManager.h

#ifndef ZIP7_INC_PASSWORD_MANAGER_H
#define ZIP7_INC_PASSWORD_MANAGER_H

#include "../../../Common/MyString.h"
#include "../../../Common/MyVector.h"

// Password entry with name and password
struct CPasswordEntry
{
    UString Name;     // Display name/label for the password
    UString Password; // The actual password

    CPasswordEntry() {}
    CPasswordEntry(const UString &name, const UString &password)
        : Name(name), Password(password) {}
};

class CPasswordManager
{
private:
    CObjectVector<CPasswordEntry> _entries;
    FString _filePath;

    void GetPasswordFilePath();

public:
    CPasswordManager();

    // Load passwords from 7z_passwords.txt
    bool Load();

    // Save passwords to 7z_passwords.txt
    bool Save();

    // Get all entries
    const CObjectVector<CPasswordEntry> &GetEntries() const { return _entries; }

    // Add a password entry (avoid duplicates by name)
    bool Add(const UString &name, const UString &password);

    // Add a password entry
    bool Add(const CPasswordEntry &entry);

    // Remove a password by index
    bool Remove(unsigned index);

    // Update a password entry
    bool Update(unsigned index, const CPasswordEntry &entry);

    // Move entry up
    bool MoveUp(unsigned index);

    // Move entry down
    bool MoveDown(unsigned index);

    // Get password entry by index
    bool Get(unsigned index, CPasswordEntry &entry) const;

    // Get password by index (for backward compatibility)
    bool GetPassword(unsigned index, UString &password) const;

    // Get count
    unsigned GetCount() const { return _entries.Size(); }

    // Clear all passwords
    void Clear();

    // Import passwords from a text file (returns count of imported passwords)
    int Import(const FString &importPath);

    // Export passwords to a text file
    bool Export(const FString &exportPath);
};

// Global instance
extern CPasswordManager g_PasswordManager;

// Delimiter for Name|||Password format
static const char *const kPasswordDelimiter = "|||";

#endif
