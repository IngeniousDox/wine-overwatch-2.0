/*
 * Copyright 2006 Jacek Caban for CodeWeavers
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <stdarg.h>
#include <stdio.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winnls.h"
#include "ole2.h"

#include "wine/debug.h"

#include "mshtml_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(mshtml);

typedef struct {
    const IHTMLSelectElementVtbl *lpHTMLSelectElementVtbl;

    HTMLElement *element;
    nsIDOMHTMLSelectElement *nsselect;
} HTMLSelectElement;

#define HTMLSELECT(x)  ((IHTMLSelectElement*)  &(x)->lpHTMLSelectElementVtbl)

#define HTMLSELECT_THIS(iface) DEFINE_THIS(HTMLSelectElement, HTMLSelectElement, iface)

static HRESULT WINAPI HTMLSelectElement_QueryInterface(IHTMLSelectElement *iface,
                                                         REFIID riid, void **ppv)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);

    *ppv = NULL;

    if(IsEqualGUID(&IID_IUnknown, riid)) {
        TRACE("(%p)->(IID_IUnknown %p)\n", This, ppv);
        *ppv = HTMLSELECT(This);
    }else if(IsEqualGUID(&IID_IDispatch, riid)) {
        TRACE("(%p)->(IID_IDispatch %p)\n", This, ppv);
        *ppv = HTMLSELECT(This);
    }else if(IsEqualGUID(&IID_IHTMLSelectElement, riid)) {
        TRACE("(%p)->(IID_IHTMLSelectElement %p)\n", This, ppv);
        *ppv = HTMLSELECT(This);
    }else if(IsEqualGUID(&IID_IHTMLElement, riid)) {
        TRACE("(%p)->(IID_IHTMLElement %p)\n", This, ppv);
        *ppv = HTMLELEM(This->element);
    }else if(IsEqualGUID(&IID_IHTMLDOMNode, riid)) {
        TRACE("(%p)->(IID_IHTMLDOMNode %p)\n", This, ppv);
        *ppv = HTMLDOMNODE(This->element->node);
    }

    if(*ppv) {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("(%p)->(%s %p)\n", This, debugstr_guid(riid), ppv);
    return E_NOINTERFACE;
}

static ULONG WINAPI HTMLSelectElement_AddRef(IHTMLSelectElement *iface)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);

    TRACE("(%p)\n", This);

    return IHTMLDocument2_AddRef(HTMLDOC(This->element->node->doc));
}

static ULONG WINAPI HTMLSelectElement_Release(IHTMLSelectElement *iface)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);

    TRACE("(%p)\n", This);

    return IHTMLDocument2_Release(HTMLDOC(This->element->node->doc));
}

static HRESULT WINAPI HTMLSelectElement_GetTypeInfoCount(IHTMLSelectElement *iface, UINT *pctinfo)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%p)\n", This, pctinfo);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_GetTypeInfo(IHTMLSelectElement *iface, UINT iTInfo,
                                              LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%u %lu %p)\n", This, iTInfo, lcid, ppTInfo);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_GetIDsOfNames(IHTMLSelectElement *iface, REFIID riid,
                                                LPOLESTR *rgszNames, UINT cNames,
                                                LCID lcid, DISPID *rgDispId)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%s %p %u %lu %p)\n", This, debugstr_guid(riid), rgszNames, cNames,
                                        lcid, rgDispId);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_Invoke(IHTMLSelectElement *iface, DISPID dispIdMember,
                            REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
                            VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%ld %s %ld %d %p %p %p %p)\n", This, dispIdMember, debugstr_guid(riid),
            lcid, wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_put_size(IHTMLSelectElement *iface, long v)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%ld)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_get_size(IHTMLSelectElement *iface, long *p)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_put_multiple(IHTMLSelectElement *iface, VARIANT_BOOL v)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%x)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_get_multiple(IHTMLSelectElement *iface, VARIANT_BOOL *p)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_put_name(IHTMLSelectElement *iface, BSTR v)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_get_name(IHTMLSelectElement *iface, BSTR *p)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    nsAString name_str;
    const PRUnichar *name;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&name_str, NULL);

    nsres = nsIDOMHTMLSelectElement_GetName(This->nsselect, &name_str);
    if(NS_SUCCEEDED(nsres)) {
        nsAString_GetData(&name_str, &name, NULL);
        *p = SysAllocString(name);
    }else {
        ERR("GetName failed: %08lx\n", nsres);
    }

    nsAString_Finish(&name_str);

    TRACE("name=%s\n", debugstr_w(name));
    return S_OK;
}

static HRESULT WINAPI HTMLSelectElement_get_options(IHTMLSelectElement *iface, IDispatch **p)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_put_onchange(IHTMLSelectElement *iface, VARIANT v)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_get_onchange(IHTMLSelectElement *iface, VARIANT *p)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_put_selectedIndex(IHTMLSelectElement *iface, long v)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%ld)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_get_selectedIndex(IHTMLSelectElement *iface, long *p)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_get_type(IHTMLSelectElement *iface, BSTR *p)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_put_value(IHTMLSelectElement *iface, BSTR v)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_get_value(IHTMLSelectElement *iface, BSTR *p)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    nsAString value_str;
    const PRUnichar *value;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&value_str, NULL);

    nsres = nsIDOMHTMLSelectElement_GetValue(This->nsselect, &value_str);
    if(NS_SUCCEEDED(nsres)) {
        nsAString_GetData(&value_str, &value, NULL);
        *p = SysAllocString(value);
    }else {
        ERR("GetValue failed: %08lx\n", nsres);
    }

    nsAString_Finish(&value_str);

    TRACE("value=%s\n", debugstr_w(value));
    return S_OK;
}

static HRESULT WINAPI HTMLSelectElement_put_disabled(IHTMLSelectElement *iface, VARIANT_BOOL v)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%x)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_get_disabled(IHTMLSelectElement *iface, VARIANT_BOOL *p)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_get_form(IHTMLSelectElement *iface, IHTMLFormElement **p)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_add(IHTMLSelectElement *iface, IHTMLElement *element,
                                            VARIANT before)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%p v)\n", This, element);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_remove(IHTMLSelectElement *iface, long index)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%ld)\n", This, index);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_put_length(IHTMLSelectElement *iface, long v)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%ld)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_get_length(IHTMLSelectElement *iface, long *p)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_get__newEnum(IHTMLSelectElement *iface, IUnknown **p)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_item(IHTMLSelectElement *iface, VARIANT name,
                                             VARIANT index, IDispatch **pdisp)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(v v %p)\n", This, pdisp);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLSelectElement_tags(IHTMLSelectElement *iface, VARIANT tagName,
                                             IDispatch **pdisp)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);
    FIXME("(%p)->(v %p)\n", This, pdisp);
    return E_NOTIMPL;
}

static void HTMLSelectElement_destructor(IUnknown *iface)
{
    HTMLSelectElement *This = HTMLSELECT_THIS(iface);

    nsIDOMHTMLSelectElement_Release(This->nsselect);
    HeapFree(GetProcessHeap(), 0, This);
}

static const IHTMLSelectElementVtbl HTMLSelectElementVtbl = {
    HTMLSelectElement_QueryInterface,
    HTMLSelectElement_AddRef,
    HTMLSelectElement_Release,
    HTMLSelectElement_GetTypeInfoCount,
    HTMLSelectElement_GetTypeInfo,
    HTMLSelectElement_GetIDsOfNames,
    HTMLSelectElement_Invoke,
    HTMLSelectElement_put_size,
    HTMLSelectElement_get_size,
    HTMLSelectElement_put_multiple,
    HTMLSelectElement_get_multiple,
    HTMLSelectElement_put_name,
    HTMLSelectElement_get_name,
    HTMLSelectElement_get_options,
    HTMLSelectElement_put_onchange,
    HTMLSelectElement_get_onchange,
    HTMLSelectElement_put_selectedIndex,
    HTMLSelectElement_get_selectedIndex,
    HTMLSelectElement_get_type,
    HTMLSelectElement_put_value,
    HTMLSelectElement_get_value,
    HTMLSelectElement_put_disabled,
    HTMLSelectElement_get_disabled,
    HTMLSelectElement_get_form,
    HTMLSelectElement_add,
    HTMLSelectElement_remove,
    HTMLSelectElement_put_length,
    HTMLSelectElement_get_length,
    HTMLSelectElement_get__newEnum,
    HTMLSelectElement_item,
    HTMLSelectElement_tags
};

void HTMLSelectElement_Create(HTMLElement *element)
{
    HTMLSelectElement *ret = HeapAlloc(GetProcessHeap(), 0, sizeof(HTMLSelectElement));
    nsresult nsres;

    ret->lpHTMLSelectElementVtbl = &HTMLSelectElementVtbl;
    ret->element = element;
    
    nsres = nsIDOMHTMLElement_QueryInterface(element->nselem, &IID_nsIDOMHTMLSelectElement,
                                             (void**)&ret->nsselect);
    if(NS_FAILED(nsres))
        ERR("Could not get nsIDOMHTMLSelectElement interfce: %08lx\n", nsres);

    element->impl = (IUnknown*)HTMLSELECT(ret);
    element->destructor = HTMLSelectElement_destructor;
}
