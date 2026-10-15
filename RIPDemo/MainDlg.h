// MainDlg.h : interface of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////
#pragma once

#include "CGlobalUnits.h"
#include "CLvRIPTaskAdapter.h"
#include "RIPipeline.h"

// ---- RIP 工作线程 -> UI 线程 的自定义消息 ----
// WM_RIP_ITEM_STATUS : wParam = 列表项下标, lParam = (RipNotify*) 堆对象, UI 线程接管并释放
// WM_RIP_ALL_DONE    : 整个队列处理结束(不论成败), wParam = 任务代号, lParam = 总耗时(ms),
//                      UI 线程回收线程句柄
// WM_RIP_PROGRESS    : 处理进度, wParam = 已完成任务(页)数, lParam = 总任务数
// WM_RIP_DOTSTAT     : 第 1 页的墨点统计结果, wParam = 任务代号,
//                      lParam = (RipDotStatNotify*) 堆对象, UI 线程接管并释放
#define WM_RIP_ITEM_STATUS	(WM_APP + 100)
#define WM_RIP_ALL_DONE		(WM_APP + 101)
#define WM_RIP_PROGRESS		(WM_APP + 102)
#define WM_RIP_DOTSTAT		(WM_APP + 103)

class CMainDlg;

// 一个输入 PDF 的聚合状态。
// 同一个文件的多页会被不同工作线程并行处理, 所以计数字段一律用 Interlocked* 原子操作。
struct RipFileEntry
{
	std::wstring	m_strFile;			// PDF 全路径
	std::string		m_strPdfUtf8;		// 同上, UTF-8(工作线程直接用)
	std::string		m_strOutDir;		// 输出目录(启动前算好并建好, 避免多线程重复建目录)
	int				m_nListIndex = -1;	// 列表项下标
	int				m_nPages = 0;		// 总页数(启动前扫描; 0 = 打不开)
	DWORD			m_dwStart = 0;		// 该文件第一页开始处理的时刻
	volatile LONG	m_lPagesDone = 0;	// 已完成页数
	volatile LONG	m_lPagesFailed = 0;	// 失败页数
	volatile LONG	m_lSaved = 0;		// 落盘文件数
	volatile LONG	m_lStarted = 0;		// 「处理中」状态只投递一次
	volatile LONG	m_lErrClaimed = 0;	// 失败原因只记第一条
	std::wstring	m_strErr;			// 首条失败原因
};

// 交给工作线程的任务包(协调线程负责 delete)
struct RipJob
{
	void*						m_pOwner = NULL;	// CMainDlg*
	HWND						m_hWndNotify = NULL;	// 状态消息投递目标
	int							m_nGen = -1;		// 本次任务代号(由 UI 线程分配)
	RIPParams					m_params;			// 本次的 RIP 参数快照
	std::string					m_strAppDir;		// 程序目录(工作线程建流水线用)
	std::vector<RipFileEntry>	m_vecFiles;			// 输入文件(只读, 多线程共享)
	std::vector<RIPPool::PageTask> m_vecTasks;		// 「文件 x 页」任务(只读, 多线程共享)
	int							m_nTotalTasks = 0;
	int							m_nThreads = 0;			// 实际使用线程数(已按可用内存钳过)
	size_t						m_nBytesPerThread = 0;	// 每线程峰值内存预估(字节), 0 = 未估算
	volatile LONG				m_lNextTask = 0;	// 原子任务取号器
	volatile LONG				m_lDone = 0;		// 已完成任务数(进度用)
	volatile LONG				m_lSaved = 0;		// 落盘文件总数
	DWORD						m_dwStart = 0;		// 整个队列的开始时刻
};

// 工作线程回传的单条任务状态
struct RipNotify
{
	int			m_nIndex;		// 列表项下标
	int			m_nStatus;		// 0等待 1处理中 2完成 3失败/已取消
	int			m_nGen;			// 任务代号, 上一代任务的迟到消息直接丢弃
	SStringW	m_sstrText;		// 状态文本

	RipNotify() : m_nIndex(-1), m_nStatus(0), m_nGen(-1) {}
};

// 工作线程回传的第 1 页墨点统计(多页 PDF 只算第 1 页)
struct RipDotStatNotify
{
	int						m_nIndex = -1;	// 列表项下标
	int						m_nGen = -1;	// 任务代号
	int						m_nDpi = 0;
	int						m_nBit = 1;
	std::vector<RIPDotStat>	m_vecStats;		// 与色版一一对应
};

class CMainDlg : public SHostWnd
	, public CLvRIPTaskAdapter::IListen
{
public:
	CMainDlg();
	~CMainDlg();

	void OnClose();
	void OnMaximize();
	void OnRestore();
	void OnMinimize();
	void OnSize(UINT nType, CSize size);
	BOOL OnInitDialog(HWND wndFocus, LPARAM lInitParam);

	// CLvRIPTaskAdapter::IListen: 左侧列表选中项变化 -> 右侧展示该 PDF 的墨点统计
	void OnListItemSelected(int nIndex) override;
	// CLvRIPTaskAdapter::IListen: 勾选状态变化 -> 作业列表落盘
	void OnListCheckChanged(int nIndex, bool bChecked) override;

	void SaveCMYKToTIFF(const char* filename, unsigned char* pData, int width, int height);

protected:
	void OnLanguage(int nID);
	void OnLanguageBtnCN();
	void OnLanguageBtnJP();

	void OnMenuFileCommand(IEvtArgs* e);
	void OnCommand(UINT uNotifyCode, int nID, HWND wndCtl);

	// ---- RIP 界面接线 ----
	void OnRipRun();					// 菜单: 开始RIP
	void OnStopRip();					// 菜单: 停止处理
	void OnBtnSelectICC();				// 按钮: 选择 ICC 曲线文件
	void OnBtnSelectOutRoot();			// 按钮: 选择输出根目录
	void OnRemoveSelectedFile();		// 菜单: 删除左侧列表选中的 PDF
	void OnClearFileList();				// 菜单: 清空左侧 PDF 列表
	void OnCheckAllFiles();				// 菜单: 全选 / 全不选
	// 墨滴体积(pL)/墨价编辑框内容一变(EventRENotify) -> 立刻重算耗墨量与成本
	BOOL OnInkEditChanged(EventArgs* e);
	// 防抖落盘定时器: 界面参数/作业列表变化 1.5s 后写一次 RIPDemo.ini
	BOOL OnAutoSaveTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	BOOL OnRipItemStatus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	BOOL OnRipAllDone(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	BOOL OnRipProgress(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	BOOL OnRipDotStat(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

	// 把第 nIndex 项的墨点统计刷到右侧面板(没有统计就显示提示文字)
	void ShowDotStatPanel(int nIndex);
	// 读界面上的墨滴体积(皮升): 控件不存在/非法时保持传入的默认值
	void GetDropPL(double& dSmall, double& dMid, double& dBig);
	// 读界面上的墨水单价(元/毫升): 控件不存在时返回默认值
	double GetInkPriceML();

	//soui消息
	EVENT_MAP_BEGIN()
		EVENT_NAME_COMMAND(L"btn_close", OnClose)
		EVENT_NAME_COMMAND(L"btn_min", OnMinimize)
		EVENT_NAME_COMMAND(L"btn_max", OnMaximize)
		EVENT_NAME_COMMAND(L"btn_restore", OnRestore)
		EVENT_NAME_COMMAND(L"zh_cn", OnLanguageBtnCN)
		EVENT_NAME_COMMAND(L"jp", OnLanguageBtnJP)
		EVENT_NAME_COMMAND(L"btn_log_all", OnBtnSelectICC)
		EVENT_NAME_COMMAND(L"btn_outroot", OnBtnSelectOutRoot)

		EVENT_ID_HANDLER(R.id.menu_file, EventSelectMenu::EventID, OnMenuFileCommand)
	EVENT_MAP_END()
		
	//HostWnd真实窗口消息处理
	BEGIN_MSG_MAP_EX(CMainDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_SIZE(OnSize)
		MSG_WM_COMMAND(OnCommand)
		MESSAGE_HANDLER(WM_RIP_ITEM_STATUS, OnRipItemStatus)
		MESSAGE_HANDLER(WM_RIP_ALL_DONE, OnRipAllDone)
		MESSAGE_HANDLER(WM_RIP_PROGRESS, OnRipProgress)
		MESSAGE_HANDLER(WM_RIP_DOTSTAT, OnRipDotStat)
		MESSAGE_HANDLER(WM_TIMER, OnAutoSaveTimer)
		CHAIN_MSG_MAP(SHostWnd)
		REFLECT_NOTIFICATIONS_EX()
	END_MSG_MAP()

private:
	// 从界面控件读取一份完整的 RIP 参数
	void CollectRipParams(RIPParams& params);
	// 把一份 RIP 参数回填到界面控件(启动时读配置用)
	void ApplyRipParamsToUI(const RIPParams& params);
	// 启动时把命令行传入的 PDF 加入列表(支持双击 PDF / 拖到 exe 上打开)
	void AddCommandLinePdfs();
	// 用配置里的作业列表重建左侧列表(含勾选状态)
	void RestoreTaskListFromConfig(const RIPParams& params);
	// 界面参数/作业列表变了: 延迟落盘(同一个定时器重复设置即防抖)
	void ScheduleConfigSave();
	// 启动 / 停止 RIP 队列
	bool StartRipQueue();
	void StopRipQueue();
	// 回收已结束的工作线程句柄。线程仍存活时最多等 dwWaitMs 毫秒；
	// 返回 true = 当前没有存活的工作线程(可以安全启动新任务)。
	// 判据用线程内核对象而不是 m_bRipRunning, 这样才能从丢了结束消息的状态里自愈。
	bool ReapRipThread(DWORD dwWaitMs = 0);

	// 协调线程入口: 内部起 N 个工作线程并行跑任务, 全部退出后回收参数包
	static unsigned int __stdcall RipThreadProc(void* pArg);
	void RunRipQueue(RipJob* pJob);

	// 线程池回调: 处理第 nTaskIndex 个任务(一页)
	static void RipTaskProc(void* pCtx, RIPipeline& pipe, int nTaskIndex, int nThreadIdx);
	void RunRipTask(RIPipeline& pipe, RipJob* pJob, int nTaskIndex);

	// 进度条: 设置 0..nTotal 范围与进度值, 并刷新文本
	void UpdateRipProgress(int nDone, int nTotal, const SStringW& sstrText);
	// 收尾: 把没跑到/还在跑的列表项标成「已取消」
	void MarkPendingCancelled();

private:
	CLvRIPTaskAdapter*	m_pRIPTaskAdapter = NULL;

	// 仅 UI 线程使用: 启动前预扫描各 PDF 的页数(工作线程各有自己的流水线)
	RIPipeline			m_ripPipe;
	HANDLE				m_hRipThread = NULL;	// 协调线程句柄(NULL = 没有存活的工作线程)
	volatile bool		m_bRipCancel = false;	// true = 请求取消
	volatile bool		m_bRipRunning = false;	// true = 队列正在跑
	int					m_nRipGen = 0;			// 任务代号(仅 UI 线程读写), 用于丢弃过期消息
	int					m_nRipTotalTasks = 0;	// 本次队列的总页数(进度条用)
	int					m_nRipDoneTasks = 0;	// 已完成的页数(进度条用)
	int					m_nLvSelIndex = -1;		// 左侧列表当前选中项(用于展示墨点统计)
};
