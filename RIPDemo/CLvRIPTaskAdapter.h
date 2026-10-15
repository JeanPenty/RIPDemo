#pragma once
#include <helper/SAdapterBase.h>
#include <vector>
#include "RIPParams.h"

class CLvRIPTaskAdapter : public SAdapterBase
{
public:
	struct ItemData
	{
		SStringW m_sstrTaskKey;
		SStringW m_sstrFilePath;
		int m_nStatus = 0;           // 0=wait 1=running 2=done 3=failed
		SStringW m_sstrStatusText;

		// 勾选状态: 只有勾上的文件才参与「开始RIP」(新增时默认全勾)
		bool m_bChecked = true;

		// 第 1 页的墨点统计(由工作线程算好后投递到 UI 线程填进来; 没跑过就是空的)
		std::vector<RIPDotStat> m_vecDotStats;
		int m_nDotDpi = 0;
		int m_nDotBit = 1;
	};
	struct IListen
	{
		// 左侧列表选中项变化(只在选中有效项时回调, 取消选中不回调)
		virtual void OnListItemSelected(int /*nIndex*/) {}
		// 勾选状态变化(用于把作业列表落盘)
		virtual void OnListCheckChanged(int /*nIndex*/, bool /*bChecked*/) {}
	};

public:
	CLvRIPTaskAdapter(SListView* pOwner, IListen* pListen)
	{
		m_pOwner = pOwner;
		SASSERT(m_pOwner);
		m_pOwner->GetEventSet()->subscribeEvent(&CLvRIPTaskAdapter::OnEventLvSelChangeing, this);
		m_pListen = pListen;
	}

	virtual int getCount()
	{
		return static_cast<int>(m_vecItemDatas.size());
	}

	// 项目文字颜色: 未勾选置灰, 一眼能看出哪些不会被处理
	//   0白 1蓝 2绿 3红
	static LPCWSTR ItemColor(const ItemData* p)
	{
		static const LPCWSTR kColors[] = { L"#FFFFFF", L"#3B82F6", L"#22C55E", L"#EF4444" };
		return p->m_bChecked ? kColors[p->m_nStatus & 3] : L"#6B7280";
	}

	virtual void getView(int position, SItemPanel* pItem, SXmlNode xmlTemplate)
	{
		auto* pGu = CGlobalUnits::Instance;

		if (0 == pItem->GetChildrenCount())
		{
			pItem->InitFromXml(&xmlTemplate);
			pItem->GetEventSet()->subscribeEvent(EventItemPanelClick::EventID, Subscriber(&CLvRIPTaskAdapter::OnEventItemPanelClick, this));

			// 勾选框的状态变化(用户点击/空格键)
			if (SCheckBox* pCheck = pItem->FindChildByName2<SCheckBox>(L"item_check"))
				pCheck->GetEventSet()->subscribeEvent(EventSwndStateChanged::EventID,
					Subscriber(&CLvRIPTaskAdapter::OnEventItemCheckChanged, this));
		}

		size_t sPos = static_cast<size_t>(position);
		if (sPos >= m_vecItemDatas.size()) return;

		ItemData* pItemData = m_vecItemDatas[sPos];
		if (NULL == pItemData) return;

		// ---- 勾选框: 回填数据里的勾选状态 ----
		// 回填会触发状态变化事件, 用 m_bBinding 把它和「用户点击」区分开
		if (SCheckBox* pCheck = pItem->FindChildByName2<SCheckBox>(L"item_check"))
		{
			m_bBinding = TRUE;
			pCheck->SetCheck(pItemData->m_bChecked ? TRUE : FALSE);
			m_bBinding = FALSE;
		}

		SStatic* pTitle = pItem->FindChildByName2<SStatic>(L"item_name");
		if (pTitle)
		{
			SStringW sstrText = pItemData->m_sstrTaskKey;
			if (!pItemData->m_sstrStatusText.IsEmpty())
				sstrText += SStringW().Format(L"    [%s]", pItemData->m_sstrStatusText.c_str());
			pTitle->SetWindowTextW(sstrText);
			// 状态文本过长时列表会被裁, 把完整文本再挂一份 tooltip
			pTitle->SetToolTipText(sstrText.c_str());
			pItem->SetToolTipText(sstrText.c_str());
			pTitle->SetAttribute(L"font", L"face:微软雅黑,size:12");
			// status colors: wait=white running=blue done=green failed=red
			pTitle->SetAttribute(L"colorText", ItemColor(pItemData));
		}
	}

	void AddItem(const SStringW& sstrTitle)
	{
		ItemData* data = new ItemData;
		data->m_sstrTaskKey = sstrTitle;
		m_vecItemDatas.push_back(data);

		notifyDataSetChanged();
	}

	// 按完整路径添加任务(给 RIP 列表用)
	//   bChecked: 勾选状态。从配置重建列表时按上次的勾选恢复
	//   pDotStats/nDotDpi/nDotBit: 上次 RIP 存下的第 1 页墨点统计,
	//     没传就是空(重启后也能算耗墨量/成本)
	void AddRipTask(const SStringW& sstrFilePath, bool bChecked = true,
	                const std::vector<RIPDotStat>* pDotStats = NULL,
	                int nDotDpi = 0, int nDotBit = 1)
	{
		ItemData* data = new ItemData;
		data->m_sstrFilePath = sstrFilePath;
		int nPos = sstrFilePath.ReverseFind(L'\\');
		data->m_sstrTaskKey = (nPos >= 0) ? sstrFilePath.Right(sstrFilePath.GetLength() - nPos - 1) : sstrFilePath;
		data->m_nStatus = 0;
		data->m_sstrStatusText = L"等待";
		data->m_bChecked = bChecked;
		if (pDotStats && !pDotStats->empty())
		{
			data->m_vecDotStats = *pDotStats;
			data->m_nDotDpi = nDotDpi;
			data->m_nDotBit = nDotBit;
		}
		m_vecItemDatas.push_back(data);

		notifyDataSetChanged();
	}

	// 按完整路径查下标(忽略大小写), 不存在返回 -1
	int FindRipTask(const SStringW& sstrFilePath) const
	{
		for (size_t i = 0; i < m_vecItemDatas.size(); ++i)
		{
			ItemData* p = m_vecItemDatas[i];
			if (p && p->m_sstrFilePath.CompareNoCase(sstrFilePath) == 0)
				return (int)i;
		}
		return -1;
	}

	ItemData* GetItem(int nIndex)
	{
		if (nIndex < 0 || nIndex >= (int)m_vecItemDatas.size()) return NULL;
		return m_vecItemDatas[nIndex];
	}

	// 更新状态并刷新列表
	void SetItemStatus(int nIndex, int nStatus, const SStringW& sstrText)
	{
		ItemData* p = GetItem(nIndex);
		if (!p) return;
		p->m_nStatus = nStatus;
		p->m_sstrStatusText = sstrText;
		notifyDataSetChanged();
	}

	void DeleteAllItem()
	{
		for (size_t i = 0; i < m_vecItemDatas.size(); ++i)
			delete m_vecItemDatas[i];
		m_vecItemDatas.clear();
		notifyDataSetChanged();
	}

	// 删除指定项(越界返回 false)；
	//   notifyDataSetChanged 会自动把越界的选中项清成 -1，调用方不用手动改
	bool RemoveItem(int nIndex)
	{
		if (nIndex < 0 || nIndex >= (int)m_vecItemDatas.size()) return false;

		delete m_vecItemDatas[nIndex];
		m_vecItemDatas.erase(m_vecItemDatas.begin() + nIndex);
		notifyDataSetChanged();
		return true;
	}

	// 当前项数
	int GetItemCount() const { return (int)m_vecItemDatas.size(); }

	// 全选 / 全不选
	void SetAllChecked(bool bChecked)
	{
		for (size_t i = 0; i < m_vecItemDatas.size(); ++i)
			m_vecItemDatas[i]->m_bChecked = bChecked;
		notifyDataSetChanged();
	}

	// 当前勾选个数
	int GetCheckedCount() const
	{
		int n = 0;
		for (size_t i = 0; i < m_vecItemDatas.size(); ++i)
			if (m_vecItemDatas[i]->m_bChecked) ++n;
		return n;
	}

	void EnsureVisible(int& nIndex)
	{
		m_pOwner->EnsureVisible(nIndex);
		notifyDataSetChanged();
	}

	void UpdateItem()
	{
		notifyDataSetChanged();
	}

private:
	std::vector<ItemData*> m_vecItemDatas;				// 列表数据
	SListView* m_pOwner;
	IListen* m_pListen;
	BOOL m_bBinding = FALSE;	// TRUE = 正在回填勾选框(不当用户点击)

protected:
	BOOL OnEventLvSelChangeing(EventLVSelChanging* pEvt)
	{
		if (NULL == pEvt) return true;
		pEvt->bubbleUp = false;
		if (-1 == pEvt->iNewSel) pEvt->bCancel = TRUE;				// 不允许选中 -1 
		else if (m_pListen) m_pListen->OnListItemSelected(pEvt->iNewSel);
		return true;
	}
	// 勾选框状态变化 -> 写回数据(不刷新列表, 避免打断点击)
	BOOL OnEventItemCheckChanged(EventArgs* e)
	{
		if (m_bBinding) return true;

		EventSwndStateChanged* pEvt = sobj_cast<EventSwndStateChanged>(e);
		if (!pEvt) return true;
		if (!EventSwndStateChanged_CheckState(pEvt, WndState_Check)) return true;

		SCheckBox* pCheck = sobj_cast<SCheckBox>(pEvt->sender);
		if (!pCheck) return true;

		SItemPanel* pItem = sobj_cast<SItemPanel>(pCheck->GetParent());
		if (!pItem) return true;

		const int nIndex = static_cast<int>(pItem->GetItemIndex());
		if (nIndex < 0 || nIndex >= (int)m_vecItemDatas.size()) return true;

		ItemData* pData = m_vecItemDatas[nIndex];
		pData->m_bChecked = (pCheck->IsChecked() != FALSE);

		// 直接改这一行的颜色:
		//   不能用 notifyDataSetChanged(), 它会在点击事件派发中重建面板,
		//   有重入/悬垂风险; 而且只需要改颜色, 没必要整行重画
		if (SStatic* pTitle = pItem->FindChildByName2<SStatic>(L"item_name"))
			pTitle->SetAttribute(L"colorText", ItemColor(pData));

		// 通知外层: 勾选状态变了(用来落盘)
		if (m_pListen) m_pListen->OnListCheckChanged(nIndex, pData->m_bChecked);
		return true;
	}

	BOOL OnEventItemPanelClick(EventArgs* e)
	{
		EventItemPanelClick* pEvt = sobj_cast<EventItemPanelClick>(e);
		if (!pEvt) return false;
		SItemPanel* pItem = sobj_cast<SItemPanel>(pEvt->sender);
		if (!pItem) return false;
		int nIndex = static_cast<int>(pItem->GetItemIndex());
		//m_pListen->OnItemClick(nIndex);
		return true;
	}
};

