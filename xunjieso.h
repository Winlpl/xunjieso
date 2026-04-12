/*
 * xunjieso.h
 * 迅捷搜 API 原生 C/C++ 头文件
 * 说明：文本编码统一采用 UTF-8
 * 程序必须使用管理员权限或者服务运行
 *
 * === 核心特性 ===
 * 1. 极致性能：采用并行计算架构
 * 2. 多模式搜索：支持通配符(拼音+首拼)、正则表达式、SQL语句等多种查询语法
 * 3. 实时同步：文件系统监控，即时感知创建、修改、移动、删除操作
 * 4. 自定义扩展：可添加自定义字段（如文件大小、创建时间、文件评分等）
 * 5. 异步操作：所有核心操作支持异步模式，不阻塞主线程
 * 6. 快速索引：软件采用解析MFT，实现快速索引全盘文件
 * 7. 安全：SQL语句仅支持SELECT语句，唯一有写入权限的只有 xjs_db_Save()
 *
 * === 注意事项 ===
 * 1.请勿在任何回调事件中, 对数据库进行写操作.
 
 * === 技术特点 ===
 * - 线程安全：关键操作提供加锁机制
 * - 低内存占用：385 万文件仅占用约 188MB 内存，索引结构紧凑
 * - 高效存储：采用紧凑数据结构，支持海量文件管理
 * - 目前仅支持 Windows
 *
 * 所有返回 const char* 的函数不需要自己释放内存，内部会自动管理内存。
 * 需要注意的是，所有返回的数据指针，请第一时间获取或拷贝。
 * 版本：见 xjs_GetVersion()
 * 官网：https://www.xunjieso.com
 * 表结构: (需要注意的是, 部分需要自己添加)
    CREATE TABLE alltable (
        Path        TEXT,      -- 完整文件路径
        FName       TEXT,      -- 文件名
        Ext         TEXT,      -- 扩展名
        ParentName  TEXT,      -- 直接父目录名称
        ParentPath  TEXT,      -- 直接父目录路径
        AnyParent   TEXT,      -- 任意一级父目录名称
        Size        TEXT,      -- 文件大小(需开启才有)
        ModTime     DATETIME,  -- 修改时间(需开启才有)
        FileType    TEXT,      -- 文件类型分类
        IsDir       INTEGER,   -- 是否为目录
        Alias       TEXT,      -- 别名(需开启才有)
        Content     BLOB       -- 文件内容（用于内容搜索）虚拟字段, 用到才会读取文件.
    );
*/

#pragma once
#include <windef.h> // BOOL

// ============================================================================
// 1. 定义调用约定
// ============================================================================
#if defined(_WIN64) || defined(__x86_64__)
    // 64位下只有一种调用约定，无需指定
    #define XJS_CALL 
#else
    // 32位
    #define XJS_CALL __stdcall
#endif

// ============================================================================
// 2. 定义导出宏 (注意：这里不要包含 XJS_CALL)
// ============================================================================
#ifdef XJS_EXPORTS
    #define XJS_API __declspec(dllexport)
#else
    #define XJS_API __declspec(dllimport)
#endif


#ifdef __cplusplus
extern "C" {
#endif

typedef struct xjs_engine xjs_engine;
typedef struct xjs_result xjs_result;
// ============================================================================
// 辅助函数
// ============================================================================
// 格式化文件大小(返回"5.2 GB")
XJS_API const char* XJS_CALL xjs_util_FormatFileSize(long long size);


// 格式化时间(返回:"2023-10-24 15:30:00")
XJS_API const char* XJS_CALL xjs_util_FormatTimestamp(long long msTimestamp);

// ============================================================================
// 异常捕获
// ============================================================================
// 启用异常捕获
XJS_API void XJS_CALL xjs_EnableException(BOOL enableTry);

// 禁用异常捕获
XJS_API void XJS_CALL xjs_DisableException();

// ============================================================================
// 引擎 API
// ============================================================================

// 创建引擎，返回文件引擎句柄
XJS_API xjs_engine* XJS_CALL xjs_Create(const char* aliasJson, const char* filterJson);

// 销毁引擎 (内部会自动销毁所有未释放的 xjs_result*)
XJS_API void XJS_CALL xjs_Destroy(xjs_engine* engine);

// 设置用户设定的默认文件引擎句柄
XJS_API void XJS_CALL xjs_SetDefaultEngine(xjs_engine* engine);

// 获取用户设定的默认文件引擎句柄
XJS_API xjs_engine* XJS_CALL xjs_GetDefaultEngine(void);

// 获取版本号，例如返回 "1.2.0.1"
XJS_API const char* XJS_CALL xjs_GetVersion(void);

// 获取最近的操作错误码
XJS_API int XJS_CALL xjs_GetLastError(xjs_engine* engine);

// 获取最近的操作错误文本
XJS_API const char* XJS_CALL xjs_GetLastErrorMsg(xjs_engine* engine);

// 设置回调
XJS_API BOOL XJS_CALL xjs_SetCallback(
    xjs_engine* engine, 
    int eventType, 
    const void* callback, /*  
                            1: 正在加载数据库
                                typedef void (*LoadStartCallback)(void* userData, xjs_engine* engine);
                            2: 数据库加载完成
                                typedef void (*LoadCompleteCallback)(void* userData, xjs_engine* engine, int fileCount);
                            3: 正在枚举某分区
                                typedef void (*EnumPartitionCallback)(void* userData, xjs_engine* engine, const char* driveLetter);
                            4: 枚举进度
                                typedef BOOL (*EnumProgressCallback)(void* userData, xjs_engine* engine, const char* driveLetter, int enumeratedCount, int totalCount); // 在遍历时，每隔50毫秒触发一次
                            5: 所有盘符枚举完成
                                typedef void (*EnumCompleteCallback)(void* userData, xjs_engine* engine, int elapsedMs);
                            10: 同步_文件创建
                                typedef BOOL (*SyncFileCreateCallback)(void* userData, xjs_engine* engine, const char* filePath);
                            11: 同步_文件修改
                                typedef BOOL (*SyncFileModifyCallback)(void* userData, xjs_engine* engine, const char* filePath);
                            12: 同步_文件移动
                                typedef BOOL (*SyncFileMoveCallback)(void* userData, xjs_engine* engine, const char* srcPath, const char* destPath);
                            13: 同步_文件删除
                                typedef BOOL (*SyncFileDeleteCallback)(void* userData, xjs_engine* engine, const char* filePath);
                            20: 搜索结果_已创建 (调用 xjs_result_Create() 时内部触发)
                                typedef void (*ResultCreateCallback)(void* userData, xjs_engine* engine, xjs_result* result);
                            21: 搜索结果_即将销毁 (调用 xjs_result_Destroy 后进入销毁队列，由内部线程排队销毁)
                                typedef void (*ResultDeleteCallback)(void* userData, xjs_engine* engine, xjs_result* result);
        */
    void* userData
);

// 获取用户设定的值，运行时的值，不会保存到数据库中
XJS_API void* XJS_CALL xjs_GetUserValue(xjs_engine* engine);

// 设置用户设定的值，运行时的值，不会保存到数据库中(非线程安全)
XJS_API void XJS_CALL xjs_SetUserValue(xjs_engine* engine, void* userData);

/* 
    锁类。通常是不需要锁的，除非需要完整的同步才需要考虑使用锁。通常来说只允许读。
    它是一个高级锁，支持锁升级：读过程可以进入写，写过程可以进入读，但解锁时请务必配对解锁。
    如果您没有完全理解读写锁，请不要使用。
*/

// 判断当前线程是否再读锁内(如果在写锁内, 也返回TRUE)
XJS_API BOOL XJS_CALL xjs_IsReadLock(xjs_engine* engine);


// 判断当前线程是否再写锁内(如果在读锁内, 也返回FALSE)
XJS_API BOOL XJS_CALL xjs_IsWriteLock(xjs_engine* engine);


// 加锁
XJS_API BOOL XJS_CALL xjs_Lock(xjs_engine* engine, BOOL isReadOnly);

// 解锁
XJS_API BOOL XJS_CALL xjs_Unlock(xjs_engine* engine, BOOL isReadOnly);

// 判断一个搜索结果对象, 是否在文件引擎中(判断`xjs_result*`是否有效)
XJS_API BOOL XJS_CALL xjs_ResultIsExist(xjs_engine* engine, xjs_result* result);

// ============================================================================
// 数据库 API
// ============================================================================

// 添加数据库字段 (需要注意的是，必须在遍历之前进行添加)
XJS_API BOOL XJS_CALL xjs_db_AddField(
    xjs_engine* engine, 
    const char* fieldName, // 字段名: 文件大小 | 修改时间 | 文件评分 | 别名 | 文件属性 | 创建时间 | 访问时间
    const char* fieldType  // 保留参数，目前自动忽略该参数，传递任何值都没用
);

// 获取数据库内存占用大小 (字节)
XJS_API long long XJS_CALL xjs_db_GetMemorySize(xjs_engine* engine);

// 加载数据库
XJS_API BOOL XJS_CALL xjs_db_Load(xjs_engine* engine, const char* path, BOOL async);

// 保存数据库
XJS_API BOOL XJS_CALL xjs_db_Save(xjs_engine* engine, const char* path);

// 清空数据库
XJS_API BOOL XJS_CALL xjs_db_Clear(xjs_engine* engine);

// 遍历分区
XJS_API BOOL XJS_CALL xjs_db_ScanPath(
    xjs_engine* engine, 
    const char* path, // 如果 path 为 nullptr，那么就会遍历电脑上所有可用的磁盘分区
    BOOL async
);

// 停止当前的遍历/扫描操作 (仅是通知扫描线程尽快退出，不会等待扫描线程结束。如果不在遍历过程中，则返回 false)
XJS_API BOOL XJS_CALL xjs_db_StopScan(xjs_engine* engine);

// 取遍历进度 - 只有遍历 NTFS 整个分区时可用。如果不在遍历，将会返回 -1
XJS_API double XJS_CALL xjs_db_GetScanProgress(xjs_engine* engine);

/* 取当前数据库状态:
    0 = 空闲
    1 = 正在加载数据库
    2 = 正在保存数据库
    3 = 正在扫描磁盘(建立索引)
    4 = 正在同步文件变化
    5 = 正在搜索
*/  
XJS_API int XJS_CALL xjs_db_GetEngineState(xjs_engine* engine);

// 取文件总数 (包含文件夹、驱动器等)
XJS_API int XJS_CALL xjs_db_GetFileCount(xjs_engine* engine);

// 复制所有文件ID (返回已复制的文件ID数量)
XJS_API int XJS_CALL xjs_db_CopyAllFileId(xjs_engine* engine, int* idArray, int bufferCount);

// 判断文件ID是否为文件夹(驱动器)
XJS_API BOOL XJS_CALL xjs_db_IsDir(xjs_engine* engine, int fileId);

// 判断文件ID是否有效.
XJS_API BOOL XJS_CALL xjs_db_IsFileIdValid(xjs_engine* engine, int fileId);

// 取文件大小
XJS_API long long XJS_CALL xjs_db_GetFileSize(xjs_engine* engine, int fileId);

// 取文件修改时间 (毫秒时间戳)
XJS_API long long XJS_CALL xjs_db_GetModifyTime(xjs_engine* engine, int fileId);

// 取文件名
XJS_API const char* XJS_CALL xjs_db_GetName(xjs_engine* engine, int fileId);

// 取父目录
XJS_API const char* XJS_CALL xjs_db_GetParentDirectory(xjs_engine* engine, int fileId);

// 取文件创建时间 (需要添加字段: "CreateTime")
XJS_API long long XJS_CALL xjs_db_GetCreateTime(xjs_engine* engine, int fileId);

// 取文件访问时间 (需要添加字段: "AccessTime")
XJS_API long long XJS_CALL xjs_db_GetAccessTime(xjs_engine* engine, int fileId);

// 取文件评分 (需要添加字段: "SmartSort")
XJS_API short XJS_CALL xjs_db_GetRating(xjs_engine* engine, int fileId);

// 增加文件评分 (需要添加字段: "SmartSort"), 负数为扣分.返回计算后的分数.如果没有写入权限, 会自动申请写入权限.
XJS_API short XJS_CALL xjs_db_AllRating(xjs_engine* engine, int fileId, short Rating);

// 取文件别名 (需要添加字段: "Alias")
XJS_API const char* XJS_CALL xjs_db_GetAlias(xjs_engine* engine, int fileId);

// 取文件属性 (需要添加字段: "FileAttributes")
XJS_API int XJS_CALL xjs_db_GetFileAttributes(xjs_engine* engine, int fileId);

// 置文件别名 (需要添加字段: "Alias")
XJS_API BOOL XJS_CALL xjs_db_SetAlias(
    xjs_engine* engine,
    int fileId, 
    const char* alias // 如果 alias 为 nullptr，那么则是删除当前文件的别名
);

// 取文件类型
XJS_API unsigned char XJS_CALL xjs_db_GetFileType(xjs_engine* engine, int fileId);

// 取文件类型字符串
XJS_API const char* XJS_CALL xjs_db_GetFileTypeStr(xjs_engine* engine, int fileId);

// 取文件扩展名 (如果是目录, 或者文件没有扩展名, 将会返回空字符串)
XJS_API const char* XJS_CALL xjs_db_GetFileExt(xjs_engine* engine, int fileId);

// 取文件路径
XJS_API const char* XJS_CALL xjs_db_GetPath(xjs_engine* engine, int fileId);

// 获取某个文件夹下的子项ID (返回实际复制的ID数量)
XJS_API int XJS_CALL xjs_db_GetChildrenIds(xjs_engine* engine, int folderId, BOOL recursive, int* buffer, int bufferSize);

// 取根目录ID
XJS_API int XJS_CALL xjs_db_GetRootDirectoryId(xjs_engine* engine, int fileId);

// 获取父目录ID (如果返回 -1，代表没有父目录了)
XJS_API int XJS_CALL xjs_db_GetParentDirectoryId(xjs_engine* engine, int fileId);

// 取文件路径ID (如果返回 -1，代表数据库中不存在这个路径。需要注意的是，它区分大小写)
XJS_API int XJS_CALL xjs_db_GetFileIdByPath(xjs_engine* engine, const char* path);


// ============================================================================
// 同步 API
// ============================================================================

/** @brief 同步_添加监控路径 (返回非0代表成功) */
XJS_API BOOL XJS_CALL xjs_sync_AddPath(xjs_engine* engine, char driveLetter);

/** @brief 同步_移除监控路径 */
XJS_API BOOL XJS_CALL xjs_sync_RemovePath(xjs_engine* engine, char driveLetter);

// 同步_开始监控
// 通常不需要调用。遍历完分区后，程序会自动添加同步，除非您调用过 xjs_sync_RemovePath 或 xjs_sync_Stop
XJS_API BOOL XJS_CALL xjs_sync_Start(xjs_engine* engine);

// 同步_暂停同步 
// 需要注意的是，暂停同步后，所有文件的变化信息会保留在内存中。如果不打算恢复同步，不应该使用此方法，
// 而应该使用：xjs_sync_RemovePath || xjs_sync_Start || xjs_sync_Stop
XJS_API void XJS_CALL xjs_sync_Pause(xjs_engine* engine, BOOL pause); 

// 全部停止监视
XJS_API void XJS_CALL xjs_sync_AllStop(xjs_engine* engine, BOOL waitQueueComplete); 

// 获取当前同步队列中积压的待处理文件数量 (用于判断磁盘高负载是否结束)
XJS_API int XJS_CALL xjs_sync_GetPendingCount(xjs_engine* engine);

// ============================================================================
// 搜索结果 API
// ============================================================================

// 创建搜索结果对象
XJS_API xjs_result* XJS_CALL xjs_result_Create(xjs_engine* engine);

// 搜索结果_设置回调 (非线程安全)   
XJS_API BOOL XJS_CALL xjs_result_SetCallback(
    xjs_result* result,
    int eventType, /* 1: 即将搜索，返回非0拦截
                        typedef int (*SearchBeforeCallback)(void* userData, xjs_engine* engine, xjs_result* result, int searchFingerprint, const char* keyword);

                    2: 搜索过程函数，返回值: 0=继续搜索, -1=停止搜索(抛弃所有), 1=停止搜索(保留已搜索到的结果)
                        typedef int (*SearchProcessCallback)(void* userData, xjs_engine* engine, xjs_result* result, const char* keyword);

                    3: 搜索等待，返回值: 0=继续等待, -1=停止等待(会继续搜索)
                        typedef int (*SearchWaitCallback)(void* userData, xjs_engine* engine, xjs_result* result, int elapsedMs);

                    4: 搜索完成，异步线程触发
                        typedef int (*SearchCompleteCallback)(void* userData, xjs_engine* engine, xjs_result* result, int searchFingerprint, const char* keyword, BOOL discarded);

                    10: 搜索结果变化，异步线程触发
                        typedef int (*SearchChangeCallback)(void* userData, xjs_engine* engine, xjs_result* result, BOOL resetCount);

                    11: 图标绘制事件，异步线程触发
                        typedef void (*SearchDrawIconCallback)(void* userData, xjs_engine* engine, xjs_result* result, int searchFingerprint, int id, int itemIndex, const void* iconData, int iconLength);
            */
    const void* callback, 
    const void* userData
);

// 销毁搜索结果对象
XJS_API BOOL XJS_CALL xjs_result_Destroy(xjs_result* result);

// 搜索结果对象是否有效(如果在销毁队列中, 将会返回FALSE)
XJS_API BOOL XJS_CALL xjs_result_IsEffective(xjs_result* result);


// 搜索 (返回本次的搜索指纹)
// 如果正在遍历磁盘，将会返回 -1
// 如果当前已经在搜索中，那么将会抛弃正在搜索的任务
XJS_API int XJS_CALL xjs_result_Query(
    xjs_result* result, 
    const char* keyword, // 搜索词：支持SQL语句。如果写的是SQL语句，那么会自动忽略 keywordType
    int keywordType,     // 搜索词类型：0=通配符, 1=正则
    BOOL waitComplete    // 如果等待，通常不能为主线程
);

// 取搜索指纹(获取最新的搜索指纹, 可用于判断`xjs_result_Query()`上次的搜索是否被覆盖了.)
XJS_API int XJS_CALL xjs_result_GetFingerprint(xjs_result* result);


// 取当前搜索词
XJS_API const void* XJS_CALL xjs_result_GetKeyword(xjs_result* result);


// 停止搜索 (搜索指纹是 xjs_result_Query() 的返回值)
XJS_API BOOL XJS_CALL xjs_result_Cancel(xjs_result* result, int searchFingerprint, BOOL waitStop);

// 获取文件图标 (如果图标是第一次获取，那么会返回一个临时图标，并触发"图标绘制事件"。同样，返回的数据指针不需要手动释放，内部管理)
// 内部提供了图标缓存
// 返回 PNG 格式。SDK 内部已经在取图标时做了 CoInitializeEx 处理
XJS_API const void* XJS_CALL xjs_result_GetFileIco(
    xjs_result* result, 
    int fileId, 
    int itemIndex,      // 从0开始。如果提供 -1 代表同步获取，不会触发回调事件
    int iconSize,       // 16 或 32
    int* outIconLength
);

// 判断搜索是否完成
XJS_API BOOL XJS_CALL xjs_result_IsCompleted(
    xjs_result* result, 
    int searchFingerprint // 搜索指纹为 xjs_result_Query() 的返回值
);

// 取迅捷搜引擎 (也就是父对象的句柄)
XJS_API xjs_engine* XJS_CALL xjs_result_GetXjsEngine(xjs_result* result);

// 取搜索耗时，单位为毫秒。如果返回 -1，则表示正在搜索
XJS_API int XJS_CALL xjs_result_GetElapsed(xjs_result* result);

// 取匹配关键词 (比如搜 *.txt，需要高亮 .txt) 返回 JSON: [".txt"]
//  文件名:"xunjieso1.0.dll"
//   正则:"[0-9]+" 返回:["1", "0"]
//   通配符:"*.txt" 返回:[".txt"]
XJS_API const char* XJS_CALL xjs_result_GetMatchKeywords(xjs_result* result, const char* textToHighlight);

// 取结果数量
XJS_API int XJS_CALL xjs_result_GetCount(xjs_result* result);

// 取文件ID (返回 -1 代表失败)
XJS_API int XJS_CALL xjs_result_GetFileId(xjs_result* result, int index);

// 复制所有文件ID (返回已复制的文件ID数量)
XJS_API int XJS_CALL xjs_result_CopyAllFileId(
    xjs_result* result, 
    int* idArray,       // 事先分配好内存
    int bufferCount     // 缓冲区成员数
);

// 按范围复制文件ID (完美适配 UI 虚拟列表/分页控件)
// 返回值：实际拷贝的数量
XJS_API int XJS_CALL xjs_result_CopyFileIdsByRange(
    xjs_result* result, 
    int startIndex,     // 从第几个开始取 (比如下拉框滚到第 500 条，就传 500)
    int* buffer,        // 接收数据的缓冲区 (只需要分配 50 * 4 = 200 字节即可)
    int bufferCount     // 要读取多少个，就写多少个
);

// 搜索结果排序 (需要注意的是，排序后不会触发任何事件，也不会改变搜索结果。如果需要结果产生变化，需要重新进行搜索)
XJS_API void XJS_CALL xjs_result_SetSortField(
    xjs_result* result, 
    const char* fieldName, // 字段名：{文件评分, 文件名, 文件夹, 修改时间, 文件大小, 文件类型} 任意一个成员值
    BOOL ascending        // 从小到大
);


// 取排序字段 (默认为`文件评分`)
XJS_API const char* XJS_CALL xjs_result_GetSortField(xjs_result* result);

// 取排序方式 (TRUE == `从大到小`)
XJS_API BOOL XJS_CALL xjs_result_GetSortway (xjs_result* result);

// 取全部排序字段, 返回JSON:["文件评分","文件名","文件夹","修改时间","文件大小","文件类型"]
XJS_API const char* XJS_CALL xjs_result_GetAllSortFieldArray(xjs_result* result);


// 置当前筛选分类 (需要注意的是，不会触发任何事件，也不会改变搜索结果。如果需要结果产生变化，需要重新进行搜索)
XJS_API BOOL XJS_CALL xjs_result_SetSelectedFilter(
    xjs_result* result, 
    const char* categoryName // 分类名：{"全部", "文件夹"...}
);

// 取当前筛选分类 (默认为'全部')
XJS_API const char* XJS_CALL xjs_result_GetSelectedFilter(xjs_result* result);



/* 取全部筛选分类 返回JSON:
[
    {
        "名称":"压缩包", 
        "类型":95, 
        "后缀":"RAR,ZIP,ZIPX,7Z,ISO,IMG,ISZ,CAB,ARJ,ACE,ALZ,UUE,TAR,GZ,GZIP,TGZ,TPZ,BZIP2,BZ2,BZ,TBZ,XPI,WIM,SWM,XAR,DEB,DMG,HFS,CPIO,LZMA,LZMA86,SPLIT,001"
    }
]
*/
XJS_API const char* XJS_CALL xjs_result_GetAllFilter(xjs_result* result);


// 移除搜索结果 从搜索结果中，移除指定ID, 返回实际移除数量.
XJS_API int XJS_CALL xjs_result_RemoveFileId(xjs_result* result, const int* idArray, int count);

// 获取用户设定的值，运行时的值，不会保存到数据库中
XJS_API void* XJS_CALL xjs_result_GetUserValue(xjs_result* result);

// 设置用户设定的值，运行时的值，不会保存到数据库中
XJS_API void XJS_CALL xjs_result_SetUserValue(xjs_result* result, void* userData);

#ifdef __cplusplus
}
#endif