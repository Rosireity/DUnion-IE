#pragma once

#include <string>
#include <vector>
#include "../Ԥ����ͷ.h"
#include "../BDS����.hpp"

// ��ȡһ��δ��ʹ�õĻ���ʱ��������id
extern unsigned getFormId();

// ����һ��SimpleForm�ı����ݰ�
extern unsigned sendForm(std::string, std::string);

// ������ʹ�õ�id
extern bool destroyForm(unsigned);

// ����һ�����ױ��ַ���
extern std::string createSimpleFormString(std::string, std::string, Json::Value&);

// ����һ���̶�ģ����ַ���
extern std::string createModalFormString(std::string, std::string, std::string, std::string);
