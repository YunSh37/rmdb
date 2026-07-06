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

#include <cassert>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "defs.h"
#include "errors.h"
#include "record/rm_defs.h"

// ===== DATETIME 工具函数 =====

/** datetime 位打包布局: year(14b)|month(4b)|day(5b)|hour(5b)|min(6b)|sec(6b)
 *  总计 40 位，按时间顺序排列（年->月->日->时->分->秒），可复用 int64 比较 */
inline int64_t datetime_pack(int year, int month, int day, int hour, int min, int sec) {
    return ((int64_t)year  << 26) |
           ((int64_t)month << 22) |
           ((int64_t)day   << 17) |
           ((int64_t)hour  << 12) |
           ((int64_t)min   << 6)  |
           (int64_t)sec;
}

inline void datetime_unpack(int64_t packed, int& year, int& month, int& day,
                            int& hour, int& min, int& sec) {
    sec   = (int)(packed & 0x3F);
    min   = (int)((packed >> 6)  & 0x3F);
    hour  = (int)((packed >> 12) & 0x1F);
    day   = (int)((packed >> 17) & 0x1F);
    month = (int)((packed >> 22) & 0x0F);
    year  = (int)((packed >> 26) & 0x3FFF);
}

/** 判断是否为闰年 */
inline bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/** 获取某年某月的天数 */
inline int days_in_month(int year, int month) {
    static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) return 29;
    return days[month];
}

/** 将打包的 datetime 值格式化为字符串 'YYYY-MM-DD HH:MM:SS' */
inline std::string datetime_format(int64_t packed) {
    int year, month, day, hour, min, sec;
    datetime_unpack(packed, year, month, day, hour, min, sec);
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << year << '-'
        << std::setw(2) << month << '-'
        << std::setw(2) << day << ' '
        << std::setw(2) << hour << ':'
        << std::setw(2) << min << ':'
        << std::setw(2) << sec;
    return oss.str();
}

/** 将 'YYYY-MM-DD HH:MM:SS' 字符串解析为打包 int64_t
 *  @throws RMDBError 格式错误或数值越界时
 *  格式要求：年-月-日 时:分:秒，月和日必须为2位数，年份范围 1000-9999 */
inline int64_t datetime_parse(const std::string& str) {
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    // 格式: "YYYY-MM-DD HH:MM:SS"，sscanf 的 %d 接受前导零和可变位数
    if (sscanf(str.c_str(), "%d-%d-%d %d:%d:%d",
               &year, &month, &day, &hour, &min, &sec) < 3) {
        throw RMDBError("DATETIME format error, expected 'YYYY-MM-DD HH:MM:SS'");
    }
    // 严格格式校验：重新格式化后必须与原字符串一致
    // 防止 '1999-1-07'（月不足2位）、'1999-001-07'（月多余位）等非法格式通过
    std::string reformatted = datetime_format(datetime_pack(year, month, day, hour, min, sec));
    if (reformatted != str) {
        throw RMDBError("DATETIME format error, expected 'YYYY-MM-DD HH:MM:SS'");
    }
    if (year < 1000 || year > 9999)
        throw RMDBError("DATETIME year out of range (1000-9999)");
    if (month < 1 || month > 12)
        throw RMDBError("DATETIME month out of range (1-12)");
    if (day < 1 || day > days_in_month(year, month))
        throw RMDBError("DATETIME day out of range");
    if (hour < 0 || hour > 23)
        throw RMDBError("DATETIME hour out of range (0-23)");
    if (min < 0 || min > 59)
        throw RMDBError("DATETIME minute out of range (0-59)");
    if (sec < 0 || sec > 59)
        throw RMDBError("DATETIME second out of range (0-59)");
    return datetime_pack(year, month, day, hour, min, sec);
}


struct TabCol {
    std::string tab_name;
    std::string col_name;

    friend bool operator<(const TabCol &x, const TabCol &y) {
        return std::make_pair(x.tab_name, x.col_name) < std::make_pair(y.tab_name, y.col_name);
    }
};

struct Value {
    ColType type;  // type of value
    union {
        int int_val;          // int value
        float float_val;      // float value
        int64_t bigint_val;   // bigint value
        int64_t datetime_val; // datetime value (packed int64)
    };
    std::string str_val;  // string value

    std::shared_ptr<RmRecord> raw;  // raw record buffer

    void set_int(int int_val_) {
        type = TYPE_INT;
        int_val = int_val_;
    }

    void set_bigint(int64_t bigint_val_) {
        type = TYPE_BIGINT;
        bigint_val = bigint_val_;
    }

    void set_float(float float_val_) {
        type = TYPE_FLOAT;
        float_val = float_val_;
    }

    void set_datetime(int64_t dt_val) {
        type = TYPE_DATETIME;
        datetime_val = dt_val;
    }

    void set_str(std::string str_val_) {
        type = TYPE_STRING;
        str_val = std::move(str_val_);
    }

    void init_raw(int len) {
        assert(raw == nullptr);
        raw = std::make_shared<RmRecord>(len);
        if (type == TYPE_INT) {
            assert(len == sizeof(int));
            *(int *)(raw->data) = int_val;
        } else if (type == TYPE_BIGINT) {
            assert(len == sizeof(int64_t));
            *(int64_t *)(raw->data) = bigint_val;
        } else if (type == TYPE_DATETIME) {
            assert(len == sizeof(int64_t));
            *(int64_t *)(raw->data) = datetime_val;
        } else if (type == TYPE_FLOAT) {
            assert(len == sizeof(float));
            *(float *)(raw->data) = float_val;
        } else if (type == TYPE_STRING) {
            if (len < (int)str_val.size()) {
                throw StringOverflowError();
            }
            memset(raw->data, 0, len);
            memcpy(raw->data, str_val.c_str(), str_val.size());
        }
    }
};

enum CompOp { OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE };

struct Condition {
    TabCol lhs_col;   // left-hand side column
    CompOp op;        // comparison operator
    bool is_rhs_val;  // true if right-hand side is a value (not a column)
    TabCol rhs_col;   // right-hand side column
    Value rhs_val;    // right-hand side value
};

struct SetClause {
    TabCol lhs;
    Value rhs;
};