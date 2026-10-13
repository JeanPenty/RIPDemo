// ============================================================================
// RIPTest.cpp —— 控制台自测: 验证 Render -> CMYK 分色 -> 1bit/2bit RIP 全链路
//
// 用法:  RIPDemo.exe --riptest <input.pdf> [outDir] [dpi] [bit] [spot]
//   例:  RIPDemo.exe --riptest test.pdf .\out 300 1
//        RIPDemo.exe --riptest test.pdf .\out 600 2 spot
//        RIPDemo.exe --riptest test.pdf .\out 600 2 --packed --prn|--prn2  (额外落 PRN1RipHeader 容器)
// 输出: 打包模式(--packed)下 <stem>_p0000_CMYK.tif(1bit/2bit 位打包, 喂板卡)
//       + <stem>_p0000_CMYK_preview.tif(8bit, 图片查看器可直接打)；
//       专色版另一个 TIFF(1bit, 也可直接打)；预览模式只出 8bit CMYK。
// 说明: C/M/Y/K 四通道位平面合并为一个 TIFF(<stem>_p0000_CMYK.tif)；
//       专色分离时每个专色另出一个 TIFF。
//
// 其它模式:
//   --seplist  <pdf>                              打印页面用到的专色列表
//   --sepdiag  <pdf> [scale] [page0] [out.bmp]    原始分离通道诊断(可导出渲染 BMP)
//   --cfgtest                                     配置读写自测
//   --pbench   <pdf> <outDir> <dpi> <bit> [--threads=N] [--packed] [spot]
//              [--fs|--bayer|--thresh] [--noserp] [--noaa[=N]]
//              [--thrc=N --thrm=N --thry=N --thrk=N] [--preview] [--prn|--prn2] [--outroot=<目录>]
//                                                 页级并行基准(线程池 + 进度)
//
// 不带 pdf 时使用 exe 同目录下的 test.pdf 做冒烟测试。
// 本函数在 _tWinMain 最前面调用；返回 >=0 就直接退出，不创建窗口。
// ============================================================================
#include "stdafx.h"
#include "RIPipeline.h"
#include "RIPParams.h"
#include "RIPConfig.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

static void DumpPlaneInfo(const char* name, const RIPPlane& pl, float inkPercent)
{
	printf("    [%-6s] %dx%d  %dbit  stride=%d bytes  出墨率=%.2f%%\n",
		   name, pl.m_nWidth, pl.m_nHeight, pl.m_nBit, pl.m_nStride,
		   inkPercent * 100.0f);
}

// 打印页面用到的分色/专色层信息，确认专色解析结果
int DumpPageSeparations(const char* szPdf)
{
	printf("=========== 页面分色信息 ===========\n");
	printf("  文件: %s\n\n", szPdf);

	RIPipeline pipe;
	std::vector<RIPSepInfo> vecSeps;
	if (!pipe.QueryPageSeparations(szPdf, 0, vecSeps))
	{
		printf("[FAIL] %s\n", pipe.LastError().c_str());
		return 2;
	}

	if (vecSeps.empty())
	{
		printf("  本页没有专色(分色)层，走普通 CMYK 合成路径即可。\n");
		return 0;
	}

	const char* kBehav[] = { "Composite", "Spot", "Disabled" };
	printf("  共 %d 个分色:\n", (int)vecSeps.size());
	for (size_t i = 0; i < vecSeps.size(); ++i)
	{
		int nb = vecSeps[i].m_nBehavior;
		if (nb < 0 || nb > 2) nb = 0;
		printf("    [%d] %-24s %s\n", (int)i, vecSeps[i].m_strName.c_str(), kBehav[nb]);
	}
	printf("\n=========== 查询结束 ===========\n");
	return 0;
}

// ---- 命令行开关(多个子命令共用) ----
static int  g_nDitherMode = 2;   // CLI: --bayer / --thresh 复用
static bool g_bFSSerp = true;    // CLI: --noserp 关闭蛇形扫描
static int  g_nPrnVer = 0;   // CLI: --prn = PRN1, --prn2 = PRN2
static std::string g_strOutRoot;  // CLI: --outroot=<目录> 输出根目录(空=跟随 PDF 目录)
static bool g_bPrev = false;     // CLI: --preview 是否输出预览(全局, 供 SavePageResult 读取)

int RunRipSelfTest(const char* szPdf, const char* szOut, int nDpi, int nBit, bool bSpot, bool bPacked,
                   bool bGrayK, int nGrayTol, bool bNoAA, int nNoAAStr,
	           const std::string& strDumpDir)
{
	printf("=========== RIP 自测开始 ===========\n");
	printf("  文件   : %s\n", szPdf);
	printf("  DPI    : %d\n", nDpi);
	printf("  位深   : %d bit\n", nBit);
	printf("  专色   : %s\n", bSpot ? "是" : "否");
	printf("  输出格式: %s\n", bPacked ? "原生位打包 TIFF" : "8bit 预览 TIFF");
	printf("  GrayToK : %s (tol=%d)\n", bGrayK ? "on" : "off", nGrayTol);
	printf("  NoAA    : %s (strength=%d)\n", bNoAA ? "on" : "off", nNoAAStr);
	printf("  抖动    : %s%s\n",
		g_nDitherMode == 2 ? "FS误差扩散" : (g_nDitherMode == 1 ? "有序网屏" : "纯阈值"),
		g_nDitherMode == 2 ? (g_bFSSerp ? " (蛇形)" : " (单向)") : "");
	printf("====================================\n\n");

	RIPParams params;
	params.m_nXDpi = nDpi;
	params.m_nYDpi = nDpi;
	params.m_nBitDepth = nBit;
	params.m_bSpotSeparate = bSpot;
	params.m_nDitherMode = g_nDitherMode;
	params.m_bEnableScreen = (g_nDitherMode != 0);
	params.m_bFSSerpentine = g_bFSSerp;
	params.m_bRotateScreenY = false;
	params.m_bGrayToK = bGrayK;
	params.m_nGrayTol = nGrayTol;
	params.m_bNoAA = bNoAA;
	params.m_nNoAAStrength = nNoAAStr;
	params.m_nPrnVersion = g_nPrnVer;
	params.m_bDumpMergedCMYK = !strDumpDir.empty();
	params.m_strDumpDir = strDumpDir;
	params.m_nThrC = 0;  params.m_nThrM = 0;
	params.m_nThrY = 0;  params.m_nThrK = 0;

	// exe 所在目录（默认 ICC 和分色曲线）
	char szExeDir[MAX_PATH] = { 0 };
	::GetModuleFileNameA(NULL, szExeDir, MAX_PATH);
	char* pSlash = strrchr(szExeDir, '\\');
	if (pSlash) *pSlash = 0;
	std::string strAppDir = szExeDir;

	std::string cmykIcc = RIPExt::FindDefaultCMYKICC(strAppDir);
	printf("  CMYK ICC: %s\n", cmykIcc.empty() ? "(未找到, 走 lcms 默认)" : cmykIcc.c_str());

	RIPipeline pipe;
	pipe.SetAppDir(strAppDir);

	// 页尺寸
	int nPage = 0;
	int nW = 0, nH = 0;
	double fWpt = 0, fHpt = 0;
	if (!pipe.QueryPageSize(szPdf, 0, nW, nH, fWpt, fHpt))
	{
		printf("[FAIL] 打开 PDF 失败: %s\n", pipe.LastError().c_str());
		return 2;
	}
	printf("  总页数  : %d\n", pipe.CountPages(szPdf));
	printf("  第1页尺寸: %.1f x %.1f pt\n", fWpt, fHpt);

	// 输出目录
	std::string outDir = RIPExt::MakeOutputDir(szPdf, g_strOutRoot);
	if (szOut && szOut[0]) outDir = szOut;
	printf("  输出目录 : %s\n\n", outDir.c_str());

	// 只跑第一页做验证
	RIPPageResult result;
	if (!pipe.RunPage(szPdf, nPage, params, result, nullptr))
	{
		printf("[FAIL] RIP 第 %d 页失败: %s\n", nPage, pipe.LastError().c_str());
		return 3;
	}

	printf("[OK] 第 %d 页 RIP 完成\n", nPage);
	printf("  输出尺寸 : %d x %d px  (%d bit)\n", result.m_nWidth, result.m_nHeight, result.m_nBit);
	printf("  渲染耗时 : %.0f ms\n", result.m_nRenderMS);
	printf("  分色耗时 : %.0f ms\n", result.m_nSepMS);
	printf("  RIP 耗时 : %.0f ms\n", result.m_nRipMS);
	printf("  总耗时   : %.0f ms\n", result.m_nRenderMS + result.m_nSepMS + result.m_nRipMS);
	printf("  平面清单 :\n");

	for (size_t i = 0; i < result.m_vecPlanes.size(); ++i)
		DumpPlaneInfo(result.m_vecInkNames[i].c_str(), result.m_vecPlanes[i], result.m_vecInkPercent[i]);

	// 落盘验证: C/M/Y/K 合并为一个多平面 TIFF；专色另出一个文件
	printf("\n  输出中间产物(TIFF):\n");
	int nSaved = 0;

	const bool bHasCMYK = (result.m_vecPlanes.size() >= 4);

	if (bHasCMYK)
	{
		// ---- C/M/Y/K 四色版位平面合并为一个 TIFF ----
		const RIPPlane* arrCMYK[4] = {
			&result.m_vecPlanes[0], &result.m_vecPlanes[1],
			&result.m_vecPlanes[2], &result.m_vecPlanes[3]
		};

		if (bPacked)
		{
			// 生产: 原生 1bit/2bit 位打包，行排布与单通道文件一致，可直接喂板卡
			std::string fn = RIPExt::JoinPath(outDir,
											  RIPExt::PlaneFileName(szPdf, nPage, "CMYK"));
			if (RIPipeline::SaveCMYKPlanesTIFF(fn, arrCMYK, nDpi, 0))
			{
				printf("    + %s  (原生 1bit/2bit, 4 通道合并)\n", fn.c_str());
				++nSaved;
			}
			else
			{
				printf("    x %s  (写入失败)\n", fn.c_str());
			}

			// 8bit 预览副本: 上面 TIFF 是 1bit/2bit + CMYK，Windows 图片查看器
			// 打不开(GDI+/WIC 只支持 8bit 的 SEPARATED)，另出一份可直接双击打开
			std::string fnv = RIPExt::JoinPath(outDir,
											   RIPExt::PlaneFileName(szPdf, nPage, "CMYK_preview"));
			if (RIPipeline::SaveCMYKPlanesTIFF(fnv, arrCMYK, nDpi, 8))
			{
				printf("    + %s  (8bit 预览, 可直接打)\n", fnv.c_str());
				++nSaved;
			}
			else
			{
				printf("    x %s  (写入失败)\n", fnv.c_str());
			}
		}
		else
		{
			// 预览模式：统一出 8bit，方便肉眼比对
			std::string fn = RIPExt::JoinPath(outDir,
											  RIPExt::PlaneFileName(szPdf, nPage, "CMYK"));
			if (RIPipeline::SaveCMYKPlanesTIFF(fn, arrCMYK, nDpi, 8))
			{
				printf("    + %s  (8bit 预览, 4 通道合并)\n", fn.c_str());
				++nSaved;
			}
			else
			{
				printf("    x %s  (写入失败)\n", fn.c_str());
			}
		}
	}

	// PRN 容器(喷头板卡): 1 = PRN1RipHeader(行内 K,C,M,Y), 2 = PRN2RipHeader(行内 C,M,Y,K)
	if (g_nPrnVer > 0 && result.m_vecPlanes.size() >= 4)
	{
		std::string fnPrn = RIPExt::PlaneFileName(szPdf, nPage, "CMYK");
		const size_t nDot = fnPrn.find_last_of('.');
		if (nDot != std::string::npos) fnPrn = fnPrn.substr(0, nDot);
		fnPrn = RIPExt::JoinPath(outDir, fnPrn + ".prn");
		const int nPrnOk = (g_nPrnVer == 2)
			? (RIPipeline::SavePlanesPRN2(fnPrn, result.m_vecPlanes,
										 result.m_nWidth, result.m_nHeight, result.m_nBit,
										 nDpi, nDpi, 1) ? 1 : 0)
			: (RIPipeline::SavePlanesPRN(fnPrn, result.m_vecPlanes,
										result.m_nWidth, result.m_nHeight, result.m_nBit,
										nDpi, nDpi, 1) ? 1 : 0);
		if (nPrnOk)
		{
			printf("    + %s  (PRN%dRipHeader 容器, %d 色)\n", fnPrn.c_str(), g_nPrnVer,
				   (int)result.m_vecPlanes.size());
			++nSaved;
		}
		else
		{
			printf("    x %s  (写入失败)\n", fnPrn.c_str());
		}
	}

	// 剩余平面: CMYK 已合并，这里只处理专色，每个专色另出一个文件
	const size_t iBegin = bHasCMYK ? 4 : 0;
	for (size_t i = iBegin; i < result.m_vecPlanes.size(); ++i)
	{
		std::string fn = RIPExt::JoinPath(outDir,
										  RIPExt::PlaneFileName(szPdf, nPage, result.m_vecInkNames[i]));

		bool bOneOk = false;
		if (bPacked)
		{
			// 生产格式: 原生 1bit/2bit，每行从左到右
			// 按印刷习惯取 MINISWHITE: 1 = 出墨
			bOneOk = RIPipeline::SavePlaneBilevelTIFF(fn, result.m_vecPlanes[i], nDpi, 0);
		}
		else
		{
			// 肉眼可读预览: 转成 8bit 灰度
			bOneOk = RIPipeline::SavePlanePreviewTIFF(fn, result.m_vecPlanes[i], nDpi);
		}

		if (bOneOk)
		{
			printf("    + %s  (%dbit%s)\n", fn.c_str(), result.m_vecPlanes[i].m_nBit,
				   bPacked ? ", 原生打包" : ", 8bit 预览");
			++nSaved;
		}
		else
		{
			printf("    x %s  (写入失败)\n", fn.c_str());
		}
	}

	printf("\n=========== RIP 自测%s ===========\n", nSaved > 0 ? "通过" : "未产生文件");
	return (nSaved > 0) ? 0 : 4;
}

// ---------------------------------------------------------------------------
// --sepdiag : 原始分离通道诊断
//   --sepdiag <pdf> [scale] [page0] [out.bmp]
//   先确认「渲染层有没有把图元画出来」再查管线，能省一大圈。
// ---------------------------------------------------------------------------
static int g_nDiagPage = 0;
static char g_szDiagBmp[512] = { 0 };

int DiagSeparation(const char* szPdf, float fScale)
{
	printf("=========== SEP DIAG ===========\n");
	printf("  file: %s  scale=%.3f  page=%d\n", szPdf, fScale, g_nDiagPage);

	fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	if (!ctx) { printf("[FAIL] fz_new_context\n"); return 9; }
	fz_register_document_handlers(ctx);

	fz_document* doc = NULL;
	fz_page* page = NULL;
	fz_separations* sepsP = NULL;
	fz_separations* sepsC = NULL;
	fz_pixmap* pix = NULL;

	fz_var(doc); fz_var(page); fz_var(sepsP); fz_var(sepsC); fz_var(pix);

	fz_try(ctx)
	{
		doc = fz_open_document(ctx, szPdf);
		page = fz_load_page(ctx, doc, g_nDiagPage);

		sepsP = fz_page_separations(ctx, page);
		if (sepsP)
		{
			int n = fz_count_separations(ctx, sepsP);
			printf("page seps: n=%d active=%d\n", n, fz_count_active_separations(ctx, sepsP));
		}
		else printf("page seps: NULL\n");

		sepsC = fz_clone_separations_for_overprint(ctx, sepsP);
		fz_separations* sepsUse = sepsC ? sepsC : sepsP;

		fz_rect bbox = fz_bound_page(ctx, page);
		fz_matrix ctm = fz_scale(fScale, fScale);
		fz_irect irect = fz_round_rect(fz_transform_rect(bbox, ctm));

		fz_colorspace* cs = fz_device_bgr(ctx);
		printf("irect: (%d,%d)-(%d,%d)\n", irect.x0, irect.y0, irect.x1, irect.y1);

		// 与 RIPipeline::RunPage 一样: 必须走官方 API，否则变换叠加两次专色通道会全 0
		pix = fz_new_pixmap_from_page_with_separations(ctx, page, ctm, cs, sepsUse, 0);

		const int nc = fz_pixmap_components(ctx, pix);
		const int w = fz_pixmap_width(ctx, pix);
		const int h = fz_pixmap_height(ctx, pix);
		const int stride = fz_pixmap_stride(ctx, pix);
		unsigned char* s = fz_pixmap_samples(ctx, pix);
		printf("pixmap: %dx%d comps=%d stride=%d (expect %d)\n",
			   w, h, nc, stride, w * nc);
		for (int c = 0; c < nc && c < 8; ++c)
			printf("  firstpx[%d] = %d\n", c, (int)s[c]);

		// 导出原始渲染 24bpp BMP（行倒序, 每行按 4 字节对齐）—— 先看渲染层
		if (g_szDiagBmp[0])
		{
			FILE* fp = fopen(g_szDiagBmp, "wb");
			if (fp)
			{
				const int rowB = ((w * 3 + 3) / 4) * 4;
				const DWORD dataSize = (DWORD)rowB * (DWORD)h;
				unsigned char hdr[54] = { 0 };
				const DWORD fileSize = 54 + dataSize;
				const DWORD offBits = 54, infoSize = 40;
				const WORD planes = 1, bpp = 24;
				hdr[0] = 'B'; hdr[1] = 'M';
				memcpy(hdr + 2, &fileSize, 4);
				memcpy(hdr + 10, &offBits, 4);
				memcpy(hdr + 14, &infoSize, 4);
				memcpy(hdr + 18, &w, 4);
				memcpy(hdr + 22, &h, 4);
				memcpy(hdr + 26, &planes, 2);
				memcpy(hdr + 28, &bpp, 2);
				memcpy(hdr + 34, &dataSize, 4);
				fwrite(hdr, 1, 54, fp);

				std::vector<unsigned char> row((size_t)rowB, 0);
				for (int y = h - 1; y >= 0; --y)
				{
					const unsigned char* r = s + (size_t)y * stride;
					for (int x = 0; x < w; ++x)
						memcpy(row.data() + (size_t)x * 3, r + (size_t)x * nc, 3);   // 源就是 BGR
					fwrite(row.data(), 1, rowB, fp);
				}
				fclose(fp);
				printf("[bmp] 已导出 %s (%dx%d)\n", g_szDiagBmp, w, h);
			}
			else printf("[bmp] 打不开 %s\n", g_szDiagBmp);
		}

		const int cols = 40, rows = 16;
		for (int c = 0; c < nc; ++c)
		{
			printf("---- ch[%d] map (top=row0) ----\n", c);
			for (int ry = 0; ry < rows; ++ry)
			{
				printf("  ");
				for (int rx = 0; rx < cols; ++rx)
				{
					int x = (int)((rx + 0.5) * w / cols); if (x >= w) x = w - 1;
					int y = (int)((ry + 0.5) * h / rows); if (y >= h) y = h - 1;
					unsigned char v = s[(size_t)y * stride + (size_t)x * nc + c];
					printf("%c", v > 160 ? '#' : (v > 40 ? '+' : (v ? '.' : ' ')));
				}
				printf("\n");
			}
		}

		printf("per-channel nonzero / max :\n");
		for (int c = 0; c < nc; ++c)
		{
			long long nz = 0; int mx = 0;
			for (int y = 0; y < h; ++y)
			{
				unsigned char* r = s + (size_t)y * stride;
				for (int x = 0; x < w; ++x)
				{
					unsigned char v = r[(size_t)x * nc + c];
					if (v) ++nz;
					if (v > mx) mx = v;
				}
			}
			printf("  ch[%d] nonzero=%lld (%5.2f%%) max=%d\n", c, nz,
				   100.0 * (double)nz / ((double)w * h), mx);
		}
	}
	fz_always(ctx)
	{
		if (pix) fz_drop_pixmap(ctx, pix);
		if (sepsC) fz_drop_separations(ctx, sepsC);
		if (sepsP) fz_drop_separations(ctx, sepsP);
		if (page) fz_drop_page(ctx, page);
		if (doc) fz_drop_document(ctx, doc);
	}
	fz_catch(ctx)
	{
		printf("[EXC] %s\n", fz_caught_message(ctx));
	}

	fz_drop_context(ctx);
	printf("=========== SEP DIAG END ===========\n");
	return 0;
}

// ============================================================================
// 配置读写自测: 写一份改过的参数, 再读回来逐个比对
//   RIPDemo.exe --cfgtest
// ============================================================================
static int RunConfigSelfTest()
{
	printf("=========== RIP 界面配置读写自测 ===========\n");

	const std::string strPath = RIPConfig::GetConfigPath();
	printf("  配置文件: %s\n", strPath.empty() ? "(找不到可写位置)" : strPath.c_str());
	if (strPath.empty())
	{
		printf("[FAIL] 程序目录和用户目录都不可写\n");
		return 1;
	}

	// 自测结束要如实还原旧路径的旧值: 先保存原文件, 结束后原样还原, 否则会把用户的配置冲掉
	std::string strCfgBak;
	{
		std::ifstream fin(strPath.c_str(), std::ios::binary);
		if (fin.good())
		{
			std::ostringstream oss;
			oss << fin.rdbuf();
			strCfgBak = oss.str();
		}
	}
	const bool bCfgExisted = !strCfgBak.empty();

	const int  kBit = 2;
	const int  kX = 720, kY = 600;
	const bool kSpot = true;
	const int  kTc = 11, kTm = 22, kTy = 33, kTk = 44;
	const std::string kIcc = "D:/icc/\xE6\xB5\x8B\xE8\xAF\x95\xE6\x9B\xB2\xE7\xBA\xBF.icc";   // 中文名, 顺带验证 UTF-8 编码

	RIPParams w;
	w.m_nBitDepth = kBit;
	w.m_nXDpi = kX;
	w.m_nYDpi = kY;
	w.m_bSpotSeparate = kSpot;
	w.m_nThrC = kTc;  w.m_nThrM = kTm;  w.m_nThrY = kTy;  w.m_nThrK = kTk;
	const bool kGrayK = true;
	const int kGrayTol = 55;
	const bool kNoAA = false;
	const int kNoAAStr = 75;
	const int  kDither = 2;
	const bool kSerp = true;
	const int  kThreads = 4;
	const int  kPrnVer = 2;     // PRN 容器格式(0/1/2)也参与读写
	w.m_nThreadCount = kThreads;
	w.m_bGrayToK = kGrayK;
	w.m_nGrayTol = kGrayTol;
	w.m_bNoAA = kNoAA;
	w.m_nNoAAStrength = kNoAAStr;
	w.m_nDitherMode = kDither;
	w.m_bFSSerpentine = kSerp;
	w.m_nPrnVersion = kPrnVer;
	w.m_strCMYKICC = kIcc;

	if (!RIPConfig::Save(w))
	{
		printf("[FAIL] 保存失败\n");
		return 2;
	}

	RIPParams r;
	if (!RIPConfig::Load(r))
	{
		printf("[FAIL] 读取失败\n");
		return 3;
	}

	int nBad = 0;
	if (r.m_nBitDepth != kBit)
	{
		printf("  [x] 位深      写=%d 读=%d\n", kBit, r.m_nBitDepth);  ++nBad;
	}
	if (r.m_nXDpi != kX || r.m_nYDpi != kY)
	{
		printf("  [x] 分辨率    写=%dx%d 读=%dx%d\n", kX, kY, r.m_nXDpi, r.m_nYDpi);  ++nBad;
	}
	if (r.m_bSpotSeparate != kSpot)
	{
		printf("  [x] 专色分离  写=%d 读=%d\n", kSpot ? 1 : 0, r.m_bSpotSeparate ? 1 : 0);  ++nBad;
	}
	if (r.m_bGrayToK != kGrayK || r.m_nGrayTol != kGrayTol)
	{
		printf("  [x] GrayToK     w=%d,%d r=%d,%d\n", kGrayK ? 1 : 0, kGrayTol,
			   r.m_bGrayToK ? 1 : 0, r.m_nGrayTol);  ++nBad;
	}
	if (r.m_bNoAA != kNoAA || r.m_nNoAAStrength != kNoAAStr)
	{
		printf("  [x] NoAA        w=%d,%d r=%d,%d\n", kNoAA ? 1 : 0, kNoAAStr,
			   r.m_bNoAA ? 1 : 0, r.m_nNoAAStrength);  ++nBad;
	}
	if (r.m_nDitherMode != kDither || r.m_bFSSerpentine != kSerp)
	{
		printf("  [x] 抖动模式   写=%d,%d 读=%d,%d\n", kDither, kSerp ? 1 : 0,
			   r.m_nDitherMode, r.m_bFSSerpentine ? 1 : 0);  ++nBad;
	}
	if (r.m_nThrC != kTc || r.m_nThrM != kTm || r.m_nThrY != kTy || r.m_nThrK != kTk)
	{
		printf("  [x] 阈值      写=%d,%d,%d,%d 读=%d,%d,%d,%d\n",
			   kTc, kTm, kTy, kTk, r.m_nThrC, r.m_nThrM, r.m_nThrY, r.m_nThrK);
		++nBad;
	}
	if (r.m_nPrnVersion != kPrnVer)
	{
		printf("  [x] PRN      写=%d 读=%d\n", kPrnVer, r.m_nPrnVersion);  ++nBad;
	}
	if (r.m_nThreadCount != kThreads)
	{
		printf("  [x] 线程数    写=%d 读=%d\n", kThreads, r.m_nThreadCount);  ++nBad;
	}

	if (r.m_strCMYKICC != kIcc)
	{
		printf("  [x] ICC 路径  写=%s 读=%s\n", kIcc.c_str(), r.m_strCMYKICC.c_str());  ++nBad;
	}

	// 还原自测前是否本来就存在(没有就删掉, 自测别新建)
	if (bCfgExisted)
	{
		std::ofstream fout(strPath.c_str(), std::ios::binary | std::ios::trunc);
		if (fout.good()) fout.write(strCfgBak.data(), (std::streamsize)strCfgBak.size());
	}
	else
	{
		::DeleteFileA(strPath.c_str());
	}

	if (nBad == 0) printf("[OK] 全部键读写/读回一致\n");
	else           printf("[FAIL] 有 %d 项不一致\n", nBad);
	return nBad;
}

// ============================================================================
// --pbench: 页级并行基准
//   --pbench <pdf> <outDir> <dpi> <bit> [--threads=N] [--packed] [spot]
//            [--fs|--bayer|--thresh] [--noserp] [--noaa[=N]]
//            [--thrc=N --thrm=N --thry=N --thrk=N] [--preview] [--prn|--prn2] [--outroot=<目录>]
//
//   用与 GUI 完全相同的 RIPPool + SavePagePlanes 走一遍，验证：
//     1) 多线程并行不改数据(可与 --threads=1 的产物 md5 对照)
//     2) 并行确实加速
//     3) 取消/重启后的生命周期没有问题
// ============================================================================
namespace
{
	struct BenchSlot
	{
		std::string m_strErr;		// 空 = 该页成功
		int			m_nSaved = 0;	// 该页落盘文件数
		std::vector<RIPDotStat> m_vecDotStats;   // 第 1 页的墨点统计(其它页为空)
		int			m_nDpi = 0;   // 本次 RIP 的 DPI(统计阈值换算用)
		int			m_nBit = 1;
		double		m_nMs = 0.0;	// 该页 RIP 总耗时
	};

	struct BenchCtx
	{
		const std::vector<RIPPool::PageTask>* m_pTasks = nullptr;
		std::vector<BenchSlot>*               m_pSlots = nullptr;
		const RIPParams*                      m_pParams = nullptr;
		bool                                  m_bPacked = true;
	};

	// 落一页的全部色版(与界面共用同一个落盘入口)
	int SavePageResult(const RIPPool::PageTask& task, const RIPParams& params,
					   const RIPPageResult& result, bool bPacked)
	{
		// bPacked=false 且带 --preview 时走完整落盘(生产 TIFF + 连续调预览)
		return RIPipeline::SavePagePlanes(task.m_strPdf, task.m_nPage, task.m_strOutDir,
										  params.YDpi(), result, !bPacked && g_bPrev, params.m_nPrnVersion);
	}

	void BenchTaskProc(void* pCtx, RIPipeline& pipe, int nTaskIdx, int nThreadIdx)
	{
		(void)nThreadIdx;
		BenchCtx* p = static_cast<BenchCtx*>(pCtx);
		const RIPPool::PageTask& task = (*p->m_pTasks)[nTaskIdx];
		BenchSlot& slot = (*p->m_pSlots)[nTaskIdx];

		const DWORD t0 = ::GetTickCount();
		RIPPageResult result;
		if (!pipe.RunPage(task.m_strPdf, task.m_nPage, *p->m_pParams, result, nullptr))
		{
			slot.m_strErr = pipe.LastError();
			return;
		}

		slot.m_nSaved = SavePageResult(task, *p->m_pParams, result, p->m_bPacked);
		slot.m_nMs = (double)(::GetTickCount() - t0);
		slot.m_vecDotStats = result.m_vecDotStats;
		slot.m_nDpi = p->m_pParams->YDpi();
		slot.m_nBit = p->m_pParams->BitDepth();

	}

	int RunPageBench(const std::string& strPdf, const std::string& strOut, int nDpi, int nBit,
					 bool bPacked, bool bSpot, int nThreads, bool bNoAA, int nNoAAStr,
					 int nThrC, int nThrM, int nThrY, int nThrK, bool bPreview)
	{
		RIPParams params;
		params.m_nXDpi = nDpi;
		params.m_nYDpi = nDpi;
		params.m_nBitDepth = nBit;
		params.m_bSpotSeparate = bSpot;
		params.m_nDitherMode = g_nDitherMode;
		params.m_bEnableScreen = (g_nDitherMode != 0);
		params.m_bFSSerpentine = g_bFSSerp;
		params.m_nThreadCount = nThreads;
		params.m_bNoAA = bNoAA;
		params.m_nNoAAStrength = nNoAAStr;
		params.m_nThrC = nThrC;
		params.m_nThrM = nThrM;
		params.m_nThrY = nThrY;
		params.m_nThrK = nThrK;
		params.m_nPrnVersion = g_nPrnVer;

		char szExeDir[MAX_PATH] = { 0 };
		::GetModuleFileNameA(NULL, szExeDir, MAX_PATH);
		char* pSlash = strrchr(szExeDir, '\\');
		if (pSlash) *pSlash = 0;
		const std::string strAppDir = szExeDir;

		printf("=========== 页级并行基准 ===========\n");
		printf("  文件   : %s\n", strPdf.c_str());
		printf("  DPI    : %d  位深: %d bit\n", nDpi, nBit);
		printf("  并行线程: %d (%s)\n", nThreads, nThreads <= 0 ? "自动" : "指定");
		printf("  输出格式: %s\n", bPacked ? "原生位打包 TIFF" : "8bit 预览 TIFF");
		printf("  抖动   : %s%s\n",
			g_nDitherMode == 2 ? "FS误差扩散" : (g_nDitherMode == 1 ? "有序网屏" : "纯阈值"),
			g_nDitherMode == 2 ? (g_bFSSerp ? " (蛇形)" : " (单向)") : "");
		printf("  NoAA: %s(%d)  THR: %d,%d,%d,%d\n", bNoAA ? "on" : "off", nNoAAStr, nThrC, nThrM, nThrY, nThrK);
		printf("  PRN : %d    预览: %s\n", g_nPrnVer, g_bPrev ? "on" : "off");
		printf("====================================\n\n");

		// 预扫描页数 + 预建输出目录 + 拆任务(文件 x 页)
		std::vector<RIPPool::PageTask> tasks;
		long long nPagePixels = 0;   // 单页像素数(取第 1 页), 用于估内存
		{
			RIPipeline probe;
			probe.SetAppDir(strAppDir);
			const int nPages = probe.CountPages(strPdf);
			if (nPages <= 0)
			{
				printf("[FAIL] 打不开 PDF: %s\n", probe.LastError().c_str());
				return 2;
			}
			// 未显式给输出目录时, 按 --outroot(输出根目录)推导; 都没有则跟随 PDF 所在目录
			const std::string strOutDir = (strOut.empty() || strOut == "-")
				? RIPExt::MakeOutputDir(strPdf, g_strOutRoot) : strOut;
			RIPExt::EnsureDirExists(strOutDir);
			// 探第 1 页尺寸: 用于按可用内存估算线程数(A4@600DPI 单页峰值约 480MB)
			{
				int nPw = 0, nPh = 0;
				double fPw = 0, fPh = 0;
				if (probe.QueryPageSize(strPdf, 0, nPw, nPh, fPw, fPh))
					nPagePixels = (long long)(fPw * nDpi / 72.0 * fPh * nDpi / 72.0);
			}
			for (int i = 0; i < nPages; ++i)
				tasks.push_back(RIPPool::PageTask(0, i, strPdf, strOutDir));
			printf("  页数   : %d\n", nPages);
			if (nPagePixels > 0)
				printf("  单页峰值内存: 约 %d MB/线程\n",
					   (int)(RIPPool::EstimatePageBytes(nPagePixels) / (1024 * 1024)));
			printf("\n");
		}

		// 按可用内存钳位, 防止「大页 x 多线程」直接吃光内存崩掉
		const size_t nBytesPerThread = RIPPool::EstimatePageBytes(nPagePixels);
		int nAvailMB = 0, nMaxByMem = 0;
		const int nThreadsUse = RIPPool::ClampThreadsByMemory(nThreads, nBytesPerThread,
													  &nAvailMB, &nMaxByMem);
		if (nThreadsUse != nThreads)
			printf("  线程调整: %d -> %d  (可用内存 %d MB, 单页峰值约 %d MB)\n\n",
				   nThreads, nThreadsUse, nAvailMB,
				   (int)(nBytesPerThread / (1024 * 1024)));

		std::vector<BenchSlot> slots(tasks.size());
		BenchCtx ctx;
		ctx.m_pTasks = &tasks;
		ctx.m_pSlots = &slots;
		ctx.m_pParams = &params;
		ctx.m_bPacked = bPacked;

		const DWORD t0 = ::GetTickCount();
		const int nUsed = RIPPool::Run(strAppDir, nThreadsUse, tasks, BenchTaskProc, &ctx,
									   nullptr, nBytesPerThread);
		const DWORD t1 = ::GetTickCount();

		int nFail = 0, nSavedAll = 0;
		for (size_t i = 0; i < slots.size(); ++i)
		{
			nSavedAll += slots[i].m_nSaved;
			if (!slots[i].m_strErr.empty())
			{
				++nFail;
				printf("  [x] p%-4d %s\n", (int)i + 1, slots[i].m_strErr.c_str());
			}
		}

		printf("\n  启动线程 : %d (请求 %d%s)\n", nUsed, nThreads,
			   (nThreadsUse != nThreads) ? ", 受可用内存限制" : "");
		printf("  成功/失败: %d / %d\n", (int)slots.size() - nFail, nFail);
		printf("  输出文件 : %d\n", nSavedAll);
		printf("  墙钟耗时 : %.2f s\n", (t1 - t0) / 1000.0);
		// ---- 墨点统计: 多页 PDF 只统计第 1 页, 其余页为空 ----
		for (size_t i = 0; i < slots.size(); ++i)
		{
			if (slots[i].m_vecDotStats.empty()) continue;
			printf("\n  === 墨点统计  page %d  (%d DPI / %d bit / %s) ===\n",
				   (int)i + 1, slots[i].m_nDpi, slots[i].m_nBit,
				   slots[i].m_vecDotStats.size() > 4 ? "含专色" : "CMYK");
			printf("  %-8s %12s %12s %12s %12s %14s\n",
				   "ink", "dots", "small", "mid", "big", "inkPixels");
			long long tD = 0, tS = 0, tM = 0, tB = 0, tP = 0;
			for (size_t k = 0; k < slots[i].m_vecDotStats.size(); ++k)
			{
				const RIPDotStat& d = slots[i].m_vecDotStats[k];
				printf("  %-8s %12lld %12lld %12lld %12lld %14lld\n", d.m_strInk.c_str(),
					   d.m_nDots, d.m_nDotsSmall, d.m_nDotsMid, d.m_nDotsBig, d.m_nInkPixels);
				tD += d.m_nDots; tS += d.m_nDotsSmall; tM += d.m_nDotsMid;
				tB += d.m_nDotsBig; tP += d.m_nInkPixels;
			}
			printf("  %-8s %12lld %12lld %12lld %12lld %14lld\n", "TOTAL", tD, tS, tM, tB, tP);
			printf("  分档: 小点 < %d um, 中点 %d~%d um, 大点 >= %d um\n",
				   params.m_nDotSmallUM, params.m_nDotSmallUM, params.m_nDotMidUM,
				   params.m_nDotMidUM);
		}

		printf("\n=========== 基准%s ===========\n", nFail == 0 ? "通过" : "有失败页");
		return (nFail == 0) ? 0 : 5;
	}
}

int HandleRipTestCommandLine(const wchar_t* szCmdLine, const wchar_t* szExePath)
{
	std::wstring wcmd = szCmdLine ? szCmdLine : L"";
	const bool bSepList = (wcmd.find(L"--seplist") != std::wstring::npos);
	const bool bCfgTest = (wcmd.find(L"--cfgtest") != std::wstring::npos);
	const bool bSepDiag = (wcmd.find(L"--sepdiag") != std::wstring::npos);
	const bool bPBench = (wcmd.find(L"--pbench") != std::wstring::npos);
	if (!bSepList && !bSepDiag && !bCfgTest && !bPBench && wcmd.find(L"--riptest") == std::wstring::npos)
		return -1;   // 不是自测模式

	// 输出: 若 stdout 已被重定向到文件/管道(方便脚本控制台), 就保持不动;
	// 只有真的没输出时再挂接/新建控制台，避免界面程序没有控制台。
	HANDLE hStdOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
	const bool bRedirected = (hStdOut != NULL && hStdOut != INVALID_HANDLE_VALUE);

	if (!bRedirected)
	{
		if (!::AttachConsole(ATTACH_PARENT_PROCESS))
			::AllocConsole();

		FILE* fOut = nullptr;
		FILE* fErr = nullptr;
		freopen_s(&fOut, "CONOUT$", "w", stdout);
		freopen_s(&fErr, "CONOUT$", "w", stderr);
	}

	setvbuf(stdout, nullptr, _IONBF, 0);

	// 简单分帧
	std::vector<std::wstring> args;
	{
		std::wstring cur;
		bool inQuote = false;
		for (wchar_t c : wcmd)
		{
			if (c == L'"') { inQuote = !inQuote; continue; }
			if (!inQuote && (c == L' ' || c == L'\t'))
			{
				if (!cur.empty()) { args.push_back(cur); cur.clear(); }
				continue;
			}
			cur += c;
		}
		if (!cur.empty()) args.push_back(cur);
	}

	// 分色诊断模式: 打印页面用到的专色 + 原始通道图(可导出 BMP)
	if (bSepDiag)
	{
		std::string strPdf;
		size_t id = 0;
		for (size_t i = 0; i < args.size(); ++i)
			if (args[i] == L"--sepdiag") { id = i + 1; break; }
		if (id < args.size()) strPdf = RIPExt::WStringToUtf8(args[id]);
		else strPdf = "spot_test.pdf";
		float fSd = 1.0f;
		if (id + 1 < args.size()) fSd = (float)_wtof(args[id + 1].c_str());
		if (fSd <= 0.0f) fSd = 1.0f;
		if (id + 2 < args.size())
		{
			const int pg = _wtoi(args[id + 2].c_str());
			if (pg >= 0) g_nDiagPage = pg;
		}
		if (id + 3 < args.size())
		{
			const std::string s = RIPExt::WStringToUtf8(args[id + 3]);
			strncpy_s(g_szDiagBmp, sizeof(g_szDiagBmp), s.c_str(), _TRUNCATE);
		}
		int rcd = DiagSeparation(strPdf.c_str(), fSd);
		printf("\n");
		fflush(stdout);
		return rcd;
	}

	if (bCfgTest)
	{
		int rcc = RunConfigSelfTest();
		printf("\n");
		fflush(stdout);
		return rcc;
	}

	if (bSepList)
	{
		std::string strPdf;
		size_t i2 = 0;
		for (size_t i = 0; i < args.size(); ++i)
			if (args[i] == L"--seplist") { i2 = i + 1; break; }
		if (i2 < args.size()) strPdf = RIPExt::WStringToUtf8(args[i2]);
		else
		{
			std::string exeDir = RIPExt::WStringToUtf8(szExePath ? szExePath : L"");
			size_t slash = exeDir.find_last_of("\\/");
			if (slash != std::string::npos) exeDir = exeDir.substr(0, slash);
			strPdf = RIPExt::JoinPath(exeDir, "spot_test.pdf");
		}
		int rc2 = DumpPageSeparations(strPdf.c_str());
		printf("\n");
		fflush(stdout);
		return rc2;
	}

	// 页级并行基准
	if (bPBench)
	{
		size_t ip = 0;
		for (size_t i = 0; i < args.size(); ++i)
			if (args[i] == L"--pbench") { ip = i + 1; break; }

		std::string strPdfB, strOutB;
		int nDpiB = 600, nBitB = 2;
		bool bPackedB = false, bSpotB = false;
		int  nThreadsB = 0;

		if (ip < args.size()) strPdfB = RIPExt::WStringToUtf8(args[ip]);
		if (ip + 1 < args.size()) strOutB = RIPExt::WStringToUtf8(args[ip + 1]);
		if (ip + 2 < args.size()) nDpiB = _wtoi(args[ip + 2].c_str());
		if (ip + 3 < args.size()) nBitB = _wtoi(args[ip + 3].c_str());
		if (ip + 4 < args.size() && args[ip + 4] == L"spot") bSpotB = true;

		bool bNoAAB = false; int nNoAAStrB = 50;
		int nTc = 0, nTm = 0, nTy = 0, nTkB = 0;
		for (size_t i = 0; i < args.size(); ++i)
		{
			if (args[i] == L"--packed") bPackedB = true;
			else if (args[i] == L"--prn") g_nPrnVer = 1;
			else if (args[i] == L"--prn2") g_nPrnVer = 2;
			else if (args[i] == L"--preview") g_bPrev = true;
			else if (args[i].rfind(L"--threads=", 0) == 0) nThreadsB = _wtoi(args[i].c_str() + 10);
			else if (args[i].rfind(L"--outroot=", 0) == 0)
				g_strOutRoot = RIPExt::WStringToUtf8(args[i].c_str() + 10);
			else if (args[i] == L"--fs") g_nDitherMode = 2;
			else if (args[i] == L"--bayer") g_nDitherMode = 1;
			else if (args[i] == L"--thresh") g_nDitherMode = 0;
			else if (args[i] == L"--noserp") g_bFSSerp = false;
			else if (args[i] == L"--noaa") bNoAAB = true;
			else if (args[i].rfind(L"--noaa=", 0) == 0)
			{
				bNoAAB = true;
				nNoAAStrB = _wtoi(args[i].c_str() + 7);
				if (nNoAAStrB < 0) nNoAAStrB = 0;
				if (nNoAAStrB > 100) nNoAAStrB = 100;
			}
			else if (args[i].rfind(L"--thrc=", 0) == 0) nTc = _wtoi(args[i].c_str() + 7);
			else if (args[i].rfind(L"--thrm=", 0) == 0) nTm = _wtoi(args[i].c_str() + 7);
			else if (args[i].rfind(L"--thry=", 0) == 0) nTy = _wtoi(args[i].c_str() + 7);
			else if (args[i].rfind(L"--thrk=", 0) == 0) nTkB = _wtoi(args[i].c_str() + 7);
		}

		if (strPdfB.empty())
		{
			std::string exeDir = RIPExt::WStringToUtf8(szExePath ? szExePath : L"");
			size_t slash = exeDir.find_last_of("\\/");
			if (slash != std::string::npos) exeDir = exeDir.substr(0, slash);
			strPdfB = RIPExt::JoinPath(exeDir, "test.pdf");
		}
		if (nDpiB <= 0) nDpiB = 600;
		if (nBitB != 1) nBitB = 2;   // 基准默认 2bit

		int rcb = RunPageBench(strPdfB, strOutB, nDpiB, nBitB, bPackedB, bSpotB, nThreadsB,
							   bNoAAB, nNoAAStrB, nTc, nTm, nTy, nTkB, g_bPrev);
		printf("\n");
		fflush(stdout);
		return rcb;
	}

	size_t idx = 0;
	for (size_t i = 0; i < args.size(); ++i)
		if (args[i] == L"--riptest") { idx = i + 1; break; }

	std::string strPdf, strOut;
	int nDpi = 300, nBit = 1;
	bool bSpot = false;
	bool bPacked = false;
	bool bGrayK = true;
	int nGrayTol = 48;
	bool bNoAA = false;
	int nNoAAStr = 50;

	if (idx < args.size()) strPdf = RIPExt::WStringToUtf8(args[idx]);
	if (idx + 1 < args.size()) strOut = RIPExt::WStringToUtf8(args[idx + 1]);
	if (idx + 2 < args.size()) nDpi = _wtoi(args[idx + 2].c_str());
	if (idx + 3 < args.size()) nBit = _wtoi(args[idx + 3].c_str());
	if (idx + 4 < args.size() && args[idx + 4] == L"spot") bSpot = true;

	// 开关: 任何位置出现 --packed 都走原生打包 TIFF
	for (size_t i = 0; i < args.size(); ++i)
		if (args[i] == L"--packed") bPacked = true;

	// --fs(默认) / --bayer / --thresh / --noserp
	for (size_t i = 0; i < args.size(); ++i)
	{
		if (args[i] == L"--fs") g_nDitherMode = 2;
		else if (args[i] == L"--bayer") g_nDitherMode = 1;
		else if (args[i] == L"--thresh") g_nDitherMode = 0;
		else if (args[i] == L"--noserp") g_bFSSerp = false;
	}

	// --nograyk / --graytol=N / --noaa[=N] / --prn|--prn2
	for (size_t i = 0; i < args.size(); ++i)
	{
		if (args[i] == L"--nograyk")
		{
			bGrayK = false;
		}
		else if (args[i].rfind(L"--graytol=", 0) == 0)
		{
			nGrayTol = _wtoi(args[i].c_str() + 10);
			if (nGrayTol < 0) nGrayTol = 0;
			if (nGrayTol > 255) nGrayTol = 255;
		}
		else if (args[i] == L"--prn")
		{
			g_nPrnVer = 1;
		}
		else if (args[i] == L"--prn2")
		{
			g_nPrnVer = 2;
		}
		else if (args[i].rfind(L"--outroot=", 0) == 0)
		{
			g_strOutRoot = RIPExt::WStringToUtf8(args[i].c_str() + 10);
		}
		else if (args[i] == L"--noaa")
		{
			bNoAA = true;
		}
		else if (args[i].rfind(L"--noaa=", 0) == 0)
		{
			bNoAA = true;
			nNoAAStr = _wtoi(args[i].c_str() + 7);
			if (nNoAAStr < 0) nNoAAStr = 0;
			if (nNoAAStr > 100) nNoAAStr = 100;
		}
	}

	// 未指定 PDF 时用 exe 同目录的 test.pdf
	if (strPdf.empty())
	{
		std::string exeDir = RIPExt::WStringToUtf8(szExePath ? szExePath : L"");
		size_t slash = exeDir.find_last_of("\\/");
		if (slash != std::string::npos) exeDir = exeDir.substr(0, slash);
		strPdf = RIPExt::JoinPath(exeDir, "test.pdf");
	}
	if (nDpi <= 0) nDpi = 300;
	if (nBit != 2) nBit = 1;

		std::string strDump;
		for (size_t i = 0; i < args.size(); ++i)
			if (args[i].rfind(L"--dump=", 0) == 0)
				strDump = RIPExt::WStringToUtf8(args[i].c_str() + 7);
	int rc = RunRipSelfTest(strPdf.c_str(), strOut.c_str(), nDpi, nBit, bSpot, bPacked,
	                        bGrayK, nGrayTol, bNoAA, nNoAAStr, strDump);

	printf("\n");
	fflush(stdout);
	return rc;
}
