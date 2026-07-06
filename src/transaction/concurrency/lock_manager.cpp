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

/* ========== 锁兼容矩阵 ========== */
// 检查新申请的 lock_mode 是否与队列当前的 group_lock_mode 兼容
// 兼容矩阵：
//           IS  IX  S   X  SIX  NON_LOCK
// IS        Y   Y   Y   N  Y    Y
// IX        Y   Y   N   N  N    Y
// S         Y   N   Y   N  N    Y
// X         N   N   N   N  N    Y
// SIX       Y   N   N   N  N    Y
static bool is_compatible(LockManager::LockMode req_mode, LockManager::GroupLockMode group_mode) {
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

/* ========== 死锁预防：NO-WAIT 策略 ========== */
// 不再使用等待图和死锁检测：当锁请求不兼容时立即中止请求事务，
// 避免形成等待链，从而预防死锁。

/**
 * @description: 通用加锁方法（NO-WAIT 死锁预防 + 两阶段封锁）
 *   1. 检查 2PL 协议：SHRINKING 状态不允许申请锁
 *   2. 检查是否已持有相同/更强的锁（锁升级路径含兼容性检查）
 *   3. 兼容 → 直接授予锁
 *   4. 不兼容 → 立即中止当前事务（NO-WAIT）
 * @param txn 申请锁的事务
 * @param lid 锁对象ID
 * @param mode 锁模式
 * @return 加锁是否成功（成功后保证持有该锁）
 * @throws TransactionAbortException 当锁请求无法立即满足时
 */
static bool lock_common(Transaction* txn, LockDataId& lid, LockManager::LockMode mode,
                        LockManager* lm, std::mutex& latch,
                        std::unordered_map<LockDataId, LockManager::LockRequestQueue>& lock_table,
                        TransactionManager* txn_mgr) {

    std::unique_lock<std::mutex> guard(latch);

    // ===== 两阶段封锁协议检查 =====
    // SHRINKING 阶段不允许获取新锁（违反 2PL）
    if (txn->get_state() == TransactionState::SHRINKING) {
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::LOCK_ON_SHIRINKING);
    }

    // 获取或创建锁请求队列
    auto& queue = lock_table[lid];

    // ===== 检查是否已持有锁（同一事务不会与自己冲突）=====
    // 若已持有相同或更强的锁则直接返回；若持有较弱锁则尝试升级
    for (auto& req : queue.request_queue_) {
        if (req.txn_id_ == txn->get_transaction_id() && req.granted_) {
            // 已持有相同或更强的锁
            if (req.lock_mode_ == mode || req.lock_mode_ == LockManager::LockMode::EXLUCSIVE) {
                return true;
            }

            // ===== 锁升级：检查与其他已授予锁的兼容性 =====
            // 计算除当前事务外其他所有已授予锁的 group_mode
            LockManager::GroupLockMode other_gm = LockManager::GroupLockMode::NON_LOCK;
            for (auto& r : queue.request_queue_) {
                if (r.txn_id_ != txn->get_transaction_id() && r.granted_) {
                    other_gm = merge_group_mode(other_gm, r.lock_mode_);
                }
            }
            // 若新模式与其他持有者不兼容 → 升级冲突，中止当前事务
            if (!is_compatible(mode, other_gm)) {
                throw TransactionAbortException(txn->get_transaction_id(), AbortReason::UPGRADE_CONFLICT);
            }

            // 执行锁升级：更新锁模式并重新计算 group_lock_mode
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

    // ===== 兼容性检查：NO-WAIT 策略 =====
    if (is_compatible(mode, queue.group_lock_mode_)) {
        // 兼容：直接授予锁
        LockManager::LockRequest req(txn->get_transaction_id(), mode);
        req.granted_ = true;
        queue.request_queue_.push_back(req);
        queue.group_lock_mode_ = merge_group_mode(queue.group_lock_mode_, mode);

        // 记录到事务的锁集合
        txn->get_lock_set()->insert(lid);

        // 首次获取锁时，将状态从 DEFAULT 切换到 GROWING
        if (txn->get_state() == TransactionState::DEFAULT) {
            txn->set_state(TransactionState::GROWING);
        }
        return true;
    }

    // ===== NO-WAIT：不兼容则立即中止请求事务 =====
    // 不进入等待队列，避免形成等待链，从而预防死锁
    throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
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
 *   将事务的锁请求从队列中移除，重新计算 group_lock_mode。
 *   NO-WAIT 策略下无需唤醒等待者（无人等待）。
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

    // 从事务的锁集合中移除
    txn->get_lock_set()->erase(lock_data_id);

    return true;
}
