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

#include <mutex>
#include <list>
#include <vector>
#include <string>
#include <algorithm>
#include "transaction/transaction.h"

class TransactionManager;  // 前向声明

static const std::string GroupLockModeStr[10] = {"NON_LOCK", "IS", "IX", "S", "X", "SIX"};

/** 索引间隙锁（谓词范围锁），用于防止幻读
 *  记录事务在索引上的扫描范围，INSERT 时检查是否与活跃范围冲突
 */
struct PredicateRange {
    txn_id_t txn_id_;           // 所属事务ID
    int tab_fd_;                // 表文件描述符
    std::string index_name_;    // 索引名称
    std::string lower_key_;     // 范围下界（原始索引键字节）
    std::string upper_key_;     // 范围上界（原始索引键字节）
    bool has_lower_;            // 是否存在下界条件
    bool has_upper_;            // 是否存在上界条件
    bool lower_inclusive_;      // 下界是否包含等号（>=为true，>为false）
    bool upper_inclusive_;      // 上界是否包含等号（<=为true，<为false）
    bool is_full_scan_;         // 是否为全表扫描（无索引的范围查询）
};

class LockManager {
public:
    /* 加锁类型，包括共享锁、排他锁、意向共享锁、意向排他锁、SIX（意向排他锁+共享锁） */
    enum class LockMode { SHARED, EXLUCSIVE, INTENTION_SHARED, INTENTION_EXCLUSIVE, S_IX };

    /* 用于标识加锁队列中排他性最强的锁类型 */
    enum class GroupLockMode { NON_LOCK, IS, IX, S, X, SIX };

    /* 事务的加锁申请 */
    class LockRequest {
    public:
        LockRequest(txn_id_t txn_id, LockMode lock_mode)
            : txn_id_(txn_id), lock_mode_(lock_mode), granted_(false) {}

        txn_id_t txn_id_;   // 申请加锁的事务ID
        LockMode lock_mode_;    // 事务申请加锁的类型
        bool granted_;          // 该事务是否已经被赋予锁
    };

    /* 数据项上的加锁队列（NO-WAIT 策略无需条件变量） */
    class LockRequestQueue {
    public:
        std::list<LockRequest> request_queue_;  // 加锁队列
        GroupLockMode group_lock_mode_ = GroupLockMode::NON_LOCK;   // 加锁队列的锁模式
    };

    LockManager() {}
    ~LockManager() {}

    /** 设置事务管理器指针（用于死锁检测时获取事务信息） */
    void set_txn_mgr(TransactionManager* txn_mgr) { txn_mgr_ = txn_mgr; }

    bool lock_shared_on_record(Transaction* txn, const Rid& rid, int tab_fd);
    bool lock_exclusive_on_record(Transaction* txn, const Rid& rid, int tab_fd);
    bool lock_shared_on_table(Transaction* txn, int tab_fd);
    bool lock_exclusive_on_table(Transaction* txn, int tab_fd);
    bool lock_IS_on_table(Transaction* txn, int tab_fd);
    bool lock_IX_on_table(Transaction* txn, int tab_fd);
    bool unlock(Transaction* txn, LockDataId lock_data_id);

    // ===== 间隙锁（谓词范围锁）接口，防止幻读 =====
    /** 原子操作：申请表级IS锁 + 注册索引扫描范围（消除TOCTOU竞态窗口） */
    bool lock_IS_on_table_with_predicate(Transaction* txn, int tab_fd,
                                          const std::string& index_name,
                                          const std::string& lower_key, const std::string& upper_key,
                                          bool has_lower, bool has_upper,
                                          bool lower_inclusive, bool upper_inclusive, bool is_full_scan);
    /** 注册索引扫描范围 */
    void add_predicate_range(txn_id_t txn_id, int tab_fd, const std::string& index_name,
                             const std::string& lower_key, const std::string& upper_key,
                             bool has_lower, bool has_upper,
                             bool lower_inclusive, bool upper_inclusive, bool is_full_scan);
    /** 移除事务的所有谓词范围（在 commit/abort 时调用） */
    void remove_predicate_ranges(txn_id_t txn_id);
    /** 检查插入/更新键是否与活跃谓词范围冲突
     * @return true 表示冲突（应中止请求事务），false 表示无冲突 */
    bool check_predicate_conflict(int tab_fd, const std::string& key, txn_id_t requesting_txn);

private:
    std::mutex latch_;      // 用于锁表的并发
    std::unordered_map<LockDataId, LockRequestQueue> lock_table_;   // 全局锁表
    TransactionManager* txn_mgr_ = nullptr;  // 事务管理器（用于死锁检测）

    // 间隙锁：所有活跃的索引扫描范围
    std::vector<PredicateRange> predicate_ranges_;
};
