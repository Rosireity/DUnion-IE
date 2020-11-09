#include "net.h"
#include <curl\curl.h>

// ע����� size * nmemb �����ݵ��ܳ���
static size_t netwirte_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
	int l = size * nmemb;
	char* buf = new char[l + 3]{ 0 };
	memcpy(buf, ptr, size * nmemb);
	buf[size * nmemb] = 0;
	*(std::string*)userdata = *(std::string*)userdata + buf;
	delete[] buf;
	return l;
}


std::string netrequest(std::string strUrl, bool mode, std::string& params)
{
	CURL* curl;
	CURLcode res;
	std::string str = "";
	std::string uep = URLTool::UrlEncode(params);
	std::string ueurl = URLTool::UrlEncode(strUrl);
	curl = curl_easy_init();
	if (curl) {
		if (!mode) {
			ueurl += (params != "" ? ("?" + uep) : "");
		}
		curl_easy_setopt(curl, CURLOPT_URL, ueurl.c_str());
		if (mode) {
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, uep.c_str());
			// ����post�ĳ��ȣ�������ı����Բ�������
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, uep.length());
			// ����post�ύ��ʽ
			curl_easy_setopt(curl, CURLOPT_POST, 1);
		}
		else {
			curl_easy_setopt(curl, CURLOPT_POST, 0);
		}
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, netwirte_callback); // ���ûص�����
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &str);
		res = curl_easy_perform(curl);
		if (res == CURLE_OK) {
			// do nothing
		}
		curl_easy_cleanup(curl);
	}
	return str;
}
