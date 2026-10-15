﻿// MainDlg.cpp : implementation of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "MainDlg.h"	
#include "RIPConfig.h"
#include <helper/SMenuEx.h>
#include <process.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

namespace
{
	// 「界面参数 / 作业列表」防抖落盘定时器(每次变化重设 = 停止操作 1.5s 后写一次)
	const UINT_PTR kAutoSaveTimerId = 0x1A21;

	// 向 UI 线程投递一条任务状态(堆对象由 UI 线程接管释放)
	void PostRipStatus(RipJob* pJob, int nIndex, int nStatus, const SStringW& sstrText)
	{
		if (!pJob || !::IsWindow(pJob->m_hWndNotify)) return;

		RipNotify* pN = new RipNotify;
		pN->m_nIndex = nIndex;
		pN->m_nStatus = nStatus;
		pN->m_nGen = pJob->m_nGen;
		pN->m_sstrText = sstrText;
		::PostMessage(pJob->m_hWndNotify, WM_RIP_ITEM_STATUS, (WPARAM)nIndex, (LPARAM)pN);
	}

	// 向 UI 线程投递第 1 页的墨点统计(堆对象由 UI 线程接管释放)
	void PostDotStat(RipJob* pJob, int nIndex, const RIPPageResult& result, int nDpi, int nBit)
	{
		if (!pJob || !::IsWindow(pJob->m_hWndNotify)) return;
		if (result.m_vecDotStats.empty()) return;

		RipDotStatNotify* pN = new RipDotStatNotify;
		pN->m_nIndex = nIndex;
		pN->m_nGen = pJob->m_nGen;
		pN->m_nDpi = nDpi;
		pN->m_nBit = nBit;
		pN->m_vecStats = result.m_vecDotStats;
		::PostMessage(pJob->m_hWndNotify, WM_RIP_DOTSTAT, (WPARAM)pN->m_nGen, (LPARAM)pN);
	}
}

CMainDlg::CMainDlg() : SHostWnd(_T("LAYOUT:XML_MAINWND"))
{
}

CMainDlg::~CMainDlg()
{
	// 等 RIP 工作线程收尾(适配器由 SListView 持有, 不在此释放)
	StopRipQueue();
	m_pRIPTaskAdapter = NULL;
}

void CMainDlg::SaveCMYKToTIFF(const char* filename, unsigned char* pData, int width, int height)
{
// 	TIFF* tif = TIFFOpen(filename, "w");
// 	if (!tif) return;
// 
// 	// 设置基础信息
// 	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, width);
// 	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, height);
// 	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
// 	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 4);      // CMYK = 4 个通道
// 	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_SEPARATED);
// 	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG); // 像素连续存储 (C M Y K C M Y K)
// 	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
// 
// 	//按行写入，防止大内存一次性拷贝
// 	tsize_t scanlineSize = TIFFScanlineSize(tif);
// 	for (int row = 0; row < height; row++)
// 	{
// 		unsigned char* pRow = pData + (row * width * 4);
// 		if (TIFFWriteScanline(tif, pRow, row) < 0)
// 		{
// 			break;
// 		}
// 	}
// 
// 	TIFFClose(tif);
}

BOOL CMainDlg::OnInitDialog(HWND hWnd, LPARAM lParam)
{
	//测试jemalloc
	{
		const size_t size = 1024 * 1024;
		void* p = je_malloc(size);
		if (p == nullptr)
		{
			printf("je_malloc failed!\n");
			return 0;
		}

		printf("je_malloc success: %p\n", p);
		memset(p, 0x55, size);
		size_t usable = je_malloc_usable_size(p);
		printf("requested : %zu bytes\n", size);
		printf("usable    : %zu bytes\n", usable);

		je_free(p);

		printf("je_free success\n");
	}

	SListView* pLvRIPTask = FindChildByName2<SListView>(L"lv_rip_task");
	SASSERT(pLvRIPTask);
	pLvRIPTask->EnableScrollBar(SSB_HORZ, FALSE);	//禁用掉水平滚动条
	m_pRIPTaskAdapter = new CLvRIPTaskAdapter(pLvRIPTask, this);
	pLvRIPTask->SetAdapter(m_pRIPTaskAdapter);
	m_pRIPTaskAdapter->Release();

	// ========================================================================
	// 初始化右侧 RIP 参数控件
	//   1) 先给一套「出厂默认值」(RIPParams 的构造函数就是默认值)
	//   2) 再用程序目录下的 RIPDemo.ini 覆盖(首次运行没有文件就保持默认)
	// ========================================================================
	{
		RIPParams params;

		// 默认 ICC 曲线: 程序目录下随程序发布的印刷曲线
		{
			SStringW sstrIcc = CGlobalUnits::Instance().m_sstrAppPath + L"900600_sml_20241004.icc";
			if (::GetFileAttributesW(sstrIcc.c_str()) != INVALID_FILE_ATTRIBUTES)
				params.m_strCMYKICC = RIPExt::WStringToUtf8(std::wstring(sstrIcc.c_str()));
		}

		// 本地配置文件优先
		RIPConfig::Load(params);

		ApplyRipParamsToUI(params);
		// 上次退出时的作业列表: 重建左侧列表(含勾选状态, 文件已不存在的跳过)
		RestoreTaskListFromConfig(params);
	}

	// 命令行传入的 PDF(双击文件 / 拖到 exe 上打开)直接进列表
	AddCommandLinePdfs();

	// 墨滴体积(皮升) + 墨价编辑框: 内容一变(EventRENotify)立刻重算耗墨量与成本,
	// 不用等失焦、也不用按回车。
	{
		LPCWSTR kDropEdits[] = { L"edit_drop_small_pl", L"edit_drop_mid_pl",
								 L"edit_drop_big_pl",   L"edit_ink_price_ml" };
		for (int i = 0; i < 4; ++i)
		{
			SEdit* pEdit = FindChildByName2<SEdit>(kDropEdits[i]);
			if (!pEdit) continue;
			pEdit->GetEventSet()->subscribeEvent(EventRENotify::EventID,
				Subscriber(&CMainDlg::OnInkEditChanged, this));
		}
	}

	// 右侧「墨点统计」面板先刷一次空态提示(此时还没有选中任何文件)
	ShowDotStatPanel(-1);

	return 0;
}

// ============================================================================
// 启动时把命令行里的 PDF 加进列表
//   用法: RIPDemo.exe D:\xx\a.pdf [b.pdf ...]
//   带 - 开头的参数一律忽略(给自测开关用), 非 .pdf 的也忽略
// ============================================================================
void CMainDlg::AddCommandLinePdfs()
{
	if (!m_pRIPTaskAdapter) return;

	int argc = 0;
	LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
	if (!argv) return;

	for (int i = 1; i < argc; ++i)
	{
		std::wstring w = argv[i];
		if (w.empty() || w[0] == L'-' || w[0] == L'/') continue;
		if (w.size() < 5) continue;

		// 扩展名必须是 .pdf(不区分大小写)
		std::wstring ext = w.substr(w.size() - 4);
		for (size_t k = 0; k < ext.size(); ++k) ext[k] = (wchar_t)towlower(ext[k]);
		if (ext != L".pdf") continue;

		// 相对路径转绝对, 再确认文件存在
		std::wstring full = w;
		if (full.find(L':') == std::wstring::npos)
		{
			wchar_t buf[MAX_PATH] = { 0 };
			if (::GetFullPathNameW(w.c_str(), MAX_PATH, buf, nullptr) > 0)
				full = buf;
		}
		if (::GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES)
			continue;

		// 已经在列表里(比如刚从配置重建过)就不重复添加
		if (m_pRIPTaskAdapter->FindRipTask(full.c_str()) >= 0)
			continue;

		m_pRIPTaskAdapter->AddRipTask(full.c_str());
	}
	::LocalFree(argv);
}

// ============================================================================
// 用配置里的作业列表重建左侧列表
//   每项 "<0|1>|<UTF-8 路径>|<dpi>|<bit>|<色版数>|<色版统计;...>"(见 RIPParams);
//   文件已不存在的直接跳过; 上次存下的第 1 页墨点统计一并恢复
// ============================================================================
void CMainDlg::RestoreTaskListFromConfig(const RIPParams& params)
{
	if (!m_pRIPTaskAdapter) return;

	for (size_t i = 0; i < params.m_vecTaskList.size(); ++i)
	{
		bool bChecked = true;
		std::string strPath;
		int nDpi = 0, nBit = 1;
		std::vector<RIPDotStat> vecStats;

		if (!RIPTaskEntry::Decode(params.m_vecTaskList[i],
				bChecked, strPath, nDpi, nBit, vecStats))
			continue;
		if (strPath.empty()) continue;

		const std::wstring w = RIPExt::Utf8ToWString(strPath);
		if (w.empty()) continue;
		if (::GetFileAttributesW(w.c_str()) == INVALID_FILE_ATTRIBUTES)
			continue;						// 路径已失效(文件被移走/改名的)

		// 上次 RIP 存下的第 1 页墨点统计一起恢复
		m_pRIPTaskAdapter->AddRipTask(w.c_str(), bChecked, &vecStats, nDpi, nBit);
	}
}

// ============================================================================
// 界面参数 / 作业列表变了: 延迟 1.5s 写盘(同一个定时器重复设置会重置计时 = 防抖),
//   这样改完参数或增删文件后即使程序异常退出, 配置也基本不会丢。
// ============================================================================
void CMainDlg::ScheduleConfigSave()
{
	SetTimer(kAutoSaveTimerId, 1500);
}

BOOL CMainDlg::OnAutoSaveTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	KillTimer(kAutoSaveTimerId);

	RIPParams params;
	CollectRipParams(params);
	RIPConfig::Save(params);
	return TRUE;
}

// ============================================================================
// 把一份 RIP 参数回填到界面控件
// ============================================================================
void CMainDlg::ApplyRipParamsToUI(const RIPParams& params)
{
	// ---- 位深: 1bit / 2bit ----
	SComboBox* pCbxBit = FindChildByName2<SComboBox>(L"cbx_nozzle_type");
	if (pCbxBit) pCbxBit->SetCurSel(params.BitDepth() == 2 ? 1 : 0);

	// ---- 输出分辨率 ----
	SEdit* pEditXDpi = FindChildByName2<SEdit>(L"edit_x_dpi");
	if (pEditXDpi) pEditXDpi->SetWindowText(SStringW().Format(L"%d", params.XDpi()));
	SEdit* pEditYDpi = FindChildByName2<SEdit>(L"edit_y_dpi");
	if (pEditYDpi) pEditYDpi->SetWindowText(SStringW().Format(L"%d", params.YDpi()));

	// ---- 专色分离 ----
	SCheckBox* pSpot = FindChildByName2<SCheckBox>(L"check_spot");
	if (pSpot) pSpot->SetCheck(params.m_bSpotSeparate ? TRUE : FALSE);

	// ---- 灰色只用K ----
	SCheckBox* pGrayK = FindChildByName2<SCheckBox>(L"check_gray2k");
	if (pGrayK) pGrayK->SetCheck(params.m_bGrayToK ? TRUE : FALSE);

	// ---- 去毛边(抗锯齿) ----
	SCheckBox* pNoAA = FindChildByName2<SCheckBox>(L"check_noaa");
	if (pNoAA) pNoAA->SetCheck(params.m_bNoAA ? TRUE : FALSE);
	SEdit* pEditNoAA = FindChildByName2<SEdit>(L"edit_noaa");
	if (pEditNoAA) pEditNoAA->SetWindowText(SStringW().Format(L"%d", params.NoAAStrength()));

	// ---- 输出 PRN(喷头板卡容器: 0=不输出 1=PRN1RipHeader 2=PRN2RipHeader) ----
	SComboBox* pCbxPrn = FindChildByName2<SComboBox>(L"cb_prn");
	if (pCbxPrn)
	{
		int v = params.m_nPrnVersion;
		if (v < 0) v = 0;
		if (v > 2) v = 2;
		pCbxPrn->SetCurSel(v);
	}

	// ---- 抖动算法(0=纯阈值 1=有序网屏 2=FS误差扩散) ----
	// 下拉项顺序: 0=FS误差扩散 1=有序网屏 2=纯阈值, 与 DitherMode 取值正好相反
	SComboBox* pCbxDither = FindChildByName2<SComboBox>(L"cb_dither");
	if (pCbxDither)
	{
		const int nMode = params.DitherMode();
		pCbxDither->SetCurSel(nMode == 2 ? 0 : nMode);
	}
	SCheckBox* pFSSerp = FindChildByName2<SCheckBox>(L"check_fsserp");
	if (pFSSerp) pFSSerp->SetCheck(params.m_bFSSerpentine ? TRUE : FALSE);

	// ---- 并行线程数(0 = 自动) ----
	SEdit* pEditThreads = FindChildByName2<SEdit>(L"edit_threads");
	if (pEditThreads) pEditThreads->SetWindowText(SStringW().Format(L"%d", params.ThreadCount()));

	// ---- CMYK 各分量阈值 ----
	const int kThr[4] = { params.ThrC(), params.ThrM(), params.ThrY(), params.ThrK() };
	LPCWSTR kThrEdits[] = { L"edit_threshold_c", L"edit_threshold_m", L"edit_threshold_y", L"edit_threshold_k" };
	for (int i = 0; i < 4; ++i)
	{
		SEdit* pEdit = FindChildByName2<SEdit>(kThrEdits[i]);
		if (pEdit) pEdit->SetWindowText(SStringW().Format(L"%d", kThr[i]));
	}

	// ---- ICC 曲线 ----
	// 配置里记的文件可能已被移动/删除，这时退回程序目录的默认曲线，
	// 否则软件一启动就带着一条无效路径，点「开始RIP」直接失败。
	SEdit* pEditIcc = FindChildByName2<SEdit>(L"edit_icc_path");
	if (pEditIcc)
	{
		SStringW sstrIcc;
		if (!params.m_strCMYKICC.empty())
			sstrIcc = RIPExt::Utf8ToWString(params.m_strCMYKICC).c_str();

		if (sstrIcc.IsEmpty() || ::GetFileAttributesW(sstrIcc.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			SStringW sstrDef = CGlobalUnits::Instance().m_sstrAppPath + L"900600_sml_20241004.icc";
			if (::GetFileAttributesW(sstrDef.c_str()) != INVALID_FILE_ATTRIBUTES)
				sstrIcc = sstrDef;
		}

		if (!sstrIcc.IsEmpty())
			pEditIcc->SetWindowText(sstrIcc);
	}

	// ---- 输出根目录(留空 = 跟随 PDF 所在目录) ----
	SEdit* pEditOutRoot = FindChildByName2<SEdit>(L"edit_outroot");
	if (pEditOutRoot)
	{
		if (!params.m_strOutputRoot.empty())
			pEditOutRoot->SetWindowText(RIPExt::Utf8ToWString(params.m_strOutputRoot).c_str());
		else
			pEditOutRoot->SetWindowText(L"");
	}

	// ---- 墨滴体积(皮升 pL): 小点 / 中点 / 大点 ----
	{
		const double kPL[3] = { params.DropSmallPL(), params.DropMidPL(), params.DropBigPL() };
		LPCWSTR kNames[] = { L"edit_drop_small_pl", L"edit_drop_mid_pl", L"edit_drop_big_pl" };
		for (int i = 0; i < 3; ++i)
		{
			SEdit* pEdit = FindChildByName2<SEdit>(kNames[i]);
			if (pEdit) pEdit->SetWindowText(SStringW().Format(L"%.4g", kPL[i]));
		}
	}

	// ---- 墨水单价(元 / 毫升) ----
	{
		SEdit* pEdit = FindChildByName2<SEdit>(L"edit_ink_price_ml");
		if (pEdit) pEdit->SetWindowText(SStringW().Format(L"%.6g", params.InkPricePerML()));
	}
}

// ============================================================================
// 从界面控件收集一份完整的 RIP 参数
// ============================================================================
void CMainDlg::CollectRipParams(RIPParams& params)
{
	// ---- 位深: 1bit / 2bit ----
	SComboBox* pCbxBit = FindChildByName2<SComboBox>(L"cbx_nozzle_type");
	const int nBitSel = pCbxBit ? pCbxBit->GetCurSel() : 0;
	params.m_nBitDepth = (nBitSel == 1) ? 2 : 1;

	// ---- 输出分辨率 ----
	int nXDpi = 300, nYDpi = 300;
	SEdit* pEditXDpi = FindChildByName2<SEdit>(L"edit_x_dpi");
	if (pEditXDpi)
	{
		const int v = _wtoi(pEditXDpi->GetWindowText().c_str());
		if (v > 0) nXDpi = v;
	}
	SEdit* pEditYDpi = FindChildByName2<SEdit>(L"edit_y_dpi");
	if (pEditYDpi)
	{
		const int v = _wtoi(pEditYDpi->GetWindowText().c_str());
		if (v > 0) nYDpi = v;
	}
	params.m_nXDpi = nXDpi;
	params.m_nYDpi = nYDpi;

	// ---- 专色分离 ----
	SCheckBox* pSpot = FindChildByName2<SCheckBox>(L"check_spot");
	params.m_bSpotSeparate = pSpot ? (pSpot->IsChecked() != FALSE) : false;

	// ---- 灰色只用K ----
	SCheckBox* pGrayK = FindChildByName2<SCheckBox>(L"check_gray2k");
	if (pGrayK) params.m_bGrayToK = (pGrayK->IsChecked() != FALSE);

	// ---- 去毛边(抗锯齿) ----
	SCheckBox* pNoAA = FindChildByName2<SCheckBox>(L"check_noaa");
	if (pNoAA) params.m_bNoAA = (pNoAA->IsChecked() != FALSE);
	{
		SEdit* pEditNoAA = FindChildByName2<SEdit>(L"edit_noaa");
		if (pEditNoAA)
		{
			int v = _wtoi(pEditNoAA->GetWindowText().c_str());
			if (v < 0) v = 0;
			if (v > 100) v = 100;
			params.m_nNoAAStrength = v;
		}
	}

	// ---- 输出 PRN(喷头板卡容器: 0=不输出 1=PRN1RipHeader 2=PRN2RipHeader) ----
	{
		SComboBox* pCbxPrn = FindChildByName2<SComboBox>(L"cb_prn");
		int v = pCbxPrn ? pCbxPrn->GetCurSel() : 0;
		if (v < 0) v = 0;
		if (v > 2) v = 2;
		params.m_nPrnVersion = v;
	}

	// ---- CMYK 各分量阈值(0~255) ----
	LPCWSTR kThrEdits[] = { L"edit_threshold_c", L"edit_threshold_m", L"edit_threshold_y", L"edit_threshold_k" };
	int* pThr[4] = { &params.m_nThrC, &params.m_nThrM, &params.m_nThrY, &params.m_nThrK };
	for (int i = 0; i < 4; ++i)
	{
		SEdit* pEdit = FindChildByName2<SEdit>(kThrEdits[i]);
		if (!pEdit) continue;
		*pThr[i] = RIPParams::Clamp255(_wtoi(pEdit->GetWindowText().c_str()));
	}

	// ---- ICC 分色曲线 ----
	SEdit* pEditIcc = FindChildByName2<SEdit>(L"edit_icc_path");
	if (pEditIcc)
	{
		SStringW sstrIcc = pEditIcc->GetWindowText();
		params.m_strCMYKICC = RIPExt::WStringToUtf8(std::wstring(sstrIcc.c_str()));
	}

	// ---- 输出根目录(留空 = 跟随 PDF 所在目录) ----
	SEdit* pEditOutRoot = FindChildByName2<SEdit>(L"edit_outroot");
	if (pEditOutRoot)
	{
		SStringW sstrRoot = pEditOutRoot->GetWindowText();
		sstrRoot.Trim();
		params.m_strOutputRoot = sstrRoot.IsEmpty()
			? std::string() : RIPExt::WStringToUtf8(std::wstring(sstrRoot.c_str()));
	}
	else
	{
		params.m_strOutputRoot.clear();
	}

	// ---- 抖动算法 ----
	{
		// 下拉项顺序: 0=FS误差扩散 1=有序网屏 2=纯阈值 -> DitherMode 2/1/0
		SComboBox* pCbxDither = FindChildByName2<SComboBox>(L"cb_dither");
		const int nSel = pCbxDither ? pCbxDither->GetCurSel() : 0;
		params.m_nDitherMode = (nSel == 1) ? 1 : ((nSel == 2) ? 0 : 2);

		SCheckBox* pFSSerp = FindChildByName2<SCheckBox>(L"check_fsserp");
		if (pFSSerp) params.m_bFSSerpentine = (pFSSerp->IsChecked() != FALSE);
	}

	// ---- 并行线程数(0 = 自动; 上限 32, 实际还会被待处理页数收敛) ----
	{
		int v = 0;
		SEdit* pEditThreads = FindChildByName2<SEdit>(L"edit_threads");
		if (pEditThreads)
		{
			v = _wtoi(pEditThreads->GetWindowText().c_str());
			if (v < 0) v = 0;
			if (v > 32) v = 32;
		}
		params.m_nThreadCount = v;
	}

	// ---- 墨滴体积(皮升) + 墨水单价(元/mL): 只用于把点数换算成耗墨量与成本 ----
	{
		double dS = params.DropSmallPL(), dM = params.DropMidPL(), dB = params.DropBigPL();
		GetDropPL(dS, dM, dB);
		params.m_dDropSmallPL = dS;
		params.m_dDropMidPL = dM;
		params.m_dDropBigPL = dB;
		params.m_dInkPricePerML = GetInkPriceML();
	}

	// ---- 以下项界面暂未暴露, 使用默认值 ----
	params.m_bEnableScreen = (params.m_nDitherMode != 0);	// 非「纯阈值」时才走有序网屏
	params.m_bRotateScreenY = false;
	params.m_nScaleToLongSide = 0;		// 0 = 按 DPI 自然缩放
	params.m_bDumpMergedCMYK = false;

	// ---- 作业列表(左侧列表): 存下来供下次启动重建 ----
	params.m_vecTaskList.clear();
	if (m_pRIPTaskAdapter)
	{
		for (int i = 0; i < m_pRIPTaskAdapter->GetItemCount(); ++i)
		{
			CLvRIPTaskAdapter::ItemData* pItem = m_pRIPTaskAdapter->GetItem(i);
			if (!pItem) continue;

			const std::wstring w(pItem->m_sstrFilePath.c_str());
			if (w.empty()) continue;

			// 连同第 1 页墨点统计一起存: 下次启动选中该项就能直接算耗墨量与成本,
			// 不必重新 RIP(点数在 RIP 时算过一次就固定了, 只有 pL/墨价是界面输入)
			params.m_vecTaskList.push_back(RIPTaskEntry::Encode(
				pItem->m_bChecked, RIPExt::WStringToUtf8(w),
				pItem->m_nDotDpi, pItem->m_nDotBit, pItem->m_vecDotStats));
		}
	}
}

// ============================================================================
// 菜单: 开始RIP
// ============================================================================
void CMainDlg::OnRipRun()
{
	// 判据一律用线程内核对象, 不看 m_bRipRunning:
	// 线程句柄还在但已经发信号 = 任务其实结束了, 立刻回收后继续启动,
	// 这样即便 WM_RIP_ALL_DONE 因为窗口销毁/消息丢失没送到也不会卡死。
	if (m_hRipThread && ::WaitForSingleObject(m_hRipThread, 0) != WAIT_OBJECT_0)
	{
		if (!m_bRipCancel)
		{
			// 没点过停止, 任务确实在跑
			SMessageBox(m_hWnd, L"RIP 任务正在执行中，请稍候…\n如需中途结束，可使用「文件 → 停止处理」。",
						L"提示", MB_OK | MB_ICONINFORMATION);
			return;
		}

		// 点过「停止处理」: 工作线程正在收尾。取消检查很密集, 正常几十毫秒就退出了,
		// 这里最多等 3 秒; 真等不到就如实告诉用户, 而不是谎报「正在执行中」。
		if (!ReapRipThread(3000))
		{
			SMessageBox(m_hWnd, L"上一个 RIP 任务正在停止中，请稍候片刻再执行。",
						L"提示", MB_OK | MB_ICONINFORMATION);
			return;
		}
	}

	// 收掉已经退出的线程句柄, 顺带把 m_bRipRunning 复位
	ReapRipThread(0);

	if (!m_pRIPTaskAdapter || m_pRIPTaskAdapter->getCount() <= 0)
	{
		SMessageBox(m_hWnd, L"请先通过「文件 → 添加文件」导入 PDF 文件。", L"提示", MB_OK | MB_ICONINFORMATION);
		return;
	}

	// 只处理勾选的文件: 一个都没勾就直说, 别让用户以为程序坏了
	if (m_pRIPTaskAdapter->GetCheckedCount() <= 0)
	{
		SMessageBox(m_hWnd, L"任务列表里没有任何勾选的文件。\n"
					L"请勾选左侧列表中要处理的 PDF（新加入的默认已勾选），\n"
					L"也可以用「文件 → 全选 / 全不选」一键勾上。",
					L"提示", MB_OK | MB_ICONINFORMATION);
		return;
	}

	if (!StartRipQueue())
	{
		SMessageBox(m_hWnd, L"未能启动 RIP 任务，请检查任务列表与参数。", L"错误", MB_OK | MB_ICONERROR);
	}
}

// ============================================================================
// 菜单: 停止处理
// ============================================================================
void CMainDlg::OnStopRip()
{
	// 用线程真相判断: 已经结束(只是结束消息没到)的任务不该再弹「确定要停止吗」
	if (ReapRipThread(0))
	{
		SMessageBox(m_hWnd, L"当前没有正在执行的 RIP 任务。", L"提示", MB_OK | MB_ICONINFORMATION);
		return;
	}

	if (SMessageBox(m_hWnd, L"确定要停止当前的 RIP 处理吗？\n已完成的文件会保留，正在处理的那一项会被丢弃。",
					L"确认", MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;

	// 只置取消标志，不做阻塞等待：
	// 工作线程会在每一页、以及灰度合成/抖动等阶段尽早退出，结束后照常投递
	// WM_RIP_ALL_DONE，由 OnRipAllDone 收尾并把 m_bRipRunning 复位。
	// 这样点「停止处理」时界面不会卡死。
	m_bRipCancel = true;

	// 把尚未结束的项标成「正在停止…」，给用户即时反馈
	if (m_pRIPTaskAdapter)
	{
		const int nTotal = m_pRIPTaskAdapter->getCount();
		for (int i = 0; i < nTotal; ++i)
		{
			CLvRIPTaskAdapter::ItemData* pItemData = m_pRIPTaskAdapter->GetItem(i);
			if (!pItemData) continue;
			if (pItemData->m_nStatus == 0 || pItemData->m_nStatus == 1)
				m_pRIPTaskAdapter->SetItemStatus(i, 1, L"正在停止…");
		}
	}
}

// ============================================================================
// 按钮: 选择 ICC 分色曲线文件
// ============================================================================
void CMainDlg::OnBtnSelectICC()
{
	wchar_t szFile[MAX_PATH] = { 0 };
	OPENFILENAME ofn = { 0 };
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hWnd;
	ofn.lpstrFilter = L"ICC 色彩配置文件 (*.icc;*.icm)\0*.icc;*.icm\0所有文件 (*.*)\0*.*\0";
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

	if (::GetOpenFileName(&ofn))
	{
		SEdit* pEditIcc = FindChildByName2<SEdit>(L"edit_icc_path");
		if (pEditIcc) pEditIcc->SetWindowText(szFile);
	}
}

// ============================================================================
// 按钮: 选择输出根目录(留空 = 跟随 PDF 所在目录)
// ============================================================================
void CMainDlg::OnBtnSelectOutRoot()
{
	// 大作业的产物动辄几十 GB, PDF 放在系统盘时会把 C 盘写满, 所以允许指到数据盘。
	// 用 SHBrowseForFolder 的经典文件夹选择框; BIF_NEWDIALOGSTYLE 需要 COM,
	// 这里防御性地初始化一次(SOUI 一般已经初始化过, 失败也不影响)。
	const HRESULT hrCo = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	wchar_t szDisplay[MAX_PATH] = { 0 };
	BROWSEINFOW bi = { 0 };
	bi.hwndOwner = m_hWnd;
	bi.pszDisplayName = szDisplay;
	bi.lpszTitle = L"选择 RIP 输出的根目录（输出会写到 <该目录>\\RIP_Out\\<PDF文件名>\\）";
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;

	LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
	if (pidl)
	{
		wchar_t szPath[MAX_PATH] = { 0 };
		if (::SHGetPathFromIDListW(pidl, szPath) && szPath[0])
		{
			SEdit* pEdit = FindChildByName2<SEdit>(L"edit_outroot");
			if (pEdit) pEdit->SetWindowText(szPath);
		}
		::CoTaskMemFree(pidl);
	}

	if (SUCCEEDED(hrCo)) ::CoUninitialize();
}

// ============================================================================
// 菜单: 删除左侧列表里选中的 PDF
// ============================================================================
void CMainDlg::OnRemoveSelectedFile()
{
	ReapRipThread(0);					// 结束消息丢失时也能正确判断任务已结束
	if (m_bRipRunning)
	{
		SMessageBox(m_hWnd, L"RIP 任务正在执行中，暂时不能删除列表项。\n请等任务结束后再试。",
					L"提示", MB_OK | MB_ICONINFORMATION);
		return;
	}
	if (!m_pRIPTaskAdapter) return;

	if (m_pRIPTaskAdapter->GetItemCount() <= 0)
	{
		SMessageBox(m_hWnd, L"左侧列表还是空的，请先通过「文件 → 添加文件」导入 PDF。",
					L"提示", MB_OK | MB_ICONINFORMATION);
		return;
	}

	SListView* pLv = FindChildByName2<SListView>(L"lv_rip_task");
	const int nSel = pLv ? pLv->GetSel() : -1;
	if (nSel < 0 || !m_pRIPTaskAdapter->GetItem(nSel))
	{
		SMessageBox(m_hWnd, L"请先在左侧列表中选中要删除的那一项。",
					L"提示", MB_OK | MB_ICONINFORMATION);
		return;
	}

	m_pRIPTaskAdapter->RemoveItem(nSel);
	ScheduleConfigSave();
}

// ============================================================================
// 菜单: 清空左侧列表
// ============================================================================
void CMainDlg::OnClearFileList()
{
	ReapRipThread(0);					// 结束消息丢失时也能正确判断任务已结束
	if (m_bRipRunning)
	{
		SMessageBox(m_hWnd, L"RIP 任务正在执行中，暂时不能清空列表。\n请等任务结束后再试。",
					L"提示", MB_OK | MB_ICONINFORMATION);
		return;
	}
	if (!m_pRIPTaskAdapter || m_pRIPTaskAdapter->GetItemCount() <= 0) return;

	if (SMessageBox(m_hWnd, L"确定要清空左侧的全部 PDF 吗？", L"确认",
					MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;

	m_pRIPTaskAdapter->DeleteAllItem();
	ScheduleConfigSave();
}

// ============================================================================
// 菜单: 全选 / 全不选
//   有任何一项没勾 -> 全部勾上; 已经全勾 -> 全部取消
// ============================================================================
void CMainDlg::OnCheckAllFiles()
{
	if (!m_pRIPTaskAdapter || m_pRIPTaskAdapter->GetItemCount() <= 0) return;

	const bool bAll = (m_pRIPTaskAdapter->GetCheckedCount() == m_pRIPTaskAdapter->GetItemCount());
	m_pRIPTaskAdapter->SetAllChecked(!bAll);
	ScheduleConfigSave();
}

// ============================================================================
// 启动 / 停止 RIP 队列
// ============================================================================
bool CMainDlg::StartRipQueue()
{
	// 绝不允许上一个协调线程还活着时启动新任务:
	// 新任务把 m_bRipCancel 清成 false 会让上一批工作线程「复活」继续跑,
	// 于是两批线程同时写同一批列表项 / 同一批输出文件, 状态彻底错乱。
	if (m_hRipThread && !ReapRipThread(0))
		return false;

	RipJob* pJob = new RipJob;
	pJob->m_pOwner = this;
	pJob->m_hWndNotify = m_hWnd;
	pJob->m_nGen = ++m_nRipGen;			// 本代任务代号, 旧任务的迟到消息会被丢弃

	// 参数快照(在工作线程启动前取, 之后界面改动不影响本次任务)
	CollectRipParams(pJob->m_params);

	// 真正用到的这组参数顺手存进本地配置，退出时再存一次兜底
	RIPConfig::Save(pJob->m_params);

	// 程序目录: 用于定位随程序发布的默认 ICC(工作线程的流水线也要用)
	pJob->m_strAppDir = RIPExt::WStringToUtf8(std::wstring(CGlobalUnits::Instance().m_sstrAppPath.c_str()));
	m_ripPipe.SetAppDir(pJob->m_strAppDir);		// m_ripPipe 只在 UI 线程预扫描页数

	// ---- 收集文件并预扫描页数(UI 线程串行, 很快) ----
	long long lMaxPixels = 0;		// 各文件第 1 页里的最大像素数(估内存用)
	for (int i = 0; i < m_pRIPTaskAdapter->getCount(); ++i)
	{
		CLvRIPTaskAdapter::ItemData* pItemData = m_pRIPTaskAdapter->GetItem(i);
		if (!pItemData || pItemData->m_sstrFilePath.IsEmpty()) continue;
		// 没勾选的文件直接跳过(状态也不动, 保留上一次的结果)
		if (!pItemData->m_bChecked) continue;

		RipFileEntry fe;
		fe.m_strFile = pItemData->m_sstrFilePath;
		fe.m_strPdfUtf8 = RIPExt::WStringToUtf8(std::wstring(fe.m_strFile.c_str()));
		fe.m_nListIndex = i;
		// 输出目录在这里算好并建好: 多线程同时建同一个目录没必要, 还容易出错
		// (指定了输出根目录就写到那里, 否则跟随 PDF 所在目录)
		fe.m_strOutDir = RIPExt::MakeOutputDir(fe.m_strPdfUtf8, pJob->m_params.m_strOutputRoot);

		const int nPages = m_ripPipe.CountPages(fe.m_strPdfUtf8);
		fe.m_nPages = (nPages > 0) ? nPages : 0;
		if (fe.m_nPages == 0)
		{
			const std::wstring werr = RIPExt::Utf8ToWString(m_ripPipe.LastError());
			m_pRIPTaskAdapter->SetItemStatus(i, 3, SStringW().Format(L"失败: %s", werr.c_str()));
		}
		else
		{
			// 量一下页尺寸: 用于按可用内存估算线程数。
			// 600DPI 的 A4 单页处理峰值约 480MB, 10 线程就是 4.8GB,
			// 内存不够时不会报错而是直接崩(Release 下就是崩溃)。
			int nPw = 0, nPh = 0;
			double fPw = 0, fPh = 0;
			if (m_ripPipe.QueryPageSize(fe.m_strPdfUtf8, 0, nPw, nPh, fPw, fPh))
			{
				double fPx = fPw * pJob->m_params.XDpi() / 72.0;
				double fPy = fPh * pJob->m_params.YDpi() / 72.0;
				if (pJob->m_params.m_nScaleToLongSide > 0)
				{
					const double fLong = (fPx > fPy) ? fPx : fPy;
					if (fLong > 0.0)
					{
						const double fK = (double)pJob->m_params.m_nScaleToLongSide / fLong;
						fPx *= fK;
						fPy *= fK;
					}
				}
				const long long nPix = (long long)(fPx * fPy);
				if (nPix > lMaxPixels) lMaxPixels = nPix;
			}
		}
		pJob->m_vecFiles.push_back(fe);
	}

	// ---- 拆成「文件 x 页」任务: 单文件多页也能并行 ----
	for (size_t f = 0; f < pJob->m_vecFiles.size(); ++f)
	{
		const RipFileEntry& fe = pJob->m_vecFiles[f];
		for (int pg = 0; pg < fe.m_nPages; ++pg)
			pJob->m_vecTasks.push_back(RIPPool::PageTask((int)f, pg, fe.m_strPdfUtf8, fe.m_strOutDir));
	}
	pJob->m_nTotalTasks = (int)pJob->m_vecTasks.size();
	pJob->m_dwStart = ::GetTickCount();

	// ---- 按可用内存钳线程数 ----
	// 每页的处理峰值与页面像素数成正比(渲染 pixmap + 灰度平面 + 预览),
	// 线程数直接乘上去; 内存不足时 new/malloc 会失败并抛异常 -> 直接崩。
	// 这里按当前可用内存(物理与提交取小)算一个安全上限, 宁可少起线程也不崩。
	const size_t nBytesPerThread = RIPPool::EstimatePageBytes(lMaxPixels);
	int nAvailMB = 0, nMaxByMem = 0;
	pJob->m_nBytesPerThread = nBytesPerThread;
	pJob->m_nThreads = RIPPool::ClampThreadsByMemory(pJob->m_params.ThreadCount(),
													 nBytesPerThread, &nAvailMB, &nMaxByMem);

	if (pJob->m_nTotalTasks == 0)
	{
		delete pJob;
		return false;
	}

	// 先把要跑的文件整体置为「等待」(打不开的项已经是失败状态, 不要覆盖)
	for (size_t f = 0; f < pJob->m_vecFiles.size(); ++f)
	{
		if (pJob->m_vecFiles[f].m_nPages > 0)
			m_pRIPTaskAdapter->SetItemStatus(pJob->m_vecFiles[f].m_nListIndex, 0, L"等待");
	}

	m_bRipCancel = false;
	m_bRipRunning = true;

	m_nRipTotalTasks = pJob->m_nTotalTasks;
	m_nRipDoneTasks = 0;
	// 线程数被内存限制调过时明说原因, 否则用户会以为「我设的 10 线程没生效」
	const int nReqWant = (pJob->m_params.ThreadCount() > 0)
		? pJob->m_params.ThreadCount() : RIPPool::AutoThreadCount();
	if (pJob->m_nThreads != nReqWant)
		UpdateRipProgress(0, m_nRipTotalTasks,
			SStringW().Format(L"RIP 进行中… 0/%d 页（线程 %d→%d，可用内存 %d MB）",
							  m_nRipTotalTasks, nReqWant, pJob->m_nThreads, nAvailMB));
	else
		UpdateRipProgress(0, m_nRipTotalTasks,
			SStringW().Format(L"RIP 进行中… 0/%d 页", m_nRipTotalTasks));

	unsigned int nTid = 0;
	m_hRipThread = (HANDLE)_beginthreadex(NULL, 0, &CMainDlg::RipThreadProc, pJob, 0, &nTid);
	if (!m_hRipThread)
	{
		delete pJob;
		m_bRipRunning = false;
		return false;
	}
	return true;
}

void CMainDlg::StopRipQueue()
{
	m_bRipCancel = true;
	// 最多等 60 秒让协调线程收尾(取消检查密度很高, 正常几十到几百毫秒就退出了)
	ReapRipThread(60000);
	m_bRipRunning = false;
}

// ----------------------------------------------------------------------------
// 回收已结束的工作线程句柄。
// 线程句柄同时也是「还有没有任务在跑」的唯一可信判据, 所以必须先
// WaitForSingleObject 确认内核对象已发信号, 再 CloseHandle, 绝不能把
// 仍在运行的线程句柄关掉(那样 m_hRipThread 会被 _beginthreadex 覆盖, 旧句柄泄漏,
// 而且旧线程结束时投递的 WM_RIP_ALL_DONE 会把新任务的状态误清)。
// ----------------------------------------------------------------------------
bool CMainDlg::ReapRipThread(DWORD dwWaitMs)
{
	if (!m_hRipThread)
		return true;								// 本来就没有任务

	if (::WaitForSingleObject(m_hRipThread, dwWaitMs) != WAIT_OBJECT_0)
		return false;								// 线程仍在运行

	::CloseHandle(m_hRipThread);
	m_hRipThread = NULL;
	m_bRipRunning = false;
	return true;
}

// ============================================================================
// 协调线程: 创建线程池跑完全部任务, 再回收任务包并通知界面收工
// ============================================================================
unsigned int __stdcall CMainDlg::RipThreadProc(void* pArg)
{
	RipJob* pJob = static_cast<RipJob*>(pArg);
	if (pJob)
	{
		CMainDlg* pThis = static_cast<CMainDlg*>(pJob->m_pOwner);

		// 必须先把要用的值取出来: RunRipQueue 内部会 delete pJob,
		// 之后再访问 pJob->m_hWndNotify 就是读已释放内存(句柄会变成垃圾值),
		// 导致 WM_RIP_ALL_DONE 投递失败, 界面永远停在「任务正在执行中」。
		const HWND  hNotify = pJob->m_hWndNotify;
		const int   nGen = pJob->m_nGen;
		const DWORD dwStart = pJob->m_dwStart;

		if (pThis)
			pThis->RunRipQueue(pJob);

		if (::IsWindow(hNotify))
			::PostMessage(hNotify, WM_RIP_ALL_DONE, (WPARAM)nGen,
						  (LPARAM)(DWORD)(::GetTickCount() - dwStart));
	}
	return 0;
}

// ============================================================================
// 线程体(协调线程): 起 N 个工作线程并行跑「文件 x 页」任务
// ============================================================================
void CMainDlg::RunRipQueue(RipJob* pJob)
{
	if (!pJob) return;

	// 页之间互不依赖, 全部丢给线程池; 每个工作线程自带一份 RIPipeline
	// 线程数已在 StartRipQueue 里按可用内存钳过(m_nThreads), 这里不再收敛
	const int nUsed = RIPPool::Run(pJob->m_strAppDir, pJob->m_nThreads,
								   pJob->m_vecTasks, &CMainDlg::RipTaskProc,
								   pJob, &m_bRipCancel, pJob->m_nBytesPerThread);

	// 一个线程都没起来(极端情况): 如实报错, 不能假装跑过了
	if (nUsed == 0)
	{
		for (size_t f = 0; f < pJob->m_vecFiles.size(); ++f)
		{
			if (pJob->m_vecFiles[f].m_nPages > 0)
				PostRipStatus(pJob, pJob->m_vecFiles[f].m_nListIndex, 3, L"失败: 无法创建工作线程");
		}
	}

	delete pJob;   // 参数包用完即删(调用方已提前取出需要的字段)
}

// ============================================================================
// 工作线程回调: 静态入口 -> 转成成员调用
// ============================================================================
void CMainDlg::RipTaskProc(void* pCtx, RIPipeline& pipe, int nTaskIndex, int nThreadIdx)
{
	(void)nThreadIdx;
	RipJob* pJob = static_cast<RipJob*>(pCtx);
	if (!pJob) return;
	CMainDlg* pThis = static_cast<CMainDlg*>(pJob->m_pOwner);
	if (pThis)
		pThis->RunRipTask(pipe, pJob, nTaskIndex);
}

// ============================================================================
// 工作线程: 处理一页(渲染 -> 分色 -> 抖动 -> 落盘), 并更新进度与文件聚合状态
// ============================================================================
void CMainDlg::RunRipTask(RIPipeline& pipe, RipJob* pJob, int nTaskIndex)
{
	const RIPPool::PageTask& task = pJob->m_vecTasks[nTaskIndex];
	RipFileEntry& fe = pJob->m_vecFiles[task.m_nGroup];
	const RIPParams& params = pJob->m_params;

	// 该文件的第一页开始时, 把列表项标成「处理中」(只投递一次)
	if (::InterlockedCompareExchange(&fe.m_lStarted, 1, 0) == 0)
	{
		fe.m_dwStart = ::GetTickCount();
		PostRipStatus(pJob, fe.m_nListIndex, 1, L"处理中");
	}

	bool bOk = false;
	int  nSaved = 0;
	std::wstring wErr;

	if (m_bRipCancel)
	{
		// 已取消: 取到号的这一页也不再开新活
	}
	else
	{
		RIPPageResult result;
		if (pipe.RunPage(task.m_strPdf, task.m_nPage, params, result, &m_bRipCancel))
		{
			nSaved = RIPipeline::SavePagePlanes(task.m_strPdf, task.m_nPage, task.m_strOutDir,
												params.YDpi(), result, true, params.m_nPrnVersion);
			bOk = true;

			// 墨点统计只做第 1 页(多页 PDF 只看首页): 结果回传 UI 线程,
			// 之后在左侧列表里选中这个 PDF 就能在右侧看到。
			if (task.m_nPage == 0)
				PostDotStat(pJob, fe.m_nListIndex, result, params.YDpi(), params.BitDepth());
		}
		else
		{
			wErr = RIPExt::Utf8ToWString(pipe.LastError());
		}
	}

	if (nSaved > 0)
	{
		::InterlockedExchangeAdd(&fe.m_lSaved, (LONG)nSaved);
		::InterlockedExchangeAdd(&pJob->m_lSaved, (LONG)nSaved);
	}

	// 真失败(不是取消)才计失败, 并只记第一条原因
	if (!bOk && !m_bRipCancel)
	{
		::InterlockedIncrement(&fe.m_lPagesFailed);
		if (::InterlockedCompareExchange(&fe.m_lErrClaimed, 1, 0) == 0)
			fe.m_strErr = SStringW().Format(L"第%d页 %s", task.m_nPage + 1, wErr.c_str()).c_str();
	}

	// 该文件的所有页都结束了, 才投递列表项最终状态
	const LONG nDone = ::InterlockedIncrement(&fe.m_lPagesDone);
	if (nDone >= fe.m_nPages)
	{
		const double fSec = (::GetTickCount() - fe.m_dwStart) / 1000.0;
		if (m_bRipCancel)
			PostRipStatus(pJob, fe.m_nListIndex, 3, L"已取消");
		else if (fe.m_lPagesFailed > 0)
			PostRipStatus(pJob, fe.m_nListIndex, 3, SStringW().Format(L"失败: %s", fe.m_strErr.c_str()));
		else
			PostRipStatus(pJob, fe.m_nListIndex, 2,
				SStringW().Format(L"完成 %d页/%d文件 %.1fs", fe.m_nPages, (int)fe.m_lSaved, fSec));
	}

	// 进度: 整个队列又完成了一页
	const LONG nAllDone = ::InterlockedIncrement(&pJob->m_lDone);
	if (::IsWindow(pJob->m_hWndNotify))
		::PostMessage(pJob->m_hWndNotify, WM_RIP_PROGRESS, (WPARAM)nAllDone, (LPARAM)pJob->m_nTotalTasks);
}

// ============================================================================
// 进度条
// ============================================================================
void CMainDlg::UpdateRipProgress(int nDone, int nTotal, const SStringW& sstrText)
{
	SProgress* pProg = FindChildByName2<SProgress>(L"prog_rip");
	if (pProg)
	{
		if (nTotal < 1) nTotal = 1;
		if (nDone < 0) nDone = 0;
		if (nDone > nTotal) nDone = nTotal;
		pProg->SetRange(0, nTotal);
		pProg->SetValue(nDone);
	}

	SStatic* pText = FindChildByName2<SStatic>(L"prog_text");
	if (pText)
		pText->SetWindowText(sstrText);
}

// ============================================================================
// UI 线程: 进度回写(每个工作线程每跑完一页投递一次)
// ============================================================================
BOOL CMainDlg::OnRipProgress(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	bHandled = TRUE;

	const int nTotal = (int)lParam;
	if (nTotal <= 0) return TRUE;

	int nDone = (int)wParam;
	if (nDone < 0) nDone = 0;
	if (nDone > nTotal) nDone = nTotal;
	m_nRipDoneTasks = nDone;

	UpdateRipProgress(nDone, nTotal,
		SStringW().Format(L"RIP 进行中… %d/%d 页", nDone, nTotal));
	return TRUE;
}

// ============================================================================
// UI 线程: 第 1 页墨点统计回写(工作线程算好后投递过来)
// ============================================================================
BOOL CMainDlg::OnRipDotStat(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	bHandled = TRUE;
	RipDotStatNotify* pN = reinterpret_cast<RipDotStatNotify*>(lParam);
	if (!pN) return TRUE;

	// 上一代任务迟到的统计直接丢弃
	if (pN->m_nGen != m_nRipGen || !m_pRIPTaskAdapter)
	{
		delete pN;
		return TRUE;
	}

	CLvRIPTaskAdapter::ItemData* pItem = m_pRIPTaskAdapter->GetItem(pN->m_nIndex);
	if (pItem)
	{
		pItem->m_vecDotStats = pN->m_vecStats;
		pItem->m_nDotDpi = pN->m_nDpi;
		pItem->m_nDotBit = pN->m_nBit;
	}

	// 正好选中这个文件: 立刻把统计刷到右侧面板
	if (m_nLvSelIndex == pN->m_nIndex)
		ShowDotStatPanel(pN->m_nIndex);

	// 点数有更新 -> 稍后连同作业列表一起落盘(重启后不用重跑 RIP 也能算耗墨量)
	ScheduleConfigSave();

	delete pN;
	return TRUE;
}

// ============================================================================
// 左侧列表选中项变化 -> 右侧展示该 PDF 的墨点统计
// ============================================================================
void CMainDlg::OnListItemSelected(int nIndex)
{
	m_nLvSelIndex = nIndex;
	ShowDotStatPanel(nIndex);
}

// ============================================================================
// 勾选状态变化 -> 作业列表落盘(延迟防抖)
// ============================================================================
void CMainDlg::OnListCheckChanged(int nIndex, bool bChecked)
{
	ScheduleConfigSave();
}

// ============================================================================
// 把某项的墨点统计刷到右侧「墨点统计」面板
//   面板固定 5 行: C / M / Y / K / 合计; 没有统计就显示提示。
// ============================================================================
void CMainDlg::ShowDotStatPanel(int nIndex)
{
	static const wchar_t* kLineNames[] = {
		L"dot_line_c", L"dot_line_m", L"dot_line_y", L"dot_line_k", L"dot_line_sum"
	};
	const int kLineCount = 5;

	for (int i = 0; i < kLineCount; ++i)
	{
		SStatic* p = FindChildByName2<SStatic>(kLineNames[i]);
		if (p) p->SetWindowTextW(L"");
	}

	SStatic* pTip   = FindChildByName2<SStatic>(L"dot_tip");
	SStatic* pPrice = FindChildByName2<SStatic>(L"dot_price");
	if (pPrice) pPrice->SetWindowTextW(L"");

	CLvRIPTaskAdapter::ItemData* pItem =
		m_pRIPTaskAdapter ? m_pRIPTaskAdapter->GetItem(nIndex) : NULL;

	if (!pItem)
	{
		if (pTip) pTip->SetWindowTextW(L"选中左侧文件后显示该文件第 1 页的墨点统计");
		return;
	}

	if (pItem->m_vecDotStats.empty())
	{
		if (pTip) pTip->SetWindowTextW(L"该文件尚未 RIP，暂无统计（只统计第 1 页）");
		return;
	}

	// 1bit 是二值位图: 只有「打点 / 不打点」两种, 没有大中小点之分。
	//   RIP 侧已把每个出墨像素记成大点(打点数=出墨像素), 这里再把小/中归零,
	//   于是「打点数 x 大点 pL」自动就是总耗墨量。
	const bool bBin = (pItem->m_nDotBit == 1);

	long long nDots = 0, nS = 0, nM = 0, nB = 0, nPix = 0;
	for (size_t k = 0; k < pItem->m_vecDotStats.size(); ++k)
	{
		const RIPDotStat& d = pItem->m_vecDotStats[k];
		nDots += d.m_nDots; nS += d.m_nDotsSmall;
		nM += d.m_nDotsMid; nB += d.m_nDotsBig; nPix += d.m_nInkPixels;
	}
	if (bBin) { nB = nDots; nS = 0; nM = 0; }

	// 界面当前的墨滴体积(皮升) + 墨水单价(元/mL) -- 耗墨量与成本都按它们现算,
	// 所以改体积/改单价立刻重算, 不需要重新 RIP(点数不变, 纯乘法)。
	double dSmallPL = 2.0, dMidPL = 6.0, dBigPL = 12.0;
	GetDropPL(dSmallPL, dMidPL, dBigPL);
	const double dPriceML  = GetInkPriceML();
	const double dTotalPL  = (double)nS * dSmallPL + (double)nM * dMidPL + (double)nB * dBigPL;
	const double dTotalML  = RIPPLToML(dTotalPL);	// 1 pL = 1e-9 mL

	for (int i = 0; i < 4; ++i)
	{
		SStatic* p = FindChildByName2<SStatic>(kLineNames[i]);
		if (!p) continue;
		if (i >= (int)pItem->m_vecDotStats.size())
		{
			p->SetWindowTextW(L"—");
			continue;
		}
		const RIPDotStat& d = pItem->m_vecDotStats[i];
		// 内部路径/名字统一是 UTF-8, 显示前转宽字符
		const std::wstring wInk = RIPExt::Utf8ToWString(d.m_strInk);
		// 耗墨量按「界面当前的墨滴体积」现算, 改体积不用重新 RIP
		const double dPL = bBin ? (double)d.m_nDots * dBigPL
		                        : RIPDotsToPL(d, dSmallPL, dMidPL, dBigPL);
		if (bBin)
			p->SetWindowTextW(SStringW().Format(
				L"%-2s  打点 %-10I64d 墨 %.1f pL", wInk.c_str(), d.m_nDots, dPL));
		else
			p->SetWindowTextW(SStringW().Format(
				L"%-2s  点 %-9I64d 小 %-8I64d 中 %-7I64d 大 %-7I64d  墨 %.1f pL",
				wInk.c_str(), d.m_nDots, d.m_nDotsSmall, d.m_nDotsMid, d.m_nDotsBig, dPL));
	}

	// 合计行(含专色): 总墨量按毫升(mL)给
	{
		SStatic* p = FindChildByName2<SStatic>(kLineNames[4]);
		if (p)
		{
			if (bBin)
				p->SetWindowTextW(SStringW().Format(
					L"合计 打点 %-10I64d 总墨量 %.6f mL", nDots, dTotalML));
			else
				p->SetWindowTextW(SStringW().Format(
					L"合计 点 %-9I64d 小 %-8I64d 中 %-7I64d 大 %-7I64d  总墨量 %.6f mL",
					nDots, nS, nM, nB, dTotalML));
		}
	}

	// 价格行: 紧跟在总墨量后面(总墨量 x 每毫升单价)
	if (pPrice)
	{
		if (dPriceML > 0.0)
			pPrice->SetWindowTextW(SStringW().Format(
				L"墨水成本  ¥%.6f   (¥%.6g /mL × %.6f mL)",
				dTotalML * dPriceML, dPriceML, dTotalML));
		else
			pPrice->SetWindowTextW(L"墨水成本  —  填上「墨价¥/mL」后自动算 (总墨量 × 单价)");
	}

	if (pTip)
	{
		if (bBin)
		{
			pTip->SetWindowTextW(SStringW().Format(
				L"第 1 页 %dDPI 1bit(打点数=出墨像素, 全按大点 pL) ｜ 出墨像素 %I64d ｜ 打点 %I64d ｜ 总耗墨量 %.6f mL (%.0f pL)",
				pItem->m_nDotDpi, nPix, nDots, dTotalML, dTotalPL));
		}
		else
		{
			// 2bit 按像素级别统计: 1=小点 2=中点 3=大点(实地色块不再合成一个点)
			pTip->SetWindowTextW(SStringW().Format(
				L"第 1 页 %dDPI %dbit 按像素级别(1=小点 2=中点 3=大点) ｜ 出墨像素 %I64d ｜ 总耗墨量 %.6f mL (%.0f pL)",
				pItem->m_nDotDpi, pItem->m_nDotBit, nPix, dTotalML, dTotalPL));
		}
	}
}

// ============================================================================
// 读界面上的墨滴体积(皮升)
//   控件不存在时保持传入值不变, 这样调用方可以先放默认值
// ============================================================================
void CMainDlg::GetDropPL(double& dSmall, double& dMid, double& dBig)
{
	LPCWSTR kNames[] = { L"edit_drop_small_pl", L"edit_drop_mid_pl", L"edit_drop_big_pl" };
	double* pOut[3] = { &dSmall, &dMid, &dBig };

	for (int i = 0; i < 3; ++i)
	{
		SEdit* pEdit = FindChildByName2<SEdit>(kNames[i]);
		if (!pEdit) continue;

		const double v = _wtof(pEdit->GetWindowText().c_str());
		*pOut[i] = (v < 0.0) ? 0.0 : v;
	}
}

// ============================================================================
// 读界面上的墨水单价(元/毫升): 控件不存在时按 0 算(界面显示「未填写」)
// ============================================================================
double CMainDlg::GetInkPriceML()
{
	double d = 0.0;
	SEdit* pEdit = FindChildByName2<SEdit>(L"edit_ink_price_ml");
	if (pEdit)
	{
		const double v = _wtof(pEdit->GetWindowText().c_str());
		d = (v < 0.0) ? 0.0 : v;
	}
	return d;
}

// ============================================================================
// 墨滴体积(pL)/墨价编辑框内容一变就重算(不需要等失焦, 也不需要回车)
//   SEdit 实为 TCtrlProxy<IEdit, SRichEdit>, 内容变化走 SRichEdit::OnTxNotify,
//   会 FireEvent(EventRENotify) -- 订阅它即可, 覆盖键盘输入 / 粘贴 / 输入法上屏。
// ============================================================================
BOOL CMainDlg::OnInkEditChanged(EventArgs* e)
{
	ShowDotStatPanel(m_nLvSelIndex);
	// 墨滴体积/墨价也要进配置文件: 停止输入 1.5s 后落盘
	ScheduleConfigSave();
	return TRUE;
}

// ============================================================================
// 取消收尾: 没跑到 / 正在跑的列表项由工作线程放弃, 在这里统一标成「已取消」
// ============================================================================
void CMainDlg::MarkPendingCancelled()
{
	if (!m_pRIPTaskAdapter) return;

	for (int i = 0; i < m_pRIPTaskAdapter->getCount(); ++i)
	{
		CLvRIPTaskAdapter::ItemData* pItem = m_pRIPTaskAdapter->GetItem(i);
		if (!pItem) continue;
		// 0=等待 1=处理中: 都还没收到最终状态
		if (pItem->m_nStatus == 0 || pItem->m_nStatus == 1)
			m_pRIPTaskAdapter->SetItemStatus(i, 3, L"已取消");
	}
}

// ============================================================================
// UI 线程: 单条任务状态回写
// ============================================================================
BOOL CMainDlg::OnRipItemStatus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	bHandled = TRUE;
	RipNotify* pN = reinterpret_cast<RipNotify*>(lParam);
	if (!pN) return TRUE;

	// 上一代任务(已停止/已结束)迟到的状态消息直接丢弃,
	// 否则会把新任务刚置好的「等待」冲掉。
	if (pN->m_nGen != m_nRipGen)
	{
		delete pN;
		return TRUE;
	}

	if (m_pRIPTaskAdapter)
		m_pRIPTaskAdapter->SetItemStatus(pN->m_nIndex, pN->m_nStatus, pN->m_sstrText);

	delete pN;
	return TRUE;
}

// ============================================================================
// UI 线程: 整个队列处理结束
// ============================================================================
BOOL CMainDlg::OnRipAllDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	bHandled = TRUE;

	// 只认当前这一代任务的结束通知: 上一代任务迟到的消息不能把新任务的状态清掉
	if ((int)wParam != m_nRipGen)
		return TRUE;

	if (m_hRipThread)
	{
		::WaitForSingleObject(m_hRipThread, 5000);
		::CloseHandle(m_hRipThread);
		m_hRipThread = NULL;
	}
	m_bRipRunning = false;

	// 取消时未跑完的项不会被工作线程收尾(它们已经退出了), 在这里补状态
	if (m_bRipCancel)
		MarkPendingCancelled();

	const double fSec = (DWORD)lParam / 1000.0;
	if (m_bRipCancel)
	{
		// 注意: 已取消的页也会走完这里的计数, 所以「已完成」不代表跑完,
		// 只代表这些页的任务已经结束(跑完的或被放弃的)
		UpdateRipProgress(m_nRipDoneTasks, m_nRipTotalTasks,
			SStringW().Format(L"已取消（用时 %.1fs，已结束 %d/%d 页）", fSec, m_nRipDoneTasks, m_nRipTotalTasks));
	}
	else
	{
		UpdateRipProgress(m_nRipTotalTasks, m_nRipTotalTasks,
			SStringW().Format(L"全部完成: %d 页，用时 %.1fs", m_nRipTotalTasks, fSec));
	}
	return TRUE;
}

void CMainDlg::OnLanguageBtnCN()
{
	OnLanguage(1);
}
void CMainDlg::OnLanguageBtnJP()
{
	OnLanguage(0);
}
void CMainDlg::OnLanguage(int nID)
{
	ITranslatorMgr *pTransMgr = SApplication::getSingletonPtr()->GetTranslator();
	SASSERT(pTransMgr);
	bool bCnLang = nID == 1;

	SXmlDoc xmlLang;
	if (SApplication::getSingletonPtr()->LoadXmlDocment(xmlLang, bCnLang ? _T("translator:lang_cn") : _T("translator:lang_jp")))
	{
		CAutoRefPtr<ITranslator> lang;
		pTransMgr->CreateTranslator(&lang);
		lang->Load(&xmlLang.root().child(L"language"), 1);//1=LD_XML
		TCHAR lngName[TR_MAX_NAME_LEN] = {0};
		lang->GetName(lngName);
        pTransMgr->SetLanguage(lngName);
		pTransMgr->InstallTranslator(lang);
        GetRoot()->SDispatchMessage(UM_SETLANGUAGE, 0, 0);
	}
}

void CMainDlg::OnMenuFileCommand(IEvtArgs* e) {
	EventSelectMenu* e2 = sobj_cast<EventSelectMenu>(e);
	OnCommand(0, e2->nMenuId, NULL);
}

void CMainDlg::OnCommand(UINT uNotifyCode, int nID, HWND wndCtl)
{
	if (uNotifyCode == 0)
	{
		if (nID == R.id.menu_file_addfile) {
			wchar_t szFile[MAX_PATH] = { 0 };
			OPENFILENAME ofn = { 0 };
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = m_hWnd; // SOUI窗口句柄
			ofn.lpstrFilter = L"PDF文件 (*.pdf)\0*.pdf\0所有文件 (*.*)\0*.*\0";
			ofn.lpstrFile = szFile;
			ofn.nMaxFile = MAX_PATH;
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

			if (GetOpenFileName(&ofn))
			{
				// 添加到 RIP 任务列表(带路径, 供「开始RIP」使用)
				m_pRIPTaskAdapter->AddRipTask(szFile);
				ScheduleConfigSave();
			}
		}
		else if (nID == R.id.menu_file_riprun) {
			OnRipRun();
		}
		else if (nID == R.id.menu_file_stoprip) {
			OnStopRip();
		}
		else if (nID == R.id.menu_file_removefile) {
			OnRemoveSelectedFile();
		}
		else if (nID == R.id.menu_file_clearfile) {
			OnClearFileList();
		}
		else if (nID == R.id.menu_file_checkall) {
			OnCheckAllFiles();
		}
	}
}

//TODO:消息映射
void CMainDlg::OnClose()
{
	// 退出前把右侧界面参数落到本地配置文件，下次启动自动回填
	{
		RIPParams params;
		CollectRipParams(params);
		RIPConfig::Save(params);
	}

	// 先停掉工作线程, 避免窗口销毁后仍有状态消息投递
	StopRipQueue();

	// 防抖落盘定时器(退出前已手动保存过, 这里只是别让它在销毁后再触发)
	KillTimer(kAutoSaveTimerId);

	SNativeWnd::DestroyWindow();
}

void CMainDlg::OnMaximize()
{
	SendMessage(WM_SYSCOMMAND, SC_MAXIMIZE);
}
void CMainDlg::OnRestore()
{
	SendMessage(WM_SYSCOMMAND, SC_RESTORE);
}
void CMainDlg::OnMinimize()
{
	SendMessage(WM_SYSCOMMAND, SC_MINIMIZE);
}

void CMainDlg::OnSize(UINT nType, CSize size)
{
	SetMsgHandled(FALSE);	
	SWindow *pBtnMax = FindChildByName(L"btn_max");
	SWindow *pBtnRestore = FindChildByName(L"btn_restore");
	if(!pBtnMax || !pBtnRestore) return;
	SWindow* pWndMain = FindChildByName(L"cap_main");

	if (nType == SIZE_MAXIMIZED)
	{
		pBtnRestore->SetVisible(TRUE);
		pBtnMax->SetVisible(FALSE);

		pWndMain->SetAttribute(L"pos", L"0,0,-0,-0");
	}
	else if (nType == SIZE_RESTORED)
	{
		pBtnRestore->SetVisible(FALSE);
		pBtnMax->SetVisible(TRUE);
	
		pWndMain->SetAttribute(L"pos", L"15,15,-15,-15");
	}
}

