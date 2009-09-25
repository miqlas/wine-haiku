/* OLE DB Conversion library tests
 *
 * Copyright 2009 Huw Davies
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

#define COBJMACROS
#define NONAMELESSUNION
#define NONAMELESSSTRUCT

#include "windef.h"
#include "winbase.h"
#include "ole2.h"
#include "msdadc.h"

#include "oledberr.h"

#include "initguid.h"
#include "msdaguid.h"

#include "wine/test.h"

static void test_dcinfo(void)
{
    IDCInfo *info;
    HRESULT hr;
    DCINFOTYPE types[2];
    DCINFO *inf;

    hr = CoCreateInstance(&CLSID_OLEDB_CONVERSIONLIBRARY, NULL, CLSCTX_INPROC_SERVER, &IID_IDCInfo, (void**)&info);
    if(FAILED(hr))
    {
        win_skip("Unable to load oledb conversion library\n");
        return;
    }

    types[0] = DCINFOTYPE_VERSION;
    hr = IDCInfo_GetInfo(info, 1, types, &inf);
    ok(hr == S_OK, "got %08x\n", hr);

    ok(inf->eInfoType == DCINFOTYPE_VERSION, "got %08x\n", inf->eInfoType);
    ok(V_VT(&inf->vData) == VT_UI4, "got %08x\n", V_VT(&inf->vData));
    ok(V_UI4(&inf->vData) == 0x110, "got %08x\n", V_UI4(&inf->vData));

    V_UI4(&inf->vData) = 0x200;
    hr = IDCInfo_SetInfo(info, 1, inf);
    ok(hr == S_OK, "got %08x\n", hr);
    CoTaskMemFree(inf);

    hr = IDCInfo_GetInfo(info, 1, types, &inf);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(inf->eInfoType == DCINFOTYPE_VERSION, "got %08x\n", inf->eInfoType);
    ok(V_VT(&inf->vData) == VT_UI4, "got %08x\n", V_VT(&inf->vData));
    ok(V_UI4(&inf->vData) == 0x200, "got %08x\n", V_UI4(&inf->vData));

    V_UI4(&inf->vData) = 0x100;
    hr = IDCInfo_SetInfo(info, 1, inf);
    ok(hr == S_OK, "got %08x\n", hr);
    CoTaskMemFree(inf);

    hr = IDCInfo_GetInfo(info, 1, types, &inf);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(inf->eInfoType == DCINFOTYPE_VERSION, "got %08x\n", inf->eInfoType);
    ok(V_VT(&inf->vData) == VT_UI4, "got %08x\n", V_VT(&inf->vData));
    ok(V_UI4(&inf->vData) == 0x100, "got %08x\n", V_UI4(&inf->vData));

    V_UI4(&inf->vData) = 0x500;
    hr = IDCInfo_SetInfo(info, 1, inf);
    ok(hr == S_OK, "got %08x\n", hr);
    CoTaskMemFree(inf);

    hr = IDCInfo_GetInfo(info, 1, types, &inf);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(inf->eInfoType == DCINFOTYPE_VERSION, "got %08x\n", inf->eInfoType);
    ok(V_VT(&inf->vData) == VT_UI4, "got %08x\n", V_VT(&inf->vData));
    ok(V_UI4(&inf->vData) == 0x500, "got %08x\n", V_UI4(&inf->vData));

    V_UI4(&inf->vData) = 0xffff;
    hr = IDCInfo_SetInfo(info, 1, inf);
    ok(hr == S_OK, "got %08x\n", hr);
    CoTaskMemFree(inf);

    hr = IDCInfo_GetInfo(info, 1, types, &inf);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(inf->eInfoType == DCINFOTYPE_VERSION, "got %08x\n", inf->eInfoType);
    ok(V_VT(&inf->vData) == VT_UI4, "got %08x\n", V_VT(&inf->vData));
    ok(V_UI4(&inf->vData) == 0xffff, "got %08x\n", V_UI4(&inf->vData));

    V_UI4(&inf->vData) = 0x12345678;
    hr = IDCInfo_SetInfo(info, 1, inf);
    ok(hr == S_OK, "got %08x\n", hr);
    CoTaskMemFree(inf);

    hr = IDCInfo_GetInfo(info, 1, types, &inf);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(inf->eInfoType == DCINFOTYPE_VERSION, "got %08x\n", inf->eInfoType);
    ok(V_VT(&inf->vData) == VT_UI4, "got %08x\n", V_VT(&inf->vData));
    ok(V_UI4(&inf->vData) == 0x12345678, "got %08x\n", V_UI4(&inf->vData));

    /* Try setting a version variant of something other than VT_UI4 */
    V_VT(&inf->vData) = VT_I4;
    V_I4(&inf->vData) = 0x200;
    hr = IDCInfo_SetInfo(info, 1, inf);
    ok(hr == DB_S_ERRORSOCCURRED, "got %08x\n", hr);
    CoTaskMemFree(inf);

    hr = IDCInfo_GetInfo(info, 1, types, &inf);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(inf->eInfoType == DCINFOTYPE_VERSION, "got %08x\n", inf->eInfoType);
    ok(V_VT(&inf->vData) == VT_UI4, "got %08x\n", V_VT(&inf->vData));
    ok(V_UI4(&inf->vData) == 0x12345678, "got %08x\n", V_UI4(&inf->vData));
    CoTaskMemFree(inf);

    /* More than one type */
    types[1] = 2;
    hr = IDCInfo_GetInfo(info, 2, types, &inf);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(inf[0].eInfoType == DCINFOTYPE_VERSION, "got %08x\n", inf[0].eInfoType);
    ok(V_VT(&inf[0].vData) == VT_UI4, "got %08x\n", V_VT(&inf[0].vData));
    ok(V_UI4(&inf[0].vData) == 0x12345678, "got %08x\n", V_UI4(&inf[0].vData));
    ok(inf[1].eInfoType == 2, "got %08x\n", inf[1].eInfoType);
    ok(V_VT(&inf[1].vData) == VT_EMPTY, "got %08x\n", V_VT(&inf[1].vData));

    hr = IDCInfo_SetInfo(info, 2, inf);
    ok(hr == S_OK, "got %08x\n", hr);
    CoTaskMemFree(inf);


    IDCInfo_Release(info);
}

struct can_convert
{
    DBTYPE type;
    DWORD can_convert_to;
} simple_convert[] =
{
    {DBTYPE_EMPTY,       0x23bfd9ff},
    {DBTYPE_NULL,        0x00001002},
    {DBTYPE_I2,          0x3b9fd9ff},
    {DBTYPE_I4,          0x3bdfd9ff},

    {DBTYPE_R4,          0x3b9fd9ff},
    {DBTYPE_R8,          0x3b9fd9ff},
    {DBTYPE_CY,          0x039fd97f},
    {DBTYPE_DATE,        0x399f99bf},

    {DBTYPE_BSTR,        0x3bffd9ff},
    {DBTYPE_IDISPATCH,   0x3bffffff},
    {DBTYPE_ERROR,       0x01001500},
    {DBTYPE_BOOL,        0x039fd9ff},

    {DBTYPE_VARIANT,     0x3bffffff},
    {DBTYPE_IUNKNOWN,    0x00003203},
    {DBTYPE_DECIMAL,     0x3b9fd97f},
    {DBTYPE_I1,          0x3b9fd9ff},

    {DBTYPE_UI1,         0x3b9fd9ff},
    {DBTYPE_UI2,         0x3b9fd9ff},
    {DBTYPE_UI4,         0x3bdfd9ff},
    {DBTYPE_I8,          0x03dfd97f},

    {DBTYPE_UI8,         0x03dfd97f},
    {DBTYPE_GUID,        0x01e01103},
    {DBTYPE_BYTES,       0x01fc110b},
    {DBTYPE_STR,         0x3bffd9ff},

    {DBTYPE_WSTR,        0x3bffd9ff},
    {DBTYPE_NUMERIC,     0x039fd97f},
    {DBTYPE_UDT,         0x00000000},
    {DBTYPE_DBDATE,      0x39801183},

    {DBTYPE_DBTIME,      0x39801183},
    {DBTYPE_DBTIMESTAMP, 0x39801183}
};


static inline BOOL array_type(DBTYPE type)
{
    return (type >= DBTYPE_I2 && type <= DBTYPE_UI4);
}

static void test_canconvert(void)
{
    IDataConvert *convert;
    HRESULT hr;
    int src_idx, dst_idx;

    hr = CoCreateInstance(&CLSID_OLEDB_CONVERSIONLIBRARY, NULL, CLSCTX_INPROC_SERVER, &IID_IDataConvert, (void**)&convert);
    if(FAILED(hr))
    {
        win_skip("Unable to load oledb conversion library\n");
        return;
    }

    /* Some older versions of the library don't support several conversions, we'll skip
       if we have such a library */
    hr = IDataConvert_CanConvert(convert, DBTYPE_EMPTY, DBTYPE_DBTIMESTAMP);
    if(hr == S_FALSE)
    {
        win_skip("Doesn't handle DBTYPE_EMPTY -> DBTYPE_DBTIMESTAMP conversion so skipping\n");
        IDataConvert_Release(convert);
        return;
    }

    /* Some older versions of the library don't support several conversions, we'll skip
       if we have such a library */
    hr = IDataConvert_CanConvert(convert, DBTYPE_EMPTY, DBTYPE_DBTIMESTAMP);
    if(hr == S_FALSE)
    {
        win_skip("Doesn't handle DBTYPE_EMPTY -> DBTYPE_DBTIMESTAMP conversion so skipping\n");
        IDataConvert_Release(convert);
        return;
    }

    for(src_idx = 0; src_idx < sizeof(simple_convert) / sizeof(simple_convert[0]); src_idx++)
        for(dst_idx = 0; dst_idx < sizeof(simple_convert) / sizeof(simple_convert[0]); dst_idx++)
        {
            BOOL expect, simple_expect;
            simple_expect = (simple_convert[src_idx].can_convert_to >> dst_idx) & 1;

            hr = IDataConvert_CanConvert(convert, simple_convert[src_idx].type, simple_convert[dst_idx].type);
            expect = simple_expect;
            ok((hr == S_OK && expect == TRUE) ||
               (hr == S_FALSE && expect == FALSE),
               "%04x -> %04x: got %08x expect conversion to be %spossible\n", simple_convert[src_idx].type,
               simple_convert[dst_idx].type, hr, expect ? "" : "not ");

            /* src DBTYPE_BYREF */
            hr = IDataConvert_CanConvert(convert, simple_convert[src_idx].type | DBTYPE_BYREF, simple_convert[dst_idx].type);
            expect = simple_expect;
            ok((hr == S_OK && expect == TRUE) ||
               (hr == S_FALSE && expect == FALSE),
               "%04x -> %04x: got %08x expect conversion to be %spossible\n", simple_convert[src_idx].type | DBTYPE_BYREF,
               simple_convert[dst_idx].type, hr, expect ? "" : "not ");

            /* dst DBTYPE_BYREF */
            hr = IDataConvert_CanConvert(convert, simple_convert[src_idx].type, simple_convert[dst_idx].type | DBTYPE_BYREF);
            expect = FALSE;
            if(simple_expect &&
               (simple_convert[dst_idx].type == DBTYPE_BYTES ||
                simple_convert[dst_idx].type == DBTYPE_STR ||
                simple_convert[dst_idx].type == DBTYPE_WSTR))
                expect = TRUE;
            ok((hr == S_OK && expect == TRUE) ||
               (hr == S_FALSE && expect == FALSE),
               "%04x -> %04x: got %08x expect conversion to be %spossible\n", simple_convert[src_idx].type,
               simple_convert[dst_idx].type | DBTYPE_BYREF, hr, expect ? "" : "not ");

            /* src & dst DBTYPE_BYREF */
            hr = IDataConvert_CanConvert(convert, simple_convert[src_idx].type | DBTYPE_BYREF, simple_convert[dst_idx].type | DBTYPE_BYREF);
            expect = FALSE;
            if(simple_expect &&
               (simple_convert[dst_idx].type == DBTYPE_BYTES ||
                simple_convert[dst_idx].type == DBTYPE_STR ||
                simple_convert[dst_idx].type == DBTYPE_WSTR))
                expect = TRUE;
            ok((hr == S_OK && expect == TRUE) ||
               (hr == S_FALSE && expect == FALSE),
               "%04x -> %04x: got %08x expect conversion to be %spossible\n", simple_convert[src_idx].type | DBTYPE_BYREF,
               simple_convert[dst_idx].type | DBTYPE_BYREF, hr, expect ? "" : "not ");

            /* src DBTYPE_ARRAY */
            hr = IDataConvert_CanConvert(convert, simple_convert[src_idx].type | DBTYPE_ARRAY, simple_convert[dst_idx].type);
            expect = FALSE;
            if(array_type(simple_convert[src_idx].type) && simple_convert[dst_idx].type == DBTYPE_VARIANT)
                expect = TRUE;
            ok((hr == S_OK && expect == TRUE) ||
               (hr == S_FALSE && expect == FALSE),
               "%04x -> %04x: got %08x expect conversion to be %spossible\n", simple_convert[src_idx].type | DBTYPE_ARRAY,
               simple_convert[dst_idx].type, hr, expect ? "" : "not ");

            /* dst DBTYPE_ARRAY */
            hr = IDataConvert_CanConvert(convert, simple_convert[src_idx].type, simple_convert[dst_idx].type | DBTYPE_ARRAY);
            expect = FALSE;
            if(array_type(simple_convert[dst_idx].type) &&
               (simple_convert[src_idx].type == DBTYPE_IDISPATCH ||
                simple_convert[src_idx].type == DBTYPE_VARIANT))
                expect = TRUE;
            ok((hr == S_OK && expect == TRUE) ||
               (hr == S_FALSE && expect == FALSE),
               "%04x -> %04x: got %08x expect conversion to be %spossible\n", simple_convert[src_idx].type,
               simple_convert[dst_idx].type | DBTYPE_ARRAY, hr, expect ? "" : "not ");

            /* src & dst DBTYPE_ARRAY */
            hr = IDataConvert_CanConvert(convert, simple_convert[src_idx].type | DBTYPE_ARRAY, simple_convert[dst_idx].type | DBTYPE_ARRAY);
            expect = FALSE;
            if(array_type(simple_convert[src_idx].type) &&
               simple_convert[src_idx].type == simple_convert[dst_idx].type)
                expect = TRUE;
            ok((hr == S_OK && expect == TRUE) ||
               (hr == S_FALSE && expect == FALSE),
               "%04x -> %04x: got %08x expect conversion to be %spossible\n", simple_convert[src_idx].type | DBTYPE_ARRAY,
               simple_convert[dst_idx].type | DBTYPE_ARRAY, hr, expect ? "" : "not ");

            /* src DBTYPE_VECTOR */
            hr = IDataConvert_CanConvert(convert, simple_convert[src_idx].type | DBTYPE_VECTOR, simple_convert[dst_idx].type);
            expect = FALSE;
            ok((hr == S_OK && expect == TRUE) ||
               (hr == S_FALSE && expect == FALSE),
               "%04x -> %04x: got %08x expect conversion to be %spossible\n", simple_convert[src_idx].type | DBTYPE_VECTOR,
               simple_convert[dst_idx].type, hr, expect ? "" : "not ");

            /* dst DBTYPE_VECTOR */
            hr = IDataConvert_CanConvert(convert, simple_convert[src_idx].type, simple_convert[dst_idx].type | DBTYPE_VECTOR);
            expect = FALSE;
            ok((hr == S_OK && expect == TRUE) ||
               (hr == S_FALSE && expect == FALSE),
               "%04x -> %04x: got %08x expect conversion to be %spossible\n", simple_convert[src_idx].type,
               simple_convert[dst_idx].type | DBTYPE_VECTOR, hr, expect ? "" : "not ");

            /* src & dst DBTYPE_VECTOR */
            hr = IDataConvert_CanConvert(convert, simple_convert[src_idx].type | DBTYPE_VECTOR, simple_convert[dst_idx].type | DBTYPE_VECTOR);
            expect = FALSE;
            ok((hr == S_OK && expect == TRUE) ||
               (hr == S_FALSE && expect == FALSE),
               "%04x -> %04x: got %08x expect conversion to be %spossible\n", simple_convert[src_idx].type | DBTYPE_VECTOR,
               simple_convert[dst_idx].type | DBTYPE_VECTOR, hr, expect ? "" : "not ");


        }

    IDataConvert_Release(convert);
}

static void test_converttoi2(void)
{
    IDataConvert *convert;
    HRESULT hr;
    signed short dst;
    BYTE src[20];
    DBSTATUS dst_status;
    DBLENGTH dst_len;
    static const WCHAR ten[] = {'1','0',0};
    BSTR b;

    hr = CoCreateInstance(&CLSID_OLEDB_CONVERSIONLIBRARY, NULL, CLSCTX_INPROC_SERVER, &IID_IDataConvert, (void**)&convert);
    if(FAILED(hr))
    {
        win_skip("Unable to load oledb conversion library\n");
        return;
    }

    dst = 0x1234;
    hr = IDataConvert_DataConvert(convert, DBTYPE_EMPTY, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0, "got %08x\n", dst);

    dst = 0x1234;
    hr = IDataConvert_DataConvert(convert, DBTYPE_NULL, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == DB_E_UNSUPPORTEDCONVERSION, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_BADACCESSOR, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x1234, "got %08x\n", dst);

    dst = 0x1234;
    *(short *)src = 0x4321;
    hr = IDataConvert_DataConvert(convert, DBTYPE_I2, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x4321, "got %08x\n", dst);

    dst = 0x1234;
    *(int *)src = 0x4321cafe;
    hr = IDataConvert_DataConvert(convert, DBTYPE_I4, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
todo_wine
    ok(hr == DB_E_DATAOVERFLOW, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_DATAOVERFLOW, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x1234, "got %08x\n", dst);

    dst = 0x1234;
    *(int *)src = 0x4321;
    hr = IDataConvert_DataConvert(convert, DBTYPE_I4, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x4321, "got %08x\n", dst);

    dst = 0x1234;
    *(FLOAT *)src = 10.75;
    hr = IDataConvert_DataConvert(convert, DBTYPE_R4, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 11, "got %08x\n", dst);

    dst = 0x1234;
    *(FLOAT *)src = -10.75;
    hr = IDataConvert_DataConvert(convert, DBTYPE_R4, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == -11, "got %08x\n", dst);

    dst = 0x1234;
    *(double *)src = 10.75;
    hr = IDataConvert_DataConvert(convert, DBTYPE_R8, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 11, "got %08x\n", dst);

    dst = 0x1234;
    ((LARGE_INTEGER *)src)->QuadPart = 107500;
    hr = IDataConvert_DataConvert(convert, DBTYPE_CY, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 11, "got %08x\n", dst);

    dst = 0x1234;
    *(DATE *)src = 10.7500;
    hr = IDataConvert_DataConvert(convert, DBTYPE_DATE, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 11, "got %08x\n", dst);

    dst = 0x1234;
    b = SysAllocString(ten);
    *(BSTR *)src = b;
    hr = IDataConvert_DataConvert(convert, DBTYPE_BSTR, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 10, "got %08x\n", dst);
    SysFreeString(b);

    dst = 0x1234;
    *(SCODE *)src = 0x4321cafe;
    hr = IDataConvert_DataConvert(convert, DBTYPE_ERROR, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == DB_E_UNSUPPORTEDCONVERSION, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_BADACCESSOR, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x1234, "got %08x\n", dst);

    dst = 0x1234;
    *(VARIANT_BOOL *)src = VARIANT_TRUE;
    hr = IDataConvert_DataConvert(convert, DBTYPE_BOOL, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == -1, "got %08x\n", dst);

    dst = 0x1234;
    *(VARIANT_BOOL *)src = VARIANT_FALSE;
    hr = IDataConvert_DataConvert(convert, DBTYPE_BOOL, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0, "got %08x\n", dst);

    dst = 0x1234;
    V_VT((VARIANT*)src) = VT_I2;
    V_I2((VARIANT*)src) = 0x4321;
    hr = IDataConvert_DataConvert(convert, DBTYPE_VARIANT, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
todo_wine
{
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
}
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
todo_wine
    ok(dst == 0x4321, "got %08x\n", dst);

    dst = 0x1234;
    memset(src, 0, sizeof(DECIMAL));
    ((DECIMAL*)src)->u1.Lo64 = 0x4321;
    hr = IDataConvert_DataConvert(convert, DBTYPE_DECIMAL, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x4321, "got %08x\n", dst);

    dst = 0x1234;
    *(signed char*)src = 0xab;
    hr = IDataConvert_DataConvert(convert, DBTYPE_I1, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == (signed short)0xffab, "got %08x\n", dst);

    dst = 0x1234;
    *(BYTE*)src = 0xab;
    hr = IDataConvert_DataConvert(convert, DBTYPE_UI1, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0xab, "got %08x\n", dst);

    dst = 0x1234;
    *(WORD*)src = 0x4321;
    hr = IDataConvert_DataConvert(convert, DBTYPE_UI2, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x4321, "got %08x\n", dst);

    dst = 0x1234;
    *(WORD*)src = 0xabcd;
    hr = IDataConvert_DataConvert(convert, DBTYPE_UI2, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == DB_E_ERRORSOCCURRED, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_DATAOVERFLOW, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x1234, "got %08x\n", dst);

    dst = 0x1234;
    *(DWORD*)src = 0xabcd1234;
    hr = IDataConvert_DataConvert(convert, DBTYPE_UI4, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
todo_wine
    ok(hr == DB_E_DATAOVERFLOW, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_DATAOVERFLOW, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x1234, "got %08x\n", dst);

    dst = 0x1234;
    *(DWORD*)src = 0x1234abcd;
    hr = IDataConvert_DataConvert(convert, DBTYPE_UI4, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
todo_wine
    ok(hr == DB_E_DATAOVERFLOW, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_DATAOVERFLOW, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x1234, "got %08x\n", dst);

    dst = 0x1234;
    *(DWORD*)src = 0x4321;
    hr = IDataConvert_DataConvert(convert, DBTYPE_UI4, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x4321, "got %08x\n", dst);

    dst = 0x1234;
    ((LARGE_INTEGER*)src)->QuadPart = 0x1234abcd;
    hr = IDataConvert_DataConvert(convert, DBTYPE_I8, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == DB_E_ERRORSOCCURRED, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_DATAOVERFLOW, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x1234, "got %08x\n", dst);

    dst = 0x1234;
    ((LARGE_INTEGER*)src)->QuadPart = 0x4321;
    hr = IDataConvert_DataConvert(convert, DBTYPE_I8, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x4321, "got %08x\n", dst);

    dst = 0x1234;
    ((ULARGE_INTEGER*)src)->QuadPart = 0x4321;
    hr = IDataConvert_DataConvert(convert, DBTYPE_UI8, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 0x4321, "got %08x\n", dst);

    dst = 0x1234;
    strcpy((char *)src, "10");
    hr = IDataConvert_DataConvert(convert, DBTYPE_STR, DBTYPE_I2, 2, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 10, "got %08x\n", dst);

    dst = 0x1234;
    strcpy((char *)src, "10");
    hr = IDataConvert_DataConvert(convert, DBTYPE_STR, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, DBDATACONVERT_LENGTHFROMNTS);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 10, "got %08x\n", dst);

    dst = 0x1234;
    memcpy(src, ten, sizeof(ten));
    hr = IDataConvert_DataConvert(convert, DBTYPE_WSTR, DBTYPE_I2, 4, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 10, "got %08x\n", dst);

    dst = 0x1234;
    memcpy(src, ten, sizeof(ten));
    hr = IDataConvert_DataConvert(convert, DBTYPE_WSTR, DBTYPE_I2, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, DBDATACONVERT_LENGTHFROMNTS);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == 10, "got %08x\n", dst);

    IDataConvert_Release(convert);
}

static void test_converttoi4(void)
{
    IDataConvert *convert;
    HRESULT hr;
    INT i4;
    BYTE src[20];
    DBSTATUS dst_status;
    DBLENGTH dst_len;
    static const WCHAR ten[] = {'1','0',0};
    BSTR b;

    hr = CoCreateInstance(&CLSID_OLEDB_CONVERSIONLIBRARY, NULL, CLSCTX_INPROC_SERVER, &IID_IDataConvert, (void**)&convert);
    if(FAILED(hr))
    {
        win_skip("Unable to load oledb conversion library\n");
        return;
    }

    i4 = 0x12345678;
    hr = IDataConvert_DataConvert(convert, DBTYPE_EMPTY, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0, "got %08x\n", i4);

    i4 = 0x12345678;
    hr = IDataConvert_DataConvert(convert, DBTYPE_NULL, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == DB_E_UNSUPPORTEDCONVERSION, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_BADACCESSOR, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0x12345678, "got %08x\n", i4);

    i4 = 0x12345678;
    *(short *)src = 0x4321;
    hr = IDataConvert_DataConvert(convert, DBTYPE_I2, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0x4321, "got %08x\n", i4);

    i4 = 0x12345678;
    *(int *)src = 0x4321cafe;
    hr = IDataConvert_DataConvert(convert, DBTYPE_I4, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0x4321cafe, "got %08x\n", i4);

    i4 = 0x12345678;
    *(FLOAT *)src = 10.75;
    hr = IDataConvert_DataConvert(convert, DBTYPE_R4, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 11, "got %08x\n", i4);

    i4 = 0x12345678;
    *(FLOAT *)src = -10.75;
    hr = IDataConvert_DataConvert(convert, DBTYPE_R4, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == -11, "got %08x\n", i4);

    i4 = 0x12345678;
    *(double *)src = 10.75;
    hr = IDataConvert_DataConvert(convert, DBTYPE_R8, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 11, "got %08x\n", i4);

    i4 = 0x12345678;
    ((LARGE_INTEGER *)src)->QuadPart = 107500;
    hr = IDataConvert_DataConvert(convert, DBTYPE_CY, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 11, "got %08x\n", i4);

    i4 = 0x12345678;
    *(DATE *)src = 10.7500;
    hr = IDataConvert_DataConvert(convert, DBTYPE_DATE, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 11, "got %08x\n", i4);

    i4 = 0x12345678;
    b = SysAllocString(ten);
    *(BSTR *)src = b;
    hr = IDataConvert_DataConvert(convert, DBTYPE_BSTR, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 10, "got %08x\n", i4);
    SysFreeString(b);

    i4 = 0x12345678;
    *(SCODE *)src = 0x4321cafe;
    hr = IDataConvert_DataConvert(convert, DBTYPE_ERROR, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == DB_E_UNSUPPORTEDCONVERSION, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_BADACCESSOR, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0x12345678, "got %08x\n", i4);

    i4 = 0x12345678;
    *(VARIANT_BOOL *)src = VARIANT_TRUE;
    hr = IDataConvert_DataConvert(convert, DBTYPE_BOOL, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0xffffffff, "got %08x\n", i4);

    i4 = 0x12345678;
    *(VARIANT_BOOL *)src = VARIANT_FALSE;
    hr = IDataConvert_DataConvert(convert, DBTYPE_BOOL, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0, "got %08x\n", i4);

    i4 = 0x12345678;
    V_VT((VARIANT*)src) = VT_I2;
    V_I2((VARIANT*)src) = 0x1234;
    hr = IDataConvert_DataConvert(convert, DBTYPE_VARIANT, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
todo_wine
{
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
}
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
todo_wine
    ok(i4 == 0x1234, "got %08x\n", i4);

    i4 = 0x12345678;
    memset(src, 0, sizeof(DECIMAL));
    ((DECIMAL*)src)->u1.Lo64 = 0x1234;
    hr = IDataConvert_DataConvert(convert, DBTYPE_DECIMAL, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0x1234, "got %08x\n", i4);

    i4 = 0x12345678;
    *(signed char*)src = 0xab;
    hr = IDataConvert_DataConvert(convert, DBTYPE_I1, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0xffffffab, "got %08x\n", i4);

    i4 = 0x12345678;
    *(BYTE*)src = 0xab;
    hr = IDataConvert_DataConvert(convert, DBTYPE_UI1, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0xab, "got %08x\n", i4);

    i4 = 0x12345678;
    *(WORD*)src = 0xabcd;
    hr = IDataConvert_DataConvert(convert, DBTYPE_UI2, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0xabcd, "got %08x\n", i4);

    i4 = 0x12345678;
    *(DWORD*)src = 0xabcd1234;
    hr = IDataConvert_DataConvert(convert, DBTYPE_UI4, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == DB_E_ERRORSOCCURRED, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_DATAOVERFLOW, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0x12345678, "got %08x\n", i4);

    i4 = 0x12345678;
    *(DWORD*)src = 0x1234abcd;
    hr = IDataConvert_DataConvert(convert, DBTYPE_UI4, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0x1234abcd, "got %08x\n", i4);

    i4 = 0x12345678;
    ((LARGE_INTEGER*)src)->QuadPart = 0x1234abcd;
    hr = IDataConvert_DataConvert(convert, DBTYPE_I8, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0x1234abcd, "got %08x\n", i4);

    i4 = 0x12345678;
    ((ULARGE_INTEGER*)src)->QuadPart = 0x1234abcd;
    hr = IDataConvert_DataConvert(convert, DBTYPE_UI8, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 0x1234abcd, "got %08x\n", i4);

    i4 = 0x12345678;
    strcpy((char *)src, "10");
    hr = IDataConvert_DataConvert(convert, DBTYPE_STR, DBTYPE_I4, 2, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 10, "got %08x\n", i4);

    i4 = 0x12345678;
    strcpy((char *)src, "10");
    hr = IDataConvert_DataConvert(convert, DBTYPE_STR, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, DBDATACONVERT_LENGTHFROMNTS);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 10, "got %08x\n", i4);

    i4 = 0x12345678;
    memcpy(src, ten, sizeof(ten));
    hr = IDataConvert_DataConvert(convert, DBTYPE_WSTR, DBTYPE_I4, 4, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 10, "got %08x\n", i4);

    i4 = 0x12345678;
    memcpy(src, ten, sizeof(ten));
    hr = IDataConvert_DataConvert(convert, DBTYPE_WSTR, DBTYPE_I4, 0, &dst_len, src, &i4, sizeof(i4), 0, &dst_status, 0, 0, DBDATACONVERT_LENGTHFROMNTS);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(i4), "got %d\n", dst_len);
    ok(i4 == 10, "got %08x\n", i4);

    IDataConvert_Release(convert);
}

static void test_converttobstr(void)
{
    IDataConvert *convert;
    HRESULT hr;
    BSTR dst;
    BYTE src[20];
    DBSTATUS dst_status;
    DBLENGTH dst_len;
    static const WCHAR ten[] = {'1','0',0};
    BSTR b;

    hr = CoCreateInstance(&CLSID_OLEDB_CONVERSIONLIBRARY, NULL, CLSCTX_INPROC_SERVER, &IID_IDataConvert, (void**)&convert);
    if(FAILED(hr))
    {
        win_skip("Unable to load oledb conversion library\n");
        return;
    }

    hr = IDataConvert_DataConvert(convert, DBTYPE_EMPTY, DBTYPE_BSTR, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst != NULL, "got %p\n", dst);
    ok(SysStringLen(dst) == 0, "got %d\n", SysStringLen(dst));
    SysFreeString(dst);

    dst = (void*)0x1234;
    hr = IDataConvert_DataConvert(convert, DBTYPE_NULL, DBTYPE_BSTR, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == DB_E_UNSUPPORTEDCONVERSION, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_BADACCESSOR, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst == (void*)0x1234, "got %p\n", dst);

    *(short *)src = 4321;
    hr = IDataConvert_DataConvert(convert, DBTYPE_I2, DBTYPE_BSTR, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst != NULL, "got %p\n", dst);
    ok(SysStringLen(dst) == 4, "got %d\n", SysStringLen(dst));
    SysFreeString(dst);

    b = SysAllocString(ten);
    *(BSTR *)src = b;
    hr = IDataConvert_DataConvert(convert, DBTYPE_BSTR, DBTYPE_BSTR, 0, &dst_len, src, &dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == sizeof(dst), "got %d\n", dst_len);
    ok(dst != NULL, "got %p\n", dst);
    ok(dst != b, "got %p src %p\n", dst, b);
    ok(!lstrcmpW(b, dst), "got %s\n", wine_dbgstr_w(dst));
    SysFreeString(dst);
    SysFreeString(b);

    IDataConvert_Release(convert);
}

static void test_converttowstr(void)
{
    IDataConvert *convert;
    HRESULT hr;
    WCHAR dst[100];
    BYTE src[20];
    DBSTATUS dst_status;
    DBLENGTH dst_len;
    static const WCHAR ten[] = {'1','0',0};
    static const WCHAR fourthreetwoone[] = {'4','3','2','1',0};
    BSTR b;

    hr = CoCreateInstance(&CLSID_OLEDB_CONVERSIONLIBRARY, NULL, CLSCTX_INPROC_SERVER, &IID_IDataConvert, (void**)&convert);
    if(FAILED(hr))
    {
        win_skip("Unable to load oledb conversion library\n");
        return;
    }


    memset(dst, 0xcc, sizeof(dst));
    hr = IDataConvert_DataConvert(convert, DBTYPE_EMPTY, DBTYPE_WSTR, 0, &dst_len, src, dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == 0, "got %d\n", dst_len);
    ok(dst[0] == 0, "got %02x\n", dst[0]);
    ok(dst[1] == 0xcccc, "got %02x\n", dst[1]);

    memset(dst, 0xcc, sizeof(dst));
    hr = IDataConvert_DataConvert(convert, DBTYPE_NULL, DBTYPE_WSTR, 0, &dst_len, src, dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == DB_E_UNSUPPORTEDCONVERSION, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_BADACCESSOR, "got %08x\n", dst_status);
    ok(dst_len == 0, "got %d\n", dst_len);
    ok(dst[0] == 0xcccc, "got %02x\n", dst[0]);

    *(short *)src = 4321;
    hr = IDataConvert_DataConvert(convert, DBTYPE_I2, DBTYPE_WSTR, 0, &dst_len, src, dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == 8, "got %d\n", dst_len);
    ok(!lstrcmpW(dst, fourthreetwoone), "got %s\n", wine_dbgstr_w(dst));

    *(short *)src = 4321;
    memset(dst, 0xcc, sizeof(dst));
    hr = IDataConvert_DataConvert(convert, DBTYPE_I2, DBTYPE_WSTR, 0, &dst_len, src, dst, 0, 0, &dst_status, 0, 0, 0);
    ok(hr == DB_E_ERRORSOCCURRED, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_E_DATAOVERFLOW, "got %08x\n", dst_status);
    ok(dst_len == 8, "got %d\n", dst_len);
    ok(dst[0] == 0xcccc, "got %02x\n", dst[0]);

    *(short *)src = 4321;
    memset(dst, 0xcc, sizeof(dst));
    hr = IDataConvert_DataConvert(convert, DBTYPE_I2, DBTYPE_WSTR, 0, &dst_len, src, NULL, 0, 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == 8, "got %d\n", dst_len);
    ok(dst[0] == 0xcccc, "got %02x\n", dst[0]);

    *(short *)src = 4321;
    memset(dst, 0xcc, sizeof(dst));
    hr = IDataConvert_DataConvert(convert, DBTYPE_I2, DBTYPE_WSTR, 0, &dst_len, src, dst, 4, 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_TRUNCATED, "got %08x\n", dst_status);
    ok(dst_len == 8, "got %d\n", dst_len);
    ok(dst[0] == '4', "got %02x\n", dst[0]);
    ok(dst[1] == 0, "got %02x\n", dst[1]);
    ok(dst[2] == 0xcccc, "got %02x\n", dst[2]);

    *(short *)src = 4321;
    memset(dst, 0xcc, sizeof(dst));
    hr = IDataConvert_DataConvert(convert, DBTYPE_I2, DBTYPE_WSTR, 0, &dst_len, src, dst, 2, 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_TRUNCATED, "got %08x\n", dst_status);
    ok(dst_len == 8, "got %d\n", dst_len);
    ok(dst[0] == 0, "got %02x\n", dst[0]);
    ok(dst[1] == 0xcccc, "got %02x\n", dst[1]);

    *(short *)src = 4321;
    memset(dst, 0xcc, sizeof(dst));
    hr = IDataConvert_DataConvert(convert, DBTYPE_I2, DBTYPE_WSTR, 0, &dst_len, src, dst, 8, 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_TRUNCATED, "got %08x\n", dst_status);
    ok(dst_len == 8, "got %d\n", dst_len);
    ok(dst[0] == '4', "got %02x\n", dst[0]);
    ok(dst[1] == '3', "got %02x\n", dst[1]);
    ok(dst[2] == '2', "got %02x\n", dst[2]);
    ok(dst[3] == 0, "got %02x\n", dst[3]);
    ok(dst[4] == 0xcccc, "got %02x\n", dst[4]);

    b = SysAllocString(ten);
    *(BSTR *)src = b;
    hr = IDataConvert_DataConvert(convert, DBTYPE_BSTR, DBTYPE_WSTR, 0, &dst_len, src, dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == 4, "got %d\n", dst_len);
    ok(!lstrcmpW(b, dst), "got %s\n", wine_dbgstr_w(dst));
    SysFreeString(b);

    memcpy(src, ten, sizeof(ten));
    hr = IDataConvert_DataConvert(convert, DBTYPE_WSTR, DBTYPE_WSTR, 2, &dst_len, src, dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == 2, "got %d\n", dst_len);
    ok(dst[0] == '1', "got %02x\n", dst[0]);
    ok(dst[1] == 0, "got %02x\n", dst[1]);

    memcpy(src, ten, sizeof(ten));
    hr = IDataConvert_DataConvert(convert, DBTYPE_WSTR, DBTYPE_WSTR, 4, &dst_len, src, dst, sizeof(dst), 0, &dst_status, 0, 0, 0);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == 4, "got %d\n", dst_len);
    ok(!lstrcmpW(ten, dst), "got %s\n", wine_dbgstr_w(dst));

    memcpy(src, ten, sizeof(ten));
    hr = IDataConvert_DataConvert(convert, DBTYPE_WSTR, DBTYPE_WSTR, 0, &dst_len, src, dst, sizeof(dst), 0, &dst_status, 0, 0, DBDATACONVERT_LENGTHFROMNTS);
    ok(hr == S_OK, "got %08x\n", hr);
    ok(dst_status == DBSTATUS_S_OK, "got %08x\n", dst_status);
    ok(dst_len == 4, "got %d\n", dst_len);
    ok(!lstrcmpW(ten, dst), "got %s\n", wine_dbgstr_w(dst));

    IDataConvert_Release(convert);
}

START_TEST(convert)
{
    OleInitialize(NULL);
    test_dcinfo();
    test_canconvert();
    test_converttoi2();
    test_converttoi4();
    test_converttobstr();
    test_converttowstr();
    OleUninitialize();
}