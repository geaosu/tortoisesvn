﻿// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2018, 2020 - TortoiseSVN
// Copyright (C) 2012-2016, 2018-2020 - TortoiseGit

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#include "stdafx.h"
#include "TortoiseUDiff.h"
#include "MainWindow.h"
#include "UnicodeUtils.h"
#include "StringUtils.h"
#include "TaskbarUUID.h"
#include "CreateProcessHelper.h"
#include "UDiffColors.h"
#include "registry.h"
#include "DPIAware.h"
#include "LoadIconEx.h"
#include "Theme.h"
#include "DarkModeHelper.h"

const UINT TaskBarButtonCreated = RegisterWindowMessage(L"TaskbarButtonCreated");

CMainWindow::CMainWindow(HINSTANCE hInst, const WNDCLASSEX* wcx /* = nullptr*/)
    : CWindow(hInst, wcx)
    , m_bShowFindBar(false)
    , m_directFunction(0)
    , m_directPointer(0)
    , m_hWndEdit(nullptr)
    , m_bMatchCase(false)
    , m_themeCallbackId(0)
{
    SetWindowTitle(L"TortoiseUDiff");
}

CMainWindow::~CMainWindow(void)
{
}

void CMainWindow::UpdateLineCount()
{
    auto numberOfLines = static_cast<intptr_t>(SendEditor(SCI_GETLINECOUNT));
    int  numDigits     = 2;
    while (numberOfLines)
    {
        numberOfLines /= 10;
        ++numDigits;
    }
    SendEditor(SCI_SETMARGINWIDTHN, 0, numDigits * static_cast<int>(SendEditor(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<LPARAM>("8"))));
}

bool CMainWindow::RegisterAndCreateWindow()
{
    WNDCLASSEX wcx;

    // Fill in the window class structure with default parameters
    wcx.cbSize = sizeof(WNDCLASSEX);
    wcx.style = CS_HREDRAW | CS_VREDRAW;
    wcx.lpfnWndProc = CWindow::stWinMsgHandler;
    wcx.cbClsExtra = 0;
    wcx.cbWndExtra = 0;
    wcx.hInstance = hResource;
    wcx.hCursor = nullptr;
    ResString clsname(hResource, IDS_APP_TITLE);
    wcx.lpszClassName = clsname;
    wcx.hIcon = LoadIconEx(hResource, MAKEINTRESOURCE(IDI_TORTOISEUDIFF), GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    wcx.hbrBackground = (HBRUSH)(COLOR_3DFACE+1);
    wcx.lpszMenuName = MAKEINTRESOURCE(IDC_TORTOISEUDIFF);
    wcx.hIconSm = LoadIconEx(wcx.hInstance, MAKEINTRESOURCE(IDI_TORTOISEUDIFF));
    if (RegisterWindow(&wcx))
    {
        if (Create(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN, nullptr))
        {
            m_FindBar.SetParent(*this);
            m_FindBar.Create(hResource, IDD_FINDBAR, *this);
            UpdateWindow(*this);
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK CMainWindow::WinMsgHandler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == TaskBarButtonCreated)
    {
        SetUUIDOverlayIcon(hwnd);
    }
    switch (uMsg)
    {
    case WM_CREATE:
        {
            m_hwnd = hwnd;
            Initialize();
        }
        break;
    case WM_COMMAND:
        {
            return DoCommand(LOWORD(wParam));
        }
        break;
    case WM_MOUSEWHEEL:
        {
            if (GET_KEYSTATE_WPARAM(wParam) == MK_SHIFT)
            {
                // scroll sideways
                SendEditor(SCI_LINESCROLL, -GET_WHEEL_DELTA_WPARAM(wParam)/40, 0);
            }
            else
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
        break;
    case WM_SIZE:
        {
            RECT rect;
            GetClientRect(*this, &rect);
            if (m_bShowFindBar)
            {
                ::SetWindowPos(m_hWndEdit, HWND_TOP,
                    rect.left, rect.top,
                    rect.right - rect.left, rect.bottom - rect.top - int(30 * CDPIAware::Instance().ScaleFactor(hwnd)),
                    SWP_SHOWWINDOW);
                ::SetWindowPos(m_FindBar, HWND_TOP,
                    rect.left, rect.bottom - int(30 * CDPIAware::Instance().ScaleFactor(hwnd)),
                    rect.right - rect.left, int(30 * CDPIAware::Instance().ScaleFactor(hwnd)),
                    SWP_SHOWWINDOW);
            }
            else
            {
                ::SetWindowPos(m_hWndEdit, HWND_TOP,
                    rect.left, rect.top,
                    rect.right-rect.left, rect.bottom-rect.top,
                    SWP_SHOWWINDOW);
                ::ShowWindow(m_FindBar, SW_HIDE);
            }
        }
        break;
    case WM_GETMINMAXINFO:
        {
            MINMAXINFO * mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = 100;
            mmi->ptMinTrackSize.y = 100;
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_CLOSE:
        ::DestroyWindow(m_hwnd);
        break;
    case WM_SETFOCUS:
        SetFocus(m_hWndEdit);
        break;
    case WM_SYSCOLORCHANGE:
        CTheme::Instance().OnSysColorChanged();
        CTheme::Instance().SetDarkTheme(CTheme::Instance().IsDarkTheme(), true);
        break;
    case WM_DPICHANGED:
    {
        CDPIAware::Instance().Invalidate();
        SendMessage(m_hWndEdit, WM_DPICHANGED, wParam, lParam);
        const RECT* rect = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(*this, NULL, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        ::RedrawWindow(*this, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_INTERNALPAINT | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
        break;
    case COMMITMONITOR_FINDMSGNEXT:
        {
            SendEditor(SCI_CHARRIGHT);
            SendEditor(SCI_SEARCHANCHOR);
            m_bMatchCase = !!wParam;
            m_findtext = (LPCTSTR)lParam;
            if (SendEditor(SCI_SEARCHNEXT, m_bMatchCase ? SCFIND_MATCHCASE : 0, (LPARAM)CUnicodeUtils::StdGetUTF8(m_findtext).c_str()) == -1)
            {
                FLASHWINFO fwi;
                fwi.cbSize = sizeof(FLASHWINFO);
                fwi.uCount = 3;
                fwi.dwTimeout = 100;
                fwi.dwFlags = FLASHW_ALL;
                fwi.hwnd = m_hwnd;
                FlashWindowEx(&fwi);
            }
            SendEditor(SCI_SCROLLCARET);
        }
        break;
    case COMMITMONITOR_FINDMSGPREV:
        {
            SendEditor(SCI_SEARCHANCHOR);
            m_bMatchCase = !!wParam;
            m_findtext = (LPCTSTR)lParam;
            if (SendEditor(SCI_SEARCHPREV, m_bMatchCase ? SCFIND_MATCHCASE : 0, (LPARAM)CUnicodeUtils::StdGetUTF8(m_findtext).c_str()) == -1)
            {
                FLASHWINFO fwi;
                fwi.cbSize = sizeof(FLASHWINFO);
                fwi.uCount = 3;
                fwi.dwTimeout = 100;
                fwi.dwFlags = FLASHW_ALL;
                fwi.hwnd = m_hwnd;
                FlashWindowEx(&fwi);
            }

            SendEditor(SCI_SCROLLCARET);
        }
        break;
    case COMMITMONITOR_FINDEXIT:
        {
            RECT rect;
            GetClientRect(*this, &rect);
            m_bShowFindBar = false;
            ::ShowWindow(m_FindBar, SW_HIDE);
            ::SetWindowPos(m_hWndEdit, HWND_TOP,
                rect.left, rect.top,
                rect.right-rect.left, rect.bottom-rect.top,
                SWP_SHOWWINDOW);
        }
        break;
    case COMMITMONITOR_FINDRESET:
        SendEditor(SCI_SETSELECTIONSTART, 0);
        SendEditor(SCI_SETSELECTIONEND, 0);
        SendEditor(SCI_SEARCHANCHOR);
        break;
    case WM_NOTIFY:
        if (reinterpret_cast<LPNMHDR>(lParam)->code == SCN_PAINTED)
            UpdateLineCount();
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    return 0;
};

LRESULT CMainWindow::DoCommand(int id)
{
    switch (id)
    {
    case ID_FILE_OPEN:
        loadOrSaveFile(true);
        break;
    case ID_FILE_SAVEAS:
        loadOrSaveFile(false);
        break;
    case ID_FILE_SAVE:
        loadOrSaveFile(false, m_filename);
        break;
    case ID_FILE_EXIT:
        ::PostQuitMessage(0);
        return 0;
    case IDM_SHOWFINDBAR:
        {
            m_bShowFindBar = true;
            ::ShowWindow(m_FindBar, SW_SHOW);
            RECT rect;
            GetClientRect(*this, &rect);
            ::SetWindowPos(m_hWndEdit, HWND_TOP,
                rect.left, rect.top,
                rect.right - rect.left, rect.bottom - rect.top - int(30 * CDPIAware::Instance().ScaleFactor(*this)),
                SWP_SHOWWINDOW);
            ::SetWindowPos(m_FindBar, HWND_TOP,
                rect.left, rect.bottom - int(30 * CDPIAware::Instance().ScaleFactor(*this)),
                rect.right - rect.left, int(30 * CDPIAware::Instance().ScaleFactor(*this)),
                SWP_SHOWWINDOW);
            ::SetFocus(m_FindBar);
            SendEditor(SCI_SETSELECTIONSTART, 0);
            SendEditor(SCI_SETSELECTIONEND, 0);
            SendEditor(SCI_SEARCHANCHOR);
        }
        break;
    case IDM_FINDNEXT:
        SendEditor(SCI_CHARRIGHT);
        SendEditor(SCI_SEARCHANCHOR);
        if (SendEditor(SCI_SEARCHNEXT, m_bMatchCase ? SCFIND_MATCHCASE : 0, (LPARAM)CUnicodeUtils::StdGetUTF8(m_findtext).c_str()) == -1)
        {
            FLASHWINFO fwi;
            fwi.cbSize = sizeof(FLASHWINFO);
            fwi.uCount = 3;
            fwi.dwTimeout = 100;
            fwi.dwFlags = FLASHW_ALL;
            fwi.hwnd = m_hwnd;
            FlashWindowEx(&fwi);
        }
        SendEditor(SCI_SCROLLCARET);
        break;
    case IDM_FINDPREV:
        SendEditor(SCI_SEARCHANCHOR);
        if (SendEditor(SCI_SEARCHPREV, m_bMatchCase ? SCFIND_MATCHCASE : 0, (LPARAM)CUnicodeUtils::StdGetUTF8(m_findtext).c_str()) == -1)
        {
            FLASHWINFO fwi;
            fwi.cbSize = sizeof(FLASHWINFO);
            fwi.uCount = 3;
            fwi.dwTimeout = 100;
            fwi.dwFlags = FLASHW_ALL;
            fwi.hwnd = m_hwnd;
            FlashWindowEx(&fwi);
        }
        SendEditor(SCI_SCROLLCARET);
        break;
    case IDM_FINDEXIT:
        if (IsWindowVisible(m_FindBar))
        {
            RECT rect;
            GetClientRect(*this, &rect);
            m_bShowFindBar = false;
            ::ShowWindow(m_FindBar, SW_HIDE);
            ::SetWindowPos(m_hWndEdit, HWND_TOP,
                rect.left, rect.top,
                rect.right-rect.left, rect.bottom-rect.top,
                SWP_SHOWWINDOW);
        }
        else
            PostQuitMessage(0);
        break;
    case ID_FILE_SETTINGS:
        {
            tstring svnCmd = L" /command:settings /page:20";
            RunCommand(svnCmd);
        }
        break;
    case ID_FILE_APPLYPATCH:
        {
            std::wstring command = L" /diff:\"";
            command += m_filename;
            command += L"\"";
            std::wstring tortoiseMergePath = GetAppDirectory() + L"TortoiseMerge.exe";
            CCreateProcessHelper::CreateProcessDetached(tortoiseMergePath.c_str(), command.c_str());
        }
        break;
    case ID_FILE_PAGESETUP:
        {
            TCHAR localeInfo[3] = { 0 };
            GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_IMEASURE, localeInfo, 3);
            // Metric system. '1' is US System
            int defaultMargin = localeInfo[0] == '0' ? 2540 : 1000;

            PAGESETUPDLG pdlg = {0};
            pdlg.lStructSize = sizeof(PAGESETUPDLG);
            pdlg.hwndOwner = *this;
            pdlg.hInstance = nullptr;
            pdlg.Flags = PSD_DEFAULTMINMARGINS|PSD_MARGINS|PSD_DISABLEPAPER|PSD_DISABLEORIENTATION;
            if (localeInfo[0] == '0')
                pdlg.Flags |= PSD_INHUNDREDTHSOFMILLIMETERS;

            CRegStdDWORD m_regMargLeft   = CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffpagesetupmarginleft", defaultMargin);
            CRegStdDWORD m_regMargTop    = CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffpagesetupmargintop", defaultMargin);
            CRegStdDWORD m_regMargRight  = CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffpagesetupmarginright", defaultMargin);
            CRegStdDWORD m_regMargBottom = CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffpagesetupmarginbottom", defaultMargin);

            pdlg.rtMargin.left   = (long)(DWORD)m_regMargLeft;
            pdlg.rtMargin.top    = (long)(DWORD)m_regMargTop;
            pdlg.rtMargin.right  = (long)(DWORD)m_regMargRight;
            pdlg.rtMargin.bottom = (long)(DWORD)m_regMargBottom;

            if (!PageSetupDlg(&pdlg))
                return false;

            m_regMargLeft   = pdlg.rtMargin.left;
            m_regMargTop    = pdlg.rtMargin.top;
            m_regMargRight  = pdlg.rtMargin.right;
            m_regMargBottom = pdlg.rtMargin.bottom;
        }
        break;
    case ID_FILE_PRINT:
        {
            PRINTDLGEX pdlg = {0};
            pdlg.lStructSize = sizeof(PRINTDLGEX);
            pdlg.hwndOwner = *this;
            pdlg.hInstance = nullptr;
            pdlg.Flags = PD_USEDEVMODECOPIESANDCOLLATE | PD_ALLPAGES | PD_RETURNDC | PD_NOCURRENTPAGE | PD_NOPAGENUMS;
            pdlg.nMinPage = 1;
            pdlg.nMaxPage = 0xffffU; // We do not know how many pages in the document
            pdlg.nCopies = 1;
            pdlg.hDC = 0;
            pdlg.nStartPage = START_PAGE_GENERAL;

            // See if a range has been selected
            size_t startPos = SendEditor(SCI_GETSELECTIONSTART);
            size_t endPos = SendEditor(SCI_GETSELECTIONEND);

            if (startPos == endPos)
                pdlg.Flags |= PD_NOSELECTION;
            else
                pdlg.Flags |= PD_SELECTION;

            HRESULT hResult = PrintDlgEx(&pdlg);
            if ((hResult != S_OK) || (pdlg.dwResultAction != PD_RESULT_PRINT))
                return 0;

            // reset all indicators
            size_t endpos = SendEditor(SCI_GETLENGTH);
            for (int i = INDIC_CONTAINER; i <= INDIC_MAX; ++i)
            {
                SendEditor(SCI_SETINDICATORCURRENT, i);
                SendEditor(SCI_INDICATORCLEARRANGE, 0, endpos);
            }
            // store and reset UI settings
            int viewws = (int)SendEditor(SCI_GETVIEWWS);
            SendEditor(SCI_SETVIEWWS, 0);
            int edgemode = (int)SendEditor(SCI_GETEDGEMODE);
            SendEditor(SCI_SETEDGEMODE, EDGE_NONE);
            SendEditor(SCI_SETWRAPVISUALFLAGS, SC_WRAPVISUALFLAG_END);

            HDC hdc = pdlg.hDC;

            RECT rectMargins, rectPhysMargins;
            POINT ptPage;
            POINT ptDpi;

            // Get printer resolution
            ptDpi.x = GetDeviceCaps(hdc, LOGPIXELSX);    // dpi in X direction
            ptDpi.y = GetDeviceCaps(hdc, LOGPIXELSY);    // dpi in Y direction

            // Start by getting the physical page size (in device units).
            ptPage.x = GetDeviceCaps(hdc, PHYSICALWIDTH);   // device units
            ptPage.y = GetDeviceCaps(hdc, PHYSICALHEIGHT);  // device units

            // Get the dimensions of the unprintable
            // part of the page (in device units).
            rectPhysMargins.left = GetDeviceCaps(hdc, PHYSICALOFFSETX);
            rectPhysMargins.top = GetDeviceCaps(hdc, PHYSICALOFFSETY);

            // To get the right and lower unprintable area,
            // we take the entire width and height of the paper and
            // subtract everything else.
            rectPhysMargins.right = ptPage.x                        // total paper width
                - GetDeviceCaps(hdc, HORZRES)                       // printable width
                - rectPhysMargins.left;                             // left unprintable margin

            rectPhysMargins.bottom = ptPage.y                       // total paper height
                - GetDeviceCaps(hdc, VERTRES)                       // printable height
                - rectPhysMargins.top;                              // right unprintable margin

            TCHAR localeInfo[3] = { 0 };
            GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_IMEASURE, localeInfo, 3);
            // Metric system. '1' is US System
            int defaultMargin = localeInfo[0] == '0' ? 2540 : 1000;
            RECT pagesetupMargin;
            CRegStdDWORD m_regMargLeft   = CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffpagesetupmarginleft", defaultMargin);
            CRegStdDWORD m_regMargTop    = CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffpagesetupmargintop", defaultMargin);
            CRegStdDWORD m_regMargRight  = CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffpagesetupmarginright", defaultMargin);
            CRegStdDWORD m_regMargBottom = CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffpagesetupmarginbottom", defaultMargin);

            pagesetupMargin.left   = (long)(DWORD)m_regMargLeft;
            pagesetupMargin.top    = (long)(DWORD)m_regMargTop;
            pagesetupMargin.right  = (long)(DWORD)m_regMargRight;
            pagesetupMargin.bottom = (long)(DWORD)m_regMargBottom;

            if (pagesetupMargin.left != 0 || pagesetupMargin.right != 0 ||
                pagesetupMargin.top != 0 || pagesetupMargin.bottom != 0)
            {
                RECT rectSetup;

                // Convert the hundredths of millimeters (HiMetric) or
                // thousandths of inches (HiEnglish) margin values
                // from the Page Setup dialog to device units.
                // (There are 2540 hundredths of a mm in an inch.)
                if (localeInfo[0] == '0')
                {
                    // Metric system. '1' is US System
                    rectSetup.left      = MulDiv (pagesetupMargin.left, ptDpi.x, 2540);
                    rectSetup.top       = MulDiv (pagesetupMargin.top, ptDpi.y, 2540);
                    rectSetup.right     = MulDiv(pagesetupMargin.right, ptDpi.x, 2540);
                    rectSetup.bottom    = MulDiv(pagesetupMargin.bottom, ptDpi.y, 2540);
                }
                else
                {
                    rectSetup.left      = MulDiv(pagesetupMargin.left, ptDpi.x, 1000);
                    rectSetup.top       = MulDiv(pagesetupMargin.top, ptDpi.y, 1000);
                    rectSetup.right     = MulDiv(pagesetupMargin.right, ptDpi.x, 1000);
                    rectSetup.bottom    = MulDiv(pagesetupMargin.bottom, ptDpi.y, 1000);
                }

                // Don't reduce margins below the minimum printable area
                rectMargins.left    = max(rectPhysMargins.left, rectSetup.left);
                rectMargins.top     = max(rectPhysMargins.top, rectSetup.top);
                rectMargins.right   = max(rectPhysMargins.right, rectSetup.right);
                rectMargins.bottom  = max(rectPhysMargins.bottom, rectSetup.bottom);
            }
            else
            {
                rectMargins.left    = rectPhysMargins.left;
                rectMargins.top     = rectPhysMargins.top;
                rectMargins.right   = rectPhysMargins.right;
                rectMargins.bottom  = rectPhysMargins.bottom;
            }

            // rectMargins now contains the values used to shrink the printable
            // area of the page.

            // Convert device coordinates into logical coordinates
            DPtoLP(hdc, (LPPOINT) &rectMargins, 2);
            DPtoLP(hdc, (LPPOINT)&rectPhysMargins, 2);

            // Convert page size to logical units and we're done!
            DPtoLP(hdc, (LPPOINT) &ptPage, 1);


            DOCINFO di = {sizeof(DOCINFO), 0, 0, 0, 0};
            di.lpszDocName = m_filename.c_str();
            di.lpszOutput = 0;
            di.lpszDatatype = 0;
            di.fwType = 0;
            if (::StartDoc(hdc, &di) < 0)
            {
                ::DeleteDC(hdc);
                return 0;
            }

            size_t lengthDoc = SendEditor(SCI_GETLENGTH);
            size_t lengthDocMax = lengthDoc;
            size_t lengthPrinted = 0;

            // Requested to print selection
            if (pdlg.Flags & PD_SELECTION)
            {
                if (startPos > endPos)
                {
                    lengthPrinted = endPos;
                    lengthDoc = startPos;
                }
                else
                {
                    lengthPrinted = startPos;
                    lengthDoc = endPos;
                }

                if (lengthDoc > lengthDocMax)
                    lengthDoc = lengthDocMax;
            }

            // We must subtract the physical margins from the printable area
            Sci_RangeToFormat frPrint;
            frPrint.hdc             = hdc;
            frPrint.hdcTarget       = hdc;
            frPrint.rc.left         = rectMargins.left - rectPhysMargins.left;
            frPrint.rc.top          = rectMargins.top - rectPhysMargins.top;
            frPrint.rc.right        = ptPage.x - rectMargins.right - rectPhysMargins.left;
            frPrint.rc.bottom       = ptPage.y - rectMargins.bottom - rectPhysMargins.top;
            frPrint.rcPage.left     = 0;
            frPrint.rcPage.top      = 0;
            frPrint.rcPage.right    = ptPage.x - rectPhysMargins.left - rectPhysMargins.right - 1;
            frPrint.rcPage.bottom   = ptPage.y - rectPhysMargins.top - rectPhysMargins.bottom - 1;

            // Print each page
            while (lengthPrinted < lengthDoc)
            {
                ::StartPage(hdc);

                frPrint.chrg.cpMin = (long)lengthPrinted;
                frPrint.chrg.cpMax = (long)lengthDoc;

                lengthPrinted = SendEditor(SCI_FORMATRANGE, true, reinterpret_cast<LPARAM>(&frPrint));

                ::EndPage(hdc);
            }

            SendEditor(SCI_FORMATRANGE, FALSE, 0);

            ::EndDoc(hdc);
            ::DeleteDC(hdc);

            if (pdlg.hDevMode)
                GlobalFree(pdlg.hDevMode);
            if (pdlg.hDevNames)
                GlobalFree(pdlg.hDevNames);
            if (pdlg.lpPageRanges)
                GlobalFree(pdlg.lpPageRanges);

            // reset the UI
            SendEditor(SCI_SETVIEWWS, viewws);
            SendEditor(SCI_SETEDGEMODE, edgemode);
            SendEditor(SCI_SETWRAPVISUALFLAGS, SC_WRAPVISUALFLAG_NONE);
        }
        break;
    case ID_VIEW_DARKMODE:
        {
            CTheme::Instance().SetDarkTheme(!CTheme::Instance().IsDarkTheme());
        }
        break;
    default:
        break;
    };
    return 1;
}

std::wstring CMainWindow::GetAppDirectory()
{
    std::wstring path;
    DWORD len = 0;
    DWORD bufferlen = MAX_PATH;     // MAX_PATH is not the limit here!
    do
    {
        bufferlen += MAX_PATH;      // MAX_PATH is not the limit here!
        auto pBuf = std::make_unique<TCHAR[]>(bufferlen);
        len = GetModuleFileName(nullptr, pBuf.get(), bufferlen);
        path = std::wstring(pBuf.get(), len);
    } while(len == bufferlen);
    path = path.substr(0, path.rfind('\\') + 1);

    return path;
}

void CMainWindow::RunCommand(const std::wstring& command)
{
    tstring tortoiseProcPath = GetAppDirectory() + L"TortoiseProc.exe";
    CCreateProcessHelper::CreateProcessDetached(tortoiseProcPath.c_str(), command.c_str());
}

LRESULT CMainWindow::SendEditor(UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (m_directFunction)
    {
        return ((SciFnDirect) m_directFunction)(m_directPointer, Msg, wParam, lParam);
    }
    return ::SendMessage(m_hWndEdit, Msg, wParam, lParam);
}

bool CMainWindow::Initialize()
{
    m_themeCallbackId = CTheme::Instance().RegisterThemeChangeCallback(
        [this]()
        {
            SetTheme(CTheme::Instance().IsDarkTheme());
        });
    m_hWndEdit = ::CreateWindow(
        L"Scintilla",
        L"Source",
        WS_CHILD | WS_VSCROLL | WS_HSCROLL | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        *this,
        nullptr,
        hResource,
        nullptr);
    if (!m_hWndEdit)
        return false;

    RECT rect;
    GetClientRect(*this, &rect);
    ::SetWindowPos(m_hWndEdit, HWND_TOP,
        rect.left, rect.top,
        rect.right-rect.left, rect.bottom-rect.top,
        SWP_SHOWWINDOW);

    m_directFunction = SendMessage(m_hWndEdit, SCI_GETDIRECTFUNCTION, 0, 0);
    m_directPointer = SendMessage(m_hWndEdit, SCI_GETDIRECTPOINTER, 0, 0);

    // Set up the global default style. These attributes are used wherever no explicit choices are made.
    CRegStdDWORD used2d(L"Software\\TortoiseSVN\\ScintillaDirect2D", TRUE);
    bool enabled2d = false;
    if (DWORD(used2d))
        enabled2d = true;
    SendEditor(SCI_SETTABWIDTH, CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffTabSize", 4));
    SendEditor(SCI_SETREADONLY, TRUE);
    UpdateLineCount();
    SendEditor(SCI_SETMARGINWIDTHN, 1);
    SendEditor(SCI_SETMARGINWIDTHN, 2);
    //Set the default windows colors for edit controls
    if (enabled2d)
    {
        SendEditor(SCI_SETTECHNOLOGY, SC_TECHNOLOGY_DIRECTWRITERETAIN);
        SendEditor(SCI_SETBUFFEREDDRAW, 0);
    }
    SendEditor(SCI_SETVIEWWS, 1);
    SendEditor(SCI_SETWHITESPACESIZE, 2);
    SendEditor(SCI_STYLESETVISIBLE, STYLE_CONTROLCHAR, TRUE);

    SetTheme(CTheme::Instance().IsDarkTheme());
    return true;
}

void CMainWindow::SetTheme(bool bDark)
{
    auto fontNameW = CRegStdString(L"Software\\TortoiseSVN\\UDiffFontName", L"Consolas");
    auto fontName  = CUnicodeUtils::StdGetUTF8(fontNameW);

    if (bDark)
    {
        DarkModeHelper::Instance().AllowDarkModeForApp(TRUE);

        DarkModeHelper::Instance().AllowDarkModeForWindow(*this, TRUE);
        SetClassLongPtr(*this, GCLP_HBRBACKGROUND, (LONG_PTR)GetStockObject(BLACK_BRUSH));
        if (FAILED(SetWindowTheme(*this, L"DarkMode_Explorer", nullptr)))
            SetWindowTheme(*this, L"Explorer", nullptr);
        DarkModeHelper::Instance().AllowDarkModeForWindow(m_hWndEdit, TRUE);
        if (FAILED(SetWindowTheme(m_hWndEdit, L"DarkMode_Explorer", nullptr)))
            SetWindowTheme(m_hWndEdit, L"Explorer", nullptr);
        BOOL darkFlag = TRUE;
        DarkModeHelper::WINDOWCOMPOSITIONATTRIBDATA data = { DarkModeHelper::WINDOWCOMPOSITIONATTRIB::WCA_USEDARKMODECOLORS, &darkFlag, sizeof(darkFlag) };
        DarkModeHelper::Instance().SetWindowCompositionAttribute(*this, &data);
        DarkModeHelper::Instance().FlushMenuThemes();
        DarkModeHelper::Instance().RefreshImmersiveColorPolicyState();
    }
    else
    {
        DarkModeHelper::Instance().AllowDarkModeForWindow(*this, FALSE);
        DarkModeHelper::Instance().AllowDarkModeForWindow(m_hWndEdit, FALSE);
        BOOL darkFlag = FALSE;
        DarkModeHelper::WINDOWCOMPOSITIONATTRIBDATA data = { DarkModeHelper::WINDOWCOMPOSITIONATTRIB::WCA_USEDARKMODECOLORS, &darkFlag, sizeof(darkFlag) };
        DarkModeHelper::Instance().SetWindowCompositionAttribute(*this, &data);
        DarkModeHelper::Instance().FlushMenuThemes();
        DarkModeHelper::Instance().RefreshImmersiveColorPolicyState();
        DarkModeHelper::Instance().AllowDarkModeForApp(FALSE);
        SetClassLongPtr(*this, GCLP_HBRBACKGROUND, (LONG_PTR)GetSysColorBrush(COLOR_3DFACE));
        SetWindowTheme(*this, L"Explorer", nullptr);
        SetWindowTheme(m_hWndEdit, L"Explorer", nullptr);
    }
    DarkModeHelper::Instance().RefreshTitleBarThemeColor(*this, bDark);

    if (bDark || CTheme::Instance().IsHighContrastModeDark())
    {
        SendEditor(SCI_STYLESETFORE, STYLE_DEFAULT, UDiffTextColorDark);
        SendEditor(SCI_STYLESETBACK, STYLE_DEFAULT, UDiffBackColorDark);
        SendEditor(SCI_SETSELFORE, TRUE, CTheme::Instance().GetThemeColor(::GetSysColor(COLOR_HIGHLIGHTTEXT)));
        SendEditor(SCI_SETSELBACK, TRUE, CTheme::Instance().GetThemeColor(::GetSysColor(COLOR_HIGHLIGHT)));
        SendEditor(SCI_SETCARETFORE, UDiffTextColorDark);
        SendEditor(SCI_SETWHITESPACEFORE, true, RGB(180, 180, 180));
        SetAStyle(STYLE_DEFAULT, UDiffTextColorDark, UDiffBackColorDark,
                  CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffFontSize", 10), fontName.c_str());
        SetAStyle(SCE_DIFF_DEFAULT, UDiffTextColorDark, UDiffBackColorDark,
                  CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffFontSize", 10), fontName.c_str());
        SetAStyle(SCE_DIFF_COMMAND,
                  CRegStdDWORD(L"Software\\TortoiseSVN\\DarkUDiffForeCommandColor", UDIFF_COLORFORECOMMAND_DARK),
                  CRegStdDWORD(L"Software\\TortoiseSVN\\DarkUDiffBackCommandColor", UDIFF_COLORBACKCOMMAND_DARK));
        SetAStyle(SCE_DIFF_POSITION,
                  CRegStdDWORD(L"Software\\TortoiseSVN\\DarkUDiffForePositionColor", UDIFF_COLORFOREPOSITION_DARK),
                  CRegStdDWORD(L"Software\\TortoiseSVN\\DarkUDiffBackPositionColor", UDIFF_COLORBACKPOSITION_DARK));
        SetAStyle(SCE_DIFF_HEADER,
                  CRegStdDWORD(L"Software\\TortoiseSVN\\DarkUDiffForeHeaderColor", UDIFF_COLORFOREHEADER_DARK),
                  CRegStdDWORD(L"Software\\TortoiseSVN\\DarkUDiffBackHeaderColor", UDIFF_COLORBACKHEADER_DARK));
        SetAStyle(SCE_DIFF_COMMENT,
                  CRegStdDWORD(L"Software\\TortoiseSVN\\DarkUDiffForeCommentColor", UDIFF_COLORFORECOMMENT_DARK),
                  CRegStdDWORD(L"Software\\TortoiseSVN\\DarkUDiffBackCommentColor", UDIFF_COLORBACKCOMMENT_DARK));
        for (int style : {SCE_DIFF_ADDED, SCE_DIFF_PATCH_ADD, SCE_DIFF_PATCH_DELETE})
        {
            SetAStyle(style,
                      CRegStdDWORD(L"Software\\TortoiseSVN\\DarkUDiffForeAddedColor", UDIFF_COLORFOREADDED_DARK),
                      CRegStdDWORD(L"Software\\TortoiseSVN\\DarkUDiffBackAddedColor", UDIFF_COLORBACKADDED_DARK));
        }
        for (int style : {SCE_DIFF_DELETED, SCE_DIFF_REMOVED_PATCH_ADD, SCE_DIFF_REMOVED_PATCH_DELETE})
        {
            SetAStyle(style,
                      CRegStdDWORD(L"Software\\TortoiseSVN\\DarkUDiffForeRemovedColor", UDIFF_COLORFOREREMOVED_DARK),
                      CRegStdDWORD(L"Software\\TortoiseSVN\\DarkUDiffBackRemovedColor", UDIFF_COLORBACKREMOVED_DARK));
        }

        SendEditor(SCI_STYLESETFORE, STYLE_BRACELIGHT, RGB(0, 150, 0));
        SendEditor(SCI_STYLESETBOLD, STYLE_BRACELIGHT, 1);
        SendEditor(SCI_STYLESETFORE, STYLE_BRACEBAD, RGB(255, 0, 0));
        SendEditor(SCI_STYLESETBOLD, STYLE_BRACEBAD, 1);
        SendEditor(SCI_SETFOLDMARGINCOLOUR, true, UDiffTextColorDark);
        SendEditor(SCI_SETFOLDMARGINHICOLOUR, true, CTheme::darkBkColor);
        SendEditor(SCI_STYLESETFORE, STYLE_LINENUMBER, RGB(140, 140, 140));
        SendEditor(SCI_STYLESETBACK, STYLE_LINENUMBER, UDiffBackColorDark);
    }
    else
    {
        SendEditor(SCI_STYLESETFORE, STYLE_DEFAULT, ::GetSysColor(COLOR_WINDOWTEXT));
        SendEditor(SCI_STYLESETBACK, STYLE_DEFAULT, ::GetSysColor(COLOR_WINDOW));
        SendEditor(SCI_SETSELFORE, TRUE, ::GetSysColor(COLOR_HIGHLIGHTTEXT));
        SendEditor(SCI_SETSELBACK, TRUE, ::GetSysColor(COLOR_HIGHLIGHT));
        SendEditor(SCI_SETCARETFORE, ::GetSysColor(COLOR_WINDOWTEXT));
        SendEditor(SCI_SETWHITESPACEFORE, true, ::GetSysColor(COLOR_3DSHADOW));
        SetAStyle(STYLE_DEFAULT, ::GetSysColor(COLOR_WINDOWTEXT), ::GetSysColor(COLOR_WINDOW),
                  CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffFontSize", 10), fontName.c_str());
        SetAStyle(SCE_DIFF_DEFAULT, ::GetSysColor(COLOR_WINDOWTEXT), ::GetSysColor(COLOR_WINDOW),
                  CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffFontSize", 10), fontName.c_str());
        SetAStyle(SCE_DIFF_COMMAND,
                  CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffForeCommandColor", UDIFF_COLORFORECOMMAND),
                  CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffBackCommandColor", UDIFF_COLORBACKCOMMAND));
        SetAStyle(SCE_DIFF_POSITION,
                  CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffForePositionColor", UDIFF_COLORFOREPOSITION),
                  CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffBackPositionColor", UDIFF_COLORBACKPOSITION));
        SetAStyle(SCE_DIFF_HEADER,
                  CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffForeHeaderColor", UDIFF_COLORFOREHEADER),
                  CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffBackHeaderColor", UDIFF_COLORBACKHEADER));
        SetAStyle(SCE_DIFF_COMMENT,
                  CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffForeCommentColor", UDIFF_COLORFORECOMMENT),
                  CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffBackCommentColor", UDIFF_COLORBACKCOMMENT));
        for (int style : {SCE_DIFF_ADDED, SCE_DIFF_PATCH_ADD, SCE_DIFF_PATCH_DELETE})
        {
            SetAStyle(style,
                      CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffForeAddedColor", UDIFF_COLORFOREADDED),
                      CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffBackAddedColor", UDIFF_COLORBACKADDED));
        }
        for (int style : {SCE_DIFF_DELETED, SCE_DIFF_REMOVED_PATCH_ADD, SCE_DIFF_REMOVED_PATCH_DELETE})
        {
            SetAStyle(style,
                      CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffForeRemovedColor", UDIFF_COLORFOREREMOVED),
                      CRegStdDWORD(L"Software\\TortoiseSVN\\UDiffBackRemovedColor", UDIFF_COLORBACKREMOVED));
        }

        SendEditor(SCI_STYLESETFORE, STYLE_BRACELIGHT, RGB(0, 150, 0));
        SendEditor(SCI_STYLESETBOLD, STYLE_BRACELIGHT, 1);
        SendEditor(SCI_STYLESETFORE, STYLE_BRACEBAD, RGB(255, 0, 0));
        SendEditor(SCI_STYLESETBOLD, STYLE_BRACEBAD, 1);
        SendEditor(SCI_SETFOLDMARGINCOLOUR, true, RGB(240, 240, 240));
        SendEditor(SCI_SETFOLDMARGINHICOLOUR, true, RGB(255, 255, 255));
        SendEditor(SCI_STYLESETFORE, STYLE_LINENUMBER, RGB(109, 109, 109));
        SendEditor(SCI_STYLESETBACK, STYLE_LINENUMBER, RGB(230, 230, 230));
    }

    auto curlexer = SendEditor(SCI_GETLEXER);
    if (CTheme::Instance().IsHighContrastMode() && curlexer != SCLEX_NULL)
    {
        SendEditor(SCI_CLEARDOCUMENTSTYLE, 0, 0);
        SendEditor(SCI_SETLEXER, SCLEX_NULL);
        SendEditor(SCI_COLOURISE, 0, -1);
    }
    else if (!CTheme::Instance().IsHighContrastMode() && curlexer != SCLEX_DIFF)
    {
        SendEditor(SCI_CLEARDOCUMENTSTYLE, 0, 0);
        SendEditor(SCI_STYLESETBOLD, SCE_DIFF_COMMENT, TRUE);        
        SendEditor(SCI_SETLEXER, SCLEX_DIFF);
        SendEditor(SCI_STYLESETBOLD, SCE_DIFF_COMMENT, TRUE);
        SendEditor(SCI_SETKEYWORDS, 0, reinterpret_cast<LPARAM>("revision"));
        SendEditor(SCI_COLOURISE, 0, -1);
    }

    HMENU hMenu = GetMenu(*this);
    UINT uCheck = MF_BYCOMMAND;
    uCheck |= CTheme::Instance().IsDarkTheme() ? MF_CHECKED : MF_UNCHECKED;
    CheckMenuItem(hMenu, ID_VIEW_DARKMODE, uCheck);
    UINT uEnabled = MF_BYCOMMAND;
    uEnabled |= CTheme::Instance().IsDarkModeAllowed() ? MF_ENABLED : MF_DISABLED;
    EnableMenuItem(hMenu, ID_VIEW_DARKMODE, uEnabled);

    ::RedrawWindow(*this, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_INTERNALPAINT | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

bool CMainWindow::LoadFile(HANDLE hFile)
{
    InitEditor();
    char data[4096] = { 0 };
    DWORD dwRead = 0;

    BOOL bRet = ReadFile(hFile, data, sizeof(data), &dwRead, nullptr);
    bool bUTF8 = IsUTF8(data, dwRead);
    while ((dwRead > 0) && (bRet))
    {
        SendEditor(SCI_ADDTEXT, dwRead,
            reinterpret_cast<LPARAM>(static_cast<char *>(data)));
        bRet = ReadFile(hFile, data, sizeof(data), &dwRead, nullptr);
    }
    SetupWindow(bUTF8);
    return true;
}

bool CMainWindow::LoadFile(LPCTSTR filename)
{
    InitEditor();
    FILE* fp = nullptr;
    _wfopen_s(&fp, filename, L"rb");
    if (!fp)
        return false;

    //SetTitle();
    char data[4096] = { 0 };
    size_t lenFile = fread(data, 1, sizeof(data), fp);
    bool bUTF8 = IsUTF8(data, lenFile);
    while (lenFile > 0)
    {
        SendEditor(SCI_ADDTEXT, lenFile,
            reinterpret_cast<LPARAM>(static_cast<char *>(data)));
        lenFile = fread(data, 1, sizeof(data), fp);
    }
    fclose(fp);
    SetupWindow(bUTF8);
    m_filename = filename;
    return true;
}

void CMainWindow::InitEditor()
{
    SendEditor(SCI_SETREADONLY, FALSE);
    SendEditor(SCI_CLEARALL);
    SendEditor(EM_EMPTYUNDOBUFFER);
    SendEditor(SCI_SETSAVEPOINT);
    SendEditor(SCI_CANCEL);
    SendEditor(SCI_SETUNDOCOLLECTION, 0);
}

void CMainWindow::SetupWindow(bool bUTF8)
{
    SendEditor(SCI_SETCODEPAGE, bUTF8 ? SC_CP_UTF8 : GetACP());

    SendEditor(SCI_SETUNDOCOLLECTION, 1);
    ::SetFocus(m_hWndEdit);
    SendEditor(EM_EMPTYUNDOBUFFER);
    SendEditor(SCI_SETSAVEPOINT);
    SendEditor(SCI_GOTOPOS, 0);

    ::ShowWindow(m_hWndEdit, SW_SHOW);
}

bool CMainWindow::SaveFile(LPCTSTR filename)
{
    FILE* fp = nullptr;
    _wfopen_s(&fp, filename, L"w+b");
    if (!fp)
    {
        TCHAR fmt[1024] = {0};
        LoadString(hResource, IDS_ERRORSAVE, fmt, _countof(fmt));
        TCHAR error[1024] = {0};
        _snwprintf_s(error, _countof(error), fmt, filename, (LPCTSTR)CFormatMessageWrapper());
        MessageBox(*this, error, L"TortoiseUDiff", MB_OK);
        return false;
    }

    LRESULT len = SendEditor(SCI_GETTEXT, 0, 0);
    auto data = std::make_unique<char[]>(len + 1);
    SendEditor(SCI_GETTEXT, len, reinterpret_cast<LPARAM>(static_cast<char *>(data.get())));
    fwrite(data.get(), sizeof(char), len-1, fp);
    fclose(fp);

    SendEditor(SCI_SETSAVEPOINT);
    ::ShowWindow(m_hWndEdit, SW_SHOW);
    return true;
}

void CMainWindow::SetTitle(LPCTSTR title)
{
    size_t len = wcslen(title);
    auto pBuf = std::make_unique<TCHAR[]>(len + 40);
    swprintf_s(pBuf.get(), len+40, L"%s - TortoiseUDiff", title);
    SetWindowTitle(std::wstring(pBuf.get()));
}

void CMainWindow::SetAStyle(int style, COLORREF fore, COLORREF back, int size, const char *face)
{
    SendEditor(SCI_STYLESETFORE, style, fore);
    SendEditor(SCI_STYLESETBACK, style, back);
    if (size >= 1)
        SendEditor(SCI_STYLESETSIZE, style, size);
    if (face)
        SendEditor(SCI_STYLESETFONT, style, reinterpret_cast<LPARAM>(face));
}

bool CMainWindow::IsUTF8(LPVOID pBuffer, size_t cb)
{
    if (cb < 2)
        return true;
    UINT16 * pVal16 = (UINT16 *)pBuffer;
    UINT8 * pVal8 = (UINT8 *)(pVal16+1);
    // scan the whole buffer for a 0x0000 sequence
    // if found, we assume a binary file
    for (size_t i=0; i<(cb-2); i=i+2)
    {
        if (0x0000 == *pVal16++)
            return false;
    }
    pVal16 = (UINT16 *)pBuffer;
    if (*pVal16 == 0xFEFF)
        return false;
    if (cb < 3)
        return false;
    if (*pVal16 == 0xBBEF)
    {
        if (*pVal8 == 0xBF)
            return true;
    }
    // check for illegal UTF8 chars
    pVal8 = (UINT8 *)pBuffer;
    for (size_t i=0; i<cb; ++i)
    {
        if ((*pVal8 == 0xC0)||(*pVal8 == 0xC1)||(*pVal8 >= 0xF5))
            return false;
        pVal8++;
    }
    pVal8 = (UINT8 *)pBuffer;
    bool bUTF8 = false;
    for (size_t i=0; i<(cb-3); ++i)
    {
        if ((*pVal8 & 0xE0)==0xC0)
        {
            pVal8++;i++;
            if ((*pVal8 & 0xC0)!=0x80)
                return false;
            bUTF8 = true;
        }
        else if ((*pVal8 & 0xF0)==0xE0)
        {
            pVal8++;i++;
            if ((*pVal8 & 0xC0)!=0x80)
                return false;
            pVal8++;i++;
            if ((*pVal8 & 0xC0)!=0x80)
                return false;
            bUTF8 = true;
        }
        else if ((*pVal8 & 0xF8)==0xF0)
        {
            pVal8++;i++;
            if ((*pVal8 & 0xC0)!=0x80)
                return false;
            pVal8++;i++;
            if ((*pVal8 & 0xC0)!=0x80)
                return false;
            pVal8++;i++;
            if ((*pVal8 & 0xC0)!=0x80)
                return false;
            bUTF8 = true;
        }
        else if (*pVal8 >= 0x80)
            return false;

        pVal8++;
    }
    if (bUTF8)
        return true;
    return false;
}

void CMainWindow::loadOrSaveFile(bool doLoad, const std::wstring& filename /* = L"" */)
{
    OPENFILENAME ofn = {0};             // common dialog box structure
    TCHAR szFile[MAX_PATH] = {0};       // buffer for file name
    // Initialize OPENFILENAME
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = *this;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile)/sizeof(TCHAR);
    TCHAR filter[1024] = { 0 };
    LoadString(hResource, IDS_PATCHFILEFILTER, filter, sizeof(filter)/sizeof(TCHAR));
    CStringUtils::PipesToNulls(filter);
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.lpstrDefExt = L"diff";
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    TCHAR fileTitle[1024] = { 0 };
    LoadString(hResource, doLoad ? IDS_OPENPATCH : IDS_SAVEPATCH, fileTitle, sizeof(fileTitle)/sizeof(TCHAR));
    ofn.lpstrTitle = fileTitle;
    ofn.Flags = OFN_ENABLESIZING | OFN_EXPLORER;
    if(doLoad)
        ofn.Flags |= OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    else
        ofn.Flags |= OFN_OVERWRITEPROMPT;
    // Display the Open dialog box.
    if( doLoad )
    {
        if (GetOpenFileName(&ofn)==TRUE)
        {
            LoadFile(ofn.lpstrFile);
        }
    }
    else
    {
        if (filename.empty())
        {
            if (GetSaveFileName(&ofn)==TRUE)
            {
                SaveFile(ofn.lpstrFile);
            }
        }
        else
            SaveFile(filename.c_str());
    }
}
