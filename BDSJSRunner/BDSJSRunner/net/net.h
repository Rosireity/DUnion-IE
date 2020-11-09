#pragma once
#include <string>

// URL编码
struct URLTool {
public:
	static unsigned char CharToHex(unsigned char x) {
		return (unsigned char)(x > 9 ? x + 55 : x + 48);
	}
	static bool IsAlphaNumber(unsigned char c) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			return true;
		return false;
	}
	// URL encode
	static std::string UrlEncode(const std::string& src) {
		std::string str_encode = "";
		unsigned char* p = (unsigned char*)src.c_str();
		unsigned char ch;
		while (*p) {
			ch = (unsigned char)*p;
			if (*p == ' ') {
				str_encode += '+';
			}
			else if (IsAlphaNumber(ch) || strchr("-_.~!*'();:@&=+$,/?#[]", ch)) {
				str_encode += *p;
			}
			else {
				str_encode += '%';
				str_encode += CharToHex((unsigned char)(ch >> 4));
				str_encode += CharToHex((unsigned char)(ch % 16));
			}
			++p;
		}
		return str_encode;
	}
	// URL decode
	static std::string UrlDecode(const std::string& src) {
		std::string str_decode = "";
		int i;
		char* cd = (char*)src.c_str();
		char p[2];
		for (i = 0; i < strlen(cd); i++) {
			memset(p, '\0', 2);
			if (cd[i] != '%') {
				str_decode += cd[i];
				continue;
			}
			p[0] = cd[++i];
			p[1] = cd[++i];
			p[0] = p[0] - 48 - ((p[0] >= 'A') ? 7 : 0) - ((p[0] >= 'a') ? 32 : 0);
			p[1] = p[1] - 48 - ((p[1] >= 'A') ? 7 : 0) - ((p[1] >= 'a') ? 32 : 0);
			str_decode += (unsigned char)(p[0] * 16 + p[1]);
		}
		return str_decode;
	}
};

// 函数名：netrequest
// 函数功能：获取一个远程请求数据
// 参数说明：url - 指定远程请求api，mode - 请求方式，params - 附加参数
// 返回值：请求结果
extern std::string netrequest(std::string, bool, std::string&);
