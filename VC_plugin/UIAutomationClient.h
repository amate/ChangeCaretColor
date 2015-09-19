/**
*	@file	UIAutomationClient
*/

#pragma once

#include <UIAutomationClient.h>
#include <atlcom.h>
#include <atlcomcli.h>
#include <functional>

class CUIAutomationFocusChangedEventHandler : 
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CUIAutomationFocusChangedEventHandler>,
	public IUIAutomationFocusChangedEventHandler
{
public:
	BEGIN_COM_MAP(CUIAutomationFocusChangedEventHandler)
		COM_INTERFACE_ENTRY(IUIAutomationFocusChangedEventHandler)
	END_COM_MAP()

	void	SetEventHandler(std::function<void(IUIAutomationElement*)> eventHandler) {
		m_eventHandler = eventHandler;
	}

	// IUIAutomationFocusChangedEventHandler
	virtual HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(
		/* [in] */ __RPC__in_opt IUIAutomationElement *sender) override
	{
		m_eventHandler(sender);
		return S_OK;
	}

private:
	std::function<void(IUIAutomationElement*)>	m_eventHandler;
};

class CUIAutomationClient
{
public:
	CUIAutomationClient();
	~CUIAutomationClient();

	CComPtr<IUIAutomation>	GetUIAutomation() const { return m_spUIAutomation; }

	void	AddFocusChangedEventHandler(std::function<void (IUIAutomationElement*)> eventHandler);

private:
	CComPtr<IUIAutomation>	m_spUIAutomation;
	CComObject<CUIAutomationFocusChangedEventHandler>* m_focusChangedEventHandler;
};

