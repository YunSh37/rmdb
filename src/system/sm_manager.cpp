/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sm_manager.h"

#include <sys/stat.h>
#include <unistd.h>

#include <fstream>

#include "index/ix.h"
#include "record/rm.h"
#include "record_printer.h"

/**
 * @description: 判断是否为一个文件夹
 * @return {bool} 返回是否为一个文件夹
 * @param {string&} db_name 数据库文件名称，与文件夹同名
 */
bool SmManager::is_dir(const std::string& db_name) {
    struct stat st;
    return stat(db_name.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * @description: 创建数据库，所有的数据库相关文件都放在数据库同名文件夹下
 * @param {string&} db_name 数据库名称
 */
void SmManager::create_db(const std::string& db_name) {
    if (is_dir(db_name)) {
        throw DatabaseExistsError(db_name);
    }
    //为数据库创建一个子目录
    std::string cmd = "mkdir " + db_name;
    if (system(cmd.c_str()) < 0) {  // 创建一个名为db_name的目录
        throw UnixError();
    }
    if (chdir(db_name.c_str()) < 0) {  // 进入名为db_name的目录
        throw UnixError();
    }
    //创建系统目录
    DbMeta *new_db = new DbMeta();
    new_db->name_ = db_name;

    // 注意，此处ofstream会在当前目录创建(如果没有此文件先创建)和打开一个名为DB_META_NAME的文件
    std::ofstream ofs(DB_META_NAME);

    // 将new_db中的信息，按照定义好的operator<<操作符，写入到ofs打开的DB_META_NAME文件中
    ofs << *new_db;  // 注意：此处重载了操作符<<

    delete new_db;

    // 创建日志文件
    disk_manager_->create_file(LOG_FILE_NAME);

    // 回到根目录
    if (chdir("..") < 0) {
        throw UnixError();
    }
}

/**
 * @description: 删除数据库，同时需要清空相关文件以及数据库同名文件夹
 * @param {string&} db_name 数据库名称，与文件夹同名
 */
void SmManager::drop_db(const std::string& db_name) {
    if (!is_dir(db_name)) {
        throw DatabaseNotFoundError(db_name);
    }
    std::string cmd = "rm -r " + db_name;
    if (system(cmd.c_str()) < 0) {
        throw UnixError();
    }
}

/**
 * @description: 打开数据库，找到数据库对应的文件夹，并加载数据库元数据和相关文件
 * @param {string&} db_name 数据库名称，与文件夹同名
 */
void SmManager::open_db(const std::string& db_name) {
    // 1. 进入数据库目录
    if (chdir(db_name.c_str()) < 0) {
        throw UnixError();
    }
    // 2. 读取元数据文件，若不存在则初始化为空数据库
    std::ifstream ifs(DB_META_NAME);
    if (ifs.is_open()) {
        ifs >> db_;
        ifs.close();
    }
    db_.name_ = db_name;
    // 3. 为每张表打开记录文件
    for (auto& entry : db_.tabs_) {
        fhs_.emplace(entry.first, rm_manager_->open_file(entry.first));
    }
    // 4. 为每张表打开索引文件
    for (auto& tab_entry : db_.tabs_) {
        for (auto& index : tab_entry.second.indexes) {
            std::string ix_name = ix_manager_->get_index_name(tab_entry.first, index.cols);
            if (ix_manager_->exists(tab_entry.first, index.cols)) {
                ihs_.emplace(ix_name, ix_manager_->open_index(tab_entry.first, index.cols));
            }
        }
    }
}

/**
 * @description: 把数据库相关的元数据刷入磁盘中
 */
void SmManager::flush_meta() {
    // 默认清空文件
    std::ofstream ofs(DB_META_NAME);
    ofs << db_;
}

/**
 * @description: 强制将当前数据库的元数据、表文件和索引文件全部刷盘
 */
void SmManager::flush_all_files() {
    // 1. 先写出并同步数据库元数据
    flush_meta();
    if (disk_manager_->is_file(DB_META_NAME)) {
        int meta_fd = disk_manager_->get_file_fd(DB_META_NAME);
        disk_manager_->sync_file(meta_fd);
        disk_manager_->close_file(meta_fd);
    }

    // 2. 同步所有表文件：文件头、缓冲池脏页、文件本身
    for (auto& entry : fhs_) {
        auto* fh = entry.second.get();
        fh->sync_file_header();
        buffer_pool_manager_->flush_all_pages(fh->GetFd());
        disk_manager_->sync_file(fh->GetFd());
    }

    // 3. 同步所有索引文件：文件头、缓冲池脏页、文件本身
    for (auto& entry : ihs_) {
        auto* ih = entry.second.get();
        ih->sync_file_header();
        buffer_pool_manager_->flush_all_pages(ih->get_fd());
        disk_manager_->sync_file(ih->get_fd());
    }
}

/**
 * @description: 扫描所有表记录，返回MVCC头中出现过的最大时间戳
 */
timestamp_t SmManager::get_max_record_timestamp() {
    timestamp_t max_ts = INVALID_TXN_ID;
    for (auto& entry : fhs_) {
        auto* fh = entry.second.get();
        int user_size = fh->get_user_record_size();
        for (RmScan scan(fh); !scan.is_end(); scan.next()) {
            auto record = fh->get_record(scan.rid(), nullptr);
            auto* hdr = reinterpret_cast<MvccHeader*>(record->data + user_size);
            if (hdr->xmin_ != INT32_MAX && hdr->xmin_ > max_ts) {
                max_ts = hdr->xmin_;
            }
            if (hdr->xmax_ != INT32_MAX && hdr->xmax_ > max_ts) {
                max_ts = hdr->xmax_;
            }
        }
    }
    return max_ts;
}

/**
 * @description: 关闭数据库并把数据落盘
 */
void SmManager::close_db() {
    // 1. 保存元数据到磁盘
    flush_meta();
    // 2. 关闭所有打开的表数据文件
    for (auto& entry : fhs_) {
        rm_manager_->close_file(entry.second.get());
    }
    fhs_.clear();
    // 3. 关闭所有打开的索引文件
    for (auto& entry : ihs_) {
        ix_manager_->close_index(entry.second.get());
    }
    ihs_.clear();
}

/**
 * @description: 显示所有的表,通过测试需要将其结果写入到output.txt,详情看题目文档
 * @param {Context*} context 
 */
void SmManager::show_tables(Context* context) {
    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    outfile << "| Tables |\n";
    RecordPrinter printer(1);
    printer.print_separator(context);
    printer.print_record({"Tables"}, context);
    printer.print_separator(context);
    for (auto &entry : db_.tabs_) {
        auto &tab = entry.second;
        printer.print_record({tab.name}, context);
        outfile << "| " << tab.name << " |\n";
    }
    printer.print_separator(context);
    outfile.close();
}

/**
 * @description: 显示表的元数据
 * @param {string&} tab_name 表名称
 * @param {Context*} context 
 */
void SmManager::desc_table(const std::string& tab_name, Context* context) {
    TabMeta &tab = db_.get_table(tab_name);

    std::vector<std::string> captions = {"Field", "Type", "Index"};
    RecordPrinter printer(captions.size());
    // Print header
    printer.print_separator(context);
    printer.print_record(captions, context);
    printer.print_separator(context);
    // Print fields
    for (auto &col : tab.cols) {
        std::vector<std::string> field_info = {col.name, coltype2str(col.type), col.index ? "YES" : "NO"};
        printer.print_record(field_info, context);
    }
    // Print footer
    printer.print_separator(context);
}

/**
 * @description: 创建表
 * @param {string&} tab_name 表的名称
 * @param {vector<ColDef>&} col_defs 表的字段
 * @param {Context*} context 
 */
void SmManager::create_table(const std::string& tab_name, const std::vector<ColDef>& col_defs, Context* context) {
    if (db_.is_table(tab_name)) {
        throw TableExistsError(tab_name);
    }
    // Create table meta
    int curr_offset = 0;
    TabMeta tab;
    tab.name = tab_name;
    for (auto &col_def : col_defs) {
        ColMeta col = {.tab_name = tab_name,
                       .name = col_def.name,
                       .type = col_def.type,
                       .len = col_def.len,
                       .offset = curr_offset,
                       .index = false};
        curr_offset += col_def.len;
        tab.cols.push_back(col);
    }
    // Create & open record file
    int record_size = curr_offset + MVCC_HEADER_SIZE;  // 记录大小 = 用户数据 + MVCC头部(xmin+xmax)
    rm_manager_->create_file(tab_name, record_size);
    db_.tabs_[tab_name] = tab;
    // fhs_[tab_name] = rm_manager_->open_file(tab_name);
    fhs_.emplace(tab_name, rm_manager_->open_file(tab_name));

    flush_meta();
}

/**
 * @description: 删除表
 * @param {string&} tab_name 表的名称
 * @param {Context*} context
 */
void SmManager::drop_table(const std::string& tab_name, Context* context) {
    // 1. 检查表是否存在
    if (!db_.is_table(tab_name)) {
        throw TableNotFoundError(tab_name);
    }
    TabMeta& tab = db_.get_table(tab_name);
    // 2. 关闭并销毁表的数据文件
    if (fhs_.count(tab_name)) {
        rm_manager_->close_file(fhs_[tab_name].get());
        fhs_.erase(tab_name);
    }
    rm_manager_->destroy_file(tab_name);
    // 3. 销毁该表上的所有索引文件
    for (auto& index : tab.indexes) {
        std::string index_name = ix_manager_->get_index_name(tab_name, index.cols);
        if (ihs_.count(index_name)) {
            ix_manager_->close_index(ihs_[index_name].get());
            ihs_.erase(index_name);
        }
        ix_manager_->destroy_index(tab_name, index.cols);
    }
    // 4. 从元数据中移除并持久化
    db_.tabs_.erase(tab_name);
    flush_meta();
}

/**
 * @description: 创建索引
 * @param {string&} tab_name 表的名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::create_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
    // 1. 检查表是否存在
    TabMeta& tab = db_.get_table(tab_name);
    // 2. 检查索引是否已存在
    if (tab.is_index(col_names)) {
        throw IndexExistsError(tab_name, col_names);
    }
    // 3. 校验列是否存在，并构建ColMeta列表
    std::vector<ColMeta> index_cols;
    for (auto& col_name : col_names) {
        auto col_iter = tab.get_col(col_name);
        index_cols.push_back(*col_iter);
    }
    // 4. 创建索引文件
    ix_manager_->create_index(tab_name, index_cols);
    // 5. 打开索引，扫描表中已有记录并插入索引
    std::unique_ptr<IxIndexHandle> ih = ix_manager_->open_index(tab_name, index_cols);
    RmFileHandle* fh = fhs_.at(tab_name).get();
    // 构建索引键缓冲区
    int key_len = 0;
    for (auto& col : index_cols) {
        key_len += col.len;
    }
    char* key_buf = new char[key_len];
    // 扫描表中所有记录，插入索引
    for (RmScan scan(fh); !scan.is_end(); scan.next()) {
        Rid rid = scan.rid();
        auto record = fh->get_record(rid, nullptr);

        // 跳过软删除记录（xmax != INT32_MAX），防止索引包含已删除记录
        // 的条目。这会导致后续INSERT触发DuplicateKeyError，因为唯一索引
        // 检查（get_value）不检查MVCC可见性。
        int user_size = fh->get_user_record_size();
        MvccHeader* hdr = reinterpret_cast<MvccHeader*>(record->data + user_size);
        if (hdr->xmax_ != INT32_MAX) {
            continue;
        }

        // 从记录中提取各列的值，拼成索引键
        int offset = 0;
        for (auto& col : index_cols) {
            memcpy(key_buf + offset, record->data + col.offset, col.len);
            offset += col.len;
        }
        // 插入索引（重复键的处理由IxNodeHandle::insert负责）
        ih->insert_entry(key_buf, rid, context->txn_);
    }
    delete[] key_buf;
    // 6. 将索引保持打开状态，存入ihs_
    std::string ix_name = ix_manager_->get_index_name(tab_name, index_cols);
    ihs_.emplace(ix_name, std::move(ih));
    // 7. 更新元数据
    IndexMeta index_meta;
    index_meta.tab_name = tab_name;
    index_meta.col_num = index_cols.size();
    index_meta.col_tot_len = key_len;
    index_meta.cols = index_cols;
    tab.indexes.push_back(index_meta);
    // 8. 持久化元数据
    flush_meta();
}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::drop_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
    // 1. 检查表是否存在
    TabMeta& tab = db_.get_table(tab_name);
    // 2. 检查索引是否存在
    auto index_iter = tab.get_index_meta(col_names);  // 不存在会抛异常
    // 3. 获取索引文件名
    std::string ix_name = ix_manager_->get_index_name(tab_name, col_names);
    // 4. 如果索引文件已打开，先关闭
    if (ihs_.count(ix_name)) {
        ix_manager_->close_index(ihs_[ix_name].get());
        ihs_.erase(ix_name);
    }
    // 5. 销毁索引文件
    ix_manager_->destroy_index(tab_name, index_iter->cols);
    // 6. 从元数据中移除
    tab.indexes.erase(index_iter);
    // 7. 持久化元数据
    flush_meta();
}

void SmManager::drop_index(const std::string& tab_name, const std::vector<ColMeta>& cols, Context* context) {
    // 将ColMeta转换为col_names后调用上面的重载
    std::vector<std::string> col_names;
    for (auto& col : cols) {
        col_names.push_back(col.name);
    }
    drop_index(tab_name, col_names, context);
}

/**
 * @description: 显示表上的所有索引
 * @param {string&} tab_name 表名称
 * @param {Context*} context
 */
void SmManager::show_index(const std::string& tab_name, Context* context) {
    TabMeta& tab = db_.get_table(tab_name);

    std::vector<std::string> captions = {"Table", "Index", "Columns"};
    RecordPrinter printer(captions.size());
    printer.print_separator(context);
    printer.print_record(captions, context);
    printer.print_separator(context);

    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);

    for (auto& index : tab.indexes) {
        // 构建列名字符串，格式为 (col1) 或 (col1, col2)
        std::string cols_str = "(";
        for (size_t i = 0; i < index.cols.size(); ++i) {
            if (i > 0) cols_str += ",";
            cols_str += index.cols[i].name;
        }
        cols_str += ")";
        // 所有索引均为唯一索引，类型显示为 "unique"
        std::string idx_type = "unique";
        printer.print_record({tab_name, idx_type, cols_str}, context);
        outfile << "| " << tab_name << " | " << idx_type << " | " << cols_str << " |\n";
    }

    printer.print_separator(context);
    outfile.close();
}

/**
 * @description: 恢复后重建所有索引
 *   crash后索引页可能丢失（仅保留数据页），需从数据表重建索引
 */
void SmManager::rebuild_all_indexes() {
    // 1. 收集所有索引信息（表名+列名）
    struct IndexInfo {
        std::string tab_name;
        std::vector<std::string> col_names;
    };
    std::vector<IndexInfo> all_indexes;
    for (auto& tab_entry : db_.tabs_) {
        for (auto& index : tab_entry.second.indexes) {
            IndexInfo info;
            info.tab_name = tab_entry.first;
            for (auto& col : index.cols) {
                info.col_names.push_back(col.name);
            }
            all_indexes.push_back(info);
        }
    }

    if (all_indexes.empty()) return;

    printf("[Recovery] 重建 %zu 个索引...\n", all_indexes.size());

    // 2. 销毁旧索引文件并清理元数据
    //    IMPORTANT: 先丢弃缓冲池中属于该索引文件的所有缓存页，
    //    防止 OS 在 destroy_file+create_index 时复用同一 fd 而导致
    //    缓冲池返回旧索引页（redo阶段的INDEX_PAGE_MODIFY可能已缓存）
    for (auto& info : all_indexes) {
        std::string ix_name = ix_manager_->get_index_name(info.tab_name, info.col_names);
        if (ihs_.count(ix_name)) {
            auto* ih = ihs_[ix_name].get();
            get_bpm()->discard_all_pages(ih->get_fd());   // 丢弃缓冲池中该fd的所有缓存页
            ix_manager_->close_index(ih);
            ihs_.erase(ix_name);
        }
        if (disk_manager_->is_file(ix_name)) {
            disk_manager_->destroy_file(ix_name);
        }
        TabMeta& tab = db_.get_table(info.tab_name);
        auto index_iter = tab.get_index_meta(info.col_names);
        if (index_iter != tab.indexes.end()) {
            tab.indexes.erase(index_iter);
        }
    }

    // 3. 重新创建索引（扫描表数据填充）
    for (auto& info : all_indexes) {
        TabMeta& tab = db_.get_table(info.tab_name);

        std::vector<ColMeta> index_cols;
        for (auto& col_name : info.col_names) {
            auto col_iter = tab.get_col(col_name);
            index_cols.push_back(*col_iter);
        }

        ix_manager_->create_index(info.tab_name, index_cols);

        int key_len = 0;
        for (auto& col : index_cols) key_len += col.len;

        auto ih = ix_manager_->open_index(info.tab_name, index_cols);
        RmFileHandle* fh = fhs_.at(info.tab_name).get();
        char* key_buf = new char[key_len];

        for (RmScan scan(fh); !scan.is_end(); scan.next()) {
            Rid rid = scan.rid();
            auto record = fh->get_record(rid, nullptr);

            // 跳过软删除记录（xmax != INT32_MAX），防止重建的索引包含
            // 已删除记录的条目。这会导致后续INSERT触发DuplicateKeyError，
            // 因为唯一索引检查（get_value）不检查MVCC可见性。
            int user_size = fh->get_user_record_size();
            MvccHeader* hdr = reinterpret_cast<MvccHeader*>(record->data + user_size);
            if (hdr->xmax_ != INT32_MAX) {
                continue;
            }

            int offset = 0;
            for (auto& col : index_cols) {
                memcpy(key_buf + offset, record->data + col.offset, col.len);
                offset += col.len;
            }
            ih->insert_entry(key_buf, rid, nullptr);
        }
        delete[] key_buf;

        std::string ix_name = ix_manager_->get_index_name(info.tab_name, info.col_names);
        ihs_.emplace(ix_name, std::move(ih));

        IndexMeta index_meta;
        index_meta.tab_name = info.tab_name;
        index_meta.col_num = static_cast<int>(index_cols.size());
        index_meta.col_tot_len = key_len;
        index_meta.cols = index_cols;
        tab.indexes.push_back(index_meta);

        printf("[Recovery]   索引 %s(%s) 重建完成\n",
               info.tab_name.c_str(), ix_name.c_str());
    }

    flush_meta();
}