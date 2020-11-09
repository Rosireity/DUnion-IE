#include "..\‘§±‡“ÎÕ∑.h"
#include <string>
#include <json\json.h>
#include "../BDSƒ⁄»›.hpp"
#include "../tick/tick.h"

static std::map<unsigned, bool> fids;

unsigned getFormId() {
	unsigned id = time(0);
	do {
		--id;
	} while (id == 0 || fids[id]);
	fids[id] = true;
	return id;
}

bool destroyForm(unsigned fid)
{
	if (fids[fid]) {
		fids.erase(fid);
		return true;
	}
	return false;
}

std::string createSimpleFormString(std::string title, std::string content, Json::Value & bttxts) {
	Json::Value jv;
	jv["type"] = "form";
	jv["title"] = title;
	jv["content"] = content;
	jv["buttons"] = bttxts;
	return jv.toStyledString();
}

std::string createModalFormString(std::string title, std::string content, std::string button1, std::string button2) {
	Json::Value jv;
	jv["type"] = "modal";
	jv["title"] = title;
	jv["content"] = content;
	jv["button1"] = button1;
	jv["button2"] = button2;
	return jv.toStyledString();
}
