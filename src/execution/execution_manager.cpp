/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "execution_manager.h"

#include "executor_delete.h"
#include "executor_index_scan.h"
#include "executor_insert.h"
#include "executor_nestedloop_join.h"
#include "executor_projection.h"
#include "executor_seq_scan.h"
#include "executor_update.h"
#include "index/ix.h"
#include "record_printer.h"
#include "recovery/log_manager.h"

const char *help_info = "Supported SQL syntax:\n"
                   "  command ;\n"
                   "command:\n"
                   "  CREATE TABLE table_name (column_name type [, column_name type ...])\n"
                   "  DROP TABLE table_name\n"
                   "  CREATE INDEX table_name (column_name)\n"
                   "  DROP INDEX table_name (column_name)\n"
                   "  INSERT INTO table_name VALUES (value [, value ...])\n"
                   "  DELETE FROM table_name [WHERE where_clause]\n"
                   "  UPDATE table_name SET column_name = value [, column_name = value ...] [WHERE where_clause]\n"
                   "  SELECT selector FROM table_name [WHERE where_clause]\n"
                   "type:\n"
                   "  {INT | FLOAT | CHAR(n)}\n"
                   "where_clause:\n"
                   "  condition [AND condition ...]\n"
                   "condition:\n"
                   "  column op {column | value}\n"
                   "column:\n"
                   "  [table_name.]column_name\n"
                   "op:\n"
                   "  {= | <> | < | > | <= | >=}\n"
                   "selector:\n"
                   "  {* | column [, column ...]}\n";

// 主要负责执行DDL语句
void QlManager::run_mutli_query(std::shared_ptr<Plan> plan, Context *context){
    if (auto x = std::dynamic_pointer_cast<DDLPlan>(plan)) {
        switch(x->tag) {
            case T_CreateTable:
            {
                sm_manager_->create_table(x->tab_name_, x->cols_, context);
                break;
            }
            case T_DropTable:
            {
                sm_manager_->drop_table(x->tab_name_, context);
                break;
            }
            case T_CreateIndex:
            {
                sm_manager_->create_index(x->tab_name_, x->tab_col_names_, context);
                break;
            }
            case T_DropIndex:
            {
                sm_manager_->drop_index(x->tab_name_, x->tab_col_names_, context);
                break;
            }
            default:
                throw InternalError("Unexpected field type");
                break;  
        }
    }
}

// 执行help; show tables; desc table; begin; commit; abort;语句
void QlManager::run_cmd_utility(std::shared_ptr<Plan> plan, txn_id_t *txn_id, Context *context) {
    // EXPLAIN 输出已在 Portal::start 中完成，此处无需额外处理
    if (auto x = std::dynamic_pointer_cast<ExplainPlan>(plan)) {
        return;
    }
    if (auto x = std::dynamic_pointer_cast<OtherPlan>(plan)) {
        switch(x->tag) {
            case T_Help:
            {
                memcpy(context->data_send_ + *(context->offset_), help_info, strlen(help_info));
                *(context->offset_) = strlen(help_info);
                break;
            }
            case T_ShowTable:
            {
                sm_manager_->show_tables(context);
                break;
            }
            case T_DescTable:
            {
                sm_manager_->desc_table(x->tab_name_, context);
                break;
            }
            case T_ShowIndex:
            {
                sm_manager_->show_index(x->tab_name_, context);
                break;
            }
            case T_Transaction_begin:
            {
                // 显示开启一个事务
                context->txn_->set_txn_mode(true);
                break;
            }  
            case T_Transaction_commit:
            {
                context->txn_ = txn_mgr_->get_transaction(*txn_id);
                txn_mgr_->commit(context->txn_, context->log_mgr_);
                break;
            }    
            case T_Transaction_rollback:
            {
                context->txn_ = txn_mgr_->get_transaction(*txn_id);
                txn_mgr_->abort(context->txn_, context->log_mgr_);
                break;
            }    
            case T_Transaction_abort:
            {
                context->txn_ = txn_mgr_->get_transaction(*txn_id);
                txn_mgr_->abort(context->txn_, context->log_mgr_);
                break;
            }
            case T_Checkpoint:
            {
                // 静态检查点：先收集DPT → 刷盘所有脏页 → 写检查点日志
                // 关键：DPT 必须在刷盘**前**收集，记录刷盘前最后一刻的脏页快照
                // 使用表名+页号代替fd+页号，确保恢复时能正确定位页面
                std::vector<std::tuple<std::string, page_id_t, lsn_t>> dpt;
                if (context->log_mgr_ != nullptr) {
                    // 1. 收集DPT（刷盘前的脏页快照），返回(PageId, lsn)
                    auto raw_dpt = sm_manager_->get_bpm()->collect_dirty_pages();

                    // 构建fd→表名的反向映射
                    std::unordered_map<int, std::string> fd_to_table;
                    for (auto& [tab_name, fh] : sm_manager_->fhs_) {
                        fd_to_table[fh->GetFd()] = tab_name;
                    }

                    // 转换为(表名, page_no, lsn)格式，恢复时不依赖fd
                    for (auto& [pid, lsn] : raw_dpt) {
                        auto it = fd_to_table.find(pid.fd);
                        if (it != fd_to_table.end()) {
                            dpt.emplace_back(it->second, pid.page_no, lsn);
                        }
                    }
                }

                // 2. 遍历所有打开的表文件：写回文件头 + 刷盘脏页 + fsync
                //    文件头（含 num_pages）必须持久化，因为 create_new_page_handle()
                //    只更新内存中的 num_pages 而不写磁盘，crash 后文件头是旧值
                for (auto& [tab_name, fh] : sm_manager_->fhs_) {
                    fh->sync_file_header();                                  // 写回文件头
                    sm_manager_->get_bpm()->flush_all_pages(fh->GetFd());    // 刷盘数据页
                    sm_manager_->get_disk_manager()->sync_file(fh->GetFd()); // fsync
                }

                // 3. 写检查点日志（记录当前ATT和DPT快照）
                if (context->log_mgr_ != nullptr) {
                    // 收集ATT
                    std::vector<std::pair<txn_id_t, lsn_t>> att;
                    for (auto& [tid, txn] : txn_mgr_->txn_map) {
                        if (txn->get_state() != TransactionState::COMMITTED &&
                            txn->get_state() != TransactionState::ABORTED) {
                            att.emplace_back(tid, txn->get_prev_lsn());
                        }
                    }

                    auto* ckpt = new CheckpointLogRecord();
                    ckpt->set_att(att);
                    ckpt->set_dpt(dpt);  // 刷盘前的脏页快照（表名格式）
                    context->log_mgr_->add_log_to_buffer(ckpt);
                    context->log_mgr_->flush_log_to_disk();
                    delete ckpt;
                }
                // 返回成功信息
                std::string msg = "Static checkpoint created successfully.\n";
                memcpy(context->data_send_ + *(context->offset_), msg.c_str(), msg.length());
                *(context->offset_) = msg.length();
                break;
            }
            default:
                throw InternalError("Unexpected field type");
                break;                        
        }

    } else if(auto x = std::dynamic_pointer_cast<SetKnobPlan>(plan)) {
        switch (x->set_knob_type_)
        {
        case ast::SetKnobType::EnableNestLoop: {
            planner_->set_enable_nestedloop_join(x->bool_value_);
            break;
        }
        case ast::SetKnobType::EnableSortMerge: {
            planner_->set_enable_sortmerge_join(x->bool_value_);
            break;
        }
        default: {
            throw RMDBError("Not implemented!\n");
            break;
        }
        }
    }
}

// 执行select语句，select语句的输出除了需要返回客户端外，还需要写入output.txt文件中
void QlManager::select_from(std::unique_ptr<AbstractExecutor> executorTreeRoot, std::vector<TabCol> sel_cols, 
                            Context *context) {
    std::vector<std::string> captions;
    captions.reserve(sel_cols.size());
    for (auto &sel_col : sel_cols) {
        captions.push_back(sel_col.col_name);
    }

    // Print header into buffer
    RecordPrinter rec_printer(sel_cols.size());
    rec_printer.print_separator(context);
    rec_printer.print_record(captions, context);
    rec_printer.print_separator(context);
    // print header into file
    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    outfile << "|";
    for(int i = 0; i < captions.size(); ++i) {
        outfile << " " << captions[i] << " |";
    }
    outfile << "\n";

    // Print records
    size_t num_rec = 0;
    // 执行query_plan
    for (executorTreeRoot->beginTuple(); !executorTreeRoot->is_end(); executorTreeRoot->nextTuple()) {
        auto Tuple = executorTreeRoot->Next();
        std::vector<std::string> columns;
        for (auto &col : executorTreeRoot->cols()) {
            std::string col_str;
            char *rec_buf = Tuple->data + col.offset;
            if (col.type == TYPE_INT) {
                col_str = std::to_string(*(int *)rec_buf);
            } else if (col.type == TYPE_BIGINT) {
                col_str = std::to_string(*(int64_t *)rec_buf);
            } else if (col.type == TYPE_DATETIME) {
                col_str = datetime_format(*(int64_t *)rec_buf);
            } else if (col.type == TYPE_FLOAT) {
                // 6 位小数精度
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(6) << *(float *)rec_buf;
                col_str = oss.str();
            } else if (col.type == TYPE_STRING) {
                col_str = std::string((char *)rec_buf, col.len);
                col_str.resize(strlen(col_str.c_str()));
            }
            columns.push_back(col_str);
        }
        // print record into buffer
        rec_printer.print_record(columns, context);
        // print record into file
        outfile << "|";
        for(int i = 0; i < columns.size(); ++i) {
            outfile << " " << columns[i] << " |";
        }
        outfile << "\n";
        num_rec++;
    }
    outfile.close();
    // Print footer into buffer
    rec_printer.print_separator(context);
    // Print record count into buffer
    RecordPrinter::print_record_count(num_rec, context);
}

// 执行DML语句
void QlManager::run_dml(std::unique_ptr<AbstractExecutor> exec){
    exec->Next();
}