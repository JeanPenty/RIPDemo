// MainDlg.cpp : implementation of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "MainDlg.h"	
#include <helper/SMenuEx.h>

CMainDlg::CMainDlg() : SHostWnd(_T("LAYOUT:XML_MAINWND"))
{
}

CMainDlg::~CMainDlg()
{
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

// 	for (int i = 0; i < 10; i++)
// 	{
// 		SStringW sstrTaskKey = SStringW().Format(L"test_%d", i);
// 		m_pRIPTaskAdapter->AddItem(sstrTaskKey);
// 	}

	return 0;
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

			std::wstring wstrFile = L"";
			if (GetOpenFileName(&ofn))
				wstrFile = std::wstring(szFile);

			int kkk = 0;
			//将这条数据添加到list中去
			//TODO:
			m_pRIPTaskAdapter->AddItem(wstrFile.c_str());
		}
	}
}

//TODO:消息映射
void CMainDlg::OnClose()
{
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

