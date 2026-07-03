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
#include <vector>
#include <iostream>
#include "log_defs.h"
#include "common/config.h"
#include "record/rm_defs.h"
#include "storage/page.h"

/* 日志记录对应操作的类型 */
enum LogType: int {
    UPDATE = 0,
    INSERT,
    DELETE,
    begin,
    commit,
    ABORT,
    CHECKPOINT
};
static std::string LogTypeStr[] = {
    "UPDATE",
    "INSERT",
    "DELETE",
    "BEGIN",
    "COMMIT",
    "ABORT",
    "CHECKPOINT"
};

class LogRecord {
public:
    LogType log_type_;         /* 日志对应操作的类型 */
    lsn_t lsn_;                /* 当前日志的lsn */
    uint32_t log_tot_len_;     /* 整个日志记录的长度 */
    txn_id_t log_tid_;         /* 创建当前日志的事务ID */
    lsn_t prev_lsn_;           /* 事务创建的前一条日志记录的lsn，用于undo */

    virtual ~LogRecord() = default;

    // 把日志记录序列化到dest中
    virtual void serialize (char* dest) const {
        memcpy(dest + OFFSET_LOG_TYPE, &log_type_, sizeof(LogType));
        memcpy(dest + OFFSET_LSN, &lsn_, sizeof(lsn_t));
        memcpy(dest + OFFSET_LOG_TOT_LEN, &log_tot_len_, sizeof(uint32_t));
        memcpy(dest + OFFSET_LOG_TID, &log_tid_, sizeof(txn_id_t));
        memcpy(dest + OFFSET_PREV_LSN, &prev_lsn_, sizeof(lsn_t));
    }
    // 从src中反序列化出一条日志记录
    virtual void deserialize(const char* src) {
        log_type_ = *reinterpret_cast<const LogType*>(src);
        lsn_ = *reinterpret_cast<const lsn_t*>(src + OFFSET_LSN);
        log_tot_len_ = *reinterpret_cast<const uint32_t*>(src + OFFSET_LOG_TOT_LEN);
        log_tid_ = *reinterpret_cast<const txn_id_t*>(src + OFFSET_LOG_TID);
        prev_lsn_ = *reinterpret_cast<const lsn_t*>(src + OFFSET_PREV_LSN);
    }
    // used for debug
    virtual void format_print() {
        std::cout << "log type in father_function: " << LogTypeStr[log_type_] << "\n";
        printf("Print Log Record:\n");
        printf("log_type_: %s\n", LogTypeStr[log_type_].c_str());
        printf("lsn: %d\n", lsn_);
        printf("log_tot_len: %d\n", log_tot_len_);
        printf("log_tid: %d\n", log_tid_);
        printf("prev_lsn: %d\n", prev_lsn_);
    }
};

class BeginLogRecord: public LogRecord {
public:
    BeginLogRecord() {
        log_type_ = LogType::begin;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }
    BeginLogRecord(txn_id_t txn_id) : BeginLogRecord() {
        log_tid_ = txn_id;
    }
    // 序列化Begin日志记录到dest中
    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
    }
    // 从src中反序列化出一条Begin日志记录
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
    }
    virtual void format_print() override {
        std::cout << "log type in son_function: " << LogTypeStr[log_type_] << "\n";
        LogRecord::format_print();
    }
};

/** commit操作的日志记录（仅包含头部，标记事务已提交） */
class CommitLogRecord: public LogRecord {
public:
    CommitLogRecord() {
        log_type_ = LogType::commit;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }
    CommitLogRecord(txn_id_t txn_id) : CommitLogRecord() {
        log_tid_ = txn_id;
    }
    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
    }
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
    }
    virtual void format_print() override {
        LogRecord::format_print();
    }
};

/** abort操作的日志记录（仅包含头部，标记事务已回滚） */
class AbortLogRecord: public LogRecord {
public:
    AbortLogRecord() {
        log_type_ = LogType::ABORT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }
    AbortLogRecord(txn_id_t txn_id) : AbortLogRecord() {
        log_tid_ = txn_id;
    }
    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
    }
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
    }
    virtual void format_print() override {
        LogRecord::format_print();
    }
};

class InsertLogRecord: public LogRecord {
public:
    InsertLogRecord() {
        log_type_ = LogType::INSERT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
    }
    InsertLogRecord(txn_id_t txn_id, RmRecord& insert_value, Rid& rid, std::string table_name)
        : InsertLogRecord() {
        log_tid_ = txn_id;
        insert_value_ = insert_value;
        rid_ = rid;
        log_tot_len_ += sizeof(int);              // record size字段
        log_tot_len_ += insert_value_.size;       // 实际记录数据
        log_tot_len_ += sizeof(Rid);              // rid
        table_name_size_ = table_name.length();
        table_name_ = new char[table_name_size_ + 1];
        memcpy(table_name_, table_name.c_str(), table_name_size_);
        table_name_[table_name_size_] = '\0';  // null终止符
        log_tot_len_ += sizeof(size_t) + table_name_size_;
    }

    ~InsertLogRecord() {
        if (table_name_ != nullptr) {
            delete[] table_name_;
            table_name_ = nullptr;
        }
    }

    // 把insert日志记录序列化到dest中
    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        memcpy(dest + offset, &insert_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, insert_value_.data, insert_value_.size);
        offset += insert_value_.size;
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        memcpy(dest + offset, &table_name_size_, sizeof(size_t));
        offset += sizeof(size_t);
        memcpy(dest + offset, table_name_, table_name_size_);
    }
    // 从src中反序列化出一条Insert日志记录
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
        insert_value_.Deserialize(src + OFFSET_LOG_DATA);
        int offset = OFFSET_LOG_DATA + insert_value_.size + sizeof(int);
        rid_ = *reinterpret_cast<const Rid*>(src + offset);
        offset += sizeof(Rid);
        table_name_size_ = *reinterpret_cast<const size_t*>(src + offset);
        offset += sizeof(size_t);
        table_name_ = new char[table_name_size_ + 1];
        memcpy(table_name_, src + offset, table_name_size_);
        table_name_[table_name_size_] = '\0';  // null终止符
    }
    void format_print() override {
        printf("insert record\n");
        LogRecord::format_print();
        printf("insert_value: %s\n", insert_value_.data);
        printf("insert rid: %d, %d\n", rid_.page_no, rid_.slot_no);
        printf("table name: %s\n", table_name_);
    }

    RmRecord insert_value_;     // 插入的记录
    Rid rid_;                   // 记录插入的位置
    char* table_name_;          // 插入记录的表名称
    size_t table_name_size_;    // 表名称的大小
};

/** delete操作的日志记录（含完整旧记录用于UNDO恢复） */
class DeleteLogRecord: public LogRecord {
public:
    DeleteLogRecord() {
        log_type_ = LogType::DELETE;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
    }
    DeleteLogRecord(txn_id_t txn_id, std::string table_name, RmRecord& deleted_record, Rid& rid)
        : DeleteLogRecord() {
        log_tid_ = txn_id;
        deleted_record_ = deleted_record;
        rid_ = rid;
        log_tot_len_ += sizeof(int);                 // record size字段
        log_tot_len_ += deleted_record_.size;        // 实际记录数据（含MVCC头）
        log_tot_len_ += sizeof(Rid);                 // rid
        table_name_size_ = table_name.length();
        table_name_ = new char[table_name_size_ + 1];
        memcpy(table_name_, table_name.c_str(), table_name_size_);
        table_name_[table_name_size_] = '\0';  // null终止符
        log_tot_len_ += sizeof(size_t) + table_name_size_;
    }

    ~DeleteLogRecord() {
        if (table_name_ != nullptr) {
            delete[] table_name_;
            table_name_ = nullptr;
        }
    }

    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        memcpy(dest + offset, &deleted_record_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, deleted_record_.data, deleted_record_.size);
        offset += deleted_record_.size;
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        memcpy(dest + offset, &table_name_size_, sizeof(size_t));
        offset += sizeof(size_t);
        memcpy(dest + offset, table_name_, table_name_size_);
    }

    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
        deleted_record_.Deserialize(src + OFFSET_LOG_DATA);
        int offset = OFFSET_LOG_DATA + deleted_record_.size + sizeof(int);
        rid_ = *reinterpret_cast<const Rid*>(src + offset);
        offset += sizeof(Rid);
        table_name_size_ = *reinterpret_cast<const size_t*>(src + offset);
        offset += sizeof(size_t);
        table_name_ = new char[table_name_size_ + 1];
        memcpy(table_name_, src + offset, table_name_size_);
        table_name_[table_name_size_] = '\0';  // null终止符
    }

    void format_print() override {
        printf("delete record\n");
        LogRecord::format_print();
        printf("delete rid: %d, %d\n", rid_.page_no, rid_.slot_no);
        printf("table name: %s\n", table_name_);
    }

    RmRecord deleted_record_;   // 被删除的完整记录（含MVCC头，用于UNDO恢复）
    Rid rid_;                   // 被删除记录的位置
    char* table_name_;          // 表名称
    size_t table_name_size_;    // 表名称的大小
};

/** update操作的日志记录（含新旧记录用于UNDO/REDO） */
class UpdateLogRecord: public LogRecord {
public:
    UpdateLogRecord() {
        log_type_ = LogType::UPDATE;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
    }
    UpdateLogRecord(txn_id_t txn_id, std::string table_name, RmRecord& old_record, RmRecord& new_record, Rid& rid)
        : UpdateLogRecord() {
        log_tid_ = txn_id;
        old_record_ = old_record;
        new_record_ = new_record;
        rid_ = rid;
        // old record
        log_tot_len_ += sizeof(int) + old_record_.size;
        // new record
        log_tot_len_ += sizeof(int) + new_record_.size;
        log_tot_len_ += sizeof(Rid);                 // rid
        table_name_size_ = table_name.length();
        table_name_ = new char[table_name_size_ + 1];
        memcpy(table_name_, table_name.c_str(), table_name_size_);
        table_name_[table_name_size_] = '\0';  // null终止符
        log_tot_len_ += sizeof(size_t) + table_name_size_;
    }

    ~UpdateLogRecord() {
        if (table_name_ != nullptr) {
            delete[] table_name_;
            table_name_ = nullptr;
        }
    }

    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        // 旧记录
        memcpy(dest + offset, &old_record_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, old_record_.data, old_record_.size);
        offset += old_record_.size;
        // 新记录
        memcpy(dest + offset, &new_record_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, new_record_.data, new_record_.size);
        offset += new_record_.size;
        // rid
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        // table name
        memcpy(dest + offset, &table_name_size_, sizeof(size_t));
        offset += sizeof(size_t);
        memcpy(dest + offset, table_name_, table_name_size_);
    }

    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
        int offset = OFFSET_LOG_DATA;
        // 旧记录
        old_record_.Deserialize(src + offset);
        offset += old_record_.size + sizeof(int);
        // 新记录
        new_record_.Deserialize(src + offset);
        offset += new_record_.size + sizeof(int);
        // rid
        rid_ = *reinterpret_cast<const Rid*>(src + offset);
        offset += sizeof(Rid);
        // table name
        table_name_size_ = *reinterpret_cast<const size_t*>(src + offset);
        offset += sizeof(size_t);
        table_name_ = new char[table_name_size_ + 1];
        memcpy(table_name_, src + offset, table_name_size_);
        table_name_[table_name_size_] = '\0';  // null终止符
    }

    void format_print() override {
        printf("update record\n");
        LogRecord::format_print();
        printf("update rid: %d, %d\n", rid_.page_no, rid_.slot_no);
        printf("table name: %s\n", table_name_);
    }

    RmRecord old_record_;       // 更新前的完整记录（含MVCC头）
    RmRecord new_record_;       // 更新后的完整记录（含MVCC头）
    Rid rid_;                   // 记录位置
    char* table_name_;          // 表名称
    size_t table_name_size_;    // 表名称的大小
};

/** 检查点日志记录：记录ATT和DPT快照，恢复时从此LSN开始分析 */
class CheckpointLogRecord: public LogRecord {
public:
    CheckpointLogRecord() {
        log_type_ = LogType::CHECKPOINT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }

    /** 设置ATT（活跃事务表） */
    void set_att(const std::vector<std::pair<txn_id_t, lsn_t>>& att) {
        att_ = att;
        att_count_ = att_.size();
        log_tot_len_ += sizeof(size_t);                     // att_count
        log_tot_len_ += att_count_ * (sizeof(txn_id_t) + sizeof(lsn_t));
    }

    /** 设置DPT（脏页表） */
    void set_dpt(const std::vector<std::pair<PageId, lsn_t>>& dpt) {
        dpt_ = dpt;
        dpt_count_ = dpt_.size();
        log_tot_len_ += sizeof(size_t);                     // dpt_count
        log_tot_len_ += dpt_count_ * (sizeof(int) + sizeof(page_id_t) + sizeof(lsn_t));
    }

    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        // ATT
        memcpy(dest + offset, &att_count_, sizeof(size_t));
        offset += sizeof(size_t);
        for (auto& [tid, lsn] : att_) {
            memcpy(dest + offset, &tid, sizeof(txn_id_t));
            offset += sizeof(txn_id_t);
            memcpy(dest + offset, &lsn, sizeof(lsn_t));
            offset += sizeof(lsn_t);
        }
        // DPT
        memcpy(dest + offset, &dpt_count_, sizeof(size_t));
        offset += sizeof(size_t);
        for (auto& [pid, lsn] : dpt_) {
            memcpy(dest + offset, &pid.fd, sizeof(int));
            offset += sizeof(int);
            memcpy(dest + offset, &pid.page_no, sizeof(page_id_t));
            offset += sizeof(page_id_t);
            memcpy(dest + offset, &lsn, sizeof(lsn_t));
            offset += sizeof(lsn_t);
        }
    }

    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
        int offset = OFFSET_LOG_DATA;
        // ATT
        att_count_ = *reinterpret_cast<const size_t*>(src + offset);
        offset += sizeof(size_t);
        att_.clear();
        for (size_t i = 0; i < att_count_; i++) {
            txn_id_t tid = *reinterpret_cast<const txn_id_t*>(src + offset);
            offset += sizeof(txn_id_t);
            lsn_t lsn = *reinterpret_cast<const lsn_t*>(src + offset);
            offset += sizeof(lsn_t);
            att_.emplace_back(tid, lsn);
        }
        // DPT
        dpt_count_ = *reinterpret_cast<const size_t*>(src + offset);
        offset += sizeof(size_t);
        dpt_.clear();
        for (size_t i = 0; i < dpt_count_; i++) {
            PageId pid;
            pid.fd = *reinterpret_cast<const int*>(src + offset);
            offset += sizeof(int);
            pid.page_no = *reinterpret_cast<const page_id_t*>(src + offset);
            offset += sizeof(page_id_t);
            lsn_t lsn = *reinterpret_cast<const lsn_t*>(src + offset);
            offset += sizeof(lsn_t);
            dpt_.emplace_back(pid, lsn);
        }
    }

    void format_print() override {
        printf("checkpoint record\n");
        LogRecord::format_print();
        printf("ATT count: %zu, DPT count: %zu\n", att_count_, dpt_count_);
    }

    std::vector<std::pair<txn_id_t, lsn_t>> att_;     // 活跃事务表
    std::vector<std::pair<PageId, lsn_t>> dpt_;        // 脏页表
    size_t att_count_ = 0;
    size_t dpt_count_ = 0;
};

/* 日志缓冲区，只有一个buffer，因此需要阻塞地去把日志写入缓冲区中 */

class LogBuffer {
public:
    LogBuffer() {
        offset_ = 0;
        memset(buffer_, 0, sizeof(buffer_));
    }

    bool is_full(int append_size) {
        if(offset_ + append_size > LOG_BUFFER_SIZE)
            return true;
        return false;
    }

    char buffer_[LOG_BUFFER_SIZE+1];
    int offset_;    // 写入log的offset
};

/* 日志管理器，负责把日志写入日志缓冲区，以及把日志缓冲区中的内容写入磁盘中 */
class LogManager {
public:
    LogManager(DiskManager* disk_manager) { disk_manager_ = disk_manager; }

    lsn_t add_log_to_buffer(LogRecord* log_record);
    void flush_log_to_disk();

    LogBuffer* get_log_buffer() { return &log_buffer_; }
    lsn_t get_persist_lsn() { return persist_lsn_; }
    lsn_t get_global_lsn() { return global_lsn_.load(); }

private:
    std::atomic<lsn_t> global_lsn_{0};  // 全局lsn，递增，用于为每条记录分发lsn
    std::mutex latch_;                  // 用于对log_buffer_的互斥访问
    LogBuffer log_buffer_;              // 日志缓冲区
    lsn_t persist_lsn_{INVALID_LSN};    // 记录已经持久化到磁盘中的最后一条日志的日志号
    DiskManager* disk_manager_;
};
