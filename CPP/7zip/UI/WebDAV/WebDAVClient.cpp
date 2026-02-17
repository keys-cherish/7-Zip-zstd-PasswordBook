/**
 * @file WebDAVClient.cpp
 * @brief WebDAV 客户端实现
 */

#include "WebDAVClient.h"
#include <sstream>
#include <algorithm>
#include <locale>
#include <codecvt>

namespace NWebDAV {

// ==================== 构造与析构 ====================

CWebDAVClient::CWebDAVClient() 
    : m_hSession(NULL)
    , m_hConnect(NULL)
    , m_connected(false)
    , m_lastStatusCode(0)
    , m_port(INTERNET_DEFAULT_HTTPS_PORT)
    , m_useSSL(true)
{
}

CWebDAVClient::~CWebDAVClient() {
    Disconnect();
}

// ==================== 连接管理 ====================

bool CWebDAVClient::Connect(const ServerConfig& config) {
    // 先断开现有连接
    Disconnect();
    m_config = config;
    
    // 解析 URL
    if (!ParseUrl(config.serverUrl)) {
        SetError(L"无法解析服务器地址: " + config.serverUrl);
        return false;
    }
    
    // 创建 WinHTTP 会话
    m_hSession = WinHttpOpen(
        L"7-Zip-zstd WebDAV Client/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    
    if (!m_hSession) {
        SetError(L"初始化 WinHTTP 失败，错误码: " + std::to_wstring(GetLastError()));
        return false;
    }
    
    // 设置超时
    int timeout = config.timeoutSeconds * 1000;
    WinHttpSetTimeouts(m_hSession, timeout, timeout, timeout, timeout);
    
    // 建立连接
    m_hConnect = WinHttpConnect(m_hSession, m_host.c_str(), m_port, 0);
    if (!m_hConnect) {
        SetError(L"无法连接到服务器: " + m_host);
        WinHttpCloseHandle(m_hSession);
        m_hSession = NULL;
        return false;
    }
    
    m_connected = true;
    return true;
}

void CWebDAVClient::Disconnect() {
    if (m_hConnect) {
        WinHttpCloseHandle(m_hConnect);
        m_hConnect = NULL;
    }
    if (m_hSession) {
        WinHttpCloseHandle(m_hSession);
        m_hSession = NULL;
    }
    m_connected = false;
}

bool CWebDAVClient::TestConnection() {
    if (!m_connected) {
        SetError(L"尚未连接到服务器");
        return false;
    }
    
    // 发送 OPTIONS 请求测试连接
    DWORD statusCode = 0;
    if (SendRequest(L"OPTIONS", L"/", NULL, NULL, 0, NULL, &statusCode)) {
        return true;
    }
    
    // OPTIONS 失败，尝试 PROPFIND 根目录
    std::map<std::wstring, std::wstring> headers;
    headers[L"Depth"] = L"0";
    
    std::string body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                       "<D:propfind xmlns:D=\"DAV:\"><D:prop><D:resourcetype/></D:prop></D:propfind>";
    
    std::vector<BYTE> response;
    if (SendRequest(L"PROPFIND", L"/", &headers, body.c_str(), body.size(), &response, &statusCode)) {
        return true;
    }
    
    // 401 表示服务器可达但认证失败
    if (statusCode == 401) {
        SetError(L"认证失败，请检查用户名和密码");
    }
    
    return false;
}

// ==================== 文件操作 ====================

bool CWebDAVClient::Upload(const std::wstring& localPath, const std::wstring& remotePath) {
    // 打开本地文件
    HANDLE hFile = CreateFileW(
        localPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hFile == INVALID_HANDLE_VALUE) {
        SetError(L"无法打开本地文件: " + localPath);
        return false;
    }
    
    // 获取文件大小
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) {
        CloseHandle(hFile);
        SetError(L"无法获取文件大小: " + localPath);
        return false;
    }
    
    // 读取文件内容
    std::vector<BYTE> fileData((size_t)fileSize.QuadPart);
    DWORD bytesRead = 0;
    
    if (!ReadFile(hFile, fileData.data(), (DWORD)fileSize.QuadPart, &bytesRead, NULL)) {
        CloseHandle(hFile);
        SetError(L"读取文件失败: " + localPath);
        return false;
    }
    CloseHandle(hFile);
    
    // 上传数据
    return UploadData(fileData.data(), fileData.size(), remotePath);
}

bool CWebDAVClient::UploadData(const void* data, size_t size, const std::wstring& remotePath) {
    if (!m_connected) {
        SetError(L"尚未连接到服务器");
        return false;
    }
    
    // 确保父目录存在
    size_t lastSlash = remotePath.rfind(L'/');
    if (lastSlash != std::wstring::npos && lastSlash > 0) {
        std::wstring parentDir = remotePath.substr(0, lastSlash);
        CreateDirectory(parentDir);  // 忽略错误，可能已存在
    }
    
    // 发送 PUT 请求
    DWORD statusCode = 0;
    std::wstring fullPath = BuildFullPath(remotePath);
    
    if (!SendRequest(L"PUT", fullPath, NULL, data, size, NULL, &statusCode)) {
        return false;
    }
    
    // 检查状态码 (200, 201, 204 都表示成功)
    if (statusCode == 200 || statusCode == 201 || statusCode == 204) {
        return true;
    }
    
    SetError(L"上传失败，服务器返回: " + std::to_wstring(statusCode));
    return false;
}

bool CWebDAVClient::Download(const std::wstring& remotePath, const std::wstring& localPath) {
    // 下载到内存
    std::vector<BYTE> data;
    if (!DownloadData(remotePath, data)) {
        return false;
    }
    
    // 写入本地文件
    HANDLE hFile = CreateFileW(
        localPath.c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hFile == INVALID_HANDLE_VALUE) {
        SetError(L"无法创建本地文件: " + localPath);
        return false;
    }
    
    DWORD bytesWritten = 0;
    BOOL result = WriteFile(hFile, data.data(), (DWORD)data.size(), &bytesWritten, NULL);
    CloseHandle(hFile);
    
    if (!result || bytesWritten != data.size()) {
        SetError(L"写入文件失败: " + localPath);
        DeleteFileW(localPath.c_str());
        return false;
    }
    
    return true;
}

bool CWebDAVClient::DownloadData(const std::wstring& remotePath, std::vector<BYTE>& data) {
    if (!m_connected) {
        SetError(L"尚未连接到服务器");
        return false;
    }
    
    data.clear();
    DWORD statusCode = 0;
    std::wstring fullPath = BuildFullPath(remotePath);
    
    if (!SendRequest(L"GET", fullPath, NULL, NULL, 0, &data, &statusCode)) {
        return false;
    }
    
    if (statusCode == 200) {
        return true;
    }
    
    if (statusCode == 404) {
        SetError(L"文件不存在: " + remotePath);
    } else {
        SetError(L"下载失败，服务器返回: " + std::to_wstring(statusCode));
    }
    
    return false;
}

bool CWebDAVClient::Delete(const std::wstring& remotePath) {
    if (!m_connected) {
        SetError(L"尚未连接到服务器");
        return false;
    }
    
    DWORD statusCode = 0;
    std::wstring fullPath = BuildFullPath(remotePath);
    
    if (!SendRequest(L"DELETE", fullPath, NULL, NULL, 0, NULL, &statusCode)) {
        return false;
    }
    
    // 200, 204 表示删除成功，404 表示已不存在（也算成功）
    if (statusCode == 200 || statusCode == 204 || statusCode == 404) {
        return true;
    }
    
    SetError(L"删除失败，服务器返回: " + std::to_wstring(statusCode));
    return false;
}

bool CWebDAVClient::Exists(const std::wstring& remotePath) {
    if (!m_connected) {
        return false;
    }
    
    DWORD statusCode = 0;
    std::wstring fullPath = BuildFullPath(remotePath);
    
    // 使用 HEAD 请求检查文件是否存在
    SendRequest(L"HEAD", fullPath, NULL, NULL, 0, NULL, &statusCode);
    
    return (statusCode == 200);
}

// ==================== 目录操作 ====================

bool CWebDAVClient::CreateDirectory(const std::wstring& remotePath) {
    if (!m_connected) {
        SetError(L"尚未连接到服务器");
        return false;
    }
    
    // 分割路径，逐级创建
    std::vector<std::wstring> parts;
    std::wstring path = remotePath;
    
    // 移除开头的斜杠
    if (!path.empty() && path[0] == L'/') {
        path = path.substr(1);
    }
    
    // 分割路径
    size_t pos = 0;
    while ((pos = path.find(L'/')) != std::wstring::npos) {
        if (pos > 0) {
            parts.push_back(path.substr(0, pos));
        }
        path = path.substr(pos + 1);
    }
    if (!path.empty()) {
        parts.push_back(path);
    }
    
    // 逐级创建目录
    std::wstring currentPath;
    for (const auto& part : parts) {
        currentPath += L"/" + part;
        std::wstring fullPath = BuildFullPath(currentPath);
        
        DWORD statusCode = 0;
        SendRequest(L"MKCOL", fullPath, NULL, NULL, 0, NULL, &statusCode);
        
        // 201 = 创建成功, 405 = 已存在, 301/302 = 重定向（已存在）
        if (statusCode != 201 && statusCode != 405 && statusCode != 301 && statusCode != 302) {
            // 409 = 父目录不存在（不应该发生，因为我们逐级创建）
            if (statusCode == 409) {
                SetError(L"创建目录失败，父目录不存在: " + currentPath);
                return false;
            }
        }
    }
    
    return true;
}

bool CWebDAVClient::ListDirectory(const std::wstring& remotePath, std::vector<RemoteFileInfo>& files) {
    if (!m_connected) {
        SetError(L"尚未连接到服务器");
        return false;
    }
    
    files.clear();
    
    // 构建 PROPFIND 请求
    std::map<std::wstring, std::wstring> headers;
    headers[L"Depth"] = L"1";  // 只获取直接子项
    headers[L"Content-Type"] = L"application/xml; charset=utf-8";
    
    std::string body = 
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<D:propfind xmlns:D=\"DAV:\">\n"
        "  <D:prop>\n"
        "    <D:displayname/>\n"
        "    <D:getcontentlength/>\n"
        "    <D:getlastmodified/>\n"
        "    <D:resourcetype/>\n"
        "  </D:prop>\n"
        "</D:propfind>";
    
    std::vector<BYTE> response;
    DWORD statusCode = 0;
    std::wstring fullPath = BuildFullPath(remotePath);
    
    if (!SendRequest(L"PROPFIND", fullPath, &headers, body.c_str(), body.size(), &response, &statusCode)) {
        return false;
    }
    
    // 207 Multi-Status 表示成功
    if (statusCode != 207) {
        SetError(L"获取目录列表失败，服务器返回: " + std::to_wstring(statusCode));
        return false;
    }
    
    // 解析 XML 响应
    return ParsePropfindResponse(response, files);
}

// ==================== 内部方法 ====================

bool CWebDAVClient::SendRequest(
    const wchar_t* method,
    const std::wstring& path,
    const std::map<std::wstring, std::wstring>* headers,
    const void* data,
    size_t dataSize,
    std::vector<BYTE>* responseData,
    DWORD* outStatusCode
) {
    if (!m_hConnect) {
        SetError(L"未建立连接");
        return false;
    }
    
    // 设置请求标志
    DWORD flags = m_useSSL ? WINHTTP_FLAG_SECURE : 0;
    
    // 创建请求
    HINTERNET hRequest = WinHttpOpenRequest(
        m_hConnect,
        method,
        path.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );
    
    if (!hRequest) {
        SetError(L"创建请求失败，错误码: " + std::to_wstring(GetLastError()));
        return false;
    }
    
    // 设置认证信息
    if (!m_config.username.empty()) {
        WinHttpSetCredentials(
            hRequest,
            WINHTTP_AUTH_TARGET_SERVER,
            WINHTTP_AUTH_SCHEME_BASIC,
            m_config.username.c_str(),
            m_config.password.c_str(),
            NULL
        );
    }
    
    // 添加自定义 Header
    if (headers) {
        for (const auto& h : *headers) {
            std::wstring header = h.first + L": " + h.second;
            WinHttpAddRequestHeaders(hRequest, header.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        }
    }
    
    // 发送请求
    BOOL result = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        const_cast<void*>(data),
        (DWORD)dataSize,
        (DWORD)dataSize,
        0
    );
    
    if (!result) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        SetError(L"发送请求失败，错误码: " + std::to_wstring(err));
        return false;
    }
    
    // 接收响应
    result = WinHttpReceiveResponse(hRequest, NULL);
    if (!result) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        SetError(L"接收响应失败，错误码: " + std::to_wstring(err));
        return false;
    }
    
    // 获取状态码
    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &size,
        WINHTTP_NO_HEADER_INDEX
    );
    
    m_lastStatusCode = statusCode;
    if (outStatusCode) {
        *outStatusCode = statusCode;
    }
    
    // 读取响应数据
    if (responseData) {
        responseData->clear();
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;
            
            std::vector<BYTE> buffer(dwSize);
            if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
            
            responseData->insert(responseData->end(), buffer.begin(), buffer.begin() + dwDownloaded);
        } while (dwSize > 0);
    }
    
    WinHttpCloseHandle(hRequest);
    
    // 检查是否成功 (2xx 状态码)
    return (statusCode >= 200 && statusCode < 300) || statusCode == 207;
}

bool CWebDAVClient::ParseUrl(const std::wstring& url) {
    // 简单的 URL 解析
    std::wstring u = url;
    
    // 检查协议
    m_useSSL = true;
    m_port = INTERNET_DEFAULT_HTTPS_PORT;
    
    if (u.find(L"https://") == 0) {
        u = u.substr(8);
        m_useSSL = true;
        m_port = INTERNET_DEFAULT_HTTPS_PORT;
    } else if (u.find(L"http://") == 0) {
        u = u.substr(7);
        m_useSSL = false;
        m_port = INTERNET_DEFAULT_HTTP_PORT;
    }
    
    // 分离主机和路径
    size_t pathPos = u.find(L'/');
    if (pathPos != std::wstring::npos) {
        m_host = u.substr(0, pathPos);
        m_urlPath = u.substr(pathPos);
    } else {
        m_host = u;
        m_urlPath = L"/";
    }
    
    // 检查是否有端口号
    size_t portPos = m_host.find(L':');
    if (portPos != std::wstring::npos) {
        std::wstring portStr = m_host.substr(portPos + 1);
        m_host = m_host.substr(0, portPos);
        m_port = (INTERNET_PORT)_wtoi(portStr.c_str());
    }
    
    return !m_host.empty();
}

std::wstring CWebDAVClient::BuildFullPath(const std::wstring& relativePath) {
    std::wstring path = m_urlPath;
    
    // 确保 urlPath 以 / 结尾
    if (!path.empty() && path.back() != L'/') {
        path += L'/';
    }
    
    // 添加 basePath
    std::wstring base = m_config.basePath;
    if (!base.empty() && base[0] == L'/') {
        base = base.substr(1);
    }
    path += base;
    
    // 确保以 / 结尾
    if (!path.empty() && path.back() != L'/') {
        path += L'/';
    }
    
    // 添加相对路径
    std::wstring rel = relativePath;
    if (!rel.empty() && rel[0] == L'/') {
        rel = rel.substr(1);
    }
    path += rel;
    
    return path;
}

void CWebDAVClient::SetError(const std::wstring& error, DWORD statusCode) {
    m_lastError = error;
    if (statusCode > 0) {
        m_lastStatusCode = statusCode;
    }
}

bool CWebDAVClient::ParsePropfindResponse(const std::vector<BYTE>& response, std::vector<RemoteFileInfo>& files) {
    // 将响应转换为宽字符串
    std::string utf8Response(response.begin(), response.end());
    
    // 简单的 UTF-8 到 UTF-16 转换
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Response.c_str(), -1, NULL, 0);
    if (wideLen <= 0) return false;
    
    std::wstring xml(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Response.c_str(), -1, &xml[0], wideLen);
    
    // 简单的 XML 解析 - 查找所有 <D:response> 元素
    size_t pos = 0;
    while ((pos = xml.find(L"<D:response", pos)) != std::wstring::npos) {
        size_t endPos = xml.find(L"</D:response>", pos);
        if (endPos == std::wstring::npos) break;
        
        std::wstring responseBlock = xml.substr(pos, endPos - pos);
        
        RemoteFileInfo info;
        
        // 提取 href (路径)
        info.fullPath = ExtractTagContent(responseBlock, L"D:href");
        
        // 提取文件名
        info.name = ExtractTagContent(responseBlock, L"D:displayname");
        if (info.name.empty() && !info.fullPath.empty()) {
            // 从路径中提取文件名
            size_t lastSlash = info.fullPath.rfind(L'/');
            if (lastSlash != std::wstring::npos) {
                info.name = info.fullPath.substr(lastSlash + 1);
            }
        }
        
        // 检查是否为目录
        info.isDirectory = (responseBlock.find(L"<D:collection") != std::wstring::npos);
        
        // 提取文件大小
        std::wstring sizeStr = ExtractTagContent(responseBlock, L"D:getcontentlength");
        if (!sizeStr.empty()) {
            info.size = _wtoi64(sizeStr.c_str());
        }
        
        // 添加到列表（跳过空名称和当前目录）
        if (!info.name.empty() && info.name != L"." && info.name != L"..") {
            files.push_back(info);
        }
        
        pos = endPos;
    }
    
    return true;
}

std::wstring CWebDAVClient::ExtractTagContent(const std::wstring& xml, const std::wstring& tagName) {
    // 查找开始标签
    std::wstring startTag1 = L"<" + tagName + L">";
    std::wstring startTag2 = L"<" + tagName + L" ";  // 带属性的情况
    
    size_t startPos = xml.find(startTag1);
    if (startPos == std::wstring::npos) {
        startPos = xml.find(startTag2);
        if (startPos == std::wstring::npos) return L"";
        // 找到 > 的位置
        startPos = xml.find(L">", startPos);
        if (startPos == std::wstring::npos) return L"";
    } else {
        startPos += startTag1.length() - 1;
    }
    startPos++;
    
    // 查找结束标签
    std::wstring endTag = L"</" + tagName + L">";
    size_t endPos = xml.find(endTag, startPos);
    if (endPos == std::wstring::npos) return L"";
    
    return xml.substr(startPos, endPos - startPos);
}

} // namespace NWebDAV