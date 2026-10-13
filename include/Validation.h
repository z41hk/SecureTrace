#pragma once

#include <cctype>
#include <cstdio>
#include <string>

class Validation {
public:
    static bool isIsoDate(const std::string& value) {
        if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
            return false;
        }
        int year = 0;
        int month = 0;
        int day = 0;
        if (std::sscanf(value.c_str(), "%d-%d-%d", &year, &month, &day) != 3) {
            return false;
        }
        if (year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) {
            return false;
        }
        return true;
    }

    static bool isDateTime(const std::string& value) {
        if (value.size() != 19) {
            return false;
        }
        if (value[4] != '-' || value[7] != '-' || value[10] != ' ' ||
            value[13] != ':' || value[16] != ':') {
            return false;
        }
        return isIsoDate(value.substr(0, 10));
    }

    static bool isUsername(const std::string& value) {
        if (value.size() < 3 || value.size() > 32) {
            return false;
        }
        for (unsigned char ch : value) {
            if (!(std::isalnum(ch) || ch == '_')) {
                return false;
            }
        }
        return true;
    }
};
