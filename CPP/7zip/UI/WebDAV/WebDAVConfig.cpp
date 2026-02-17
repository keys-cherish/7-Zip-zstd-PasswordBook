/**
 * @file WebDAVConfig.cpp
 * @brief 配置管理实现
 */

#include "WebDAVConfig.h"
#include <shlobj.h>
#include <wincrypt.h>

#pragma comment(lib, "crypt32.lib")

namespace NWebDAV {

std::wstring CWebDAVConfig::GetConfigDirPath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        std::wstring configDir = std::wstring(path) + L"\\7-Zip-zstd";
        // 确保目录存在
        CreateDirectoryW(configDir.c_str(), NULL);
        return configDir;
    }
    return L"";
}

std::wstring CWebDAVConfig::GetConfigFilePath() {
    return GetConfigDirPath() + L"\\webdav.ini";
}

std::wstring CWebDAVConfig::GetDefaultPasswordBookPath() {
    return GetConfigDirPath() + L"\\passwords.db";
}

std::wstring CWebDAVConfig::GetDefaultBookmarksPath() {
    return GetConfigDirPath() + L"\\bookmarks.xml";
}

bool CWebDAVConfig::Load(ServerConfig& config) {
    std::wstring path = GetConfigFilePath();
    
    // 服务器设置
    config.serverUrl = ReadIniString(L"Server", L"URL", L"");
    config.username = ReadIniString(L"Server", L"Username", L"");
    config.password = DecryptPassword(ReadIniString(L"Server", L"Password", L""));
    config.basePath = ReadIniString(L"Server", L"BasePath", L"/dav/7z-zstd_PB_PS/");
    config.timeoutSeconds = ReadIniInt(L"Server", L"Timeout", 30);
    
    // 备份设置
    config.encryptPassword = DecryptPassword(ReadIniString(L"Backup", L"EncryptPassword", L""));
    config.keepVersions = ReadIniInt(L"Backup", L"KeepVersions", 10);
    
    // 自动备份设置
    config.autoBackup = ReadIniInt(L"AutoBackup", L"Enabled", 0) != 0;
    config.autoBackupDelay = ReadIniInt(L"AutoBackup", L"DelaySeconds", 30);
    
    return true;
}

bool CWebDAVConfig::Save(const ServerConfig& config) {
    // 确保配置目录存在
    GetConfigDirPath();
    
    // 服务器设置
    WriteIniString(L"Server", L"URL", config.serverUrl);
    WriteIniString(L"Server", L"Username", config.username);
    WriteIniString(L"Server", L"Password", EncryptPassword(config.password));
    WriteIniString(L"Server", L"BasePath", config.basePath);
    WriteIniString(L"Server", L"Timeout", std::to_wstring(config.timeoutSeconds));
    
    // 备份设置
    WriteIniString(L"Backup", L"EncryptPassword", EncryptPassword(config.encryptPassword));
    WriteIniString(L"Backup", L"KeepVersions", std::to_wstring(config.keepVersions));
    
    // 自动备份设置
    WriteIniString(L"AutoBackup", L"Enabled", config.autoBackup ? L"1" : L"0");
    WriteIniString(L"AutoBackup", L"DelaySeconds", std::to_wstring(config.autoBackupDelay));
    
    return true;
}

std::vector<ServerPreset> CWebDAVConfig::GetPresets() {
    std::vector<ServerPreset> presets;
    
    // 坚果云
    ServerPreset jianguo;
    jianguo.name = L"坚果云";
    jianguo.url = L"https://dav.jianguoyun.com";
    jianguo.defaultPath = L"/dav/7z-zstd_PB_PS/";
    jianguo.helpText = L"请使用坚果云「应用专用密码」，在坚果云网页版「账户信息」-「安全选项」中生成";
    presets.push_back(jianguo);
    
    // 123网盘
    ServerPreset pan123;
    pan123.name = L"123网盘";
    pan123.url = L"https://dav.123pan.com";
    pan123.defaultPath = L"/7z-zstd_PB_PS/";
    pan123.helpText = L"请在 123 网盘开通 WebDAV 功能后使用";
    presets.push_back(pan123);
    
    // Alist
    ServerPreset alist;
    alist.name = L"Alist";
    alist.url = L"http://localhost:5244";
    alist.defaultPath = L"/dav/7z-zstd_PB_PS/";
    alist.helpText = L"请先部署 Alist 服务并配置存储";
    presets.push_back(alist);
    
    return presets;
}

std::wstring CWebDAVConfig::ReadIniString(
    const std::wstring& section,
    const std::wstring& key,
    const std::wstring& defaultValue
) {
    wchar_t buffer[4096] = {0};
    GetPrivateProfileStringW(
        section.c_str(),
        key.c_str(),
        defaultValue.c_str(),
        buffer,
        sizeof(buffer) / sizeof(wchar_t),
        GetConfigFilePath().c_str()
    );
    return std::wstring(buffer);
}

int CWebDAVConfig::ReadIniInt(
    const std::wstring& section,
    const std::wstring& key,
    int defaultValue
) {
    return GetPrivateProfileIntW(
        section.c_str(),
        key.c_str(),
        defaultValue,
        GetConfigFilePath().c_str()
    );
}

bool CWebDAVConfig::WriteIniString(
    const std::wstring& section,
    const std::wstring& key,
    const std::wstring& value
) {
    return WritePrivateProfileStringW(
        section.c_str(),
        key.c_str(),
        value.c_str(),
        GetConfigFilePath().c_str()
    ) != 0;
}

std::wstring CWebDAVConfig::EncryptPassword(const std::wstring& password) {
    if (password.empty()) return L"";
    
    // 使用 Windows DPAPI 加密
    DATA_BLOB dataIn, dataOut;
    dataIn.pbData = (BYTE*)password.c_str();
    dataIn.cbData = (DWORD)((password.length() + 1) * sizeof(wchar_t));
    
    if (!CryptProtectData(&dataIn, NULL, NULL, NULL, NULL, 0, &dataOut)) {
        return password;  // 加密失败，返回原文
    }
    
    // 转换为 Base64
    DWORD base64Len = 0;
    CryptBinaryToStringW(dataOut.pbData, dataOut.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &base64Len);
    
    std::wstring result(base64Len, 0);
    CryptBinaryToStringW(dataOut.pbData, dataOut.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &result[0], &base64Len);
    
    LocalFree(dataOut.pbData);
    
    // 去除末尾的空字符
    while (!result.empty() && result.back() == 0) {
        result.pop_back();
    }
    
    return result;
}

std::wstring CWebDAVConfig::DecryptPassword(const std::wstring& encrypted) {
    if (encrypted.empty()) return L"";
    
    // 从 Base64 解码
    DWORD binaryLen = 0;
    if (!CryptStringToBinaryW(encrypted.c_str(), 0, CRYPT_STRING_BASE64, NULL, &binaryLen, NULL, NULL)) {
        return encrypted;  // 可能是未加密的旧配置
    }
    
    std::vector<BYTE> binary(binaryLen);
    if (!CryptStringToBinaryW(encrypted.c_str(), 0, CRYPT_STRING_BASE64, binary.data(), &binaryLen, NULL, NULL)) {
        return encrypted;
    }
    
    // 使用 DPAPI 解密
    DATA_BLOB dataIn, dataOut;
    dataIn.pbData = binary.data();
    dataIn.cbData = binaryLen;
    
    if (!CryptUnprotectData(&dataIn, NULL, NULL, NULL, NULL, 0, &dataOut)) {
        return encrypted;  // 解密失败
    }
    
    std::wstring result((wchar_t*)dataOut.pbData);
    LocalFree(dataOut.pbData);
    
    return result;
}

} // namespace NWebDAV