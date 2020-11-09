#include "Ԥ����ͷ.h"
#include "BDS����.hpp"

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

// ������Ϣ
template<typename T>
static void PR(T arg) {
#ifndef RELEASED
	std::cout << arg << std::endl;
#endif // !RELEASED
}

// ת��Json����Ϊ�ַ���
static std::string toJsonString(Json::Value& v) {
	Json::StreamWriterBuilder w;
	std::ostringstream os;
	std::unique_ptr<Json::StreamWriter> jsonWriter(w.newStreamWriter());
	jsonWriter->write(v, &os);
	return std::string(os.str());
}

// ת���ַ���ΪJson����
static Json::Value toJson(std::string s) {
	Json::Value jv;
	Json::CharReaderBuilder r;
	JSONCPP_STRING errs;
	std::unique_ptr<Json::CharReader> const jsonReader(r.newCharReader());
	bool res = jsonReader->parse(s.c_str(), s.c_str() + s.length(), &jv, &errs);
	if (!res || !errs.empty()) {
		PR("JSONת��ʧ��.." + errs);
	}
	return jv;
}

/*
 UTF-8 ת GBK
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

// �޳����ҿո�
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

// ��ȡBDS��������·��
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

#pragma region JS�ص����API����

#ifndef JSDIR
// js���������Ŀ¼
#define JSDIR u8"js"
#endif // !JSDIR

// ��פ����
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

/**************** JS�ص����� ****************/

// ��������fileReadAllText
// ���ܣ��ļ���������ȡһ���ı�
// ����������1��
// �������ͣ��ַ���
// ������⣺fname - �ļ��������BDSλ�ã�
// ����ֵ���ַ���
static void jsAPI_fileReadAllText(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() < 1)
		return;
	Isolate* niso = info.GetIsolate();
	HandleScope handle_scope(niso);
	String::Utf8Value arg1(niso, info[0]);
	std::string fname = std::string(*arg1);
	// ���ļ�
	DWORD flen, dwHigh, dwBytes;
	HANDLE sfp;
	char* sbuf;
	// �˴������ļ�����
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

// ��������fileWriteAllText
// ���ܣ��ļ������ȫ��д��һ���ַ���
// ����������2��
// �������ͣ��ַ������ַ���
// ������⣺fname - �ļ��������BDSλ�ã���content - �ı�����
// ����ֵ���Ƿ�д�ɹ�
static void jsAPI_fileWriteAllText(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() < 2)
		return;
	bool ret = false;
	Isolate* niso = info.GetIsolate();
	HandleScope handle_scope(niso);
	String::Utf8Value arg1(niso, info[0]);
	String::Utf8Value arg2(niso, info[1]);
	// д�ļ�
	write_lock.lock();
	FILE * f = NULL;
	fopen_s(&f, *arg1, "w");
	if (f != NULL) {
		fputs(*arg2, f);
		fclose(f);
		ret = true;
	}
	else
		PR(u8"�ļ�����ʧ�ܡ���");
	Local<Boolean> result = Boolean::New(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
	write_lock.unlock();
}

// ��������fileWriteLine
// ���ܣ��ļ������׷��һ���ַ���
// ����������2��
// �������ͣ��ַ������ַ���
// ������⣺fname - �ļ��������BDSλ�ã���content - ׷������
// ����ֵ���Ƿ�д�ɹ�
static void jsAPI_fileWriteLine(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() < 2)
		return;
	bool ret = false;
	Isolate* niso = info.GetIsolate();
	HandleScope handle_scope(niso);
	String::Utf8Value arg1(niso, info[0]);
	String::Utf8Value arg2(niso, info[1]);
	std::string content = std::string(*arg2) + '\n';
	// д�ļ�
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
		PR(u8"�ļ�����ʧ�ܡ���");
	Local<Boolean> result = Boolean::New(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
	write_lock.unlock();
}

// ��������log
// ���ܣ���׼�������ӡ��Ϣ
// ����������1��
// �������ͣ��ַ���
// ������⣺���������׼���ַ���
static void jsAPI_log(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value arg(info.GetIsolate(), info[0]);
	std::cout << *arg << std::endl;
}

// ��������logout
// ���ܣ�����һ�����������Ϣ���ɱ����أ�
// ����������1��
// �������ͣ��ַ���
// ������⣺�����͵���������ַ���
static void jsAPI_logout(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value arg(info.GetIsolate(), info[0]);
	logWriteLine(std::string(*arg) + "\n");
}

// ��������TimeNow
// ���ܣ�����һ����ǰʱ����ַ���
// ����������0��
// ����ֵ���ַ���
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

// ��������setShareData
// ���ܣ����빲������
// ����������2��
// �������ͣ��ַ���������/��������
// ������⣺key - �ؼ��֣�value - ��������
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

// ��������removeShareData
// ���ܣ�ɾ����������
// ����������1��
// �������ͣ��ַ���
// ������⣺key - �ؼ���
// ����ֵ��������
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
		// ��������
		info.GetReturnValue().Set(pfunc->Get(isolate));
		pfunc->Reset();
		delete pfunc;
	}
	shareData[cstr] = NULL;
	shareData.erase(cstr);
}

// ��������getShareData
// ���ܣ���ȡ��������
// ����������1��
// �������ͣ��ַ���
// ������⣺key - �ؼ���
// ����ֵ����������
static void jsAPI_getShareData(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	Isolate* niso = isolate;
	Local<Context> ncon = isolate->GetCurrentContext();
	HandleScope handle_scope(niso);
	String::Utf8Value arg1(niso, info[0]);
	Persistent<Object>* pfunc = shareData[*arg1];
	if (pfunc != NULL) {
		// ��������
		info.GetReturnValue().Set(pfunc->Get(isolate));
	}
}

static std::unordered_map<std::string, std::string> cmddescripts;

// ��������setCommandDescribe
// ���ܣ�����һ��ȫ��ָ��˵��
// ����������2��
// �������ͣ��ַ������ַ���
// ������⣺cmd - ���description - ����˵��
// ��ע������ע�����������ܲ���ı�ͻ��˽���
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

// ��������setBeforeActListener
// ���ܣ�ע������¼�����ǰ������
// ����������2��
// �������ͣ��ַ�������������
// ������⣺key - ע���ùؼ��֣�func - ���¼�����ʱ�Ļص���������
static void jsAPI_setBeforeActListener(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	Isolate* niso = info.GetIsolate();
	Local<Context> ncon = niso->GetCurrentContext();
	Persistent<Context>* pncon = getTrueContext(ncon);
	if (pncon != NULL) {	// �ҵ�
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

// ��������removeBeforeActListener
// ���ܣ��Ƴ�����¼�������
// ����������1��
// �������ͣ��ַ���
// ������⣺key - ע���ùؼ���
// ����ֵ���ɼ�����
static void jsAPI_removeBeforeActListener(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	Isolate* niso = isolate;
	Local<Context> ncon = isolate->GetCurrentContext();
	Persistent<Context>* pncon = getTrueContext(ncon);
	if (pncon != NULL) {	// �ҵ�
		HandleScope handle_scope(niso);
		String::Utf8Value arg1(niso, info[0]);
		Persistent<Object>* pfunc = beforecallbacks[*arg1][pncon];
		if (pfunc != NULL) {
			// ��������
			info.GetReturnValue().Set(pfunc->Get(isolate));
			pfunc->Reset();
			delete pfunc;
		}
		beforecallbacks[*arg1][pncon] = NULL;
		beforecallbacks[*arg1].erase(pncon);
	}
}

// ��������getBeforeActListener
// ���ܣ���ȡ����¼�������
// ����������1��
// �������ͣ��ַ���
// ������⣺key - ע���ùؼ���
// ����ֵ����ǰ���õļ�����
static void jsAPI_getBeforeActListener(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	Isolate* niso = isolate;
	Local<Context> ncon = isolate->GetCurrentContext();
	Persistent<Context>* pncon = getTrueContext(ncon);
	if (pncon != NULL) {	// �ҵ�
		HandleScope handle_scope(niso);
		String::Utf8Value arg1(niso, info[0]);
		Persistent<Object>* pfunc = beforecallbacks[*arg1][pncon];
		if (pfunc != NULL) {
			// ��������
			info.GetReturnValue().Set(pfunc->Get(isolate));
		}
	}
}

// ��������setAfterActListener
// ���ܣ�ע������¼����غ������
// ����������2��
// �������ͣ��ַ�������������
// ������⣺key - ע���ùؼ��֣�func - ���¼�����ʱ�Ļص���������
static void jsAPI_setAfterActListener(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	Isolate* niso = info.GetIsolate();
	Local<Context> ncon = niso->GetCurrentContext();
	Persistent<Context>* pncon = getTrueContext(ncon);
	if (pncon != NULL) {	// �ҵ�
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

// ��������removeAfterActListener
// ���ܣ��Ƴ�����¼����غ������
// ����������1��
// �������ͣ��ַ���
// ������⣺key - ע���ùؼ���
// ����ֵ���ɼ�����
static void jsAPI_removeAfterActListener(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	Isolate* niso = isolate;
	Local<Context> ncon = niso->GetCurrentContext();
	Persistent<Context>* pncon = getTrueContext(ncon);
	if (pncon != NULL) {	// �ҵ�
		HandleScope handle_scope(niso);
		String::Utf8Value arg1(niso, info[0]);
		Persistent<Object>* pfunc = aftercallbacks[*arg1][pncon];
		if (pfunc != NULL) {
			// ��������
			info.GetReturnValue().Set(pfunc->Get(isolate));
			pfunc->Reset();
			delete pfunc;
		}
		aftercallbacks[*arg1][pncon] = NULL;
		aftercallbacks[*arg1].erase(pncon);
	}
}

// ��������getAfterActListener
// ���ܣ���ȡ����¼����غ������
// ����������1��
// �������ͣ��ַ���
// ������⣺key - ע���ùؼ���
// ����ֵ����ǰ���õļ�����
static void jsAPI_getAfterActListener(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	Isolate* niso = isolate;
	Local<Context> ncon = niso->GetCurrentContext();
	Persistent<Context>* pncon = getTrueContext(ncon);
	if (pncon != NULL) {	// �ҵ�
		HandleScope handle_scope(niso);
		String::Utf8Value arg1(niso, info[0]);
		Persistent<Object>* pfunc = aftercallbacks[*arg1][pncon];
		if (pfunc != NULL) {
			// ��������
			info.GetReturnValue().Set(pfunc->Get(isolate));
		}
	}
}

// ��������runScript
// ���ܣ�ʹ��ȫ�ֻ���ִ��һ�νű�������ؽ��
// ����������1��
// �������ͣ��ַ���
// ������⣺script - �﷨��ȷ��һ�νű��ı�
// ����ֵ���ű�ִ�н��
static void jsAPI_runScript(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	// �������־ֲ�scope��
	Locker v8lock(isolate);
	Isolate::Scope isolate_scope(isolate);
	HandleScope handle_scope(isolate);
	String::Utf8Value arg(isolate, info[0]);
	TryCatch try_catch(isolate);
	// ���ݶ���ģ�崴�������Ļ�����������scope��
	Local<Context> context = isolate->GetCurrentContext();
	Context::Scope context_scope(context);
	// ��������JavaScript���ַ�����Դ��
	Local<String> source;
	if (String::NewFromUtf8(isolate, *arg, NewStringType::kNormal).ToLocal(&source)) {
		// ���ַ���������ű�����
		Local<Script> script;
		if (Script::Compile(context, source).ToLocal(&script)) {
			// ִ�нű���
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

// ��������runcmd
// ���ܣ�ִ�к�ָ̨��
// ����������1��
// �������ͣ��ַ���
// ������⣺cmd - �﷨��ȷ��MCָ��
// ����ֵ���Ƿ�����ִ��
static void jsAPI_runcmd(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 1)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value arg(info.GetIsolate(), info[0]);
	bool ret = runcmd(*arg);
	// ���÷���ֵ
	Local<Boolean> result = Boolean::New(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
}

// ��������getOnLinePlayers
// ���ܣ���ȡ��������б�
// ����������0��
// ����ֵ������б��Json�ַ���
static void jsAPI_getOnLinePlayers(const FunctionCallbackInfo<Value>& info) {
	HandleScope handle_scope(info.GetIsolate());
	std::string ret = getOnLinePlayers();
	// ���÷���ֵ
	Local<String> result = String::NewFromUtf8(info.GetIsolate(), ret.c_str()).ToLocalChecked();
	info.GetReturnValue().Set(result);
}

// ��������reNameByUuid
// ���ܣ�������һ��ָ���������
// ����������2��
// �������ͣ��ַ������ַ���
// ������⣺uuid - ������ҵ�uuid�ַ�����newName - �µ�����
// ����ֵ���Ƿ������ɹ�
// ����ע���ú������ܲ������ͻ���ʵ����ʾ����
static void jsAPI_reNameByUuid(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value uuid(info.GetIsolate(), info[0]);
	String::Utf8Value name(info.GetIsolate(), info[1]);
	bool ret = reNameByUuid(*uuid, *name);
	// ���÷���ֵ
	Local<Boolean> result = Boolean::New(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
}

// ��������runcmdAs
// ���ܣ�ģ�����ִ��һ��ָ��
// ����������2��
// �������ͣ��ַ������ַ���
// ������⣺uuid - ������ҵ�uuid�ַ�����cmd - ��ģ��ִ�е�ָ��
// ����ֵ���Ƿ��ͳɹ�
static void jsAPI_runcmdAs(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	Isolate* ciso = info.GetIsolate();
	HandleScope handle_scope(ciso);
	String::Utf8Value uuid(ciso, info[0]);
	String::Utf8Value cmd(ciso, info[1]);
	bool ret = runcmdAs(*uuid, *cmd);
	// ���÷���ֵ
	Local<Boolean> result = Boolean::New(ciso, ret);
	info.GetReturnValue().Set(result);
}

// ��������sendSimpleForm
// ���ܣ���ָ������ҷ���һ���򵥱�
// ����������4��
// �������ͣ��ַ������ַ������ַ������ַ���
// ������⣺uuid - ������ҵ�uuid�ַ�����title - �����⣬content - ���ݣ�buttons - ��ť�ı������ַ���
// ����ֵ�������ı�id��Ϊ 0 ��ʾ����ʧ��
static void jsAPI_sendSimpleForm(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 4)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value uuid(info.GetIsolate(), info[0]);
	String::Utf8Value title(info.GetIsolate(), info[1]);
	String::Utf8Value content(info.GetIsolate(), info[2]);
	String::Utf8Value bttxts(info.GetIsolate(), info[3]);
	UINT ret = sendSimpleForm(*uuid, *title, *content, *bttxts);
	// ���÷���ֵ
	Local<Integer> result = Integer::NewFromUnsigned(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
}

// ��������sendModalForm
// ���ܣ���ָ������ҷ���һ��ģʽ�Ի���
// ����������5��
// �������ͣ��ַ������ַ������ַ������ַ������ַ���
// ������⣺uuid - ������ҵ�uuid�ַ�����title - �����⣬content - ���ݣ�button1 ��ť1���⣨����ð�ťselectedΪtrue����button2 ��ť2���⣨����ð�ťselectedΪfalse��
// ����ֵ�������ı�id��Ϊ 0 ��ʾ����ʧ��
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
	// ���÷���ֵ
	Local<Integer> result = Integer::NewFromUnsigned(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
}

// ��������sendCustomForm
// ���ܣ���ָ������ҷ���һ���Զ����
// ����������2��
// �������ͣ��ַ������ַ���
// ������⣺uuid - ������ҵ�uuid�ַ�����json - �Զ������json�ַ�����Ҫʹ���Զ�������ͣ��ο�nk��pm��ʽ��minebbsר����
// ����ֵ�������ı�id��Ϊ 0 ��ʾ����ʧ��
static void jsAPI_sendCustomForm(const FunctionCallbackInfo<Value>& info) {
	if (info.Length() != 2)
		return;
	HandleScope handle_scope(info.GetIsolate());
	String::Utf8Value uuid(info.GetIsolate(), info[0]);
	String::Utf8Value json(info.GetIsolate(), info[1]);
	UINT ret = sendCustomForm(*uuid, *json);
	// ���÷���ֵ
	Local<Integer> result = Integer::NewFromUnsigned(info.GetIsolate(), ret);
	info.GetReturnValue().Set(result);
}

// ��������releaseForm
// ���ܣ�����һ����
// ����������1��
// �������ͣ�����
// ������⣺formid - ��id
// ����ֵ���Ƿ��ͷųɹ�
//����ע���ѱ����յ��ı��ᱻ�Զ��ͷţ�
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
		// ���÷���ֵ
		Local<Boolean> result = Boolean::New(info.GetIsolate(), ret);
		info.GetReturnValue().Set(result);
	}
}

// ��������request
// ���ܣ�����һ��Զ��HTTP����
// ����������4��
// �������ͣ��ַ������ַ������ַ�������������
// ������⣺urlpath - Զ�̽ӿ�·����mode - ���ʷ�ʽ��params - �������ݣ�func - ��ȡ���ݻص�
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

// ��������setTimeout
// ���ܣ���ʱִ��һ��ָ��
// ����������2��
// �������ͣ��ַ���/����������
// ������⣺code - ����ʱִ�е�ָ���ַ���/��������millisec - ��ʱ������
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
				// ����ű���
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
				// �˴�ִ�б���ű�
				TryCatch try_catch(isolate);
				// ���ݶ���ģ�崴�������Ļ�����������scope��
				Context::Scope context_scope(context);
				// ��������JavaScript���ַ�����Դ��
				Local<String> source;
				if (String::NewFromUtf8(isolate, *code, NewStringType::kNormal).ToLocal(&source)) {
					// ���ַ���������ű�����
					Local<Script> script;
					if (Script::Compile(context, source).ToLocal(&script)) {
						// ����ű���
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


/**************** C�ص��ӿڶ������� ****************/

// ͨ��ע���������setListener�ص����󶨹ؼ��ּ���

/**************** ����������� ****************/

//static int txtnum = 0;
static std::map<std::string, std::string> texts;
// �����б�
static std::unordered_map<Persistent<Context>*, std::string> errfiles;

// ���س־û�����ͷ
static Persistent<Context>* getTrueContext(Local<Context>& c) {
	for (auto& e : errfiles) {
		if (e.first->Get(isolate) == c) {
			return e.first;
		}
	}
	return NULL;
}

// ��ȡ�ı��ļ��������ݲ������ַ���
static std::string fileReadAllText(const char* info) {
	// ���ļ�
	std::string str = "";
	DWORD flen, dwHigh, dwBytes;
	HANDLE sfp;
	char* sbuf;
	// �˴������ļ�����
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

// ��ȡ����js�ı����ڴ���
// path - Ŀ¼��ANSI
static void readAllJsFile(std::string path) {
	std::string pair = path + "\\*.js";
	WIN32_FIND_DATAA ffd;
	HANDLE dfh = FindFirstFileA(pair.c_str(), &ffd);
	if (INVALID_HANDLE_VALUE != dfh) {
		//txtnum = 0;
		if (texts.size() != 0) {
			texts.clear();
		}
		//texts	// ���js�ļ���������������
		do
		{
			if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				// Ŀ¼����������
			}
			else
			{
				std::string strFname = std::string(".\\" + path + "\\" + ffd.cFileName);
				PR(u8"��ȡJS�ļ���" + strFname);
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

// JS�������

static std::string getUUID() {
	std::string str = "";
	GUID guid;
	if (!CoCreateGuid(&guid))
	{
		char buffer[64] = { 0 };
		_snprintf_s(buffer, sizeof(buffer),
			//"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",    //��д
			"%08x%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",        //Сд
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

// ʹ��libcurl��ȡԶ�����ݣ���ִ��js�ص�
static void netCurlCallbk(std::string url, bool mode, std::string params, std::string uuid) {
	std::string data = netrequest(url, mode, params);
	Persistent<Object>* p = netfuncs[uuid];
	if (p != NULL) {
		// �ص�
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

// ע��Զ�̵��ò�ִ��
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

// �첽ִ��һ����������
static void runcodeFunc(Persistent<Object>* pscript, Persistent<Context> * tc) {
	// ���ø��־ֲ�scope
	Locker v8lock(isolate);
	Isolate::Scope isolate_scope(isolate);
	HandleScope handle_scope(isolate);
	Local<Context> context = tc->Get(isolate);
	Context::Scope context_scope(context);
	// �������ƻ�ȡ�ű�����
	Handle<Object> value_func;
	Local<Object> funco = pscript->Get(isolate);
	if (funco->IsFunction()) {
		// ���к���
		Local<Value> result;
		if (funco->CallAsFunction(context, context->Global(), 0, NULL).ToLocal(&result)) {
			// �첽���� ���践��
		}
	}
}

// �첽ִ��һ�νű�
static void runcode(Persistent<Script> *pscript, Persistent<Context>* tc) {
	// �������־ֲ�scope��
	Locker v8lock(isolate);
	Isolate::Scope isolate_scope(isolate);
	HandleScope handle_scope(isolate);
	TryCatch try_catch(isolate);
	// ���ݶ���ģ�崴�������Ļ�����������scope��
	Local<Context> context = tc->Get(isolate);
	Context::Scope context_scope(context);
	Local<Script> script = pscript->Get(isolate);
	// ִ�нű���
	Local<Value> result;
	if (script->Run(context).ToLocal(&result)) {
			// �첽ִ�У����践��
	}
	else {
			prException(isolate, try_catch.Exception());
	}
}

// ��ʱִ�нű�
static void delayrun(std::string uuid, int x) {
	std::this_thread::sleep_for(std::chrono::milliseconds(x));
	// �˴��ű����벢ִ��
	Persistent<Script>* c = timeouts[uuid];
	Persistent<Context>* con = delaycontexts[uuid];
	runcode(c, con);
	c->Reset();
	delete c;
	timeouts.erase(uuid);
	delaycontexts.erase(uuid);
};

// ��ʱִ�нű�
static void delayrunfunc(std::string uuid, int x) {
	std::this_thread::sleep_for(std::chrono::milliseconds(x));
	// �˴��ű����벢ִ��
	Persistent<Object>* c = timeoutfuncs[uuid];
	Persistent<Context>* con = delaycontexts[uuid];
	runcodeFunc(c, con);
	c->Reset();
	delete c;
	timeoutfuncs.erase(uuid);
	delaycontexts.erase(uuid);
};

#pragma endregion


#pragma region ���HOOK�������

/**************** �ű�ִ������ ****************/

// ��ӡ�쳣��Ϣ
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

// ����ģʽ
static enum class ActMode : UCHAR
{
	BEFORE = 0,
	AFTER = 1
};

// ͨ���ؼ��ֵ��ü���
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
			// �������ƻ�ȡ�ű�����
			Handle<Object> value_func;
			Local<Object> funco = var.second->Get(isolate);
			if (funco->IsFunction()) {
				// �������
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

// ���йؼ���
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

// ά��IDת��Ϊ�����ַ�
static std::string toDimenStr(int dimensionId) {
	switch (dimensionId) {
	case 0:return u8"������";
	case 1:return u8"����";
	case 2:return u8"ĩ��";
	default:
		break;
	}
	return u8"δ֪ά��";
}

// ��JSON�����и�����һ�����Ϣ
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

// ��JSON�����и������������Ϣ
static void addMobInfo(Json::Value& jv, Mob* p) {
	jv["mobname"] = p->getNameTag();
	int did = p->getDimensionId();
	jv["dimensionid"] = did;
	jv["XYZ"] = toJson(p->getPos()->toJsonString());
}

static VA p_spscqueue;

static VA p_level;

static VA p_ServerNetworkHandle = 0;

// ִ�к��ָ��
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

// ����������
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

// ģ��ָ��
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

// ��ȡ��������б���Ϣ
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

// �ж�ָ���Ƿ�Ϊ����б���ָ��
static bool checkIsPlayer(void* p) {
	return playerSign[(Player*)p];
}

unsigned sendForm(std::string uuid, std::string str)
{
	unsigned fid = getFormId();
	// �˴�����������
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

// ����ָ�����һ����
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

// ����ָ�����һ��ģ��Ի���
static UINT sendModalForm(char* uuid, char* title, char* content, char* button1, char* button2) {
	Player* p = onlinePlayers[uuid];
	if (!playerSign[p])
		return 0;
	std::string str = createModalFormString(title, content, button1, button2);
	return sendForm(uuid, str);
}

// ����ָ�����һ���Զ���GUI����
static UINT sendCustomForm(char* uuid, char* json) {
	Player* p = onlinePlayers[uuid];
	if (!playerSign[p])
		return 0;
	return sendForm(uuid, json);
}

// ��׼����������
static const VA STD_COUT_HANDLE = SYM_OBJECT(VA,
	MSSYM_B2UUA3impB2UQA4coutB1AA3stdB2AAA23VB2QDA5basicB1UA7ostreamB1AA2DUB2QDA4charB1UA6traitsB1AA1DB1AA3stdB3AAAA11B1AA1A); //140721370820768


// ����һ�����������Ϣ
static void logWriteLine(std::string cmdout) {
	SYMCALL(VA, MSSYM_MD5_b5f2f0a753fc527db19ac8199ae8f740, STD_COUT_HANDLE, cmdout.c_str(), cmdout.length());
}

static VA regHandle = 0;

// ע��ָ������
static void setCommandDescribe(const char* cmd, const char* description) {
	Locker v8lock(isolate);
	if (regHandle) {
		std::string c = std::string(cmd);
		std::string ct = std::string(description);
		SYMCALL(VA, MSSYM_MD5_8574de98358ff66b5a913417f44dd706, regHandle, &c, ct.c_str(), 0, 64, 0);
	}
}

// �ش��˺�Դ��Ϣ
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

/**************** ���HOOK���� ****************/


// ��ȡָ�����
THook2(_JS_GETSPSCQUEUE,VA, MSSYM_MD5_3b8fb7204bf8294ee636ba7272eec000,
	VA _this) {
	p_spscqueue = original(_this);
	return p_spscqueue;
}

// ��ȡ��Ϸ��ʼ��ʱ������Ϣ
THook2(_JS_ONGAMESESSION, VA,
	MSSYM_MD5_9f3b3524a8d04242c33d9c188831f836,
	void* a1, void* a2, VA* a3, void* a4, void* a5, void* a6, void* a7) {
	p_ServerNetworkHandle = *a3;
	return original(a1, a2, a3, a4, a5, a6, a7);
}

// ��ȡ��ͼ��ʼ����Ϣ
THook2(_JS_LEVELINIT, VA, MSSYM_MD5_4ff87e34eeebbfcf3f44d8d9ab7658e3,
	VA a1, VA a2, VA a3, VA a4, VA a5, VA a6, VA a7, VA a8, VA a9, VA a10, VA a11, VA a12) {
	VA level = original(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
	p_level = level;
	return level;
}

// ��ȡ��ҳ�ʼ��ʱ��ͼ������Ϣ
THook2(_JS_PLAYERINIT, Player *, MSSYM_MD5_c4b0cddb50ed88e87acce18b5bd3fb8a,
	Player* _this, VA level, __int64 a3, int a4, __int64 a5, __int64 a6, void* uuid, std::string& struuid, __int64* a9, __int64 a10, __int64 a11) {
	p_level = level;
	return original(_this, level, a3, a4, a5, a6, uuid, struuid, a9, a10, a11);
}

// ���������ļ��е�ͼ�����ַ�
THook2(_JS_GETOLDSEEDSTR, UINT, MSSYM_MD5_d2496e689e9641a96868df357e31ad87,
	std::string *pSeedstr, VA a2) {
	oldseedstr = std::string(*pSeedstr);
	return original(pSeedstr, a2);
}

// ��д������Ϸ��Ϣ����������ϢΪ�����ļ�����Ϣ
THook2(_JS_HIDESEEDPACKET, void,
	MSSYM_B1QA5writeB1AE15StartGamePacketB2AAE21UEBAXAEAVBinaryStreamB3AAAA1Z,
	VA _this, VA a2) {
	if (oldseedstr != "") {				// IDA LevelSettings::LevelSettings
		*((UINT*)(_this + 40)) = atoi(oldseedstr.c_str());
	}
	return original(_this, a2);
}

// ע��ָ������������list����ע��
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

// ��������̨����ָ��
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

// ��������ָ̨�����
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

// ���ѡ���
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
			jv["selected"] = fmp->getSelectStr();		// �ر���л��sysca11
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

// ��Ҳ�����Ʒ
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

// ��ҷ��÷���
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

// ����ƻ�����
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

// ��ҿ���׼��
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

// ��ҿ�Ͱ׼��
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

// ��ҹر�����
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

// ��ҹر�ľͰ
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

// ��ҷ���ȡ������
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

// ����л�ά��
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

// ��������
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

// �������
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

// ������Ϣ
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

// �����ı�
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

// ����ָ��
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

// ��Ҽ�������
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

// ����뿪��Ϸ
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

// �˴�Ϊ��ֹ����������֣����ó���
THook2(_JS_ONLOGOUT, VA,
	MSSYM_B3QQUE13EServerPlayerB2AAA9UEAAPEAXIB1AA1Z,
	Player* a1, VA a2) {
	Locker v8lock(isolate);
	if (playerSign[a1]) {				// �������ǳ���Ϸ�û���ִ��ע��
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

// ����ƶ���Ϣ����
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

// ��ҹ���ʱ��������
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

// ���ڱ��ⲿ��������
void onPostInit() {
	if (isinited)
		return;
	isinited = true;
	// ��������js�ļ����ڴ�
	readAllJsFile(JSDIR);
	// ��ȡBDS��������·��
	SetCurrentDirectoryA(getLocalPath().c_str());
	// V8��ʼ��
	V8::InitializeICUDefaultLocation(".");
	V8::InitializeExternalStartupData(".");
	mplatform = platform::NewDefaultPlatform();
	V8::InitializePlatform(mplatform.get());
	V8::Initialize();
	// ��������������ʵ����
	pcreate_params = new Isolate::CreateParams();
	pcreate_params->array_buffer_allocator =
		ArrayBuffer::Allocator::NewDefaultAllocator();
	isolate = Isolate::New(*pcreate_params);
	// �ֲ�������
	{
		Locker v8locker(isolate);
		TryCatch try_catch(isolate);
		// �������־ֲ�scope��
		Isolate::Scope isolate_scope(isolate);
		HandleScope handle_scope(isolate);
		// ��������ģ�塣
		Local<ObjectTemplate> global = ObjectTemplate::New(isolate);
		// TODO �˴�ע�����ģ�������C++����
		Local<Name> name;
		if (String::NewFromUtf8(isolate, u8"log").ToLocal(&name)) {	// ��ӡ����
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_log));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"logout").ToLocal(&name)) {	// �����������
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_logout));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"fileReadAllText").ToLocal(&name)) {	// ��ȡ�ļ�
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_fileReadAllText));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"fileWriteAllText").ToLocal(&name)) {	// д���ļ�
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_fileWriteAllText));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"fileWriteLine").ToLocal(&name)) {	// ׷��д��һ��
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_fileWriteLine));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"runScript").ToLocal(&name)) {	// ע��ִ��һ��js�ı�
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_runScript));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"setCommandDescribe").ToLocal(&name)) {	// ����һ��ָ������
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_setCommandDescribe));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"runcmd").ToLocal(&name)) {	// ִ�к�ָ̨��
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_runcmd));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"reNameByUuid").ToLocal(&name)) {	// ����һ�������
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_reNameByUuid));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"runcmdAs").ToLocal(&name)) {	// ģ��ָ��
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_runcmdAs));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"getOnLinePlayers").ToLocal(&name)) {	// ������������б�
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_getOnLinePlayers));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"sendSimpleForm").ToLocal(&name)) {	// ����һ�����ױ�
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_sendSimpleForm));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"sendModalForm").ToLocal(&name)) {	// ����һ��ģʽ�Ի���
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_sendModalForm));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"sendCustomForm").ToLocal(&name)) {	// ����һ���Զ����
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_sendCustomForm));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"releaseForm").ToLocal(&name)) {	// ����һ����
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_releaseForm));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"setShareData").ToLocal(&name)) {	// ���ù�������
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_setShareData));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"getShareData").ToLocal(&name)) {	// ��ȡ��������
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_getShareData));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"removeShareData").ToLocal(&name)) {	// �Ƴ���������
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_removeShareData));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"setBeforeActListener").ToLocal(&name)) {	// �����¼�����ǰ������
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_setBeforeActListener));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"setAfterActListener").ToLocal(&name)) {	// �����¼������������
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_setAfterActListener));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"getBeforeActListener").ToLocal(&name)) {	// ��ȡ�¼�����ǰ������
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_getBeforeActListener));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"getAfterActListener").ToLocal(&name)) {	// ��ȡ�¼������������
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_getAfterActListener));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"removeBeforeActListener").ToLocal(&name)) {	// �Ƴ��¼�����ǰ������
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_removeBeforeActListener));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"removeAfterActListener").ToLocal(&name)) {	// �Ƴ��¼������������
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_removeAfterActListener));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"TimeNow").ToLocal(&name)) {	// ����һ����ǰʱ���ַ���
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_TimeNow));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"request").ToLocal(&name)) {	// Զ�̷���һ��HTTP��ַ
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_request));
		}
		else
			prException(isolate, try_catch.Exception());
		if (String::NewFromUtf8(isolate, u8"setTimeout").ToLocal(&name)) {	// ��ʱִ�нű�
			global->Set(name, FunctionTemplate::New(isolate, jsAPI_setTimeout));
		}
		else
			prException(isolate, try_catch.Exception());
		// ���ݶ���ģ�崴�������Ļ�����������scope��
		for (auto& txt : texts) {
			Persistent<Context>* pcontext = new Persistent<Context>();
			Local<Context> c = Context::New(isolate, NULL, global);
			pcontext->Reset(isolate, c);
			errfiles[pcontext] = txt.first;
			Context::Scope context_scope(c);
			std::string x = txt.second;
			Local<String> source;
			if (String::NewFromUtf8(isolate, x.c_str(), NewStringType::kNormal).ToLocal(&source)) {
				// ���ַ���������ű�����
				Local<Script> script;
				if (Script::Compile(c, source).ToLocal(&script)) {
					// ִ�нű���
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

// �ر�V8���棬�ͷ���Դ
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

	// ִ�� main ����
	int ret = original(argc, argv, envp);

	return ret;
}

#pragma endregion




void init() {
	std::cout << u8"{[���]JS���ƽ̨��װ�ء�" << std::endl;
}

void exit() {
	onRelease();
}

