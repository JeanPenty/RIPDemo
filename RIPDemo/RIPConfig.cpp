#include "stdafx.h"
#include "RIPConfig.h"
#include "RIPipeline.h"

#include <cstdio>
#include <cstdlib>

namespace
{
	std::string g_strCfgPath;			// 配置文件全路径(UTF-8)
	bool        g_bPathResolved = false;

	// 目录能否新建文件(不能就说明该目录只读，需要退到用户目录)
	bool IsDirWritable(const std::wstring& wDirIn)
	{
		if (wDirIn.empty()) return false;

		std::wstring wDir = wDirIn;
		if (wDir[wDir.size() - 1] != L'\\' && wDir[wDir.size() - 1] != L'/')
			wDir += L'\\';

		const std::wstring wTest = wDir + L"~riptest.tmp";
		FILE* fp = _wfopen(wTest.c_str(), L"wb");
		if (!fp) return false;

		fclose(fp);
		::DeleteFileW(wTest.c_str());
		return true;
	}

	// 取 exe 所在目录(带结尾反斜杠)
	std::wstring GetExeDir()
	{
		wchar_t szPath[MAX_PATH] = { 0 };
		if (::GetModuleFileNameW(NULL, szPath, MAX_PATH) == 0)
			return std::wstring();

		std::wstring s = szPath;
		const size_t p = s.find_last_of(L"\\/");
		if (p != std::wstring::npos) s = s.substr(0, p + 1);
		return s;
	}

	// %LOCALAPPDATA%\RIPDemo\ (目录不存在时创建)
	std::wstring GetLocalAppDataDir()
	{
		wchar_t szPath[MAX_PATH] = { 0 };
		const DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", szPath, MAX_PATH);
		if (n == 0 || n >= MAX_PATH) return std::wstring();

		std::wstring s = szPath;
		if (s[s.size() - 1] != L'\\' && s[s.size() - 1] != L'/')
			s += L'\\';
		s += L"RIPDemo\\";

		RIPExt::EnsureDirExists(RIPExt::WStringToUtf8(s));
		return s;
	}

	std::string Trim(const std::string& s)
	{
		size_t a = 0;
		size_t b = s.size();
		while (a < b && (unsigned char)s[a] <= ' ') ++a;
		while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
		return s.substr(a, b - a);
	}

	int ToInt(const std::string& s, int nDef)
	{
		if (s.empty()) return nDef;
		return std::atoi(s.c_str());
	}
}

std::string RIPConfig::GetConfigPath()
{
	if (g_bPathResolved) return g_strCfgPath;
	g_bPathResolved = true;

	// 1) 优先程序目录
	const std::wstring wExeDir = GetExeDir();
	if (IsDirWritable(wExeDir))
	{
		g_strCfgPath = RIPExt::WStringToUtf8(wExeDir + L"RIPDemo.ini");
		return g_strCfgPath;
	}

	// 2) 退路: 用户目录
	const std::wstring wLocal = GetLocalAppDataDir();
	if (IsDirWritable(wLocal))
		g_strCfgPath = RIPExt::WStringToUtf8(wLocal + L"RIPDemo.ini");

	return g_strCfgPath;
}

bool RIPConfig::Save(const RIPParams& params)
{
	const std::string strPath = GetConfigPath();
	if (strPath.empty()) return false;

	std::string s;
	s += "# RIPDemo settings (UTF-8). Auto-generated, safe to edit by hand.\n";
	s += "# key=value, one per line; '#' starts a comment.\n";

	char szBuf[256] = { 0 };

	sprintf_s(szBuf, sizeof(szBuf), "bitdepth=%d\n", params.BitDepth());
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "dpi_x=%d\n", params.XDpi());
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "dpi_y=%d\n", params.YDpi());
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "spot_separate=%d\n", params.m_bSpotSeparate ? 1 : 0);
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "threads=%d\n", params.ThreadCount());
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "screen_enable=%d\n", params.m_bEnableScreen ? 1 : 0);
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "dither_mode=%d\n", params.DitherMode());
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "fs_serpentine=%d\n", params.m_bFSSerpentine ? 1 : 0);
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "screen_rotate_y=%d\n", params.m_bRotateScreenY ? 1 : 0);
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "thr_c=%d\n", params.ThrC());
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "thr_m=%d\n", params.ThrM());
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "thr_y=%d\n", params.ThrY());
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "thr_k=%d\n", params.ThrK());
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "scale_long_side=%d\n", params.m_nScaleToLongSide);
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "dump_merged_cmyk=%d\n", params.m_bDumpMergedCMYK ? 1 : 0);
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "output_prn=%d\n", params.m_nPrnVersion);
	s += szBuf;

	sprintf_s(szBuf, sizeof(szBuf), "gray_to_k=%d\n", params.m_bGrayToK ? 1 : 0);
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "gray_tol=%d\n", params.GrayTol());
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "noaa=%d\n", params.m_bNoAA ? 1 : 0);
	s += szBuf;
	sprintf_s(szBuf, sizeof(szBuf), "noaa_strength=%d\n", params.NoAAStrength());
	s += szBuf;
	s += "cmyk_icc=" + params.m_strCMYKICC + "\n";
	s += "input_icc=" + params.m_strInputICC + "\n";
	s += "dump_dir=" + params.m_strDumpDir + "\n";
	s += "output_root=" + params.m_strOutputRoot + "\n";

	FILE* fp = _wfopen(RIPExt::Utf8ToWString(strPath).c_str(), L"wb");
	if (!fp) return false;

	const size_t nWrite = fwrite(s.data(), 1, s.size(), fp);
	fclose(fp);
	return nWrite == s.size();
}

bool RIPConfig::Load(RIPParams& params)
{
	const std::string strPath = GetConfigPath();
	if (strPath.empty()) return false;

	FILE* fp = _wfopen(RIPExt::Utf8ToWString(strPath).c_str(), L"rb");
	if (!fp) return false;

	std::string s;
	char szBuf[4096] = { 0 };
	size_t nRead = 0;
	while ((nRead = fread(szBuf, 1, sizeof(szBuf), fp)) > 0)
		s.append(szBuf, nRead);
	fclose(fp);

	if (s.empty()) return false;

	bool bAny = false;
	size_t nPos = 0;
	while (nPos <= s.size())
	{
		size_t nEnd = s.find('\n', nPos);
		if (nEnd == std::string::npos) nEnd = s.size();

		const std::string strLine = Trim(s.substr(nPos, nEnd - nPos));
		nPos = nEnd + 1;

		if (strLine.empty() || strLine[0] == '#' || strLine[0] == ';') continue;

		const size_t nEq = strLine.find('=');
		if (nEq == std::string::npos) continue;

		const std::string strKey = Trim(strLine.substr(0, nEq));
		const std::string strVal = Trim(strLine.substr(nEq + 1));
		bAny = true;

		if (strKey == "bitdepth")
		{
			params.m_nBitDepth = (ToInt(strVal, 1) == 2) ? 2 : 1;
		}
		else if (strKey == "dpi_x")
		{
			const int v = ToInt(strVal, 0);
			if (v > 0) params.m_nXDpi = v;
		}
		else if (strKey == "dpi_y")
		{
			const int v = ToInt(strVal, 0);
			if (v > 0) params.m_nYDpi = v;
		}
		else if (strKey == "spot_separate")
		{
			params.m_bSpotSeparate = (ToInt(strVal, 0) != 0);
		}
		else if (strKey == "threads")
		{
			const int v = ToInt(strVal, 0);
			params.m_nThreadCount = (v < 0) ? 0 : (v > 32 ? 32 : v);
		}
		else if (strKey == "dither_mode")
		{
			const int v = ToInt(strVal, 2);
			params.m_nDitherMode = (v < 0 || v > 2) ? 2 : v;
			params.m_bEnableScreen = (params.m_nDitherMode != 0);
		}
		else if (strKey == "fs_serpentine")
		{
			params.m_bFSSerpentine = (ToInt(strVal, 1) != 0);
		}
		else if (strKey == "screen_enable")
		{
			// 老配置兼容: 只有没写 dither_mode 时才靠这个键决定
			params.m_bEnableScreen = (ToInt(strVal, 1) != 0);
			if (!params.m_bEnableScreen) params.m_nDitherMode = 0;
		}
		else if (strKey == "screen_rotate_y")
		{
			params.m_bRotateScreenY = (ToInt(strVal, 0) != 0);
		}
		else if (strKey == "thr_c")
		{
			params.m_nThrC = RIPParams::Clamp255(ToInt(strVal, 0));
		}
		else if (strKey == "thr_m")
		{
			params.m_nThrM = RIPParams::Clamp255(ToInt(strVal, 0));
		}
		else if (strKey == "thr_y")
		{
			params.m_nThrY = RIPParams::Clamp255(ToInt(strVal, 0));
		}
		else if (strKey == "thr_k")
		{
			params.m_nThrK = RIPParams::Clamp255(ToInt(strVal, 0));
		}
		else if (strKey == "scale_long_side")
		{
			const int v = ToInt(strVal, 0);
			if (v >= 0) params.m_nScaleToLongSide = v;
		}
		else if (strKey == "dump_merged_cmyk")
		{
			params.m_bDumpMergedCMYK = (ToInt(strVal, 0) != 0);
		}
		else if (strKey == "output_prn")
		{
			// 0 = 不输出, 1 = PRN1RipHeader, 2 = PRN2RipHeader(旧值 1 = 输出 PRN1)
			int v = ToInt(strVal, 0);
			if (v < 0) v = 0;
			if (v > 2) v = 2;
			params.m_nPrnVersion = v;
		}
		else if (strKey == "gray_to_k")
		{
			params.m_bGrayToK = (ToInt(strVal, 1) != 0);
		}
		else if (strKey == "gray_tol")
		{
			params.m_nGrayTol = RIPParams::Clamp255(ToInt(strVal, 48));
		}
		else if (strKey == "noaa")
		{
			params.m_bNoAA = (ToInt(strVal, 0) != 0);
		}
		else if (strKey == "noaa_strength")
		{
			const int v = ToInt(strVal, 50);
			params.m_nNoAAStrength = v < 0 ? 0 : (v > 100 ? 100 : v);
		}
		else if (strKey == "cmyk_icc")
		{
			params.m_strCMYKICC = strVal;
		}
		else if (strKey == "input_icc")
		{
			params.m_strInputICC = strVal;
		}
		else if (strKey == "dump_dir")
		{
			params.m_strDumpDir = strVal;
		}
		else if (strKey == "output_root")
		{
			params.m_strOutputRoot = strVal;
		}
	}

	return bAny;
}
