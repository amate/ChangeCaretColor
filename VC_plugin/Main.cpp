/* ===========================================================================
                           TTB Plugin Template(VC++)

                                 Main.cpp

   =========================================================================== */
#include "stdafx.h"
#include <memory>
#include <chrono>
#include <imm.h>
#pragma comment(lib, "imm32.lib")
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include <atlframe.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include "Plugin.h"
#include "MessageDef.h"
#include "Main.h"

#include "MinHook\MinHook.h"

#include "UIAutomationClient.h"

using namespace std::chrono;

#if 0
#if defined _M_X64
#pragma comment(lib, "MinHook\\libMinHook.x64.lib")
#elif defined _M_IX86
#pragma comment(lib, "MinHook\\libMinHook.x86.lib")
#endif
#endif

bool APIHook();
void APIUnHook();

bool IgnoreWndClass(HWND hWnd);

bool IsImeOpen(HWND hWnd);

// --------------------------------------------------------
//    コマンドの情報
// --------------------------------------------------------
#if COMMAND_COUNT > 0
PLUGIN_COMMAND_INFO COMMAND_INFO[COMMAND_COUNT] =
{
	{
#ifdef _WIN64
		L"Command1",					//Name
		L"コマンド1",					//Caption
#else
		"Command1",						//Name
		"コマンド1",					//Caption
#endif
		COMMAND1_ID,					//CommandID
		0,								//Attr
		-1,								//ResID
		dmToolMenu | dmHotKeyMenu | dmChecked,
										//DispMenu
		0,								//TimerInterval
		0								// TimerCounter（未使用）
	},
	{
#ifdef _WIN64
		L"Command2",					// コマンド名（英名）
		L"コマンド2",					// コマンド説明（日本語）
#else
		"Command2",						// コマンド名（英名）
		"コマンド2",					// コマンド説明（日本語）
#endif
		COMMAND2_ID,					// コマンドID
		0,								// Attr（未使用）
		-1,								// ResTd(未使用）
		dmSystemMenu | dmHotKeyMenu,
										// DispMenu
		0,								// TimerInterval[msec] 0で使用しない
		0								// TimerCounter（未使用）
	},
};
#endif

enum { kMaxIgnoreList = 20 };

struct SharedConfigData
{
	COLORREF	colorOpen;
	DWORD		MinWidthOpen;
	COLORREF	colorClose;
	DWORD		MinWidthClose;

	char		ignoreList[kMaxIgnoreList][MAX_PATH];
};

// -----------------------------------------------------------
//		グローバル変数
// -----------------------------------------------------------
#pragma data_seg(".sharedata")
HHOOK g_hHook = NULL;
SharedConfigData	g_shared_config_data = { };
#pragma data_seg()

CAppModule _Module;

HINSTANCE g_hInst;
bool	  g_bSucceedAPIHook = false;

HWND		g_wnd30C4 = NULL;
LONG		g_ShowHideCount = 0;
BOOL		g_bSucceedCreateCaret = false;
HBITMAP		g_bmpCaret = NULL;
HBITMAP		g_CreateCaretArgBitmap = NULL;
int			g_CreateCaretWidth = 0;
int			g_CreateCaretHeight = 0;

#define HOOKCOUNT  3

typedef BOOL (WINAPI* pfCreateCaret)(HWND, HBITMAP, int, int);
typedef BOOL (WINAPI *pfShowCaret)(HWND);
typedef BOOL (WINAPI* pfHideCaret)(HWND);
//typedef BOOL (WINAPI* pfDestroyCaret)();

pfCreateCaret	orgCreateCaret = NULL;
pfShowCaret		orgShowCaret = NULL;
pfHideCaret		orgHideCaret = NULL;
//pfDestroyCaret	orgDestroyCaret = NULL;

std::unique_ptr<CUIAutomationClient>	g_uiAutomation;

enum { kWatchDogFocusHanderTimerInterval = 30 * 1000 };
UINT_PTR g_watchDogTimerId = 0;
steady_clock::time_point	g_lastFocusChangedTime;

/////////////////////////////////////////////////////////////////////////
// CCaretWindow

LPCWSTR kCaretWindowClassName = L"CustomCaretWindow_";
enum { WM_SHOWCARETIFNEED = WM_APP + 100 };

enum { kCaretWidth = 4, kCaretRightPadding = 2 };

typedef CWinTraits<WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED>	CCaretWinTraits;

class CCaretWindow : public CDoubleBufferWindowImpl <CCaretWindow, CWindow, CCaretWinTraits>
{
public:
	DECLARE_WND_CLASS(kCaretWindowClassName)

	enum { kTimerId = 1, kTimerInterval = 100 };

	void	SetSender(IUIAutomationElement* sender) {
		m_spSender = sender;
		if (sender) {
			SetTimer(kTimerId, kTimerInterval);
			//::ShowWindow(SW_SHOWNOACTIVATE);
			SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOSENDCHANGING);
		} else {
			KillTimer(kTimerId);
			ShowWindow(SW_HIDE);
		}
	}

	void	ChangeIMEState(bool bOn)
	{
		if (m_imeOn != bOn) {
			m_imeOn = bOn;
			Invalidate(FALSE);
		}
	}

	// overrides
	void DoPaint(CDCHandle dc)
	{
		RECT rc;
		GetClientRect(&rc);
		if (m_imeOn) {
			dc.FillSolidRect(&rc, g_shared_config_data.colorOpen);
		} else {
			dc.FillSolidRect(&rc, g_shared_config_data.colorClose);
		}
	}

	BEGIN_MSG_MAP_EX(CCaretWindow)
		MSG_WM_CREATE(OnCreate)
		MESSAGE_HANDLER_EX(WM_SHOWCARETIFNEED, OnShowCaretIfNeed)
		MSG_WM_TIMER(OnTimer)
		CHAIN_MSG_MAP(__super)
	END_MSG_MAP()

	int OnCreate(LPCREATESTRUCT /*lpCreateStruct*/)
	{
		::SetLayeredWindowAttributes(m_hWnd, 0, 100, LWA_ALPHA);
		return 0;
	}

	LRESULT OnShowCaretIfNeed(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		if (lParam != 0) {
			SetSender(nullptr);
			return 0;
		}
		ChangeIMEState(wParam != 0);
		return 0;
	}

	void OnTimer(UINT_PTR nIDEvent)
	{
		if (nIDEvent != kTimerId)
			return;

		HWND foreHwnd = ::GetForegroundWindow();
		CRect rcForeground;
		::GetWindowRect(foreHwnd, &rcForeground);

		CRect rcBound;
		CComPtr<IUIAutomationElement> spSender = m_spSender;
		if (spSender) {
			spSender->get_CurrentBoundingRectangle(&rcBound);

			enum { kCaretWidth = 4, kCaretRightPadding = 2 };
			CRect rcCaret = rcBound;
			rcCaret.left -= kCaretWidth + kCaretRightPadding;
			rcCaret.right = rcBound.left - kCaretRightPadding;
			MoveWindow(rcCaret);
		}

		DWORD threadId = ::GetWindowThreadProcessId(foreHwnd, nullptr);
		GUITHREADINFO guiInfo = { sizeof(GUITHREADINFO) };
		::GetGUIThreadInfo(threadId, &guiInfo);
		enum { IMC_GETOPENSTATUS = 0x5 };
		HWND imeHwnd = ::ImmGetDefaultIMEWnd(guiInfo.hwndFocus);
		LRESULT ret = ::SendMessage(imeHwnd, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0);
		ChangeIMEState(ret != 0);

		if (
			(rcForeground.PtInRect(rcBound.TopLeft()) == FALSE &&
			 rcForeground.PtInRect(rcBound.BottomRight()) == FALSE) )
		{
			ShowWindow(SW_HIDE);
		} else {
			ShowWindow(SW_SHOWNOACTIVATE);
		}
	}
	
private:
	CComPtr<IUIAutomationElement>	m_spSender;
	bool	m_imeOn;

}	g_wndCaret;


class CHookUnHookManager
{
public:
	CHookUnHookManager() : m_hHookEngineDll(NULL)
	{
		if (MH_Initialize() == MH_OK) 
			g_bSucceedAPIHook = APIHook();
	}

	~CHookUnHookManager()
	{
		if (g_bSucceedAPIHook)
			APIUnHook();
	}

private:
	HMODULE m_hHookEngineDll;

} ;

// --------------------------------------------------------
//    フックプロシージャ
// --------------------------------------------------------


LRESULT CALLBACK CallWndProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION) {
#if 1
		CWPRETSTRUCT* pcwp = (CWPRETSTRUCT*)lParam;
		if (pcwp->message == WM_IME_NOTIFY && pcwp->wParam == IMN_SETOPENSTATUS) {
			if (IgnoreWndClass(g_wnd30C4) == false) {
				LONG Count = g_ShowHideCount;
				HWND wnd30C4 = g_wnd30C4;
				int Width = g_CreateCaretWidth;
				int Height = g_CreateCaretHeight;

				POINT	ptLocal2;
				::GetCaretPos(&ptLocal2);
				::DestroyCaret();
				::CreateCaret(wnd30C4, NULL, Width, Height);
				::SetCaretPos(ptLocal2.x, ptLocal2.y);

				// キャレットにIMEの状態を知らせる
				HWND caretHwnd = ::FindWindow(kCaretWindowClassName, nullptr);
				if (caretHwnd) {
					::PostMessage(caretHwnd, WM_SHOWCARETIFNEED, IsImeOpen(wnd30C4 ? wnd30C4 : pcwp->hwnd), 0);
				}

				if (Count > 0) {
					for (int i = 0; i < Count; ++i) {
						::ShowCaret(wnd30C4);
					}
				} else if (Count < 0) {
					for (LONG i = 0; i > Count; --i) {
						::HideCaret(wnd30C4);
					}
				}
			}
		} else if (pcwp->message == WM_IME_SETCONTEXT && pcwp->wParam == FALSE) {
			// フォーカスが変わらなくてもIMEがオフになる状態があるので
			// その時キャレットを隠すようにする
			HIMC hContext = ::ImmGetContext(pcwp->hwnd);
			//ATLTRACE(L"wm_ime_setcontext : HIMC %x : lParam : %x\n", hContext, pcwp->lParam);

			BOOL hideCaret = hContext == NULL;
			HWND caretHwnd = ::FindWindow(kCaretWindowClassName, nullptr);
			if (caretHwnd && hideCaret) {
				::PostMessage(caretHwnd, WM_SHOWCARETIFNEED, 0, hideCaret);
			}

		} else if (pcwp->message == WM_SETFOCUS) {
			static CHookUnHookManager s_mng;
		}
#endif
	}
	return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}


void FocusChangedHandler(IUIAutomationElement* sender)
{
	g_lastFocusChangedTime = steady_clock::now();

	CONTROLTYPEID	controlTypeId = 0;
	sender->get_CurrentControlType(&controlTypeId);
	if (controlTypeId != UIA_EditControlTypeId &&
		controlTypeId != UIA_ComboBoxControlTypeId &&
		controlTypeId != UIA_CustomControlTypeId /*firefoxでページからflash内のエディットにフォーカスを移したとき最後に呼ばれてしまう*/) {
		g_wndCaret.SetSender(nullptr);
		return;
	}
	if (controlTypeId == UIA_CustomControlTypeId)
		return;	// 何もしない

	enum { kMaxBoundHeight = 30 };
	CRect rcBound;
	sender->get_CurrentBoundingRectangle(&rcBound);
#if 0
	if (rcBound.Height() > kMaxBoundHeight) {
		g_wndCaret.SetSender(nullptr);
		return;
	}
#endif
	HWND editHwnd = NULL;
	sender->get_CurrentNativeWindowHandle((UIA_HWND*)&editHwnd);

	if (controlTypeId == UIA_ComboBoxControlTypeId) {
		if (editHwnd) {
			DWORD windowStyle = ::GetWindowLong(editHwnd, GWL_STYLE);
			if (windowStyle & CBS_DROPDOWNLIST) {
				g_wndCaret.SetSender(nullptr);
				return;
			}
		} else {
			CComPtr<IUIAutomationValuePattern> spValuePattern;
			sender->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern, (void**)&spValuePattern);
			if (spValuePattern) {
				BOOL bReadOnly = FALSE;
				spValuePattern->get_CurrentIsReadOnly(&bReadOnly);
				if (bReadOnly) {
					g_wndCaret.SetSender(nullptr);
					return;
				}
			} else {
				CComVariant vControlType = UIA_EditControlTypeId;
				CComPtr<IUIAutomationCondition> spCndEdit;
				g_uiAutomation->GetUIAutomation()->CreatePropertyCondition(UIA_ControlTypePropertyId, vControlType, &spCndEdit);
				if (spCndEdit) {
					CComPtr<IUIAutomationElement> spChildElm;
					sender->FindFirst(TreeScope_Children, spCndEdit, &spChildElm);
					if (spChildElm == nullptr) {
						g_wndCaret.SetSender(nullptr);
						return;
					}
				}
			}
		}
	} else if (controlTypeId == UIA_EditControlTypeId) {
		if (editHwnd) {
			WCHAR className[128] = L"";
			::GetClassName(editHwnd, className, 128);
			if (::wcscmp(className, L"Edit") == 0) {
				g_wndCaret.SetSender(nullptr);
				return;
			}

		}

		CComPtr<IUIAutomationValuePattern> spValuePattern;
		sender->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern, (void**)&spValuePattern);
		if (spValuePattern) {
			BOOL bReadOnly = FALSE;
			spValuePattern->get_CurrentIsReadOnly(&bReadOnly);
			if (bReadOnly) {
				g_wndCaret.SetSender(nullptr);
				return;
			}
		}
	}

	HWND foreHwnd = ::GetForegroundWindow();
	DWORD threadId = ::GetWindowThreadProcessId(foreHwnd, nullptr);
	GUITHREADINFO guiInfo = { sizeof(GUITHREADINFO) };
	::GetGUIThreadInfo(threadId, &guiInfo);

	// 色つきキャレットが表示されている
	if (guiInfo.flags == GUI_CARETBLINKING && editHwnd) {
		g_wndCaret.SetSender(nullptr);
		return;
	}

	CRect rcCaret = rcBound;
	rcCaret.left -= kCaretWidth + kCaretRightPadding;
	rcCaret.right = rcBound.left - kCaretRightPadding;
	g_wndCaret.MoveWindow(rcCaret);


	enum { IMC_GETOPENSTATUS = 0x5 };
	HWND imeHwnd = ::ImmGetDefaultIMEWnd(guiInfo.hwndFocus);
	BOOL ret = ::SendMessage(imeHwnd, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0);
	g_wndCaret.ChangeIMEState(ret != 0);

	g_wndCaret.SetSender(sender);

#if 0
	BOOL ret = ::SendMessageCallback(imeHwnd, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0,
		[](HWND hwnd, UINT uMsg, ULONG_PTR dwData, LRESULT lResult) {
		g_wndCaret.ChangeIMEState(lResult != 0);
	}, 0);
#endif


}


// --------------------------------------------------------
//    プラグインがロードされたときに呼ばれる
// --------------------------------------------------------
BOOL Init(void)
{

	g_hHook = SetWindowsHookEx(WH_CALLWNDPROCRET, CallWndProc, g_hInst, 0);
	if (!g_hHook)
		return FALSE;

	char configpath[MAX_PATH];
	::GetModuleFileNameA(g_hInst, configpath, MAX_PATH);
	::PathRemoveFileSpecA(configpath);
	::lstrcatA(configpath, "\\config.ini");
	{
		BYTE r = (BYTE)::GetPrivateProfileIntA("Open", "r", 0, configpath);
		BYTE g = (BYTE)::GetPrivateProfileIntA("Open", "g", 200, configpath);
		BYTE b = (BYTE)::GetPrivateProfileIntA("Open", "b", 0, configpath);
		g_shared_config_data.colorOpen = RGB(r, g, b);
		g_shared_config_data.MinWidthOpen = ::GetPrivateProfileIntA("Open", "MinWidth", 3, configpath);
	}
	{
		BYTE r = (BYTE)::GetPrivateProfileIntA("Close", "r", 255, configpath);
		BYTE g = (BYTE)::GetPrivateProfileIntA("Close", "g", 0, configpath);
		BYTE b = (BYTE)::GetPrivateProfileIntA("Close", "b", 0, configpath);
		g_shared_config_data.colorClose = RGB(r, g, b);
		g_shared_config_data.MinWidthClose = ::GetPrivateProfileIntA("Close", "MinWidth", 2, configpath);
	}
	{
		char ignorelist[2048] = "";
		::GetPrivateProfileStringA("Ignore", "Ignore", "", ignorelist, 2048, configpath);
		char* classname = ::strtok(ignorelist, ",");
		int i = 0;
		while (classname) {
			if (kMaxIgnoreList <= i)
				break;
			::strcpy_s(g_shared_config_data.ignoreList[i], classname);
			++i;
			classname = ::strtok(nullptr, ",");
		}
	}

	bool ShowCustomCaretWindow = ::GetPrivateProfileIntA("CustomCaretWindow", "Show", 1, configpath) != 0;
	if (ShowCustomCaretWindow) {
		bool CustomCaretWindow = ::GetPrivateProfileIntA("CustomCaretWindow", "x64", 0, configpath) != 0;

		SYSTEM_INFO sysInfo = {};
		GetNativeSystemInfo(&sysInfo);

		BOOL bIsWow64 = FALSE;
		ATLVERIFY(IsWow64Process(GetCurrentProcess(), &bIsWow64));

		if (   (CustomCaretWindow == false && (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL || bIsWow64))
			|| (CustomCaretWindow &&  sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 && bIsWow64 == FALSE) )
		{
			g_wndCaret.Create(NULL);
			g_wndCaret.MoveWindow(200, 200, 10, 50);

			g_uiAutomation.reset(new CUIAutomationClient);
			g_uiAutomation->AddFocusChangedEventHandler(FocusChangedHandler);

			g_lastFocusChangedTime = steady_clock::now();
	#if 0 
			g_watchDogTimerId = ::SetTimer(NULL, 0, kWatchDogFocusHanderTimerInterval, [](HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
				if (milliseconds(kWatchDogFocusHanderTimerInterval) < (steady_clock::now() - g_lastFocusChangedTime)) {
					g_uiAutomation->AddFocusChangedEventHandler(FocusChangedHandler);
				}
			});
	#endif
		}
	}

#ifndef _DEBUG
	::PostMessageA(HWND_BROADCAST, WM_NULL, 0, 0);	// すべてのウィンドウにフックを仕掛ける
#endif
	return TRUE;
}

// --------------------------------------------------------
//    プラグインがアンロードされたときに呼ばれる
// --------------------------------------------------------
void Unload(void)
{
	if (g_hHook)
		UnhookWindowsHookEx(g_hHook);
	g_hHook = NULL;

	if (g_wndCaret.IsWindow()) {
		//KillTimer(NULL, g_watchDogTimerId);

		g_uiAutomation.reset();

		g_wndCaret.DestroyWindow();
	}

	// UnHookしたのでdllのunloadを促す
	::PostMessageA(HWND_BROADCAST, WM_NULL, 0, 0);
}

// --------------------------------------------------------
//    コマンド実行時に呼ばれる
// --------------------------------------------------------
BOOL Execute(int CmdId, HWND hWnd)
{
	return TRUE;
}

// --------------------------------------------------------
//    グローバルフックメッセージがやってくる
// --------------------------------------------------------
void Hook(UINT Msg, DWORD wParam, DWORD lParam)
{
}





void RefreshData()
{
	if (g_bmpCaret) {
		::DeleteObject(g_bmpCaret);
		g_bmpCaret = NULL;
	}
	g_wnd30C4 = NULL;
	g_ShowHideCount = 0;
	g_bSucceedCreateCaret = false;
	g_bmpCaret = NULL;
	g_CreateCaretArgBitmap = NULL;
	g_CreateCaretWidth = 0;
	g_CreateCaretHeight = 0;
}

bool IgnoreWndClass(HWND hWnd)
{
	char classname[MAX_PATH];
	::GetClassNameA(hWnd, classname, MAX_PATH);
	for (int i = 0; i < kMaxIgnoreList; ++i) {
		if (g_shared_config_data.ignoreList[i][0] == '\0')
			break;
		if (::strcmpi(g_shared_config_data.ignoreList[i], classname) == 0)
			return true;
	}
	return false;
}


bool GetConversionStatus(HWND hWnd, DWORD* pConversion)
{
	if (pConversion == nullptr)
		return false;

	HIMC hImc = ::ImmGetContext(hWnd);
	if (hImc) {
		if (::ImmGetOpenStatus(hImc)) {
			DWORD Conversion = 0;
			DWORD Sentence = 0;
			if (::ImmGetConversionStatus(hImc, &Conversion, &Sentence)) {
				// 共有メモリからフラグを調べる処理がある
				*pConversion = Conversion;
			}
		}
		::ImmReleaseContext(hWnd, hImc);
	}

	return true;
}

bool	IsImeOpen(HWND hWnd)
{
	DWORD Conversion = 0;
	GetConversionStatus(hWnd, &Conversion);
	if (IME_CMODE_ALPHANUMERIC == Conversion || IME_CMODE_NOCONVERSION == Conversion) {
		return false;
	} else {
		return true;
	}
}

BOOL WINAPI NewCreateCaret(
    __in HWND hWnd,
    __in_opt HBITMAP hBitmap,
    __in int nWidth,
    __in int nHeight)
{
	int local2 = 0;

	if (hBitmap) {
		BITMAP bmp = { 0 };
		::GetObjectA(hBitmap, sizeof(bmp), &bmp);
		nWidth = bmp.bmWidth;
		nHeight= bmp.bmHeight;
		// 何か処理があるがわからん
	}

	HBITMAP hMemBitmap = hBitmap;
	HBITMAP hBmpForDelete = NULL;
	if (IgnoreWndClass(hWnd) == false) {
		HDC hdc = ::GetDC(hWnd);
		if (hdc) {
			HDC memdc = ::CreateCompatibleDC(hdc);
			if (memdc) {
				// キャレットの色を決める
				nWidth = g_shared_config_data.MinWidthOpen;
				COLORREF color = g_shared_config_data.colorOpen;
				DWORD Conversion = 0;
				if (GetConversionStatus(hWnd, &Conversion)) {
					if (IME_CMODE_ALPHANUMERIC == Conversion || IME_CMODE_NOCONVERSION == Conversion) {
						nWidth = g_shared_config_data.MinWidthClose;
						color = g_shared_config_data.colorClose;
					}
				}

				hMemBitmap = ::CreateCompatibleBitmap(hdc, nWidth, nHeight);
				hBmpForDelete = hMemBitmap;
				if (hMemBitmap) {
					color = ~color;
					color &= 0x00FFFFFF;
					HBRUSH hbr = ::CreateSolidBrush(color);
					if (hbr) {
						HGDIOBJ hPrevBitmap = ::SelectObject(memdc, hMemBitmap);
						RECT rc;
						rc.top = 0;
						rc.left = 0;
						rc.right = nWidth;
						rc.bottom = nHeight;
						::FillRect(memdc, &rc, hbr);
						::SelectObject(memdc, hPrevBitmap);
						::DeleteObject(hbr);
					}
				}
				::DeleteDC(memdc);
			}
			::ReleaseDC(hWnd, hdc);
		}
	}
	// オリジナルのCreateCaretを呼ぶ
	BOOL bRet = orgCreateCaret(hWnd, hMemBitmap, nWidth, nHeight);
	if (bRet) {
		RefreshData();

		g_wnd30C4 = hWnd;
		g_bSucceedCreateCaret = true;
		g_bmpCaret = hBmpForDelete;
		g_CreateCaretArgBitmap = hBitmap;
		g_CreateCaretWidth	= nWidth;
		g_CreateCaretHeight	= nHeight;

	} else if (hBmpForDelete) {		
		::DeleteObject(hBmpForDelete);
	}
	return bRet;
}

BOOL WINAPI NewShowCaret(__in_opt HWND hWnd)
{
	// オリジナルのShowCaretを呼ぶ
	BOOL bRet = orgShowCaret(hWnd);
	if (bRet && hWnd && hWnd == g_wnd30C4) {
		::InterlockedIncrement(&g_ShowHideCount);		
	}
	return bRet;
}

BOOL WINAPI NewHideCaret(__in_opt HWND hWnd)
{
	// オリジナルのHideCaretを呼ぶ
	BOOL bRet = orgHideCaret(hWnd);
	if (bRet && hWnd && hWnd == g_wnd30C4) {
		::InterlockedDecrement(&g_ShowHideCount);		
	}
	return bRet;
}

#if 0
BOOL WINAPI NewDestroyCaret()
{
	// オリジナルのCreateCaretを呼ぶ
	BOOL bRet = orgDestroyCaret();
	if (bRet) {
		RefreshData();		
	}
	return bRet;
}
#endif


bool APIHook()
{
	MH_CreateHook(static_cast<void*>(&CreateCaret), 
		static_cast<void*>(&NewCreateCaret), reinterpret_cast<void**>(&orgCreateCaret));
	MH_EnableHook(&CreateCaret);

	MH_CreateHook(static_cast<void*>(&ShowCaret), 
		static_cast<void*>(&NewShowCaret), reinterpret_cast<void**>(&orgShowCaret));
	MH_EnableHook(&ShowCaret);

	MH_CreateHook(static_cast<void*>(&HideCaret), 
		static_cast<void*>(&NewHideCaret), reinterpret_cast<void**>(&orgHideCaret));
	MH_EnableHook(&HideCaret);

	//MH_CreateHook(static_cast<void*>(&DestroyCaret), 
	//	static_cast<void*>(&DestroyCaret), reinterpret_cast<void**>(&orgDestroyCaret));
	//MH_EnableHook(&DestroyCaret);
	
	return true;
}

void APIUnHook()
{
	MH_DisableHook(&CreateCaret);
	MH_DisableHook(&ShowCaret);
	MH_DisableHook(&HideCaret);
	//MH_DisableHook(&DestroyCaret);

	MH_Uninitialize();
}



// --------------------------------------------------------
//    DLLMain
// --------------------------------------------------------
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
		g_hInst = hModule;

	} else if (ul_reason_for_call == DLL_PROCESS_DETACH) {

	}
	
	return TRUE;
}
