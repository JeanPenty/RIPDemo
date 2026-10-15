#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ============================================================================
// RIP 参数与结果，全部可独立修改，最终交给 RIPipeline 消费。
// 本头文件刻意不引用 SOUI / MuPDF / lcms，方便单独编译与自测。
// ============================================================================
struct RIPParams
{
	// ---- 输出分辨率（像素/英寸）----
	int    m_nXDpi = 300;
	int    m_nYDpi = 300;

	// ---- 输出位深：1 或 2 ----
	int    m_nBitDepth = 1;

	// ---- 并行线程数（页级并行）----
	// 0 = 自动（按 CPU 核心数，上限 RIPPool::kMaxAutoThreads）
	// 1 = 串行；实际上限 32，且会被待处理页数收敛
	// 说明: 每个工作线程持有一份独立的 RIPipeline（含 MuPDF context 与图像缓存），
	//       线程越多内存占用越高，页数很少时也没必要开满。
	int    m_nThreadCount = 0;

	// ---- CMYK 阈值：低于该分量的分色不再出墨（0~255），用于抑制浅色脏点墨 ----
	int    m_nThrC = 0;
	int    m_nThrM = 0;
	int    m_nThrY = 0;
	int    m_nThrK = 0;

	// ---- 专色处理方式 ----
	// false（默认）: 不解析专色图层，专色按其等效色合成进 CMYK，只输出 C/M/Y/K 四个色版
	// true         : 解析 PDF 里的专色（Separation / DeviceN）图层，
	//                每个专色作为独立色版输出（C/M/Y/K + N 个专色版）
	bool   m_bSpotSeparate = false;

	// ---- ICC 曲线 ----
	// 空字符串表示使用程序目录下的默认曲线（随程序携带的 sRGB / CMYK ICC）
	std::string m_strInputICC;   // 输入（RGB）曲线，一般留空走内置 sRGB
	std::string m_strCMYKICC;    // 目标 CMYK 曲线，对应界面上的 "ICC曲线(CMYK分色曲线)"

	// ---- 输出根目录 ----
	// 空 = 跟随 PDF 所在目录(老行为): <PDF目录>\RIP_Out\<PDF文件名>\
	// 非空 = 所有输出集中到 <输出根目录>\RIP_Out\<PDF文件名>\
	// 用途: 大作业动辄几十 GB, PDF 放在 C 盘时输出会写满系统盘。
	std::string m_strOutputRoot;

	// ---- 页面缩放 ----
	// 0 表示按 DPI 自然缩放；>0 时把页面长边缩放到该像素数（用于控大小）
	int    m_nScaleToLongSide = 0;

	// ---- 抖动算法 ----
	// DitherMode(): 0 = 纯阈值二值化(不出网)
	//               1 = 有序网屏(4x4 Bayer, 网点稳定规整)
	//               2 = FS 误差扩散(Floyd-Steinberg, 默认) —— 层次最细腻
	// 说明: m_nDitherMode 只区分「是否 FS」; 非 FS 时由 m_bEnableScreen 决定
	//       走有序网屏还是纯阈值(沿用老配置里的 screen_enable 键)。
	int    m_nDitherMode = 2;
	bool   m_bFSSerpentine = true;     // FS 蛇形扫描(逐行来回走), 消除单向拖尾纹理

	// ---- 加网 ----
	bool   m_bEnableScreen = true;
	bool   m_bRotateScreenY = false;   // 每列相位错开，肉眼更细腻

	// ---- 灰色只用 K 表达(剔除 CMY) ----
	// 黑字/灰块经 ICC 分色后常变成「四色黑」, 四个色版各打各的网点,
	// 边缘相位不同, 打印出来黑字周围有彩色毛边。开启后: C/M/Y 三分量
	// 差值不超过 m_nGrayTol 的像素视为中性色, 灰量并入 K 版、CMY 清零,
	// 黑就归黑, 顺带省墨。图片类彩色内容不受影响(差值大不会被当作灰)。
	bool   m_bGrayToK = true;
	int    m_nGrayTol = 48;        // 0~255, 越大越激进(更多接近中性的颜色被转成 K)

	// ---- 去除文字抗锯齿(硬边缘, 消毛边) ----
	// 对 CMYK 各版墨量做 S 曲线: 低于下限归 0、高于上限归 255、中间线性拉伸,
	// 文字边缘的过渡灰阶被压掉, 1bit/2bit 输出边缘干净利落。图片类内容建议关闭。
	// ---- 输出 PRN(喷头板卡容器) ----
	// 0 = 不输出; 1 = PRN1RipHeader(256 字节头, 逐行 K,C,M,Y,[S..] 交织);
	// 2 = PRN2RipHeader(48 字节头, 逐行 C,M,Y,K 交织)。
	// 两者都是 1 = 出墨, 与 TIFF 位平面一致; 每页落一份 <stem>_pNNNN_CMYK.prn。
	int    m_nPrnVersion = 0;

	// ---- 墨点统计(多页 PDF 只统计第 1 页) ----
	// 按像素级别统计: 每个出墨像素就是一发墨滴。
	//   1bit: 出墨=1, 全部记成大点(打点数=出墨像素数)
	//   2bit: 像素值即墨滴档位 1=小点 2=中点 3=大点
	// 结果在 RIPPageResult::m_vecDotStats 里, 界面按「文件」展示。
	bool   m_bDotStat = true;      // 关掉可省一点统计时间
	int    m_nDotSmallUM = 100;    // (已废弃, 旧版按 um 分档的参数, 保留兼容 ini)
	int    m_nDotMidUM = 200;      // (已废弃, 同上)

	// 每颗墨滴的体积(皮升 pL): 用来把点数换算成耗墨量
	//   耗墨量(pL) = 小点数*小点pL + 中点数*中点pL + 大点数*大点pL
	// 按实际喷头墨滴规格填(常见: 小点 1.5~4pL, 中点 5~9pL, 大点 12~30pL)
	double m_dDropSmallPL = 2.0;
	double m_dDropMidPL   = 6.0;
	double m_dDropBigPL   = 12.0;

	// 墨水单价(元 / 毫升): 用总耗墨量算印刷成本
	//   成本(元) = 总耗墨量(mL) * 单价(元/mL)
	// 只为界面估算, 不参与 RIP 运算; 填 0 表示不统计成本。
	double m_dInkPricePerML = 0.0;

	bool   m_bNoAA = false;
	int    m_nNoAAStrength = 50;   // 0~100, 100 = 完全硬切

	// ---- 中间产物（调试用，默认关）----
	bool   m_bDumpMergedCMYK = false;
	std::string m_strDumpDir;

	// ---- 作业列表(左侧列表) ----
	// 退出/参数变化时把左侧作业列表存下来, 下次启动按它重建(含勾选状态
	//   与第 1 页墨点统计 -> 重启后选中文件仍能直接算耗墨量/成本, 不必重跑 RIP)。
	// 每项格式(除路径外全是 ASCII):
	//   "<0|1>|<路径>|<dpi>|<bit>|<色版数>|<色版统计;...>"
	//   色版统计 = "<色版名hex>:小点:中点:大点:总点:出墨像素:最大面积"
	// '|' 不是 Windows 路径的合法字符, 可以安全当分隔符;
	// 色版名可能是任意字节的专色名, 所以做十六进制编码避开分隔符。
	// 兼容老配置: 只有前 2 段时视为「无统计」。顺序即列表顺序。
	std::vector<std::string> m_vecTaskList;

	// 阈值统一压到 [0,255]
	inline int ThrC() const { return Clamp255(m_nThrC); }
	inline int ThrM() const { return Clamp255(m_nThrM); }
	inline int ThrY() const { return Clamp255(m_nThrY); }
	inline int ThrK() const { return Clamp255(m_nThrK); }
	inline int GrayTol() const { return Clamp255(m_nGrayTol); }
	inline int DitherMode() const { return m_nDitherMode == 2 ? 2 : (m_bEnableScreen ? 1 : 0); }
	inline int NoAAStrength() const { return m_nNoAAStrength < 0 ? 0 : (m_nNoAAStrength > 100 ? 100 : m_nNoAAStrength); }
	inline int BitDepth() const { return m_nBitDepth == 2 ? 2 : 1; }
	inline int ThreadCount() const { return m_nThreadCount < 0 ? 0 : (m_nThreadCount > 32 ? 32 : m_nThreadCount); }
	inline double DropSmallPL() const { return m_dDropSmallPL < 0.0 ? 0.0 : m_dDropSmallPL; }
	inline double DropMidPL() const { return m_dDropMidPL < 0.0 ? 0.0 : m_dDropMidPL; }
	inline double DropBigPL() const { return m_dDropBigPL < 0.0 ? 0.0 : m_dDropBigPL; }
	inline double InkPricePerML() const { return m_dInkPricePerML < 0.0 ? 0.0 : m_dInkPricePerML; }
	inline int XDpi() const { return m_nXDpi < 1 ? 1 : m_nXDpi; }
	inline int YDpi() const { return m_nYDpi < 1 ? 1 : m_nYDpi; }

	static inline int Clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
};

// ----------------------------------------------------------------------------
// 页面上的一个分色（专色）描述
//   m_nBehavior: 0=Composite(按等效色合成到 CMYK) 1=Spot(独立色版) 2=Disabled
// ----------------------------------------------------------------------------
struct RIPSepInfo
{
	std::string m_strName;		// 分色名，如 "Gold" / "Varnish"
	int m_nBehavior = 0;		// fz_separation_behavior
};

// ----------------------------------------------------------------------------
// 一个待输出色版的连续 8bit 灰度数据（w*h，0=无墨 255=满墨）
// ----------------------------------------------------------------------------
struct RIPGrayPlane
{
	std::string m_strName;					// 色版名，如 "C" / "K" / "Gold"
	std::vector<unsigned char> m_vecGray;	// w*h
	int m_nThreshold = 0;					// 该色版的分量阈值
};

// ----------------------------------------------------------------------------
// 一个分色平面（一个 ink channel）的位图结果
//   m_vecBits 按 packed 位存储，m_nStride = 8 * bytesPerLine / bit
//   bit=1 -> 1 bit/px；bit=2 -> 2 bit/px，每像素 2bit 占一个位槽的 2 位
// ----------------------------------------------------------------------------
struct RIPPlane
{
	std::vector<unsigned char> m_vecBits;   // 打包后的位图
	int m_nWidth = 0;
	int m_nHeight = 0;
	int m_nBit = 1;
	int m_nStride = 0;                       // 每行字节数
	int m_nBitsPerLine = 0;                  // = m_nStride * 8

	inline uint8_t GetPixel(int x, int y) const
	{
		const unsigned char* row = m_vecBits.data() + (size_t)y * m_nStride;
		if (m_nBit == 1)
			return (row[x >> 3] >> (7 - (x & 7))) & 0x01;
		const int byteIdx = x >> 2;
		const int shift = 6 - ((x & 3) << 1);
		return (row[byteIdx] >> shift) & 0x03;
	}
};

// ----------------------------------------------------------------------------
// 一次 RIP 的结果：4 个 CMYK 平面；专色分离时后面再追加 N 个专色平面
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// 一个色版的「墨点」统计(按像素级别: 1bit 全记大点; 2bit L1/L2/L3 = 小/中/大点)
// ----------------------------------------------------------------------------
struct RIPDotStat
{
	std::string m_strInk;            // 色版名: C / M / Y / K / 专色名
	long long m_nDotsSmall = 0;      // 小点个数
	long long m_nDotsMid = 0;        // 中点个数
	long long m_nDotsBig = 0;        // 大点个数
	long long m_nDots = 0;           // 总点数 = 三档之和
	long long m_nInkPixels = 0;      // 出墨像素数(像素口径下 = 总点数)
	long long m_nMaxArea = 0;        // (已废弃, 像素口径下恒为 0; 字段保留兼容旧 ini)
};

// 由「点数 x 单颗墨滴体积」算耗墨量(皮升 pL)
//   1 pL = 1e-9 mL = 1e-6 uL; 例: 4000 pL = 4 nL = 0.004 uL
inline double RIPDotsToPL(const RIPDotStat& d, double dSmallPL, double dMidPL, double dBigPL)
{
	return (double)d.m_nDotsSmall * dSmallPL
	     + (double)d.m_nDotsMid   * dMidPL
	     + (double)d.m_nDotsBig   * dBigPL;
}

// 皮升 -> 毫升
//   1 L = 1e15 pL, 1 mL = 1e-3 L = 1e12 pL  =>  1 pL = 1e-9 mL
inline double RIPPLToML(double dPL) { return dPL * 1e-9; }

// ----------------------------------------------------------------------------
// 作业列表项在 ini 里的编码 / 解析
//   格式(除路径外全是 ASCII):
//     "<0|1>|<路径>|<dpi>|<bit>|<色版数>|<色版统计;...>"
//   色版统计 = "<色版名hex>:<小点>:<中点>:<大点>:<总点>:<出墨像素>:<最大面积>"
//   色版名可能是任意字节的专色名, 做十六进制编码避开 '|' ';' ':' 串扰。
//   只有前 2 段的老配置视为「无统计」(向后兼容)。
// ----------------------------------------------------------------------------
namespace RIPTaskEntry
{
	inline std::string HexEncode(const std::string& s)
	{
		static const char* kHex = "0123456789ABCDEF";
		std::string r;
		r.reserve(s.size() * 2);
		for (size_t i = 0; i < s.size(); ++i)
		{
			const unsigned char c = (unsigned char)s[i];
			r.push_back(kHex[c >> 4]);
			r.push_back(kHex[c & 0x0F]);
		}
		return r;
	}

	inline int HexVal(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}

	inline std::string HexDecode(const std::string& s)
	{
		std::string r;
		r.reserve(s.size() / 2);
		for (size_t i = 0; i + 1 < s.size(); i += 2)
		{
			const int a = HexVal(s[i]), b = HexVal(s[i + 1]);
			if (a < 0 || b < 0) break;
			r.push_back((char)((a << 4) | b));
		}
		return r;
	}

	inline long long ToLL(const std::string& s)
	{
		long long v = 0;
		bool bNeg = false;
		size_t i = 0;
		if (i < s.size() && (s[i] == '-' || s[i] == '+')) { bNeg = (s[i] == '-'); ++i; }
		for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) v = v * 10 + (s[i] - '0');
		return bNeg ? -v : v;
	}

	// 按分隔符切段(不跳过空段)
	inline std::vector<std::string> Split(const std::string& s, char ch)
	{
		std::vector<std::string> r;
		for (size_t p = 0; p <= s.size(); )
		{
			const size_t q = s.find(ch, p);
			if (q == std::string::npos) { r.push_back(s.substr(p)); break; }
			r.push_back(s.substr(p, q - p));
			p = q + 1;
		}
		return r;
	}

	// 一个作业项 -> ini 里的一行
	inline std::string Encode(bool bChecked, const std::string& strPathUtf8,
	                          int nDpi, int nBit, const std::vector<RIPDotStat>& vecStats)
	{
		std::string s = bChecked ? "1|" : "0|";
		s += strPathUtf8;
		s += "|" + std::to_string((long long)nDpi);
		s += "|" + std::to_string((long long)nBit);
		s += "|" + std::to_string((long long)vecStats.size());
		s += "|";
		for (size_t k = 0; k < vecStats.size(); ++k)
		{
			const RIPDotStat& t = vecStats[k];
			if (k) s += ';';
			s += HexEncode(t.m_strInk);
			s += ":" + std::to_string(t.m_nDotsSmall);
			s += ":" + std::to_string(t.m_nDotsMid);
			s += ":" + std::to_string(t.m_nDotsBig);
			s += ":" + std::to_string(t.m_nDots);
			s += ":" + std::to_string(t.m_nInkPixels);
			s += ":" + std::to_string(t.m_nMaxArea);
		}
		return s;
	}

	// ini 里的一行 -> 作业项(老格式时 vecStats 为空)。返回 false = 这行无效
	inline bool Decode(const std::string& s, bool& bChecked, std::string& strPathUtf8,
	                   int& nDpi, int& nBit, std::vector<RIPDotStat>& vecStats)
	{
		bChecked = true; strPathUtf8.clear(); nDpi = 0; nBit = 1; vecStats.clear();
		if (s.empty()) return false;
		if (s[0] == '0' || s[0] == '1') bChecked = (s[0] != '0');

		const std::vector<std::string> f = Split(s, '|');
		if (f.size() < 2) return false;
		strPathUtf8 = f[1];
		if (f.size() < 6) return true;			// 老格式: 只有勾选 + 路径

		nDpi = (int)ToLL(f[2]);
		nBit = (ToLL(f[3]) == 2) ? 2 : 1;

		const std::vector<std::string> inks = Split(f[5], ';');
		for (size_t k = 0; k < inks.size(); ++k)
		{
			const std::vector<std::string> g = Split(inks[k], ':');
			if (g.size() != 7) continue;
			RIPDotStat t;
			t.m_strInk = HexDecode(g[0]);
			t.m_nDotsSmall = ToLL(g[1]);
			t.m_nDotsMid = ToLL(g[2]);
			t.m_nDotsBig = ToLL(g[3]);
			t.m_nDots = ToLL(g[4]);
			t.m_nInkPixels = ToLL(g[5]);
			t.m_nMaxArea = ToLL(g[6]);
			vecStats.push_back(t);
		}
		return true;
	}
}

struct RIPPageResult
{
	std::vector<RIPPlane> m_vecPlanes;        // 顺序: C M Y K [Spot...]
	std::vector<std::string> m_vecInkNames;   // 与 m_vecPlanes 一一对应的色版名
	std::vector<float> m_vecInkPercent;       // 各色版平均墨量(0~1)，用于界面显示
	int   m_nWidth = 0;
	int   m_nHeight = 0;
	int   m_nBit = 1;
	double m_nRenderMS = 0.0;                 // 渲染耗时(ms)
	double m_nSepMS = 0.0;                    // 分色耗时(ms)
	// 抖动前的 8bit 连续调 CMYK(交织 C,M,Y,K), 仅供生成预览。
	// 预览若直接展开抖动后的位深档位, 极浅色(米色圆底/浅灰高光, 墨量只有 2~6%)
	// 会表现成稀疏网点, 缩小看就是「整块消失」; 连续调预览才是所见即所得。
	std::vector<unsigned char> m_vecPreviewCMYK;
	// 预览实际尺寸与降采样倍率: 超大页(600DPI 上亿像素)预览会按整数倍盒式
	// 降采样, 否则光预览就要几 GB。倍率 1 表示 1:1, 与老版本完全一致。
	int   m_nPreviewW = 0;                    // 0 = 与 m_nWidth 相同
	int   m_nPreviewH = 0;                    // 0 = 与 m_nHeight 相同
	int   m_nPreviewScale = 1;                // 整数倍降采样倍率
	double m_nRipMS = 0.0;                    // 阈值/加网/打包耗时(ms)
	// 墨点统计(与 m_vecPlanes 一一对应; 未统计或非第 1 页时为空)
	std::vector<RIPDotStat> m_vecDotStats;
};
