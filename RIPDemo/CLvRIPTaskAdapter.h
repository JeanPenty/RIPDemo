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

		// 第 1 页的墨点统计(由工作线程算好后投递到 UI 线程填进来; 没跑过就是空的)
		std::vector<RIPDotStat> m_vecDotStats;
		int m_nDotDpi = 0;
		int m_nDotBit = 1;
	};
	struct IListen
	{
		// 左侧列表选中项变化(只在选中有效项时回调, 取消选中不回调)
		virtual void OnListItemSelected(int /*nIndex*/) {}
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

	virtual void getView(int position, SItemPanel* pItem, SXmlNode xmlTemplate)
	{
		auto* pGu = CGlobalUnits::Instance;

		if (0 == pItem->GetChildrenCount())
		{
			pItem->InitFromXml(&xmlTemplate);
			pItem->GetEventSet()->subscribeEvent(EventItemPanelClick::EventID, Subscriber(&CLvRIPTaskAdapter::OnEventItemPanelClick, this));
		}

		size_t sPos = static_cast<size_t>(position);
		if (sPos >= m_vecItemDatas.size()) return;

		ItemData* pItemData = m_vecItemDatas[sPos];
		if (NULL == pItemData) return;

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
			static const LPCWSTR kColors[] = { L"#FFFFFF", L"#3B82F6", L"#22C55E", L"#EF4444" };
			pTitle->SetAttribute(L"colorText", kColors[pItemData->m_nStatus & 3]);
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
	void AddRipTask(const SStringW& sstrFilePath)
	{
		ItemData* data = new ItemData;
		data->m_sstrFilePath = sstrFilePath;
		int nPos = sstrFilePath.ReverseFind(L'\\');
		data->m_sstrTaskKey = (nPos >= 0) ? sstrFilePath.Right(sstrFilePath.GetLength() - nPos - 1) : sstrFilePath;
		data->m_nStatus = 0;
		data->m_sstrStatusText = L"等待";
		m_vecItemDatas.push_back(data);

		notifyDataSetChanged();
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

protected:
	BOOL OnEventLvSelChangeing(EventLVSelChanging* pEvt)
	{
		if (NULL == pEvt) return true;
		pEvt->bubbleUp = false;
		if (-1 == pEvt->iNewSel) pEvt->bCancel = TRUE;				// 不允许选中 -1 
		else if (m_pListen) m_pListen->OnListItemSelected(pEvt->iNewSel);
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

