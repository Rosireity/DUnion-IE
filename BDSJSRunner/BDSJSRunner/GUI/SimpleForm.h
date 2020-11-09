#pragma once

#include <string>
#include <vector>
#include "../预编译头.h"
#include "../BDS内容.hpp"

// 获取一个未被使用的基于时间秒数的id
extern unsigned getFormId();

// 发送一个SimpleForm的表单数据包
extern unsigned sendForm(std::string, std::string);

// 销毁已使用的id
extern bool destroyForm(unsigned);

// 创建一个简易表单字符串
extern std::string createSimpleFormString(std::string, std::string, Json::Value&);

// 创建一个固定模板表单字符串
extern std::string createModalFormString(std::string, std::string, std::string, std::string);
