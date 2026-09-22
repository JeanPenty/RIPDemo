// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//  are changed infrequently
//

#pragma once

#define  _CRT_SECURE_NO_WARNINGS
#define	 DLL_SOUI
#include <souistd.h>
#include <core/SHostDialog.h>
#include <control/SMessageBox.h>
#include <control/SouiCtrls.h>
#include <res.mgr/SObjDefAttr.h>
#include <com-cfg.h>
#include <SouiFactory.h>
#include "resource.h"
using namespace SNS;

//mupdf
#include "mupdf/fitz.h"
#include "mupdf/pdf.h"
#include "mupdf/fitz/color.h"

//tiff
#include "tiffio.h"

//lcms
#include "lcms2.h"

#include <string>
#include <vector>
#include <memory>

#include <chrono> // 添加此头文件用于计时
#include <iostream>

#include <ShlObj_core.h>
#include <chrono>
#include <GdiPlus.h>
using namespace Gdiplus;
#pragma comment(lib, "Gdiplus.lib")

#include "FuncTimer.h"
#define FUNC_TIME_COST() FuncTimer __func_timer__(__FUNCTION__)
#define PERF_SCOPE(name, ms) PerfScope __perf_scope_##__LINE__(name, ms)
#define PERF_LOG	//用来控制是否打印性能监控日志


#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

// 确保在 Debug 模式下，所有的 new 都会被重定向到带行号的调试版本
#ifdef _DEBUG
#ifndef DBG_NEW
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#define new DBG_NEW
#endif
#endif