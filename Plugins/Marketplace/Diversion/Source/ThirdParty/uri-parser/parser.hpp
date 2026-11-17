// Copyright (C) 2003  Davis E. King (davis@dlib.net)
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Code in this file is slight modification of code written by
// Davis E. King (davis@dlib.net) for dlib.
// Original file: http://dlib.net/dlib/server/server_http.cpp.html
// Copyright (c) 2022 Bhumit Attarde
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <sstream>
#include <string>

namespace parser {

inline unsigned char toHex(unsigned char x) {
    return x + (x > 9 ? ('A' - 10) : '0');
}

inline unsigned char fromHex(unsigned char ch) {
    if (ch <= '9' && ch >= '0')
        ch -= '0';
    else if (ch <= 'f' && ch >= 'a')
        ch -= 'a' - 10;
    else if (ch <= 'F' && ch >= 'A')
        ch -= 'A' - 10;
    else
        ch = 0;
    return ch;
}

inline std::string encodeUrl(const std::string& s) {
    std::ostringstream os;

    for (char ci : s) {
        if ((ci >= 'a' && ci <= 'z') || (ci >= 'A' && ci <= 'Z') ||
            (ci >= '0' && ci <= '9')) { // allowed
            os << ci;
        } else if (ci == ' ') {
            os << '+';
        } else {
            os << '%' << toHex(ci >> 4) << toHex(ci % 16);
        }
    }

    return os.str();
}

inline std::string decodeUrl(const std::string& str) {
    std::string result;
    std::string::size_type i;
    for (i = 0; i < str.size(); ++i) {
        if (str[i] == '+') {
            result += ' ';
        } else if (str[i] == '%' && str.size() > i + 2) {
            const unsigned char ch1 = fromHex(str[i + 1]);
            const unsigned char ch2 = fromHex(str[i + 2]);
            const unsigned char ch = (ch1 << 4) | ch2;
            result += ch;
            i += 2;
        } else {
            result += str[i];
        }
    }
    return result;
}

} // namespace parser