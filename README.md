# 迅捷搜（xunjieso）

> 高性能 Windows 文件搜索引擎 DLL，基于 MFT 解析与并行计算架构

**官网：** [https://www.xunjieso.com](https://www.xunjieso.com)  
**作者：** 龙鹏林（小蜗牛）  
**许可证：** 见 [LICENSE.md](LICENSE)

---

## ✨ 核心特性

| 特性 | 说明 |
|------|------|
| 🚀 极致性能 | 采用并行计算架构，充分利用多核 CPU |
| 🔍 多模式搜索 | 支持通配符（拼音 + 首拼）、正则表达式、SQL 语句等多种查询语法 |
| ⚡ 实时同步 | 文件系统监控，即时感知创建、修改、移动、删除操作 |
| 🔧 自定义扩展 | 可添加自定义字段（文件大小、创建时间、文件评分等） |
| 🔄 异步操作 | 所有核心操作支持异步模式，不阻塞主线程 |
| 📂 快速索引 | 解析 NTFS MFT，实现快速索引全盘文件 |
| 🔒 安全 | SQL 仅支持 SELECT 语句，唯一有写入权限的只有 `xjs_db_Save()` |

## 📊 技术特点

- **线程安全** — 关键操作提供读写锁机制，支持锁升级
- **低内存占用** — 385 万文件仅占用约 **188MB** 内存，索引结构紧凑
- **高效存储** — 采用紧凑数据结构，支持海量文件管理
- **平台支持** — 目前仅支持 **Windows**
- **编码统一** — 全局采用 **UTF-8** 编码
- **权限要求** — 程序必须使用**管理员权限**或以**服务**方式运行

---

## 🛠️ 环境要求

- **操作系统：** Windows（NTFS 文件系统）
- **编译器：** 支持 C/C++ 调用的编译器（MSVC、MinGW 等）
- **架构：** x86（32 位） / x64（64 位）
- **权限：** 管理员权限或服务运行

---

## 📦 快速集成

### 1. 引入头文件

```c
#include "xunjieso.h"
```

### 2. 基本使用流程

```c
// 创建引擎
xjs_engine* engine = xjs_Create(nullptr, nullptr);

// 添加需要的数据库字段（必须在遍历之前调用）
xjs_db_AddField(engine, "文件大小", nullptr);
xjs_db_AddField(engine, "修改时间", nullptr);

// 遍历全盘（异步）
xjs_db_ScanPath(engine, nullptr, TRUE);

// ... 等待遍历完成（可通过回调或轮询 xjs_db_GetEngineState 判断）

// 创建搜索结果对象
xjs_result* result = xjs_result_Create(engine);

// 执行搜索（通配符模式）
int fingerprint = xjs_result_Query(result, "*.dll", 0, TRUE);

// 获取结果数量
int count = xjs_result_GetCount(result);

// 遍历结果
for (int i = 0; i < count; i++) {
    int fileId = xjs_result_GetFileId(result, i);
    const char* path = xjs_db_GetPath(engine, fileId);
    const char* name = xjs_db_GetName(engine, fileId);
    // ...
}

// 销毁搜索结果
xjs_result_Destroy(result);

// 销毁引擎（内部会自动销毁所有未释放的 xjs_result*）
xjs_Destroy(engine);
```

### 3. 调用约定

| 平台 | 调用约定 |
|------|---------|
| x64 | 默认（无需指定） |
| x86 | `__stdcall` |

导出宏已通过 `XJS_API` 封装，调用约定已通过 `XJS_CALL` 封装，正常使用即可。

---

## 📖 API 概览

### 辅助函数

| 函数 | 说明 |
|------|------|
| `xjs_util_FormatFileSize` | 格式化文件大小（返回 `"5.2 GB"`） |
| `xjs_util_FormatTimestamp` | 格式化时间戳（返回 `"2023-10-24 15:30:00"`） |

### 异常捕获

| 函数 | 说明 |
|------|------|
| `xjs_EnableException` | 启用异常捕获 |
| `xjs_DisableException` | 禁用异常捕获 |

### 引擎 API

| 函数 | 说明 |
|------|------|
| `xjs_Create` | 创建引擎实例 |
| `xjs_Destroy` | 销毁引擎实例 |
| `xjs_SetDefaultEngine` | 设置默认引擎句柄 |
| `xjs_GetDefaultEngine` | 获取默认引擎句柄 |
| `xjs_GetVersion` | 获取版本号（如 `"1.2.0.1"`） |
| `xjs_GetLastError` | 获取最近错误码 |
| `xjs_GetLastErrorMsg` | 获取最近错误文本 |
| `xjs_SetCallback` | 设置回调事件 |
| `xjs_Lock` / `xjs_Unlock` | 读写锁操作 |
| `xjs_IsReadLock` / `xjs_IsWriteLock` | 查询锁状态 |

### 数据库 API

| 函数 | 说明 |
|------|------|
| `xjs_db_AddField` | 添加自定义字段 |
| `xjs_db_Load` | 加载数据库 |
| `xjs_db_Save` | 保存数据库 |
| `xjs_db_Clear` | 清空数据库 |
| `xjs_db_ScanPath` | 遍历分区 / 路径 |
| `xjs_db_StopScan` | 停止遍历 |
| `xjs_db_GetScanProgress` | 获取遍历进度 |
| `xjs_db_GetEngineState` | 获取引擎状态 |
| `xjs_db_GetFileCount` | 获取文件总数 |
| `xjs_db_GetPath` | 获取文件路径 |
| `xjs_db_GetName` | 获取文件名 |
| `xjs_db_GetFileSize` | 获取文件大小 |
| `xjs_db_GetModifyTime` | 获取修改时间 |
| `xjs_db_GetChildrenIds` | 获取子项 ID |
| `xjs_db_GetFileIdByPath` | 通过路径获取文件 ID |
| ... | 更多字段访问函数见头文件 |

### 同步 API

| 函数 | 说明 |
|------|------|
| `xjs_sync_AddPath` | 添加监控路径 |
| `xjs_sync_RemovePath` | 移除监控路径 |
| `xjs_sync_Start` | 开始监控 |
| `xjs_sync_Pause` | 暂停同步 |
| `xjs_sync_AllStop` | 全部停止监视 |
| `xjs_sync_GetPendingCount` | 获取积压待处理数量 |

### 搜索结果 API

| 函数 | 说明 |
|------|------|
| `xjs_result_Create` | 创建搜索结果对象 |
| `xjs_result_Destroy` | 销毁搜索结果对象 |
| `xjs_result_Query` | 执行搜索 |
| `xjs_result_Cancel` | 停止搜索 |
| `xjs_result_GetCount` | 获取结果数量 |
| `xjs_result_GetFileId` | 获取指定位置的文件 ID |
| `xjs_result_CopyFileIdsByRange` | 按范围复制文件 ID（适配虚拟列表） |
| `xjs_result_SetSortField` | 设置排序字段 |
| `xjs_result_SetSelectedFilter` | 设置筛选分类 |
| `xjs_result_GetFileIco` | 获取文件图标（PNG） |
| `xjs_result_GetMatchKeywords` | 获取匹配关键词（用于高亮） |
| `xjs_result_SetCallback` | 设置搜索结果回调 |

---

## 📋 数据库表结构

```sql
CREATE TABLE alltable (
    Path        TEXT,      -- 完整文件路径
    FName       TEXT,      -- 文件名
    Ext         TEXT,      -- 扩展名
    ParentName  TEXT,      -- 直接父目录名称
    ParentPath  TEXT,      -- 直接父目录路径
    AnyParent   TEXT,      -- 任意一级父目录名称
    Size        TEXT,      -- 文件大小（需开启）
    ModTime     DATETIME,  -- 修改时间（需开启）
    FileType    TEXT,      -- 文件类型分类
    IsDir       INTEGER,   -- 是否为目录
    Alias       TEXT,      -- 别名（需开启）
    Content     BLOB       -- 文件内容（虚拟字段，用于内容搜索，用到时才读取文件）
);
```

> 部分字段需要通过 `xjs_db_AddField()` 手动添加开启。

---

## 🔔 回调事件一览

### 引擎级回调（`xjs_SetCallback`）

| 事件类型 | 说明 |
|---------|------|
| `1` | 正在加载数据库 |
| `2` | 数据库加载完成 |
| `3` | 正在枚举某分区 |
| `4` | 枚举进度（每 50ms 触发） |
| `5` | 所有盘符枚举完成 |
| `10` | 同步 — 文件创建 |
| `11` | 同步 — 文件修改 |
| `12` | 同步 — 文件移动 |
| `13` | 同步 — 文件删除 |
| `20` | 搜索结果已创建 |
| `21` | 搜索结果即将销毁 |

### 搜索结果回调（`xjs_result_SetCallback`）

| 事件类型 | 说明 |
|---------|------|
| `1` | 即将搜索（返回非 0 可拦截） |
| `2` | 搜索过程（返回 0 继续 / -1 停止并丢弃 / 1 停止并保留） |
| `3` | 搜索等待 |
| `4` | 搜索完成 |
| `10` | 搜索结果变化 |
| `11` | 图标绘制事件 |

---

## ⚠️ 注意事项

1. **请勿在任何回调事件中对数据库进行写操作。**
2. 所有返回 `const char*` / `const void*` 的函数**不需要手动释放内存**，内部自动管理。
3. 所有返回的数据指针，请**第一时间获取或拷贝**，因为后续操作可能使其失效。
4. 读写锁（`xjs_Lock`）是一个高级锁，支持锁升级。如果没有完全理解读写锁，**请不要使用**。
5. 通常来说只允许读，除非需要完整的同步才需要考虑使用锁。
6. 暂停同步后文件变化信息会保留在内存中，如不打算恢复，应使用 `xjs_sync_RemovePath` + `xjs_sync_Start` + `xjs_sync_Stop` 组合。

---

## 📄 许可证

本项目的许可证详见 [LICENSE](LICENSE) 文件。包含的第三方组件声明如下：

| 组件 | 许可证 |
|------|--------|
| [PCRE2](https://www.pcre.org/) | BSD-3-Clause WITH PCRE2-exception |
| [oneTBB](https://github.com/oneapi-src/oneTBB) | Apache License 2.0 |
| [Hyrise SQL Parser](https://github.com/hyrise/sql-parser) | MIT License |

---

## 📬 联系方式

| 渠道 | 信息 |
|------|------|
| QQ | 11345429 |
| 邮箱 | 11345429@qq.com |
| 官网 | [https://www.xunjieso.com](https://www.xunjieso.com) |
