#pragma once
#include "../预编译头.h"
#include <queue>
#include <functional>
#include "tick.h"

// 危险任务队列
static std::queue<std::function<void()>> todos;

void safeTick(std::function<void()> F) {
	todos.push(F);
}

// 单次tick
THook2(_JS_LEVELTICK, void,
	MSSYM_B1QA4tickB1AA5LevelB2AAA7UEAAXXZ,
	VA a1, VA a2, VA a3, VA a4) {
	original(a1, a2, a3, a4);
	int l = todos.size();
	if (l > 0) {
		for (int i = 0; i < l; i++) {
			todos.front()();
			todos.pop();
		}
	}
}