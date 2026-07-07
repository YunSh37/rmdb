/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <map>
#include <unordered_map>
#include <memory>
#include "log_manager.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"
#include "system/sm_manager.h"

class RecoveryManager {
public:
    RecoveryManager(DiskManager* disk_manager, BufferPoolManager* buffer_pool_manager, SmManager* sm_manager) {
        disk_manager_ = disk_manager;
        buffer_pool_manager_ = buffer_pool_manager;
        sm_manager_ = sm_manager;
    }

    /** ARIES恢复主流程：analyze → redo → undo */
    void analyze();
    void redo();
    void undo();

    /** 获取恢复后的事务ID最大值（用于恢复时间戳避免MVCC可见性错误） */
    txn_id_t get_max_txn_id() const { return max_txn_id_; }

private:
    /** 从日志数据中反序列化一条日志记录 */
    std::unique_ptr<LogRecord> parse_log_record(const char* data);

    /** 获取表文件句柄（若未打开则打开） */
    RmFileHandle* get_table_fh(const std::string& tab_name);

    /** 安全获取文件fd（表文件可能已被删除，返回-1表示不存在） */
    int get_file_fd_safe(const std::string& tab_name);

    /** 确保指定页面在磁盘上存在（若不存在则写入零页扩展文件） */
    bool ensure_page_exists(const std::string& tab_name, int page_no);

    DiskManager* disk_manager_;
    BufferPoolManager* buffer_pool_manager_;
    SmManager* sm_manager_;

    // === Analyze阶段产物 ===
    /** ATT: 活跃事务表 (txn_id → 最后一条日志LSN) */
    std::unordered_map<txn_id_t, lsn_t> att_;

    /** DPT: 脏页表 (PageId → 该页首次变脏的LSN) */
    std::unordered_map<PageId, lsn_t, PageIdHash> dpt_;

    /** REDO起始LSN（DPT中最小LSN） */
    lsn_t redo_lsn_ = INVALID_LSN;

    /** 日志文件偏移记录：(LSN, 文件偏移量) —— 用于undo阶段快速定位日志 */
    std::vector<std::pair<lsn_t, int>> lsn_offsets_;

    /** REDO 提示数组（与 lsn_offsets_ 一一对应），避免 REDO 阶段重复反序列化
     *  仅对 INSERT/UPDATE/DELETE 有效，fd=-1 表示非数据操作 */
    struct RedoHint {
        int fd = -1;
        int page_no = 0;
    };
    std::vector<RedoHint> redo_hints_;

    /** 完整日志数据（在analyze阶段读入） */
    std::vector<char> log_data_;

    /** 检查点日志的LSN（若存在） */
    lsn_t checkpoint_lsn_ = INVALID_LSN;

    /** 恢复后的最大事务ID（用于恢复时间戳避免MVCC可见性错误） */
    txn_id_t max_txn_id_ = INVALID_TXN_ID;
};
