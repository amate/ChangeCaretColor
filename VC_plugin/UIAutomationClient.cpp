
#include "stdafx.h"
#include "UIAutomationClient.h"

CUIAutomationClient::CUIAutomationClient() : m_focusChangedEventHandler(nullptr)
{
	::CoInitialize(NULL);

	m_spUIAutomation.CoCreateInstance(__uuidof(CUIAutomation));
	ATLASSERT(m_spUIAutomation);
}


CUIAutomationClient::~CUIAutomationClient()
{
	// RemoveAllEventHandlers‚Å‰ð•ú‚³‚ê‚Ü‚·
	m_focusChangedEventHandler = nullptr;

	m_spUIAutomation->RemoveAllEventHandlers();

	m_spUIAutomation.Release();

	::CoUninitialize();
}

void	CUIAutomationClient::AddFocusChangedEventHandler(std::function<void(IUIAutomationElement*)> eventHandler)
{
	HRESULT hr = S_OK;
	if (m_focusChangedEventHandler) {
		hr = m_spUIAutomation->RemoveFocusChangedEventHandler(m_focusChangedEventHandler);
		ATLASSERT(hr == S_OK);
	}
	CComObject<CUIAutomationFocusChangedEventHandler>::CreateInstance(&m_focusChangedEventHandler);
	m_focusChangedEventHandler->SetEventHandler(eventHandler);

	hr = m_spUIAutomation->AddFocusChangedEventHandler(nullptr, m_focusChangedEventHandler);
	ATLASSERT(hr == S_OK);
}




