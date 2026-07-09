/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include <cstring>
#include "log_manager.h"

/**
 * @description: 添加日志记录到日志缓冲区中，并返回日志记录号
 * @param {LogRecord*} log_record 要写入缓冲区的日志记录
 * @return {lsn_t} 返回该日志的日志记录号
 */
lsn_t LogManager::add_log_to_buffer(LogRecord* log_record) {
    std::unique_lock<std::recursive_mutex> lock(latch_);

    // 1. 分配LSN
    lsn_t lsn = global_lsn_.fetch_add(1);
    log_record->lsn_ = lsn;

    // 2. 序列化日志记录到临时buffer
    int log_len = log_record->log_tot_len_;
    char* serialized = new char[log_len];
    log_record->serialize(serialized);

    // 3. 若缓冲区空间不足，先刷盘
    if (log_buffer_.is_full(log_len)) {
        flush_log_to_disk();
    }

    // 4. 拷贝到日志缓冲区
    memcpy(log_buffer_.buffer_ + log_buffer_.offset_, serialized, log_len);
    log_buffer_.offset_ += log_len;

    delete[] serialized;
    return lsn;
}

/**
 * @description: 把日志缓冲区的内容刷到磁盘中
 * 使用 recursive_mutex 确保多线程安全：commit/abort/checkpoint/executor 均可安全调用
 */
void LogManager::flush_log_to_disk() {
    std::unique_lock<std::recursive_mutex> lock(latch_);

    if (log_buffer_.offset_ == 0) {
        return;  // 缓冲区为空，无需刷盘
    }

    // 写入磁盘
    disk_manager_->write_log(log_buffer_.buffer_, log_buffer_.offset_);

    // CRITICAL: fsync 确保 WAL 日志真正持久化到磁盘。
    // 若没有 fsync，COMMIT/ABORT/CHECKPOINT 日志可能仅存在于 OS 缓冲区缓存中，
    // 服务器 crash 后这些关键日志丢失 → 已提交事务被错误回滚。
    disk_manager_->sync_file(disk_manager_->GetLogFd());

    // 更新持久化LSN（当前global_lsn_-1即是最后一条写入的日志）
    persist_lsn_ = global_lsn_.load() - 1;

    // 重置缓冲区
    log_buffer_.offset_ = 0;
    memset(log_buffer_.buffer_, 0, LOG_BUFFER_SIZE + 1);
}
