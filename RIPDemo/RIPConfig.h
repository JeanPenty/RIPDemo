#pragma once

#include <string>
#include "RIPParams.h"

// ============================================================================
// RIP 界面参数的本地持久化
//
//   - 配置文件: RIPDemo.ini，内容为 UTF-8 的 key=value 文本
//   - 位置: 优先放程序目录(绿色部署，整个目录拷走即带走配置)；
//           程序目录只读时退到 %LOCALAPPDATA%\RIPDemo\
//   - 只保存界面能改动的参数，软件启动时读回来回填控件
// ============================================================================
namespace RIPConfig
{
	// 取配置文件全路径(UTF-8)。首次调用时做一次可写性探测，结果会缓存。
	std::string GetConfigPath();

	// 保存参数；返回 false 表示写盘失败
	bool Save(const RIPParams& params);

	// 读取参数；返回 false 表示文件不存在或读不出内容(此时 params 保持原样)
	bool Load(RIPParams& params);
}
