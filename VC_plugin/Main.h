/* ===========================================================================
                           TTB Plugin Template(VC++)

                                 Main.h

   =========================================================================== */

#pragma once

// --------------------------------------------------------
//    プラグインの情報
// --------------------------------------------------------
/* プラグインの名前（２バイトも可能） */
#ifdef _WIN64
#define PLUGIN_NAME		L"ChangeCaretColorx64"
#else
#define PLUGIN_NAME		"ChangeCaretColor"
#endif
/* プラグインのタイプ */
//#define PLUGIN_TYPE		ptLoadAtUse
#define	PLUGIN_TYPE	ptAlwaysLoad

// --------------------------------------------------------
//    コマンドの情報
// --------------------------------------------------------
/* コマンドの数 */
#define COMMAND_COUNT	0

/* コマンドID */
#define COMMAND1_ID	0
#define COMMAND2_ID	1

// --------------------------------------------------------
//    グローバル変数
// --------------------------------------------------------
#if COMMAND_COUNT > 0
extern PLUGIN_COMMAND_INFO COMMAND_INFO[COMMAND_COUNT];
#endif

// --------------------------------------------------------
//    関数定義
// --------------------------------------------------------
BOOL Init(void);
void Unload(void);
BOOL Execute(int, HWND);
void Hook(UINT, DWORD, DWORD);
