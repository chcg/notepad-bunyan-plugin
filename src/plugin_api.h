#pragma once

#include <windows.h>
#include <tchar.h>

using PFUNCPLUGINCMD = void (*)();

struct ShortcutKey {
    bool _isCtrl;
    bool _isAlt;
    bool _isShift;
    unsigned char _key;
};

struct FuncItem {
    TCHAR _itemName[64];
    PFUNCPLUGINCMD _pFunc;
    int _cmdID;
    bool _init2Check;
    ShortcutKey* _pShKey;
};

struct NppData {
    HWND _nppHandle;
    HWND _scintillaMainHandle;
    HWND _scintillaSecondHandle;
};

struct toolbarIconsWithDarkMode {
    HBITMAP hToolbarBmp;
    HICON hToolbarIcon;
    HICON hToolbarIconDarkMode;
};

struct DockedWidgetData {
    HWND hClient;
    const wchar_t* pszName;
    int dlgID;
    UINT uMask;
    HICON hIconTab;
    const wchar_t* pszAddInfo;
    RECT rcFloat;
    int iPrevCont;
    const wchar_t* pszModuleName;
};

struct SCNotification {
    NMHDR nmhdr;
};

constexpr UINT NPPMSG = WM_USER + 1000;
constexpr UINT NPPM_MODELESSDIALOG = NPPMSG + 12;
constexpr UINT NPPM_GETCURRENTSCINTILLA = NPPMSG + 4;
constexpr UINT NPPM_DMMSHOW = NPPMSG + 30;
constexpr UINT NPPM_DMMHIDE = NPPMSG + 31;
constexpr UINT NPPM_DMMUPDATEDISPINFO = NPPMSG + 32;
constexpr UINT NPPM_DMMREGASDCKDLG = NPPMSG + 33;
constexpr UINT NPPM_SETMENUITEMCHECK = NPPMSG + 40;
constexpr UINT NPPM_GETCURRENTBUFFERID = NPPMSG + 60;
constexpr UINT NPPM_ADDTOOLBARICON_FORDARKMODE = NPPMSG + 101;

constexpr WPARAM MODELESSDIALOGADD = 0;
constexpr WPARAM MODELESSDIALOGREMOVE = 1;

constexpr UINT DWS_ICONTAB = 0x00000001;
constexpr UINT DWS_ICONBAR = 0x00000002;
constexpr UINT DWS_ADDINFO = 0x00000004;
constexpr UINT DWS_USEOWNDARKMODE = 0x00000008;
constexpr UINT DWS_DF_CONT_LEFT = 0x00000000;
constexpr UINT DWS_DF_CONT_RIGHT = 0x10000000;
constexpr UINT DWS_DF_CONT_TOP = 0x20000000;
constexpr UINT DWS_DF_CONT_BOTTOM = 0x30000000;
constexpr UINT DWS_DF_FLOATING = 0x80000000;

constexpr UINT NPPN_TBMODIFICATION = 1002;
constexpr UINT NPPN_FILEBEFORECLOSE = 1003;
constexpr UINT NPPN_SHUTDOWN = 1009;
constexpr UINT NPPN_BUFFERACTIVATED = 1010;

constexpr UINT SCI_GETTEXT = 2182;
constexpr UINT SCI_GETTEXTLENGTH = 2183;
constexpr UINT SCI_SETTEXT = 2181;
constexpr UINT SCI_GETLINE = 2153;
constexpr UINT SCI_GETLINECOUNT = 2154;
constexpr UINT SCI_GETCODEPAGE = 2137;
constexpr UINT SCI_LINELENGTH = 2350;
constexpr UINT SCI_BEGINUNDOACTION = 2078;
constexpr UINT SCI_ENDUNDOACTION = 2079;