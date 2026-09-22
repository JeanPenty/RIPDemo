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
	TIFF* tif = TIFFOpen(filename, "w");
	if (!tif) return;

	// 设置基础信息
	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, width);
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, height);
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 4);      // CMYK = 4 个通道
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_SEPARATED);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG); // 像素连续存储 (C M Y K C M Y K)

	// 设置压缩方式 (可选，建议用 LZW 或 Deflate 压缩以减小文件体积)
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);

	// 写入数据
	// libtiff 建议按行写入，防止大内存一次性拷贝
	tsize_t scanlineSize = TIFFScanlineSize(tif);
	for (int row = 0; row < height; row++)
	{
		unsigned char* pRow = pData + (row * width * 4);
		if (TIFFWriteScanline(tif, pRow, row) < 0)
		{
			break;
		}
	}

	TIFFClose(tif);
}

BOOL CMainDlg::OnInitDialog(HWND hWnd, LPARAM lParam)
{
	auto& pGu = CGlobalUnits::Instance();
	cmsHTRANSFORM hTransform = CGlobalUnits::Instance().m_hTransformBGRA2CMYK;

	SStringW sstrPDFPath = SStringW().Format(L"%stest.pdf", CGlobalUnits::Instance().m_sstrAppPath.c_str());
	std::string strPDFPath = S_CW2A(sstrPDFPath, CP_UTF8);

	fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) return 0;
	fz_document* doc = nullptr;
	fz_register_document_handlers(ctx);

	fz_try(ctx)
	{
		doc = fz_open_document(ctx, strPDFPath.c_str());
		int nCount = fz_count_pages(ctx, doc);

		if (nCount > 0)
		{
			fz_page* page = fz_load_page(ctx, doc, 0); // 注意这里使用 nPage 而不是固定的 0
			float zoom = 600 / 72.0f;		//这里DPI是600
			fz_matrix ctm = fz_scale(zoom, zoom);
			fz_pixmap* pix = fz_new_pixmap_from_page(ctx, page, ctm, fz_device_bgr(ctx), 1);

			int nWidth = fz_pixmap_width(ctx, pix);
			int nHeight = fz_pixmap_height(ctx, pix);
			unsigned char* pSamples = fz_pixmap_samples(ctx, pix);

			SAutoRefPtr<IBitmapS> pBitmap = NULL;
			GETRENDERFACTORY->CreateBitmap(&pBitmap);
			if (pBitmap)
			{
				pBitmap->Init(nWidth, nHeight, (const LPVOID)pSamples);

				SStringW sstrName = SStringW().Format(L"%sExport_page_0.png", pGu.m_sstrAppPath.c_str());
				pBitmap->Save(sstrName, (LPVOID)&ImageFormatPNG);

				size_t width = pBitmap->Width();
				size_t height = pBitmap->Height();
				unsigned __int64 totalSize = width * height * 4;
				pBitmap->LockPixelBits();
				unsigned char* lpBmp = (unsigned char*)pBitmap->GetPixelBits();
				auto pData = std::shared_ptr<unsigned char>(new unsigned char[totalSize], std::default_delete<unsigned char[]>());
				ZeroMemory(pData.get(), width * height * 4);
				memcpy(pData.get(), lpBmp, width * height * 4);
				pBitmap->UnlockPixelBits(lpBmp);

				std::vector<unsigned char> cmykBuffer(totalSize * 4);
				cmsDoTransform(hTransform, lpBmp, cmykBuffer.data(), nWidth);

				int kkk = 0;
			}

			DWORD dwError = GetLastError();

			fz_drop_pixmap(ctx, pix);
			fz_drop_page(ctx, page);
		}
	}
	fz_catch(ctx)
	{
		// 可以在此处添加错误提示，防止由于 PDF 加密或损坏导致的崩溃
	}

	if (doc) fz_drop_document(ctx, doc);
	fz_drop_context(ctx);

	return 0;
}

// BOOL CMainDlg::OnInitDialog(HWND hWnd, LPARAM lParam)
// {
// 	cmsHTRANSFORM hTransform = CGlobalUnits::Instance().m_hTransformBGRA2CMYK;
// 
// 	fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
// 	if (!ctx) return 0;
// 	fz_register_document_handlers(ctx);
// 
// 	fz_document* doc = nullptr;
// 	fz_page* page = nullptr;
// 
// 	SStringW sstrPDFPath = SStringW().Format(L"%stest.pdf", CGlobalUnits::Instance().m_sstrAppPath.c_str());
// 	std::string strPDFPath = S_CW2A(sstrPDFPath, CP_UTF8);
// 	fz_try(ctx)
// 	{
// 		doc = fz_open_document(ctx, strPDFPath.c_str());
// 		int nCount = fz_count_pages(ctx, doc);
// 
// 		//将每一页的数据转为图片，然后再去做分色处理
// 		for (int i = 0; i < nCount; i++)
// 		{
// 			fz_pixmap* pix = nullptr;
// 			auto startTime = std::chrono::high_resolution_clock::now();
// 
// 			unsigned char* pSamples = nullptr;
// 			int nWidth = 0, nHeight = 0;
// 			{
// 				char bufPage[128];
// 				sprintf(bufPage, "Page_Load: %d", i); // 使用 %lld 打印整数
// 
// 				PERF_SCOPE(bufPage, true);
// 
// 				page = fz_load_page(ctx, doc, i);
// 				float zoomX = 400 / 72.0f;
// 				float zoomY = 600 / 72.0f;
// 				fz_matrix ctm = fz_scale(zoomX, zoomY);
// 				pix = fz_new_pixmap_from_page(ctx, page, ctm, fz_device_rgb(ctx), 0);
// 				nWidth = fz_pixmap_width(ctx, pix);
// 				nHeight = fz_pixmap_height(ctx, pix);
// 				pSamples = fz_pixmap_samples(ctx, pix);
// 			}
// 
// 			unsigned __int64 totalSize = nWidth * nHeight * 3;
// 			static std::vector<unsigned char> cmykBuffer(totalSize * 3);
// 			if (cmykBuffer.size() < totalSize * 3) {
// 				cmykBuffer.resize(totalSize * 3); // 仅在页面变大时才重新分配
// 			}
// 
// 			{
// 				PERF_SCOPE("cmsDoTransform", false);
// 				cmsDoTransform(hTransform, pSamples, cmykBuffer.data(), nWidth);
// 			}
// 
// 			fz_drop_pixmap(ctx, pix);
// 			fz_empty_store(ctx);
// 
// 			char bufName[256];
// 			sprintf(bufName, "tiff_%d.tiff", i); // 使用 %lld 打印整数
// 			SaveCMYKToTIFF(bufName, cmykBuffer.data(), nWidth, nHeight);
// 		}
// 	}
// 	fz_catch(ctx)
// 	{
// 	}
// 
// 	// 关键：退出前释放文档和上下文
// 	if (doc) fz_drop_document(ctx, doc);
// 	if (ctx) fz_drop_context(ctx);
// 
// 	return 0;
// }

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

