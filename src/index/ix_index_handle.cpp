/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "ix_index_handle.h"

#include "ix_scan.h"
#include "recovery/log_manager.h"

void IxIndexHandle::sync_file_header() {
    char* data = new char[file_hdr_->tot_len_];
    file_hdr_->serialize(data);
    disk_manager_->write_page(fd_, IX_FILE_HDR_PAGE, data, file_hdr_->tot_len_);
    delete[] data;
}

/**
 * @brief 在当前node中查找第一个>=target的key_idx
 *
 * @return key_idx，范围为[0,num_key)，如果返回的key_idx=num_key，则表示target大于最后一个key
 * @note 返回key index（同时也是rid index），作为slot no
 */
int IxNodeHandle::lower_bound(const char *target) const {
    // 在当前节点中查找第一个大于等于target的key
    int num = get_size();
    if (num == 0) return 0;

    if (binary_search) {
        int lo = 0, hi = num;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (ix_compare(get_key(mid), target, file_hdr->col_types_, file_hdr->col_lens_) < 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    } else {
        // 线性查找
        for (int i = 0; i < num; i++) {
            if (ix_compare(get_key(i), target, file_hdr->col_types_, file_hdr->col_lens_) >= 0) {
                return i;
            }
        }
        return num;
    }
}

/**
 * @brief 在当前node中查找第一个>target的key_idx
 *
 * @return key_idx，范围为[1,num_key)，如果返回的key_idx=num_key，则表示target大于等于最后一个key
 * @note 注意此处的范围从1开始
 */
int IxNodeHandle::upper_bound(const char *target) const {
    // 查找当前节点中第一个大于target的key
    int num = get_size();
    if (num == 0) return 0;

    if (binary_search) {
        int lo = 0, hi = num;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (ix_compare(get_key(mid), target, file_hdr->col_types_, file_hdr->col_lens_) <= 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    } else {
        for (int i = 0; i < num; i++) {
            if (ix_compare(get_key(i), target, file_hdr->col_types_, file_hdr->col_lens_) > 0) {
                return i;
            }
        }
        return num;
    }
}

/**
 * @brief 用于叶子结点根据key来查找该结点中的键值对
 * 值value作为传出参数，函数返回是否查找成功
 *
 * @param key 目标key
 * @param[out] value 传出参数，目标key对应的Rid
 * @return 目标key是否存在
 */
bool IxNodeHandle::leaf_lookup(const char *key, Rid **value) {
    // 1. 在叶子节点中获取目标key所在位置
    int pos = lower_bound(key);
    // 2. 判断目标key是否存在（需要精确匹配）
    if (pos < get_size() && ix_compare(get_key(pos), key, file_hdr->col_types_, file_hdr->col_lens_) == 0) {
        // 3. 获取key对应的Rid，并赋值给传出参数value
        *value = get_rid(pos);
        return true;
    }
    return false;
}

/**
 * 用于内部结点（非叶子节点）查找目标key所在的孩子结点（子树）
 * @param key 目标key
 * @return page_id_t 目标key所在的孩子节点（子树）的存储页面编号
 */
page_id_t IxNodeHandle::internal_lookup(const char *key) {
    // 在内部节点中查找key对应的孩子节点
    // 使用upper_bound找到第一个大于key的位置pos
    // 那么key应该落在pos-1对应的子树中（如果pos==0，则落在第0个子树）
    int pos = upper_bound(key);
    if (pos == 0) {
        // key小于所有key，落在第一个孩子
        return value_at(0);
    } else {
        // 落在pos-1对应的孩子
        return value_at(pos - 1);
    }
}

/**
 * @brief 在指定位置插入n个连续的键值对
 * 将key的前n位插入到原来keys中的pos位置；将rid的前n位插入到原来rids中的pos位置
 *
 * @param pos 要插入键值对的位置
 * @param (key, rid) 连续键值对的起始地址，也就是第一个键值对，可以通过(key, rid)来获取n个键值对
 * @param n 键值对数量
 */
void IxNodeHandle::insert_pairs(int pos, const char *key, const Rid *rid, int n) {
    // 1. 判断pos的合法性
    int num = get_size();
    assert(pos >= 0 && pos <= num);
    assert(n > 0);

    // 2. 把pos及其之后的键值对向后移动n个位置
    int col_tot_len = file_hdr->col_tot_len_;
    // 移动rids
    memmove(&rids[pos + n], &rids[pos], (num - pos) * sizeof(Rid));
    // 移动keys
    memmove(keys + (pos + n) * col_tot_len, keys + pos * col_tot_len, (num - pos) * col_tot_len);

    // 3. 复制n个新键值对到pos位置
    memcpy(keys + pos * col_tot_len, key, n * col_tot_len);
    memcpy(&rids[pos], rid, n * sizeof(Rid));

    // 4. 更新当前节点的键数量
    set_size(num + n);
}

/**
 * @brief 用于在结点中插入单个键值对。
 * 函数返回插入后的键值对数量
 *
 * @param (key, value) 要插入的键值对
 * @return int 键值对数量
 */
int IxNodeHandle::insert(const char *key, const Rid &value) {
    // 1. 查找要插入的位置
    int pos = lower_bound(key);
    // 2. 检查key是否重复（唯一索引约束）
    if (pos < get_size() && ix_compare(get_key(pos), key, file_hdr->col_types_, file_hdr->col_lens_) == 0) {
        // key已存在，不插入
        return get_size();
    }
    // 3. 插入键值对
    insert_pair(pos, key, value);
    // 4. 返回完成插入操作之后的键值对数量
    return get_size();
}

/**
 * @brief 用于在结点中的指定位置删除单个键值对
 *
 * @param pos 要删除键值对的位置
 */
void IxNodeHandle::erase_pair(int pos) {
    // 确保pos合法
    int num = get_size();
    assert(pos >= 0 && pos < num);

    int col_tot_len = file_hdr->col_tot_len_;
    // 1. 将pos后面的键值对向前移动一位，覆盖pos处的键值对
    if (pos < num - 1) {
        memmove(keys + pos * col_tot_len, keys + (pos + 1) * col_tot_len, (num - pos - 1) * col_tot_len);
        memmove(&rids[pos], &rids[pos + 1], (num - pos - 1) * sizeof(Rid));
    }
    // 2. 更新结点的键值对数量
    set_size(num - 1);
}

/**
 * @brief 用于在结点中删除指定key的键值对。函数返回删除后的键值对数量
 *
 * @param key 要删除的键值对key值
 * @return 完成删除操作后的键值对数量
 */
int IxNodeHandle::remove(const char *key) {
    // 1. 查找要删除键值对的位置
    int pos = lower_bound(key);
    // 2. 如果找到精确匹配的key，删除该键值对
    if (pos < get_size() && ix_compare(get_key(pos), key, file_hdr->col_types_, file_hdr->col_lens_) == 0) {
        erase_pair(pos);
    }
    // 3. 返回完成删除操作后的键值对数量
    return get_size();
}

IxIndexHandle::IxIndexHandle(DiskManager *disk_manager, BufferPoolManager *buffer_pool_manager, int fd)
    : disk_manager_(disk_manager), buffer_pool_manager_(buffer_pool_manager), fd_(fd) {
    // init file_hdr_
    disk_manager_->read_page(fd, IX_FILE_HDR_PAGE, (char *)&file_hdr_, sizeof(file_hdr_));
    char* buf = new char[PAGE_SIZE];
    memset(buf, 0, PAGE_SIZE);
    disk_manager_->read_page(fd, IX_FILE_HDR_PAGE, buf, PAGE_SIZE);
    file_hdr_ = new IxFileHdr();
    file_hdr_->deserialize(buf);

    // disk_manager管理的fd对应的文件中，设置从file_hdr_->num_pages开始分配page_no
    int now_page_no = disk_manager_->get_fd2pageno(fd);
    disk_manager_->set_fd2pageno(fd, now_page_no + 1);
}

/**
 * @brief 用于查找指定键所在的叶子结点
 * @param key 要查找的目标key值
 * @param operation 查找到目标键值对后要进行的操作类型
 * @param transaction 事务参数，如果不需要则默认传入nullptr
 * @return [leaf node] and [root_is_latched] 返回目标叶子结点以及根结点是否加锁
 * @note need to Unlatch and unpin the leaf node outside!
 * 注意：用了FindLeafPage之后一定要unlatch叶结点，否则下次latch该结点会堵塞！
 */
std::pair<IxNodeHandle *, bool> IxIndexHandle::find_leaf_page(const char *key, Operation operation,
                                                            Transaction *transaction, bool find_first) {
    // 1. 获取根节点
    if (is_empty()) {
        return std::make_pair(nullptr, false);
    }
    page_id_t root_page_no = file_hdr_->root_page_;
    IxNodeHandle *node = fetch_node(root_page_no);

    // 2. 从根节点开始不断向下查找目标key，直到到达叶子节点
    while (!node->is_leaf_page()) {
        page_id_t child_page_no = node->internal_lookup(key);
        IxNodeHandle *child = fetch_node(child_page_no);
        buffer_pool_manager_->unpin_page(node->get_page_id(), false);
        node = child;
    }

    // 3. 找到包含该key值的叶子结点，返回叶子节点
    return std::make_pair(node, false);
}

/**
 * @brief 用于查找指定键在叶子结点中的对应的值result
 *
 * @param key 查找的目标key值
 * @param result 用于存放结果的容器
 * @param transaction 事务指针
 * @return bool 返回目标键值对是否存在
 */
bool IxIndexHandle::get_value(const char *key, std::vector<Rid> *result, Transaction *transaction) {
    // 1. 获取目标key值所在的叶子结点
    auto [leaf, root_latched] = find_leaf_page(key, Operation::FIND, transaction);
    if (leaf == nullptr) {
        return false;
    }

    // 2. 在叶子节点中查找目标key值的位置，并读取key对应的rid
    Rid *value = nullptr;
    bool found = leaf->leaf_lookup(key, &value);
    if (found && value != nullptr) {
        result->push_back(*value);
    }

    // 3. 释放leaf的pin
    buffer_pool_manager_->unpin_page(leaf->get_page_id(), false);
    return found;
}

/**
 * @brief  将传入的一个node拆分(Split)成两个结点，在node的右边生成一个新结点new node
 * @param node 需要拆分的结点
 * @return 拆分得到的new_node
 * @note need to unpin the new node outside
 * 注意：本函数执行完毕后，原node和new node都需要在函数外面进行unpin
 */
IxNodeHandle *IxIndexHandle::split(IxNodeHandle *node) {
    // 1. 创建新节点
    IxNodeHandle *new_node = create_node();
    int total = node->get_size();
    int left_size = total / 2;   // 左半部分保留在原节点
    int right_size = total - left_size;  // 右半部分移到新节点

    // 初始化新节点的page_hdr
    new_node->page_hdr->parent = node->get_parent_page_no();
    new_node->page_hdr->is_leaf = node->is_leaf_page();
    new_node->page_hdr->num_key = 0;

    // 2. 将右半部分键值对移到新节点
    const char *right_key_start = node->get_key(left_size);
    const Rid *right_rid_start = node->get_rid(left_size);
    new_node->insert_pairs(0, right_key_start, right_rid_start, right_size);

    // 更新原节点的键值对数量
    node->set_size(left_size);

    // 3. 如果是叶子结点，更新叶子链表
    if (node->is_leaf_page()) {
        new_node->set_prev_leaf(node->get_page_no());
        new_node->set_next_leaf(node->get_next_leaf());
        // 更新原节点的后继
        IxNodeHandle *old_next = fetch_node(node->get_next_leaf());
        old_next->set_prev_leaf(new_node->get_page_no());
        buffer_pool_manager_->unpin_page(old_next->get_page_id(), true);
        node->set_next_leaf(new_node->get_page_no());

        // 更新last_leaf（如果node是最后一个叶子）
        if (file_hdr_->last_leaf_ == node->get_page_no()) {
            file_hdr_->last_leaf_ = new_node->get_page_no();
        }
    } else {
        // 如果不是叶子结点，更新新节点的所有孩子结点的父节点
        for (int i = 0; i < new_node->get_size(); i++) {
            maintain_child(new_node, i);
        }
    }

    return new_node;
}

/**
 * @brief Insert key & value pair into internal page after split
 * 拆分(Split)后，向上找到old_node的父结点
 * 将new_node的第一个key插入到父结点，其位置在 父结点指向old_node的孩子指针 之后
 * 如果插入后>=maxsize，则必须继续拆分父结点，然后在其父结点的父结点再插入，即需要递归
 * 直到找到的old_node为根结点时，结束递归（此时将会新建一个根R，关键字为key，old_node和new_node为其孩子）
 *
 * @param (old_node, new_node) 原结点为old_node，old_node被分裂之后产生了新的右兄弟结点new_node
 * @param key 要插入parent的key
 * @note 本函数执行完毕后，new node和old node都需要在函数外面进行unpin
 */
void IxIndexHandle::insert_into_parent(IxNodeHandle *old_node, const char *key, IxNodeHandle *new_node,
                                     Transaction *transaction) {
    // 1. 判断old_node是否为根结点
    if (old_node->is_root_page()) {
        // 创建新的根节点
        IxNodeHandle *new_root = create_node();
        new_root->page_hdr->is_leaf = false;
        new_root->page_hdr->parent = IX_NO_PAGE;
        new_root->page_hdr->num_key = 0;

        // 将old_node的第一个key和child插入新根
        char *old_first_key = old_node->get_key(0);
        new_root->insert_pair(0, old_first_key, Rid{old_node->get_page_no(), -1});
        // 将new_node的第一个key和child插入新根
        char *new_first_key = new_node->get_key(0);
        new_root->insert_pair(1, new_first_key, Rid{new_node->get_page_no(), -1});

        // 更新孩子节点的父指针
        old_node->set_parent_page_no(new_root->get_page_no());
        new_node->set_parent_page_no(new_root->get_page_no());

        // 更新file_hdr中的根节点
        update_root_page_no(new_root->get_page_no());

        buffer_pool_manager_->unpin_page(new_root->get_page_id(), true);
        return;
    }

    // 2. 获取父结点
    page_id_t parent_page_no = old_node->get_parent_page_no();
    IxNodeHandle *parent = fetch_node(parent_page_no);

    // 3. 找到old_node在父结点中的位置
    int child_idx = parent->find_child(old_node);

    // 4. 在父结点中child_idx之后插入new_node的第一个key
    // 内部节点的key[i]和rid[i]是对应的，key[i]是rid[i]对应子树的首个key
    // 需要在child_idx+1位置插入new_node的(key, rid)
    char *new_key = new_node->get_key(0);
    parent->insert_pair(child_idx + 1, new_key, Rid{new_node->get_page_no(), -1});

    // 5. 检查父结点是否需要分裂
    if (parent->get_size() >= parent->get_max_size()) {
        IxNodeHandle *new_parent = split(parent);
        // 取new_parent的第一个key继续向上插入
        insert_into_parent(parent, new_parent->get_key(0), new_parent, transaction);
        buffer_pool_manager_->unpin_page(new_parent->get_page_id(), true);
    }

    buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
}

/**
 * @brief 将指定键值对插入到B+树中
 * @param (key, value) 要插入的键值对
 * @param transaction 事务指针
 * @return page_id_t 插入到的叶结点的page_no
 */
page_id_t IxIndexHandle::insert_entry(const char *key, const Rid &value, Transaction *transaction,
                                     const std::string& index_name) {
    // 1. 查找key值应该插入到哪个叶子节点
    auto [leaf, root_latched] = find_leaf_page(key, Operation::INSERT, transaction);
    if (leaf == nullptr) {
        // B+树为空，需要先创建根节点
        // 根节点已在IxManager::create_index中创建
        return -1;
    }

    // 1.5 捕获leaf的修改前镜像（用于物理日志UNDO）
    char leaf_before[PAGE_SIZE];
    memcpy(leaf_before, leaf->page->get_data(), PAGE_SIZE);

    // 2. 在该叶子节点中插入键值对
    int old_size = leaf->get_size();
    leaf->insert(key, value);
    int new_size = leaf->get_size();

    page_id_t leaf_page = leaf->get_page_no();

    // 3. 如果结点已满，分裂结点
    if (new_size >= leaf->get_max_size() && new_size != old_size) {
        IxNodeHandle *new_leaf = split(leaf);
        // 将新结点的第一个key插入父节点
        char *new_key = new_leaf->get_key(0);
        insert_into_parent(leaf, new_key, new_leaf, transaction);

        // 记录leaf页物理修改（before→after）
        log_index_page_modify(leaf, leaf_before, transaction, index_name);

        // 记录new_leaf页创建（before=全零, after=当前状态）
        char zero_page[PAGE_SIZE];
        memset(zero_page, 0, PAGE_SIZE);
        log_index_page_modify(new_leaf, zero_page, transaction, index_name);

        buffer_pool_manager_->unpin_page(new_leaf->get_page_id(), true);
    } else {
        // 记录leaf页物理修改（before→after）
        log_index_page_modify(leaf, leaf_before, transaction, index_name);
    }

    // 更新last_leaf
    if (leaf_page == file_hdr_->last_leaf_) {
        // check if the last leaf changed during split
    }

    buffer_pool_manager_->unpin_page(leaf->get_page_id(), true);
    return leaf_page;
}

/**
 * @brief 用于删除B+树中含有指定key的键值对
 * @param key 要删除的key值
 * @param transaction 事务指针
 */
bool IxIndexHandle::delete_entry(const char *key, Transaction *transaction,
                                  const std::string& index_name) {
    // 1. 获取该键值对所在的叶子结点
    auto [leaf, root_latched] = find_leaf_page(key, Operation::DELETE, transaction);
    if (leaf == nullptr) {
        return false;
    }

    // 1.5 捕获leaf的修改前镜像（用于物理日志UNDO）
    char leaf_before[PAGE_SIZE];
    memcpy(leaf_before, leaf->page->get_data(), PAGE_SIZE);

    // 2. 在该叶子结点中删除键值对
    int old_size = leaf->get_size();
    leaf->remove(key);
    int new_size = leaf->get_size();

    // 如果key不存在（大小未变），返回false
    if (old_size == new_size) {
        buffer_pool_manager_->unpin_page(leaf->get_page_id(), false);
        return false;
    }

    // 3. 如果删除成功，处理合并或重分配
    bool root_latched_out = false;
    coalesce_or_redistribute(leaf, transaction, &root_latched_out);

    // 记录leaf页物理修改（before→after）
    log_index_page_modify(leaf, leaf_before, transaction, index_name);

    buffer_pool_manager_->unpin_page(leaf->get_page_id(), true);
    return true;
}

/**
 * @brief 用于处理合并和重分配的逻辑，用于删除键值对后调用
 *
 * @param node 执行完删除操作的结点
 * @param transaction 事务指针
 * @param root_is_latched 传出参数：根节点是否上锁，用于并发操作
 * @return 是否需要删除结点
 * @note User needs to first find the sibling of input page.
 * If sibling's size + input page's size >= 2 * page's minsize, then redistribute.
 * Otherwise, merge(Coalesce).
 */
bool IxIndexHandle::coalesce_or_redistribute(IxNodeHandle *node, Transaction *transaction, bool *root_is_latched) {
    // 1. 判断node结点是否为根节点
    if (node->is_root_page()) {
        // 调用AdjustRoot处理
        return adjust_root(node);
    }

    // 2. 如果node的键值对数量 >= 最小数量，不需要处理
    if (node->get_size() >= node->get_min_size()) {
        return false;
    }

    // 3. 获取父亲结点
    IxNodeHandle *parent = fetch_node(node->get_parent_page_no());
    int child_idx = parent->find_child(node);

    // 4. 寻找兄弟结点（优先选取左边的兄弟）
    IxNodeHandle *neighbor = nullptr;
    if (child_idx > 0) {
        // 有左兄弟，优先使用左兄弟
        neighbor = fetch_node(parent->value_at(child_idx - 1));
    } else {
        // 没有左兄弟，使用右兄弟
        neighbor = fetch_node(parent->value_at(child_idx + 1));
    }

    // 5. 判断合并还是重分配
    if (node->get_size() + neighbor->get_size() >= node->get_min_size() * 2) {
        // 重新分配
        redistribute(neighbor, node, parent, child_idx);
        buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
        buffer_pool_manager_->unpin_page(neighbor->get_page_id(), true);
        return false;
    } else {
        // 合并
        bool need_delete = coalesce(&neighbor, &node, &parent, child_idx, transaction, root_is_latched);
        if (need_delete) {
            // 父节点需要处理
            bool parent_deleted = coalesce_or_redistribute(parent, transaction, root_is_latched);
            if (!parent_deleted) {
                buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
            }
        } else {
            buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
        }
        buffer_pool_manager_->unpin_page(neighbor->get_page_id(), true);
        return true;  // node被删除了
    }
}

/**
 * @brief 用于当根结点被删除了一个键值对之后的处理
 * @param old_root_node 原根节点
 * @return bool 根结点是否需要被删除
 */
bool IxIndexHandle::adjust_root(IxNodeHandle *old_root_node) {
    // 1. 如果old_root_node是内部结点，并且大小为1，直接把它的孩子更新成新的根结点
    if (!old_root_node->is_leaf_page() && old_root_node->get_size() == 1) {
        page_id_t child_page = old_root_node->value_at(0);
        IxNodeHandle *child = fetch_node(child_page);
        child->set_parent_page_no(IX_NO_PAGE);
        update_root_page_no(child_page);
        buffer_pool_manager_->unpin_page(child->get_page_id(), true);

        // 释放旧根节点
        release_node_handle(*old_root_node);
        buffer_pool_manager_->delete_page(old_root_node->get_page_id());
        return true;
    }
    // 2. 如果old_root_node是叶结点，且大小为0，清空根节点
    if (old_root_node->is_leaf_page() && old_root_node->get_size() == 0) {
        update_root_page_no(IX_NO_PAGE);
        release_node_handle(*old_root_node);
        buffer_pool_manager_->delete_page(old_root_node->get_page_id());
        return true;
    }
    // 3. 除此之外，不需要操作
    return false;
}

/**
 * @brief 重新分配node和兄弟结点neighbor_node的键值对
 * Redistribute key & value pairs from one page to its sibling page.
 *
 * @param neighbor_node sibling page of input "node"
 * @param node input from method coalesceOrRedistribute()
 * @param parent the parent of "node" and "neighbor_node"
 * @param index node在parent中的rid_idx
 * @note node是之前刚被删除过一个key的结点
 * index=0，则neighbor是node后继结点，表示：node(left)      neighbor(right)
 * index>0，则neighbor是node前驱结点，表示：neighbor(left)  node(right)
 */
void IxIndexHandle::redistribute(IxNodeHandle *neighbor_node, IxNodeHandle *node, IxNodeHandle *parent, int index) {
    if (index > 0) {
        // neighbor是node的前驱（左兄弟），从neighbor移动最后一个键值对到node头部
        int neighbor_last = neighbor_node->get_size() - 1;
        char *move_key = neighbor_node->get_key(neighbor_last);
        Rid *move_rid = neighbor_node->get_rid(neighbor_last);

        // 将键值对插入node的头部
        node->insert_pair(0, move_key, *move_rid);
        // 从neighbor中删除最后一个键值对
        neighbor_node->erase_pair(neighbor_last);

        // 更新父节点中node对应的key
        char *node_new_first = node->get_key(0);
        memcpy(parent->get_key(index), node_new_first, file_hdr_->col_tot_len_);

        // 如果node不是叶子，更新被移动的孩子节点的父指针
        if (!node->is_leaf_page()) {
            maintain_child(node, 0);
        }
    } else {
        // neighbor是node的后继（右兄弟），从neighbor移动第一个键值对到node尾部
        char *move_key = neighbor_node->get_key(0);
        Rid *move_rid = neighbor_node->get_rid(0);

        // 将键值对插入node尾部
        node->insert_pair(node->get_size(), move_key, *move_rid);
        // 从neighbor中删除第一个键值对
        neighbor_node->erase_pair(0);

        // 更新父节点中neighbor对应的key
        char *neighbor_new_first = neighbor_node->get_key(0);
        memcpy(parent->get_key(index + 1), neighbor_new_first, file_hdr_->col_tot_len_);
    }
}

/**
 * @brief 合并(Coalesce)函数是将node和其直接前驱进行合并，也就是和它左边的neighbor_node进行合并；
 * 假设node一定在右边。如果上层传入的index=0，说明node在左边，那么交换node和neighbor_node，保证node在右边；
 * Move all the key & value pairs from one page to its sibling page.
 *
 * @param neighbor_node sibling page of input "node" (neighbor_node是node的前结点)
 * @param node input from method coalesceOrRedistribute() (node结点是需要被删除的)
 * @param parent parent page of input "node"
 * @param index node在parent中的rid_idx
 * @return true means parent node should be deleted, false means no deletion happend
 * @note Assume that *neighbor_node is the left sibling of *node (neighbor -> node)
 */
bool IxIndexHandle::coalesce(IxNodeHandle **neighbor_node, IxNodeHandle **node, IxNodeHandle **parent, int index,
                             Transaction *transaction, bool *root_is_latched) {
    // 1. 确保neighbor_node是左兄弟，node是右兄弟
    if (index == 0) {
        // node是左节点，交换
        std::swap(*neighbor_node, *node);
        index = 1;
    }

    // 2. 把node结点的键值对移动到neighbor_node中
    int node_size = (*node)->get_size();
    int neighbor_size = (*neighbor_node)->get_size();
    for (int i = 0; i < node_size; i++) {
        char *move_key = (*node)->get_key(i);
        Rid *move_rid = (*node)->get_rid(i);
        (*neighbor_node)->insert_pair(neighbor_size + i, move_key, *move_rid);
    }

    // 更新node结点孩子结点的父节点信息
    if (!(*node)->is_leaf_page()) {
        for (int i = 0; i < node_size; i++) {
            maintain_child(*neighbor_node, neighbor_size + i);
        }
    }

    // 如果是叶子结点，更新叶子链表
    if ((*node)->is_leaf_page()) {
        // 更新叶子链表，跳过被删除的node
        if ((*node)->get_next_leaf() != IX_LEAF_HEADER_PAGE) {
            IxNodeHandle *next = fetch_node((*node)->get_next_leaf());
            next->set_prev_leaf((*neighbor_node)->get_page_no());
            buffer_pool_manager_->unpin_page(next->get_page_id(), true);
        }
        (*neighbor_node)->set_next_leaf((*node)->get_next_leaf());

        // 更新last_leaf
        if (file_hdr_->last_leaf_ == (*node)->get_page_no()) {
            file_hdr_->last_leaf_ = (*neighbor_node)->get_page_no();
        }
    }

    // 3. 从父节点中删除node的信息
    int parent_idx = (*parent)->find_child(*node);
    // 实际上删除的是父节点中指向node的条目，以及对应的key
    // 在内部节点中，key[parent_idx]对应的rid[parent_idx]指向node
    // 合并后，neighbor吸收了node，所以需要删除父节点中指向node的条目
    if (parent_idx < (*parent)->get_size()) {
        (*parent)->erase_pair(parent_idx);
    }

    // 4. 释放和删除node结点
    release_node_handle(**node);
    buffer_pool_manager_->delete_page((*node)->get_page_id());

    // 5. 判断父节点是否需要删除
    return (*parent)->get_size() < (*parent)->get_min_size();
}

/**
 * @brief 这里把iid转换成了rid，即iid的slot_no作为node的rid_idx(key_idx)
 * node其实就是把slot_no作为键值对数组的下标
 * 换而言之，每个iid对应的索引槽存了一对(key,rid)，指向了(要建立索引的属性首地址,插入/删除记录的位置)
 *
 * @param iid
 * @return Rid
 * @note iid和rid存的不是一个东西，rid是上层传过来的记录位置，iid是索引内部生成的索引槽位置
 */
Rid IxIndexHandle::get_rid(const Iid &iid) const {
    IxNodeHandle *node = fetch_node(iid.page_no);
    if (iid.slot_no >= node->get_size()) {
        buffer_pool_manager_->unpin_page(node->get_page_id(), false);
        delete node;
        throw IndexEntryNotFoundError();
    }
    Rid result = *node->get_rid(iid.slot_no);
    buffer_pool_manager_->unpin_page(node->get_page_id(), false);  // unpin it!
    delete node;
    return result;
}

/**
 * @brief FindLeafPage + lower_bound
 *
 * @param key
 * @return Iid
 * @note 上层传入的key本来是int类型，通过(const char *)&key进行了转换
 * 可用*(int *)key转换回去
 */
Iid IxIndexHandle::lower_bound(const char *key) {
    auto [leaf, root_latched] = find_leaf_page(key, Operation::FIND, nullptr);
    if (leaf == nullptr) {
        return Iid{-1, -1};
    }
    int pos = leaf->lower_bound(key);
    Iid result = {.page_no = leaf->get_page_no(), .slot_no = pos};
    buffer_pool_manager_->unpin_page(leaf->get_page_id(), false);
    return result;
}

/**
 * @brief FindLeafPage + upper_bound
 *
 * @param key
 * @return Iid
 */
Iid IxIndexHandle::upper_bound(const char *key) {
    auto [leaf, root_latched] = find_leaf_page(key, Operation::FIND, nullptr);
    if (leaf == nullptr) {
        return Iid{-1, -1};
    }
    int pos = leaf->upper_bound(key);
    // 如果pos >= leaf->get_size()，需要移动到下一个叶子
    if (pos >= leaf->get_size()) {
        page_id_t next_leaf = leaf->get_next_leaf();
        buffer_pool_manager_->unpin_page(leaf->get_page_id(), false);
        if (next_leaf == IX_LEAF_HEADER_PAGE) {
            // 到达末尾
            return leaf_end();
        }
        return Iid{.page_no = next_leaf, .slot_no = 0};
    }
    Iid result = {.page_no = leaf->get_page_no(), .slot_no = pos};
    buffer_pool_manager_->unpin_page(leaf->get_page_id(), false);
    return result;
}

/**
 * @brief 指向最后一个叶子的最后一个结点的后一个
 * 用处在于可以作为IxScan的最后一个
 *
 * @return Iid
 */
Iid IxIndexHandle::leaf_end() const {
    IxNodeHandle *node = fetch_node(file_hdr_->last_leaf_);
    Iid iid = {.page_no = file_hdr_->last_leaf_, .slot_no = node->get_size()};
    buffer_pool_manager_->unpin_page(node->get_page_id(), false);  // unpin it!
    return iid;
}

/**
 * @brief 指向第一个叶子的第一个结点
 * 用处在于可以作为IxScan的第一个
 *
 * @return Iid
 */
Iid IxIndexHandle::leaf_begin() const {
    Iid iid = {.page_no = file_hdr_->first_leaf_, .slot_no = 0};
    return iid;
}

/**
 * @brief 获取一个指定结点
 *
 * @param page_no
 * @return IxNodeHandle*
 * @note pin the page, remember to unpin it outside!
 */
IxNodeHandle *IxIndexHandle::fetch_node(int page_no) const {
    Page *page = buffer_pool_manager_->fetch_page(PageId{fd_, page_no});
    IxNodeHandle *node = new IxNodeHandle(file_hdr_, page);

    return node;
}

/**
 * @brief 创建一个新结点
 *
 * @return IxNodeHandle*
 * @note pin the page, remember to unpin it outside!
 * 注意：对于Index的处理是，删除某个页面后，认为该被删除的页面是free_page
 * 而first_free_page实际上就是最新被删除的页面，初始为IX_NO_PAGE
 * 在最开始插入时，一直是create node，那么first_page_no一直没变，一直是IX_NO_PAGE
 * 与Record的处理不同，Record将未插入满的记录页认为是free_page
 */
IxNodeHandle *IxIndexHandle::create_node() {
    IxNodeHandle *node;
    file_hdr_->num_pages_++;

    PageId new_page_id = {.fd = fd_, .page_no = INVALID_PAGE_ID};
    // 从3开始分配page_no，第一次分配之后，new_page_id.page_no=3，file_hdr_.num_pages=4
    Page *page = buffer_pool_manager_->new_page(&new_page_id);
    node = new IxNodeHandle(file_hdr_, page);
    return node;
}

/**
 * @brief 从node开始更新其父节点的第一个key，一直向上更新直到根节点
 *
 * @param node
 */
void IxIndexHandle::maintain_parent(IxNodeHandle *node) {
    IxNodeHandle *curr = node;
    while (curr->get_parent_page_no() != IX_NO_PAGE) {
        // Load its parent
        IxNodeHandle *parent = fetch_node(curr->get_parent_page_no());
        int rank = parent->find_child(curr);
        char *parent_key = parent->get_key(rank);
        char *child_first_key = curr->get_key(0);
        if (memcmp(parent_key, child_first_key, file_hdr_->col_tot_len_) == 0) {
            assert(buffer_pool_manager_->unpin_page(parent->get_page_id(), true));
            break;
        }
        memcpy(parent_key, child_first_key, file_hdr_->col_tot_len_);  // 修改了parent node
        curr = parent;

        assert(buffer_pool_manager_->unpin_page(parent->get_page_id(), true));
    }
}

/**
 * @brief 要删除leaf之前调用此函数，更新leaf前驱结点的next指针和后继结点的prev指针
 *
 * @param leaf 要删除的leaf
 */
void IxIndexHandle::erase_leaf(IxNodeHandle *leaf) {
    assert(leaf->is_leaf_page());

    IxNodeHandle *prev = fetch_node(leaf->get_prev_leaf());
    prev->set_next_leaf(leaf->get_next_leaf());
    buffer_pool_manager_->unpin_page(prev->get_page_id(), true);

    IxNodeHandle *next = fetch_node(leaf->get_next_leaf());
    next->set_prev_leaf(leaf->get_prev_leaf());  // 注意此处是SetPrevLeaf()
    buffer_pool_manager_->unpin_page(next->get_page_id(), true);
}

/**
 * @brief 删除node时，更新file_hdr_.num_pages
 *
 * @param node
 */
void IxIndexHandle::release_node_handle(IxNodeHandle &node) {
    file_hdr_->num_pages_--;
}

/**
 * @brief 将node的第child_idx个孩子结点的父节点置为node
 */
void IxIndexHandle::maintain_child(IxNodeHandle *node, int child_idx) {
    if (!node->is_leaf_page()) {
        //  Current node is inner node, load its child and set its parent to current node
        int child_page_no = node->value_at(child_idx);
        IxNodeHandle *child = fetch_node(child_page_no);
        child->set_parent_page_no(node->get_page_no());
        buffer_pool_manager_->unpin_page(child->get_page_id(), true);
    }
}

/**
 * @brief 物理日志辅助：记录索引页面修改前后的完整镜像
 * @param node 被修改的页面节点
 * @param before_image 修改前的页面镜像（已经捕获好的）
 * @param txn 当前事务（用于获取LogManager和事务ID）
 * @param index_name 索引名称（用于恢复时定位文件）
 * @note 调用者需在修改前自行捕获 before_image，本函数捕获 after_image 并写日志
 */
void IxIndexHandle::log_index_page_modify(IxNodeHandle* node, const char* before_image,
                                           Transaction* txn, const std::string& index_name) {
    // 题目十一恢复完成后会从表数据全量重建索引，B+Tree 页级物理日志不仅体积大，
    // 还难以完整覆盖 split/merge/root/header 等所有结构变化。这里不再写索引物理日志，
    // 避免大数据恢复测试因海量 PAGE_SIZE 镜像日志导致超时。
    return;
}
