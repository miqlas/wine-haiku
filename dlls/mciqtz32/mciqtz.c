/*
 * DirectShow MCI Driver
 *
 * Copyright 2009 Christian Costa
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "mmddk.h"
#include "wine/debug.h"
#include "mciqtz_private.h"
#include "digitalv.h"
#include "wownt32.h"

WINE_DEFAULT_DEBUG_CHANNEL(mciqtz);

static DWORD MCIQTZ_mciClose(UINT, DWORD, LPMCI_GENERIC_PARMS);
static DWORD MCIQTZ_mciStop(UINT, DWORD, LPMCI_GENERIC_PARMS);

/*======================================================================*
 *                          MCI QTZ implementation                      *
 *======================================================================*/

HINSTANCE MCIQTZ_hInstance = 0;

/***********************************************************************
 *              DllMain (MCIQTZ.0)
 */
BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID fImpLoad)
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hInstDLL);
        MCIQTZ_hInstance = hInstDLL;
        break;
    }
    return TRUE;
}

/**************************************************************************
 *                              MCIQTZ_mciGetOpenDev            [internal]
 */
static WINE_MCIQTZ* MCIQTZ_mciGetOpenDev(UINT wDevID)
{
    WINE_MCIQTZ* wma = (WINE_MCIQTZ*)mciGetDriverData(wDevID);

    if (!wma) {
        WARN("Invalid wDevID=%u\n", wDevID);
        return NULL;
    }
    return wma;
}

/**************************************************************************
 *                              MCIQTZ_drvOpen                  [internal]
 */
static DWORD MCIQTZ_drvOpen(LPCWSTR str, LPMCI_OPEN_DRIVER_PARMSW modp)
{
    WINE_MCIQTZ* wma;
    static const WCHAR mciAviWStr[] = {'M','C','I','A','V','I',0};

    TRACE("(%s, %p)\n", debugstr_w(str), modp);

    /* session instance */
    if (!modp)
        return 0xFFFFFFFF;

    wma = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WINE_MCIQTZ));
    if (!wma)
        return 0;

    modp->wType = MCI_DEVTYPE_DIGITAL_VIDEO;
    wma->wDevID = modp->wDeviceID;
    modp->wCustomCommandTable = wma->command_table = mciLoadCommandResource(MCIQTZ_hInstance, mciAviWStr, 0);
    mciSetDriverData(wma->wDevID, (DWORD_PTR)wma);

    return modp->wDeviceID;
}

/**************************************************************************
 *                              MCIQTZ_drvClose         [internal]
 */
static DWORD MCIQTZ_drvClose(DWORD dwDevID)
{
    WINE_MCIQTZ* wma;

    TRACE("(%04x)\n", dwDevID);

    wma = MCIQTZ_mciGetOpenDev(dwDevID);

    if (wma) {
        /* finish all outstanding things */
        MCIQTZ_mciClose(dwDevID, MCI_WAIT, NULL);

        mciFreeCommandResource(wma->command_table);
        mciSetDriverData(dwDevID, 0);
        HeapFree(GetProcessHeap(), 0, wma);
        return 1;
    }

    return (dwDevID == 0xFFFFFFFF) ? 1 : 0;
}

/**************************************************************************
 *                              MCIQTZ_drvConfigure             [internal]
 */
static DWORD MCIQTZ_drvConfigure(DWORD dwDevID)
{
    WINE_MCIQTZ* wma;

    TRACE("(%04x)\n", dwDevID);

    wma = MCIQTZ_mciGetOpenDev(dwDevID);
    if (!wma)
        return 0;

    MCIQTZ_mciStop(dwDevID, MCI_WAIT, NULL);

    MessageBoxA(0, "Sample QTZ Wine Driver !", "MM-Wine Driver", MB_OK);

    return 1;
}

/***************************************************************************
 *                              MCIQTZ_mciOpen                  [internal]
 */
static DWORD MCIQTZ_mciOpen(UINT wDevID, DWORD dwFlags,
                            LPMCI_DGV_OPEN_PARMSW lpOpenParms)
{
    WINE_MCIQTZ* wma;
    HRESULT hr;
    DWORD style = 0;
    RECT rc = { 0, 0, 0, 0 };

    TRACE("(%04x, %08X, %p)\n", wDevID, dwFlags, lpOpenParms);

    if (!lpOpenParms)
        return MCIERR_NULL_PARAMETER_BLOCK;

    wma = MCIQTZ_mciGetOpenDev(wDevID);
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;

    MCIQTZ_mciStop(wDevID, MCI_WAIT, NULL);

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    wma->uninit = SUCCEEDED(hr);

    hr = CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IGraphBuilder, (LPVOID*)&wma->pgraph);
    if (FAILED(hr)) {
        TRACE("Cannot create filtergraph (hr = %x)\n", hr);
        goto err;
    }

    hr = IGraphBuilder_QueryInterface(wma->pgraph, &IID_IMediaControl, (LPVOID*)&wma->pmctrl);
    if (FAILED(hr)) {
        TRACE("Cannot get IMediaControl interface (hr = %x)\n", hr);
        goto err;
    }

    hr = IGraphBuilder_QueryInterface(wma->pgraph, &IID_IMediaSeeking, (void**)&wma->seek);
    if (FAILED(hr)) {
        TRACE("Cannot get IMediaSeeking interface (hr = %x)\n", hr);
        goto err;
    }

    hr = IGraphBuilder_QueryInterface(wma->pgraph, &IID_IVideoWindow, (void**)&wma->vidwin);
    if (FAILED(hr)) {
        TRACE("Cannot get IVideoWindow interface (hr = %x)\n", hr);
        goto err;
    }

    hr = IGraphBuilder_QueryInterface(wma->pgraph, &IID_IBasicVideo, (void**)&wma->vidbasic);
    if (FAILED(hr)) {
        TRACE("Cannot get IBasicVideo interface (hr = %x)\n", hr);
        goto err;
    }

    if (!(dwFlags & MCI_OPEN_ELEMENT) || (dwFlags & MCI_OPEN_ELEMENT_ID)) {
        TRACE("Wrong dwFlags %x\n", dwFlags);
        goto err;
    }

    if (!lpOpenParms->lpstrElementName || !lpOpenParms->lpstrElementName[0]) {
        TRACE("Invalid filename specified\n");
        goto err;
    }

    TRACE("Open file %s\n", debugstr_w(lpOpenParms->lpstrElementName));

    hr = IGraphBuilder_RenderFile(wma->pgraph, lpOpenParms->lpstrElementName, NULL);
    if (FAILED(hr)) {
        TRACE("Cannot render file (hr = %x)\n", hr);
        goto err;
    }

    IVideoWindow_put_AutoShow(wma->vidwin, OAFALSE);
    IVideoWindow_put_Visible(wma->vidwin, OAFALSE);
    if (dwFlags & MCI_DGV_OPEN_WS)
        style = lpOpenParms->dwStyle;
    if (dwFlags & MCI_DGV_OPEN_PARENT) {
        IVideoWindow_put_MessageDrain(wma->vidwin, (OAHWND)lpOpenParms->hWndParent);
        IVideoWindow_put_WindowState(wma->vidwin, SW_HIDE);
        IVideoWindow_put_WindowStyle(wma->vidwin, style|WS_CHILD);
        IVideoWindow_put_Owner(wma->vidwin, (OAHWND)lpOpenParms->hWndParent);
        GetClientRect(lpOpenParms->hWndParent, &rc);
        IVideoWindow_SetWindowPosition(wma->vidwin, rc.left, rc.top, rc.right - rc.top, rc.bottom - rc.top);
        wma->parent = (HWND)lpOpenParms->hWndParent;
    }
    else if (style)
        IVideoWindow_put_WindowStyle(wma->vidwin, style);
    IBasicVideo_GetVideoSize(wma->vidbasic, &rc.right, &rc.bottom);
    wma->opened = TRUE;

    if (dwFlags & MCI_NOTIFY)
        mciDriverNotify(HWND_32(LOWORD(lpOpenParms->dwCallback)), wDevID, MCI_NOTIFY_SUCCESSFUL);

    return 0;

err:
    if (wma->vidbasic)
        IUnknown_Release(wma->vidbasic);
    wma->vidbasic = NULL;
    if (wma->seek)
        IUnknown_Release(wma->seek);
    wma->seek = NULL;
    if (wma->vidwin)
        IUnknown_Release(wma->vidwin);
    wma->vidwin = NULL;
    if (wma->pgraph)
        IGraphBuilder_Release(wma->pgraph);
    wma->pgraph = NULL;
    if (wma->pmctrl)
        IMediaControl_Release(wma->pmctrl);
    wma->pmctrl = NULL;

    if (wma->uninit)
        CoUninitialize();
    wma->uninit = 0;

    return MCIERR_INTERNAL;
}

/***************************************************************************
 *                              MCIQTZ_mciClose                 [internal]
 */
static DWORD MCIQTZ_mciClose(UINT wDevID, DWORD dwFlags, LPMCI_GENERIC_PARMS lpParms)
{
    WINE_MCIQTZ* wma;

    TRACE("(%04x, %08X, %p)\n", wDevID, dwFlags, lpParms);

    wma = MCIQTZ_mciGetOpenDev(wDevID);
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;

    MCIQTZ_mciStop(wDevID, MCI_WAIT, NULL);

    if (wma->opened) {
        IUnknown_Release(wma->vidwin);
        IUnknown_Release(wma->vidbasic);
        IUnknown_Release(wma->seek);
        IGraphBuilder_Release(wma->pgraph);
        IMediaControl_Release(wma->pmctrl);
        if (wma->uninit)
            CoUninitialize();
        wma->opened = FALSE;
    }

    return 0;
}

/***************************************************************************
 *                              MCIQTZ_mciPlay                  [internal]
 */
static DWORD MCIQTZ_mciPlay(UINT wDevID, DWORD dwFlags, LPMCI_PLAY_PARMS lpParms)
{
    WINE_MCIQTZ* wma;
    HRESULT hr;
    REFERENCE_TIME time1 = 0, time2 = 0;
    GUID format;
    DWORD pos1;

    TRACE("(%04x, %08X, %p)\n", wDevID, dwFlags, lpParms);

    if (!lpParms)
        return MCIERR_NULL_PARAMETER_BLOCK;

    wma = MCIQTZ_mciGetOpenDev(wDevID);
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;

    IMediaSeeking_GetTimeFormat(wma->seek, &format);
    if (dwFlags & MCI_FROM) {
        if (IsEqualGUID(&format, &TIME_FORMAT_MEDIA_TIME))
            time1 = lpParms->dwFrom * 10000;
        else
            time1 = lpParms->dwFrom;
        pos1 = AM_SEEKING_AbsolutePositioning;
    } else
        pos1 = AM_SEEKING_NoPositioning;
    if (dwFlags & MCI_TO) {
        if (IsEqualGUID(&format, &TIME_FORMAT_MEDIA_TIME))
            time2 = lpParms->dwTo * 10000;
        else
            time2 = lpParms->dwTo;
    } else
        IMediaSeeking_GetDuration(wma->seek, &time2);
    IMediaSeeking_SetPositions(wma->seek, &time1, pos1, &time2, AM_SEEKING_AbsolutePositioning);

    hr = IMediaControl_Run(wma->pmctrl);
    if (FAILED(hr)) {
        TRACE("Cannot run filtergraph (hr = %x)\n", hr);
        return MCIERR_INTERNAL;
    }

    IVideoWindow_put_Visible(wma->vidwin, OATRUE);

    if (dwFlags & MCI_NOTIFY)
        mciDriverNotify(HWND_32(LOWORD(lpParms->dwCallback)), wDevID, MCI_NOTIFY_SUCCESSFUL);
    return 0;
}

/***************************************************************************
 *                              MCIQTZ_mciSeek                  [internal]
 */
static DWORD MCIQTZ_mciSeek(UINT wDevID, DWORD dwFlags, LPMCI_SEEK_PARMS lpParms)
{
    WINE_MCIQTZ* wma;
    HRESULT hr;
    LONGLONG newpos;

    TRACE("(%04x, %08X, %p)\n", wDevID, dwFlags, lpParms);

    if (!lpParms)
        return MCIERR_NULL_PARAMETER_BLOCK;

    wma = MCIQTZ_mciGetOpenDev(wDevID);
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;

    MCIQTZ_mciStop(wDevID, MCI_WAIT, NULL);

    if (dwFlags & MCI_SEEK_TO_START) {
        newpos = 0;
    } else if (dwFlags & MCI_SEEK_TO_END) {
        FIXME("MCI_SEEK_TO_END not implemented yet\n");
        return MCIERR_INTERNAL;
    } else if (dwFlags & MCI_TO) {
        FIXME("MCI_TO not implemented yet\n");
        return MCIERR_INTERNAL;
    } else {
        WARN("dwFlag doesn't tell where to seek to...\n");
        return MCIERR_MISSING_PARAMETER;
    }

    hr = IMediaSeeking_SetPositions(wma->seek, &newpos, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
    if (FAILED(hr)) {
        FIXME("Cannot set position (hr = %x)\n", hr);
        return MCIERR_INTERNAL;
    }

    if (dwFlags & MCI_NOTIFY)
        mciDriverNotify(HWND_32(LOWORD(lpParms->dwCallback)), wDevID, MCI_NOTIFY_SUCCESSFUL);

    return 0;
}

/***************************************************************************
 *                              MCIQTZ_mciStop                  [internal]
 */
static DWORD MCIQTZ_mciStop(UINT wDevID, DWORD dwFlags, LPMCI_GENERIC_PARMS lpParms)
{
    WINE_MCIQTZ* wma;
    HRESULT hr;

    TRACE("(%04x, %08X, %p)\n", wDevID, dwFlags, lpParms);

    wma = MCIQTZ_mciGetOpenDev(wDevID);
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;

    if (!wma->opened)
        return 0;

    hr = IMediaControl_Stop(wma->pmctrl);
    if (FAILED(hr)) {
        TRACE("Cannot stop filtergraph (hr = %x)\n", hr);
        return MCIERR_INTERNAL;
    }

    if (!wma->parent)
        IVideoWindow_put_Visible(wma->vidwin, OAFALSE);

    return 0;
}

/***************************************************************************
 *                              MCIQTZ_mciPause                 [internal]
 */
static DWORD MCIQTZ_mciPause(UINT wDevID, DWORD dwFlags, LPMCI_GENERIC_PARMS lpParms)
{
    WINE_MCIQTZ* wma;
    HRESULT hr;

    TRACE("(%04x, %08X, %p)\n", wDevID, dwFlags, lpParms);

    wma = MCIQTZ_mciGetOpenDev(wDevID);
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;

    hr = IMediaControl_Pause(wma->pmctrl);
    if (FAILED(hr)) {
        TRACE("Cannot pause filtergraph (hr = %x)\n", hr);
        return MCIERR_INTERNAL;
    }

    return 0;
}

/***************************************************************************
 *                              MCIQTZ_mciGetDevCaps            [internal]
 */
static DWORD MCIQTZ_mciGetDevCaps(UINT wDevID, DWORD dwFlags, LPMCI_GETDEVCAPS_PARMS lpParms)
{
    WINE_MCIQTZ* wma;

    TRACE("(%04x, %08X, %p)\n", wDevID, dwFlags, lpParms);

    if (!lpParms)
        return MCIERR_NULL_PARAMETER_BLOCK;

    wma = MCIQTZ_mciGetOpenDev(wDevID);
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;

    if (!(dwFlags & MCI_GETDEVCAPS_ITEM))
        return MCIERR_MISSING_PARAMETER;

    switch (lpParms->dwItem) {
        case MCI_GETDEVCAPS_CAN_RECORD:
            lpParms->dwReturn = MAKEMCIRESOURCE(FALSE, MCI_FALSE);
            TRACE("MCI_GETDEVCAPS_CAN_RECORD = %08x\n", lpParms->dwReturn);
            break;
        case MCI_GETDEVCAPS_HAS_AUDIO:
            lpParms->dwReturn = MAKEMCIRESOURCE(TRUE, MCI_TRUE);
            TRACE("MCI_GETDEVCAPS_HAS_AUDIO = %08x\n", lpParms->dwReturn);
            break;
        case MCI_GETDEVCAPS_HAS_VIDEO:
            lpParms->dwReturn = MAKEMCIRESOURCE(TRUE, MCI_TRUE);
            TRACE("MCI_GETDEVCAPS_HAS_VIDEO = %08x\n", lpParms->dwReturn);
            break;
        case MCI_GETDEVCAPS_DEVICE_TYPE:
            lpParms->dwReturn = MAKEMCIRESOURCE(MCI_DEVTYPE_DIGITAL_VIDEO, MCI_DEVTYPE_DIGITAL_VIDEO);
            TRACE("MCI_GETDEVCAPS_DEVICE_TYPE = %08x\n", lpParms->dwReturn);
            break;
        case MCI_GETDEVCAPS_USES_FILES:
            lpParms->dwReturn = MAKEMCIRESOURCE(TRUE, MCI_TRUE);
            TRACE("MCI_GETDEVCAPS_USES_FILES = %08x\n", lpParms->dwReturn);
            break;
        case MCI_GETDEVCAPS_COMPOUND_DEVICE:
            lpParms->dwReturn = MAKEMCIRESOURCE(TRUE, MCI_TRUE);
            TRACE("MCI_GETDEVCAPS_COMPOUND_DEVICE = %08x\n", lpParms->dwReturn);
            break;
        case MCI_GETDEVCAPS_CAN_EJECT:
            lpParms->dwReturn = MAKEMCIRESOURCE(FALSE, MCI_FALSE);
            TRACE("MCI_GETDEVCAPS_EJECT = %08x\n", lpParms->dwReturn);
            break;
        case MCI_GETDEVCAPS_CAN_PLAY:
            lpParms->dwReturn = MAKEMCIRESOURCE(TRUE, MCI_TRUE);
            TRACE("MCI_GETDEVCAPS_CAN_PLAY = %08x\n", lpParms->dwReturn);
            break;
        case MCI_GETDEVCAPS_CAN_SAVE:
            lpParms->dwReturn = MAKEMCIRESOURCE(FALSE, MCI_FALSE);
            TRACE("MCI_GETDEVCAPS_CAN_SAVE = %08x\n", lpParms->dwReturn);
            break;
        case MCI_DGV_GETDEVCAPS_CAN_REVERSE:
            lpParms->dwReturn = MAKEMCIRESOURCE(FALSE, MCI_FALSE);
            TRACE("MCI_DGV_GETDEVCAPS_CAN_REVERSE = %08x\n", lpParms->dwReturn);
            break;
        case MCI_DGV_GETDEVCAPS_CAN_STRETCH:
            lpParms->dwReturn = MAKEMCIRESOURCE(FALSE, MCI_FALSE); /* FIXME */
            TRACE("MCI_DGV_GETDEVCAPS_CAN_STRETCH = %08x\n", lpParms->dwReturn);
            break;
        case MCI_DGV_GETDEVCAPS_CAN_LOCK:
            lpParms->dwReturn = MAKEMCIRESOURCE(FALSE, MCI_FALSE);
            TRACE("MCI_DGV_GETDEVCAPS_CAN_LOCK = %08x\n", lpParms->dwReturn);
            break;
        case MCI_DGV_GETDEVCAPS_CAN_FREEZE:
            lpParms->dwReturn = MAKEMCIRESOURCE(FALSE, MCI_FALSE);
            TRACE("MCI_DGV_GETDEVCAPS_CAN_FREEZE = %08x\n", lpParms->dwReturn);
            break;
        case MCI_DGV_GETDEVCAPS_CAN_STR_IN:
            lpParms->dwReturn = MAKEMCIRESOURCE(FALSE, MCI_FALSE);
            TRACE("MCI_DGV_GETDEVCAPS_CAN_STRETCH_INPUT = %08x\n", lpParms->dwReturn);
            break;
        case MCI_DGV_GETDEVCAPS_HAS_STILL:
            lpParms->dwReturn = MAKEMCIRESOURCE(FALSE, MCI_FALSE);
            TRACE("MCI_DGV_GETDEVCAPS_HAS_STILL = %08x\n", lpParms->dwReturn);
            break;
        case MCI_DGV_GETDEVCAPS_CAN_TEST:
            lpParms->dwReturn = MAKEMCIRESOURCE(FALSE, MCI_FALSE); /* FIXME */
            TRACE("MCI_DGV_GETDEVCAPS_CAN_TEST = %08x\n", lpParms->dwReturn);
            break;
        case MCI_DGV_GETDEVCAPS_MAX_WINDOWS:
            lpParms->dwReturn = 1;
            TRACE("MCI_DGV_GETDEVCAPS_MAX_WINDOWS = %u\n", lpParms->dwReturn);
            return 0;
        default:
            WARN("Unknown capability %08x\n", lpParms->dwItem);
            /* Fall through */
        case MCI_DGV_GETDEVCAPS_MAXIMUM_RATE: /* unknown to w2k */
        case MCI_DGV_GETDEVCAPS_MINIMUM_RATE: /* unknown to w2k */
            return MCIERR_UNSUPPORTED_FUNCTION;
    }

    return MCI_RESOURCE_RETURNED;
}

/***************************************************************************
 *                              MCIQTZ_mciSet                   [internal]
 */
static DWORD MCIQTZ_mciSet(UINT wDevID, DWORD dwFlags, LPMCI_DGV_SET_PARMS lpParms)
{
    WINE_MCIQTZ* wma;

    TRACE("(%04x, %08X, %p)\n", wDevID, dwFlags, lpParms);

    if (!lpParms)
        return MCIERR_NULL_PARAMETER_BLOCK;

    wma = MCIQTZ_mciGetOpenDev(wDevID);
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;

    if (dwFlags & MCI_SET_TIME_FORMAT) {
        switch (lpParms->dwTimeFormat) {
            case MCI_FORMAT_MILLISECONDS:
                TRACE("MCI_SET_TIME_FORMAT = MCI_FORMAT_MILLISECONDS\n");
                wma->time_format = MCI_FORMAT_MILLISECONDS;
                break;
            case MCI_FORMAT_FRAMES:
                TRACE("MCI_SET_TIME_FORMAT = MCI_FORMAT_FRAMES\n");
                wma->time_format = MCI_FORMAT_FRAMES;
                break;
            default:
                WARN("Bad time format %u\n", lpParms->dwTimeFormat);
                return MCIERR_BAD_TIME_FORMAT;
        }
    }

    if (dwFlags & MCI_SET_DOOR_OPEN)
        FIXME("MCI_SET_DOOR_OPEN not implemented yet\n");
    if (dwFlags & MCI_SET_DOOR_CLOSED)
        FIXME("MCI_SET_DOOR_CLOSED not implemented yet\n");
    if (dwFlags & MCI_SET_AUDIO)
        FIXME("MCI_SET_AUDIO not implemented yet\n");
    if (dwFlags & MCI_SET_VIDEO)
        FIXME("MCI_SET_VIDEO not implemented yet\n");
    if (dwFlags & MCI_SET_ON)
        FIXME("MCI_SET_ON not implemented yet\n");
    if (dwFlags & MCI_SET_OFF)
        FIXME("MCI_SET_OFF not implemented yet\n");
    if (dwFlags & MCI_SET_AUDIO_LEFT)
        FIXME("MCI_SET_AUDIO_LEFT not implemented yet\n");
    if (dwFlags & MCI_SET_AUDIO_RIGHT)
        FIXME("MCI_SET_AUDIO_RIGHT not implemented yet\n");

    if (dwFlags & ~0x7f03 /* All MCI_SET flags mask */)
        ERR("Unknown flags %08x\n", dwFlags & ~0x7f03);

    return 0;
}

/***************************************************************************
 *                              MCIQTZ_mciStatus                [internal]
 */
static DWORD MCIQTZ_mciStatus(UINT wDevID, DWORD dwFlags, LPMCI_DGV_STATUS_PARMSW lpParms)
{
    WINE_MCIQTZ* wma;
    HRESULT hr;

    TRACE("(%04x, %08X, %p)\n", wDevID, dwFlags, lpParms);

    if (!lpParms)
        return MCIERR_NULL_PARAMETER_BLOCK;

    wma = MCIQTZ_mciGetOpenDev(wDevID);
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;

    if (!(dwFlags & MCI_STATUS_ITEM)) {
        WARN("No status item specified\n");
        return MCIERR_UNRECOGNIZED_COMMAND;
    }

    switch (lpParms->dwItem) {
        case MCI_STATUS_LENGTH: {
            LONGLONG duration = -1;
            GUID format;
            switch (wma->time_format) {
                case MCI_FORMAT_MILLISECONDS: format = TIME_FORMAT_MEDIA_TIME; break;
                case MCI_FORMAT_FRAMES: format = TIME_FORMAT_FRAME; break;
                default: ERR("Unhandled format %x\n", wma->time_format); break;
            }
            hr = IMediaSeeking_SetTimeFormat(wma->seek, &format);
            if (FAILED(hr)) {
                FIXME("Cannot set time format (hr = %x)\n", hr);
                lpParms->dwReturn = 0;
                break;
            }
            hr = IMediaSeeking_GetDuration(wma->seek, &duration);
            if (FAILED(hr) || duration < 0) {
                FIXME("Cannot read duration (hr = %x)\n", hr);
                lpParms->dwReturn = 0;
            } else if (wma->time_format != MCI_FORMAT_MILLISECONDS)
                lpParms->dwReturn = duration;
            else
                lpParms->dwReturn = duration / 10000;
            break;
        }
        case MCI_STATUS_POSITION: {
            REFERENCE_TIME curpos;

            hr = IMediaSeeking_GetCurrentPosition(wma->seek, &curpos);
            if (FAILED(hr)) {
                FIXME("Cannot get position (hr = %x)\n", hr);
                return MCIERR_INTERNAL;
            }
            lpParms->dwReturn = curpos / 10000;
            break;
        }
        case MCI_STATUS_NUMBER_OF_TRACKS:
            FIXME("MCI_STATUS_NUMBER_OF_TRACKS not implemented yet\n");
            return MCIERR_UNRECOGNIZED_COMMAND;
        case MCI_STATUS_MODE: {
            LONG state = State_Stopped;
            IMediaControl_GetState(wma->pmctrl, -1, &state);
            if (state == State_Stopped)
                state = MCI_MODE_STOP;
            else if (state == State_Running)
                state = MCI_MODE_PLAY;
            else if (state == State_Paused)
                state = MCI_MODE_PAUSE;
            break;
        }
        case MCI_STATUS_MEDIA_PRESENT:
            FIXME("MCI_STATUS_MEDIA_PRESENT not implemented yet\n");
            return MCIERR_UNRECOGNIZED_COMMAND;
        case MCI_STATUS_TIME_FORMAT:
            lpParms->dwReturn = wma->time_format;
            break;
        case MCI_STATUS_READY:
            FIXME("MCI_STATUS_READY not implemented yet\n");
            return MCIERR_UNRECOGNIZED_COMMAND;
        case MCI_STATUS_CURRENT_TRACK:
            FIXME("MCI_STATUS_CURRENT_TRACK not implemented yet\n");
            return MCIERR_UNRECOGNIZED_COMMAND;
        default:
            FIXME("Unknown command %08X\n", lpParms->dwItem);
            return MCIERR_UNRECOGNIZED_COMMAND;
    }

    if (dwFlags & MCI_NOTIFY)
        mciDriverNotify(HWND_32(LOWORD(lpParms->dwCallback)), wDevID, MCI_NOTIFY_SUCCESSFUL);

    return 0;
}

/***************************************************************************
 *                              MCIQTZ_mciWhere                 [internal]
 */
static DWORD MCIQTZ_mciWhere(UINT wDevID, DWORD dwFlags, LPMCI_DGV_RECT_PARMS lpParms)
{
    WINE_MCIQTZ* wma;
    HRESULT hr;
    HWND hWnd;
    RECT rc;
    DWORD ret = MCIERR_UNRECOGNIZED_COMMAND;

    TRACE("(%04x, %08X, %p)\n", wDevID, dwFlags, lpParms);

    if (!lpParms)
        return MCIERR_NULL_PARAMETER_BLOCK;

    wma = MCIQTZ_mciGetOpenDev(wDevID);
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;

    hr = IVideoWindow_get_Owner(wma->vidwin, (OAHWND*)&hWnd);
    if (FAILED(hr)) {
        TRACE("No video stream, returning no window error\n");
        return MCIERR_NO_WINDOW;
    }

    if (dwFlags & MCI_DGV_WHERE_SOURCE) {
        if (dwFlags & MCI_DGV_WHERE_MAX)
            FIXME("MCI_DGV_WHERE_SOURCE_MAX stub %s\n", wine_dbgstr_rect(&rc));
        IBasicVideo_GetSourcePosition(wma->vidbasic, &rc.left, &rc.top, &rc.right, &rc.bottom);
        TRACE("MCI_DGV_WHERE_SOURCE %s\n", wine_dbgstr_rect(&rc));
    }
    if (dwFlags & MCI_DGV_WHERE_DESTINATION) {
        if (dwFlags & MCI_DGV_WHERE_MAX)
            FIXME("MCI_DGV_WHERE_DESTINATION_MAX stub %s\n", wine_dbgstr_rect(&rc));
        IBasicVideo_GetDestinationPosition(wma->vidbasic, &rc.left, &rc.top, &rc.right, &rc.bottom);
        TRACE("MCI_DGV_WHERE_DESTINATION %s\n", wine_dbgstr_rect(&rc));
    }
    if (dwFlags & MCI_DGV_WHERE_FRAME) {
        if (dwFlags & MCI_DGV_WHERE_MAX)
            FIXME("MCI_DGV_WHERE_FRAME_MAX not supported yet\n");
        else
            FIXME("MCI_DGV_WHERE_FRAME not supported yet\n");
        goto out;
    }
    if (dwFlags & MCI_DGV_WHERE_VIDEO) {
        if (dwFlags & MCI_DGV_WHERE_MAX)
            FIXME("MCI_DGV_WHERE_VIDEO_MAX not supported yet\n");
        else
            FIXME("MCI_DGV_WHERE_VIDEO not supported yet\n");
        goto out;
    }
    if (dwFlags & MCI_DGV_WHERE_WINDOW) {
        if (dwFlags & MCI_DGV_WHERE_MAX) {
            GetWindowRect(GetDesktopWindow(), &rc);
            rc.right -= rc.left;
            rc.bottom -= rc.top;
            TRACE("MCI_DGV_WHERE_WINDOW_MAX %s\n", wine_dbgstr_rect(&rc));
        } else {
            IVideoWindow_GetWindowPosition(wma->vidwin, &rc.left, &rc.top, &rc.right, &rc.bottom);
            TRACE("MCI_DGV_WHERE_WINDOW %s\n", wine_dbgstr_rect(&rc));
        }
    }
    ret = 0;
out:
    lpParms->rc = rc;
    return ret;
}

/***************************************************************************
 *                              MCIQTZ_mciWindow                [internal]
 */
static DWORD MCIQTZ_mciWindow(UINT wDevID, DWORD dwFlags, LPMCI_DGV_WINDOW_PARMSW lpParms)
{
    WINE_MCIQTZ *wma = MCIQTZ_mciGetOpenDev(wDevID);

    TRACE("(%04x, %08X, %p)\n", wDevID, dwFlags, lpParms);

    if (!lpParms)
        return MCIERR_NULL_PARAMETER_BLOCK;
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;
    if (dwFlags & MCI_TEST)
        return 0;

    if (dwFlags & MCI_DGV_WINDOW_HWND && (IsWindow(lpParms->hWnd) || !lpParms->hWnd)) {
        LONG visible = OATRUE;
        LONG style = 0;
        TRACE("Setting hWnd to %p\n", lpParms->hWnd);
        IVideoWindow_get_Visible(wma->vidwin, &visible);
        IVideoWindow_put_Visible(wma->vidwin, OAFALSE);
        IVideoWindow_get_WindowStyle(wma->vidwin, &style);
        style &= ~WS_CHILD;
        if (lpParms->hWnd)
            IVideoWindow_put_WindowStyle(wma->vidwin, style|WS_CHILD);
        else
            IVideoWindow_put_WindowStyle(wma->vidwin, style);
        IVideoWindow_put_Owner(wma->vidwin, (OAHWND)lpParms->hWnd);
        IVideoWindow_put_MessageDrain(wma->vidwin, (OAHWND)lpParms->hWnd);
        IVideoWindow_put_Visible(wma->vidwin, visible);
        wma->parent = lpParms->hWnd;
    }
    if (dwFlags & MCI_DGV_WINDOW_STATE) {
        TRACE("Setting nCmdShow to %d\n", lpParms->nCmdShow);
        IVideoWindow_put_WindowState(wma->vidwin, lpParms->nCmdShow);
    }
    if (dwFlags & MCI_DGV_WINDOW_TEXT) {
        TRACE("Setting caption to %s\n", debugstr_w(lpParms->lpstrText));
        IVideoWindow_put_Caption(wma->vidwin, lpParms->lpstrText);
    }
    return 0;
}

/******************************************************************************
 *              MCIAVI_mciUpdate            [internal]
 */
static DWORD MCIQTZ_mciUpdate(UINT wDevID, DWORD dwFlags, LPMCI_DGV_UPDATE_PARMS lpParms)
{
    WINE_MCIQTZ *wma;
    DWORD res = 0;

    TRACE("%04x, %08x, %p\n", wDevID, dwFlags, lpParms);

    if (!lpParms)
        return MCIERR_NULL_PARAMETER_BLOCK;

    wma = MCIQTZ_mciGetOpenDev(wDevID);
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;

    if (dwFlags & MCI_DGV_UPDATE_HDC) {
        LONG state, size;
        BYTE *data;
        BITMAPINFO *info;
        HRESULT hr;
        RECT src, dest;
        LONG visible = OATRUE;

        res = MCIERR_INTERNAL;
        IMediaControl_GetState(wma->pmctrl, -1, &state);
        if (state == State_Running)
            return MCIERR_UNSUPPORTED_FUNCTION;
        /* If in stopped state, nothing has been drawn to screen
         * moving to pause, which is needed for the old dib renderer, will result
         * in a single frame drawn, so hide the window here */
        IVideoWindow_get_Visible(wma->vidwin, &visible);
        if (wma->parent)
            IVideoWindow_put_Visible(wma->vidwin, OAFALSE);
        /* FIXME: Should we check the original state and restore it? */
        IMediaControl_Pause(wma->pmctrl);
        IMediaControl_GetState(wma->pmctrl, -1, &state);
        if (FAILED(hr = IBasicVideo_GetCurrentImage(wma->vidbasic, &size, NULL))) {
            WARN("Could not get image size (hr = %x)\n", hr);
            goto out;
        }
        data = HeapAlloc(GetProcessHeap(), 0, size);
        info = (BITMAPINFO*)data;
        IBasicVideo_GetCurrentImage(wma->vidbasic, &size, (LONG*)data);
        data += info->bmiHeader.biSize;

        IBasicVideo_GetSourcePosition(wma->vidbasic, &src.left, &src.top, &src.right, &src.bottom);
        IBasicVideo_GetDestinationPosition(wma->vidbasic, &dest.left, &dest.top, &dest.right, &dest.bottom);
        StretchDIBits(lpParms->hDC,
              dest.left, dest.top, dest.right + dest.left, dest.bottom + dest.top,
              src.left, src.top, src.right + src.left, src.bottom + src.top,
              data, info, DIB_RGB_COLORS, SRCCOPY);
        HeapFree(GetProcessHeap(), 0, data);
        res = 0;
out:
        if (wma->parent)
            IVideoWindow_put_Visible(wma->vidwin, visible);
    }
    else if (dwFlags)
        FIXME("Unhandled flags %x\n", dwFlags);
    return res;
}

/***************************************************************************
 *                              MCIQTZ_mciSetAudio              [internal]
 */
static DWORD MCIQTZ_mciSetAudio(UINT wDevID, DWORD dwFlags, LPMCI_DGV_SETAUDIO_PARMSW lpParms)
{
    WINE_MCIQTZ *wma;

    FIXME("(%04x, %08x, %p) : stub\n", wDevID, dwFlags, lpParms);

    if (!lpParms)
        return MCIERR_NULL_PARAMETER_BLOCK;

    wma = MCIQTZ_mciGetOpenDev(wDevID);
    if (!wma)
        return MCIERR_INVALID_DEVICE_ID;

    MCIQTZ_mciStop(wDevID, MCI_WAIT, NULL);

    return 0;
}

/*======================================================================*
 *                          MCI QTZ entry points                        *
 *======================================================================*/

/**************************************************************************
 *                              DriverProc (MCIQTZ.@)
 */
LRESULT CALLBACK MCIQTZ_DriverProc(DWORD_PTR dwDevID, HDRVR hDriv, UINT wMsg,
                                   LPARAM dwParam1, LPARAM dwParam2)
{
    TRACE("(%08lX, %p, %08X, %08lX, %08lX)\n",
          dwDevID, hDriv, wMsg, dwParam1, dwParam2);

    switch (wMsg) {
        case DRV_LOAD:                  return 1;
        case DRV_FREE:                  return 1;
        case DRV_OPEN:                  return MCIQTZ_drvOpen((LPCWSTR)dwParam1, (LPMCI_OPEN_DRIVER_PARMSW)dwParam2);
        case DRV_CLOSE:                 return MCIQTZ_drvClose(dwDevID);
        case DRV_ENABLE:                return 1;
        case DRV_DISABLE:               return 1;
        case DRV_QUERYCONFIGURE:        return 1;
        case DRV_CONFIGURE:             return MCIQTZ_drvConfigure(dwDevID);
        case DRV_INSTALL:               return DRVCNF_RESTART;
        case DRV_REMOVE:                return DRVCNF_RESTART;
    }

    /* session instance */
    if (dwDevID == 0xFFFFFFFF)
        return 1;

    switch (wMsg) {
        case MCI_OPEN_DRIVER:   return MCIQTZ_mciOpen      (dwDevID, dwParam1, (LPMCI_DGV_OPEN_PARMSW)     dwParam2);
        case MCI_CLOSE_DRIVER:  return MCIQTZ_mciClose     (dwDevID, dwParam1, (LPMCI_GENERIC_PARMS)       dwParam2);
        case MCI_PLAY:          return MCIQTZ_mciPlay      (dwDevID, dwParam1, (LPMCI_PLAY_PARMS)          dwParam2);
        case MCI_SEEK:          return MCIQTZ_mciSeek      (dwDevID, dwParam1, (LPMCI_SEEK_PARMS)          dwParam2);
        case MCI_STOP:          return MCIQTZ_mciStop      (dwDevID, dwParam1, (LPMCI_GENERIC_PARMS)       dwParam2);
        case MCI_PAUSE:         return MCIQTZ_mciPause     (dwDevID, dwParam1, (LPMCI_GENERIC_PARMS)       dwParam2);
        case MCI_GETDEVCAPS:    return MCIQTZ_mciGetDevCaps(dwDevID, dwParam1, (LPMCI_GETDEVCAPS_PARMS)    dwParam2);
        case MCI_SET:           return MCIQTZ_mciSet       (dwDevID, dwParam1, (LPMCI_DGV_SET_PARMS)       dwParam2);
        case MCI_STATUS:        return MCIQTZ_mciStatus    (dwDevID, dwParam1, (LPMCI_DGV_STATUS_PARMSW)   dwParam2);
        case MCI_WHERE:         return MCIQTZ_mciWhere     (dwDevID, dwParam1, (LPMCI_DGV_RECT_PARMS)      dwParam2);
        /* Digital Video specific */
        case MCI_SETAUDIO:      return MCIQTZ_mciSetAudio  (dwDevID, dwParam1, (LPMCI_DGV_SETAUDIO_PARMSW) dwParam2);
        case MCI_UPDATE:
            return MCIQTZ_mciUpdate(dwDevID, dwParam1, (LPMCI_DGV_UPDATE_PARMS)dwParam2);
        case MCI_WINDOW:
            return MCIQTZ_mciWindow(dwDevID, dwParam1, (LPMCI_DGV_WINDOW_PARMSW)dwParam2);
        case MCI_PUT:
        case MCI_RECORD:
        case MCI_RESUME:
        case MCI_INFO:
        case MCI_LOAD:
        case MCI_SAVE:
        case MCI_FREEZE:
        case MCI_REALIZE:
        case MCI_UNFREEZE:
        case MCI_STEP:
        case MCI_COPY:
        case MCI_CUT:
        case MCI_DELETE:
        case MCI_PASTE:
        case MCI_CUE:
        /* Digital Video specific */
        case MCI_CAPTURE:
        case MCI_MONITOR:
        case MCI_RESERVE:
        case MCI_SIGNAL:
        case MCI_SETVIDEO:
        case MCI_QUALITY:
        case MCI_LIST:
        case MCI_UNDO:
        case MCI_CONFIGURE:
        case MCI_RESTORE:
            FIXME("Unimplemented command [%08X]\n", wMsg);
            break;
        case MCI_SPIN:
        case MCI_ESCAPE:
            WARN("Unsupported command [%08X]\n", wMsg);
            break;
        case MCI_OPEN:
        case MCI_CLOSE:
            FIXME("Shouldn't receive a MCI_OPEN or CLOSE message\n");
            break;
        default:
            TRACE("Sending msg [%08X] to default driver proc\n", wMsg);
            return DefDriverProc(dwDevID, hDriv, wMsg, dwParam1, dwParam2);
    }

    return MCIERR_UNRECOGNIZED_COMMAND;
}
