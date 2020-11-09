#include "预编译头.h"
#include "BDS内容.hpp"

#include<combaseapi.h>

#include "v8.h"
#include "libplatform/libplatform.h"
#include "json/json.h"
#include "curl/curl.h"
#include <stdio.h>
#include <thread>
#include <mutex>
#include "GUI/SimpleForm.h"
#include "tick/tick.h"
#include "net/net.h"

using namespace v8;

static bool runcmd(std::string);
static bool reNameByUuid(std::string, std::string);
static void prException(Isolate* iso, Local<Value> );
static std::string getOnLinePlayers();

// 调试信息
template<typename T>
static void PR(T arg) {
#ifndef RELEASED
	std::cout << arg << std::endl;
#endif // !RELEASED
}

// 转换Json对象为字符串
static std::string toJsonString(Json::Value& v) {
	Json::StreamWriterBuilder w;
	std::ostringstream os;
	std::unique_ptr<Json::StreamWriter> jsonWriter(w.newStreamWriter());
	jsonWriter->write(v, &os);
	return std::string(os.str());
}

// 转换字符串为Json对象
static Json::Value toJson(std::string s) {
	Json::Value jv;
	Json::CharReaderBuilder r;
	JSONCPP_STRING errs;
	std::unique_ptr<Json::CharReader> const jsonReader(r.newCharReader());
	bool res = jsonReader->parse(s.c_str(), s.c_str() + s.length(), &jv, &errs);
	if (!res || !errs.empty()) {
		PR("JSON转换失败.." + errs);
	}
	return jv;
}

/*
 UTF-8 转 GBK
 */
static std::string UTF8ToGBK(const char* strUTF8)
{
	int len = MultiByteToWideChar(CP_UTF8, 0, strUTF8, -1, NULL, 0);
	wchar_t* wszGBK = new wchar_t[len + 1];
	memset(wszGBK, 0, len * 2 + 2);
	MultiByteToWideChar(CP_UTF8, 0, strUTF8, -1, wszGBK, len);
	len = WideCharToMultiByte(CP_ACP, 0, wszGBK, -1, NULL, 0, NULL, NULL);
	char* szGBK = new char[len + 1];
	memset(szGBK, 0, len + 1);
	WideCharToMultiByte(CP_ACP, 0, wszGBK, -1, szGBK, len, NULL, NULL);
	std::string strTemp(szGBK);
	if (wszGBK) delete[] wszGBK;
	if (szGBK) delete[] szGBK;
	return strTemp;
}

// 剔除左右空格
std::string& trim(std::string& s)
{
	if (s.empty())
	{
		return s;
	}
	s.erase(0, s.find_first_not_of(" "));
	s.erase(s.find_last_not_of(" ") + 1);
	return s;
}

static char localpath[MAX_PATH] = { 0 };

// 获取BDS完整程序路径
static std::string getLocalPath() {
	if (!localpath[0]) {
		GetModuleFileNameA(NULL, localpath, _countof(localpath));
		for (int l = strlen(localpath); l >= 0; l--) {
			if (localpath[l] == '\\') {
				localpath[l] = localpath[l + 1] = localpath[l + 2] = 0;
				break;
			}
		}
	}
	return std::string(localpath);
}

#pragma region JS回调相关API定义

#ifndef JSDIR
// js插件库所在目录
#define JSDIR u8"js"
#endif // !JSDIR

// 常驻容器
static Isolate* isolate;

static std::string getUUID();
static void runcode(std::string);
static void waitForRequest(std::string, bool mode, std::string, Local<Object>&);
static void delayrun(std::string, int);
static void delayrunfunc(std::string, int);
static void setCommandDescribe(const char*, const char*);
static bool runcmdAs(char*, std::string);
static UINT sendSimpleForm(char *, char *, char *, char *);
static UINT sendModalForm(char*, char*, char*, char*, char*);
static UINT sendCustomForm(char*, char*);
static void logWriteLine(std::string );
static Persistent<Context>* getTrueContext(Local<Context>&);

static std::mutex write_lock;

/**************** JS回调区域 ****************/

// 函数名：fileReadAllText
// 功能：文件输入流读取一个文本
// 参数个数：1个
// 参数类型：字符串
// 参数详解：fname - 文件名（相对BDS位置）
// 返回值：字符串
static void jsAPI_fileReadAllText(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() < 1)
		return;
	Isolate* niso = info.GetIsolate();
	HandleScope handle_scope(niso);
	String::Utf8Value arg1(niso, info[0]);
	std::string fname = std::string(*arg1);
	// 读文件
	DWORD flen, dwHigh, dwBytes;
	HANDLE sfp;
	char* sbuf;
	// 此处进行文件解析
	sfp = CreateFileA(fname.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (sfp != INVALID_HANDLE_VALUE)
	{
		flen = GetFileSize(sfp, &dwHigh);
		if (flen > 0) {
			sbuf = new char[1 * flen + 3]{ 0 };
			if (ReadFile(sfp, sbuf, flen, &dwBytes, NULL)) {
				Local<String> result = String::NewFromUtf8(niso, sbuf).ToLocalChecked();
				info.GetReturnValue().Set(result);
			}
			delete[] sbuf;
		}
		CloseHandle(sfp);
	}
}

// 函数名：fileWriteAllText
// 功能：文件输出流全新写入一个字符串
// 参数个数：2个
// 参数类型：字符串，字符串
// 参数详解：fname - 文件名（相对BDS位置），content - 文本内容
// 返回值：是否写成功
static void jsAPI_fileWriteAllText(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() < 2)
		return;
	bool ret = false;
	Isolate* niso = info.GetIsolate();
	HandleScope handle_scope(niso);
	String::Utf8Value arg1(niso, info[0]);
	String::Utf8Value arg2(niso, info[1]);
	// 写文件
	write_lock.lock();
	FILE * f = NULL;
	fopen_s(&f, *arg1, "w");
	if (f != NULL) {
		fputs(*arg2, f);
		fclose(f);
		ret = true;
	}
	else
		PR(u8"文件创建失败。。");
	Local<Boolean> result = Boolean::New(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
	write_lock.unlock();
}

// 函数名：fileWriteLine
// 功能：文件输出流追加一行字符串
// 参数个数：2个
// 参数类型：字符串，字符串
// 参数详解：fname - 文件名（相对BDS位置），content - 追加内容
// 返回值：是否写成功
static void jsAPI_fileWriteLine(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() < 2)
		return;
	bool ret = false;
	Isolate* niso = info.GetIsolate();
	HandleScope handle_scope(niso);
	String::Utf8Value arg1(niso, info[0]);
	String::Utf8Value arg2(niso, info[1]);
	std::string content = std::string(*arg2) + '\n';
	// 写文件
	write_lock.lock();
	FILE* f = NULL;
	fopen_s(&f, *arg1, "a");
	if (f != NULL) {
		//fseek(f, 0, SEEK_END);
		fputs(content.c_str(), f);
		fclose(f);
		ret = true;
	}
	else
		PR(u8"文件创建失败。。");
	Local<Boolean> result = Boolean::New(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
	write_lock.unlock();
}

// 函数名：log
// 功能：标准输出流打印消息
// 参数个数：1个
// 参数类型：字符串
// 参数详解：待输出至标准流字符串
static void jsAPI_log(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value arg(info.GetIsolate(), info[0]);
	std::cout << *arg << std::endl;
}

// 函数名：logout
// 功能：发送一条命令输出消息（可被拦截）
// 参数个数：1个
// 参数类型：字符串
// 参数详解：待发送的命令输出字符串
static void jsAPI_logout(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value arg(info.GetIsolate(), info[0]);
	logWriteLine(std::string(*arg) + "\n");
}

// 函数名：TimeNow
// 功能：返回一个当前时间的字符串
// 参数个数：0个
// 返回值：字符串
static void jsAPI_TimeNow(const FunctionCallbackInfo<Value>& info) {
	auto timet = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	tm t;
	localtime_s(&t, &timet);
	char strdate[256];
	sprintf_s(strdate, "%d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1,
		t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
	std::string str = std::string(strdate);
	HandleScope handle_scope(isolate);
	Local<String> result = String::NewFromUtf8(isolate, str.c_str()).ToLocalChecked();
	info.GetReturnValue().Set(result);
}

static std::map <std::string, Persistent<Object>*> shareData;

// 函数名：setShareData
// 功能：存入共享数据
// 参数个数：2个
// 参数类型：字符串，数据/函数对象
// 参数详解：key - 关键字，value - 共享数据
static void jsAPI_setShareData(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	Isolate* niso = info.GetIsolate();
	Local<Context> ncon = niso->GetCurrentContext();
	HandleScope handle_scope(niso);
	String::Utf8Value arg1(niso, info[0]);
	Local<Object> arg2 = info[1]->ToObject(ncon).ToLocalChecked();
	std::string cstr = std::string(*arg1);
	Persistent<Object>* pfunc = shareData[cstr];
	if (pfunc != NULL) {
		pfunc->Reset();
		delete pfunc;
	}
	pfunc = new Persistent<Object>(isolate, arg2);
	shareData[cstr] = pfunc;
}

// 函数名：removeShareData
// 功能：删除共享数据
// 参数个数：1个
// 参数类型：字符串
// 参数详解：key - 关键字
// 返回值：旧数据
static void jsAPI_removeShareData(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	Isolate* niso = isolate;
	Local<Context> ncon = isolate->GetCurrentContext();
	HandleScope handle_scope(niso);
	String::Utf8Value arg1(niso, info[0]);
	std::string cstr = std::string(*arg1);
	Persistent<Object>* pfunc = shareData[cstr];
	if (pfunc != NULL) {
		// 存在数据
		info.GetReturnValue().Set(pfunc->Get(isolate));
		pfunc->Reset();
		delete pfunc;
	}
	shareData[cstr] = NULL;
	shareData.erase(cstr);
}

// 函数名：getShareData
// 功能：获取共享数据
// 参数个数：1个
// 参数类型：字符串
// 参数详解：key - 关键字
// 返回值：共享数据
static void jsAPI_getShareData(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	Isolate* niso = isolate;
	Local<Context> ncon = isolate->GetCurrentContext();
	HandleScope handle_scope(niso);
	String::Utf8Value arg1(niso, info[0]);
	Persistent<Object>* pfunc = shareData[*arg1];
	if (pfunc != NULL) {
		// 存在数据
		info.GetReturnValue().Set(pfunc->Get(isolate));
	}
}

static std::unordered_map<std::string, std::string> cmddescripts;

// 函数名：setCommandDescribe
// 功能：设置一个全局指令说明
// 参数个数：2个
// 参数类型：字符串，字符串
// 参数详解：cmd - 命令，description - 命令说明
// 备注：延期注册的情况，可能不会改变客户端界面
static void jsAPI_setCommandDescribe(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	Isolate* niso = info.GetIsolate();
	Local<Context> ncon = niso->GetCurrentContext();
	HandleScope handle_scope(niso);
	String::Utf8Value cmd(niso, info[0]);
	String::Utf8Value description(niso, info[1]);
	std::string strcmd = trim(*&std::string(*cmd));
	if (!strcmd.empty()) {
		cmddescripts[strcmd] = std::string(*description);
		setCommandDescribe(strcmd.c_str(), *description);
	}
}

static std::map <std::string, std::map<Persistent<Context> *, Persistent<Object>*>> beforecallbacks, aftercallbacks;

// 函数名：setBeforeActListener
// 功能：注册玩家事件加载前监听器
// 参数个数：2个
// 参数类型：字符串，函数对象
// 参数详解：key - 注册用关键字，func - 供事件触发时的回调函数对象
static void jsAPI_setBeforeActListener(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	Isolate* niso = info.GetIsolate();
	Local<Context> ncon = niso->GetCurrentContext();
	Persistent<Context>* pncon = getTrueContext(ncon);
	if (pncon != NULL) {	// 找到
		HandleScope handle_scope(niso);
		String::Utf8Value arg1(niso, info[0]);
		Local<Object> arg2 = info[1]->ToObject(ncon).ToLocalChecked();
		std::string cstr = std::string(*arg1);
		Persistent<Object>* pfunc = beforecallbacks[cstr][pncon];
		if (pfunc != NULL) {
			pfunc->Reset();
			delete pfunc;
		}
		pfunc = new Persistent<Object>(isolate, arg2);
		beforecallbacks[cstr][pncon] = pfunc;
	}
}

// 函数名：removeBeforeActListener
// 功能：移除玩家事件监听器
// 参数个数：1个
// 参数类型：字符串
// 参数详解：key - 注册用关键字
// 返回值：旧监听器
static void jsAPI_removeBeforeActListener(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	Isolate* niso = isolate;
	Local<Context> ncon = isolate->GetCurrentContext();
	Persistent<Context>* pncon = getTrueContext(ncon);
	if (pncon != NULL) {	// 找到
		HandleScope handle_scope(niso);
		String::Utf8Value arg1(niso, info[0]);
		Persistent<Object>* pfunc = beforecallbacks[*arg1][pncon];
		if (pfunc != NULL) {
			// 存在数据
			info.GetReturnValue().Set(pfunc->Get(isolate));
			pfunc->Reset();
			delete pfunc;
		}
		beforecallbacks[*arg1][pncon] = NULL;
		beforecallbacks[*arg1].erase(pncon);
	}
}

// 函数名：getBeforeActListener
// 功能：获取玩家事件监听器
// 参数个数：1个
// 参数类型：字符串
// 参数详解：key - 注册用关键字
// 返回值：当前设置的监听器
static void jsAPI_getBeforeActListener(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	Isolate* niso = isolate;
	Local<Context> ncon = isolate->GetCurrentContext();
	Persistent<Context>* pncon = getTrueContext(ncon);
	if (pncon != NULL) {	// 找到
		HandleScope handle_scope(niso);
		String::Utf8Value arg1(niso, info[0]);
		Persistent<Object>* pfunc = beforecallbacks[*arg1][pncon];
		if (pfunc != NULL) {
			// 存在数据
			info.GetReturnValue().Set(pfunc->Get(isolate));
		}
	}
}

// 函数名：setAfterActListener
// 功能：注册玩家事件加载后监听器
// 参数个数：2个
// 参数类型：字符串，函数对象
// 参数详解：key - 注册用关键字，func - 供事件触发时的回调函数对象
static void jsAPI_setAfterActListener(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	Isolate* niso = info.GetIsolate();
	Local<Context> ncon = niso->GetCurrentContext();
	Persistent<Context>* pncon = getTrueContext(ncon);
	if (pncon != NULL) {	// 找到
		HandleScope handle_scope(niso);
		String::Utf8Value arg1(niso, info[0]);
		Local<Object> arg2 = info[1]->ToObject(ncon).ToLocalChecked();
		std::string cstr = std::string(*arg1);
		Persistent<Object>* pfunc = aftercallbacks[cstr][pncon];
		if (pfunc != NULL) {
			pfunc->Reset();
			delete pfunc;
		}
		pfunc = new Persistent<Object>(isolate, arg2);
		aftercallbacks[cstr][pncon] = pfunc;
	}
}

// 函数名：removeAfterActListener
// 功能：移除玩家事件加载后监听器
// 参数个数：1个
// 参数类型：字符串
// 参数详解：key - 注册用关键字
// 返回值：旧监听器
static void jsAPI_removeAfterActListener(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	Isolate* niso = isolate;
	Local<Context> ncon = niso->GetCurrentContext();
	Persistent<Context>* pncon = getTrueContext(ncon);
	if (pncon != NULL) {	// 找到
		HandleScope handle_scope(niso);
		String::Utf8Value arg1(niso, info[0]);
		Persistent<Object>* pfunc = aftercallbacks[*arg1][pncon];
		if (pfunc != NULL) {
			// 存在数据
			info.GetReturnValue().Set(pfunc->Get(isolate));
			pfunc->Reset();
			delete pfunc;
		}
		aftercallbacks[*arg1][pncon] = NULL;
		aftercallbacks[*arg1].erase(pncon);
	}
}

// 函数名：getAfterActListener
// 功能：获取玩家事件加载后监听器
// 参数个数：1个
// 参数类型：字符串
// 参数详解：key - 注册用关键字
// 返回值：当前设置的监听器
static void jsAPI_getAfterActListener(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	Isolate* niso = isolate;
	Local<Context> ncon = niso->GetCurrentContext();
	Persistent<Context>* pncon = getTrueContext(ncon);
	if (pncon != NULL) {	// 找到
		HandleScope handle_scope(niso);
		String::Utf8Value arg1(niso, info[0]);
		Persistent<Object>* pfunc = aftercallbacks[*arg1][pncon];
		if (pfunc != NULL) {
			// 存在数据
			info.GetReturnValue().Set(pfunc->Get(isolate));
		}
	}
}

// 函数名：runScript
// 功能：使用全局环境执行一段脚本命令并返回结果
// 参数个数：1个
// 参数类型：字符串
// 参数详解：script - 语法正确的一段脚本文本
// 返回值：脚本执行结果
static void jsAPI_runScript(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	// 创建各种局部scope。
	Locker v8lock(isolate);
	Isolate::Scope isolate_scope(isolate);
	HandleScope handle_scope(isolate);
	String::Utf8Value arg(isolate, info[0]);
	TryCatch try_catch(isolate);
	// 根据对象模板创建上下文环境，并设置scope。
	Local<Context> context = isolate->GetCurrentContext();
	Context::Scope context_scope(context);
	// 创建包含JavaScript的字符串资源。
	Local<String> source;
	if (String::NewFromUtf8(isolate, *arg, NewStringType::kNormal).ToLocal(&source)) {
		// 从字符串编译出脚本对象。
		Local<Script> script;
		if (Script::Compile(context, source).ToLocal(&script)) {
			// 执行脚本。
			Local<Value> result;
			if (script->Run(context).ToLocal(&result)) {
				info.GetReturnValue().Set(result);
			}
			else {
				prException(isolate, try_catch.Exception());
			}
		}
		else
			prException(isolate, try_catch.Exception());
	}
	else
		prException(isolate, try_catch.Exception());
}

// 函数名：runcmd
// 功能：执行后台指令
// 参数个数：1个
// 参数类型：字符串
// 参数详解：cmd - 语法正确的MC指令
// 返回值：是否正常执行
static void jsAPI_runcmd(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value arg(info.GetIsolate(), info[0]);
	bool ret = runcmd(*arg);
	// 设置返回值
	Local<Boolean> result = Boolean::New(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
}

// 函数名：getOnLinePlayers
// 功能：获取在线玩家列表
// 参数个数：0个
// 返回值：玩家列表的Json字符串
static void jsAPI_getOnLinePlayers(const FunctionCallbackInfo<Value>& info) {
	HandleScope handle_scope(info.GetIsolate());
	std::string ret = getOnLinePlayers();
	// 设置返回值
	Local<String> result = String::NewFromUtf8(info.GetIsolate(), ret.c_str()).ToLocalChecked();
	info.GetReturnValue().Set(result);
}

// 函数名：reNameByUuid
// 功能：重命名一个指定的玩家名
// 参数个数：2个
// 参数类型：字符串，字符串
// 参数详解：uuid - 在线玩家的uuid字符串，newName - 新的名称
// 返回值：是否命名成功
// （备注：该函数可能不会变更客户端实际显示名）
static void jsAPI_reNameByUuid(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value uuid(info.GetIsolate(), info[0]);
	String::Utf8Value name(info.GetIsolate(), info[1]);
	bool ret = reNameByUuid(*uuid, *name);
	// 设置返回值
	Local<Boolean> result = Boolean::New(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
}

// 函数名：runcmdAs
// 功能：模拟玩家执行一个指令
// 参数个数：2个
// 参数类型：字符串，字符串
// 参数详解：uuid - 在线玩家的uuid字符串，cmd - 待模拟执行的指令
// 返回值：是否发送成功
static void jsAPI_runcmdAs(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	Isolate* ciso = info.GetIsolate();
	HandleScope handle_scope(ciso);
	String::Utf8Value uuid(ciso, info[0]);
	String::Utf8Value cmd(ciso, info[1]);
	bool ret = runcmdAs(*uuid, *cmd);
	// 设置返回值
	Local<Boolean> result = Boolean::New(ciso, ret);
	info.GetReturnValue().Set(result);
}

// 函数名：sendSimpleForm
// 功能：向指定的玩家发送一个简单表单
// 参数个数：4个
// 参数类型：字符串，字符串，字符串，字符串
// 参数详解：uuid - 在线玩家的uuid字符串，title - 表单标题，content - 内容，buttons - 按钮文本数组字符串
// 返回值：创建的表单id，为 0 表示发送失败
static void jsAPI_sendSimpleForm(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 4)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value uuid(info.GetIsolate(), info[0]);
	String::Utf8Value title(info.GetIsolate(), info[1]);
	String::Utf8Value content(info.GetIsolate(), info[2]);
	String::Utf8Value bttxts(info.GetIsolate(), info[3]);
	UINT ret = sendSimpleForm(*uuid, *title, *content, *bttxts);
	// 设置返回值
	Local<Integer> result = Integer::NewFromUnsigned(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
}

// 函数名：sendModalForm
// 功能：向指定的玩家发送一个模式对话框
// 参数个数：5个
// 参数类型：字符串，字符串，字符串，字符串，字符串
// 参数详解：uuid - 在线玩家的uuid字符串，title - 表单标题，content - 内容，button1 按钮1标题（点击该按钮selected为true），button2 按钮2标题（点击该按钮selected为false）
// 返回值：创建的表单id，为 0 表示发送失败
static void jsAPI_sendModalForm(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 5)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value uuid(info.GetIsolate(), info[0]);
	String::Utf8Value title(info.GetIsolate(), info[1]);
	String::Utf8Value content(info.GetIsolate(), info[2]);
	String::Utf8Value button1(info.GetIsolate(), info[3]);
	String::Utf8Value button2(info.GetIsolate(), info[4]);
	UINT ret = sendModalForm(*uuid, *title, *content, *button1, *button2);
	// 设置返回值
	Local<Integer> result = Integer::NewFromUnsigned(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
}

// 函数名：sendCustomForm
// 功能：向指定的玩家发送一个自定义表单
// 参数个数：2个
// 参数类型：字符串，字符串
// 参数详解：uuid - 在线玩家的uuid字符串，json - 自定义表单的json字符串（要使用自定义表单类型，参考nk、pm格式或minebbs专栏）
// 返回值：创建的表单id，为 0 表示发送失败
static void jsAPI_sendCustomForm(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value uuid(info.GetIsolate(), info[0]);
	String::Utf8Value json(info.GetIsolate(), info[1]);
	UINT ret = sendCustomForm(*uuid, *json);
	// 设置返回值
	Local<Integer> result = Integer::NewFromUnsigned(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
}

// 函数名：releaseForm
// 功能：放弃一个表单
// 参数个数：1个
// 参数类型：整型
// 参数详解：formid - 表单id
// 返回值：是否释放成功
//（备注：已被接收到的表单会被自动释放）
static void jsAPI_releaseForm(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	if (!info[0]->IsInt32())
		return;
	HandleScope handle_scope(isolate);
	Local<Context> context = isolate->GetCurrentContext();
	Local<Int32> formid;
	if (info[0]->ToInt32(context).ToLocal(&formid)) {
		UINT fid = formid->Int32Value(context).ToChecked();
		bool ret = destroyForm(fid);
		// 设置返回值
		Local<Boolean> result = Boolean::New(info.GetIsolate(), ret);
		info.GetReturnValue().Set(result);
	}
}

// 函数名：request
// 功能：发起一个远程HTTP请求
// 参数个数：4个
// 参数类型：字符串，字符串，字符串，函数对象
// 参数详解：urlpath - 远程接口路径，mode - 访问方式，params - 附加数据，func - 获取内容回调
static void jsAPI_request(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 4)
		return;
	Isolate *niso = info.GetIsolate();
	HandleScope handle_scope(niso);
	String::Utf8Value url(niso, info[0]);
	String::Utf8Value mode(niso, info[1]);
	String::Utf8Value params(niso, info[2]);
	Local<Object> func = info[3]->ToObject(niso->GetCurrentContext()).ToLocalChecked();
	std::string sm = std::string(*mode);
	transform(sm.begin(), sm.end(), sm.begin(), ::tolower);
	if (sm == "get" || sm == "post") {
		std::string surl = std::string(*url);
		std::string sparams = std::string(*params);
		waitForRequest(surl, sm == "post", sparams, func);
	}
}

static std::map <std::string, Persistent<Script>*> timeouts;
static std::map <std::string, Persistent<Object>*> timeoutfuncs;
static std::map <std::string, Persistent<Context>*> delaycontexts;

// 函数名：setTimeout
// 功能：延时执行一条指令
// 参数个数：2个
// 参数类型：字符串/函数，整型
// 参数详解：code - 待延时执行的指令字符串/函数对象，millisec - 延时毫秒数
static void jsAPI_setTimeout(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	Locker v8lock(isolate);
	HandleScope handle_scope(isolate);
	Local<Context> context = isolate->GetCurrentContext();
	Persistent<Context>* tc = getTrueContext(context);
	Local<Int32> msec;
	if (info[1]->ToInt32(context).ToLocal(&msec)) {
		if (tc != NULL) {
			if (info[0]->IsFunction()) {
				// 保存脚本。
				Persistent<Object>* p = new Persistent<Object>();
				p->Reset(isolate, info[0]->ToObject(context).ToLocalChecked());
				std::string uid = getUUID();
				timeoutfuncs[uid] = p;
				delaycontexts[uid] = tc;
				int m = (int)msec->Int32Value(context).ToChecked();
				std::thread t(delayrunfunc, uid, m);
				t.detach();
			}
			else {
				String::Utf8Value code(isolate, info[0]);
				// 此处执行编译脚本
				TryCatch try_catch(isolate);
				// 根据对象模板创建上下文环境，并设置scope。
				Context::Scope context_scope(context);
				// 创建包含JavaScript的字符串资源。
				Local<String> source;
				if (String::NewFromUtf8(isolate, *code, NewStringType::kNormal).ToLocal(&source)) {
					// 从字符串编译出脚本对象。
					Local<Script> script;
					if (Script::Compile(context, source).ToLocal(&script)) {
						// 保存脚本。
						Persistent<Script>* p = new Persistent<Script>();
						p->Reset(isolate, script);
						std::string uid = getUUID();
						timeouts[uid] = p;
						delaycontexts[uid] = tc;
						int m = (int)msec->Int32Value(context).ToChecked();
						std::thread t(delayrun, uid, m);
						t.detach();
					}
					else
						prException(isolate, try_catch.Exception());
				}
				else
					prException(isolate, try_catch.Exception());
			}
		}
	}
}


/**************** C回调接口定义区域 ****************/

// 通过注册监听机制setListener回调，绑定关键字即可

/**************** 其它相关内容 ****************/

//static int txtnum = 0;
static std::map<std::string, std::string> texts;
// 错误列表
static std::unordered_map<Persistent<Context>*, std::string> errfiles;

// 返回持久化环境头
static Persistent<Context>* getTrueContext(Local<Context>& c) {
	for (auto& e : errfiles) {
		if (e.first->Get(isolate) == c) {
			return e.first;
		}
	}
	return NULL;
}

// 读取文本文件所有内容并返回字符串
static std::string fileReadAllText(const char* info) {
	// 读文件
	std::string str = "";
	DWORD flen, dwHigh, dwBytes;
	HANDLE sfp;
	char* sbuf;
	// 此处进行文件解析
	sfp = CreateFileA(info, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (sfp != INVALID_HANDLE_VALUE)
	{
		flen = GetFileSize(sfp, &dwHigh);
		if (flen == 0) {
			CloseHandle(sfp);
			return str;
		}
		sbuf = new char[1 * flen + 3]{ 0 };
		if (ReadFile(sfp, sbuf, flen, &dwBytes, NULL)) {
			str = std::string(sbuf);
		}
		delete [] sbuf;
		CloseHandle(sfp);
	}
	return str;
}

// 读取所有js文本到内存中
// path - 目录名ANSI
static void readAllJsFile(std::string path) {
	std::string pair = path + "\\*.js";
	WIN32_FIND_DATAA ffd;
	HANDLE dfh = FindFirstFileA(pair.c_str(), &ffd);
	if (INVALID_HANDLE_VALUE != dfh) {
		//txtnum = 0;
		if (texts.size() != 0) {
			texts.clear();
		}
		//texts	// 最大js文件个数不再设限制
		do
		{
			if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				// 目录，不作处理
			}
			else
			{
				std::string strFname = std::string(".\\" + path + "\\" + ffd.cFileName);
				PR(u8"读取JS文件：" + strFname);
				texts[strFname] = fileReadAllText(strFname.c_str());
			}
		} while (FindNextFileA(dfh, &ffd) != 0);
		int dwError = GetLastError();
		if (dwError != ERROR_NO_MORE_FILES)
		{
			//DisplayErrorBox(TEXT("FindFirstFile"));
		}
		FindClose(dfh);
	}
}

// JS网络相关

static std::string getUUID() {
	std::string str = "";
	GUID guid;
	if (!CoCreateGuid(&guid))
	{
		char buffer[64] = { 0 };
		_snprintf_s(buffer, sizeof(buffer),
			//"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",    //大写
			"%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",        //小写
			guid.Data1, guid.Data2, guid.Data3,
			guid.Data4[0], guid.Data4[1], guid.Data4[2],
			guid.Data4[3], guid.Data4[4], guid.Data4[5],
			guid.Data4[6], guid.Data4[7]);
		str = std::string(buffer);
	}
	return str;
}

static const bool HTTPMODE_GET = false, HTTPMODE_POST = true;

static std::map<std::string, Persistent<Object>*> netfuncs;

// 使用libcurl获取远程数据，并执行js回调
static void netCurlCallbk(std::string url, bool mode, std::string params, std::string uuid) {
	std::string data = netrequest(url, mode, params);
	Persistent<Object>* p = netfuncs[uuid];
	if (p != NULL) {
		// 回调
		Locker v8lock(isolate);
		HandleScope hs = HandleScope(isolate);
		Local<Object> ofn = p->Get(isolate);
		if (ofn->IsFunction()) {
			Persistent<Context>* tc = delaycontexts[uuid];
			if (tc != NULL) {
				Local<Context> context = tc->Get(isolate);
				Local<Value> result;
				Local<Value> arg = Local<Value>::New(isolate, String::NewFromUtf8(isolate, data.c_str()).ToLocalChecked());
				ofn->CallAsFunction(context, context->Global(), 1, &arg).ToLocal(&result);
				delaycontexts.erase(uuid);
			}
		}
		p->Reset();
		delete p;
	}
	netfuncs.erase(uuid);
}

// 注册远程调用并执行
static void waitForRequest(std::string url, bool mode, std::string params, Local<Object>& p) {
	std::string uuid = getUUID();
	Local<Context> c = isolate->GetCurrentContext();
	Persistent<Context>* tc = getTrueContext(c);
	if (tc != NULL) {
		Persistent<Object>* pp = new Persistent<Object>(isolate, p);
		netfuncs[uuid] = pp;
		delaycontexts[uuid] = tc;
		std::thread t(netCurlCallbk, url, mode, params, uuid);
		t.detach();
	}
}

// 异步执行一个函数对象
static void runcodeFunc(Persistent<Object>* pscript, Persistent<Context> * tc) {
	// 设置各种局部scope
	Locker v8lock(isolate);
	Isolate::Scope isolate_scope(isolate);
	HandleScope handle_scope(isolate);
	Local<Context> context = tc->Get(isolate);
	Context::Scope context_scope(context);
	// 根据名称获取脚本函数
	Handle<Object> value_func;
	Local<Object> funco = pscript->Get(isolate);
	if (funco->IsFunction()) {
		// 运行函数
		Local<Value> result;
		if (funco->CallAsFunction(context, context->Global(), 0, NULL).ToLocal(&result)) {
			// 异步运行 无需返回
		}
	}
}

// 异步执行一段脚本
static void runcode(Persistent<Script> *pscript, Persistent<Context>* tc) {
	// 创建各种局部scope。
	Locker v8lock(isolate);
	Isolate::Scope isolate_scope(isolate);
	HandleScope handle_scope(isolate);
	TryCatch try_catch(isolate);
	// 根据对象模板创建上下文环境，并设置scope。
	Local<Context> context = tc->Get(isolate);
	Context::Scope context_scope(context);
	Local<Script> script = pscript->Get(isolate);
	// 执行脚本。
	Local<Value> result;
	if (script->Run(context).ToLocal(&result)) {
			// 异步执行，无需返回
	}
	else {
			prException(isolate, try_catch.Exception());
	}
}

// 延时执行脚本
static void delayrun(std::string uuid, int x) {
	std::this_thread::sleep_for(std::chrono::milliseconds(x));
	// 此处脚本编译并执行
	Persistent<Script>* c = timeouts[uuid];
	Persistent<Context>* con = delaycontexts[uuid];
	runcode(c, con);
	c->Reset();
	delete c;
	timeouts.erase(uuid);
	delaycontexts.erase(uuid);
};

// 延时执行脚本
static void delayrunfunc(std::string uuid, int x) {
	std::this_thread::sleep_for(std::chrono::milliseconds(x));
	// 此处脚本编译并执行
	Persistent<Object>* c = timeoutfuncs[uuid];
	Persistent<Context>* con = delaycontexts[uuid];
	runcodeFunc(c, con);
	c->Reset();
	delete c;
	timeoutfuncs.erase(uuid);
	delaycontexts.erase(uuid);
};

#pragma endregion


#pragma region 插件HOOK调用相关

/**************** 脚本执行区域 ****************/

// 打印异常信息
static void prException(Isolate *iso, Local<Value> e) {
	String::Utf8Value error(isolate, e);
	Local<Context> lc = iso->GetCurrentContext();
	Persistent<Context>* pat = NULL;
	std::string f = "";
	for (auto& erf : errfiles) {
		pat = erf.first;
		if (pat->Get(isolate) == lc) {
			f = erf.second;
			break;
		}
	}
	printf("[File] %s : script error: %s\n", f.c_str(), *error);
}

// 监听模式
static enum class ActMode : UCHAR
{
	BEFORE = 0,
	AFTER = 1
};

// 通过关键字调用监听
static bool runScriptCallBackListener(std::string act, ActMode mode, std::string jv) {
	auto callv = (mode == ActMode::BEFORE ? beforecallbacks[act] :
		aftercallbacks[act]);
	bool ret = true;
	if (callv.size() != 0) {
		Locker v8lock(isolate);
		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		for(auto var : callv)
		{
			Persistent<Context>* mcontext = var.first;
			Local<Context> context = mcontext->Get(isolate);
			Context::Scope context_scope(context);
			// 根据名称获取脚本函数
			Handle<Object> value_func;
			Local<Object> funco = var.second->Get(isolate);
			if (funco->IsFunction()) {
				// 构造参数
				Local<Value> arg = Local<Value>::New(isolate, String::NewFromUtf8(isolate, jv.c_str()).ToLocalChecked());
				Local<Value> result;
				if (funco->CallAsFunction(context, context->Global(), 1, &arg).ToLocal(&result)) {
					ret = (ret && !(result == Boolean::New(isolate, false)));
				}
			}
		}
	}
	return ret;
}

// 所有关键字
static struct ACTEVENT {
	const std::string ONSERVERCMD = u8"onServerCmd";
	const std::string ONSERVERCMDOUTPUT = u8"onServerCmdOutput";
	const std::string ONFORMSELECT = u8"onFormSelect";
	const std::string ONUSEITEM = u8"onUseItem";
	const std::string ONMOVE = u8"onMove";
	const std::string ONATTACK = u8"onAttack";
	const std::string ONPLACEDBLOCK = u8"onPlacedBlock";
	const std::string ONDESTROYBLOCK = u8"onDestroyBlock";
	const std::string ONSTARTOPENCHEST = u8"onStartOpenChest";
	const std::string ONSTARTOPENBARREL = u8"onStartOpenBarrel";
	const std::string ONCHANGEDIMENSION = u8"onChangeDimension";
	const std::string ONLOADNAME = u8"onLoadName";
	const std::string ONPLAYERLEFT = u8"onPlayerLeft";
	const std::string ONSTOPOPENCHEST = u8"onStopOpenChest";
	const std::string ONSTOPOPENBARREL = u8"onStopOpenBarrel";
	const std::string ONSETSLOT = u8"onSetSlot";
	const std::string ONMOBDIE = u8"onMobDie";
	const std::string ONRESPAWN = u8"onRespawn";
	const std::string ONCHAT = u8"onChat";
	const std::string ONINPUTTEXT = u8"onInputText";
	const std::string ONINPUTCOMMAND = u8"onInputCommand";
} ActEvent;

// 维度ID转换为中文字符
static std::string toDimenStr(int dimensionId) {
	switch (dimensionId) {
	case 0:return u8"主世界";
	case 1:return u8"地狱";
	case 2:return u8"末地";
	default:
		break;
	}
	return u8"未知维度";
}

// 在JSON数据中附上玩家基本信息
static void addPlayerInfo(Json::Value &jv, Player *p) {
	if (p) {
		jv["playername"] = p->getNameTag();
		int did = p->getDimensionId();
		jv["dimensionid"] = did;
		jv["dimension"] = toDimenStr(did);
		jv["isstand"] = p->isStand();
		jv["XYZ"] = toJson(p->getPos()->toJsonString());
	}
}

// 在JSON数据中附上生物基本信息
static void addMobInfo(Json::Value& jv, Mob* p) {
	jv["mobname"] = p->getNameTag();
	int did = p->getDimensionId();
	jv["dimensionid"] = did;
	jv["XYZ"] = toJson(p->getPos()->toJsonString());
}

static VA p_spscqueue;

static VA p_level;

static VA p_ServerNetworkHandle = 0;

// 执行后端指令
static bool runcmd(std::string cmd) {
	if (p_spscqueue != 0) {
		if (p_level) {
			auto fr = [cmd]() {
				SYMCALL(bool, MSSYM_MD5_b5c9e566146b3136e6fb37f0c080d91e, p_spscqueue, cmd);
			};
			safeTick(fr);
			return true;
		}
	}
	return false;
}

static std::unordered_map<std::string, Player*> onlinePlayers;
static std::unordered_map<Player*, bool> playerSign;

// 重设新名字
static bool reNameByUuid(std::string uuid, std::string newName) {
	bool ret = false;
	Player* taget = onlinePlayers[uuid];
	if (playerSign[taget]) {
		auto fr = [uuid, newName]() {
			Player* p = onlinePlayers[uuid];
			if (playerSign[p]) {
				p->reName(newName);
			}
		};
		safeTick(fr);
		ret = true;
	}
	return ret;
}

// 模拟指令
static bool runcmdAs(char* uuid, std::string cmd) {
	Player* p = onlinePlayers[uuid];
	if (playerSign[p]) {								// IDA ServerNetworkHandler::handle, https://github.com/NiclasOlofsson/MiNET/blob/master/src/MiNET/MiNET/Net/MCPE%20Protocol%20Documentation.md
		std::string suuid = uuid;
		std::string scmd = cmd;
		auto fr = [suuid, scmd]() {
			Player* p = onlinePlayers[suuid];
			if (playerSign[p]) {
				VA nid = p->getNetId();
				VA tpk;
				CommandRequestPacket src;
				SYMCALL(VA, MSSYM_B1QE12createPacketB1AE16MinecraftPacketsB2AAA2SAB1QA2AVB2QDA6sharedB1UA3ptrB1AA7VPacketB3AAAA3stdB2AAE20W4MinecraftPacketIdsB3AAAA1Z,
					&tpk, 76);
				memcpy((void*)(tpk + 40), &scmd, sizeof(scmd));
				SYMCALL(VA, MSSYM_B1QA6handleB1AE20ServerNetworkHandlerB2AAE26UEAAXAEBVNetworkIdentifierB2AAE24AEBVCommandRequestPacketB3AAAA1Z,
					p_ServerNetworkHandle, nid, tpk);
			}
		};
		safeTick(fr);
		return true;
	}
	return false;
}

// 获取在线玩家列表信息
static std::string getOnLinePlayers() {
	Json::Value rt;
	Json::Value jv;
	for (auto& op : playerSign) {
		Player* p = op.first;
		if (op.second) {
			jv["playername"] = p->getNameTag();
			jv["uuid"] = p->getUuid()->toString();
			jv["xuid"] = p->getXuid(p_level);
			rt.append(jv);
		}
	}
	return rt.toStyledString();
}

// 判断指针是否为玩家列表中指针
static bool checkIsPlayer(void* p) {
	return playerSign[(Player*)p];
}

unsigned sendForm(std::string uuid, std::string str)
{
	unsigned fid = getFormId();
	// 此处自主创建包
	auto fr = [uuid, fid, str]() {
		Player* p = onlinePlayers[uuid];
		if (playerSign[p]) {
			VA tpk;
			ModalFormRequestPacket sec;
			SYMCALL(VA, MSSYM_B1QE12createPacketB1AE16MinecraftPacketsB2AAA2SAB1QA2AVB2QDA6sharedB1UA3ptrB1AA7VPacketB3AAAA3stdB2AAE20W4MinecraftPacketIdsB3AAAA1Z,
				&tpk, 100);
			*(VA*)(tpk + 40) = fid;
			*(std::string*)(tpk + 48) = str;
			p->sendPacket(tpk);
		}
	};
	safeTick(fr);
	return fid;
}

// 发送指定玩家一个表单
static UINT sendSimpleForm(char* uuid, char* title, char* content, char* buttons) {
	Player* p = onlinePlayers[uuid];
	if (!playerSign[p])
		return 0;
	Json::Value bts;
	Json::Value ja = toJson(buttons);
	for (int i = 0; i < ja.size(); i++) {
		Json::Value bt;
		bt["text"] = ja[i];
		bts.append(bt);
	}
	std::string str = createSimpleFormString(title, content, bts);
	return sendForm(uuid, str);
}

// 发送指定玩家一个模板对话框
static UINT sendModalForm(char* uuid, char* title, char* content, char* button1, char* button2) {
	Player* p = onlinePlayers[uuid];
	if (!playerSign[p])
		return 0;
	std::string str = createModalFormString(title, content, button1, button2);
	return sendForm(uuid, str);
}

// 发送指定玩家一个自定义GUI界面
static UINT sendCustomForm(char* uuid, char* json) {
	Player* p = onlinePlayers[uuid];
	if (!playerSign[p])
		return 0;
	return sendForm(uuid, json);
}

// 标准输出句柄常量
static const VA STD_COUT_HANDLE = SYM_OBJECT(VA,
	MSSYM_B2UUA3impB2UQA4coutB1AA3stdB2AAA23VB2QDA5basicB1UA7ostreamB1AA2DUB2QDA4charB1UA6traitsB1AA1DB1AA3stdB3AAAA11B1AA1A); //140721370820768


// 发送一个命令输出消息
static void logWriteLine(std::string cmdout) {
	SYMCALL(VA, MSSYM_MD5_b5f2f0a753fc527db19ac8199ae8f740, STD_COUT_HANDLE, cmdout.c_str(), cmdout.length());
}

static VA regHandle = 0;

// 注册指令描述
static void setCommandDescribe(const char* cmd, const char* description) {
	Locker v8lock(isolate);
	if (regHandle) {
		std::string c = std::string(cmd);
		std::string ct = std::string(description);
		SYMCALL(VA, MSSYM_MD5_8574de98358ff66b5a913417f44dd706, regHandle, &c, ct.c_str(), 0, 64, 0);
	}
}

// 回传伤害源信息
static Json::Value getDamageInfo(void* p, void* dsrc) {
	char v72;
	VA  v2[2];
	v2[0] = (VA)p;
	v2[1] = (VA)dsrc;
	auto v7 = *((VA*)(v2[0] + 816));
	auto srActid = (VA*)(*(VA(__fastcall**)(VA, char*))(*(VA*)v2[1] + 56))(
		v2[1], &v72);
	auto SrAct = SYMCALL(Actor*,
		MSSYM_B1QE11fetchEntityB1AA5LevelB2AAE13QEBAPEAVActorB2AAE14UActorUniqueIDB3AAUA1NB1AA1Z,
		v7, *srActid, 0);
	std::string sr_name = "";
	std::string sr_type = "";
	if (SrAct) {
		sr_name = SrAct->getNameTag();
		int srtype = checkIsPlayer(SrAct) ? 319 : SrAct->getEntityTypeId();
		SYMCALL(std::string&, MSSYM_MD5_af48b8a1869a49a3fb9a4c12f48d5a68, &sr_type, srtype);
	}
	Json::Value jv;
	if (checkIsPlayer(p)) {
		addPlayerInfo(jv, (Player*)p);
		std::string playertype;				// IDA Player::getEntityTypeId
		SYMCALL(std::string&, MSSYM_MD5_af48b8a1869a49a3fb9a4c12f48d5a68, &playertype, 319);
		jv["mobname"] = jv["playername"];
		jv["mobtype"] = playertype;			// "entity.player.name"
	}
	else {
		addMobInfo(jv, (Mob*)p);
		jv["mobtype"] = ((Mob*)p)->getEntityTypeName();
	}
	jv["srcname"] = sr_name;
	jv["srctype"] = sr_type;
	jv["dmcase"] = *((UINT*)dsrc + 2);
	return jv;
}

static std::string oldseedstr;

/**************** 插件HOOK区域 ****************/


// 获取指令队列
THook2(_JS_GETSPSCQUEUE,VA, MSSYM_MD5_3b8fb7204bf8294ee636ba7272eec000,
	VA _this) {
	p_spscqueue = original(_this);
	return p_spscqueue;
}

// 获取游戏初始化时基本信息
THook2(_JS_ONGAMESESSION, VA,
	MSSYM_MD5_9f3b3524a8d04242c33d9c188831f836,
	void* a1, void* a2, VA* a3, void* a4, void* a5, void* a6, void* a7) {
	p_ServerNetworkHandle = *a3;
	return original(a1, a2, a3, a4, a5, a6, a7);
}

// 获取地图初始化信息
THook2(_JS_LEVELINIT, VA, MSSYM_MD5_4ff87e34eeebbfcf3f44d8d9ab7658e3,
	VA a1, VA a2, VA a3, VA a4, VA a5, VA a6, VA a7, VA a8, VA a9, VA a10, VA a11, VA a12) {
	VA level = original(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
	p_level = level;
	return level;
}

// 获取玩家初始化时地图基本信息
THook2(_JS_PLAYERINIT, Player *, MSSYM_MD5_c4b0cddb50ed88e87acce18b5bd3fb8a,
	Player* _this, VA level, __int64 a3, int a4, __int64 a5, __int64 a6, void* uuid, std::string& struuid, __int64* a9, __int64 a10, __int64 a11) {
	p_level = level;
	return original(_this, level, a3, a4, a5, a6, uuid, struuid, a9, a10, a11);
}

// 保存设置文件中地图种子字符
THook2(_JS_GETOLDSEEDSTR, UINT, MSSYM_MD5_d2496e689e9641a96868df357e31ad87,
	std::string *pSeedstr, VA a2) {
	oldseedstr = std::string(*pSeedstr);
	return original(pSeedstr, a2);
}

// 改写发信游戏信息包中种子信息为设置文件中信息
THook2(_JS_HIDESEEDPACKET, void,
	MSSYM_B1QA5writeB1AE15StartGamePacketB2AAE21UEBAXAEAVBinaryStreamB3AAAA1Z,
	VA _this, VA a2) {
	if (oldseedstr != "") {				// IDA LevelSettings::LevelSettings
		*((UINT*)(_this + 40)) = atoi(oldseedstr.c_str());
	}
	return original(_this, a2);
}

// 注册指令描述引用自list命令注册
THook2(_JS_ONLISTCMDREG, VA, MSSYM_B1QA5setupB1AE11ListCommandB2AAE22SAXAEAVCommandRegistryB3AAAA1Z,
	VA handle) {
	Locker v8locker(isolate);
	regHandle = handle;
	for (auto& v : cmddescripts) {
		std::string c = std::string(v.first);
		std::string ct = std::string(v.second);
		SYMCALL(VA, MSSYM_MD5_8574de98358ff66b5a913417f44dd706, handle, &c, ct.c_str(), 0, 64, 0);
	}
	return original(handle);
}

// 服务器后台输入指令
THook2(_JS_ONSERVERCMD, bool,
	MSSYM_MD5_b5c9e566146b3136e6fb37f0c080d91e,
	VA _this, std::string *cmd) {
	Json::Value jv;
	jv["cmd"] = *cmd;
	bool ret = runScriptCallBackListener(ActEvent.ONSERVERCMD, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		bool result = original(_this, cmd);
		jv["result"] = result;
		runScriptCallBackListener(ActEvent.ONSERVERCMD, ActMode::AFTER, toJsonString(jv));
		return result;
	}
	return ret;
}

// 服务器后台指令输出
THook2(_JS_ONSERVERCMDOUTPUT, VA,
	MSSYM_MD5_b5f2f0a753fc527db19ac8199ae8f740,
	VA handle, char *str, VA size) {
	if (handle == STD_COUT_HANDLE) {
		Json::Value jv;
		jv["output"] = std::string(str);
		bool ret = runScriptCallBackListener(ActEvent.ONSERVERCMDOUTPUT, ActMode::BEFORE, toJsonString(jv));
		if (ret) {
			VA result = original(handle, str, size);
			jv["result"] = ret;
			runScriptCallBackListener(ActEvent.ONSERVERCMDOUTPUT, ActMode::AFTER, toJsonString(jv));
			return result;
		}
		return handle;
	}
	return original(handle, str, size);
}

// 玩家选择表单
THook2(_JS_ONFORMSELECT, void,
	MSSYM_MD5_8b7f7560f9f8353e6e9b16449ca999d2,
	VA _this, VA id, VA handle, ModalFormResponsePacket** fp) {
	ModalFormResponsePacket* fmp = *fp;
	Player* p = SYMCALL(Player*, MSSYM_B2QUE15getServerPlayerB1AE20ServerNetworkHandlerB2AAE20AEAAPEAVServerPlayerB2AAE21AEBVNetworkIdentifierB2AAA1EB1AA1Z,
		handle, id, *(char*)((VA)fmp + 16));
	if (p != NULL) {
		UINT fid = fmp->getFormId();
		if (destroyForm(fid)) {
			Json::Value jv;
			addPlayerInfo(jv, p);
			jv["uuid"] = p->getUuid()->toString();
			jv["formid"] = fid;
			jv["selected"] = fmp->getSelectStr();		// 特别鸣谢：sysca11
			bool ret = runScriptCallBackListener(ActEvent.ONFORMSELECT, ActMode::BEFORE, toJsonString(jv));
			if (ret) {
				original(_this, id, handle, fp);
				jv["result"] = ret;
				runScriptCallBackListener(ActEvent.ONFORMSELECT, ActMode::AFTER, toJsonString(jv));
			}
			return;
		}
	}
	original(_this, id, handle, fp);
}

// 玩家操作物品
THook2(_JS_ONUSEITEM,bool,
	MSSYM_B1QA9useItemOnB1AA8GameModeB2AAA4UEAAB1UE14NAEAVItemStackB2AAE12AEBVBlockPosB2AAA9EAEBVVec3B2AAA9PEBVBlockB3AAAA1Z,
	void* _this, ItemStack* item, BlockPos* pBlkpos, unsigned __int8 a4, void* v5, Block* pBlk) {
	auto pPlayer = *reinterpret_cast<Player**>(reinterpret_cast<VA>(_this) + 8);
	Json::Value jv;
	addPlayerInfo(jv, pPlayer);
	jv["position"] = toJson(pBlkpos->getPosition()->toJsonString());
	jv["itemid"] = item->getId();
	jv["itemaux"] = item->getAuxValue();
	jv["itemname"] = item->getName();
	bool ret = runScriptCallBackListener(ActEvent.ONUSEITEM, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		bool result = original(_this, item, pBlkpos, a4, v5, pBlk);
		jv["result"] = result;
		runScriptCallBackListener(ActEvent.ONUSEITEM, ActMode::AFTER, toJsonString(jv));
		return result;
	}
	return ret;
}

// 玩家放置方块
THook2(_JS_ONPLACEDBLOCK, bool,
	MSSYM_B1QA8mayPlaceB1AE11BlockSourceB2AAA4QEAAB1UE10NAEBVBlockB2AAE12AEBVBlockPosB2AAE10EPEAVActorB3AAUA1NB1AA1Z,
	BlockSource* _this, Block* pBlk, BlockPos* pBlkpos, unsigned __int8 a4, struct Actor* pPlayer, bool _bool) {
	Json::Value jv;
	if (pPlayer && checkIsPlayer(pPlayer)) {
		addPlayerInfo(jv, (Player*)pPlayer);
		jv["position"] = toJson(pBlkpos->getPosition()->toJsonString());
		jv["blockid"] = pBlk->getLegacyBlock()->getBlockItemID();
		jv["blockname"] = pBlk->getLegacyBlock()->getFullName();
		bool ret = runScriptCallBackListener(ActEvent.ONPLACEDBLOCK, ActMode::BEFORE, jv.toStyledString());
		if (ret) {
			ret = original(_this, pBlk, pBlkpos, a4, pPlayer, _bool);
			jv["result"] = ret;
			runScriptCallBackListener(ActEvent.ONPLACEDBLOCK, ActMode::AFTER, jv.toStyledString());
		}
		return ret;
	}
	return original(_this, pBlk, pBlkpos, a4, pPlayer, _bool);
}

// 玩家破坏方块
THook2(_JS_ONDESTROYBLOCK, bool,
	MSSYM_B2QUE20destroyBlockInternalB1AA8GameModeB2AAA4AEAAB1UE13NAEBVBlockPosB2AAA1EB1AA1Z,
	void* _this, BlockPos* pBlkpos) {
	auto pPlayer = *reinterpret_cast<Player**>(reinterpret_cast<VA>(_this) + 8);
	auto pBlockSource = *(BlockSource**)(*((VA*)_this + 1) + 800);
	auto pBlk = pBlockSource->getBlock(pBlkpos);
	Json::Value jv;
	addPlayerInfo(jv, pPlayer);
	jv["position"] = toJson(pBlkpos->getPosition()->toJsonString());
	jv["blockid"] = pBlk->getLegacyBlock()->getBlockItemID();
	jv["blockname"] = pBlk->getLegacyBlock()->getFullName();
	bool ret = runScriptCallBackListener(ActEvent.ONDESTROYBLOCK, ActMode::BEFORE, jv.toStyledString());
	if (ret) {
		ret = original(_this, pBlkpos);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONDESTROYBLOCK, ActMode::AFTER, jv.toStyledString());
	}
	return ret;
}

// 玩家开箱准备
THook2(_JS_ONCHESTBLOCKUSE, bool,
	MSSYM_B1QA3useB1AE10ChestBlockB2AAA4UEBAB1UE11NAEAVPlayerB2AAE12AEBVBlockPosB3AAAA1Z,
	void* _this, Player* pPlayer, BlockPos* pBlkpos) {
	//auto pBlockSource = (BlockSource*)*((__int64*)pPlayer + 105);
	//auto pBlk = pBlockSource->getBlock(pBlkpos);
	Json::Value jv;
	addPlayerInfo(jv, pPlayer);
	jv["position"] = toJson(pBlkpos->getPosition()->toJsonString());
	bool ret = runScriptCallBackListener(ActEvent.ONSTARTOPENCHEST, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		ret = original(_this, pPlayer, pBlkpos);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONSTARTOPENCHEST, ActMode::AFTER, toJsonString(jv));
	}
	return ret;
}

// 玩家开桶准备
THook2(_JS_ONBARRELBLOCKUSE, bool,
	MSSYM_B1QA3useB1AE11BarrelBlockB2AAA4UEBAB1UE11NAEAVPlayerB2AAE12AEBVBlockPosB3AAAA1Z,
	void* _this, Player* pPlayer, BlockPos* pBlkpos) {
	Json::Value jv;
	addPlayerInfo(jv, pPlayer);
	jv["position"] = toJson(pBlkpos->getPosition()->toJsonString());
	bool ret = runScriptCallBackListener(ActEvent.ONSTARTOPENBARREL, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		ret = original(_this, pPlayer, pBlkpos);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONSTARTOPENBARREL, ActMode::AFTER, toJsonString(jv));
	}
	return ret;
}

// 玩家关闭箱子
THook2(_JS_ONSTOPOPENCHEST, void,
	MSSYM_B1QA8stopOpenB1AE15ChestBlockActorB2AAE15UEAAXAEAVPlayerB3AAAA1Z,
	void* _this, Player* pPlayer) {
	auto real_this = reinterpret_cast<void*>(reinterpret_cast<VA>(_this) - 248);
	auto pBlkpos = reinterpret_cast<BlockActor*>(real_this)->getPosition();
	Json::Value jv;
	addPlayerInfo(jv, pPlayer);
	jv["position"] = toJson(pBlkpos->getPosition()->toJsonString());
	runScriptCallBackListener(ActEvent.ONSTOPOPENCHEST, ActMode::BEFORE, toJsonString(jv));
	original(_this, pPlayer);
	runScriptCallBackListener(ActEvent.ONSTOPOPENCHEST, ActMode::AFTER, toJsonString(jv));
}

// 玩家关闭木桶
THook2(_JS_STOPOPENBARREL, void,
	MSSYM_B1QA8stopOpenB1AE16BarrelBlockActorB2AAE15UEAAXAEAVPlayerB3AAAA1Z,
	void* _this, Player* pPlayer) {
	auto real_this = reinterpret_cast<void*>(reinterpret_cast<VA>(_this) - 248);
	auto pBlkpos = reinterpret_cast<BlockActor*>(real_this)->getPosition();
	Json::Value jv;
	addPlayerInfo(jv, pPlayer);
	jv["position"] = toJson(pBlkpos->getPosition()->toJsonString());
	runScriptCallBackListener(ActEvent.ONSTOPOPENBARREL, ActMode::BEFORE, toJsonString(jv));
	original(_this, pPlayer);
	runScriptCallBackListener(ActEvent.ONSTOPOPENBARREL, ActMode::AFTER, toJsonString(jv));
}

// 玩家放入取出数量
THook2(_JS_ONSETSLOT, void,
	MSSYM_B1QA7setSlotB1AE26LevelContainerManagerModelB2AAE28UEAAXHAEBUContainerItemStackB3AAUA1NB1AA1Z,
	LevelContainerManagerModel* _this, int a2, ContainerItemStack* a3) {
	int slot = a2;
	ContainerItemStack* pItemStack = a3;
	auto nid = pItemStack->getId();
	auto naux = pItemStack->getAuxValue();
	auto nsize = pItemStack->getStackSize();
	auto nname = std::string(pItemStack->getName());

	auto pPlayer = _this->getPlayer();
	VA v3 = *((VA*)_this + 1);				// IDA LevelContainerManagerModel::_getContainer
	BlockSource* bs = *(BlockSource**)(*(VA*)(v3 + 808) + 72);
	BlockPos* pBlkpos = (BlockPos*)((char*)_this + 176);
	Block* pBlk = bs->getBlock(pBlkpos);

	Json::Value jv;
	jv["itemid"] = nid;
	jv["itemcount"] = nsize;
	jv["itemname"] = std::string(nname);
	jv["itemaux"] = naux;
	addPlayerInfo(jv, pPlayer);
	jv["position"] = toJson(pBlkpos->getPosition()->toJsonString());
	jv["blockid"] = pBlk->getLegacyBlock()->getBlockItemID();
	jv["blockname"] = pBlk->getLegacyBlock()->getFullName();
	jv["slot"] = slot;
	bool ret = runScriptCallBackListener(ActEvent.ONSETSLOT, ActMode::BEFORE, jv.toStyledString());
	if (ret) {
		original(_this, slot, pItemStack);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONSETSLOT, ActMode::AFTER, jv.toStyledString());
	}
}

// 玩家切换维度
THook2(_JS_ONCHANGEDIMENSION, bool,
	MSSYM_B2QUE21playerChangeDimensionB1AA5LevelB2AAA4AEAAB1UE11NPEAVPlayerB2AAE26AEAVChangeDimensionRequestB3AAAA1Z,
	void* _this, Player* pPlayer, void* req) {
	Json::Value jv;
	addPlayerInfo(jv, pPlayer);
	jv["position"] = jv["XYZ"];//toJson(pPlayer->getPos()->toJsonString());
	bool ret = runScriptCallBackListener(ActEvent.ONCHANGEDIMENSION, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		ret = original(_this, pPlayer, req);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONCHANGEDIMENSION, ActMode::AFTER, toJsonString(jv));
	}
	return ret;
}

// 生物死亡
THook2(_JS_ONMOBDIE, void,
	MSSYM_B1QA3dieB1AA3MobB2AAE26UEAAXAEBVActorDamageSourceB3AAAA1Z,
	Mob* _this, void* dmsg) {
	Json::Value jv = getDamageInfo(_this, dmsg);
	bool ret = runScriptCallBackListener(ActEvent.ONMOBDIE, ActMode::BEFORE, jv.toStyledString());
	if (ret) {
		original(_this, dmsg);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONMOBDIE, ActMode::AFTER, jv.toStyledString());
	}
}

// 玩家重生
THook2(_JS_PLAYERRESPAWN, void, MSSYM_B1QA7respawnB1AA6PlayerB2AAA7UEAAXXZ,
	Player* pPlayer) {
	Json::Value jv;
	addPlayerInfo(jv, pPlayer);
	bool ret = runScriptCallBackListener(ActEvent.ONRESPAWN, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		original(pPlayer);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONRESPAWN, ActMode::AFTER, toJsonString(jv));
	}
	return;
}

// 聊天消息
THook2(_JS_ONCHAT,void,
	MSSYM_MD5_ad251f2fd8c27eb22c0c01209e8df83c,
	void* _this, std::string& player_name, std::string& target, std::string& msg, std::string& chat_style) {
	Json::Value jv;
	jv["playername"] = player_name;
	jv["target"] = target;
	jv["msg"] = msg;
	jv["chatstyle"] = chat_style;
	bool ret = runScriptCallBackListener(ActEvent.ONCHAT, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		original(_this, player_name, target, msg, chat_style);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONCHAT, ActMode::AFTER, toJsonString(jv));
	}
}

// 输入文本
THook2(JS_ONINPUTTEXT, void,
	MSSYM_B1QA6handleB1AE20ServerNetworkHandlerB2AAE26UEAAXAEBVNetworkIdentifierB2AAE14AEBVTextPacketB3AAAA1Z,
	VA _this, VA id, TextPacket* tp) {
	Json::Value jv;
	Player* p = SYMCALL(Player*, MSSYM_B2QUE15getServerPlayerB1AE20ServerNetworkHandlerB2AAE20AEAAPEAVServerPlayerB2AAE21AEBVNetworkIdentifierB2AAA1EB1AA1Z,
		_this, id, *((char*)tp + 16));
	if (p != NULL) {
		addPlayerInfo(jv, p);
	}
	jv["msg"] = tp->toString();
	bool ret = runScriptCallBackListener(ActEvent.ONINPUTTEXT, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		original(_this, id, tp);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONINPUTTEXT, ActMode::AFTER, toJsonString(jv));
	}
}

// 输入指令
THook2(_JS_ONINPUTCOMMAND, void,
	MSSYM_B1QA6handleB1AE20ServerNetworkHandlerB2AAE26UEAAXAEBVNetworkIdentifierB2AAE24AEBVCommandRequestPacketB3AAAA1Z,
	VA _this, VA id, CommandRequestPacket *crp) {
	Json::Value jv;
	Player* p = SYMCALL(Player*, MSSYM_B2QUE15getServerPlayerB1AE20ServerNetworkHandlerB2AAE20AEAAPEAVServerPlayerB2AAE21AEBVNetworkIdentifierB2AAA1EB1AA1Z,
		_this, id, *((char*)crp + 16));
	if (p != NULL) {
		addPlayerInfo(jv, p);
	}
	jv["cmd"] = crp->toString();
	bool ret = runScriptCallBackListener(ActEvent.ONINPUTCOMMAND, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		original(_this, id, crp);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONINPUTCOMMAND, ActMode::AFTER, toJsonString(jv));
	}
}

// 玩家加载名字
THook2(_JS_ONCREATEPLAYER,Player *,
	MSSYM_B2QUE15createNewPlayerB1AE20ServerNetworkHandlerB2AAE20AEAAAEAVServerPlayerB2AAE21AEBVNetworkIdentifierB2AAE21AEBVConnectionRequestB3AAAA1Z,
	VA a1, VA a2, VA **a3) {
	auto pPlayer = original(a1, a2, a3);
	Json::Value jv;
	jv["playername"] = pPlayer->getNameTag();
	auto uuid = pPlayer->getUuid()->toString();
	jv["uuid"] = uuid;
	jv["xuid"] = std::string(pPlayer->getXuid(p_level));
	onlinePlayers[uuid] = pPlayer;
	playerSign[pPlayer] = true;
	bool ret = runScriptCallBackListener(ActEvent.ONLOADNAME, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONLOADNAME, ActMode::AFTER, toJsonString(jv));
	}
	return pPlayer;
}

// 玩家离开游戏
THook2(_JS_ONPLAYERLEFT, void,
	MSSYM_B2QUE12onPlayerLeftB1AE20ServerNetworkHandlerB2AAE21AEAAXPEAVServerPlayerB3AAUA1NB1AA1Z,
	VA _this, Player* pPlayer, char v3){
	Json::Value jv;
	jv["playername"] = pPlayer->getNameTag();
	auto uuid = pPlayer->getUuid()->toString();
	jv["uuid"] = uuid;
	jv["xuid"] = pPlayer->getXuid(p_level);
	playerSign[pPlayer] = false;
	playerSign.erase(pPlayer);
	onlinePlayers[uuid] = NULL;
	onlinePlayers.erase(uuid);
	bool ret = runScriptCallBackListener(ActEvent.ONPLAYERLEFT, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		original(_this, pPlayer, v3);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONPLAYERLEFT, ActMode::AFTER, toJsonString(jv));
	}
}

// 此处为防止意外崩溃出现，设置常锁
THook2(_JS_ONLOGOUT, VA,
	MSSYM_B3QQUE13EServerPlayerB2AAA9UEAAPEAXIB1AA1Z,
	Player* a1, VA a2) {
	Locker v8lock(isolate);
	if (playerSign[a1]) {				// 非正常登出游戏用户，执行注销
		playerSign[a1] = false;
		playerSign.erase(a1);
		const std::string* uuid = NULL;
		for (auto& p : onlinePlayers) {
			if (p.second == a1) {
				uuid = &p.first;
				break;
			}
		}
		if (uuid)
			onlinePlayers.erase(*uuid);
	}
	return original(a1, a2);
}

// 玩家移动信息构筑
THook2(_JS_ONMOVE, __int64,
	MSSYM_B2QQE170MovePlayerPacketB2AAA4QEAAB1AE10AEAVPlayerB2AAE14W4PositionModeB1AA11B1AA2HHB1AA1Z,
	void* _this, Player* pPlayer, char v3, int v4, int v5) {
	__int64 reto = 0;
	Json::Value jv;
	addPlayerInfo(jv, pPlayer);
	jv["position"] = jv["XYZ"];//toJson(pPlayer->getPos()->toJsonString());
	bool ret = runScriptCallBackListener(ActEvent.ONMOVE, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		reto = original(_this, pPlayer, v3, v3, v4);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONMOVE, ActMode::AFTER, toJsonString(jv));
	}
	return reto;
}

// 玩家攻击时触发调用
THook2(_JS_ONATTACK, bool,
	MSSYM_B1QA6attackB1AA6PlayerB2AAA4UEAAB1UE10NAEAVActorB3AAAA1Z,
	Player* class_this, Actor* pactor) {
	std::string p_player_name = class_this->getNameTag();
	std::string p_actor_name = pactor->getNameTag();
	std::string actor_typename = pactor->getTypeName();
	Vec3* p_position = class_this->getPos();
	Json::Value jv;
	addPlayerInfo(jv, class_this);
	jv["actorname"] = p_actor_name;
	jv["actortype"] = actor_typename;
	jv["position"] = jv["XYZ"];// toJson(p_position->toJsonString());
	std::string strv = toJsonString(jv);
	bool ret = runScriptCallBackListener(ActEvent.ONATTACK, ActMode::BEFORE, toJsonString(jv));
	if (ret) {
		ret = original(class_this, pactor);
		jv["result"] = ret;
		runScriptCallBackListener(ActEvent.ONATTACK, ActMode::AFTER, toJsonString(jv));
	}
	return ret;
}

static bool isinited = false;
static Isolate::CreateParams* pcreate_params = NULL;
static std::unique_ptr<Platform> mplatform;

// 用于被外部程序引用
void onPostInit() {
	if (isinited)
		return;
	isinited = true;
	// 加载所有js文件至内存
	readAllJsFile(JSDIR);
	// 获取BDS完整程序路径
	SetCurrentDirectoryA(getLocalPath().c_str());
	// V8初始化
	V8::InitializeICUDefaultLocation(".");
	V8::InitializeExternalStartupData(".");
	mplatform = platform::NewDefaultPlatform();
	V8::InitializePlatform(mplatform.get());
	V8::Initialize();
	// 创建单独的引擎实例。
	pcreate_params = new Isolate::CreateParams();
	pcreate_params->array_buffer_allocator =
		ArrayBuffer::Allocator::NewDefaultAllocator();
	isolate = Isolate::New(*pcreate_params);
	// 局部作用域
	{
		Locker v8locker(isolate);
		TryCatch try_catch(isolate);
		// 创建各种局部scope。
		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		// 创建对象模板。
		Local<ObjectTemplate> global = ObjectTemplate::New(isolate);
		// TODO 此处注册对象模板中添加C++函数
		Local<Name> name;
		if (String::NewFromUtf8(isolate, u8"log").ToLocal(&name)) {	// 打印函数
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_log));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"logout").ToLocal(&name)) {	// 命令输出函数
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_logout));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"fileReadAllText").ToLocal(&name)) {	// 读取文件
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_fileReadAllText));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"fileWriteAllText").ToLocal(&name)) {	// 写入文件
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_fileWriteAllText));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"fileWriteLine").ToLocal(&name)) {	// 追加写入一行
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_fileWriteLine));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"runScript").ToLocal(&name)) {	// 注入执行一段js文本
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_runScript));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"setCommandDescribe").ToLocal(&name)) {	// 设置一个指令描述
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_setCommandDescribe));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"runcmd").ToLocal(&name)) {	// 执行后台指令
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_runcmd));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"reNameByUuid").ToLocal(&name)) {	// 设置一个玩家名
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_reNameByUuid));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"runcmdAs").ToLocal(&name)) {	// 模拟指令
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_runcmdAs));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"getOnLinePlayers").ToLocal(&name)) {	// 返回在线玩家列表
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_getOnLinePlayers));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"sendSimpleForm").ToLocal(&name)) {	// 发送一个简易表单
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_sendSimpleForm));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"sendModalForm").ToLocal(&name)) {	// 发送一个模式对话框
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_sendModalForm));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"sendCustomForm").ToLocal(&name)) {	// 发送一个自定义表单
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_sendCustomForm));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"releaseForm").ToLocal(&name)) {	// 放弃一个表单
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_releaseForm));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"setShareData").ToLocal(&name)) {	// 设置共享数据
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_setShareData));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"getShareData").ToLocal(&name)) {	// 获取共享数据
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_getShareData));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"removeShareData").ToLocal(&name)) {	// 移除共享数据
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_removeShareData));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"setBeforeActListener").ToLocal(&name)) {	// 设置事件发生前监听器
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_setBeforeActListener));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"setAfterActListener").ToLocal(&name)) {	// 设置事件发生后监听器
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_setAfterActListener));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"getBeforeActListener").ToLocal(&name)) {	// 获取事件发生前监听器
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_getBeforeActListener));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"getAfterActListener").ToLocal(&name)) {	// 获取事件发生后监听器
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_getAfterActListener));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"removeBeforeActListener").ToLocal(&name)) {	// 移除事件发生前监听器
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_removeBeforeActListener));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"removeAfterActListener").ToLocal(&name)) {	// 移除事件发生后监听器
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_removeAfterActListener));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"TimeNow").ToLocal(&name)) {	// 返回一个当前时间字符串
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_TimeNow));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"request").ToLocal(&name)) {	// 远程访问一个HTTP地址
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_request));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"setTimeout").ToLocal(&name)) {	// 延时执行脚本
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_setTimeout));
		}
		else
			prException(isolate, try_catch.Exception());
		// 根据对象模板创建上下文环境，并设置scope。
		for (auto& txt : texts) {
			Persistent<Context>* pcontext = new Persistent<Context>();
			Local<Context> c = Context::New(isolate, NULL, global);
			pcontext->Reset(isolate, c);
			errfiles[pcontext] = txt.first;
			Context::Scope context_scope(c);
			std::string x = txt.second;
			Local<String> source;
			if (String::NewFromUtf8(isolate, x.c_str(), NewStringType::kNormal).ToLocal(&source)) {
				// 从字符串编译出脚本对象。
				Local<Script> script;
				if (Script::Compile(c, source).ToLocal(&script)) {
					// 执行脚本。
					Local<Value> result;
					if (!script->Run(c).ToLocal(&result)) {
						prException(isolate, try_catch.Exception());
					}
				}
				else
					prException(isolate, try_catch.Exception());
			}
			else
				prException(isolate, try_catch.Exception());
		}
	}
}

// 关闭V8引擎，释放资源
static void onRelease() {
	if (isinited) {
		isolate->Dispose();
		V8::Dispose();
		errfiles.clear();
		V8::ShutdownPlatform();
		delete pcreate_params->array_buffer_allocator;
		texts.clear();
	}
}

THook2(_JS_MAIN, int,
	MSSYM_A4main,
	int argc, char* argv[], char* envp[]) {
	onPostInit();

	// 执行 main 函数
	int ret = original(argc, argv, envp);

	return ret;
}

#pragma endregion




void init() {
	std::cout << u8"{[插件]JS插件平台已装载。" << std::endl;
}

void exit() {
	onRelease();
}

