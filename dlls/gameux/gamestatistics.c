/*
 *    Gameux library coclass GameStatistics implementation
 *
 * Copyright (C) 2010 Mariusz Pluciński
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
#define COBJMACROS

#include "config.h"

#include "ole2.h"
#include "winreg.h"
#include "msxml2.h"
#include "shlwapi.h"
#include "shlobj.h"

#include "gameux.h"
#include "gameux_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(gameux);

/*
 * constant definitions
 */
#define MAX_CATEGORY_LENGTH 60
#define MAX_NAME_LENGTH 30
#define MAX_VALUE_LENGTH 30
#define MAX_CATEGORIES 10
#define MAX_STATS_PER_CATEGORY 10
/*******************************************************************************
 * Game statistics helper components
 */
/*******************************************************************************
 * struct GAMEUX_STATS
 *
 * set of structures for containing game's data
 */
struct GAMEUX_STATS_STAT
{
    WCHAR sName[MAX_NAME_LENGTH+1];
    WCHAR sValue[MAX_VALUE_LENGTH+1];
};
struct GAMEUX_STATS_CATEGORY
{
    WCHAR sName[MAX_CATEGORY_LENGTH+1];
    struct GAMEUX_STATS_STAT stats[MAX_STATS_PER_CATEGORY];
};
struct GAMEUX_STATS
{
    WCHAR sStatsFile[MAX_PATH];
    struct GAMEUX_STATS_CATEGORY categories[MAX_CATEGORIES];
};
/*******************************************************************************
 * GAMEUX_createStatsDirectory
 *
 * Helper function, creates directory to store game statistics
 *
 * Parameters
 *  path                [I]     path to game statistics file.
 *                              base directory of this file will
 *                              be created if it doesn't exists
 */
static HRESULT GAMEUX_createStatsDirectory(LPCWSTR lpFilePath)
{
    HRESULT hr;
    WCHAR lpDirectoryPath[MAX_PATH];
    LPCWSTR lpEnd;

    lpEnd = StrRChrW(lpFilePath, NULL, '\\');
    lstrcpynW(lpDirectoryPath, lpFilePath, lpEnd-lpFilePath+1);

    hr = HRESULT_FROM_WIN32(SHCreateDirectoryExW(NULL, lpDirectoryPath, NULL));

    if(hr == HRESULT_FROM_WIN32(ERROR_FILE_EXISTS) ||
       hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
        hr = S_FALSE;

    return hr;
}
/*******************************************************************
 * GAMEUX_updateStatisticsFile
 *
 * Helper function updating data stored in statistics file
 *
 * Parameters:
 *  data                [I]     pointer to struct containing
 *                              statistics data
 */
static HRESULT GAMEUX_updateStatisticsFile(struct GAMEUX_STATS *stats)
{
    static const WCHAR sStatistics[] = {'S','t','a','t','i','s','t','i','c','s',0};
    static const WCHAR sCategory[] = {'C','a','t','e','g','o','r','y',0};
    static const WCHAR sIndex[] = {'I','n','d','e','x',0};
    static const WCHAR sStatistic[] = {'S','t','a','t','i','s','t','i','c',0};
    static const WCHAR sName[] = {'N','a','m','e',0};
    static const WCHAR sValue[] = {'V','a','l','u','e',0};

    HRESULT hr = S_OK;
    IXMLDOMDocument *document;
    IXMLDOMElement *root, *categoryElement, *statisticsElement;
    IXMLDOMNode *categoryNode, *statisticsNode;
    VARIANT vStatsFilePath, vValue;
    BSTR bstrStatistics = NULL, bstrCategory = NULL, bstrIndex = NULL,
        bstrStatistic = NULL, bstrName = NULL, bstrValue = NULL;
    int i, j;

    TRACE("(%p)\n", stats);

    V_VT(&vStatsFilePath) = VT_BSTR;
    V_BSTR(&vStatsFilePath) = SysAllocString(stats->sStatsFile);
    if(!V_BSTR(&vStatsFilePath))
        hr = E_OUTOFMEMORY;

    if(SUCCEEDED(hr))
        hr = CoCreateInstance(&CLSID_DOMDocument, NULL, CLSCTX_INPROC_SERVER,
                              &IID_IXMLDOMDocument, (void**)&document);

    if(SUCCEEDED(hr))
    {
        bstrStatistics = SysAllocString(sStatistics);
        if(!bstrStatistics)
            hr = E_OUTOFMEMORY;
    }

    if(SUCCEEDED(hr))
        hr = IXMLDOMDocument_createElement(document, bstrStatistics, &root);

    if(SUCCEEDED(hr))
    {
        bstrCategory = SysAllocString(sCategory);
        if(!bstrCategory)
            hr = E_OUTOFMEMORY;
    }

    if(SUCCEEDED(hr))
    {
        bstrIndex = SysAllocString(sIndex);
        if(!bstrIndex)
            hr = E_OUTOFMEMORY;
    }

    if(SUCCEEDED(hr))
    {
        bstrStatistic = SysAllocString(sStatistic);
        if(!bstrStatistic)
            hr = E_OUTOFMEMORY;
    }

    if(SUCCEEDED(hr))
    {
        bstrName = SysAllocString(sName);
        if(!bstrName)
            hr = E_OUTOFMEMORY;
    }

    if(SUCCEEDED(hr))
    {
        bstrValue = SysAllocString(sValue);
        if(!bstrValue)
            hr = E_OUTOFMEMORY;
    }

    if(SUCCEEDED(hr))

    if(SUCCEEDED(hr))
        for(i=0; i<MAX_CATEGORIES; ++i)
        {
            if(lstrlenW(stats->categories[i].sName)==0)
                continue;

            V_VT(&vValue) = VT_INT;
            V_INT(&vValue) = NODE_ELEMENT;

            hr = IXMLDOMDocument_createNode(document, vValue, bstrCategory, NULL, &categoryNode);

            if(SUCCEEDED(hr))
                hr = IXMLDOMNode_QueryInterface(categoryNode, &IID_IXMLDOMElement, (LPVOID*)&categoryElement);

            V_INT(&vValue) = i;
            if(SUCCEEDED(hr))
                hr = IXMLDOMElement_setAttribute(categoryElement, bstrIndex, vValue);

            if(SUCCEEDED(hr))
            {
                V_VT(&vValue) = VT_BSTR;
                V_BSTR(&vValue) = SysAllocString(stats->categories[i].sName);
                if(!V_BSTR(&vValue))
                    hr = E_OUTOFMEMORY;
            }

            if(SUCCEEDED(hr))
            {
                TRACE("storing category %d: %s\n", i, debugstr_w(V_BSTR(&vValue)));
                hr = IXMLDOMElement_setAttribute(categoryElement, bstrName, vValue);
            }

            SysFreeString(V_BSTR(&vValue));

            if(SUCCEEDED(hr))
            {
                for(j=0; j<MAX_STATS_PER_CATEGORY; ++j)
                {
                    if(lstrlenW(stats->categories[i].stats[j].sName)==0)
                        continue;

                    V_VT(&vValue) = VT_INT;
                    V_INT(&vValue) = NODE_ELEMENT;

                    hr = IXMLDOMDocument_createNode(document, vValue, bstrStatistic, NULL, &statisticsNode);

                    if(SUCCEEDED(hr))
                        hr = IXMLDOMNode_QueryInterface(statisticsNode, &IID_IXMLDOMElement, (LPVOID*)&statisticsElement);

                    V_INT(&vValue) = j;
                    if(SUCCEEDED(hr))
                        hr = IXMLDOMElement_setAttribute(statisticsElement, bstrIndex, vValue);

                    if(SUCCEEDED(hr))
                    {
                        V_VT(&vValue) = VT_BSTR;
                        V_BSTR(&vValue) = SysAllocString(stats->categories[i].stats[j].sName);
                        if(!V_BSTR(&vValue))
                            hr = E_OUTOFMEMORY;
                    }

                    if(SUCCEEDED(hr))
                    {
                        TRACE("    storing statistic %d: name: %s\n", j, debugstr_w(V_BSTR(&vValue)));
                        hr = IXMLDOMElement_setAttribute(statisticsElement, bstrName, vValue);
                    }

                    SysFreeString(V_BSTR(&vValue));

                    if(SUCCEEDED(hr))
                    {
                        V_VT(&vValue) = VT_BSTR;
                        V_BSTR(&vValue) = SysAllocString(stats->categories[i].stats[j].sValue);
                        if(!V_BSTR(&vValue))
                            hr = E_OUTOFMEMORY;
                    }

                    if(SUCCEEDED(hr))
                    {
                        TRACE("    storing statistic %d: name: %s\n", j, debugstr_w(V_BSTR(&vValue)));
                        hr = IXMLDOMElement_setAttribute(statisticsElement, bstrValue, vValue);
                    }

                    SysFreeString(V_BSTR(&vValue));

                    if(SUCCEEDED(hr))
                        hr = IXMLDOMElement_appendChild(categoryNode, statisticsNode, &statisticsNode);

                    IXMLDOMElement_Release(statisticsElement);
                    IXMLDOMNode_Release(statisticsNode);
                }
            }

            if(SUCCEEDED(hr))
                hr = IXMLDOMElement_appendChild(root, categoryNode, &categoryNode);

            IXMLDOMElement_Release(categoryElement);
            IXMLDOMNode_Release(categoryNode);

            if(FAILED(hr))
                break;
        }

    if(SUCCEEDED(hr))
        hr = IXMLDOMDocument_putref_documentElement(document, root);

    IXMLDOMElement_Release(root);

    TRACE("saving game statistics in %s file\n", debugstr_w(stats->sStatsFile));
    if(SUCCEEDED(hr))
        hr = GAMEUX_createStatsDirectory(stats->sStatsFile);

    if(SUCCEEDED(hr))
        hr = IXMLDOMDocument_save(document, vStatsFilePath);

    IXMLDOMDocument_Release(document);

    SysFreeString(bstrValue);
    SysFreeString(bstrName);
    SysFreeString(bstrStatistic);
    SysFreeString(bstrIndex);
    SysFreeString(bstrCategory);
    SysFreeString(bstrStatistics);
    SysFreeString(V_BSTR(&vStatsFilePath));
    TRACE("ret=0x%x\n", hr);
    return hr;
}
/*******************************************************************************
 * GAMEUX_buildStatisticsFilePath
 * Creates path to file contaning statistics of game with given id.
 *
 * Parameters:
 *  lpApplicationId                         [I]     application id of game,
 *                                                  as string
 *  lpStatisticsFile                        [O]     array where path will be
 *                                                  stored. It's size must be
 *                                                  at least MAX_PATH
 */
static HRESULT GAMEUX_buildStatisticsFilePath(
        LPCWSTR lpApplicationId,
        LPWSTR lpStatisticsFile)
{
    static const WCHAR sBackslash[] = {'\\',0};
    static const WCHAR sStatisticsDir[] = {'\\','M','i','c','r','o','s','o','f','t',
            '\\','W','i','n','d','o','w','s','\\','G','a','m','e','E','x','p',
            'l','o','r','e','r','\\','G','a','m','e','S','t','a','t','i','s',
            't','i','c','s',0};
    static const WCHAR sDotGamestats[] = {'.','g','a','m','e','s','t','a','t','s',0};

    HRESULT hr;

    hr = SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, lpStatisticsFile);

    if(SUCCEEDED(hr))
    {
        lstrcatW(lpStatisticsFile, sStatisticsDir);
        lstrcatW(lpStatisticsFile, lpApplicationId);
        lstrcatW(lpStatisticsFile, sBackslash);
        lstrcatW(lpStatisticsFile, lpApplicationId);
        lstrcatW(lpStatisticsFile, sDotGamestats);
    }

    return hr;
}
/*******************************************************************************
 * GAMEUX_getAppIdFromGDFPath
 *
 * Loads application identifier associated with given GDF binary.
 * Routine reads identifier from registry, so will fail if game
 * is not registered.
 *
 * Parameters:
 *  GDFBinaryPath                       [I]     path to gdf binary
 *  lpApplicationId                     [O]     place to store application id.
 *                                              must be at least 49 characters
 *                                              to store guid and termination 0
 */
static HRESULT GAMEUX_getAppIdFromGDFPath(
        LPCWSTR GDFBinaryPath,
        LPWSTR lpApplicationId)
{
    static const WCHAR sApplicationId[] =
            {'A','p','p','l','i','c','a','t','i','o','n','I','d',0};

    HRESULT hr;
    GAME_INSTALL_SCOPE installScope;
    GUID instanceId;
    LPWSTR lpRegistryPath;
    DWORD dwLength = 49*sizeof(WCHAR);/* place for GUID */

    TRACE("(%s, %p)\n", debugstr_w(GDFBinaryPath), lpApplicationId);

    if(!GDFBinaryPath)
        return E_INVALIDARG;

    installScope = GIS_CURRENT_USER;
    hr = GAMEUX_FindGameInstanceId(GDFBinaryPath, installScope, &instanceId);

    if(hr == S_FALSE)
    {
        installScope = GIS_ALL_USERS;
        hr = GAMEUX_FindGameInstanceId(GDFBinaryPath, installScope, &instanceId);
    }

    if(hr == S_FALSE)
        /* game not registered, so statistics cannot be used */
        hr = E_FAIL;

    if(SUCCEEDED(hr))
        /* game is registered, let's read it's application id from registry */
        hr = GAMEUX_buildGameRegistryPath(installScope, &instanceId, &lpRegistryPath);

    if(SUCCEEDED(hr))
        hr = HRESULT_FROM_WIN32(RegGetValueW(HKEY_LOCAL_MACHINE,
                lpRegistryPath, sApplicationId, RRF_RT_REG_SZ,
                NULL, lpApplicationId, &dwLength));

    HeapFree(GetProcessHeap(), 0, lpRegistryPath);

    TRACE("found app id: %s, return: %#x\n", debugstr_w(lpApplicationId), hr);
    return hr;
}
/*******************************************************************
 * GAMEUX_loadGameStatisticsFromFile
 * Helper function, loads game statistics from file and stores them
 * in the structure.
 *
 * Parameters:
 *  data                [I/O]   structure containing file name to
 *                              load and data fields to store data in
 */
static HRESULT GAMEUX_loadStatisticsFromFile(struct GAMEUX_STATS *data)
{
    static const WCHAR sStatistics[] = {'S','t','a','t','i','s','t','i','c','s',0};
    static const WCHAR sCategory[] = {'C','a','t','e','g','o','r','y',0};
    static const WCHAR sIndex[] = {'I','n','d','e','x',0};
    static const WCHAR sStatistic[] = {'S','t','a','t','i','s','t','i','c',0};
    static const WCHAR sName[] = {'N','a','m','e',0};
    static const WCHAR sValue[] = {'V','a','l','u','e',0};

    HRESULT hr = S_OK;
    IXMLDOMDocument *document = NULL;
    IXMLDOMElement *root = NULL, *categoryElement, *statisticElement;
    IXMLDOMNode *categoryNode, *statisticNode;
    IXMLDOMNodeList *rootChildren = NULL, *categoryChildren;
    VARIANT vStatsFilePath, vValue;
    BSTR bstrStatistics = NULL, bstrCategory = NULL, bstrIndex = NULL,
        bstrStatistic = NULL, bstrName = NULL, bstrValue = NULL;
    VARIANT_BOOL isSuccessful =  VARIANT_FALSE;
    int i, j;

    TRACE("(%p)\n", data);

    V_VT(&vStatsFilePath) = VT_BSTR;
    V_BSTR(&vStatsFilePath) = SysAllocString(data->sStatsFile);
    if(!V_BSTR(&vStatsFilePath))
        hr = E_OUTOFMEMORY;

    if(SUCCEEDED(hr))
        hr = CoCreateInstance(&CLSID_DOMDocument30, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument, (void**)&document);

    if(SUCCEEDED(hr))
    {
        bstrStatistics = SysAllocString(sStatistics);
        if(!bstrStatistics)
            hr = E_OUTOFMEMORY;
    }

    if(SUCCEEDED(hr))
    {
        bstrCategory = SysAllocString(sCategory);
        if(!bstrCategory)
            hr = E_OUTOFMEMORY;
    }

    if(SUCCEEDED(hr))
    {
        bstrIndex = SysAllocString(sIndex);
        if(!bstrIndex)
            hr = E_OUTOFMEMORY;
    }

    if(SUCCEEDED(hr))
    {
        bstrStatistic = SysAllocString(sStatistic);
        if(!bstrStatistic)
            hr = E_OUTOFMEMORY;
    }

    if(SUCCEEDED(hr))
    {
        bstrName = SysAllocString(sName);
        if(!bstrName)
            hr = E_OUTOFMEMORY;
    }

    if(SUCCEEDED(hr))
    {
        bstrValue = SysAllocString(sValue);
        if(!bstrValue)
            hr = E_OUTOFMEMORY;
    }

    if(SUCCEEDED(hr))
        hr = IXMLDOMDocument_load(document, vStatsFilePath, &isSuccessful);

    if(hr == S_OK && isSuccessful != VARIANT_TRUE)
        hr = S_FALSE;

    if( hr == S_OK )
        hr = IXMLDOMDocument_get_documentElement(document, &root);

    if(hr == S_OK)
        hr = IXMLDOMElement_get_childNodes(root, &rootChildren);

    if(hr == S_OK)
    {
        hr = S_OK;
        while(hr == S_OK)
        {
            hr = IXMLDOMNodeList_nextNode(rootChildren, &categoryNode);

            if(hr == S_OK)
            {
                hr = IXMLDOMNode_QueryInterface(categoryNode, &IID_IXMLDOMElement, (LPVOID*)&categoryElement);

                if(SUCCEEDED(hr))
                {
                    hr = IXMLDOMElement_getAttribute(categoryElement, bstrIndex, &vValue);
                    if( hr == S_OK && V_VT(&vValue) != VT_BSTR)
                        hr = E_FAIL;

                    if(SUCCEEDED(hr))
                    {
                        i = StrToIntW(V_BSTR(&vValue));
                        hr = IXMLDOMElement_getAttribute(categoryElement, bstrName, &vValue);
                        if( hr == S_OK && V_VT(&vValue) != VT_BSTR)
                            hr = E_FAIL;
                    }

                    if(SUCCEEDED(hr))
                    {
                        lstrcpynW(data->categories[i].sName, V_BSTR(&vValue), MAX_CATEGORY_LENGTH);
                        TRACE("category %d name %s\n", i, debugstr_w(data->categories[i].sName));
                        hr = IXMLDOMElement_get_childNodes(categoryElement, &categoryChildren);
                    }

                    if(SUCCEEDED(hr))
                    {
                        hr = S_OK;
                        while(hr == S_OK)
                        {
                            hr = IXMLDOMNodeList_nextNode(categoryChildren, &statisticNode);

                            if(hr == S_OK)
                            {
                                hr = IXMLDOMNode_QueryInterface(statisticNode, &IID_IXMLDOMElement, (LPVOID*)&statisticElement);

                                if(SUCCEEDED(hr))
                                {
                                    hr = IXMLDOMElement_getAttribute(statisticElement, bstrIndex, &vValue);
                                    if( hr == S_OK && V_VT(&vValue) != VT_BSTR)
                                        hr = E_FAIL;

                                    if(SUCCEEDED(hr))
                                    {
                                        j = StrToIntW(V_BSTR(&vValue));
                                        hr = IXMLDOMElement_getAttribute(statisticElement, bstrName, &vValue);
                                        if( hr == S_OK && V_VT(&vValue) != VT_BSTR)
                                            hr = E_FAIL;
                                    }

                                    if(SUCCEEDED(hr))
                                    {
                                        lstrcpynW(data->categories[i].stats[j].sName, V_BSTR(&vValue), MAX_NAME_LENGTH);
                                        hr = IXMLDOMElement_getAttribute(statisticElement, bstrValue, &vValue);
                                        if( hr == S_OK && V_VT(&vValue) != VT_BSTR)
                                            hr = E_FAIL;
                                    }

                                    if(SUCCEEDED(hr))
                                        lstrcpynW(data->categories[i].stats[j].sValue, V_BSTR(&vValue), MAX_VALUE_LENGTH);

                                    TRACE("  statistic %d name %s value %s\n", j,
                                            debugstr_w(data->categories[i].stats[j].sName),
                                            debugstr_w(data->categories[i].stats[j].sValue));
                                    IXMLDOMElement_Release(statisticElement);
                                }

                                IXMLDOMNode_Release(statisticNode);
                            }
                        }

                        if(SUCCEEDED(hr))
                            hr = S_OK;
                    }
                    IXMLDOMElement_Release(categoryElement);
                }

                IXMLDOMNode_Release(categoryNode);
            }
        }
        if(SUCCEEDED(hr))
            hr = S_OK;
    }

    if(rootChildren) IXMLDOMNodeList_Release(rootChildren);
    if(root) IXMLDOMElement_Release(root);
    if(document) IXMLDOMDocument_Release(document);

    SysFreeString(bstrValue);
    SysFreeString(bstrName);
    SysFreeString(bstrStatistic);
    SysFreeString(bstrIndex);
    SysFreeString(bstrCategory);
    SysFreeString(bstrStatistics);
    SysFreeString(V_BSTR(&vStatsFilePath));
    return hr;
}
/*******************************************************************
 * GAMEUX_loadGameStatistics
 *
 * Helper function which loads game statistics associated with game
 * into interface's internal structures
 *
 * Parameters:
 *  pStats              [O]     structure which will receive data
 *  sGameId             [I]     application instance Id, stored as string
 *                              to avoid additional conversions
 *  openType            [I]     allowed ways of opening statistics
 *  pOpenResult         [O]     way used to open statistics
 *
 */
static HRESULT GAMEUX_loadGameStatistics(struct GAMEUX_STATS *pStats,
        LPWSTR sGameId,
        GAMESTATS_OPEN_TYPE openType,
        GAMESTATS_OPEN_RESULT* pOpenResult)
{
    HRESULT hr;
    TRACE("(%p, %s, %d, %p)\n", pStats, debugstr_w(sGameId), openType, pOpenResult);

    hr = GAMEUX_buildStatisticsFilePath(sGameId, pStats->sStatsFile);

    hr = GAMEUX_loadStatisticsFromFile(pStats);
    TRACE("ldstats finished, res: %#x\n", hr);
    if(hr == S_OK)
    {
        *pOpenResult = GAMESTATS_OPEN_OPENED;
    }
    else if(hr == S_FALSE && openType == GAMESTATS_OPEN_OPENORCREATE) /* file does not exist */
    {
        /* create new statitics, not yet connected with file */
        ZeroMemory(pStats->categories, sizeof(pStats->categories));
        *pOpenResult = GAMESTATS_OPEN_CREATED;
        hr = S_OK;
    }
    else
        hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    TRACE("openResult=%#x ret=%#x\n", *pOpenResult, hr);
    return hr;
}
 /*******************************************************************
 * IGameStatistics implementation
 */
typedef struct _GameStatisticsImpl
{
    const struct IGameStatisticsVtbl *lpVtbl;
    LONG ref;
    struct GAMEUX_STATS stats;
} GameStatisticsImpl;

static inline GameStatisticsImpl *impl_from_IGameStatistics( IGameStatistics *iface )
{
    return (GameStatisticsImpl *)((char*)iface - FIELD_OFFSET(GameStatisticsImpl, lpVtbl));
}
static inline IGameStatistics *IGameStatistics_from_impl( GameStatisticsImpl* This )
{
    return (struct IGameStatistics*)&This->lpVtbl;
}


static HRESULT WINAPI GameStatisticsImpl_QueryInterface(
        IGameStatistics *iface,
        REFIID riid,
        void **ppvObject)
{
    GameStatisticsImpl *This = impl_from_IGameStatistics( iface );

    TRACE("%p %s %p\n", This, debugstr_guid( riid ), ppvObject );

    *ppvObject = NULL;

    if ( IsEqualGUID( riid, &IID_IUnknown ) ||
         IsEqualGUID( riid, &IID_IGameStatistics ) )
    {
        *ppvObject = iface;
    }
    else
    {
        FIXME("interface %s not implemented\n", debugstr_guid(riid));
        return E_NOINTERFACE;
    }

    IGameStatistics_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI GameStatisticsImpl_AddRef(IGameStatistics *iface)
{
    GameStatisticsImpl *This = impl_from_IGameStatistics( iface );
    LONG ref;

    ref = InterlockedIncrement(&This->ref);

    TRACE("(%p): ref=%d\n", This, ref);
    return ref;
}

static ULONG WINAPI GameStatisticsImpl_Release(IGameStatistics *iface)
{
    GameStatisticsImpl *This = impl_from_IGameStatistics( iface );
    LONG ref;

    ref = InterlockedDecrement( &This->ref );
    TRACE("(%p): ref=%d\n", This, ref);

    if ( ref == 0 )
    {
        TRACE("freeing IGameStatistics\n");
        HeapFree( GetProcessHeap(), 0, This );
    }

    return ref;
}

static HRESULT WINAPI GameStatisticsImpl_GetMaxCategoryLength(
    IGameStatistics *iface,
    UINT *cch)
{
    TRACE("(%p, %p)\n", iface, cch);
    if(!cch)
        return E_INVALIDARG;

    *cch = MAX_CATEGORY_LENGTH;
    return S_OK;
}

static HRESULT WINAPI GameStatisticsImpl_GetMaxNameLength(
    IGameStatistics *iface,
    UINT *cch)
{
    TRACE("(%p, %p)\n", iface, cch);
    if(!cch)
        return E_INVALIDARG;

    *cch = MAX_NAME_LENGTH;
    return S_OK;
}

static HRESULT WINAPI GameStatisticsImpl_GetMaxValueLength(
    IGameStatistics *iface,
    UINT *cch)
{
    TRACE("(%p, %p)\n", iface, cch);
    if(!cch)
        return E_INVALIDARG;

    *cch = MAX_VALUE_LENGTH;
    return S_OK;
}

static HRESULT WINAPI GameStatisticsImpl_GetMaxCategories(
    IGameStatistics *iface,
    WORD *pMax)
{
    TRACE("(%p, %p)\n", iface, pMax);
    if(!pMax)
        return E_INVALIDARG;

    *pMax = MAX_CATEGORIES;
    return S_OK;
}

static HRESULT WINAPI GameStatisticsImpl_GetMaxStatsPerCategory(
    IGameStatistics *iface,
    WORD *pMax)
{
    TRACE("(%p, %p)\n", iface, pMax);
    if(!pMax)
        return E_INVALIDARG;

    *pMax = MAX_STATS_PER_CATEGORY;
    return S_OK;
}

static HRESULT WINAPI GameStatisticsImpl_SetCategoryTitle(
    IGameStatistics *iface,
    WORD categoryIndex,
    LPCWSTR title)
{
    HRESULT hr = S_OK;
    DWORD dwLength;
    GameStatisticsImpl *This = impl_from_IGameStatistics(iface);

    TRACE("(%p, %d, %s)\n", This, categoryIndex, debugstr_w(title));

    if(!title || categoryIndex >= MAX_CATEGORIES)
        return E_INVALIDARG;

    dwLength = lstrlenW(title);

    if(dwLength > MAX_CATEGORY_LENGTH)
    {
        hr = S_FALSE;
        dwLength = MAX_CATEGORY_LENGTH;
    }

    lstrcpynW(This->stats.categories[categoryIndex].sName,
              title, dwLength+1);

    return hr;
}

static HRESULT WINAPI GameStatisticsImpl_GetCategoryTitle(
    IGameStatistics *iface,
    WORD categoryIndex,
    LPWSTR *pTitle)
{
    HRESULT hr = S_OK;
    LONG nLength;
    GameStatisticsImpl *This = impl_from_IGameStatistics(iface);

    TRACE("%p, %d, %p\n", This, categoryIndex, pTitle);

    *pTitle = NULL;

    if(!pTitle || categoryIndex >= MAX_CATEGORIES)
        hr = E_INVALIDARG;


    if(SUCCEEDED(hr))
    {
        nLength = lstrlenW(This->stats.categories[categoryIndex].sName);
        if(nLength != 0)
        {
            *pTitle = CoTaskMemAlloc(sizeof(WCHAR)*(nLength+1));
            lstrcpyW(*pTitle, This->stats.categories[categoryIndex].sName);
        }
    }

    return hr;
}

static HRESULT WINAPI GameStatisticsImpl_GetStatistic(
    IGameStatistics *iface,
    WORD categoryIndex,
    WORD statIndex,
    LPWSTR *pName,
    LPWSTR *pValue)
{
    HRESULT hr = S_OK;
    LONG nLength;
    GameStatisticsImpl *This = impl_from_IGameStatistics(iface);

    TRACE("%p, %d,%d, %p, %p\n", This, categoryIndex, statIndex, pName, pValue);

    if(!pName || !pValue)
        return E_INVALIDARG;

    *pName = NULL;
    *pValue = NULL;

    if(categoryIndex >= MAX_CATEGORIES || statIndex >= MAX_STATS_PER_CATEGORY)
        hr = E_INVALIDARG;

    if(SUCCEEDED(hr))
    {
        nLength = lstrlenW(This->stats.categories[categoryIndex].stats[statIndex].sName);
        if(nLength != 0)
        {
            *pName = CoTaskMemAlloc(sizeof(WCHAR)*(nLength+1));
            if(!(*pName))
                hr = E_OUTOFMEMORY;
            else
                lstrcpyW(*pName, This->stats.categories[categoryIndex].stats[statIndex].sName);
        }
    }

    if(SUCCEEDED(hr))
    {
        nLength = lstrlenW(This->stats.categories[categoryIndex].stats[statIndex].sValue);
        if(nLength != 0)
        {
            *pValue = CoTaskMemAlloc(sizeof(WCHAR)*(nLength+1));
            if(!(*pValue))
                hr = E_OUTOFMEMORY;
            else
                lstrcpyW(*pValue, This->stats.categories[categoryIndex].stats[statIndex].sValue);
        }
    }

    TRACE("returning pair; %s => %s\n", debugstr_w(*pName), debugstr_w(*pValue));
    return hr;
}

static HRESULT WINAPI GameStatisticsImpl_SetStatistic(
    IGameStatistics *iface,
    WORD categoryIndex,
    WORD statIndex,
    LPCWSTR name,
    LPCWSTR value)
{
    HRESULT hr = S_OK;
    DWORD dwNameLen, dwValueLen;
    GameStatisticsImpl *This = impl_from_IGameStatistics(iface);

    TRACE("(%p, %d, %d, %s, %s)\n", This, categoryIndex, statIndex,
          debugstr_w(name), debugstr_w(value));

    if(!name)
        return S_FALSE;

    if(categoryIndex >= MAX_CATEGORIES || statIndex >= MAX_STATS_PER_CATEGORY)
        return E_INVALIDARG;

    dwNameLen = lstrlenW(name);

    if(dwNameLen > MAX_NAME_LENGTH)
    {
        hr = S_FALSE;
        dwNameLen = MAX_NAME_LENGTH;
    }

    lstrcpynW(This->stats.categories[categoryIndex].stats[statIndex].sName,
              name, dwNameLen+1);

    if(value)
    {
        dwValueLen = lstrlenW(value);

        if(dwValueLen > MAX_VALUE_LENGTH)
        {
            hr = S_FALSE;
            dwValueLen = MAX_VALUE_LENGTH;
        }

        lstrcpynW(This->stats.categories[categoryIndex].stats[statIndex].sValue,
                  value, dwValueLen+1);
    }
    else
        /* Windows allows to pass NULL as value */
        This->stats.categories[categoryIndex].stats[statIndex].sValue[0] = 0;

    return hr;
}

static HRESULT WINAPI GameStatisticsImpl_Save(
    IGameStatistics *iface,
    BOOL trackChanges)
{
    GameStatisticsImpl *This = impl_from_IGameStatistics(iface);
    HRESULT hr = S_OK;

    TRACE("(%p, %d)\n", This, trackChanges);

    if(trackChanges == TRUE)
        FIXME("tracking changes not yet implemented\n");

    hr = GAMEUX_updateStatisticsFile(&This->stats);

    return hr;
}

static HRESULT WINAPI GameStatisticsImpl_SetLastPlayedCategory(
    IGameStatistics *iface,
    UINT categoryIndex)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI GameStatisticsImpl_GetLastPlayedCategory(
    IGameStatistics *iface,
    UINT *pCategoryIndex)
{
    FIXME("stub\n");
    return E_NOTIMPL;
}

static const struct IGameStatisticsVtbl GameStatisticsImplVtbl =
{
    GameStatisticsImpl_QueryInterface,
    GameStatisticsImpl_AddRef,
    GameStatisticsImpl_Release,
    GameStatisticsImpl_GetMaxCategoryLength,
    GameStatisticsImpl_GetMaxNameLength,
    GameStatisticsImpl_GetMaxValueLength,
    GameStatisticsImpl_GetMaxCategories,
    GameStatisticsImpl_GetMaxStatsPerCategory,
    GameStatisticsImpl_SetCategoryTitle,
    GameStatisticsImpl_GetCategoryTitle,
    GameStatisticsImpl_GetStatistic,
    GameStatisticsImpl_SetStatistic,
    GameStatisticsImpl_Save,
    GameStatisticsImpl_SetLastPlayedCategory,
    GameStatisticsImpl_GetLastPlayedCategory
};


HRESULT create_IGameStatistics(GameStatisticsImpl** ppStats)
{
    TRACE("(%p)\n", ppStats);

    *ppStats = HeapAlloc( GetProcessHeap(), 0, sizeof(**ppStats));
    if(!(*ppStats))
        return E_OUTOFMEMORY;

    (*ppStats)->lpVtbl = &GameStatisticsImplVtbl;
    (*ppStats)->ref = 1;

    TRACE("returning coclass: %p\n", *ppStats);
    return S_OK;
}

/*******************************************************************************
 * IGameStatisticsMgr implementation
 */
typedef struct _GameStatisticsMgrImpl
{
    const struct IGameStatisticsMgrVtbl *lpVtbl;
    LONG ref;
} GameStatisticsMgrImpl;

static inline GameStatisticsMgrImpl *impl_from_IGameStatisticsMgr( IGameStatisticsMgr *iface )
{
    return (GameStatisticsMgrImpl *)((char*)iface - FIELD_OFFSET(GameStatisticsMgrImpl, lpVtbl));
}


static HRESULT WINAPI GameStatisticsMgrImpl_QueryInterface(
        IGameStatisticsMgr *iface,
        REFIID riid,
        void **ppvObject)
{
    GameStatisticsMgrImpl *This = impl_from_IGameStatisticsMgr( iface );

    TRACE("%p %s %p\n", This, debugstr_guid( riid ), ppvObject );

    *ppvObject = NULL;

    if(IsEqualGUID(riid, &IID_IUnknown) ||
       IsEqualGUID(riid, &IID_IGameStatisticsMgr) )
    {
        *ppvObject = iface;
    }
    else
    {
        FIXME("interface %s not implemented\n", debugstr_guid(riid));
        return E_NOINTERFACE;
    }

    IGameStatisticsMgr_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI GameStatisticsMgrImpl_AddRef(IGameStatisticsMgr *iface)
{
    GameStatisticsMgrImpl *This = impl_from_IGameStatisticsMgr( iface );
    LONG ref;

    ref = InterlockedIncrement(&This->ref);

    TRACE("(%p): ref=%d\n", This, ref);
    return ref;
}

static ULONG WINAPI GameStatisticsMgrImpl_Release(IGameStatisticsMgr *iface)
{
    GameStatisticsMgrImpl *This = impl_from_IGameStatisticsMgr( iface );
    LONG ref;

    ref = InterlockedDecrement(&This->ref);
    TRACE("(%p): ref=%d\n", This, ref);

    if ( ref == 0 )
    {
        TRACE("freeing GameStatistics object\n");
        HeapFree( GetProcessHeap(), 0, This);
    }

    return ref;
}

static HRESULT STDMETHODCALLTYPE GameStatisticsMgrImpl_GetGameStatistics(
        IGameStatisticsMgr* iface,
        LPCWSTR GDFBinaryPath,
        GAMESTATS_OPEN_TYPE openType,
        GAMESTATS_OPEN_RESULT *pOpenResult,
        IGameStatistics **ppiStats)
{
    HRESULT hr;
    WCHAR lpApplicationId[49];
    GameStatisticsImpl *statisticsImpl;

    TRACE("(%p, %s, 0x%x, %p, %p)\n", iface, debugstr_w(GDFBinaryPath), openType, pOpenResult, ppiStats);

    hr = GAMEUX_getAppIdFromGDFPath(GDFBinaryPath, lpApplicationId);

    if(SUCCEEDED(hr))
        hr = create_IGameStatistics(&statisticsImpl);

    if(SUCCEEDED(hr))
    {
        *ppiStats = IGameStatistics_from_impl(statisticsImpl);
        hr = GAMEUX_buildStatisticsFilePath(lpApplicationId, statisticsImpl->stats.sStatsFile);
    }

    if(SUCCEEDED(hr))
        hr = GAMEUX_loadGameStatistics(&statisticsImpl->stats, lpApplicationId, openType, pOpenResult);

    return hr;
}

static HRESULT STDMETHODCALLTYPE GameStatisticsMgrImpl_RemoveGameStatistics(
        IGameStatisticsMgr* iface,
        LPCWSTR GDFBinaryPath)
{
    HRESULT hr;
    WCHAR lpApplicationId[49];
    WCHAR sStatsFile[MAX_PATH];

    TRACE("(%p, %s)\n", iface, debugstr_w(GDFBinaryPath));

    hr = GAMEUX_getAppIdFromGDFPath(GDFBinaryPath, lpApplicationId);

    if(SUCCEEDED(hr))
        hr = GAMEUX_buildStatisticsFilePath(lpApplicationId, sStatsFile);

    if(SUCCEEDED(hr))
        hr = (DeleteFileW(sStatsFile)==TRUE ? S_OK : HRESULT_FROM_WIN32(GetLastError()));

    return hr;
}

static const struct IGameStatisticsMgrVtbl GameStatisticsMgrImplVtbl =
{
    GameStatisticsMgrImpl_QueryInterface,
    GameStatisticsMgrImpl_AddRef,
    GameStatisticsMgrImpl_Release,
    GameStatisticsMgrImpl_GetGameStatistics,
    GameStatisticsMgrImpl_RemoveGameStatistics,
};

HRESULT GameStatistics_create(
        IUnknown *pUnkOuter,
        IUnknown **ppObj)
{
    GameStatisticsMgrImpl *pGameStatistics;

    TRACE("(%p, %p)\n", pUnkOuter, ppObj);

    pGameStatistics = HeapAlloc( GetProcessHeap(), 0, sizeof (*pGameStatistics) );

    if( !pGameStatistics )
        return E_OUTOFMEMORY;

    pGameStatistics->lpVtbl = &GameStatisticsMgrImplVtbl;
    pGameStatistics->ref = 1;

    *ppObj = (IUnknown*)(&pGameStatistics->lpVtbl);

    TRACE("returning iface %p\n", *ppObj);
    return S_OK;
}
