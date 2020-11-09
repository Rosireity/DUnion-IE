// 文件名：testcase
// 文件功能：测试附加功能是否正常

/******************************** 预处理设置区 ********************************/

// 表单事件处理器
var formcallback = {};
// 表单超时计时器
var timeouts = {};

function getUuid(p) {
	let pl = getOnLinePlayers();
	if (pl != null) {
		let jpl = JSON.parse(pl);
		if (jpl != null) {
			for (let i = 0, l = jpl.length; i < l; i++) {
				let jp = jpl[i];
				if (jp.playername == p)	// 找到
					return jp.uuid;
            }
        }
	}
	return null;
}

const strtest = ['后台指令tell', '前缀/去前缀名', '模拟执行me指令'];

// 定义前缀、取消前缀
function cusName(p) {
	const pre = '[前缀]';
	let i = p.indexOf(pre);
	if (i > -1) {
		return p.substring(pre.length);
	}
	return pre + p;
}

// 针对某玩家进行测试
function testcasefunc(p) {
	let uuid = getUuid(p);
	if (uuid != null) {
		// 即将进入测试环节
		let ghandle = sendSimpleForm(uuid, '测试用例', '请在30秒内做出选择测试项：', JSON.stringify(strtest));
		let strhandle = "" + ghandle;
		timeouts[strhandle] = Date.now();	// 设置一个计时器
		formcallback[strhandle] = function (e) {
			timeouts[strhandle] = null;	// 撤计时器
			delete timeouts[strhandle];
			switch (e.selected) {
				case "0":
					runcmd('tell "' + p + '" 你好 js');
					break;
				case "1":
					reNameByUuid(uuid, cusName(p));
					break;
				case "2":
					runcmdAs(uuid, '/me 你好 js');
					break;
				case "null":
					log('玩家' + p + '取消了选择测试项。');
					break;
			}
			formcallback[strhandle] = null;		// 撤回调处理
			delete formcallback[strhandle];
		};
		setTimeout(function () {	// 超时检测
			if (timeouts[strhandle] != null) {
				let nat = Date.now();
				log('当前待选择时间：' + nat + ',预设时间：' + timeouts[strhandle]);
				if (nat - timeouts[strhandle] > 30000) {
					// 超时
					log('玩家' + p + '的表单已超时, fid=' + strhandle);
					releaseForm(ghandle);
					formcallback[strhandle] = null;
					delete formcallback[strhandle];
					timeouts[strhandle] = null;
					delete timeouts[strhandle];
				}
			}
		}, 31000);
    }
}

function _testcase_trans(c, p) {
	let dc = c.trim();
	if (dc == '/testcase') {		// 进入检测环节
		setTimeout('testcasefunc("' + p + '")', 100);
		return false;
	}
	return true;
}

function resolve(e) {
	if (formcallback[e.formid] != null) {
		formcallback[e.formid](e);
    }
}

/******************************** 监听器设置区 ********************************/

// 设置GUI接收监听器
setAfterActListener('onFormSelect', function (e) {
	let je = JSON.parse(e);
	resolve(je);
});

setBeforeActListener('onInputCommand', function (e) {
    let je = JSON.parse(e);
	let r = _testcase_trans(je.cmd, je.playername);
	if (!r)
		return false;	// 直接拦截
	return true;
});

// 注册指令
setCommandDescribe('testcase', '测试附加功能是否正常');

log('testcase 已加载。用法(仅限玩家)：/testcase');