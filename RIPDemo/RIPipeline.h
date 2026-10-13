#pragma once
#include "RIPParams.h"

// ============================================================================
// RIPipeline —— PDF 页面 -> 位图 -> CMYK 分色（含专色分离） -> 1bit/2bit 半色调
//
// 依赖: MuPDF(渲染) + lcms2(ICC 色彩变换)
//
// 单页调用无状态、可重入（lcms 变换句柄按 ICC 缓存），
// 且每实例持有独立的 MuPDF context（fz_context 不是线程安全的），
// 因此可以「一个线程一个 RIPipeline」并行跑多页 —— 见下方 RIPPool。
// ============================================================================
class RIPipeline
{
public:
	RIPipeline();
	~RIPipeline();

	// 设置程序目录，用于定位随程序携带的默认 ICC 文件
	void SetAppDir(const std::string& strAppDir) { m_strAppDir = strAppDir; }

	// MuPDF 图像缓存的字节上限（0 = 用 MuPDF 默认值）。
	// 多线程并行时每个线程一份 context，各自缓存，总内存会成倍上涨，
	// 所以线程池会显式调小一点（见 RIPPool::Run 的 kStoreBytesMultiThread）。
	void SetStoreBytes(size_t nStoreBytes) { m_nStoreBytes = nStoreBytes; }

	// 最近一次失败原因
	const std::string& LastError() const { return m_strErr; }

	// 取 PDF 总页数（失败返回 0）
	int CountPages(const std::string& strPdfPath);

	// 查询第 nPage 页用到的分色（专色）列表；无专色时 out 为空且返回 true
	bool QueryPageSeparations(const std::string& strPdfPath, int nPage,
							  std::vector<RIPSepInfo>& out);

	// 取第 nPage 页的原始尺寸（pt），用于界面预估输出大小
	bool QueryPageSize(const std::string& strPdfPath, int nPage,
					   int& nW, int& nH, double& fPageWpt, double& fPageHpt);

	// 核心: 把 PDF 第 nPage 页 RIP 成 1bit/2bit 分色平面
	//   params.m_bSpotSeparate = false: 不解析专色图层，合成后按 CMYK 提取（4 个平面）
	//   params.m_bSpotSeparate = true : 解析专色图层并各自独立成版（4 + N 个平面）
	//   pbCancel 可选，置 true 可尽早中止当前页（MuPDF cookie）
	bool RunPage(const std::string& strPdfPath,
				 int nPage,
				 const RIPParams& params,
				 RIPPageResult& out,
				 const volatile bool* pbCancel = nullptr);

	// 把一页的 RIP 结果按生产规则落盘（GUI 与 CLI 共用同一套规则）:
	//   C/M/Y/K 合并写成一个多平面 TIFF（原生 1bit/2bit 位打包）
	//   bWithPreview = true 时额外出 8bit 预览副本（图片查看器可直接打开）
	//   专色各出一个独立文件
	//   返回成功写出的文件数
	static int SavePagePlanes(const std::string& strPdf, int nPage0,
							  const std::string& strOutDir, int nDpi,
							  const RIPPageResult& result, bool bWithPreview = true,
							  int nPrnVersion = 0);

	// 中间产物: 把单一平面按 8bit 灰度 TIFF 落盘（观察可读，调试用）
	// 按 PRN1RipHeader 结构把各色版写成 .prn(供 BYHX 板卡读取)
	//   位平面顺序(逐行交织): K, C, M, Y, S1, S2 ...
	//   1 = 出墨, 与 RIPPlane 一致; 行尾按 4 字节对齐补 0
	//   bMatch8ColorQuirk: 8 色时按「K,C,M,Y,S4,S1,S2,S3」写(与现有解析端一致)
	static bool SavePlanesPRN(const std::string& strPath,
							  const std::vector<RIPPlane>& vecPlanes,
							  int nWidth, int nHeight, int nBit,
							  int nXDpi, int nYDpi, int nCopies = 1,
							  bool bMatch8ColorQuirk = true);

	// PRN2RipHeader(48 字节头 = 12 个 long, 标识 1381564416):
	//   逐行按 C, M, Y, K[, S1..] 交织写位平面(与 RIPPageResult 顺序一致),
	//   行字节数按 4 字节对齐; 1 = 出墨, 与 TIFF 位平面一致。
	static bool SavePlanesPRN2(const std::string& strPath,
							   const std::vector<RIPPlane>& vecPlanes,
							   int nWidth, int nHeight, int nBit,
							   int nXDpi, int nYDpi, int nCopies = 1);

	static bool SaveMergedCMYKToTIFF(const std::string& strPath,
									 const unsigned char* pCMYK, int w, int h, int nDpi);

	// 中间产物: 把单个平面按 8bit 灰度 TIFF 存盘（人眼可读，调试用）
	static bool SavePlanePreviewTIFF(const std::string& strPath, const RIPPlane& plane, int nDpi);

	// 生产用: 把单个平面按原生 1bit / 2bit 打包 TIFF 存盘
	//   photometric: 0 = MINISWHITE(0 表示白), 1 = MINISBLACK(0 表示黑)
	static bool SavePlaneBilevelTIFF(const std::string& strPath, const RIPPlane& plane,
								 int nDpi, int photometric = 0);

	// 把 C/M/Y/K 四个色版合并写成一个多平面 TIFF（不再按色版分开存储）
	//   SamplesPerPixel=4、PlanarConfig=SEPARATE（每通道一条独立位平面）
	//   Photometric=SEPARATED(CMYK)、0 = 不出墨、最大 = 满墨
	//   nForceBits = 0 : 生产用，位深沿用各色版自身的 1bit/2bit，行按位打包，
	//                    排布与单通道文件完全一致，便于按通道直接喂给板卡
	//   nForceBits = 8 : 预览用，把位打包数据展开成 8bit/样本，可直接看图
	//   要求四个色版的宽高一致（nForceBits = 0 时还要求位深一致）
	static bool SaveCMYKPlanesTIFF(const std::string& strPath,
								   const RIPPlane* arrPlanes[4],
								   int nDpi, int nForceBits = 0);

private:
	// 色彩分离 / 抖动都在 RIPipeline.cpp 里按「行带」实现(RunPage 内部),
	// 单页峰值内存与页面积无关, 600DPI 超大页也能跑; 详见 RIPipeline.cpp。

	// lcms 变换缓存
	void* GetRgb2CmykTransform(const RIPParams& params);
	void  ReleaseTransformCache();

	void* m_pTransform = nullptr;       // cmsHTRANSFORM
	std::string m_strCurCmykICC;        // 当前变换对应的 ICC 路径
	// ICC 以内存方式打开（lcms 的 *_from_file 不认中文路径），缓冲需保持存活
	std::vector<unsigned char> m_vecInProfileBuf;
	std::vector<unsigned char> m_vecOutProfileBuf;

	// MuPDF 上下文（实例首次使用时惰性创建，随实例析构释放）
	// 注意: fz_context 不是线程安全的, 一个 RIPipeline 只能被一个线程使用。
	void* EnsureContext();

	std::string m_strAppDir;
	std::string m_strErr;

	void*  m_pCtx = nullptr;       // fz_context*（每实例一份）
	size_t m_nStoreBytes = 0;      // 0 = FZ_STORE_DEFAULT
};

// ============================================================================
// 路径 / 文件 / 默认 ICC 工具
// ============================================================================
namespace RIPExt {

	std::string FindDefaultCMYKICC(const std::string& appDir);
	std::string FindDefaultRGBICC(const std::string& appDir);

	std::string WStringToUtf8(const std::wstring& w);
	std::wstring Utf8ToWString(const std::string& s);
	bool FileExistsA(const std::string& path);

	// 由 PDF 路径生成输出目录（RIP_Out 子目录），多级创建
	//   outputRoot 非空: <outputRoot>\RIP_Out\<PDF文件名\>
	//   outputRoot 为空: <PDF所在目录>\RIP_Out\<PDF文件名\>  (老行为)
	std::string MakeOutputDir(const std::string& pdfPath,
							  const std::string& outputRoot = std::string());
	std::string JoinPath(const std::string& a, const std::string& b);

	// 确保目录存在（UTF-8 路径，逐级创建）；已存在返回 true
	bool EnsureDirExists(const std::string& strDir);

	// 把分色名净化成可用作文件名的形式
	std::string SanitizeInkName(const std::string& strInk);

	// 位图文件名: <base>_p0001_C.tif
	std::string PlaneFileName(const std::string& pdfPath, int nPage, const std::string& strInk);
}

// ============================================================================
// 页级任务线程池
//
//   RIP 是「一页一页独立」的：每页自己渲染、自己分色、自己落盘到独立文件，
//   页与页之间没有任何共享数据。所以把「文件 x 页」拆成任务丢进线程池，
//   单文件多页也能并行（只按文件并行时单文件任务会退化成串行）。
//
//   每个工作线程持有自己的一份 RIPipeline（MuPDF context / lcms 变换 /
//   ICC 缓冲都是实例私有的），线程之间不共享任何状态，因此不需要加锁。
// ============================================================================
namespace RIPPool
{
	// 自动模式（nThreads <= 0）的线程数上限
	static const int    kMaxAutoThreads = 8;
	// 多线程时每个线程的 MuPDF 缓存上限，避免 N 份 256MB 缓存把内存吃光
	static const size_t kStoreBytesMultiThread = 64u * 1024u * 1024u;
	// 每线程固定内存开销(MuPDF store 之外的部分: lcms 变换/工作缓冲等)
	static const size_t kThreadFixedBytes = 128u * 1024u * 1024u;
	// 单页峰值内存估算的系数(字节/像素): 渲染 3 + 灰度 4 + 预览 4
	static const double kPeakBytesPerPixel = 1.5;

	// 一页的处理任务
	struct PageTask
	{
		int			m_nGroup = 0;		// 所属文件下标（调用方自定义，回调里用）
		int			m_nPage = 0;		// 0 基页号
		std::string	m_strPdf;			// 输入 PDF（UTF-8）
		std::string	m_strOutDir;		// 输出目录（UTF-8）

		PageTask() {}
		PageTask(int nGroup, int nPage, const std::string& strPdf, const std::string& strOutDir)
			: m_nGroup(nGroup), m_nPage(nPage), m_strPdf(strPdf), m_strOutDir(strOutDir) {}
	};

	// 任务回调：在 nThreadIdx 号线程上处理 vecTasks[nTaskIdx]
	//   pipe 是该线程私有的流水线实例，可以一直复用
	typedef void (*TaskProc)(void* pCtx, RIPipeline& pipe, int nTaskIdx, int nThreadIdx);

	// 取「自动模式」线程数：CPU 核心数，上限 kMaxAutoThreads
	int AutoThreadCount();

	// ---- 内存感知: 防止「大页 x 多线程」把内存吃光导致崩溃 ----
	// 估算单页处理时的峰值内存(字节)。nPixels <= 0 时返回 0(无法估算)。
	size_t EstimatePageBytes(long long nPixels);

	// 按当前可用内存把线程数钳到安全值, 返回 >= 1 的线程数。
	//   nWant           : 期望线程数(<= 0 = 自动)
	//   nBytesPerThread : 每线程峰值内存预估, 0 = 不做限制
	//   pnAvailMB  : 可选, 输出当前可用内存(MB, 物理与提交上限取小)
	//   pnMaxByMem : 可选, 输出「只按内存算」允许的最大线程数
	// 注: 不返回文字, 由调用方自己拼文案(宽字符 printf 在 C locale 下
	//     遇到中文会截断输出)。
	int ClampThreadsByMemory(int nWant, size_t nBytesPerThread,
							 int* pnAvailMB = nullptr, int* pnMaxByMem = nullptr);

	// 并行执行 vecTasks，返回实际启动的线程数（0 = 一个线程都没起来）
	//   nThreads <= 0 : 自动（CPU 核心数，上限 kMaxAutoThreads）
	//   nThreads 会按任务数收敛，最终线程数 = min(nThreads, 任务数)
	//   nBytesPerThread: 每线程峰值内存预估(字节), 0 = 不做内存限制。
	//                   超出可用内存承受范围时自动减少线程数(安全网)。
	//   pbCancel      : 置 true 后各线程尽快退出，还没取号的任务不再执行
	//   即使只起来 1 个线程，剩余任务也会被它取完，不会漏任务
	int Run(const std::string& strAppDir, int nThreads,
			const std::vector<PageTask>& vecTasks,
			TaskProc pfnTask, void* pCtx,
			const volatile bool* pbCancel,
			size_t nBytesPerThread = 0);
}
