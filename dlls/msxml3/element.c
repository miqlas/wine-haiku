/*
 *    DOM Document implementation
 *
 * Copyright 2005 Mike McCormack
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

#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winnls.h"
#include "ole2.h"
#include "msxml6.h"

#include "msxml_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(msxml);

#ifdef HAVE_LIBXML2

typedef struct _domelem
{
    xmlnode node;
    const struct IXMLDOMElementVtbl *lpVtbl;
    LONG ref;
} domelem;

static inline domelem *impl_from_IXMLDOMElement( IXMLDOMElement *iface )
{
    return (domelem *)((char*)iface - FIELD_OFFSET(domelem, lpVtbl));
}

static inline xmlNodePtr get_element( const domelem *This )
{
    return This->node.node;
}

static HRESULT WINAPI domelem_QueryInterface(
    IXMLDOMElement *iface,
    REFIID riid,
    void** ppvObject )
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), ppvObject);

    if ( IsEqualGUID( riid, &IID_IXMLDOMElement ) ||
         IsEqualGUID( riid, &IID_IXMLDOMNode ) ||
         IsEqualGUID( riid, &IID_IDispatch ) ||
         IsEqualGUID( riid, &IID_IUnknown ) )
    {
        *ppvObject = &This->lpVtbl;
    }
    else if(node_query_interface(&This->node, riid, ppvObject))
    {
        return *ppvObject ? S_OK : E_NOINTERFACE;
    }
    else
    {
        FIXME("interface %s not implemented\n", debugstr_guid(riid));
        return E_NOINTERFACE;
    }

    IUnknown_AddRef( (IUnknown*)*ppvObject );
    return S_OK;
}

static ULONG WINAPI domelem_AddRef(
    IXMLDOMElement *iface )
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    LONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%d\n", This, ref);

    return ref;
}

static ULONG WINAPI domelem_Release(
    IXMLDOMElement *iface )
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%d\n", This, ref);

    if(!ref) {
        destroy_xmlnode(&This->node);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI domelem_GetTypeInfoCount(
    IXMLDOMElement *iface,
    UINT* pctinfo )
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p)\n", This, pctinfo);

    *pctinfo = 1;

    return S_OK;
}

static HRESULT WINAPI domelem_GetTypeInfo(
    IXMLDOMElement *iface,
    UINT iTInfo, LCID lcid,
    ITypeInfo** ppTInfo )
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    HRESULT hr;

    TRACE("(%p)->(%u %u %p)\n", This, iTInfo, lcid, ppTInfo);

    hr = get_typeinfo(IXMLDOMElement_tid, ppTInfo);

    return hr;
}

static HRESULT WINAPI domelem_GetIDsOfNames(
    IXMLDOMElement *iface,
    REFIID riid, LPOLESTR* rgszNames,
    UINT cNames, LCID lcid, DISPID* rgDispId )
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    ITypeInfo *typeinfo;
    HRESULT hr;

    TRACE("(%p)->(%s %p %u %u %p)\n", This, debugstr_guid(riid), rgszNames, cNames,
          lcid, rgDispId);

    if(!rgszNames || cNames == 0 || !rgDispId)
        return E_INVALIDARG;

    hr = get_typeinfo(IXMLDOMElement_tid, &typeinfo);
    if(SUCCEEDED(hr))
    {
        hr = ITypeInfo_GetIDsOfNames(typeinfo, rgszNames, cNames, rgDispId);
        ITypeInfo_Release(typeinfo);
    }

    return hr;
}

static HRESULT WINAPI domelem_Invoke(
    IXMLDOMElement *iface,
    DISPID dispIdMember, REFIID riid, LCID lcid,
    WORD wFlags, DISPPARAMS* pDispParams, VARIANT* pVarResult,
    EXCEPINFO* pExcepInfo, UINT* puArgErr )
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    ITypeInfo *typeinfo;
    HRESULT hr;

    TRACE("(%p)->(%d %s %d %d %p %p %p %p)\n", This, dispIdMember, debugstr_guid(riid),
          lcid, wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);

    hr = get_typeinfo(IXMLDOMElement_tid, &typeinfo);
    if(SUCCEEDED(hr))
    {
        hr = ITypeInfo_Invoke(typeinfo, &(This->lpVtbl), dispIdMember, wFlags, pDispParams,
                pVarResult, pExcepInfo, puArgErr);
        ITypeInfo_Release(typeinfo);
    }

    return hr;
}

static HRESULT WINAPI domelem_get_nodeName(
    IXMLDOMElement *iface,
    BSTR* p )
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p)\n", This, p);

    return node_get_nodeName(&This->node, p);
}

static HRESULT WINAPI domelem_get_nodeValue(
    IXMLDOMElement *iface,
    VARIANT* value)
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p)\n", This, value);

    if(!value)
        return E_INVALIDARG;

    V_VT(value) = VT_NULL;
    V_BSTR(value) = NULL; /* tests show that we should do this */
    return S_FALSE;
}

static HRESULT WINAPI domelem_put_nodeValue(
    IXMLDOMElement *iface,
    VARIANT value)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    TRACE("(%p)->(v%d)\n", This, V_VT(&value));
    return E_FAIL;
}

static HRESULT WINAPI domelem_get_nodeType(
    IXMLDOMElement *iface,
    DOMNodeType* domNodeType )
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p)\n", This, domNodeType);

    *domNodeType = NODE_ELEMENT;
    return S_OK;
}

static HRESULT WINAPI domelem_get_parentNode(
    IXMLDOMElement *iface,
    IXMLDOMNode** parent )
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p)\n", This, parent);

    return node_get_parent(&This->node, parent);
}

static HRESULT WINAPI domelem_get_childNodes(
    IXMLDOMElement *iface,
    IXMLDOMNodeList** outList)
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p)\n", This, outList);

    return node_get_child_nodes(&This->node, outList);
}

static HRESULT WINAPI domelem_get_firstChild(
    IXMLDOMElement *iface,
    IXMLDOMNode** domNode)
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p)\n", This, domNode);

    return node_get_first_child(&This->node, domNode);
}

static HRESULT WINAPI domelem_get_lastChild(
    IXMLDOMElement *iface,
    IXMLDOMNode** domNode)
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p)\n", This, domNode);

    return node_get_last_child(&This->node, domNode);
}

static HRESULT WINAPI domelem_get_previousSibling(
    IXMLDOMElement *iface,
    IXMLDOMNode** domNode)
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p)\n", This, domNode);

    return node_get_previous_sibling(&This->node, domNode);
}

static HRESULT WINAPI domelem_get_nextSibling(
    IXMLDOMElement *iface,
    IXMLDOMNode** domNode)
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p)\n", This, domNode);

    return node_get_next_sibling(&This->node, domNode);
}

static HRESULT WINAPI domelem_get_attributes(
    IXMLDOMElement *iface,
    IXMLDOMNamedNodeMap** attributeMap)
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p)\n", This, attributeMap);

    *attributeMap = create_nodemap((IXMLDOMNode*)&This->lpVtbl);
    return S_OK;
}

static HRESULT WINAPI domelem_insertBefore(
    IXMLDOMElement *iface,
    IXMLDOMNode* newNode, VARIANT refChild,
    IXMLDOMNode** outOldNode)
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p x%d %p)\n", This, newNode, V_VT(&refChild), outOldNode);

    return node_insert_before(&This->node, newNode, &refChild, outOldNode);
}

static HRESULT WINAPI domelem_replaceChild(
    IXMLDOMElement *iface,
    IXMLDOMNode* newNode,
    IXMLDOMNode* oldNode,
    IXMLDOMNode** outOldNode)
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p %p %p)\n", This, newNode, oldNode, outOldNode);

    return node_replace_child(&This->node, newNode, oldNode, outOldNode);
}

static HRESULT WINAPI domelem_removeChild(
    IXMLDOMElement *iface,
    IXMLDOMNode* domNode, IXMLDOMNode** oldNode)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    return IXMLDOMNode_removeChild( IXMLDOMNode_from_impl(&This->node), domNode, oldNode );
}

static HRESULT WINAPI domelem_appendChild(
    IXMLDOMElement *iface,
    IXMLDOMNode* newNode, IXMLDOMNode** outNewNode)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    return IXMLDOMNode_appendChild( IXMLDOMNode_from_impl(&This->node), newNode, outNewNode );
}

static HRESULT WINAPI domelem_hasChildNodes(
    IXMLDOMElement *iface,
    VARIANT_BOOL* pbool)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    return IXMLDOMNode_hasChildNodes( IXMLDOMNode_from_impl(&This->node), pbool );
}

static HRESULT WINAPI domelem_get_ownerDocument(
    IXMLDOMElement *iface,
    IXMLDOMDocument** domDocument)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    return IXMLDOMNode_get_ownerDocument( IXMLDOMNode_from_impl(&This->node), domDocument );
}

static HRESULT WINAPI domelem_cloneNode(
    IXMLDOMElement *iface,
    VARIANT_BOOL deep, IXMLDOMNode** outNode)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    TRACE("(%p)->(%d %p)\n", This, deep, outNode);
    return node_clone( &This->node, deep, outNode );
}

static HRESULT WINAPI domelem_get_nodeTypeString(
    IXMLDOMElement *iface,
    BSTR* p)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    static const WCHAR elementW[] = {'e','l','e','m','e','n','t',0};

    TRACE("(%p)->(%p)\n", This, p);

    return return_bstr(elementW, p);
}

static HRESULT WINAPI domelem_get_text(
    IXMLDOMElement *iface,
    BSTR* p)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    return IXMLDOMNode_get_text( IXMLDOMNode_from_impl(&This->node), p );
}

static HRESULT WINAPI domelem_put_text(
    IXMLDOMElement *iface,
    BSTR p)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    TRACE("(%p)->(%s)\n", This, debugstr_w(p));
    return node_put_text( &This->node, p );
}

static HRESULT WINAPI domelem_get_specified(
    IXMLDOMElement *iface,
    VARIANT_BOOL* isSpecified)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    FIXME("(%p)->(%p) stub!\n", This, isSpecified);
    *isSpecified = VARIANT_TRUE;
    return S_OK;
}

static HRESULT WINAPI domelem_get_definition(
    IXMLDOMElement *iface,
    IXMLDOMNode** definitionNode)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    FIXME("(%p)->(%p)\n", This, definitionNode);
    return E_NOTIMPL;
}

static HRESULT WINAPI domelem_get_nodeTypedValue(
    IXMLDOMElement *iface,
    VARIANT* var1)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    return IXMLDOMNode_get_nodeTypedValue( IXMLDOMNode_from_impl(&This->node), var1 );
}

static HRESULT WINAPI domelem_put_nodeTypedValue(
    IXMLDOMElement *iface,
    VARIANT value)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    VARIANT type;
    HRESULT hr;

    TRACE("(%p)\n", This);

    /* for untyped node coerce to BSTR and set */
    if (IXMLDOMElement_get_dataType(iface, &type) == S_FALSE)
    {
        if (V_VT(&value) != VT_BSTR)
        {
            hr = VariantChangeType(&value, &value, 0, VT_BSTR);
            if (hr == S_OK)
            {
                hr = node_set_content(&This->node, V_BSTR(&value));
                VariantClear(&value);
            }
        }
        else
            hr = node_set_content(&This->node, V_BSTR(&value));
    }
    else
    {
        FIXME("not implemented for typed nodes. type %s\n", debugstr_w(V_BSTR(&value)));
        VariantClear(&type);
        return E_NOTIMPL;
    }

    return hr;
}

static HRESULT WINAPI domelem_get_dataType(
    IXMLDOMElement *iface,
    VARIANT* typename)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    xmlChar *pVal = xmlGetNsProp(get_element(This), (const xmlChar*)"dt",
                                 (const xmlChar*)"urn:schemas-microsoft-com:datatypes");

    TRACE("(%p)->(%p)\n", This, typename);

    V_VT(typename) = VT_NULL;

    if (pVal)
    {
        V_VT(typename) = VT_BSTR;
        V_BSTR(typename) = bstr_from_xmlChar( pVal );
        xmlFree(pVal);
    }

    return (V_VT(typename) != VT_NULL) ? S_OK : S_FALSE;
}

static HRESULT WINAPI domelem_put_dataType(
    IXMLDOMElement *iface,
    BSTR p)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    return IXMLDOMNode_put_dataType( IXMLDOMNode_from_impl(&This->node), p );
}

static HRESULT WINAPI domelem_get_xml(
    IXMLDOMElement *iface,
    BSTR* p)
{
    domelem *This = impl_from_IXMLDOMElement( iface );

    TRACE("(%p)->(%p)\n", This, p);

    return node_get_xml(&This->node, TRUE, FALSE, p);
}

static HRESULT WINAPI domelem_transformNode(
    IXMLDOMElement *iface,
    IXMLDOMNode* domNode, BSTR* p)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    return IXMLDOMNode_transformNode( IXMLDOMNode_from_impl(&This->node), domNode, p );
}

static HRESULT WINAPI domelem_selectNodes(
    IXMLDOMElement *iface,
    BSTR p, IXMLDOMNodeList** outList)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    return IXMLDOMNode_selectNodes( IXMLDOMNode_from_impl(&This->node), p, outList );
}

static HRESULT WINAPI domelem_selectSingleNode(
    IXMLDOMElement *iface,
    BSTR p, IXMLDOMNode** outNode)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    return IXMLDOMNode_selectSingleNode( IXMLDOMNode_from_impl(&This->node), p, outNode );
}

static HRESULT WINAPI domelem_get_parsed(
    IXMLDOMElement *iface,
    VARIANT_BOOL* isParsed)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    FIXME("(%p)->(%p) stub!\n", This, isParsed);
    *isParsed = VARIANT_TRUE;
    return S_OK;
}

static HRESULT WINAPI domelem_get_namespaceURI(
    IXMLDOMElement *iface,
    BSTR* p)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    return IXMLDOMNode_get_namespaceURI( IXMLDOMNode_from_impl(&This->node), p );
}

static HRESULT WINAPI domelem_get_prefix(
    IXMLDOMElement *iface,
    BSTR* prefix)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    TRACE("(%p)->(%p)\n", This, prefix);
    return node_get_prefix( &This->node, prefix );
}

static HRESULT WINAPI domelem_get_baseName(
    IXMLDOMElement *iface,
    BSTR* name)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    TRACE("(%p)->(%p)\n", This, name);
    return node_get_base_name( &This->node, name );
}

static HRESULT WINAPI domelem_transformNodeToObject(
    IXMLDOMElement *iface,
    IXMLDOMNode* domNode, VARIANT var1)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    return IXMLDOMNode_transformNodeToObject( IXMLDOMNode_from_impl(&This->node), domNode, var1 );
}

static HRESULT WINAPI domelem_get_tagName(
    IXMLDOMElement *iface,
    BSTR* p)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    xmlNodePtr element;
    const xmlChar *prefix;
    xmlChar *qname;

    TRACE("(%p)->(%p)\n", This, p );

    if (!p) return E_INVALIDARG;

    element = get_element( This );
    if ( !element )
        return E_FAIL;

    prefix = element->ns ? element->ns->prefix : NULL;
    qname = xmlBuildQName(element->name, prefix, NULL, 0);

    *p = bstr_from_xmlChar(qname);
    if (qname != element->name) xmlFree(qname);

    return *p ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI domelem_getAttribute(
    IXMLDOMElement *iface,
    BSTR name, VARIANT* value)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    xmlNodePtr element;
    xmlChar *xml_name, *xml_value = NULL;
    HRESULT hr = S_FALSE;

    TRACE("(%p)->(%s %p)\n", This, debugstr_w(name), value);

    if(!value || !name)
        return E_INVALIDARG;

    element = get_element( This );
    if ( !element )
        return E_FAIL;

    V_BSTR(value) = NULL;
    V_VT(value) = VT_NULL;

    xml_name = xmlChar_from_wchar( name );

    if(!xmlValidateNameValue(xml_name))
        hr = E_FAIL;
    else
        xml_value = xmlGetNsProp(element, xml_name, NULL);

    heap_free(xml_name);
    if(xml_value)
    {
        V_VT(value) = VT_BSTR;
        V_BSTR(value) = bstr_from_xmlChar( xml_value );
        xmlFree(xml_value);
        hr = S_OK;
    }

    return hr;
}

static HRESULT WINAPI domelem_setAttribute(
    IXMLDOMElement *iface,
    BSTR name, VARIANT value)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    xmlNodePtr element;
    xmlChar *xml_name, *xml_value;
    HRESULT hr;
    VARIANT var;

    TRACE("(%p)->(%s var)\n", This, debugstr_w(name));

    element = get_element( This );
    if ( !element )
        return E_FAIL;

    VariantInit(&var);
    hr = VariantChangeType(&var, &value, 0, VT_BSTR);
    if(hr != S_OK)
    {
        FIXME("VariantChangeType failed\n");
        return hr;
    }

    xml_name = xmlChar_from_wchar( name );
    xml_value = xmlChar_from_wchar( V_BSTR(&var) );

    if(!xmlSetNsProp(element, NULL,  xml_name, xml_value))
        hr = E_FAIL;

    heap_free(xml_value);
    heap_free(xml_name);
    VariantClear(&var);

    return hr;
}

static HRESULT WINAPI domelem_removeAttribute(
    IXMLDOMElement *iface,
    BSTR p)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    IXMLDOMNamedNodeMap *attr;
    HRESULT hr;

    TRACE("(%p)->(%s)\n", This, debugstr_w(p));

    hr = IXMLDOMElement_get_attributes(iface, &attr);
    if (hr != S_OK) return hr;

    hr = IXMLDOMNamedNodeMap_removeNamedItem(attr, p, NULL);
    IXMLDOMNamedNodeMap_Release(attr);

    return hr;
}

static HRESULT WINAPI domelem_getAttributeNode(
    IXMLDOMElement *iface,
    BSTR p, IXMLDOMAttribute** attributeNode )
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    xmlChar *xml_name;
    xmlNodePtr element;
    xmlAttrPtr attr;
    IUnknown *unk;
    HRESULT hr = S_FALSE;

    TRACE("(%p)->(%s %p)\n", This, debugstr_w(p), attributeNode);

    if(!attributeNode)
        return E_FAIL;

    *attributeNode = NULL;

    element = get_element( This );
    if ( !element )
        return E_FAIL;

    xml_name = xmlChar_from_wchar(p);

    if(!xmlValidateNameValue(xml_name))
    {
        heap_free(xml_name);
        return E_FAIL;
    }

    attr = xmlHasProp(element, xml_name);
    if(attr) {
        unk = create_attribute((xmlNodePtr)attr);
        hr = IUnknown_QueryInterface(unk, &IID_IXMLDOMAttribute, (void**)attributeNode);
        IUnknown_Release(unk);
    }

    heap_free(xml_name);

    return hr;
}

static HRESULT WINAPI domelem_setAttributeNode(
    IXMLDOMElement *iface,
    IXMLDOMAttribute* attribute,
    IXMLDOMAttribute** old)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    xmlChar *name, *value;
    BSTR nameW, prefix;
    xmlAttrPtr attr;
    VARIANT valueW;
    HRESULT hr;

    FIXME("(%p)->(%p %p): semi-stub\n", This, attribute, old);

    if (!attribute) return E_INVALIDARG;

    if (old) *old = NULL;

    hr = IXMLDOMAttribute_get_nodeName(attribute, &nameW);
    if (hr != S_OK) return hr;

    hr = IXMLDOMAttribute_get_nodeValue(attribute, &valueW);
    if (hr != S_OK)
    {
        SysFreeString(nameW);
        return hr;
    }

    TRACE("attribute: %s=%s\n", debugstr_w(nameW), debugstr_w(V_BSTR(&valueW)));

    hr = IXMLDOMAttribute_get_prefix(attribute, &prefix);
    if (hr == S_OK)
    {
        FIXME("namespaces not supported: %s\n", debugstr_w(prefix));
        SysFreeString(prefix);
    }

    name = xmlChar_from_wchar(nameW);
    value = xmlChar_from_wchar(V_BSTR(&valueW));

    if (!name || !value)
    {
        SysFreeString(nameW);
        VariantClear(&valueW);
        heap_free(name);
        heap_free(value);
        return E_OUTOFMEMORY;
    }

    attr = xmlSetNsProp(get_element(This), NULL, name, value);

    SysFreeString(nameW);
    VariantClear(&valueW);
    heap_free(name);
    heap_free(value);

    return attr ? S_OK : E_FAIL;
}

static HRESULT WINAPI domelem_removeAttributeNode(
    IXMLDOMElement *iface,
    IXMLDOMAttribute* domAttribute,
    IXMLDOMAttribute** attributeNode)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    FIXME("(%p)->(%p %p)\n", This, domAttribute, attributeNode);
    return E_NOTIMPL;
}

static HRESULT WINAPI domelem_getElementsByTagName(
    IXMLDOMElement *iface,
    BSTR tagName, IXMLDOMNodeList** resultList)
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    xmlNodePtr element;
    HRESULT hr;

    TRACE("(%p)->(%s %p)\n", This, debugstr_w(tagName), resultList);

    if (!tagName || !resultList) return E_INVALIDARG;
    if (!(element = get_element(This))) return E_FAIL;

    if (tagName[0] == '*' && tagName[1] == 0)
    {
        static const WCHAR formatallW[] = {'/','/','*',0};
        hr = queryresult_create(element, formatallW, resultList);
    }
    else
    {
        static const WCHAR xpathformat[] =
            { '/','/','*','[','l','o','c','a','l','-','n','a','m','e','(',')','=','\'' };
        static const WCHAR closeW[] = { '\'',']',0 };

        LPWSTR pattern;
        WCHAR *ptr;
        INT length;

        length = lstrlenW(tagName);

        /* without two WCHARs from format specifier */
        ptr = pattern = heap_alloc(sizeof(xpathformat) + length*sizeof(WCHAR) + sizeof(closeW));

        memcpy(ptr, xpathformat, sizeof(xpathformat));
        ptr += sizeof(xpathformat)/sizeof(WCHAR);
        memcpy(ptr, tagName, length*sizeof(WCHAR));
        ptr += length;
        memcpy(ptr, closeW, sizeof(closeW));

        TRACE("%s\n", debugstr_w(pattern));
        hr = queryresult_create(element, pattern, resultList);
        heap_free(pattern);
    }

    return hr;
}

static HRESULT WINAPI domelem_normalize(
    IXMLDOMElement *iface )
{
    domelem *This = impl_from_IXMLDOMElement( iface );
    FIXME("%p\n", This);
    return E_NOTIMPL;
}

static const struct IXMLDOMElementVtbl domelem_vtbl =
{
    domelem_QueryInterface,
    domelem_AddRef,
    domelem_Release,
    domelem_GetTypeInfoCount,
    domelem_GetTypeInfo,
    domelem_GetIDsOfNames,
    domelem_Invoke,
    domelem_get_nodeName,
    domelem_get_nodeValue,
    domelem_put_nodeValue,
    domelem_get_nodeType,
    domelem_get_parentNode,
    domelem_get_childNodes,
    domelem_get_firstChild,
    domelem_get_lastChild,
    domelem_get_previousSibling,
    domelem_get_nextSibling,
    domelem_get_attributes,
    domelem_insertBefore,
    domelem_replaceChild,
    domelem_removeChild,
    domelem_appendChild,
    domelem_hasChildNodes,
    domelem_get_ownerDocument,
    domelem_cloneNode,
    domelem_get_nodeTypeString,
    domelem_get_text,
    domelem_put_text,
    domelem_get_specified,
    domelem_get_definition,
    domelem_get_nodeTypedValue,
    domelem_put_nodeTypedValue,
    domelem_get_dataType,
    domelem_put_dataType,
    domelem_get_xml,
    domelem_transformNode,
    domelem_selectNodes,
    domelem_selectSingleNode,
    domelem_get_parsed,
    domelem_get_namespaceURI,
    domelem_get_prefix,
    domelem_get_baseName,
    domelem_transformNodeToObject,
    domelem_get_tagName,
    domelem_getAttribute,
    domelem_setAttribute,
    domelem_removeAttribute,
    domelem_getAttributeNode,
    domelem_setAttributeNode,
    domelem_removeAttributeNode,
    domelem_getElementsByTagName,
    domelem_normalize,
};

static const tid_t domelem_iface_tids[] = {
    IXMLDOMElement_tid,
    0
};

static dispex_static_data_t domelem_dispex = {
    NULL,
    IXMLDOMElement_tid,
    NULL,
    domelem_iface_tids
};

IUnknown* create_element( xmlNodePtr element )
{
    domelem *This;

    This = heap_alloc( sizeof *This );
    if ( !This )
        return NULL;

    This->lpVtbl = &domelem_vtbl;
    This->ref = 1;

    init_xmlnode(&This->node, element, (IXMLDOMNode*)&This->lpVtbl, &domelem_dispex);

    return (IUnknown*) &This->lpVtbl;
}

#endif
