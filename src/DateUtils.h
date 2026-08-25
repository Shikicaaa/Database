#pragma once
#include <string>
#include <string_view>
#include <stdexcept>
#include <cstdint>
#include "Serializer.h"

namespace DateUtils {

    inline bool fast_parse_int(std::string_view& s, int& out_val, char sep1, char sep2 = '\0') {
        if (s.empty() || s[0] < '0' || s[0] > '9') return false;
        
        int val = 0;
        size_t i = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            val = val * 10 + (s[i] - '0');
            i++;
        }
        
        if (i < s.size() && (s[i] == sep1 || s[i] == sep2)) {
            s.remove_prefix(i + 1);
        } else {
            s.remove_prefix(i);
        }
        
        out_val = val;
        return true;
    }

    inline DateTime parse_date(std::string_view original_s) {
        std::string_view view = original_s;
        
        int a = 0, b = 0, c_val = 0;
        int hh = 0, mm = 0, ss = 0;
        bool has_time = false;

        if (!fast_parse_int(view, a, '-', '/') ||
            !fast_parse_int(view, b, '-', '/') ||
            !fast_parse_int(view, c_val, ' ', 'T')) {
            throw std::runtime_error("Invalid date format '" + std::string(original_s) + 
                "'. Supported: YYYY-MM-DD, DD-MM-YYYY, DD-MM-YY");
        }

        if (!view.empty()) {
            has_time = true;
            if (!fast_parse_int(view, hh, ':') ||
                !fast_parse_int(view, mm, ':') ||
                !fast_parse_int(view, ss, '\0')) {
                throw std::runtime_error("Invalid time format in '" + std::string(original_s) + "'. Expected HH:MM:SS");
            }
        }

        int year, month, day;
        
        if (a >= 1000) {
            year = a; month = b; day = c_val;
        } else if (c_val >= 1000) {
            day = a; month = b; year = c_val;
        } else if (c_val <= 99) {
            day = a; month = b; year = 2000 + c_val;
        } else {
            throw std::runtime_error("Ambiguous date '" + std::string(original_s) + "'. Use full 4-digit year to avoid ambiguity.");
        }

        if (year < 0 || year > 9999) {
            throw std::runtime_error("Year " + std::to_string(year) + " out of range [0-9999] in '" + std::string(original_s) + "'");
        }
        if (month < 1 || month > 12) {
            throw std::runtime_error("Invalid month " + std::to_string(month) + " in '" + std::string(original_s) + "'");
        }

        static constexpr uint8_t DAYS_IN_MONTH[] = {
            0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
        };
        uint8_t max_days = DAYS_IN_MONTH[month];

        if (month == 2) {
            bool is_leap_year = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
            if (is_leap_year) max_days = 29;
        }

        if (day < 1 || day > max_days) {
            throw std::runtime_error("Invalid day " + std::to_string(day) + " for month " + std::to_string(month) + " in '" + std::string(original_s) + "'");
        }

        if (hh < 0 || hh > 23) throw std::runtime_error("Invalid hour in '" + std::string(original_s) + "'");
        if (mm < 0 || mm > 59) throw std::runtime_error("Invalid minute in '" + std::string(original_s) + "'");
        if (ss < 0 || ss > 59) throw std::runtime_error("Invalid second in '" + std::string(original_s) + "'");

        DateTime dt{};
        dt.year = static_cast<int16_t>(year);
        dt.month = static_cast<uint8_t>(month);
        dt.day = static_cast<uint8_t>(day);
        dt.hour = static_cast<uint8_t>(hh);
        dt.minute = static_cast<uint8_t>(mm);
        dt.second = static_cast<uint8_t>(ss);

        return dt;
    }
} 