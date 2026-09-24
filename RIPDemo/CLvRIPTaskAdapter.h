#pragma once
#include <helper/SAdapterBase.h>
#include <vector>

class CLvRIPTaskAdapter : public SAdapterBase
{
public:
	struct ItemData
	{
		SStringW m_sstrTaskKey;
	};
	struct IListen
	{
		//virtual void OnItemClick(int& nIndex) = 0;
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
		pTitle->SetWindowTextW(pItemData->m_sstrTaskKey);

		pTitle->SetAttribute(L"font", L"face:微软雅黑,size:12");
	}

	void AddItem(const SStringW& sstrTitle)
	{
		ItemData* data = new ItemData;
		data->m_sstrTaskKey = sstrTitle;
		m_vecItemDatas.push_back(data);

		notifyDataSetChanged();
	}

	void DeleteAllItem()
	{
		m_vecItemDatas.clear();
		notifyDataSetChanged();
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
	std::vector<ItemData*> m_vecItemDatas;				// 数据 
	SListView* m_pOwner;
	IListen* m_pListen;

protected:
	BOOL OnEventLvSelChangeing(EventLVSelChanging* pEvt)
	{
		if (NULL == pEvt) return true;
		pEvt->bubbleUp = false;
		if (-1 == pEvt->iNewSel) pEvt->bCancel = TRUE;				// 不能设置 -1 
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

