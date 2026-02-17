// CPP/7zip/UI/WebDAV/WebDAVBackup.h
#ifndef WEBDAV_BACKUP_H
#define WEBDAV_BACKUP_H

#include "WebDAVClient.h"
#include <map>
#include <ctime>

namespace N7Zip {
namespace WebDAV {

struct FileRecord {
    std::wstring path;
    std::wstring hash;      // MD5
    time_t modTime;
    uint64_t size;
};

struct BackupManifest {
    time_t lastFullBackup;
    time_t lastBackup;
    std::map<std::wstring, FileRecord> files;
};

class WebDAVBackup {
public:
    WebDAVBackup(WebDAVClient& client);

    // 设置备份参数
    void SetSourceDir(const std::wstring& dir);
    void SetCompressionMethod(const std::wstring& method); // zstd, lzma2
    void SetCompressionLevel(int level);
    void SetPassword(const std::wstring& password);
    void SetFullBackupInterval(int days);
    void AddExcludePattern(const std::wstring& pattern);

    // 执行备份
    bool Backup();           // 自动判断全量/增量
    bool FullBackup();
    bool IncrementalBackup();

    // 恢复
    bool Restore(const std::wstring& targetDir, time_t targetTime = 0);

    // 状态
    int GetChangedFileCount() const;
    std::wstring GetLastBackupFile() const;

private:
    WebDAVClient& m_client;
    std::wstring m_sourceDir;
    std::wstring m_method;
    int m_level;
    std::wstring m_password;
    int m_fullBackupDays;
    std::vector<std::wstring> m_excludePatterns;
    BackupManifest m_manifest;

    bool LoadManifest();
    bool SaveManifest();
    std::vector<std::wstring> FindChangedFiles();
    std::wstring CalculateMD5(const std::wstring& filePath);
    bool CompressAndUpload(const std::vector<std::wstring>& files, 
                           const std::wstring& archiveName);
};

} // namespace WebDAV
} // namespace N7Zip

#endif