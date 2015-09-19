/* ===========================================================================
                           TTB Plugin Template(VC++)

                                 Main.h

   =========================================================================== */

#pragma once

// --------------------------------------------------------
//    �v���O�C���̏��
// --------------------------------------------------------
/* �v���O�C���̖��O�i�Q�o�C�g���\�j */
#ifdef _WIN64
#define PLUGIN_NAME		L"ChangeCaretColorx64"
#else
#define PLUGIN_NAME		"ChangeCaretColor"
#endif
/* �v���O�C���̃^�C�v */
//#define PLUGIN_TYPE		ptLoadAtUse
#define	PLUGIN_TYPE	ptAlwaysLoad

// --------------------------------------------------------
//    �R�}���h�̏��
// --------------------------------------------------------
/* �R�}���h�̐� */
#define COMMAND_COUNT	0

/* �R�}���hID */
#define COMMAND1_ID	0
#define COMMAND2_ID	1

// --------------------------------------------------------
//    �O���[�o���ϐ�
// --------------------------------------------------------
#if COMMAND_COUNT > 0
extern PLUGIN_COMMAND_INFO COMMAND_INFO[COMMAND_COUNT];
#endif

// --------------------------------------------------------
//    �֐���`
// --------------------------------------------------------
BOOL Init(void);
void Unload(void);
BOOL Execute(int, HWND);
void Hook(UINT, DWORD, DWORD);
