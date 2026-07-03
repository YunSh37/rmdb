/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "lock_manager.h"
#include "transaction/transaction_manager.h"
#include <algorithm>
#include <unordered_set>
#include <chrono>
#include <thread>

/* ========== 锁兼容矩阵 ========== */
// 检查新申请的 lock_mode 是否与队列当前的 group_lock_mode 兼容
static bool is_compatible(LockManager::LockMode req_mode, LockManager::GroupLockMode group_mode) {
    // 兼容矩阵：
    //           IS  IX  S   X  SIX  NON_LOCK
    // IS        Y   Y   Y   N  Y    Y
    // IX        Y   Y   N   N  N    Y
    // S         Y   N   Y   N  N    Y
    // X         N   N   N   N  N    Y
    // SIX       Y   N   N   N  N    Y
    switch (req_mode) {
        case LockManager::LockMode::INTENTION_SHARED:
            return group_mode != LockManager::GroupLockMode::X;
        case LockManager::LockMode::INTENTION_EXCLUSIVE:
            return group_mode == LockManager::GroupLockMode::NON_LOCK
                || group_mode == LockManager::GroupLockMode::IS
                || group_mode == LockManager::GroupLockMode::IX;
        case LockManager::LockMode::SHARED:
            return group_mode == LockManager::GroupLockMode::NON_LOCK
                || group_mode == LockManager::GroupLockMode::IS
                || group_mode == LockManager::GroupLockMode::S;
        case LockManager::LockMode::EXLUCSIVE:
            return group_mode == LockManager::GroupLockMode::NON_LOCK;
        case LockManager::LockMode::S_IX:
            return group_mode == LockManager::GroupLockMode::NON_LOCK
                || group_mode == LockManager::GroupLockMode::IS;
        default:
            return false;
    }
}

// 更新 GroupLockMode（合并新锁模式到当前组锁模式）
static LockManager::GroupLockMode merge_group_mode(LockManager::GroupLockMode current,
                                                      LockManager::LockMode new_mode) {
    switch (new_mode) {
        case LockManager::LockMode::SHARED:
            if (current == LockManager::GroupLockMode::NON_LOCK) return LockManager::GroupLockMode::S;
            if (current == LockManager::GroupLockMode::IS)   return LockManager::GroupLockMode::S;
            if (current == LockManager::GroupLockMode::IX)   return LockManager::GroupLockMode::SIX;
            if (current == LockManager::GroupLockMode::S)    return LockManager::GroupLockMode::S;
            if (current == LockManager::GroupLockMode::SIX)  return LockManager::GroupLockMode::SIX;
            return LockManager::GroupLockMode::S;
        case LockManager::LockMode::EXLUCSIVE:
            return LockManager::GroupLockMode::X;
        case LockManager::LockMode::INTENTION_SHARED:
            if (current == LockManager::GroupLockMode::NON_LOCK) return LockManager::GroupLockMode::IS;
            if (current == LockManager::GroupLockMode::IX)   return LockManager::GroupLockMode::IX;
            if (current == LockManager::GroupLockMode::S)    return LockManager::GroupLockMode::S;
            if (current == LockManager::GroupLockMode::SIX)  return LockManager::GroupLockMode::SIX;
            if (current == LockManager::GroupLockMode::IS)    return LockManager::GroupLockMode::IS;
            return LockManager::GroupLockMode::IS;
        case LockManager::LockMode::INTENTION_EXCLUSIVE:
            if (current == LockManager::GroupLockMode::NON_LOCK) return LockManager::GroupLockMode::IX;
            if (current == LockManager::GroupLockMode::IS)   return LockManager::GroupLockMode::IX;
            if (current == LockManager::GroupLockMode::S)    return LockManager::GroupLockMode::SIX;
            if (current == LockManager::GroupLockMode::IX)   return LockManager::GroupLockMode::IX;
            return LockManager::GroupLockMode::IX;
        case LockManager::LockMode::S_IX:
            if (current == LockManager::GroupLockMode::NON_LOCK) return LockManager::GroupLockMode::SIX;
            if (current == LockManager::GroupLockMode::IS)   return LockManager::GroupLockMode::SIX;
            return LockManager::GroupLockMode::SIX;
        default:
            return current;
    }
}

/* ========== 死锁检测：等待图 ========== */
// 全局等待图：waiter → holder（waiter等待holder释放锁）
static std::mutex wait_graph_mutex;
static std::unordered_map<txn_id_t, std::unordered_set<txn_id_t>> wait_for_graph;

static void add_wait_edge(txn_id_t waiter, txn_id_t holder) {
    std::lock_guard<std::mutex> lock(wait_graph_mutex);
    wait_for_graph[waiter].insert(holder);
}

static void remove_wait_edges(txn_id_t txn) {
    std::lock_guard<std::mutex> lock(wait_graph_mutex);
    wait_for_graph.erase(txn);
    for (auto& pair : wait_for_graph) {
        pair.second.erase(txn);
    }
}

// DFS 检测从 start 出发是否存在环
static bool dfs_cycle(txn_id_t start, txn_id_t current,
                      std::unordered_set<txn_id_t>& visited,
                      std::unordered_set<txn_id_t>& in_stack,
                      std::vector<txn_id_t>& cycle_path) {
    visited.insert(current);
    in_stack.insert(current);
    cycle_path.push_back(current);

    auto it = wait_for_graph.find(current);
    if (it != wait_for_graph.end()) {
        for (txn_id_t next : it->second) {
            if (next == start) {
                cycle_path.push_back(start);
                return true;  // 找到环
            }
            if (in_stack.count(next) == 0) {
                if (dfs_cycle(start, next, visited, in_stack, cycle_path)) {
                    return true;
                }
            }
        }
    }

    cycle_path.pop_back();
    in_stack.erase(current);
    return false;
}

// 检测等待图中是否有环，返回环中的事务ID列表（若存在）
static std::vector<txn_id_t> detect_cycle() {
    std::lock_guard<std::mutex> lock(wait_graph_mutex);
    std::unordered_set<txn_id_t> visited;

    for (auto& pair : wait_for_graph) {
        if (visited.count(pair.first) == 0) {
            std::unordered_set<txn_id_t> in_stack;
            std::vector<txn_id_t> cycle_path;
            if (dfs_cycle(pair.first, pair.first, visited, in_stack, cycle_path)) {
                return cycle_path;
            }
        }
    }
    return {};
}

// 选择环中最年轻的事务（最大时间戳）作为牺牲品
static txn_id_t pick_victim(const std::vector<txn_id_t>& cycle, TransactionManager* txn_mgr) {
    txn_id_t victim = cycle[0];
    timestamp_t max_ts = 0;

    for (txn_id_t tid : cycle) {
        Transaction* txn = txn_mgr->get_transaction(tid);
        if (txn != nullptr) {
            timestamp_t ts = txn->get_start_ts();
            if (ts > max_ts) {
                max_ts = ts;
                victim = tid;
            }
        }
    }
    return victim;
}

/* ========== 锁申请核心逻辑 ========== */

/**
 * @description: 通用加锁方法
 * @param txn 申请锁的事务
 * @param lid 锁对象ID
 * @param mode 锁模式
 * @param txn_mgr 事务管理器（用于死锁检测时选择牺牲者）
 * @return 加锁是否成功
 */
static bool lock_common(Transaction* txn, LockDataId& lid, LockManager::LockMode mode,
                        LockManager* lm, std::mutex& latch,
                        std::unordered_map<LockDataId, LockManager::LockRequestQueue>& lock_table,
                        TransactionManager* txn_mgr) {

    std::unique_lock<std::mutex> guard(latch);

    // 获取或创建锁请求队列
    auto& queue = lock_table[lid];

    // 检查是否已经持有锁（同一事务不会与自己冲突）
    // 若已持有相同或更强的锁则直接返回；若持有较弱锁则升级
    for (auto& req : queue.request_queue_) {
        if (req.txn_id_ == txn->get_transaction_id() && req.granted_) {
            if (req.lock_mode_ == mode || req.lock_mode_ == LockManager::LockMode::EXLUCSIVE) {
                return true;  // 已持有相同或更强的锁
            }
            // 锁升级：更新锁模式并重新计算 group_lock_mode
            req.lock_mode_ = mode;
            queue.group_lock_mode_ = LockManager::GroupLockMode::NON_LOCK;
            for (auto& r : queue.request_queue_) {
                if (r.granted_) {
                    queue.group_lock_mode_ = merge_group_mode(queue.group_lock_mode_, r.lock_mode_);
                }
            }
            return true;
        }
    }

    // 检查是否与已授予的锁兼容
    if (is_compatible(mode, queue.group_lock_mode_)) {
        // 兼容：直接授予锁
        LockManager::LockRequest req(txn->get_transaction_id(), mode);
        req.granted_ = true;
        queue.request_queue_.push_back(req);
        queue.group_lock_mode_ = merge_group_mode(queue.group_lock_mode_, mode);

        // 记录到事务的锁集合
        txn->get_lock_set()->insert(lid);
        return true;
    }

    // 不兼容：需要等待
    // 构建等待边（waiter等待所有已授予锁的持有者）
    for (auto& req : queue.request_queue_) {
        if (req.granted_ && req.txn_id_ != txn->get_transaction_id()) {
            add_wait_edge(txn->get_transaction_id(), req.txn_id_);
        }
    }

    // 死锁检测
    auto cycle = detect_cycle();
    if (!cycle.empty()) {
        // 清理等待边
        for (auto& req : queue.request_queue_) {
            if (req.granted_) {
                remove_wait_edges(txn->get_transaction_id());
            }
        }
        txn_id_t victim = pick_victim(cycle, txn_mgr);
        // 从锁表中移除牺牲者的等待请求
        for (auto& table_pair : lock_table) {
            auto& q = table_pair.second;
            q.request_queue_.remove_if([victim](const LockManager::LockRequest& r) {
                return r.txn_id_ == victim && !r.granted_;
            });
        }
        // 清理牺牲者的等待边
        remove_wait_edges(victim);
        // 唤醒等待的线程
        queue.cv_.notify_all();

        if (victim == txn->get_transaction_id()) {
            throw TransactionAbortException(victim, AbortReason::DEADLOCK_PREVENTION);
        }
        // 如果牺牲者不是当前事务，当前事务仍然需要等待，递归重试加锁
        // 但需要先清理当前事务的等待边（因为牺牲者已被清除）
    }

    // 加入等待队列
    LockManager::LockRequest req(txn->get_transaction_id(), mode);
    req.granted_ = false;
    queue.request_queue_.push_back(req);

    // 阻塞等待，直到被唤醒
    queue.cv_.wait(guard, [&]() {
        // 检查是否可以被授予
        // 重新计算当前队列中排在此请求前的所有已授予锁的 group_mode
        LockManager::GroupLockMode gm = LockManager::GroupLockMode::NON_LOCK;
        for (auto& r : queue.request_queue_) {
            if (&r == &req) break;  // 只考虑排在此请求前的
            if (r.granted_) {
                gm = merge_group_mode(gm, r.lock_mode_);
            }
        }
        return is_compatible(mode, gm);
    });

    // 被唤醒，授予锁
    for (auto& r : queue.request_queue_) {
        if (r.txn_id_ == txn->get_transaction_id() && !r.granted_) {
            r.granted_ = true;
            break;
        }
    }
    queue.group_lock_mode_ = merge_group_mode(queue.group_lock_mode_, mode);

    // 清理等待边
    remove_wait_edges(txn->get_transaction_id());

    // 记录到事务的锁集合
    txn->get_lock_set()->insert(lid);

    return true;
}

/**
 * @description: 申请行级共享锁
 */
bool LockManager::lock_shared_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    LockDataId lid(tab_fd, rid, LockDataType::RECORD);
    return lock_common(txn, lid, LockMode::SHARED, this, latch_, lock_table_, txn_mgr_);
}

/**
 * @description: 申请行级排他锁
 */
bool LockManager::lock_exclusive_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    LockDataId lid(tab_fd, rid, LockDataType::RECORD);
    return lock_common(txn, lid, LockMode::EXLUCSIVE, this, latch_, lock_table_, txn_mgr_);
}

/**
 * @description: 申请表级读锁
 */
bool LockManager::lock_shared_on_table(Transaction* txn, int tab_fd) {
    LockDataId lid(tab_fd, LockDataType::TABLE);
    return lock_common(txn, lid, LockMode::SHARED, this, latch_, lock_table_, txn_mgr_);
}

/**
 * @description: 申请表级写锁
 */
bool LockManager::lock_exclusive_on_table(Transaction* txn, int tab_fd) {
    LockDataId lid(tab_fd, LockDataType::TABLE);
    return lock_common(txn, lid, LockMode::EXLUCSIVE, this, latch_, lock_table_, txn_mgr_);
}

/**
 * @description: 申请表级意向读锁
 */
bool LockManager::lock_IS_on_table(Transaction* txn, int tab_fd) {
    LockDataId lid(tab_fd, LockDataType::TABLE);
    return lock_common(txn, lid, LockMode::INTENTION_SHARED, this, latch_, lock_table_, txn_mgr_);
}

/**
 * @description: 申请表级意向写锁
 */
bool LockManager::lock_IX_on_table(Transaction* txn, int tab_fd) {
    LockDataId lid(tab_fd, LockDataType::TABLE);
    return lock_common(txn, lid, LockMode::INTENTION_EXCLUSIVE, this, latch_, lock_table_, txn_mgr_);
}

/**
 * @description: 释放锁
 */
bool LockManager::unlock(Transaction* txn, LockDataId lock_data_id) {
    std::unique_lock<std::mutex> guard(latch_);

    auto it = lock_table_.find(lock_data_id);
    if (it == lock_table_.end()) {
        return true;  // 锁不存在
    }

    auto& queue = it->second;

    // 从队列中移除该事务的请求
    queue.request_queue_.remove_if([txn](const LockRequest& req) {
        return req.txn_id_ == txn->get_transaction_id();
    });

    // 重新计算 group_lock_mode
    queue.group_lock_mode_ = GroupLockMode::NON_LOCK;
    for (auto& req : queue.request_queue_) {
        if (req.granted_) {
            queue.group_lock_mode_ = merge_group_mode(queue.group_lock_mode_, req.lock_mode_);
        }
    }

    // 唤醒等待者
    queue.cv_.notify_all();

    // 从事务的锁集合中移除
    txn->get_lock_set()->erase(lock_data_id);

    // 清理等待边
    remove_wait_edges(txn->get_transaction_id());

    return true;
}
