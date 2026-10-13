#include "stdafx.h"
#include "RIPipeline.h"

#include <windows.h>
#include <mutex>
#include <algorithm>
#include <fstream>
#include <cstdio>      // FILE / _wfopen / fwrite(PRN 落盘)
#include <cstring>     // memcpy
#include <process.h>   // _beginthreadex / _endthreadex（RIPPool 线程池）

// ============================================================================
// 路径 / 文件小工具
// ============================================================================
namespace RIPExt
{
	std::string WStringToUtf8(const std::wstring& w)
	{
		if (w.empty()) return std::string();
		int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n, 0);
		::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
		return s;
	}

	std::wstring Utf8ToWString(const std::string& s)
	{
		if (s.empty()) return std::wstring();
		int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
		std::wstring w(n, 0);
		::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
		return w;
	}

	bool FileExistsA(const std::string& path)
	{
		// 路径是 UTF-8，必须转宽字符再查，否则中文路径会被误判为不存在
		if (path.empty()) return false;
		const DWORD attr = ::GetFileAttributesW(Utf8ToWString(path).c_str());
		return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	std::string JoinPath(const std::string& a, const std::string& b)
	{
		if (a.empty()) return b;
		std::string r = a;
		if (r.back() != '\\' && r.back() != '/') r += '\\';
		return r + b;
	}

	std::string FindDefaultCMYKICC(const std::string& appDir)
	{
		// ISO 9006 印刷 CMYK 曲线，随程序发布
		const char* kCandidates[] = {
			"900600_sml_20241004.icc",
			"CMYK.icc",
			"default_cmyk.icc",
			"USWebCoatedSWOP.icc",
			"CoatedFOGRA39.icc",
		};
		for (const char* name : kCandidates)
		{
			std::string p = JoinPath(appDir, name);
			if (FileExistsA(p)) return p;
		}
		return std::string();
	}

	std::string FindDefaultRGBICC(const std::string& appDir)
	{
		const char* kCandidates[] = {
			"sRGB_v4_ICC_preference.icc",
			"sRGB_IEC61966_2.1.icc",
			"sRGB Color Space Profile.icm",
		};
		for (const char* name : kCandidates)
		{
			std::string p = JoinPath(appDir, name);
			if (FileExistsA(p)) return p;
		}
		return std::string();
	}

	// 递归创建目录（UTF-8 路径）。已存在直接返回 true。
	bool EnsureDirExists(const std::string& strDir)
	{
		if (strDir.empty()) return true;

		const std::wstring w = Utf8ToWString(strDir);
		DWORD attr = ::GetFileAttributesW(w.c_str());
		if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
			return true;

		// i 从 3 开始，跳过 "C:\" 这类盘符前缀
		for (size_t i = 3; i < w.size(); ++i)
		{
			if (w[i] == L'\\' || w[i] == L'/')
			{
				const std::wstring sub = w.substr(0, i);
				::CreateDirectoryW(sub.c_str(), nullptr);
			}
		}
		::CreateDirectoryW(w.c_str(), nullptr);

		attr = ::GetFileAttributesW(w.c_str());
		return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}

	std::string MakeOutputDir(const std::string& pdfPath, const std::string& outputRoot)
	{
		// 输出目录: <root 或 pdfDir>\RIP_Out\<fileStem>
		std::string dir, fileStem;
		size_t slash = pdfPath.find_last_of("\\/");
		if (slash == std::string::npos) { dir = "."; fileStem = pdfPath; }
		else { dir = pdfPath.substr(0, slash); fileStem = pdfPath.substr(slash + 1); }

		size_t dot = fileStem.find_last_of('.');
		if (dot != std::string::npos) fileStem = fileStem.substr(0, dot);
		if (fileStem.empty()) fileStem = "output";

		// 指定了输出根目录就用它(大作业输出几十 GB, 常常要挪到数据盘)
		const std::string base = outputRoot.empty() ? dir : outputRoot;
		std::string out = JoinPath(JoinPath(base, "RIP_Out"), fileStem);

		EnsureDirExists(out);
		return out;
	}

	std::string SanitizeInkName(const std::string& strInk)
	{
		std::string r = strInk;
		for (size_t i = 0; i < r.size(); ++i)
		{
			const unsigned char c = (unsigned char)r[i];
			if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
				c == '"' || c == '<' || c == '>' || c == '|' || c < 0x20)
				r[i] = '_';
		}
		while (!r.empty() && (r.back() == ' ' || r.back() == '.'))
			r.pop_back();
		if (r.empty()) r = "ink";
		return r;
	}

	std::string PlaneFileName(const std::string& pdfPath, int nPage, const std::string& strInk)
	{
		std::string stem = pdfPath;
		size_t slash = stem.find_last_of("\\/");
		if (slash != std::string::npos) stem = stem.substr(slash + 1);
		size_t dot = stem.find_last_of('.');
		if (dot != std::string::npos) stem = stem.substr(0, dot);

		char buf[64];
		sprintf(buf, "_p%04d_", nPage);
		return stem + buf + SanitizeInkName(strInk) + ".tif";
	}
}

// ============================================================================
// MuPDF 上下文: 每个 RIPipeline 实例一份（原来是全局共用一份）
//
//   fz_context 内部有大量可变状态（对象表、缓存、异常栈），不是线程安全的。
//   多线程并行 RIP 时共用一份会互相踩内存（偶发崩溃 / 渲染错乱），
//   所以改成每实例一份，配套约定就是: 一个线程只用自己的那份 RIPipeline。
// ============================================================================
namespace
{
	// 按宽字符把整个文件读进内存（路径为 UTF-8）
	bool ReadFileToMemUtf8(const std::string& strPath, std::vector<unsigned char>& out)
	{
		out.clear();
		if (strPath.empty()) return false;
		const std::wstring w = RIPExt::Utf8ToWString(strPath);
		std::ifstream f(w.c_str(), std::ios::binary | std::ios::ate);
		if (!f.is_open()) return false;
		const std::streamoff n = f.tellg();
		if (n <= 0) { f.close(); return false; }
		f.seekg(0, std::ios::beg);
		out.resize((size_t)n);
		f.read((char*)out.data(), n);
		const bool ok = (f.gcount() == n);
		f.close();
		if (!ok) out.clear();
		return ok;
	}
}

void* RIPipeline::EnsureContext()
{
	if (m_pCtx)
		return m_pCtx;

	// 多线程并行时每个线程一份 context, 各自的图像缓存会累加,
	// 所以线程池会把 m_nStoreBytes 调小(见 RIPPool::kStoreBytesMultiThread)
	fz_context* ctx = fz_new_context(nullptr, nullptr,
									 m_nStoreBytes ? (size_t)m_nStoreBytes : (size_t)FZ_STORE_DEFAULT);
	if (!ctx)
	{
		m_strErr = "MuPDF 初始化失败(无法创建 context)";
		return nullptr;
	}
	fz_register_document_handlers(ctx);
	m_pCtx = ctx;
	return m_pCtx;
}

// ============================================================================
// 构造 / 析构
// ============================================================================
RIPipeline::RIPipeline()
{
}

RIPipeline::~RIPipeline()
{
	ReleaseTransformCache();

	// MuPDF 上下文是实例私有的, 必须随实例一起释放
	if (m_pCtx)
	{
		fz_drop_context((fz_context*)m_pCtx);
		m_pCtx = nullptr;
	}
}

void RIPipeline::ReleaseTransformCache()
{
	if (m_pTransform)
	{
		cmsDeleteTransform((cmsHTRANSFORM)m_pTransform);
		m_pTransform = nullptr;
	}
	m_strCurCmykICC.clear();
	m_vecInProfileBuf.clear();
	m_vecOutProfileBuf.clear();
}

// ============================================================================
// lcms RGB -> CMYK 变换（带缓存）
// ============================================================================
void* RIPipeline::GetRgb2CmykTransform(const RIPParams& params)
{
	std::string cmykIcc = params.m_strCMYKICC;
	if (cmykIcc.empty())
		cmykIcc = RIPExt::FindDefaultCMYKICC(m_strAppDir);

	// 同一支 ICC 复用变换句柄
	if (m_pTransform && m_strCurCmykICC == cmykIcc)
		return m_pTransform;

	ReleaseTransformCache();

	cmsHPROFILE hIn = nullptr;
	cmsHPROFILE hOut = nullptr;

	std::string rgbIcc = params.m_strInputICC;
	if (rgbIcc.empty())
		rgbIcc = RIPExt::FindDefaultRGBICC(m_strAppDir);

	// 路径是 UTF-8，lcms 的 *_from_file 只认 ANSI，统一改走内存方式
	if (!rgbIcc.empty())
	{
		if (ReadFileToMemUtf8(rgbIcc, m_vecInProfileBuf))
			hIn = cmsOpenProfileFromMem(m_vecInProfileBuf.data(),
											(cmsUInt32Number)m_vecInProfileBuf.size());
	}
	if (!hIn)
		hIn = cmsCreate_sRGBProfile();

	if (!cmykIcc.empty())
	{
		if (ReadFileToMemUtf8(cmykIcc, m_vecOutProfileBuf))
			hOut = cmsOpenProfileFromMem(m_vecOutProfileBuf.data(),
										 (cmsUInt32Number)m_vecOutProfileBuf.size());
		if (!hOut)
		{
			// 明确给了曲线却读不出来，属于配置错误，报错而不是静默降级成默认分色
			if (hIn) cmsCloseProfile(hIn);
			m_vecInProfileBuf.clear();
			m_vecOutProfileBuf.clear();
			m_strErr = "无法打开 CMYK ICC 文件（路径含中文或文件损坏）";
			return nullptr;
		}
	}
	else
	{
		// 未提供 CMYK 曲线时退回 NULL profile，走 lcms 默认分色（仅用于验证流程）
		hOut = cmsCreateNULLProfile();
	}

	// pixmap 固定按 BGRA 处理，直接对应 TYPE_BGRA_8
	m_pTransform = cmsCreateTransform(
		hIn, TYPE_BGRA_8,
		hOut, TYPE_CMYK_8,
		INTENT_PERCEPTUAL,
		cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_HIGHRESPRECALC);

	cmsCloseProfile(hIn);
	cmsCloseProfile(hOut);

	if (!m_pTransform)
	{
		m_strErr = "cmsCreateTransform 失败";
		return nullptr;
	}

	m_strCurCmykICC = cmykIcc;
	return m_pTransform;
}

// ============================================================================
// 行带(band)流式 RIP
//
//   600DPI 的超大页(例如 1670x740mm)单页有近 7 亿像素。整页同时驻留
//   渲染位图 + BGRA + CMYK + 4 个灰度平面 + 预览约 21 B/像素(约 14GB),
//   单线程也放不下 —— 必须让峰值内存与页面积解耦, 按行带滚动处理。
//
//   但输出必须与整页处理逐字节一致, 所以:
//     a. 带高 kBandRows 取 2 的幂, 带起点也是 2 的幂, 保证 FS 蛇形扫描的
//        行方向(奇偶)在带边界连续;
//     b. 每条带上下各多渲染 kBandPad 行作为上下文, 去毛边的 K 邻域
//        (半径 kBandPad)在带内与整页完全一致, 只有真正的页边界才 clamp;
//     c. FS 的误差扩散缓冲跨带保留, 带边界照常把误差扩散到下一带;
//     d. Bayer 有序网屏用整页的全局 y 定位网点格子。
// ============================================================================
namespace
{
	// 4x4 Bayer 有序抖动阈值表: 取值 0..15
	const int kBayer4x4[16] = {
		 0,  8,  2, 10,
		12,  4, 14,  6,
		 3, 11,  1,  9,
		15,  7, 13,  5
	};

	// 每个色版网屏原点的平移, 避免四个色版的网点完全重合
	const int kPhase[5] = { 0, 5, 11, 3, 7 };
	const int kPhaseN = (int)(sizeof(kPhase) / sizeof(kPhase[0]));

	// 行带高度(像素): 必须是 2 的幂(保证蛇形相位与预览降采样块对齐)
	const int kBandRows = 256;
	// 去毛边 K 邻域滤波半径(= 下方 vecKNb 的滤波半径), 带上下各需这么多行上下文
	const int kBandPad = 4;
	// 预览允许的最大像素数: 超出就按整数倍盒式降采样
	// (A4@600DPI 约 3480 万像素, 保持 1:1 不降采样, 与老版本预览一致)
	const double kMaxPreviewPixels = 64.0e6;

	// 取某像素的 4x4 有序抖动阈值(0..15)
	//   注意: 相位只用于平移网点格子(像素位置), 绝不能加到灰阶值上,
	//         否则纯白(灰阶 0)会被抬过阈值而打出本不该有的墨点
	inline int BayerCell(int x, int y, int planeIdx, bool bEnable, bool bRotateY)
	{
		if (!bEnable) return 0;
		// 错位旋转: 奇数列在版面上下错开一行, 减少规则纹理摩尔纹
		const int py = bRotateY ? (y + ((x >> 1) & 1)) : y;
		const int ph = kPhase[planeIdx % kPhaseN];
		const int px = x + (ph & 3);
		const int qy = py + ((ph >> 2) & 3);
		return kBayer4x4[((qy & 3) << 2) | (px & 3)];
	}

	// ------------------------------------------------------------------
	// FS 误差扩散的行状态(双行误差缓冲 + 当前/下一行指针)
	//   跨行带保留: 带结束时 pCur 已经换到「下一行待扩散的误差」,
	//   下一带接着用, 与整页一次跑完完全等价。
	// ------------------------------------------------------------------
	struct FSErrState
	{
		std::vector<int> m_vecErrA;
		std::vector<int> m_vecErrB;
		int* m_pCur = nullptr;   // 指向 data() + 1, 左侧留 1 个 padding 省掉边界判断
		int* m_pNext = nullptr;

		void Init(int w)
		{
			m_vecErrA.assign((size_t)w + 2, 0);
			m_vecErrB.assign((size_t)w + 2, 0);
			m_pCur = m_vecErrA.data() + 1;
			m_pNext = m_vecErrB.data() + 1;
		}
		void Swap() { int* t = m_pCur; m_pCur = m_pNext; m_pNext = t; }
	};

	// 一个色版在整页上的抖动态(位平面 + FS 行状态 + 累计墨量)
	struct BandPlaneState
	{
		RIPPlane			m_plane;
		std::string			m_strName;
		int					m_nThreshold = 0;
		FSErrState			m_fs;
		unsigned __int64	m_nOn = 0;
	};

	// ------------------------------------------------------------------
	// 行带灰阶生成: 带位图 -> (GCR / 去毛边) -> 每色版 8bit 灰阶
	//
	//   src       : 带位图采样(每像素 nBaseComp + nSpotChan 字节), 行距 = w * 通道数
	//   bandH     : 带位图行数(含上下文行), 对应整页第 [nBandY0, nBandY0 + bandH) 行
	//   nOutLocal : 需要输出的第一行在带内的局部行号(>= 上方上下文的行数)
	//   nOutRows  : 需要输出的行数
	//   nPageH    : 整页高度, 邻域滤波在页边界 clamp 时要用(与整页处理一致)
	//   pCmykRows : 可选, 回传输出行在 GCR 之后、去毛边之前的 8bit CMYK,
	//               用于生成预览 —— 预览要连续调, 不能是抖动结果
	// ------------------------------------------------------------------
	bool BuildGrayBand(const unsigned char* src, int w, int bandH,
					   int nBandY0, int nOutLocal, int nOutRows, int nPageH,
					   int nBaseComp, int nSpotChan,
					   const std::vector<std::string>& vecSpotNames,
					   const RIPParams& params, void* hTransformIn,
					   std::vector<RIPGrayPlane>& out,
					   std::vector<unsigned char>* pCmykRows,
					   std::string& strErr,
					   const volatile bool* pbCancel)
	{
		const size_t nBandPix = (size_t)w * bandH;
		const int nChanTotal = nBaseComp + nSpotChan;
		out.clear();

		if (nOutLocal < 0 || nOutRows <= 0 || nOutLocal + nOutRows > bandH)
		{
			strErr = "行带参数错误";
			return false;
		}
		const size_t nOutPix = (size_t)w * nOutRows;

		// ---------- 1. 带位图 -> CMYK 四个色版 ----------
		if (nBaseComp >= 3)
		{
			cmsHTRANSFORM hx = (cmsHTRANSFORM)hTransformIn;
			if (!hx) { strErr = "cmsCreateTransform 失败"; return false; }

			if (pbCancel && *pbCancel) { strErr = "已取消"; return false; }

			// 补 alpha 通道, 凑成 lcms 需要的 BGRA
			std::vector<unsigned char> bgra(nBandPix * 4);
			for (size_t i = 0; i < nBandPix; ++i)
			{
				const unsigned char* p = src + i * nChanTotal;
				unsigned char* q = bgra.data() + i * 4;
				q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = 0xFF;
			}

			std::vector<unsigned char> cmyk(nBandPix * 4, 0);
			cmsDoTransformLineStride(hx,
									 bgra.data(), cmyk.data(),
									 (cmsUInt32Number)w, (cmsUInt32Number)bandH,
									 (cmsUInt32Number)4 * w,
									 (cmsUInt32Number)4 * w,
									 0, 0);

			// ---------- 1b. 灰成分剔除: 中性色只用 K 表达 ----------
			// 灰/中性色经 ICC 后常变成「四色灰」, 四个色版上都有墨且网点位置
			// 互不相同, 叠印后会带明显的杂色毛边。
			// C/M/Y 极差足够小(中性色)时折算成 K: 视觉上更干净, CMY 清零。
			std::vector<unsigned char> vecNeutral;
			if (params.m_bGrayToK)
			{
				const int nTol = params.GrayTol();
				vecNeutral.resize(nBandPix, 0);
				unsigned char* pC = cmyk.data();
				for (size_t i = 0; i < nBandPix; ++i, pC += 4)
				{
					if (pbCancel && ((i & 0xFFFFF) == 0) && *pbCancel)
					{
						strErr = "已取消";
						return false;
					}
					const int c1 = pC[0], m1 = pC[1], y1 = pC[2];
					int mx = c1 > m1 ? c1 : m1; if (y1 > mx) mx = y1;
					int mn = c1 < m1 ? c1 : m1; if (y1 < mn) mn = y1;
					if (mx - mn <= nTol)
					{
						const int k2 = pC[3] + mx;   // 补回被清掉的黑量, 保证浅墨不丢
						pC[0] = pC[1] = pC[2] = 0;
						pC[3] = (unsigned char)(k2 > 255 ? 255 : k2);
						vecNeutral[i] = 1;
					}
				}
			}

			// 预览: 输出行的连续调 CMYK(GCR 之后、去毛边之前)
			if (pCmykRows)
			{
				pCmykRows->resize(nOutPix * 4);
				if (nOutPix > 0)
					memcpy(pCmykRows->data(),
						   cmyk.data() + (size_t)nOutLocal * w * 4,
						   nOutPix * 4);
			}

			static const char* kCmykName[4] = { "C", "M", "Y", "K" };
			const int kThr[4] = { params.ThrC(), params.ThrM(), params.ThrY(), params.ThrK() };

			// 去抗锯齿(可选): 只对 K 版、只对中性像素做 S 曲线
			int nAALo = 0, nAAHi = 255;
			if (params.m_bNoAA)
			{
				int half = 128 * (100 - params.NoAAStrength()) / 100;
				if (half < 2) half = 2;
				nAALo = 128 - half;
				nAAHi = 128 + half;
			}

			// 笔画邻域: 先横后纵的可分离最大滤波(半径 kBandPad), 判断某像素
			// 是否紧挨着实心笔画(K >= nAAHi)。
			//
			// 只有「中性 + 紧贴笔画」才做完整 S 曲线(文字线条的抗锯齿带);
			// 「中性 + 远离笔画」只清掉近白噪点; 其余保持原墨。
			// 曾经对全部中性像素做 S 曲线, 结果把米色圆底/浅灰高光/浅粉装饰
			// (K 只有 10~60)整块清零 —— 图标、浅色内容会整块消失。
			//
			// 纵向滤波用整页全局行号 clamp: 带上下各多渲染 kBandPad 行上下文,
			// 所以带内输出行拿到的邻域与整页完全一致(只有页边界才 clamp)。
			std::vector<unsigned char> vecKNb;
			if (nAALo > 0 || nAAHi < 255)
			{
				const int kR = kBandPad;
				std::vector<unsigned char> vecRowMax(nBandPix, 0);
				for (int y = 0; y < bandH; ++y)
				{
					const unsigned char* pSrc = cmyk.data() + (size_t)y * w * 4 + 3;
					unsigned char* pDst = vecRowMax.data() + (size_t)y * w;
					for (int x = 0; x < w; ++x)
					{
						const int x0 = x - kR > 0 ? x - kR : 0;
						const int x1 = x + kR < w - 1 ? x + kR : w - 1;
						unsigned char mx = 0;
						for (int t = x0; t <= x1; ++t)
							if (pSrc[(size_t)t * 4] > mx) mx = pSrc[(size_t)t * 4];
						pDst[x] = mx;
					}
				}

				vecKNb.assign(nBandPix, 0);
				for (int y = 0; y < bandH; ++y)
				{
					const int gy = nBandY0 + y;                    // 整页全局行号
					const int gy0 = gy - kR > 0 ? gy - kR : 0;
					const int gy1 = gy + kR < nPageH - 1 ? gy + kR : nPageH - 1;
					int ly0 = gy0 - nBandY0; if (ly0 < 0) ly0 = 0;
					int ly1 = gy1 - nBandY0; if (ly1 > bandH - 1) ly1 = bandH - 1;
					for (int x = 0; x < w; ++x)
					{
						unsigned char mx = 0;
						for (int t = ly0; t <= ly1; ++t)
						{
							const unsigned char v = vecRowMax[(size_t)t * w + x];
							if (v > mx) mx = v;
						}
						vecKNb[(size_t)y * w + x] = mx;
					}
				}
			}

			// ---------- 1c. 输出行 -> 四个灰阶平面 ----------
			for (int c = 0; c < 4; ++c)
			{
				RIPGrayPlane pl;
				pl.m_strName = kCmykName[c];
				pl.m_nThreshold = kThr[c];
				pl.m_vecGray.resize(nOutPix);

				const unsigned char* pIn = cmyk.data() + (size_t)nOutLocal * w * 4 + c;
				if ((nAALo > 0 || nAAHi < 255) && c == 3)
				{
					const unsigned char* pNb = vecKNb.data() + (size_t)nOutLocal * w;
					const unsigned char* pNt = vecNeutral.empty()
						? nullptr : (vecNeutral.data() + (size_t)nOutLocal * w);
					for (size_t i = 0; i < nOutPix; ++i)
					{
						int v = pIn[i * 4];
						if (pNt && pNt[i])
						{
							if (pNb[i] >= nAAHi)
							{
								// 抗锯齿带上的像素: 完整 S 曲线, 浅墨归 0、浓墨归 255
								if (v <= nAALo) v = 0;
								else if (v >= nAAHi) v = 255;
								else v = (int)(((v - nAALo) * 255) / (nAAHi - nAALo));
							}
							else if (v <= nAALo / 4)
							{
								// 远离笔画的近白噪点(白底经 ICC 也常有 K = 3~10), 直接清零
								v = 0;
							}
						}
						pl.m_vecGray[i] = (unsigned char)v;
					}
				}
				else
				{
					for (size_t i = 0; i < nOutPix; ++i)
						pl.m_vecGray[i] = pIn[i * 4];
				}
				out.push_back(std::move(pl));
			}
		}

		// ---------- 2. 专色通道 -> 每个专色一个独立灰阶平面 ----------
		for (int c = 0; c < nSpotChan; ++c)
		{
			RIPGrayPlane pl;
			if (c < (int)vecSpotNames.size() && !vecSpotNames[c].empty())
			{
				pl.m_strName = vecSpotNames[c];
			}
			else
			{
				char buf[32];
				sprintf(buf, "Spot%d", c + 1);
				pl.m_strName = buf;
			}

			pl.m_nThreshold = 0;   // 专色不设阈值过滤: > 0 即出墨
			pl.m_vecGray.resize(nOutPix);

			const int chIdx = nBaseComp + c;
			const unsigned char* pIn = src + (size_t)nOutLocal * w * nChanTotal + chIdx;
			for (size_t i = 0; i < nOutPix; ++i)
				pl.m_vecGray[i] = pIn[i * nChanTotal];

			out.push_back(std::move(pl));
		}

		return true;
	}

	// ------------------------------------------------------------------
	// 行带抖动: 把 vecGray 的 nRows 行(整页起点 nGlobalY0)按当前抖动模式
	// 写入各色版的整页位平面, 并把墨量累加到状态里。
	//   FS 模式: 误差缓冲存在 state 里, 跨带继续扩散
	//   FS 权重: 右 7/16, 左下 3/16, 下 5/16, 右下 1/16
	// ------------------------------------------------------------------
	// ========================================================================
	// 墨点统计: 把一版位图的出墨像素按 8 连通聚成一个个「点」, 按等效直径分档
	//
	//   1bit: 出墨 = 1
	//   2bit: 出墨 = 值 != 0 (1/2/3 都算一个点, 只是墨量不同)
	//
	// 算法 = 游程(run) + 并查集单遍标记:
	//   - 每行只提取「出墨游程」, 全 0 的字节直接跳过, 上亿像素的页也很快
	//   - 相邻两行游程的 x 区间有交叠(8 连通, 端点允许错开 1 像素)就合并
	//   - 一个连通域不再延续到下一行时立刻结算面积并回收槽位,
	//     所以槽位池只跟「单行游程数」有关, 跟页高无关(不会随页高膨胀)
	// ========================================================================
	struct DotSlot
	{
		int       m_nParent = -1;   // 父槽位(仅 m_nState==2 时有效)
		long long m_nArea = 0;      // 仅根槽位有效: 该连通域的像素数
		int       m_nState = 0;     // 0=空闲 1=根 2=已并入别的根
		int       m_nGen = 0;       // 最后一次被引用的行号+1
	};

	struct DotRun
	{
		int m_nX0 = 0;
		int m_nX1 = 0;
		int m_nId = -1;             // 所属连通域槽位
	};

	void CollectDotStatPlane(const RIPPlane& plane, int nDpi,
							 int nSmallUM, int nMidUM, RIPDotStat& out)
	{
		out.m_nDotsSmall = out.m_nDotsMid = out.m_nDotsBig = 0;
		out.m_nDots = out.m_nInkPixels = out.m_nMaxArea = 0;

		const int w = plane.m_nWidth;
		const int h = plane.m_nHeight;
		const int stride = plane.m_nStride;
		const unsigned char* pBits = plane.m_vecBits.data();
		if (w <= 0 || h <= 0 || stride <= 0 || !pBits) return;

		const int bit = (plane.m_nBit == 2) ? 2 : 1;
		if (nDpi < 1) nDpi = 1;
		if (nSmallUM <= 0) nSmallUM = 100;
		if (nMidUM <= nSmallUM) nMidUM = nSmallUM * 2;

		// 分档面积阈值: 等效直径 d(um) 的圆面积(像素)
		//   area = PI/4 * (d * dpi / 25400)^2
		const double fPxPerUM = (double)nDpi / 25400.0;
		const double fPi4 = 3.14159265358979323846 / 4.0;
		double fAreaSmall = fPi4 * ((double)nSmallUM * fPxPerUM) * ((double)nSmallUM * fPxPerUM);
		double fAreaMid = fPi4 * ((double)nMidUM * fPxPerUM) * ((double)nMidUM * fPxPerUM);
		if (fAreaSmall < 1.0) fAreaSmall = 1.0;
		if (fAreaMid < fAreaSmall + 1.0) fAreaMid = fAreaSmall + 1.0;

		std::vector<DotSlot> vecSlot;
		std::vector<int>     vecFree;
		std::vector<int>     vecFreePending2;  // 上一行废弃的槽位: 隔一行才允许复用,
		                                      // 因为上一行的旧 id 还会在下一行被引用
		std::vector<int>     vecFreePending;   // 本行已废弃的槽位: 延迟到行末才放回,
		                                      // 否则同一行内被 NewSlot 复用, 会让上一行
		                                      // 窗口外的旧 id 指到别的连通域上去

		std::vector<DotRun>  vecPrev, vecCur;
		vecSlot.reserve(4096);

		// ---- 槽位池 ----
		auto NewSlot = [&]() -> int
		{
			int id;
			if (!vecFree.empty()) { id = vecFree.back(); vecFree.pop_back(); }
			else { id = (int)vecSlot.size(); vecSlot.push_back(DotSlot()); }
			DotSlot& s = vecSlot[id];
			s.m_nParent = -1; s.m_nArea = 0; s.m_nState = 1; s.m_nGen = 0;
			return id;
		};
		auto FreeSlot = [&](int id)
		{
			if (id < 0) return;
			vecSlot[id].m_nParent = -1;
			vecSlot[id].m_nArea = 0;
			vecSlot[id].m_nState = 0;
			vecFree.push_back(id);
		};
		// 同 FreeSlot, 但槽位只进「本行待回收」列表: 本行内绝不复用
		auto RetireSlot = [&](int id)
		{
			if (id < 0) return;
			vecSlot[id].m_nParent = -1;
			vecSlot[id].m_nArea = 0;
			vecSlot[id].m_nState = 3;   // 已结算: FindSlot 到这里返回 -1
			vecFreePending.push_back(id);
		};
		auto FindSlot = [&](int id) -> int
		{
			while (id >= 0 && vecSlot[id].m_nState == 2) id = vecSlot[id].m_nParent;
			if (id >= 0 && vecSlot[id].m_nState != 1) return -1;   // 空闲 / 已废弃
			return id;
		};
		// 合并两个连通域(都必须是活着的), 返回合并后的根
		auto MergeSlot = [&](int a, int b) -> int
		{
			a = FindSlot(a); b = FindSlot(b);
			if (a < 0) return b;
			if (b < 0) return a;
			if (a == b) return a;
			if (vecSlot[a].m_nArea < vecSlot[b].m_nArea) { int t = a; a = b; b = t; }
			vecSlot[a].m_nArea += vecSlot[b].m_nArea;
			// b 立刻回收: 合并窗口内所有指向 b 的引用马上会被统一改写成 a,
			// 窗口之外(上一行的相邻游程)即便还写着 b, 它在本次结算里 Find 到 -1 会直接跳过,
			// 而 b 的像素数已经并入 a, 不会丢。
			// 只挂链, 不立即回收: 上一行里可能还有(扫描窗口之外的)旧 id 指向 b,
			// 那些位置要靠 FindSlot 顺着 parent 走到 a。立即回收会让它们
			// Find 到 -1, 于是与上一行明明相连的一段被当成新连通域 —— 点会被拆开。
			vecSlot[b].m_nArea = 0;
			vecSlot[b].m_nState = 2;          // 已并入 a
			vecSlot[b].m_nParent = a;
			vecFreePending.push_back(b);      // 行末真正回收

			return a;
		};

		int nGen = 0;
		for (int y = 0; y < h; ++y)
		{
			++nGen;
			const unsigned char* pRow = pBits + (size_t)y * stride;

			// ---- 1. 提取本行的出墨游程 ----
			vecCur.clear();
			int nPrevIdx = -1;      // 本行上一段的游程下标(用于跨字节拼接)
			if (bit == 1)
			{
				for (int bx = 0; bx < stride; ++bx)
				{
					unsigned int bv = pRow[bx];
					if (!bv) continue;
					const int xBase = bx << 3;
					int x = 0;
					while (x < 8)
					{
						if (bv & (0x80u >> x))
						{
							const int x0 = xBase + x;
							while (x < 8 && (bv & (0x80u >> x))) ++x;
							int x1 = xBase + x - 1;
							if (x1 >= w) x1 = w - 1;
							if (x0 < w)
							{
								// 跨字节延续: 同一行的出墨段不能被字节边界切成两截
								if (nPrevIdx >= 0 && vecCur[nPrevIdx].m_nX1 + 1 == x0)
									vecCur[nPrevIdx].m_nX1 = x1;
								else
								{
									DotRun r; r.m_nX0 = x0; r.m_nX1 = x1; r.m_nId = -1;
									vecCur.push_back(r);
									nPrevIdx = (int)vecCur.size() - 1;
								}
							}
						}
						else ++x;
					}
				}
			}
			else
			{
				for (int bx = 0; bx < stride; ++bx)
				{
					unsigned int bv = pRow[bx];
					if (!bv) continue;
					const int xBase = bx << 2;
					int x = 0;
					while (x < 4)
					{
						if ((bv >> (6 - (x << 1))) & 3u)
						{
							const int x0 = xBase + x;
							while (x < 4 && (((bv >> (6 - (x << 1))) & 3u) != 0)) ++x;
							int x1 = xBase + x - 1;
							if (x1 >= w) x1 = w - 1;
							if (x0 < w)
							{
								// 跨字节延续: 同一行的出墨段不能被字节边界切成两截
								if (nPrevIdx >= 0 && vecCur[nPrevIdx].m_nX1 + 1 == x0)
									vecCur[nPrevIdx].m_nX1 = x1;
								else
								{
									DotRun r; r.m_nX0 = x0; r.m_nX1 = x1; r.m_nId = -1;
									vecCur.push_back(r);
									nPrevIdx = (int)vecCur.size() - 1;
								}
							}
						}
						else ++x;
					}
				}
			}

			const int nc = (int)vecCur.size();
			for (int c = 0; c < nc; ++c)
				out.m_nInkPixels += (long long)(vecCur[c].m_nX1 - vecCur[c].m_nX0 + 1);

			// ---- 2. 与上一行游程做 8 连通合并 ----
			if (nc > 0)
			{
				const int np = (int)vecPrev.size();
				int i = 0;
				for (int c = 0; c < nc; ++c)
				{
					DotRun& r = vecCur[c];
					// 游标只前进: 上一行里右端够不到本游程的全跳过
					while (i < np && vecPrev[i].m_nX1 + 1 < r.m_nX0) ++i;

					const int iStart = i;
					int j = i;
					int nRoot = -1;
					while (j < np && vecPrev[j].m_nX0 <= r.m_nX1 + 1)
					{
						const int rr = FindSlot(vecPrev[j].m_nId);
						if (rr >= 0)
							nRoot = (nRoot < 0) ? rr : MergeSlot(nRoot, rr);
						++j;
					}

					// 命中窗口内的引用统一改写成最终根:
					// 槽位回收后若还留着旧 id, 会指到别的连通域上去
					if (nRoot >= 0)
					{
						for (int jj = iStart; jj < j; ++jj)
							vecPrev[jj].m_nId = nRoot;
					}
					else
					{
						nRoot = NewSlot();      // 与上一行不相连 -> 新连通域
					}

					r.m_nId = nRoot;
					vecSlot[nRoot].m_nArea += (long long)(r.m_nX1 - r.m_nX0 + 1);

					// j-1 那个游程还可能和下一个 cur 游程重叠, 游标停在它上面
					if (j > iStart) i = j - 1;
				}
			}

			// 本行引用到的连通域标记成「本行有效」
			for (int c = 0; c < nc; ++c)
			{
				const int rr = FindSlot(vecCur[c].m_nId);
				if (rr >= 0) vecSlot[rr].m_nGen = nGen;
			}

			// ---- 3. 上一行里本行没引用到的连通域 -> 已结束, 结算 ----
			for (size_t j = 0; j < vecPrev.size(); ++j)
			{
				const int rr = FindSlot(vecPrev[j].m_nId);
				if (rr < 0) continue;
				if (vecSlot[rr].m_nGen == nGen) continue;   // 本行还在延续
				vecSlot[rr].m_nGen = nGen;                  // 标记已结算, 防重复

				const long long area = vecSlot[rr].m_nArea;
				if (area > 0)
				{
					if (area > out.m_nMaxArea) out.m_nMaxArea = area;
					const double fArea = (double)area;
					if (fArea < fAreaSmall)      ++out.m_nDotsSmall;
					else if (fArea < fAreaMid)   ++out.m_nDotsMid;
					else                         ++out.m_nDotsBig;
					++out.m_nDots;
				}
				RetireSlot(rr);
			}

			// 行末统一回收本行废弃的槽位
			if (!vecFreePending.empty())
			{
				// 上一行(隔了一整行)释放的槽位, 现在才允许复用
				for (size_t f = 0; f < vecFreePending2.size(); ++f)
				{
					const int fid = vecFreePending2[f];
					vecSlot[fid].m_nState = 0;
					vecSlot[fid].m_nParent = -1;
					vecFree.push_back(fid);
				}
				vecFreePending2.clear();
				vecFreePending2.swap(vecFreePending);
				vecFreePending.clear();
			}

			vecPrev.swap(vecCur);
		}

		// ---- 4. 最后一行之后还在延续的连通域全部结算 ----
		//    ++nGen: 换一个"代", 这样所有存活连通域都满足「本代未被引用」,
		//    再用同一个 m_nGen 标记做去重(同一个根在 vecPrev 里可能出现多次)
		++nGen;
		for (size_t j = 0; j < vecPrev.size(); ++j)
		{
			const int rr = FindSlot(vecPrev[j].m_nId);
			if (rr < 0) continue;
			if (vecSlot[rr].m_nGen == nGen) continue;
			vecSlot[rr].m_nGen = nGen;

			const long long area = vecSlot[rr].m_nArea;
			if (area > 0)
			{
				if (area > out.m_nMaxArea) out.m_nMaxArea = area;
				const double fArea = (double)area;
				if (fArea < fAreaSmall)      ++out.m_nDotsSmall;
				else if (fArea < fAreaMid)   ++out.m_nDotsMid;
				else                         ++out.m_nDotsBig;
				++out.m_nDots;
			}
			FreeSlot(rr);
		}
	}

	bool DitherBandRows(const std::vector<RIPGrayPlane>& vecGray,
						int w, int nRows, int nGlobalY0,
						const RIPParams& params,
						std::vector<BandPlaneState>& vecSt,
						std::string& strErr,
						const volatile bool* pbCancel)
	{
		if (vecGray.size() != vecSt.size())
		{
			strErr = "色版数量不一致";
			return false;
		}

		const int nMode = params.DitherMode();   // 0=纯阈值 1=有序网屏 2=FS

		for (size_t k = 0; k < vecGray.size(); ++k)
		{
			const RIPGrayPlane& gray = vecGray[k];
			BandPlaneState& st = vecSt[k];
			const int thrV = st.m_nThreshold;
			const int maxV = (st.m_plane.m_nBit == 1) ? 1 : 3;
			const int stride = st.m_plane.m_nStride;

			// 阈值上线性拉伸表: 灰阶 <= 阈值一律 0, 保证纯白永不出点
			unsigned char lut[256];
			for (int i = 0; i < 256; ++i)
			{
				int v = (i <= thrV) ? 0 : ((thrV >= 255) ? 255 : ((i - thrV) * 255) / (255 - thrV));
				lut[i] = (unsigned char)(v > 255 ? 255 : v);
			}
			const int nLevel255 = 255 / maxV;   // 满阶对应的灰阶跨度: 1bit->255, 2bit->85

			const unsigned char* pGray = gray.m_vecGray.data();
			unsigned char* pBits = st.m_plane.m_vecBits.data();

			if (nMode == 2)
			{
				FSErrState& fs = st.m_fs;
				int* pCur = fs.m_pCur;
				int* pNext = fs.m_pNext;

				for (int y = 0; y < nRows; ++y)
				{
					const int gy = nGlobalY0 + y;
					if (pbCancel && ((gy & 31) == 0) && *pbCancel)
					{
						strErr = "已取消";
						return false;
					}

					const unsigned char* pSrc = pGray + (size_t)y * w;
					unsigned char* pDst = pBits + (size_t)gy * stride;

					std::fill(pNext - 1, pNext + w + 1, 0);

					const bool bL2R = (!params.m_bFSSerpentine) || ((gy & 1) == 0);

					if (bL2R)
					{
						for (int x = 0; x < w; ++x)
						{
							int v = lut[pSrc[x]] + pCur[x];
							if (v < 0) v = 0; else if (v > 255) v = 255;

							const int q = (maxV == 1) ? ((v >= 128) ? 1 : 0) : ((v * maxV + 128) / 255);
							const int e = v - q * nLevel255;

							if (q)
							{
								if (maxV == 1)
									pDst[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
								else
									pDst[x >> 2] |= (unsigned char)(q << (6 - ((x & 3) << 1)));
								st.m_nOn += (unsigned __int64)q;
							}

							if (e)
							{
								pCur[x + 1] += (e * 7) >> 4;
								pNext[x - 1] += (e * 3) >> 4;
								pNext[x] += (e * 5) >> 4;
								pNext[x + 1] += (e * 1) >> 4;
							}
						}
					}
					else
					{
						// 蛇形反向: 误差权重左右对调
						for (int x = w - 1; x >= 0; --x)
						{
							int v = lut[pSrc[x]] + pCur[x];
							if (v < 0) v = 0; else if (v > 255) v = 255;

							const int q = (maxV == 1) ? ((v >= 128) ? 1 : 0) : ((v * maxV + 128) / 255);
							const int e = v - q * nLevel255;

							if (q)
							{
								if (maxV == 1)
									pDst[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
								else
									pDst[x >> 2] |= (unsigned char)(q << (6 - ((x & 3) << 1)));
								st.m_nOn += (unsigned __int64)q;
							}

							if (e)
							{
								pCur[x - 1] += (e * 7) >> 4;
								pNext[x + 1] += (e * 3) >> 4;
								pNext[x] += (e * 5) >> 4;
								pNext[x - 1] += (e * 1) >> 4;
							}
						}
					}

					int* pTmp = pCur; pCur = pNext; pNext = pTmp;
				}

				fs.m_pCur = pCur;
				fs.m_pNext = pNext;
			}
			else
			{
				const bool bScreen = (nMode == 1);

				for (int y = 0; y < nRows; ++y)
				{
					const int gy = nGlobalY0 + y;
					if (pbCancel && ((gy & 255) == 0) && *pbCancel)
					{
						strErr = "已取消";
						return false;
					}

					const unsigned char* pSrc = pGray + (size_t)y * w;
					unsigned char* pDst = pBits + (size_t)gy * stride;

					for (int x = 0; x < w; ++x)
					{
						const int g = (int)pSrc[x];
						int q = 0;

						// 只有严格大于阈值的像素才可能出墨: 纯白(0)在阈值 0 时也不出墨
						if (g > thrV)
						{
							int level = (thrV >= 255) ? 255 : ((g - thrV) * 255) / (255 - thrV);
							if (level > 255) level = 255;

							if (bScreen)
							{
								// 有序网屏: 灰阶按 maxV*64 分级, 再叠上网点的门限(0..60)
								const int scaled = level * maxV * 64 / 255;
								const int d = BayerCell(x, gy, (int)k, true, params.m_bRotateScreenY) * 4;
								q = (scaled + d) / 64;
							}
							else
							{
								// 节点量化: 按阈值上灰阶四舍五入到档位
								q = (level * maxV + 127) / 255;
							}
							if (q > maxV) q = maxV;
						}

						if (q)
						{
							if (maxV == 1)
								pDst[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
							else
								pDst[x >> 2] |= (unsigned char)(q << (6 - ((x & 3) << 1)));
							st.m_nOn += (unsigned __int64)q;
						}
					}
				}
			}
		}

		return true;
	}

	// 按整页尺寸建立各色版状态(位平面一次性分配, 后续按行带往里填)
	void InitBandPlanes(const std::vector<RIPGrayPlane>& vecGray, int w, int h,
						int bit, std::vector<BandPlaneState>& vecSt)
	{
		const int stride = (w * bit + 7) >> 3;
		vecSt.clear();
		vecSt.resize(vecGray.size());
		for (size_t k = 0; k < vecGray.size(); ++k)
		{
			BandPlaneState& st = vecSt[k];
			st.m_strName = vecGray[k].m_strName;
			st.m_nThreshold = vecGray[k].m_nThreshold;
			st.m_nOn = 0;

			RIPPlane& pl = st.m_plane;
			pl.m_nWidth = w;
			pl.m_nHeight = h;
			pl.m_nBit = bit;
			pl.m_nStride = stride;
			pl.m_nBitsPerLine = stride << 3;
			pl.m_vecBits.assign((size_t)stride * h, 0);

			st.m_fs.Init(w);
		}
	}
}

// ============================================================================
// 查询总页数（失败返回 0）
// ============================================================================
int RIPipeline::CountPages(const std::string& strPdfPath)
{
	m_strErr.clear();

	if (!RIPExt::FileExistsA(strPdfPath))
	{
		m_strErr = "PDF 文件不存在";
		return 0;
	}

	fz_context* ctx = (fz_context*)EnsureContext();
	if (!ctx) { m_strErr = "MuPDF 初始化失败"; return 0; }

	fz_document* doc = nullptr;
	int nPages = 0;

	fz_var(doc);
	fz_try(ctx)
	{
		doc = fz_open_document(ctx, strPdfPath.c_str());
		nPages = fz_count_pages(ctx, doc);
	}
	fz_always(ctx)
	{
		if (doc) fz_drop_document(ctx, doc);
	}
	fz_catch(ctx)
	{
		const char* msg = fz_caught_message(ctx);
		m_strErr = msg ? msg : "打开 PDF 失败";
		nPages = 0;
	}
	return nPages;
}

// ============================================================================
// 查询页面分色（专色）列表
// ============================================================================
bool RIPipeline::QueryPageSeparations(const std::string& strPdfPath, int nPage,
									  std::vector<RIPSepInfo>& out)
{
	out.clear();
	m_strErr.clear();

	if (!RIPExt::FileExistsA(strPdfPath))
	{
		m_strErr = "PDF 文件不存在";
		return false;
	}

	fz_context* ctx = (fz_context*)EnsureContext();
	if (!ctx) { m_strErr = "MuPDF 初始化失败"; return false; }

	fz_document* doc = nullptr;
	fz_page* page = nullptr;
	fz_separations* seps = nullptr;
	bool bOk = false;

	fz_var(doc);
	fz_var(page);
	fz_var(seps);
	fz_try(ctx)
	{
		doc = fz_open_document(ctx, strPdfPath.c_str());
		if (nPage < 0 || nPage >= fz_count_pages(ctx, doc))
			fz_throw(ctx, FZ_ERROR_ARGUMENT, "page out of range");

		page = fz_load_page(ctx, doc, nPage);
		seps = fz_page_separations(ctx, page);
		if (seps)
		{
			const int n = fz_count_separations(ctx, seps);
			for (int i = 0; i < n; ++i)
			{
				RIPSepInfo info;
				const char* nm = fz_separation_name(ctx, seps, i);
				info.m_strName = nm ? nm : "";
				info.m_nBehavior = (int)fz_separation_current_behavior(ctx, seps, i);
				out.push_back(info);
			}
		}
		bOk = true;
	}
	fz_always(ctx)
	{
		if (seps) fz_drop_separations(ctx, seps);
		if (page) fz_drop_page(ctx, page);
		if (doc) fz_drop_document(ctx, doc);
	}
	fz_catch(ctx)
	{
		const char* msg = fz_caught_message(ctx);
		m_strErr = msg ? msg : "查询分色失败";
		out.clear();
		bOk = false;
	}
	return bOk;
}

// ============================================================================
// 查询页尺寸
// ============================================================================
bool RIPipeline::QueryPageSize(const std::string& strPdfPath, int nPage,
							   int& nW, int& nH, double& fPageWpt, double& fPageHpt)
{
	fz_context* ctx = (fz_context*)EnsureContext();
	if (!ctx) { m_strErr = "MuPDF 初始化失败"; return false; }

	fz_document* doc = nullptr;
	fz_page* page = nullptr;
	bool ok = false;

	fz_var(doc);
	fz_var(page);
	fz_try(ctx)
	{
		doc = fz_open_document(ctx, strPdfPath.c_str());
		if (nPage < 0 || nPage >= fz_count_pages(ctx, doc))
			fz_throw(ctx, FZ_ERROR_ARGUMENT, "page out of range");

		page = fz_load_page(ctx, doc, nPage);
		fz_rect r = fz_bound_page(ctx, page);
		fPageWpt = r.x1 - r.x0;
		fPageHpt = r.y1 - r.y0;
		nW = (int)(fPageWpt + 0.5);
		nH = (int)(fPageHpt + 0.5);
		ok = true;
	}
	fz_always(ctx)
	{
		if (page) fz_drop_page(ctx, page);
		if (doc) fz_drop_document(ctx, doc);
	}
	fz_catch(ctx)
	{
		m_strErr = "打开 PDF 失败或页码越界";
		ok = false;
	}
	return ok;
}

// ============================================================================
// 核心: PDF -> 位图 -> CMYK / CMYK+专色 分色 -> 1bit/2bit 平面
// ============================================================================
bool RIPipeline::RunPage(const std::string& strPdfPath,
                         int nPage,
                         const RIPParams& params,
                         RIPPageResult& out,
                         const volatile bool* pbCancel)
{
	m_strErr.clear();

	if (!RIPExt::FileExistsA(strPdfPath))
	{
		m_strErr = "PDF 文件不存在";
		return false;
	}

	fz_context* ctx = (fz_context*)EnsureContext();
	if (!ctx)
	{
		m_strErr = "MuPDF 初始化失败";
		return false;
	}

	fz_document* doc = nullptr;
	fz_page* page = nullptr;
	fz_display_list* list = nullptr;
	fz_separations* sepsPage = nullptr;
	fz_separations* sepsRender = nullptr;
	bool bOk = false;

	const DWORD t0 = ::GetTickCount();

	fz_var(doc);
	fz_var(page);
	fz_var(list);
	fz_var(sepsPage);
	fz_var(sepsRender);

	fz_try(ctx)
	{
		// ---------- 1. 打开文档并算好输出尺寸 ----------
		doc = fz_open_document(ctx, strPdfPath.c_str());
		const int nPages = fz_count_pages(ctx, doc);
		if (nPage < 0 || nPage >= nPages)
			fz_throw(ctx, FZ_ERROR_ARGUMENT, "page out of range");

		page = fz_load_page(ctx, doc, nPage);

		fz_rect bbox = fz_bound_page(ctx, page);
		const float pwPt = bbox.x1 - bbox.x0;
		const float phPt = bbox.y1 - bbox.y0;
		if (pwPt <= 0.f || phPt <= 0.f)
			fz_throw(ctx, FZ_ERROR_GENERIC, "bad page size");

		// DPI -> 缩放系数(1pt = 1/72 inch)
		float sx = params.XDpi() / 72.0f;
		float sy = params.YDpi() / 72.0f;

		// 可选: 按长边像素数缩放
		if (params.m_nScaleToLongSide > 0)
		{
			const float longPx = (pwPt > phPt ? pwPt * sx : phPt * sy);
			if (longPx > 0.f)
			{
				const float kf = (float)params.m_nScaleToLongSide / longPx;
				sx *= kf;
				sy *= kf;
			}
		}

		const fz_matrix ctm = fz_scale(sx, sy);
		const fz_irect irect = fz_round_rect(fz_transform_rect(bbox, ctm));
		const int w = irect.x1 - irect.x0;
		const int h = irect.y1 - irect.y0;
		if (w <= 0 || h <= 0)
			fz_throw(ctx, FZ_ERROR_GENERIC, "bad render size");

		if (pbCancel && *pbCancel)
			fz_throw(ctx, FZ_ERROR_ABORT, "cancelled");

		// ---------- 2. 专色处理 ----------
		// 目标: 每个专色(Separation/DeviceN)图层单独生成一个色版。
		//   fz_clone_separations_for_overprint 把 Composite 提升为 SPOT;
		//   渲染时基础色空间按 BGR 走, 专色各占一个额外通道。
		std::vector<std::string> vecSpotNames;

		if (params.m_bSpotSeparate)
		{
			sepsPage = fz_page_separations(ctx, page);
			if (sepsPage)
			{
				sepsRender = fz_clone_separations_for_overprint(ctx, sepsPage);
				if (!sepsRender)
					sepsRender = fz_keep_separations(ctx, sepsPage);
			}

			if (sepsRender)
			{
				const int n = fz_count_separations(ctx, sepsRender);
				for (int i = 0; i < n; ++i)
				{
					if (fz_separation_current_behavior(ctx, sepsRender, i) != FZ_SEPARATION_SPOT)
						fz_set_separation_behavior(ctx, sepsRender, i, FZ_SEPARATION_SPOT);
					const char* nm = fz_separation_name(ctx, sepsRender, i);
					vecSpotNames.push_back(nm ? nm : "");
				}
			}
		}

		// ---------- 3. 行带参数 ----------
		// 默认按 kBandRows 切带; 页高不足一条带时退化为「整页一次渲染」,
		// 这时用的就是老版本那套官方 API, 结果与老版本逐字节一致。
		// 环境变量 RIP_BAND_ROWS=<n> 可强制更小的带高, 用于自测
		// 「分带结果 == 整页结果」。
		int nBandRows = kBandRows;
		{
			char szRows[16] = { 0 };
			if (::GetEnvironmentVariableA("RIP_BAND_ROWS", szRows, sizeof(szRows)) > 0)
			{
				const int n = atoi(szRows);
				if (n >= 16) nBandRows = n;
			}
		}
		const bool bBanded = (nBandRows < h);

		fz_colorspace* csRender = fz_device_bgr(ctx);
		if (bBanded)
			list = fz_new_display_list_from_page(ctx, page);

		// 预览降采样倍率: 大页若按 1:1 出预览, 光预览就要几 GB
		int nPrevScale = 1;
		for (;;)
		{
			const long long pw = (w + nPrevScale - 1) / nPrevScale;
			const long long ph = (h + nPrevScale - 1) / nPrevScale;
			if ((double)pw * (double)ph <= kMaxPreviewPixels) break;
			if (nPrevScale >= 64) break;
			nPrevScale <<= 1;
		}
		const int nPrevW = (w + nPrevScale - 1) / nPrevScale;
		const int nPrevH = (h + nPrevScale - 1) / nPrevScale;

		std::vector<unsigned char> vecPrev;         // 输出预览(nPrevW * nPrevH * 4)
		std::vector<unsigned int>  vecPrevAcc;      // 预览列累加缓冲
		vecPrev.assign((size_t)nPrevW * nPrevH * 4, 0);
		vecPrevAcc.assign((size_t)nPrevW * 4, 0);
		int nAccRows = 0;

		std::vector<BandPlaneState> vecSt;          // 各色版状态(整页位平面)
		bool bStReady = false;

		std::vector<unsigned char> vecCmykRows;     // 当前带输出行的连续调 CMYK
		std::vector<RIPGrayPlane> vecGray;          // 当前带的灰阶
		std::vector<unsigned char> tight;           // 位图行距不紧排时的压紧缓冲

		// 调试用: --dump 需要整页合并 CMYK(只有显式开启时才分配)
		const bool bDump = params.m_bDumpMergedCMYK && !params.m_strDumpDir.empty();
		std::vector<unsigned char> vecDump;
		if (bDump)
			vecDump.assign((size_t)w * h * 4, 0);

		double nRenderMS = 0.0, nSepMS = 0.0, nRipMS = 0.0;

		// ---------- 4. 逐行带: 渲染 -> 色彩分离 -> 抖动 ----------
		for (int y0 = 0; y0 < h; y0 += nBandRows)
		{
			if (pbCancel && *pbCancel)
				fz_throw(ctx, FZ_ERROR_ABORT, "cancelled");

			const int nRows = (y0 + nBandRows <= h) ? nBandRows : (h - y0);
			const int nTop = (y0 >= kBandPad) ? kBandPad : 0;
			const int nBot = (y0 + nRows + kBandPad <= h) ? kBandPad : 0;
			const int nBandY0 = y0 - nTop;
			const int nBandH = nTop + nRows + nBot;

			fz_pixmap* pixBand = nullptr;
			fz_device* devBand = nullptr;
			int bwOut = 0, bhOut = 0;      // 模式 3 会得到整页尺寸的位图
			int nRenderMode = 1;           // RIP_RENDER_MODE 自测开关, 1~6 种渲染写法
			{
				char szMode[8] = { 0 };
				if (::GetEnvironmentVariableA("RIP_RENDER_MODE", szMode, sizeof(szMode)) > 0)
					nRenderMode = atoi(szMode);
			}
			fz_var(pixBand);
			fz_var(devBand);

			fz_try(ctx)
			{
				const DWORD tr0 = ::GetTickCount();
				if (bBanded)
				{
					// ---- 只渲染这一条带 ----
					// 手工路径必须严格照官方 fz_new_pixmap_from_page* 的写法来:
					//   1) 按「带 bbox」建位图并清成 255(基础色通道 255 / 专色通道 0);
					//   2) draw device 带 ctm —— 设备变换只在这里生效一次;
					//   3) fz_run_display_list 必须传 fz_identity。
					// 历史坑: 曾把 ctm 同时给 draw device 和 fz_run_page, 变换叠加
					// 两次, 缩放 != 1 时专色通道全 0 —— 千万别改回去。
					fz_irect bandBbox = irect;
					bandBbox.y0 = irect.y0 + nBandY0;
					bandBbox.y1 = bandBbox.y0 + nBandH;

					// RIP_RENDER_MODE 自测开关, 1~6 种渲染写法:
					//   1 = 带 bbox 的 draw device(ctm) + display list(identity)   [默认]
					//   2 = 带 bbox 的 draw device(ctm) + page(identity)
					//   3 = 官方整页 API (内存大, 仅对照)
					//   4 = 不带 bbox 的 draw device(ctm) + display list(identity)
					//   5 = draw device(identity) + display list(ctm)
					//   6 = draw device(identity) + page(ctm)
					//   7 = 同 4 但不清零   8 = 同 4 但 fz_clear_pixmap(0)
					pixBand = fz_new_pixmap_with_bbox(ctx, csRender, bandBbox, sepsRender, 0);
					// 模式 7 = 不清零 / 模式 8 = fz_clear_pixmap(0) / 其余 = 清成 255
					if (nRenderMode == 7)
						;
					else if (nRenderMode == 8)
						fz_clear_pixmap(ctx, pixBand);
					else
						fz_clear_pixmap_with_value(ctx, pixBand, 255);

					if (nRenderMode == 3)
					{
						// 官方整页 API (对照用, 内存大)
						fz_drop_pixmap(ctx, pixBand);
						pixBand = nullptr;
						if (sepsRender)
							pixBand = fz_new_pixmap_from_display_list_with_separations(
								ctx, list, ctm, csRender, sepsRender, 0);
						else
							pixBand = fz_new_pixmap_from_display_list(ctx, list, ctm, csRender, 0);
					}
					else if (nRenderMode == 6)
					{
						devBand = fz_new_draw_device(ctx, fz_identity, pixBand);
						fz_run_page(ctx, page, devBand, ctm, NULL);
						fz_close_device(ctx, devBand);
						fz_drop_device(ctx, devBand);
						devBand = nullptr;
					}
					else if (nRenderMode == 5)
					{
						devBand = fz_new_draw_device(ctx, fz_identity, pixBand);
						fz_run_display_list(ctx, list, devBand, ctm, fz_infinite_rect, NULL);
						fz_close_device(ctx, devBand);
						fz_drop_device(ctx, devBand);
						devBand = nullptr;
					}
					else if (nRenderMode == 4 || nRenderMode == 7 || nRenderMode == 8)
					{
						devBand = fz_new_draw_device(ctx, ctm, pixBand);
						fz_run_display_list(ctx, list, devBand, fz_identity, fz_infinite_rect, NULL);
						fz_close_device(ctx, devBand);
						fz_drop_device(ctx, devBand);
						devBand = nullptr;
					}
					else if (nRenderMode == 2)
					{
						devBand = fz_new_draw_device_with_bbox(ctx, ctm, pixBand, &bandBbox);
						fz_run_page(ctx, page, devBand, fz_identity, NULL);
						fz_close_device(ctx, devBand);
						fz_drop_device(ctx, devBand);
						devBand = nullptr;
					}
					else   // 1 = 默认
					{
						devBand = fz_new_draw_device_with_bbox(ctx, ctm, pixBand, &bandBbox);
						fz_run_display_list(ctx, list, devBand, fz_identity, fz_infinite_rect, NULL);
						fz_close_device(ctx, devBand);
						fz_drop_device(ctx, devBand);
						devBand = nullptr;
					}
					bwOut = fz_pixmap_width(ctx, pixBand);
					bhOut = fz_pixmap_height(ctx, pixBand);
				}
				else
				{
					// 页高不足一条带: 直接走老版本那套官方 API, 逐字节一致
					// (模式 9 = 这里也走手工路径, 用于隔离排查)
					if (nRenderMode == 9 || nRenderMode == 10)
					{
						fz_irect bandBbox = irect;
						pixBand = fz_new_pixmap_with_bbox(ctx, csRender, bandBbox, sepsRender, 0);
						fz_clear_pixmap_with_value(ctx, pixBand, 255);
						// 10 = 不带 bbox 的 draw device; 9 = 带 bbox
						devBand = (nRenderMode == 10)
							? fz_new_draw_device(ctx, ctm, pixBand)
							: fz_new_draw_device_with_bbox(ctx, ctm, pixBand, &bandBbox);
						fz_run_page(ctx, page, devBand, fz_identity, NULL);
						fz_close_device(ctx, devBand);
						fz_drop_device(ctx, devBand);
						devBand = nullptr;
						// 自测: 手工 vs 官方 位图逐字节对比
						if (::GetEnvironmentVariableA("RIP_DUMP_PIX", nullptr, 0) > 0)
						{
							fz_pixmap* p2 = fz_new_pixmap_from_page(ctx, page, ctm, csRender, 0);
							FILE* f1 = fopen("E:/_pix_manual.raw", "wb");
							FILE* f2 = fopen("E:/_pix_official.raw", "wb");
							const int mw = fz_pixmap_width(ctx, pixBand);
							const int mh = fz_pixmap_height(ctx, pixBand);
							const int mc = fz_pixmap_components(ctx, pixBand);
							const int mst = fz_pixmap_stride(ctx, pixBand);
							if (f1) {
								for (int yy = 0; yy < mh; ++yy)
									fwrite(fz_pixmap_samples(ctx, pixBand) + (size_t)yy * mst,
										   1, (size_t)mw * mc, f1);
								fclose(f1);
							}
							if (f2) { fwrite(fz_pixmap_samples(ctx, p2), 1,
								(size_t)fz_pixmap_width(ctx, p2) * fz_pixmap_height(ctx, p2)
								* fz_pixmap_components(ctx, p2), f2); fclose(f2); }
							fz_drop_pixmap(ctx, p2);
							printf("[dump] manual %dx%dx%d stride=%d  official %dx%dx%d\\n",
								mw, mh, mc, mst,
								fz_pixmap_width(ctx, p2), fz_pixmap_height(ctx, p2),
								fz_pixmap_components(ctx, p2));
						}
					}
					else if (sepsRender)
						pixBand = fz_new_pixmap_from_page_with_separations(
							ctx, page, ctm, csRender, sepsRender, 0);
					else
						pixBand = fz_new_pixmap_from_page(ctx, page, ctm, csRender, 0);
				}
				nRenderMS += (double)(::GetTickCount() - tr0);

				if (!pixBand)
					fz_throw(ctx, FZ_ERROR_GENERIC, "band render failed");

				const int bw = (bwOut > 0) ? bwOut : fz_pixmap_width(ctx, pixBand);
				const int bh = (bhOut > 0) ? bhOut : fz_pixmap_height(ctx, pixBand);
				const int bc = fz_pixmap_components(ctx, pixBand);
				const unsigned char* pbs = fz_pixmap_samples(ctx, pixBand);
				const int bstride = fz_pixmap_stride(ctx, pixBand);

				if (::GetEnvironmentVariableA("RIP_DEBUG_PIX", nullptr, 0) > 0)
					printf("[pix] irect=(%d,%d,%d,%d) bandBbox=(%d,%d,%d,%d) pix=(%d,%d,%d,%d,%d) mode=%d banded=%d\\n",
						   irect.x0, irect.y0, irect.x1, irect.y1,
						   irect.x0, irect.y0 + nBandY0, irect.x1, irect.y0 + nBandY0 + nBandH,
						   fz_pixmap_x(ctx, pixBand), fz_pixmap_y(ctx, pixBand),
						   bw, bh, bc, nRenderMode, (int)bBanded);

				if (bw != w || bc < 3)
					fz_throw(ctx, FZ_ERROR_GENERIC, "band size mismatch");
				if (nRenderMode != 3 && bh != nBandH)
					fz_throw(ctx, FZ_ERROR_GENERIC, "band size mismatch");

				// 行距不紧排时先压紧(绝大多数情况 pixmap 行距就是 w * 通道数, 零拷贝)
				const int nRowBytes = bw * bc;
				const unsigned char* pTight = pbs;
				if (bstride != nRowBytes)
				{
					tight.resize((size_t)nRowBytes * bh);
					for (int y = 0; y < bh; ++y)
						memcpy(tight.data() + (size_t)y * nRowBytes,
							   pbs + (size_t)y * bstride, (size_t)nRowBytes);
					pTight = tight.data();
				}

				const DWORD ts0 = ::GetTickCount();

				if (!BuildGrayBand(pTight, bw, bh, nBandY0, nTop, nRows, h,
								   3, bc - 3, vecSpotNames, params,
								   GetRgb2CmykTransform(params),
								   vecGray, &vecCmykRows, m_strErr, pbCancel))
				{
					if (pbCancel && *pbCancel)
						fz_throw(ctx, FZ_ERROR_ABORT, "cancelled");
					fz_throw(ctx, FZ_ERROR_GENERIC, m_strErr.c_str());
				}
				nSepMS += (double)(::GetTickCount() - ts0);

				// 带位图用完立刻释放 —— 峰值内存与页面积解耦的关键
				fz_drop_pixmap(ctx, pixBand);
				pixBand = nullptr;

				// 首次拿到色版清单后一次性分配整页位平面
				if (!bStReady)
				{
					InitBandPlanes(vecGray, bw, h, params.BitDepth(), vecSt);
					bStReady = true;
				}

				const DWORD trp0 = ::GetTickCount();
				if (!DitherBandRows(vecGray, bw, nRows, y0, params, vecSt, m_strErr, pbCancel))
				{
					if (pbCancel && *pbCancel)
						fz_throw(ctx, FZ_ERROR_ABORT, "cancelled");
					fz_throw(ctx, FZ_ERROR_GENERIC, m_strErr.c_str());
				}
				nRipMS += (double)(::GetTickCount() - trp0);

				// 预览: 整数倍盒式降采样(倍率 1 时就是原样拷贝)
				if (!vecPrev.empty() && !vecCmykRows.empty())
				{
					const unsigned char* pRows = vecCmykRows.data();
					for (int y = 0; y < nRows; ++y)
					{
						const int gy = y0 + y;
						const unsigned char* pRow = pRows + (size_t)y * bw * 4;
						for (int x = 0; x < bw; ++x)
						{
							unsigned int* a = &vecPrevAcc[(size_t)(x / nPrevScale) * 4];
							const unsigned char* q = pRow + (size_t)x * 4;
							a[0] += q[0]; a[1] += q[1]; a[2] += q[2]; a[3] += q[3];
						}
						++nAccRows;

						if (((gy + 1) % nPrevScale) == 0 || gy == h - 1)
						{
							const int py = gy / nPrevScale;
							if (py >= 0 && py < nPrevH)
							{
								unsigned char* dst = &vecPrev[(size_t)py * nPrevW * 4];
								for (int px = 0; px < nPrevW; ++px)
								{
									int x1 = (px + 1) * nPrevScale;
									if (x1 > bw) x1 = bw;
									const unsigned int nDiv =
										(unsigned int)(x1 - px * nPrevScale) * (unsigned int)nAccRows;
									if (!nDiv) continue;
									for (int ch = 0; ch < 4; ++ch)
									{
										const unsigned int s = vecPrevAcc[(size_t)px * 4 + ch];
										dst[px * 4 + ch] = (unsigned char)((s + nDiv / 2) / nDiv);
									}
								}
							}
							std::fill(vecPrevAcc.begin(), vecPrevAcc.end(), 0u);
							nAccRows = 0;
						}
					}
				}

				// --dump: 合并 CMYK(调试用, 显式开启才走)
				if (bDump && vecGray.size() >= 4)
				{
					for (int c = 0; c < 4; ++c)
					{
						const unsigned char* pSrc = vecGray[c].m_vecGray.data();
						unsigned char* pDst = vecDump.data() + (size_t)y0 * w * 4 + c;
						for (int y = 0; y < nRows; ++y)
						{
							const unsigned char* s = pSrc + (size_t)y * bw;
							unsigned char* d = pDst + (size_t)y * w * 4;
							for (int x = 0; x < bw; ++x)
								d[(size_t)x * 4] = s[x];
						}
					}
				}
			}
			fz_always(ctx)
			{
				if (devBand) { fz_drop_device(ctx, devBand); devBand = nullptr; }
				if (pixBand) { fz_drop_pixmap(ctx, pixBand); pixBand = nullptr; }
			}
			fz_catch(ctx)
			{
				if (pbCancel && *pbCancel)
					fz_throw(ctx, FZ_ERROR_ABORT, "cancelled");
				fz_rethrow(ctx);
			}
		}

		// ---------- 5. 收尾: 汇总各色版 ----------
		if (!bStReady)
			fz_throw(ctx, FZ_ERROR_GENERIC, "empty page");

		const int nBit = params.BitDepth();
		const int nMaxV = (1 << nBit) - 1;

		out.m_vecPlanes.clear();
		out.m_vecInkNames.clear();
		out.m_vecInkPercent.clear();
		for (size_t k = 0; k < vecSt.size(); ++k)
		{
			out.m_vecInkNames.push_back(vecSt[k].m_strName);
			// 真实平均墨量: 2bit 时一个墨点可能是 1/3 或 2/3 墨
			out.m_vecInkPercent.push_back(
				(float)((double)vecSt[k].m_nOn / ((double)w * h * nMaxV)));
			out.m_vecPlanes.push_back(std::move(vecSt[k].m_plane));
		}
		out.m_nWidth = w;
		out.m_nHeight = h;
		out.m_nBit = nBit;
		out.m_vecPreviewCMYK.swap(vecPrev);
		out.m_nPreviewW = nPrevW;
		out.m_nPreviewH = nPrevH;
		out.m_nPreviewScale = nPrevScale;
		out.m_nRenderMS = nRenderMS;
		out.m_nSepMS = nSepMS;
		out.m_nRipMS = nRipMS;
		// ---------- 6. 墨点统计(多页 PDF 只统计第 1 页) ----------
		out.m_vecDotStats.clear();
		if (params.m_bDotStat && nPage == 0)
		{
			out.m_vecDotStats.resize(out.m_vecPlanes.size());
			for (size_t k = 0; k < out.m_vecPlanes.size(); ++k)
			{
				RIPDotStat& ds = out.m_vecDotStats[k];
				ds.m_strInk = out.m_vecInkNames[k];
				CollectDotStatPlane(out.m_vecPlanes[k], params.YDpi(),
									params.m_nDotSmallUM, params.m_nDotMidUM, ds);
			}
		}


		if (bDump)
		{
			const std::string fn = RIPExt::JoinPath(params.m_strDumpDir,
				RIPExt::PlaneFileName(strPdfPath, nPage, "merged"));
			SaveMergedCMYKToTIFF(fn, vecDump.data(), w, h, params.YDpi());
		}

		bOk = true;
	}
	fz_always(ctx)
	{
		if (sepsRender) fz_drop_separations(ctx, sepsRender);
		if (sepsPage) fz_drop_separations(ctx, sepsPage);
		if (list) fz_drop_display_list(ctx, list);
		if (page) fz_drop_page(ctx, page);
		if (doc) fz_drop_document(ctx, doc);
	}
	fz_catch(ctx)
	{
		const char* msg = fz_caught_message(ctx);
		m_strErr = msg ? msg : "RIP 失败";
		bOk = false;
	}

	return bOk;
}

// ============================================================================
// TIFF 落盘
// ============================================================================
// ============================================================================
// PRN 容器(BYHX 板卡 PRN1RipHeader): 各色版写成一个 .prn
//
//   文件结构: [256 字节头][(y=0..h-1) x (K,C,M,Y,S1,S2,...) 各一行位平面]
//   每行每色版写 nBytesPerLine 字节(按 4 字节对齐, 不足补 0)
//   头部 64 个 DWORD = 256 字节, 字段与外部约定的 PRN1RipHeader 一致
//   位平面 1 = 出墨, 与我们的 TIFF 位平面一致
// ============================================================================
namespace
{
	// 与外部约定一一对应: 64 个 DWORD(全 DWORD, 自然对齐无填充 -> 恰好 256 字节)
	struct Prn1RipHeader
	{
		unsigned long nSignature;
		unsigned long nXDPI;
		unsigned long nYDPI;
		unsigned long nBytesPerLine;
		unsigned long nHeight;
		unsigned long nWidth;
		unsigned long nPaperWidth;
		unsigned long nColors;
		unsigned long nBits;
		unsigned long nReserved1;
		unsigned long nReserved2;
		unsigned long nReserved3;
		unsigned long nCopys;
		unsigned long nReserved4[51];
	};
	static_assert(sizeof(Prn1RipHeader) == 256, "PRN1RipHeader must be 256 bytes");

	const unsigned long kPrn1Signature = 5458008UL;   // 固定头部标识


	// 与外部约定一一对应: 12 个 long = 48 字节
	struct Prn2RipHeader
	{
		long nSignature;      // 固定值 1381564416
		long nXDPI;
		long nYDPI;
		long nWidth;
		long nHeight;
		long nBytesPerLine;
		long nColors;
		long nReserve1;
		long nReserve2;
		long nBitsPerPiexl;
		long nGraySize;
		long nReserve3;
	};
	static_assert(sizeof(Prn2RipHeader) == 48, "PRN2RipHeader must be 48 bytes");

	const long kPrn2Signature = 1381564416L;   // PRN2 固定头部标识
}

bool RIPipeline::SavePlanesPRN(const std::string& strPath,
							   const std::vector<RIPPlane>& vecPlanes,
							   int nWidth, int nHeight, int nBit,
							   int nXDpi, int nYDpi, int nCopies,
							   bool bMatch8ColorQuirk)
{
	if (nWidth <= 0 || nHeight <= 0 || nBit <= 0 || vecPlanes.size() < 4)
		return false;

	// 外部解析端只支持 4~8 色(CMYK + 最多 4 个专色), 多出的截掉
	size_t nColors = vecPlanes.size();
	if (nColors > 8) nColors = 8;

	// RIPPageResult 里的顺序是 C(0) M(1) Y(2) K(3) S1(4) ...
	// 板卡侧逐行读取顺序是 K, C, M, Y, S1, S2, S3(8 色时外部代码写的是 S4, S1, S2, S3)
	int kOrder[8];
	kOrder[0] = 3; kOrder[1] = 0; kOrder[2] = 1; kOrder[3] = 2;
	for (int i = 4; i < 8; ++i) kOrder[i] = i;
	if (nColors == 8 && bMatch8ColorQuirk)
	{
		kOrder[4] = 7; kOrder[5] = 4; kOrder[6] = 5; kOrder[7] = 6;
	}

	// 每行每色版字节数: 位平面原始宽度向上取整到 4 字节
	const int nRaw = (nWidth * nBit + 7) / 8;
	const unsigned long nBpl = (unsigned long)(((nRaw + 3) / 4) * 4);

	Prn1RipHeader hdr;
	::ZeroMemory(&hdr, sizeof(hdr));
	hdr.nSignature    = kPrn1Signature;
	hdr.nXDPI         = (unsigned long)(nXDpi > 0 ? nXDpi : 0);
	hdr.nYDPI         = (unsigned long)(nYDpi > 0 ? nYDpi : 0);
	hdr.nBytesPerLine = nBpl;
	hdr.nHeight       = (unsigned long)nHeight;
	hdr.nWidth        = (unsigned long)nWidth;
	hdr.nPaperWidth   = 0;
	hdr.nColors       = (unsigned long)nColors;
	hdr.nBits         = (unsigned long)nBit;
	hdr.nCopys        = (unsigned long)(nCopies > 0 ? nCopies : 1);

	{
		const size_t sp = strPath.find_last_of("\\/");
		if (sp != std::string::npos) RIPExt::EnsureDirExists(strPath.substr(0, sp));
	}

	FILE* fp = _wfopen(RIPExt::Utf8ToWString(strPath).c_str(), L"wb");
	if (!fp) return false;

	bool ok = (fwrite(&hdr, 1, sizeof(hdr), fp) == sizeof(hdr));

	std::vector<unsigned char> row((size_t)nBpl * nColors);
	for (int y = 0; ok && y < nHeight; ++y)
	{
		::ZeroMemory(row.data(), row.size());
		for (size_t c = 0; c < nColors; ++c)
		{
			const RIPPlane& pl = vecPlanes[(size_t)kOrder[c]];
			if (pl.m_nStride <= 0 || y >= pl.m_nHeight) continue;

			const unsigned char* pSrc = pl.m_vecBits.data() + (size_t)y * pl.m_nStride;
			const size_t nCopy = ((size_t)pl.m_nStride < (size_t)nBpl)
				? (size_t)pl.m_nStride : (size_t)nBpl;
			::memcpy(row.data() + (size_t)c * nBpl, pSrc, nCopy);
		}
		if (fwrite(row.data(), 1, row.size(), fp) != row.size())
			ok = false;
	}

	fclose(fp);
	return ok;
}


// ============================================================================
// PRN2 容器(喷头板卡 PRN2RipHeader):
//   48 字节头(12 个 long) + 逐行按 C,M,Y,K[,S1..] 交织的位平面
//   行字节数按 4 字节对齐, 1 = 出墨(与 TIFF 位平面同一份数据)
// ============================================================================
bool RIPipeline::SavePlanesPRN2(const std::string& strPath,
								const std::vector<RIPPlane>& vecPlanes,
								int nWidth, int nHeight, int nBit,
								int nXDpi, int nYDpi, int nCopies)
{
	(void)nCopies;   // PRN2 头里没有份数字段
	if (nWidth <= 0 || nHeight <= 0 || nBit <= 0 || vecPlanes.size() < 4)
		return false;

	// 解析端只支持 4~8 色(CMYK + 最多 4 个专色), 多出的截掉
	size_t nColors = vecPlanes.size();
	if (nColors > 8) nColors = 8;

	// 行内顺序与 RIPPageResult 一致: C(0) M(1) Y(2) K(3) S1(4) ...
	// 唯一例外: 8 色时解析端读的是 C,M,Y,K,S4,S1,S2,S3
	int kOrder[8];
	for (int i = 0; i < 8; ++i) kOrder[i] = i;
	if (nColors == 8)
	{
		kOrder[4] = 7; kOrder[5] = 4; kOrder[6] = 5; kOrder[7] = 6;
	}

	// 每行每色版字节数: 位平面原始宽度向上取整到 4 字节
	const int nRaw = (nWidth * nBit + 7) / 8;
	const long nBpl = (long)(((nRaw + 3) / 4) * 4);

	Prn2RipHeader hdr;
	::ZeroMemory(&hdr, sizeof(hdr));
	hdr.nSignature    = kPrn2Signature;
	hdr.nXDPI         = (long)(nXDpi > 0 ? nXDpi : 0);
	hdr.nYDPI         = (long)(nYDpi > 0 ? nYDpi : 0);
	hdr.nWidth        = (long)nWidth;
	hdr.nHeight       = (long)nHeight;
	hdr.nBytesPerLine = nBpl;
	hdr.nColors       = (long)nColors;
	hdr.nReserve1     = 0;
	hdr.nReserve2     = 0;
	hdr.nBitsPerPiexl = (long)nBit;
	hdr.nGraySize     = 0;   // 解析端未使用该字段
	hdr.nReserve3     = 0;

	{
		const size_t sp = strPath.find_last_of("\\/");
		if (sp != std::string::npos) RIPExt::EnsureDirExists(strPath.substr(0, sp));
	}

	FILE* fp = _wfopen(RIPExt::Utf8ToWString(strPath).c_str(), L"wb");
	if (!fp) return false;

	bool ok = (fwrite(&hdr, 1, sizeof(hdr), fp) == sizeof(hdr));

	std::vector<unsigned char> row((size_t)nBpl * nColors);
	for (int y = 0; ok && y < nHeight; ++y)
	{
		::ZeroMemory(row.data(), row.size());
		for (size_t c = 0; c < nColors; ++c)
		{
			const RIPPlane& pl = vecPlanes[(size_t)kOrder[c]];
			if (pl.m_nStride <= 0 || y >= pl.m_nHeight) continue;

			const unsigned char* pSrc = pl.m_vecBits.data() + (size_t)y * pl.m_nStride;
			const size_t nCopy = ((size_t)pl.m_nStride < (size_t)nBpl)
				? (size_t)pl.m_nStride : (size_t)nBpl;
			::memcpy(row.data() + (size_t)c * nBpl, pSrc, nCopy);
		}
		if (fwrite(row.data(), 1, row.size(), fp) != row.size())
			ok = false;
	}

	fclose(fp);
	return ok;
}

bool RIPipeline::SaveMergedCMYKToTIFF(const std::string& strPath,
									  const unsigned char* pCMYK, int w, int h, int nDpi)
{
	// 输出目录可能还不存在（例如调用方显式指定），落盘前先确保父目录已创建
	{
		const size_t sp = strPath.find_last_of("\\/");
		if (sp != std::string::npos) RIPExt::EnsureDirExists(strPath.substr(0, sp));
	}
	TIFF* tif = TIFFOpenW(RIPExt::Utf8ToWString(strPath).c_str(), "w");
	if (!tif) return false;

	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, w);
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, h);
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 4);
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_SEPARATED);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
	TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	if (nDpi > 0)
	{
		TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
		TIFFSetField(tif, TIFFTAG_XRESOLUTION, (float)nDpi);
		TIFFSetField(tif, TIFFTAG_YRESOLUTION, (float)nDpi);
	}

	bool ok = true;
	for (int row = 0; row < h; ++row)
	{
		const unsigned char* pRow = pCMYK + (size_t)row * w * 4;
		if (TIFFWriteScanline(tif, (void*)pRow, row) < 0) { ok = false; break; }
	}
	TIFFClose(tif);
	return ok;
}

bool RIPipeline::SavePlanePreviewTIFF(const std::string& strPath, const RIPPlane& plane, int nDpi)
{
	// 输出目录可能还不存在（例如调用方显式指定），落盘前先确保父目录已创建
	{
		const size_t sp = strPath.find_last_of("\\/");
		if (sp != std::string::npos) RIPExt::EnsureDirExists(strPath.substr(0, sp));
	}
	TIFF* tif = TIFFOpenW(RIPExt::Utf8ToWString(strPath).c_str(), "w");
	if (!tif) return false;

	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, plane.m_nWidth);
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, plane.m_nHeight);
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
	if (nDpi > 0)
	{
		TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
		TIFFSetField(tif, TIFFTAG_XRESOLUTION, (float)nDpi);
		TIFFSetField(tif, TIFFTAG_YRESOLUTION, (float)nDpi);
	}

	const int maxV = (1 << plane.m_nBit) - 1;
	std::vector<unsigned char> row((size_t)plane.m_nWidth);

	bool ok = true;
	for (int y = 0; y < plane.m_nHeight; ++y)
	{
		for (int x = 0; x < plane.m_nWidth; ++x)
		{
			const int v = plane.GetPixel(x, y);
			// 0 号平面(不出墨)显示为白，其余为黑
			row[x] = (unsigned char)(255 - (v * 255 / (maxV ? maxV : 1)));
		}
		if (TIFFWriteScanline(tif, row.data(), y) < 0) { ok = false; break; }
	}
	TIFFClose(tif);
	return ok;
}

// 生产用: 原生 1bit / 2bit 打包 TIFF
bool RIPipeline::SavePlaneBilevelTIFF(const std::string& strPath, const RIPPlane& plane,
									  int nDpi, int photometric)
{
	// 输出目录可能还不存在（例如调用方显式指定），落盘前先确保父目录已创建
	{
		const size_t sp = strPath.find_last_of("\\/");
		if (sp != std::string::npos) RIPExt::EnsureDirExists(strPath.substr(0, sp));
	}
	TIFF* tif = TIFFOpenW(RIPExt::Utf8ToWString(strPath).c_str(), "w");
	if (!tif) return false;

	// 位打包: 1bit -> 8 px/byte, 2bit -> 4 px/byte
	const int nPixPerByte = 8 / plane.m_nBit;

	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, plane.m_nWidth);
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, plane.m_nHeight);
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, plane.m_nBit);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC,
				 photometric == 1 ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_MINISWHITE);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
	TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	// 未压缩时第一个字节的高位先输出（TIFF 规范要求）
	TIFFSetField(tif, TIFFTAG_FILLORDER, FILLORDER_MSB2LSB);
	if (nDpi > 0)
	{
		TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
		TIFFSetField(tif, TIFFTAG_XRESOLUTION, (float)nDpi);
		TIFFSetField(tif, TIFFTAG_YRESOLUTION, (float)nDpi);
	}

	// m_vecBits 已按 m_nStride(=8*bitsPerLine) 打包，直接整行写出即可。
	// 若 photometric 取 MINISBLACK，则需把每一位取反。
	std::vector<unsigned char> row((size_t)plane.m_nStride);
	bool ok = true;
	for (int y = 0; y < plane.m_nHeight; ++y)
	{
		const unsigned char* pSrc = plane.m_vecBits.data() + (size_t)y * plane.m_nStride;
		if (photometric == 1)
		{
			for (int i = 0; i < plane.m_nStride; ++i) row[i] = (unsigned char)~pSrc[i];
			if (TIFFWriteScanline(tif, row.data(), y) < 0) { ok = false; break; }
		}
		else
		{
			if (TIFFWriteScanline(tif, (void*)pSrc, y) < 0) { ok = false; break; }
		}
	}
	TIFFClose(tif);
	(void)nPixPerByte;
	return ok;
}

// 生产用: C/M/Y/K 四色版合并成一个多平面 TIFF
//   SamplesPerPixel = 4、PlanarConfig = SEPARATE
//   -> 每个通道一条独立位平面，行字节排布与单通道文件完全一致。
//   Photometric = SEPARATED(CMYK): 0 = 不出墨，最大 = 满墨，与色版位语义一致。
//   注意: PLANARCONFIG_SEPARATE 下单条扫描线按「单样本」计算，
//         所以 SIZE 就等于 plane.m_nStride（已按 8 像素对齐过）。
bool RIPipeline::SaveCMYKPlanesTIFF(const std::string& strPath,
									const RIPPlane* arrPlanes[4],
									int nDpi, int nForceBits)
{
	for (int i = 0; i < 4; ++i)
	{
		if (!arrPlanes[i]) return false;
	}

	const RIPPlane& p0 = *arrPlanes[0];
	const int nW = p0.m_nWidth;
	const int nH = p0.m_nHeight;
	const int nBit = p0.m_nBit;
	if (nW <= 0 || nH <= 0 || nBit <= 0 || nBit > 8) return false;

	// 0 = 沿用各色版原生位深(生产)；8 = 统一展开成 8bit(预览)
	const int nOutBits = (nForceBits == 8) ? 8 : nBit;

	// 四个通道必须同尺寸；生产模式下还要求同位深，否则平面无法对齐
	for (int i = 1; i < 4; ++i)
	{
		const RIPPlane& p = *arrPlanes[i];
		if (p.m_nWidth != nW || p.m_nHeight != nH)
			return false;
		if (nOutBits != 8 && p.m_nBit != nBit)
			return false;
	}

	// 输出目录可能还不存在（例如调用方显式指定），落盘前先确保父目录已创建
	{
		const size_t sp = strPath.find_last_of("\\/");
		if (sp != std::string::npos) RIPExt::EnsureDirExists(strPath.substr(0, sp));
	}

	TIFF* tif = TIFFOpenW(RIPExt::Utf8ToWString(strPath).c_str(), "w");
	if (!tif) return false;

	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, nW);
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, nH);
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, nOutBits);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 4);
	TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_SEPARATED);	// CMYK
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_SEPARATE);	// 每通道一条独立位平面
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
	TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	// 未压缩时第一个字节的高位先输出（TIFF 规范要求）
	TIFFSetField(tif, TIFFTAG_FILLORDER, FILLORDER_MSB2LSB);
	if (nDpi > 0)
	{
		TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
		TIFFSetField(tif, TIFFTAG_XRESOLUTION, (float)nDpi);
		TIFFSetField(tif, TIFFTAG_YRESOLUTION, (float)nDpi);
	}

	// SEPARATE 模式按「通道优先」顺序落盘: 先 C 的全部行，再 M / Y / K
	bool ok = true;

	// 预览: 位打包数据展开成 8bit，样本值 = 墨量(0 不出墨 / 255 满墨)
	std::vector<unsigned char> row8;
	if (nOutBits == 8) row8.resize((size_t)nW);

	for (int s = 0; s < 4 && ok; ++s)
	{
		const RIPPlane& pl = *arrPlanes[s];
		if (pl.m_vecBits.size() < (size_t)nH * pl.m_nStride) { ok = false; break; }

		const int nMaxV = (1 << pl.m_nBit) - 1;
		const int nDiv = nMaxV ? nMaxV : 1;

		for (int y = 0; y < nH; ++y)
		{
			void* pRow = nullptr;
			if (nOutBits == 8)
			{
				for (int x = 0; x < nW; ++x)
					row8[x] = (unsigned char)(pl.GetPixel(x, y) * 255 / nDiv);
				pRow = row8.data();
			}
			else
			{
				pRow = (void*)(pl.m_vecBits.data() + (size_t)y * pl.m_nStride);
			}

			if (TIFFWriteScanline(tif, pRow, (uint32_t)y, (uint16_t)s) < 0)
			{
				ok = false;
				break;
			}
		}
	}
	TIFFClose(tif);
	return ok;
}

// ============================================================================
// 一页的落盘规则（GUI 与 CLI 共用）
// ============================================================================
int RIPipeline::SavePagePlanes(const std::string& strPdf, int nPage0,
							   const std::string& strOutDir, int nDpi,
							   const RIPPageResult& result, bool bWithPreview, int nPrnVersion)
{
	if (!RIPExt::EnsureDirExists(strOutDir))
		return 0;

	int nSaved = 0;

	// PRN 容器(喷头板卡): 1 = PRN1RipHeader, 2 = PRN2RipHeader
	if (nPrnVersion > 0 && result.m_vecPlanes.size() >= 4)
	{
		std::string strPrnName = RIPExt::PlaneFileName(strPdf, nPage0 + 1, "CMYK");
		const size_t nDot = strPrnName.find_last_of('.');
		if (nDot != std::string::npos) strPrnName = strPrnName.substr(0, nDot);
		strPrnName += ".prn";
		const bool bPrnOk = (nPrnVersion == 2)
			? SavePlanesPRN2(RIPExt::JoinPath(strOutDir, strPrnName), result.m_vecPlanes,
							 result.m_nWidth, result.m_nHeight, result.m_nBit,
							 nDpi, nDpi, 1)
			: SavePlanesPRN(RIPExt::JoinPath(strOutDir, strPrnName), result.m_vecPlanes,
							result.m_nWidth, result.m_nHeight, result.m_nBit,
							nDpi, nDpi, 1);
		if (bPrnOk)
			++nSaved;
	}

	if (result.m_vecPlanes.size() >= 4)
	{
		const RIPPlane* arrCMYK[4] = {
			&result.m_vecPlanes[0], &result.m_vecPlanes[1],
			&result.m_vecPlanes[2], &result.m_vecPlanes[3]
		};

		// 生产文件: 原生 1bit/2bit 位打包, 行排布与单通道文件一致, 可直接按通道喂板卡
		const std::string strFn = RIPExt::JoinPath(strOutDir,
			RIPExt::PlaneFileName(strPdf, nPage0 + 1, "CMYK"));
// 		if (SaveCMYKPlanesTIFF(strFn, arrCMYK, nDpi, 0))
// 			++nSaved;

		++nSaved;

		// 8bit 预览副本: 生产 TIFF 是 1bit/2bit + CMYK(SEPARATED),
		// Windows 图片查看器打不开(GDI+/WIC 只支持 8bit 的 SEPARATED);
		// 专色单版是标准 1bpp 索引图能直接打开, 所以只给 CMYK 出预览。
		if (bWithPreview)
		{
			const std::string strFnPrev = RIPExt::JoinPath(strOutDir,
				RIPExt::PlaneFileName(strPdf, nPage0 + 1, "CMYK_preview"));
			// 预览优先用「抖动前」的 8bit 连续调: 所见即所得,
			// 极浅色(米色圆底/浅灰高光)不会因半调稀疏网点在缩小预览时看似整块消失。
			// 没有连续调数据(异常路径)时退回展开抖动后的位深档位。
			bool bPrev = false;
			if (!result.m_vecPreviewCMYK.empty())
			{
				// 大页预览是按整数倍盒式降采样后的图, 尺寸与 DPI 同步换算,
				// 这样预览的物理尺寸与生产输出一致(不会显得被裁剪)
				const int nPvW = (result.m_nPreviewW > 0) ? result.m_nPreviewW : result.m_nWidth;
				const int nPvH = (result.m_nPreviewH > 0) ? result.m_nPreviewH : result.m_nHeight;
				const int nScale = (result.m_nPreviewScale > 1) ? result.m_nPreviewScale : 1;
				int nPvDpi = (int)((double)nDpi / nScale + 0.5);
				if (nPvDpi < 1) nPvDpi = 1;
				bPrev = SaveMergedCMYKToTIFF(strFnPrev, result.m_vecPreviewCMYK.data(),
											 nPvW, nPvH, nPvDpi);
			}
			else
			{
				bPrev = SaveCMYKPlanesTIFF(strFnPrev, arrCMYK, nDpi, 8);
			}
			if (bPrev)
				++nSaved;
		}
	}

	// 专色版: 每个专色各出一个文件
	for (size_t i = 4; i < result.m_vecPlanes.size(); ++i)
	{
		const std::string strInk = (i < result.m_vecInkNames.size())
			? result.m_vecInkNames[i] : std::string("Spot");
		const std::string strFn = RIPExt::JoinPath(strOutDir,
			RIPExt::PlaneFileName(strPdf, nPage0 + 1, strInk));
		if (SavePlaneBilevelTIFF(strFn, result.m_vecPlanes[i], nDpi, 0))
			++nSaved;
	}

	return nSaved;
}

// ============================================================================
// 页级任务线程池 RIPPool
// ============================================================================
namespace
{
	// 传给工作线程的参数（每个线程一份，必须活到线程结束）
	// 给系统与其它进程留的固定内存余量(不会分配给 RIP 线程池)
	const size_t kMemoryReserveBytes = 768u * 1024u * 1024u;
	// 最多只按可用内存的这个比例估线程数(留出峰值抖动空间)
	const double kMemoryUseRatio = 0.6;

	struct RipPoolArg
	{
		RIPipeline*							m_pPipe;		// 本线程私有的流水线
		const std::vector<RIPPool::PageTask>* m_pTasks;
		RIPPool::TaskProc					m_pfnTask;
		void*								m_pCtx;
		const volatile bool*				m_pbCancel;
		volatile LONG*						m_plNextTask;	// 全局任务取号器
		int									m_nThreadIdx;
	};

	unsigned int __stdcall RipPoolThreadProc(void* pArg)
	{
		RipPoolArg* p = static_cast<RipPoolArg*>(pArg);
		if (!p || !p->m_pPipe || !p->m_pTasks || !p->m_pfnTask)
			return 0;

		for (;;)
		{
			// 取号前先看取消: 取消后不再取新任务
			if (p->m_pbCancel && *p->m_pbCancel)
				break;

			const LONG n = ::InterlockedIncrement(p->m_plNextTask) - 1;
			if (n >= (LONG)p->m_pTasks->size())
				break;						// 任务取完

			// 取到号之后再看一次, 避免取消瞬间又多跑一页
			if (p->m_pbCancel && *p->m_pbCancel)
				break;

			p->m_pfnTask(p->m_pCtx, *p->m_pPipe, (int)n, p->m_nThreadIdx);
		}
		return 0;
	}
}

// ----------------------------------------------------------------------------
// 单页峰值内存估算 + 按可用内存钳线程数
// ----------------------------------------------------------------------------
size_t RIPPool::EstimatePageBytes(long long nPixels)
{
	if (nPixels <= 0)
		return 0;   // 无法估算: 让调用方自己决定
	return (size_t)((double)nPixels * kPeakBytesPerPixel) + kThreadFixedBytes;
}

int RIPPool::ClampThreadsByMemory(int nWant, size_t nBytesPerThread,
								  int* pnAvailMB, int* pnMaxByMem)
{
	int n = (nWant <= 0) ? AutoThreadCount() : nWant;
	if (n < 1) n = 1;
	if (nBytesPerThread == 0)
		return n;   // 调用方没给预估, 不做限制

	MEMORYSTATUSEX ms;
	memset(&ms, 0, sizeof(ms));
	ms.dwLength = sizeof(ms);
	if (!::GlobalMemoryStatusEx(&ms))
		return n;

	// 真正的瓶颈通常是「提交(Commit)上限」而不是物理内存, 两者取小
	unsigned long long avail = ms.ullAvailPhys;
	if (ms.ullAvailPageFile > 0 && ms.ullAvailPageFile < avail)
		avail = ms.ullAvailPageFile;

	if (pnAvailMB)
		*pnAvailMB = (int)(avail / 1048576u);

	const double fBudget = (double)avail * kMemoryUseRatio
						 - (double)kMemoryReserveBytes;
	if (fBudget <= 0.0)
	{
		if (pnMaxByMem) *pnMaxByMem = 1;
		return 1;   // 内存极度紧张: 退回单线程(峰值最小, 不会崩)
	}

	int nMax = (int)(fBudget / (double)nBytesPerThread);
	if (nMax < 1) nMax = 1;
	if (pnMaxByMem)
		*pnMaxByMem = nMax;
	if (n <= nMax)
		return n;   // 期望值本来就安全, 不动

	return nMax;
}

int RIPPool::AutoThreadCount()
{
	SYSTEM_INFO si;
	::GetSystemInfo(&si);
	int n = (int)si.dwNumberOfProcessors;
	if (n < 1) n = 1;
	if (n > kMaxAutoThreads) n = kMaxAutoThreads;
	return n;
}

int RIPPool::Run(const std::string& strAppDir, int nThreads,
				 const std::vector<PageTask>& vecTasks,
				 TaskProc pfnTask, void* pCtx,
				 const volatile bool* pbCancel,
				 size_t nBytesPerThread)
{
	if (vecTasks.empty() || !pfnTask)
		return 0;

	int nWant = (nThreads <= 0) ? AutoThreadCount() : nThreads;
	if (nWant < 1) nWant = 1;
	// 安全网: 调用方给了每线程内存预估就再钳一次, 忘记钳也不会崩
	nWant = ClampThreadsByMemory(nWant, nBytesPerThread);
	if (nWant > (int)vecTasks.size()) nWant = (int)vecTasks.size();
	if (nWant > 32) nWant = 32;

	volatile LONG lNext = 0;

	// 每线程一份流水线: MuPDF context / lcms 变换 / ICC 缓冲都是线程私有的
	std::vector<RIPipeline*> vecPipes(nWant, (RIPipeline*)nullptr);
	std::vector<RipPoolArg>  vecArgs(nWant);
	std::vector<HANDLE>      vecThreads;
	vecThreads.reserve(nWant);

	for (int i = 0; i < nWant; ++i)
	{
		vecPipes[i] = new RIPipeline;
		vecPipes[i]->SetAppDir(strAppDir);
		if (nWant > 1)
			vecPipes[i]->SetStoreBytes(kStoreBytesMultiThread);

		RipPoolArg& a = vecArgs[i];
		a.m_pPipe = vecPipes[i];
		a.m_pTasks = &vecTasks;
		a.m_pfnTask = pfnTask;
		a.m_pCtx = pCtx;
		a.m_pbCancel = pbCancel;
		a.m_plNextTask = &lNext;
		a.m_nThreadIdx = i;

		unsigned int nTid = 0;
		HANDLE h = (HANDLE)_beginthreadex(nullptr, 0, RipPoolThreadProc, &a, 0, &nTid);
		if (h)
			vecThreads.push_back(h);
	}

	const int nStarted = (int)vecThreads.size();

	// 等所有线程退出再释放参数与流水线（vecArgs/vecPipes 必须活到线程结束）
	for (size_t i = 0; i < vecThreads.size(); ++i)
	{
		::WaitForSingleObject(vecThreads[i], INFINITE);
		::CloseHandle(vecThreads[i]);
	}
	for (size_t i = 0; i < vecPipes.size(); ++i)
		delete vecPipes[i];

	// nStarted 可能小于 nWant(线程创建失败)，但只要 >= 1，
	// 取号器会把剩下的任务全部派给已启动的线程，不会漏任务。
	return nStarted;
}
