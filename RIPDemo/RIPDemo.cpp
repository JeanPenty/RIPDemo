// dui-demo.cpp : main source file
//

#include "stdafx.h"
#include "MainDlg.h"
#include <SAppCfg.h>
#pragma comment(lib, "shlwapi.lib")

#define SYS_NAMED_RESOURCE _T("soui-sys-resource.dll")

#define INIT_R_DATA
#include "res/resource.h"

using namespace SNS;

//debug时方便调试设置当前目录以便从文件加载资源
SStringT SetDefaultDir()
{
	TCHAR szCurrentDir[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, szCurrentDir, sizeof(szCurrentDir));

	LPTSTR lpInsertPos = _tcsrchr(szCurrentDir, _T('\\'));
#ifdef _DEBUG
	_tcscpy(lpInsertPos + 1, _T("..\\RIPDemo"));
#else
	_tcscpy(lpInsertPos + 1, _T("\0"));
#endif
	SetCurrentDirectory(szCurrentDir);
	return szCurrentDir;
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine, int /*nCmdShow*/)
{
	HRESULT hRes = OleInitialize(NULL);
	SASSERT(SUCCEEDED(hRes));
	SStringT strDir = SetDefaultDir();

	int nRet = 0;
    SApplication app(hInstance);
    SAppCfg cfg;
	//使用SKIA渲染界面
	cfg.SetRender(Render_Skia);
    cfg.SetImgDecoder(ImgDecoder_Stb).SetAppDir(strDir).SetLog(TRUE);
    cfg.SetSysResPeFile(SYS_NAMED_RESOURCE);
#ifdef _DEBUG
	cfg.SetAppResFile(strDir+_T("/uires"));
#else
	cfg.SetAppResPeHandle(hInstance);
#endif//_DEBUG

	//加载多语言翻译模块。
	cfg.EnableMultiLang(_T("translator:lang_cn"), TRUE);
    // regisger external widget and skinobj

	if (!cfg.DoConfig(&app))
    {
        return -1;
    }
    // 如果需要在代码中使用R::id::namedid这种方式来使用控件必须要这一行代码：2016年2月2日，R::id,R.name是由uiresbuilder
    // 增加-h .\res\resource.h 这2个参数后生成的。
    app.InitXmlNamedID((const LPCWSTR *)&R.name, (const int *)&R.id, sizeof(R.id) / sizeof(int));

    {
        // show main window
        CMainDlg dlgMain;
        dlgMain.Create(GetActiveWindow());
        dlgMain.SendMessage(WM_INITDIALOG);
        dlgMain.CenterWindow(dlgMain.m_hWnd);
		dlgMain.ShowWindow(SW_SHOWNORMAL);
        nRet = app.Run(dlgMain.m_hWnd);
    }
	OleUninitialize();
	return nRet;
}
