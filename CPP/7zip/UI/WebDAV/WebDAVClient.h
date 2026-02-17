/**
 * @file WebDAVClient.h
 * @brief WebDAV 客户端类声明
 * @note 使用 WinHTTP 实现，无需额外依赖
 */

#ifndef __WEBDAV_CLIENT_H
#define __WEBDAV_CLIENT_H

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <map>

#pragma comment(lib, "winhttp.lib")

namespace NWebDAV {

/**
 * @brief 服务器配置结构
 */
struct ServerConfig {
    std::wstring serverUrl;         // 服务器地址，如: https://dav.jianguoyun.com
    std::wstring username;          // 用户名
    std::wstring password;          // 密码（坚果云需使用应用专用密码）
    std::wstring basePath;          // 基础路径，默认: /dav/7z-zstd_PB_PS/
    std::wstring encryptPassword;   // 备份文件加密密码
    int timeoutSeconds;             // 超时时间（秒）
    bool autoBackup;                // 是否启用自动备份
    int autoBackupDelay;            // 自动备份延迟（秒）
    int keepVersions;               // 保留历史版本数量
    
    ServerConfig() 
        : basePath(L"/dav/7z-zstd_PB_PS/")
        , timeoutSeconds(30)
        , autoBackup(false)
        , autoBackupDelay(30)
        , keepVersions(10) 
    {}
};

/**
 * @brief 远程文件信息
 */
struct RemoteFileInfo {
    std::wstring name;          // 文件名
    std::wstring fullPath;      // 完整路径
    uint64_t size;              // 文件大小
    FILETIME modTime;           // 修改时间
    bool isDirectory;           // 是否为目录
    
    RemoteFileInfo() : size(0), isDirectory(false) {
        modTime.dwLowDateTime = 0;
        modTime.dwHighDateTime = 0;
    }
};

/**
 * @brief WebDAV 客户端类
 * @note 封装所有 WebDAV 操作，线程安全
 */
class CWebDAVClient {
public:
    CWebDAVClient();
    ~CWebDAVClient();

    // ==================== 连接管理 ====================
    
    /**
     * @brief 连接到 WebDAV 服务器
     * @param config 服务器配置
     * @return 成功返回 true
     */
    bool Connect(const ServerConfig& config);
    
    /**
     * @brief 断开连接
     */
    void Disconnect();
    
    /**
     * @brief 检查是否已连接
     */
    bool IsConnected() const { return m_connected; }
    
    /**
     * @brief 测试连接是否有效
     * @return 连接有效返回 true
     */
    bool TestConnection();

    // ==================== 文件操作 ====================
    
    /**
     * @brief 上传本地文件到服务器
     * @param localPath 本地文件路径
     * @param remotePath 远程相对路径（相对于 basePath）
     * @return 成功返回 true
     */
    bool Upload(const std::wstring& localPath, const std::wstring& remotePath);
    
    /**
     * @brief 上传内存数据到服务器
     * @param data 数据指针
     * @param size 数据大小
     * @param remotePath 远程相对路径
     * @return 成功返回 true
     */
    bool UploadData(const void* data, size_t size, const std::wstring& remotePath);
    
    /**
     * @brief 从服务器下载文件到本地
     * @param remotePath 远程相对路径
     * @param localPath 本地文件路径
     * @return 成功返回 true
     */
    bool Download(const std::wstring& remotePath, const std::wstring& localPath);
    
    /**
     * @brief 从服务器下载文件到内存
     * @param remotePath 远程相对路径
     * @param data 输出数据缓冲区
     * @return 成功返回 true
     */
    bool DownloadData(const std::wstring& remotePath, std::vector<BYTE>& data);
    
    /**
     * @brief 删除远程文件或目录
     * @param remotePath 远程相对路径
     * @return 成功返回 true
     */
    bool Delete(const std::wstring& remotePath);
    
    /**
     * @brief 检查远程路径是否存在
     * @param remotePath 远程相对路径
     * @return 存在返回 true
     */
    bool Exists(const std::wstring& remotePath);

    // ==================== 目录操作 ====================
    
    /**
     * @brief 创建远程目录（递归创建）
     * @param remotePath 远程目录路径
     * @return 成功返回 true
     */
    bool CreateDirectory(const std::wstring& remotePath);
    
    /**
     * @brief 列出远程目录内容
     * @param remotePath 远程目录路径
     * @param files 输出文件列表
     * @return 成功返回 true
     */
    bool ListDirectory(const std::wstring& remotePath, std::vector<RemoteFileInfo>& files);

    // ==================== 状态查询 ====================
    
    /**
     * @brief 获取最后一次错误信息
     */
    std::wstring GetLastError() const { return m_lastError; }
    
    /**
     * @brief 获取最后一次 HTTP 状态码
     */
    DWORD GetLastStatusCode() const { return m_lastStatusCode; }
    
    /**
     * @brief 获取基础路径
     */
    std::wstring GetBasePath() const { return m_config.basePath; }
    
    /**
     * @brief 获取当前配置
     */
    const ServerConfig& GetConfig() const { return m_config; }

private:
    HINTERNET m_hSession;           // WinHTTP 会话句柄
    HINTERNET m_hConnect;           // WinHTTP 连接句柄
    ServerConfig m_config;          // 服务器配置
    bool m_connected;               // 连接状态
    std::wstring m_lastError;       // 最后错误信息
    DWORD m_lastStatusCode;         // 最后 HTTP 状态码
    
    // 解析后的 URL 组件
    std::wstring m_host;            // 主机名
    std::wstring m_urlPath;         // URL 路径部分
    INTERNET_PORT m_port;           // 端口
    bool m_useSSL;                  // 是否使用 HTTPS

    // ==================== 内部方法 ====================
    
    /**
     * @brief 发送 HTTP 请求
     */
    bool SendRequest(const wchar_t* method, 
                     const std::wstring& path,
                     const std::map<std::wstring, std::wstring>* headers,
                     const void* data, 
                     size_t dataSize,
                     std::vector<BYTE>* responseData, 
                     DWORD* outStatusCode);
    
    /**
     * @brief 解析 URL
     */
    bool ParseUrl(const std::wstring& url);
    
    /**
     * @brief 构建完整的远程路径
     */
    std::wstring BuildFullPath(const std::wstring& relativePath);
    
    /**
     * @brief 设置错误信息
     */
    void SetError(const std::wstring& error, DWORD statusCode = 0);
    
    /**
     * @brief 解析 PROPFIND XML 响应
     */
    bool ParsePropfindResponse(const std::vector<BYTE>& response, 
                               std::vector<RemoteFileInfo>& files);
    
    /**
     * @brief 从字符串中提取标签内容
     */
    std::wstring ExtractTagContent(const std::wstring& xml, 
                                   const std::wstring& tagName);
};

} // namespace NWebDAV

#endif // __WEBDAV_CLIENT_H