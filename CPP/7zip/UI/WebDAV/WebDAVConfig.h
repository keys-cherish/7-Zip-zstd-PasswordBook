/**
 * @file WebDAVConfig.h
 * @brief 配置管理类声明
 * @note 配置文件路径: %APPDATA%\7-Zip-zstd\webdav.ini
 */

#ifndef __WEBDAV_CONFIG_H
#define __WEBDAV_CONFIG_H

#include "WebDAVClient.h"

namespace NWebDAV {

/**
 * @brief 预设服务器配置
 */
struct ServerPreset {
    std::wstring name;          // 显示名称
    std::wstring url;           // 服务器 URL
    std::wstring defaultPath;   // 默认路径
    std::wstring helpText;      // 帮助提示
};

/**
 * @brief 配置管理类
 */
class CWebDAVConfig {
public:
    /**
     * @brief 获取配置文件路径
     */
    static std::wstring GetConfigFilePath();
    
    /**
     * @brief 获取配置目录路径
     */
    static std::wstring GetConfigDirPath();
    
    /**
     * @brief 获取密码本默认路径
     */
    static std::wstring GetDefaultPasswordBookPath();
    
    /**
     * @brief 获取书签默认路径
     */
    static std::wstring GetDefaultBookmarksPath();
    
    /**
     * @brief 加载配置
     * @param config 输出配置
     * @return 成功返回 true
     */
    static bool Load(ServerConfig& config);
    
    /**
     * @brief 保存配置
     * @param config 要保存的配置
     * @return 成功返回 true
     */
    static bool Save(const ServerConfig& config);
    
    /**
     * @brief 获取预设服务器列表
     */
    static std::vector<ServerPreset> GetPresets();
    
    /**
     * @brief 加密密码（用于存储）
     */
    static std::wstring EncryptPassword(const std::wstring& password);
    
    /**
     * @brief 解密密码
     */
    static std::wstring DecryptPassword(const std::wstring& encrypted);

private:
    static std::wstring ReadIniString(const std::wstring& section, 
                                      const std::wstring& key, 
                                      const std::wstring& defaultValue);
    static int ReadIniInt(const std::wstring& section, 
                          const std::wstring& key, 
                          int defaultValue);
    static bool WriteIniString(const std::wstring& section, 
                               const std::wstring& key, 
                               const std::wstring& value);
};

} // namespace NWebDAV

#endif // __WEBDAV_CONFIG_H