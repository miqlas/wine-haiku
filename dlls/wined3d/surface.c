/*
 * IWineD3DSurface Implementation
 *
 * Copyright 1998 Lionel Ulmer
 * Copyright 2000-2001 TransGaming Technologies Inc.
 * Copyright 2002-2005 Jason Edmeades
 * Copyright 2002-2003 Raphael Junqueira
 * Copyright 2004 Christian Costa
 * Copyright 2005 Oliver Stieber
 * Copyright 2006-2008 Stefan Dösinger for CodeWeavers
 * Copyright 2007-2008 Henri Verbeet
 * Copyright 2006-2008 Roderick Colenbrander
 * Copyright 2009-2010 Henri Verbeet for CodeWeavers
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

#include "config.h"
#include "wine/port.h"
#include "wined3d_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d_surface);
WINE_DECLARE_DEBUG_CHANNEL(d3d);

static void surface_cleanup(IWineD3DSurfaceImpl *This)
{
    TRACE("(%p) : Cleaning up.\n", This);

    if (This->texture_name || (This->flags & SFLAG_PBO) || !list_empty(&This->renderbuffers))
    {
        const struct wined3d_gl_info *gl_info;
        renderbuffer_entry_t *entry, *entry2;
        struct wined3d_context *context;

        context = context_acquire(This->resource.device, NULL);
        gl_info = context->gl_info;

        ENTER_GL();

        if (This->texture_name)
        {
            TRACE("Deleting texture %u.\n", This->texture_name);
            glDeleteTextures(1, &This->texture_name);
        }

        if (This->flags & SFLAG_PBO)
        {
            TRACE("Deleting PBO %u.\n", This->pbo);
            GL_EXTCALL(glDeleteBuffersARB(1, &This->pbo));
        }

        LIST_FOR_EACH_ENTRY_SAFE(entry, entry2, &This->renderbuffers, renderbuffer_entry_t, entry)
        {
            TRACE("Deleting renderbuffer %u.\n", entry->id);
            gl_info->fbo_ops.glDeleteRenderbuffers(1, &entry->id);
            HeapFree(GetProcessHeap(), 0, entry);
        }

        LEAVE_GL();

        context_release(context);
    }

    if (This->flags & SFLAG_DIBSECTION)
    {
        /* Release the DC. */
        SelectObject(This->hDC, This->dib.holdbitmap);
        DeleteDC(This->hDC);
        /* Release the DIB section. */
        DeleteObject(This->dib.DIBsection);
        This->dib.bitmap_data = NULL;
        This->resource.allocatedMemory = NULL;
    }

    if (This->flags & SFLAG_USERPTR) IWineD3DSurface_SetMem((IWineD3DSurface *)This, NULL);
    if (This->overlay_dest) list_remove(&This->overlay_entry);

    HeapFree(GetProcessHeap(), 0, This->palette9);

    resource_cleanup((IWineD3DResource *)This);
}

void surface_set_container(IWineD3DSurfaceImpl *surface, enum wined3d_container_type type, IWineD3DBase *container)
{
    TRACE("surface %p, container %p.\n", surface, container);

    if (!container && type != WINED3D_CONTAINER_NONE)
        ERR("Setting NULL container of type %#x.\n", type);

    if (type == WINED3D_CONTAINER_SWAPCHAIN)
    {
        surface->get_drawable_size = get_drawable_size_swapchain;
    }
    else
    {
        switch (wined3d_settings.offscreen_rendering_mode)
        {
            case ORM_FBO:
                surface->get_drawable_size = get_drawable_size_fbo;
                break;

            case ORM_BACKBUFFER:
                surface->get_drawable_size = get_drawable_size_backbuffer;
                break;

            default:
                ERR("Unhandled offscreen rendering mode %#x.\n", wined3d_settings.offscreen_rendering_mode);
                return;
        }
    }

    surface->container.type = type;
    surface->container.u.base = container;
}

struct blt_info
{
    GLenum binding;
    GLenum bind_target;
    enum tex_types tex_type;
    GLfloat coords[4][3];
};

struct float_rect
{
    float l;
    float t;
    float r;
    float b;
};

static inline void cube_coords_float(const RECT *r, UINT w, UINT h, struct float_rect *f)
{
    f->l = ((r->left * 2.0f) / w) - 1.0f;
    f->t = ((r->top * 2.0f) / h) - 1.0f;
    f->r = ((r->right * 2.0f) / w) - 1.0f;
    f->b = ((r->bottom * 2.0f) / h) - 1.0f;
}

static void surface_get_blt_info(GLenum target, const RECT *rect, GLsizei w, GLsizei h, struct blt_info *info)
{
    GLfloat (*coords)[3] = info->coords;
    struct float_rect f;

    switch (target)
    {
        default:
            FIXME("Unsupported texture target %#x\n", target);
            /* Fall back to GL_TEXTURE_2D */
        case GL_TEXTURE_2D:
            info->binding = GL_TEXTURE_BINDING_2D;
            info->bind_target = GL_TEXTURE_2D;
            info->tex_type = tex_2d;
            coords[0][0] = (float)rect->left / w;
            coords[0][1] = (float)rect->top / h;
            coords[0][2] = 0.0f;

            coords[1][0] = (float)rect->right / w;
            coords[1][1] = (float)rect->top / h;
            coords[1][2] = 0.0f;

            coords[2][0] = (float)rect->left / w;
            coords[2][1] = (float)rect->bottom / h;
            coords[2][2] = 0.0f;

            coords[3][0] = (float)rect->right / w;
            coords[3][1] = (float)rect->bottom / h;
            coords[3][2] = 0.0f;
            break;

        case GL_TEXTURE_RECTANGLE_ARB:
            info->binding = GL_TEXTURE_BINDING_RECTANGLE_ARB;
            info->bind_target = GL_TEXTURE_RECTANGLE_ARB;
            info->tex_type = tex_rect;
            coords[0][0] = rect->left;  coords[0][1] = rect->top;       coords[0][2] = 0.0f;
            coords[1][0] = rect->right; coords[1][1] = rect->top;       coords[1][2] = 0.0f;
            coords[2][0] = rect->left;  coords[2][1] = rect->bottom;    coords[2][2] = 0.0f;
            coords[3][0] = rect->right; coords[3][1] = rect->bottom;    coords[3][2] = 0.0f;
            break;

        case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
            info->binding = GL_TEXTURE_BINDING_CUBE_MAP_ARB;
            info->bind_target = GL_TEXTURE_CUBE_MAP_ARB;
            info->tex_type = tex_cube;
            cube_coords_float(rect, w, h, &f);

            coords[0][0] =  1.0f;   coords[0][1] = -f.t;   coords[0][2] = -f.l;
            coords[1][0] =  1.0f;   coords[1][1] = -f.t;   coords[1][2] = -f.r;
            coords[2][0] =  1.0f;   coords[2][1] = -f.b;   coords[2][2] = -f.l;
            coords[3][0] =  1.0f;   coords[3][1] = -f.b;   coords[3][2] = -f.r;
            break;

        case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
            info->binding = GL_TEXTURE_BINDING_CUBE_MAP_ARB;
            info->bind_target = GL_TEXTURE_CUBE_MAP_ARB;
            info->tex_type = tex_cube;
            cube_coords_float(rect, w, h, &f);

            coords[0][0] = -1.0f;   coords[0][1] = -f.t;   coords[0][2] = f.l;
            coords[1][0] = -1.0f;   coords[1][1] = -f.t;   coords[1][2] = f.r;
            coords[2][0] = -1.0f;   coords[2][1] = -f.b;   coords[2][2] = f.l;
            coords[3][0] = -1.0f;   coords[3][1] = -f.b;   coords[3][2] = f.r;
            break;

        case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
            info->binding = GL_TEXTURE_BINDING_CUBE_MAP_ARB;
            info->bind_target = GL_TEXTURE_CUBE_MAP_ARB;
            info->tex_type = tex_cube;
            cube_coords_float(rect, w, h, &f);

            coords[0][0] = f.l;   coords[0][1] =  1.0f;   coords[0][2] = f.t;
            coords[1][0] = f.r;   coords[1][1] =  1.0f;   coords[1][2] = f.t;
            coords[2][0] = f.l;   coords[2][1] =  1.0f;   coords[2][2] = f.b;
            coords[3][0] = f.r;   coords[3][1] =  1.0f;   coords[3][2] = f.b;
            break;

        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
            info->binding = GL_TEXTURE_BINDING_CUBE_MAP_ARB;
            info->bind_target = GL_TEXTURE_CUBE_MAP_ARB;
            info->tex_type = tex_cube;
            cube_coords_float(rect, w, h, &f);

            coords[0][0] = f.l;   coords[0][1] = -1.0f;   coords[0][2] = -f.t;
            coords[1][0] = f.r;   coords[1][1] = -1.0f;   coords[1][2] = -f.t;
            coords[2][0] = f.l;   coords[2][1] = -1.0f;   coords[2][2] = -f.b;
            coords[3][0] = f.r;   coords[3][1] = -1.0f;   coords[3][2] = -f.b;
            break;

        case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
            info->binding = GL_TEXTURE_BINDING_CUBE_MAP_ARB;
            info->bind_target = GL_TEXTURE_CUBE_MAP_ARB;
            info->tex_type = tex_cube;
            cube_coords_float(rect, w, h, &f);

            coords[0][0] = f.l;   coords[0][1] = -f.t;   coords[0][2] =  1.0f;
            coords[1][0] = f.r;   coords[1][1] = -f.t;   coords[1][2] =  1.0f;
            coords[2][0] = f.l;   coords[2][1] = -f.b;   coords[2][2] =  1.0f;
            coords[3][0] = f.r;   coords[3][1] = -f.b;   coords[3][2] =  1.0f;
            break;

        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
            info->binding = GL_TEXTURE_BINDING_CUBE_MAP_ARB;
            info->bind_target = GL_TEXTURE_CUBE_MAP_ARB;
            info->tex_type = tex_cube;
            cube_coords_float(rect, w, h, &f);

            coords[0][0] = -f.l;   coords[0][1] = -f.t;   coords[0][2] = -1.0f;
            coords[1][0] = -f.r;   coords[1][1] = -f.t;   coords[1][2] = -1.0f;
            coords[2][0] = -f.l;   coords[2][1] = -f.b;   coords[2][2] = -1.0f;
            coords[3][0] = -f.r;   coords[3][1] = -f.b;   coords[3][2] = -1.0f;
            break;
    }
}

static inline void surface_get_rect(IWineD3DSurfaceImpl *This, const RECT *rect_in, RECT *rect_out)
{
    if (rect_in)
        *rect_out = *rect_in;
    else
    {
        rect_out->left = 0;
        rect_out->top = 0;
        rect_out->right = This->currentDesc.Width;
        rect_out->bottom = This->currentDesc.Height;
    }
}

/* GL locking and context activation is done by the caller */
void draw_textured_quad(IWineD3DSurfaceImpl *src_surface, const RECT *src_rect, const RECT *dst_rect, WINED3DTEXTUREFILTERTYPE Filter)
{
    struct blt_info info;

    surface_get_blt_info(src_surface->texture_target, src_rect, src_surface->pow2Width, src_surface->pow2Height, &info);

    glEnable(info.bind_target);
    checkGLcall("glEnable(bind_target)");

    /* Bind the texture */
    glBindTexture(info.bind_target, src_surface->texture_name);
    checkGLcall("glBindTexture");

    /* Filtering for StretchRect */
    glTexParameteri(info.bind_target, GL_TEXTURE_MAG_FILTER,
            wined3d_gl_mag_filter(magLookup, Filter));
    checkGLcall("glTexParameteri");
    glTexParameteri(info.bind_target, GL_TEXTURE_MIN_FILTER,
            wined3d_gl_min_mip_filter(minMipLookup, Filter, WINED3DTEXF_NONE));
    checkGLcall("glTexParameteri");
    glTexParameteri(info.bind_target, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(info.bind_target, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    checkGLcall("glTexEnvi");

    /* Draw a quad */
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord3fv(info.coords[0]);
    glVertex2i(dst_rect->left, dst_rect->top);

    glTexCoord3fv(info.coords[1]);
    glVertex2i(dst_rect->right, dst_rect->top);

    glTexCoord3fv(info.coords[2]);
    glVertex2i(dst_rect->left, dst_rect->bottom);

    glTexCoord3fv(info.coords[3]);
    glVertex2i(dst_rect->right, dst_rect->bottom);
    glEnd();

    /* Unbind the texture */
    glBindTexture(info.bind_target, 0);
    checkGLcall("glBindTexture(info->bind_target, 0)");

    /* We changed the filtering settings on the texture. Inform the
     * container about this to get the filters reset properly next draw. */
    if (src_surface->container.type == WINED3D_CONTAINER_TEXTURE)
    {
        IWineD3DBaseTextureImpl *texture = src_surface->container.u.texture;
        texture->baseTexture.texture_rgb.states[WINED3DTEXSTA_MAGFILTER] = WINED3DTEXF_POINT;
        texture->baseTexture.texture_rgb.states[WINED3DTEXSTA_MINFILTER] = WINED3DTEXF_POINT;
        texture->baseTexture.texture_rgb.states[WINED3DTEXSTA_MIPFILTER] = WINED3DTEXF_NONE;
    }
}

HRESULT surface_init(IWineD3DSurfaceImpl *surface, WINED3DSURFTYPE surface_type, UINT alignment,
        UINT width, UINT height, UINT level, BOOL lockable, BOOL discard, WINED3DMULTISAMPLE_TYPE multisample_type,
        UINT multisample_quality, IWineD3DDeviceImpl *device, DWORD usage, enum wined3d_format_id format_id,
        WINED3DPOOL pool, void *parent, const struct wined3d_parent_ops *parent_ops)
{
    const struct wined3d_gl_info *gl_info = &device->adapter->gl_info;
    const struct wined3d_format *format = wined3d_get_format(gl_info, format_id);
    void (*cleanup)(IWineD3DSurfaceImpl *This);
    unsigned int resource_size;
    HRESULT hr;

    if (multisample_quality > 0)
    {
        FIXME("multisample_quality set to %u, substituting 0\n", multisample_quality);
        multisample_quality = 0;
    }

    /* FIXME: Check that the format is supported by the device. */

    resource_size = wined3d_format_calculate_size(format, alignment, width, height);
    if (!resource_size) return WINED3DERR_INVALIDCALL;

    /* Look at the implementation and set the correct Vtable. */
    switch (surface_type)
    {
        case SURFACE_OPENGL:
            surface->lpVtbl = &IWineD3DSurface_Vtbl;
            cleanup = surface_cleanup;
            break;

        case SURFACE_GDI:
            surface->lpVtbl = &IWineGDISurface_Vtbl;
            cleanup = surface_gdi_cleanup;
            break;

        default:
            ERR("Requested unknown surface implementation %#x.\n", surface_type);
            return WINED3DERR_INVALIDCALL;
    }

    hr = resource_init((IWineD3DResource *)surface, WINED3DRTYPE_SURFACE,
            device, resource_size, usage, format, pool, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize resource, returning %#x.\n", hr);
        return hr;
    }

    /* "Standalone" surface. */
    surface_set_container(surface, WINED3D_CONTAINER_NONE, NULL);

    surface->currentDesc.Width = width;
    surface->currentDesc.Height = height;
    surface->currentDesc.MultiSampleType = multisample_type;
    surface->currentDesc.MultiSampleQuality = multisample_quality;
    surface->texture_level = level;
    list_init(&surface->overlays);

    /* Flags */
    surface->flags = SFLAG_NORMCOORD; /* Default to normalized coords. */
    if (discard) surface->flags |= SFLAG_DISCARD;
    if (lockable || format_id == WINED3DFMT_D16_LOCKABLE) surface->flags |= SFLAG_LOCKABLE;

    /* Quick lockable sanity check.
     * TODO: remove this after surfaces, usage and lockability have been debugged properly
     * this function is too deep to need to care about things like this.
     * Levels need to be checked too, since they all affect what can be done. */
    switch (pool)
    {
        case WINED3DPOOL_SCRATCH:
            if(!lockable)
            {
                FIXME("Called with a pool of SCRATCH and a lockable of FALSE "
                        "which are mutually exclusive, setting lockable to TRUE.\n");
                lockable = TRUE;
            }
            break;

        case WINED3DPOOL_SYSTEMMEM:
            if (!lockable)
                FIXME("Called with a pool of SYSTEMMEM and a lockable of FALSE, this is acceptable but unexpected.\n");
            break;

        case WINED3DPOOL_MANAGED:
            if (usage & WINED3DUSAGE_DYNAMIC)
                FIXME("Called with a pool of MANAGED and a usage of DYNAMIC which are mutually exclusive.\n");
            break;

        case WINED3DPOOL_DEFAULT:
            if (lockable && !(usage & (WINED3DUSAGE_DYNAMIC | WINED3DUSAGE_RENDERTARGET | WINED3DUSAGE_DEPTHSTENCIL)))
                WARN("Creating a lockable surface with a POOL of DEFAULT, that doesn't specify DYNAMIC usage.\n");
            break;

        default:
            FIXME("Unknown pool %#x.\n", pool);
            break;
    };

    if (usage & WINED3DUSAGE_RENDERTARGET && pool != WINED3DPOOL_DEFAULT)
    {
        FIXME("Trying to create a render target that isn't in the default pool.\n");
    }

    /* Mark the texture as dirty so that it gets loaded first time around. */
    surface_add_dirty_rect(surface, NULL);
    list_init(&surface->renderbuffers);

    TRACE("surface %p, memory %p, size %u\n", surface, surface->resource.allocatedMemory, surface->resource.size);

    /* Call the private setup routine */
    hr = IWineD3DSurface_PrivateSetup((IWineD3DSurface *)surface);
    if (FAILED(hr))
    {
        ERR("Private setup failed, returning %#x\n", hr);
        cleanup(surface);
        return hr;
    }

    return hr;
}

static void surface_force_reload(IWineD3DSurfaceImpl *surface)
{
    surface->flags &= ~(SFLAG_ALLOCATED | SFLAG_SRGBALLOCATED);
}

void surface_set_texture_name(IWineD3DSurfaceImpl *surface, GLuint new_name, BOOL srgb)
{
    GLuint *name;
    DWORD flag;

    TRACE("surface %p, new_name %u, srgb %#x.\n", surface, new_name, srgb);

    if(srgb)
    {
        name = &surface->texture_name_srgb;
        flag = SFLAG_INSRGBTEX;
    }
    else
    {
        name = &surface->texture_name;
        flag = SFLAG_INTEXTURE;
    }

    if (!*name && new_name)
    {
        /* FIXME: We shouldn't need to remove SFLAG_INTEXTURE if the
         * surface has no texture name yet. See if we can get rid of this. */
        if (surface->flags & flag)
            ERR("Surface has %s set, but no texture name.\n", debug_surflocation(flag));
        surface_modify_location(surface, flag, FALSE);
    }

    *name = new_name;
    surface_force_reload(surface);
}

void surface_set_texture_target(IWineD3DSurfaceImpl *surface, GLenum target)
{
    TRACE("surface %p, target %#x.\n", surface, target);

    if (surface->texture_target != target)
    {
        if (target == GL_TEXTURE_RECTANGLE_ARB)
        {
            surface->flags &= ~SFLAG_NORMCOORD;
        }
        else if (surface->texture_target == GL_TEXTURE_RECTANGLE_ARB)
        {
            surface->flags |= SFLAG_NORMCOORD;
        }
    }
    surface->texture_target = target;
    surface_force_reload(surface);
}

/* Context activation is done by the caller. */
static void surface_bind_and_dirtify(IWineD3DSurfaceImpl *This, BOOL srgb) {
    DWORD active_sampler;

    /* We don't need a specific texture unit, but after binding the texture the current unit is dirty.
     * Read the unit back instead of switching to 0, this avoids messing around with the state manager's
     * gl states. The current texture unit should always be a valid one.
     *
     * To be more specific, this is tricky because we can implicitly be called
     * from sampler() in state.c. This means we can't touch anything other than
     * whatever happens to be the currently active texture, or we would risk
     * marking already applied sampler states dirty again.
     *
     * TODO: Track the current active texture per GL context instead of using glGet
     */
    GLint active_texture;
    ENTER_GL();
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    LEAVE_GL();
    active_sampler = This->resource.device->rev_tex_unit_map[active_texture - GL_TEXTURE0_ARB];

    if (active_sampler != WINED3D_UNMAPPED_STAGE)
    {
        IWineD3DDeviceImpl_MarkStateDirty(This->resource.device, STATE_SAMPLER(active_sampler));
    }
    IWineD3DSurface_BindTexture((IWineD3DSurface *)This, srgb);
}

/* This function checks if the primary render target uses the 8bit paletted format. */
static BOOL primary_render_target_is_p8(IWineD3DDeviceImpl *device)
{
    if (device->render_targets && device->render_targets[0])
    {
        IWineD3DSurfaceImpl *render_target = device->render_targets[0];
        if ((render_target->resource.usage & WINED3DUSAGE_RENDERTARGET)
                && (render_target->resource.format->id == WINED3DFMT_P8_UINT))
            return TRUE;
    }
    return FALSE;
}

/* This call just downloads data, the caller is responsible for binding the
 * correct texture. */
/* Context activation is done by the caller. */
static void surface_download_data(IWineD3DSurfaceImpl *This, const struct wined3d_gl_info *gl_info)
{
    const struct wined3d_format *format = This->resource.format;

    /* Only support read back of converted P8 surfaces */
    if (This->flags & SFLAG_CONVERTED && format->id != WINED3DFMT_P8_UINT)
    {
        FIXME("Readback conversion not supported for format %s.\n", debug_d3dformat(format->id));
        return;
    }

    ENTER_GL();

    if (format->flags & WINED3DFMT_FLAG_COMPRESSED)
    {
        TRACE("(%p) : Calling glGetCompressedTexImageARB level %d, format %#x, type %#x, data %p.\n",
                This, This->texture_level, format->glFormat, format->glType,
                This->resource.allocatedMemory);

        if (This->flags & SFLAG_PBO)
        {
            GL_EXTCALL(glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, This->pbo));
            checkGLcall("glBindBufferARB");
            GL_EXTCALL(glGetCompressedTexImageARB(This->texture_target, This->texture_level, NULL));
            checkGLcall("glGetCompressedTexImageARB");
            GL_EXTCALL(glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, 0));
            checkGLcall("glBindBufferARB");
        }
        else
        {
            GL_EXTCALL(glGetCompressedTexImageARB(This->texture_target,
                    This->texture_level, This->resource.allocatedMemory));
            checkGLcall("glGetCompressedTexImageARB");
        }

        LEAVE_GL();
    } else {
        void *mem;
        GLenum gl_format = format->glFormat;
        GLenum gl_type = format->glType;
        int src_pitch = 0;
        int dst_pitch = 0;

        /* In case of P8 the index is stored in the alpha component if the primary render target uses P8 */
        if (format->id == WINED3DFMT_P8_UINT && primary_render_target_is_p8(This->resource.device))
        {
            gl_format = GL_ALPHA;
            gl_type = GL_UNSIGNED_BYTE;
        }

        if (This->flags & SFLAG_NONPOW2)
        {
            unsigned char alignment = This->resource.device->surface_alignment;
            src_pitch = format->byte_count * This->pow2Width;
            dst_pitch = IWineD3DSurface_GetPitch((IWineD3DSurface *) This);
            src_pitch = (src_pitch + alignment - 1) & ~(alignment - 1);
            mem = HeapAlloc(GetProcessHeap(), 0, src_pitch * This->pow2Height);
        } else {
            mem = This->resource.allocatedMemory;
        }

        TRACE("(%p) : Calling glGetTexImage level %d, format %#x, type %#x, data %p\n",
                This, This->texture_level, gl_format, gl_type, mem);

        if (This->flags & SFLAG_PBO)
        {
            GL_EXTCALL(glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, This->pbo));
            checkGLcall("glBindBufferARB");

            glGetTexImage(This->texture_target, This->texture_level, gl_format, gl_type, NULL);
            checkGLcall("glGetTexImage");

            GL_EXTCALL(glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, 0));
            checkGLcall("glBindBufferARB");
        } else {
            glGetTexImage(This->texture_target, This->texture_level, gl_format, gl_type, mem);
            checkGLcall("glGetTexImage");
        }
        LEAVE_GL();

        if (This->flags & SFLAG_NONPOW2)
        {
            const BYTE *src_data;
            BYTE *dst_data;
            UINT y;
            /*
             * Some games (e.g. warhammer 40k) don't work properly with the odd pitches, preventing
             * the surface pitch from being used to box non-power2 textures. Instead we have to use a hack to
             * repack the texture so that the bpp * width pitch can be used instead of bpp * pow2width.
             *
             * We're doing this...
             *
             * instead of boxing the texture :
             * |<-texture width ->|  -->pow2width|   /\
             * |111111111111111111|              |   |
             * |222 Texture 222222| boxed empty  | texture height
             * |3333 Data 33333333|              |   |
             * |444444444444444444|              |   \/
             * -----------------------------------   |
             * |     boxed  empty | boxed empty  | pow2height
             * |                  |              |   \/
             * -----------------------------------
             *
             *
             * we're repacking the data to the expected texture width
             *
             * |<-texture width ->|  -->pow2width|   /\
             * |111111111111111111222222222222222|   |
             * |222333333333333333333444444444444| texture height
             * |444444                           |   |
             * |                                 |   \/
             * |                                 |   |
             * |            empty                | pow2height
             * |                                 |   \/
             * -----------------------------------
             *
             * == is the same as
             *
             * |<-texture width ->|    /\
             * |111111111111111111|
             * |222222222222222222|texture height
             * |333333333333333333|
             * |444444444444444444|    \/
             * --------------------
             *
             * this also means that any references to allocatedMemory should work with the data as if were a
             * standard texture with a non-power2 width instead of texture boxed up to be a power2 texture.
             *
             * internally the texture is still stored in a boxed format so any references to textureName will
             * get a boxed texture with width pow2width and not a texture of width currentDesc.Width.
             *
             * Performance should not be an issue, because applications normally do not lock the surfaces when
             * rendering. If an app does, the SFLAG_DYNLOCK flag will kick in and the memory copy won't be released,
             * and doesn't have to be re-read.
             */
            src_data = mem;
            dst_data = This->resource.allocatedMemory;
            TRACE("(%p) : Repacking the surface data from pitch %d to pitch %d\n", This, src_pitch, dst_pitch);
            for (y = 1 ; y < This->currentDesc.Height; y++) {
                /* skip the first row */
                src_data += src_pitch;
                dst_data += dst_pitch;
                memcpy(dst_data, src_data, dst_pitch);
            }

            HeapFree(GetProcessHeap(), 0, mem);
        }
    }

    /* Surface has now been downloaded */
    This->flags |= SFLAG_INSYSMEM;
}

/* This call just uploads data, the caller is responsible for binding the
 * correct texture. */
/* Context activation is done by the caller. */
static void surface_upload_data(IWineD3DSurfaceImpl *This, const struct wined3d_gl_info *gl_info,
        const struct wined3d_format *format, BOOL srgb, const GLvoid *data)
{
    GLsizei width = This->currentDesc.Width;
    GLsizei height = This->currentDesc.Height;
    GLenum internal;

    if (srgb)
    {
        internal = format->glGammaInternal;
    }
    else if (This->resource.usage & WINED3DUSAGE_RENDERTARGET && surface_is_offscreen(This))
    {
        internal = format->rtInternal;
    }
    else
    {
        internal = format->glInternal;
    }

    TRACE("This %p, internal %#x, width %d, height %d, format %#x, type %#x, data %p.\n",
            This, internal, width, height, format->glFormat, format->glType, data);
    TRACE("target %#x, level %u, resource size %u.\n",
            This->texture_target, This->texture_level, This->resource.size);

    if (format->heightscale != 1.0f && format->heightscale != 0.0f) height *= format->heightscale;

    ENTER_GL();

    if (This->flags & SFLAG_PBO)
    {
        GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, This->pbo));
        checkGLcall("glBindBufferARB");

        TRACE("(%p) pbo: %#x, data: %p.\n", This, This->pbo, data);
        data = NULL;
    }

    if (format->flags & WINED3DFMT_FLAG_COMPRESSED)
    {
        TRACE("Calling glCompressedTexSubImage2DARB.\n");

        GL_EXTCALL(glCompressedTexSubImage2DARB(This->texture_target, This->texture_level,
                0, 0, width, height, internal, This->resource.size, data));
        checkGLcall("glCompressedTexSubImage2DARB");
    }
    else
    {
        TRACE("Calling glTexSubImage2D.\n");

        glTexSubImage2D(This->texture_target, This->texture_level,
                0, 0, width, height, format->glFormat, format->glType, data);
        checkGLcall("glTexSubImage2D");
    }

    if (This->flags & SFLAG_PBO)
    {
        GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0));
        checkGLcall("glBindBufferARB");
    }

    LEAVE_GL();

    if (gl_info->quirks & WINED3D_QUIRK_FBO_TEX_UPDATE)
    {
        IWineD3DDeviceImpl *device = This->resource.device;
        unsigned int i;

        for (i = 0; i < device->numContexts; ++i)
        {
            context_surface_update(device->contexts[i], This);
        }
    }
}

/* This call just allocates the texture, the caller is responsible for binding
 * the correct texture. */
/* Context activation is done by the caller. */
static void surface_allocate_surface(IWineD3DSurfaceImpl *This, const struct wined3d_gl_info *gl_info,
        const struct wined3d_format *format, BOOL srgb)
{
    BOOL enable_client_storage = FALSE;
    GLsizei width = This->pow2Width;
    GLsizei height = This->pow2Height;
    const BYTE *mem = NULL;
    GLenum internal;

    if (srgb)
    {
        internal = format->glGammaInternal;
    }
    else if (This->resource.usage & WINED3DUSAGE_RENDERTARGET && surface_is_offscreen(This))
    {
        internal = format->rtInternal;
    }
    else
    {
        internal = format->glInternal;
    }

    if (format->heightscale != 1.0f && format->heightscale != 0.0f) height *= format->heightscale;

    TRACE("(%p) : Creating surface (target %#x)  level %d, d3d format %s, internal format %#x, width %d, height %d, gl format %#x, gl type=%#x\n",
            This, This->texture_target, This->texture_level, debug_d3dformat(format->id),
            internal, width, height, format->glFormat, format->glType);

    ENTER_GL();

    if (gl_info->supported[APPLE_CLIENT_STORAGE])
    {
        if (This->flags & (SFLAG_NONPOW2 | SFLAG_DIBSECTION | SFLAG_CONVERTED)
                || !This->resource.allocatedMemory)
        {
            /* In some cases we want to disable client storage.
             * SFLAG_NONPOW2 has a bigger opengl texture than the client memory, and different pitches
             * SFLAG_DIBSECTION: Dibsections may have read / write protections on the memory. Avoid issues...
             * SFLAG_CONVERTED: The conversion destination memory is freed after loading the surface
             * allocatedMemory == NULL: Not defined in the extension. Seems to disable client storage effectively
             */
            glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
            checkGLcall("glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE)");
            This->flags &= ~SFLAG_CLIENT;
            enable_client_storage = TRUE;
        }
        else
        {
            This->flags |= SFLAG_CLIENT;

            /* Point opengl to our allocated texture memory. Do not use resource.allocatedMemory here because
             * it might point into a pbo. Instead use heapMemory, but get the alignment right.
             */
            mem = (BYTE *)(((ULONG_PTR) This->resource.heapMemory + (RESOURCE_ALIGNMENT - 1)) & ~(RESOURCE_ALIGNMENT - 1));
        }
    }

    if (format->flags & WINED3DFMT_FLAG_COMPRESSED && mem)
    {
        GL_EXTCALL(glCompressedTexImage2DARB(This->texture_target, This->texture_level,
                internal, width, height, 0, This->resource.size, mem));
        checkGLcall("glCompressedTexImage2DARB");
    }
    else
    {
        glTexImage2D(This->texture_target, This->texture_level,
                internal, width, height, 0, format->glFormat, format->glType, mem);
        checkGLcall("glTexImage2D");
    }

    if(enable_client_storage) {
        glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
        checkGLcall("glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE)");
    }
    LEAVE_GL();
}

/* In D3D the depth stencil dimensions have to be greater than or equal to the
 * render target dimensions. With FBOs, the dimensions have to be an exact match. */
/* TODO: We should synchronize the renderbuffer's content with the texture's content. */
/* GL locking is done by the caller */
void surface_set_compatible_renderbuffer(IWineD3DSurfaceImpl *surface, unsigned int width, unsigned int height)
{
    const struct wined3d_gl_info *gl_info = &surface->resource.device->adapter->gl_info;
    renderbuffer_entry_t *entry;
    GLuint renderbuffer = 0;
    unsigned int src_width, src_height;

    src_width = surface->pow2Width;
    src_height = surface->pow2Height;

    /* A depth stencil smaller than the render target is not valid */
    if (width > src_width || height > src_height) return;

    /* Remove any renderbuffer set if the sizes match */
    if (gl_info->supported[ARB_FRAMEBUFFER_OBJECT]
            || (width == src_width && height == src_height))
    {
        surface->current_renderbuffer = NULL;
        return;
    }

    /* Look if we've already got a renderbuffer of the correct dimensions */
    LIST_FOR_EACH_ENTRY(entry, &surface->renderbuffers, renderbuffer_entry_t, entry)
    {
        if (entry->width == width && entry->height == height)
        {
            renderbuffer = entry->id;
            surface->current_renderbuffer = entry;
            break;
        }
    }

    if (!renderbuffer)
    {
        gl_info->fbo_ops.glGenRenderbuffers(1, &renderbuffer);
        gl_info->fbo_ops.glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        gl_info->fbo_ops.glRenderbufferStorage(GL_RENDERBUFFER,
                surface->resource.format->glInternal, width, height);

        entry = HeapAlloc(GetProcessHeap(), 0, sizeof(renderbuffer_entry_t));
        entry->width = width;
        entry->height = height;
        entry->id = renderbuffer;
        list_add_head(&surface->renderbuffers, &entry->entry);

        surface->current_renderbuffer = entry;
    }

    checkGLcall("set_compatible_renderbuffer");
}

GLenum surface_get_gl_buffer(IWineD3DSurfaceImpl *surface)
{
    IWineD3DSwapChainImpl *swapchain = surface->container.u.swapchain;

    TRACE("surface %p.\n", surface);

    if (surface->container.type != WINED3D_CONTAINER_SWAPCHAIN)
    {
        ERR("Surface %p is not on a swapchain.\n", surface);
        return GL_NONE;
    }

    if (swapchain->back_buffers && swapchain->back_buffers[0] == surface)
    {
        if (swapchain->render_to_fbo)
        {
            TRACE("Returning GL_COLOR_ATTACHMENT0\n");
            return GL_COLOR_ATTACHMENT0;
        }
        TRACE("Returning GL_BACK\n");
        return GL_BACK;
    }
    else if (surface == swapchain->front_buffer)
    {
        TRACE("Returning GL_FRONT\n");
        return GL_FRONT;
    }

    FIXME("Higher back buffer, returning GL_BACK\n");
    return GL_BACK;
}

/* Slightly inefficient way to handle multiple dirty rects but it works :) */
void surface_add_dirty_rect(IWineD3DSurfaceImpl *surface, const RECT *dirty_rect)
{
    TRACE("surface %p, dirty_rect %s.\n", surface, wine_dbgstr_rect(dirty_rect));

    if (!(surface->flags & SFLAG_INSYSMEM) && (surface->flags & SFLAG_INTEXTURE))
        /* No partial locking for textures yet. */
        surface_load_location(surface, SFLAG_INSYSMEM, NULL);

    surface_modify_location(surface, SFLAG_INSYSMEM, TRUE);
    if (dirty_rect)
    {
        surface->dirtyRect.left = min(surface->dirtyRect.left, dirty_rect->left);
        surface->dirtyRect.top = min(surface->dirtyRect.top, dirty_rect->top);
        surface->dirtyRect.right = max(surface->dirtyRect.right, dirty_rect->right);
        surface->dirtyRect.bottom = max(surface->dirtyRect.bottom, dirty_rect->bottom);
    }
    else
    {
        surface->dirtyRect.left = 0;
        surface->dirtyRect.top = 0;
        surface->dirtyRect.right = surface->currentDesc.Width;
        surface->dirtyRect.bottom = surface->currentDesc.Height;
    }

    /* if the container is a basetexture then mark it dirty. */
    if (surface->container.type == WINED3D_CONTAINER_TEXTURE)
    {
        TRACE("Passing to container.\n");
        IWineD3DBaseTexture_SetDirty((IWineD3DBaseTexture *)surface->container.u.texture, TRUE);
    }
}

static BOOL surface_convert_color_to_float(IWineD3DSurfaceImpl *surface, DWORD color, WINED3DCOLORVALUE *float_color)
{
    const struct wined3d_format *format = surface->resource.format;
    IWineD3DDeviceImpl *device = surface->resource.device;

    switch (format->id)
    {
        case WINED3DFMT_P8_UINT:
            if (surface->palette)
            {
                float_color->r = surface->palette->palents[color].peRed / 255.0f;
                float_color->g = surface->palette->palents[color].peGreen / 255.0f;
                float_color->b = surface->palette->palents[color].peBlue / 255.0f;
            }
            else
            {
                float_color->r = 0.0f;
                float_color->g = 0.0f;
                float_color->b = 0.0f;
            }
            float_color->a = primary_render_target_is_p8(device) ? color / 255.0f : 1.0f;
            break;

        case WINED3DFMT_B5G6R5_UNORM:
            float_color->r = ((color >> 11) & 0x1f) / 31.0f;
            float_color->g = ((color >> 5) & 0x3f) / 63.0f;
            float_color->b = (color & 0x1f) / 31.0f;
            float_color->a = 1.0f;
            break;

        case WINED3DFMT_B8G8R8_UNORM:
        case WINED3DFMT_B8G8R8X8_UNORM:
            float_color->r = D3DCOLOR_R(color);
            float_color->g = D3DCOLOR_G(color);
            float_color->b = D3DCOLOR_B(color);
            float_color->a = 1.0f;
            break;

        case WINED3DFMT_B8G8R8A8_UNORM:
            float_color->r = D3DCOLOR_R(color);
            float_color->g = D3DCOLOR_G(color);
            float_color->b = D3DCOLOR_B(color);
            float_color->a = D3DCOLOR_A(color);
            break;

        default:
            ERR("Unhandled conversion from %s to floating point.\n", debug_d3dformat(format->id));
            return FALSE;
    }

    return TRUE;
}

/* Do not call while under the GL lock. */
static ULONG WINAPI IWineD3DSurfaceImpl_Release(IWineD3DSurface *iface)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;
    ULONG ref = InterlockedDecrement(&This->resource.ref);
    TRACE("(%p) : Releasing from %d\n", This, ref + 1);

    if (!ref)
    {
        surface_cleanup(This);
        This->resource.parent_ops->wined3d_object_destroyed(This->resource.parent);

        TRACE("(%p) Released.\n", This);
        HeapFree(GetProcessHeap(), 0, This);
    }

    return ref;
}

/* ****************************************************
   IWineD3DSurface IWineD3DResource parts follow
   **************************************************** */

/* Do not call while under the GL lock. */
void surface_internal_preload(IWineD3DSurfaceImpl *surface, enum WINED3DSRGB srgb)
{
    IWineD3DDeviceImpl *device = surface->resource.device;

    TRACE("iface %p, srgb %#x.\n", surface, srgb);

    if (surface->container.type == WINED3D_CONTAINER_TEXTURE)
    {
        IWineD3DBaseTextureImpl *texture = surface->container.u.texture;

        TRACE("Passing to container.\n");
        texture->baseTexture.internal_preload((IWineD3DBaseTexture *)texture, srgb);
    }
    else
    {
        struct wined3d_context *context = NULL;

        TRACE("(%p) : About to load surface\n", surface);

        if (!device->isInDraw) context = context_acquire(device, NULL);

        if (surface->resource.format->id == WINED3DFMT_P8_UINT
                || surface->resource.format->id == WINED3DFMT_P8_UINT_A8_UNORM)
        {
            if (palette9_changed(surface))
            {
                TRACE("Reloading surface because the d3d8/9 palette was changed\n");
                /* TODO: This is not necessarily needed with hw palettized texture support */
                surface_load_location(surface, SFLAG_INSYSMEM, NULL);
                /* Make sure the texture is reloaded because of the palette change, this kills performance though :( */
                surface_modify_location(surface, SFLAG_INTEXTURE, FALSE);
            }
        }

        IWineD3DSurface_LoadTexture((IWineD3DSurface *)surface, srgb == SRGB_SRGB ? TRUE : FALSE);

        if (surface->resource.pool == WINED3DPOOL_DEFAULT)
        {
            /* Tell opengl to try and keep this texture in video ram (well mostly) */
            GLclampf tmp;
            tmp = 0.9f;
            ENTER_GL();
            glPrioritizeTextures(1, &surface->texture_name, &tmp);
            LEAVE_GL();
        }

        if (context) context_release(context);
    }
}

static void WINAPI IWineD3DSurfaceImpl_PreLoad(IWineD3DSurface *iface)
{
    surface_internal_preload((IWineD3DSurfaceImpl *)iface, SRGB_ANY);
}

/* Context activation is done by the caller. */
static void surface_remove_pbo(IWineD3DSurfaceImpl *This, const struct wined3d_gl_info *gl_info)
{
    This->resource.heapMemory = HeapAlloc(GetProcessHeap() ,0 , This->resource.size + RESOURCE_ALIGNMENT);
    This->resource.allocatedMemory =
            (BYTE *)(((ULONG_PTR) This->resource.heapMemory + (RESOURCE_ALIGNMENT - 1)) & ~(RESOURCE_ALIGNMENT - 1));

    ENTER_GL();
    GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, This->pbo));
    checkGLcall("glBindBufferARB(GL_PIXEL_UNPACK_BUFFER, This->pbo)");
    GL_EXTCALL(glGetBufferSubDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0, This->resource.size, This->resource.allocatedMemory));
    checkGLcall("glGetBufferSubDataARB");
    GL_EXTCALL(glDeleteBuffersARB(1, &This->pbo));
    checkGLcall("glDeleteBuffersARB");
    LEAVE_GL();

    This->pbo = 0;
    This->flags &= ~SFLAG_PBO;
}

BOOL surface_init_sysmem(IWineD3DSurfaceImpl *surface)
{
    if (!surface->resource.allocatedMemory)
    {
        surface->resource.heapMemory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                surface->resource.size + RESOURCE_ALIGNMENT);
        if (!surface->resource.heapMemory)
        {
            ERR("Out of memory\n");
            return FALSE;
        }
        surface->resource.allocatedMemory =
            (BYTE *)(((ULONG_PTR)surface->resource.heapMemory + (RESOURCE_ALIGNMENT - 1)) & ~(RESOURCE_ALIGNMENT - 1));
    }
    else
    {
        memset(surface->resource.allocatedMemory, 0, surface->resource.size);
    }

    surface_modify_location(surface, SFLAG_INSYSMEM, TRUE);

    return TRUE;
}

/* Do not call while under the GL lock. */
static void WINAPI IWineD3DSurfaceImpl_UnLoad(IWineD3DSurface *iface)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *) iface;
    IWineD3DDeviceImpl *device = This->resource.device;
    const struct wined3d_gl_info *gl_info;
    renderbuffer_entry_t *entry, *entry2;
    struct wined3d_context *context;

    TRACE("(%p)\n", iface);

    if(This->resource.pool == WINED3DPOOL_DEFAULT) {
        /* Default pool resources are supposed to be destroyed before Reset is called.
         * Implicit resources stay however. So this means we have an implicit render target
         * or depth stencil. The content may be destroyed, but we still have to tear down
         * opengl resources, so we cannot leave early.
         *
         * Put the surfaces into sysmem, and reset the content. The D3D content is undefined,
         * but we can't set the sysmem INDRAWABLE because when we're rendering the swapchain
         * or the depth stencil into an FBO the texture or render buffer will be removed
         * and all flags get lost
         */
        surface_init_sysmem(This);
    }
    else
    {
        /* Load the surface into system memory */
        surface_load_location(This, SFLAG_INSYSMEM, NULL);
        surface_modify_location(This, SFLAG_INDRAWABLE, FALSE);
    }
    surface_modify_location(This, SFLAG_INTEXTURE, FALSE);
    surface_modify_location(This, SFLAG_INSRGBTEX, FALSE);
    This->flags &= ~(SFLAG_ALLOCATED | SFLAG_SRGBALLOCATED);

    context = context_acquire(device, NULL);
    gl_info = context->gl_info;

    /* Destroy PBOs, but load them into real sysmem before */
    if (This->flags & SFLAG_PBO)
        surface_remove_pbo(This, gl_info);

    /* Destroy fbo render buffers. This is needed for implicit render targets, for
     * all application-created targets the application has to release the surface
     * before calling _Reset
     */
    LIST_FOR_EACH_ENTRY_SAFE(entry, entry2, &This->renderbuffers, renderbuffer_entry_t, entry) {
        ENTER_GL();
        gl_info->fbo_ops.glDeleteRenderbuffers(1, &entry->id);
        LEAVE_GL();
        list_remove(&entry->entry);
        HeapFree(GetProcessHeap(), 0, entry);
    }
    list_init(&This->renderbuffers);
    This->current_renderbuffer = NULL;

    /* If we're in a texture, the texture name belongs to the texture.
     * Otherwise, destroy it. */
    if (This->container.type != WINED3D_CONTAINER_TEXTURE)
    {
        ENTER_GL();
        glDeleteTextures(1, &This->texture_name);
        This->texture_name = 0;
        glDeleteTextures(1, &This->texture_name_srgb);
        This->texture_name_srgb = 0;
        LEAVE_GL();
    }

    context_release(context);

    resource_unload((IWineD3DResourceImpl *)This);
}

/* ******************************************************
   IWineD3DSurface IWineD3DSurface parts follow
   ****************************************************** */

/* Read the framebuffer back into the surface */
static void read_from_framebuffer(IWineD3DSurfaceImpl *This, const RECT *rect, void *dest, UINT pitch)
{
    IWineD3DDeviceImpl *device = This->resource.device;
    const struct wined3d_gl_info *gl_info;
    struct wined3d_context *context;
    BYTE *mem;
    GLint fmt;
    GLint type;
    BYTE *row, *top, *bottom;
    int i;
    BOOL bpp;
    RECT local_rect;
    BOOL srcIsUpsideDown;
    GLint rowLen = 0;
    GLint skipPix = 0;
    GLint skipRow = 0;

    if(wined3d_settings.rendertargetlock_mode == RTL_DISABLE) {
        static BOOL warned = FALSE;
        if(!warned) {
            ERR("The application tries to lock the render target, but render target locking is disabled\n");
            warned = TRUE;
        }
        return;
    }

    context = context_acquire(device, This);
    context_apply_blit_state(context, device);
    gl_info = context->gl_info;

    ENTER_GL();

    /* Select the correct read buffer, and give some debug output.
     * There is no need to keep track of the current read buffer or reset it, every part of the code
     * that reads sets the read buffer as desired.
     */
    if (surface_is_offscreen(This))
    {
        /* Mapping the primary render target which is not on a swapchain.
         * Read from the back buffer. */
        TRACE("Mapping offscreen render target.\n");
        glReadBuffer(device->offscreenBuffer);
        srcIsUpsideDown = TRUE;
    }
    else
    {
        /* Onscreen surfaces are always part of a swapchain */
        GLenum buffer = surface_get_gl_buffer(This);
        TRACE("Mapping %#x buffer.\n", buffer);
        glReadBuffer(buffer);
        checkGLcall("glReadBuffer");
        srcIsUpsideDown = FALSE;
    }

    /* TODO: Get rid of the extra rectangle comparison and construction of a full surface rectangle */
    if(!rect) {
        local_rect.left = 0;
        local_rect.top = 0;
        local_rect.right = This->currentDesc.Width;
        local_rect.bottom = This->currentDesc.Height;
    } else {
        local_rect = *rect;
    }
    /* TODO: Get rid of the extra GetPitch call, LockRect does that too. Cache the pitch */

    switch (This->resource.format->id)
    {
        case WINED3DFMT_P8_UINT:
        {
            if (primary_render_target_is_p8(device))
            {
                /* In case of P8 render targets the index is stored in the alpha component */
                fmt = GL_ALPHA;
                type = GL_UNSIGNED_BYTE;
                mem = dest;
                bpp = This->resource.format->byte_count;
            } else {
                /* GL can't return palettized data, so read ARGB pixels into a
                 * separate block of memory and convert them into palettized format
                 * in software. Slow, but if the app means to use palettized render
                 * targets and locks it...
                 *
                 * Use GL_RGB, GL_UNSIGNED_BYTE to read the surface for performance reasons
                 * Don't use GL_BGR as in the WINED3DFMT_R8G8B8 case, instead watch out
                 * for the color channels when palettizing the colors.
                 */
                fmt = GL_RGB;
                type = GL_UNSIGNED_BYTE;
                pitch *= 3;
                mem = HeapAlloc(GetProcessHeap(), 0, This->resource.size * 3);
                if(!mem) {
                    ERR("Out of memory\n");
                    LEAVE_GL();
                    return;
                }
                bpp = This->resource.format->byte_count * 3;
            }
        }
        break;

        default:
            mem = dest;
            fmt = This->resource.format->glFormat;
            type = This->resource.format->glType;
            bpp = This->resource.format->byte_count;
    }

    if (This->flags & SFLAG_PBO)
    {
        GL_EXTCALL(glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, This->pbo));
        checkGLcall("glBindBufferARB");
        if (mem)
        {
            ERR("mem not null for pbo -- unexpected\n");
            mem = NULL;
        }
    }

    /* Save old pixel store pack state */
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLen);
    checkGLcall("glGetIntegerv");
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPix);
    checkGLcall("glGetIntegerv");
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRow);
    checkGLcall("glGetIntegerv");

    /* Setup pixel store pack state -- to glReadPixels into the correct place */
    glPixelStorei(GL_PACK_ROW_LENGTH, This->currentDesc.Width);
    checkGLcall("glPixelStorei");
    glPixelStorei(GL_PACK_SKIP_PIXELS, local_rect.left);
    checkGLcall("glPixelStorei");
    glPixelStorei(GL_PACK_SKIP_ROWS, local_rect.top);
    checkGLcall("glPixelStorei");

    glReadPixels(local_rect.left, (!srcIsUpsideDown) ? (This->currentDesc.Height - local_rect.bottom) : local_rect.top ,
                 local_rect.right - local_rect.left,
                 local_rect.bottom - local_rect.top,
                 fmt, type, mem);
    checkGLcall("glReadPixels");

    /* Reset previous pixel store pack state */
    glPixelStorei(GL_PACK_ROW_LENGTH, rowLen);
    checkGLcall("glPixelStorei");
    glPixelStorei(GL_PACK_SKIP_PIXELS, skipPix);
    checkGLcall("glPixelStorei");
    glPixelStorei(GL_PACK_SKIP_ROWS, skipRow);
    checkGLcall("glPixelStorei");

    if (This->flags & SFLAG_PBO)
    {
        GL_EXTCALL(glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, 0));
        checkGLcall("glBindBufferARB");

        /* Check if we need to flip the image. If we need to flip use glMapBufferARB
         * to get a pointer to it and perform the flipping in software. This is a lot
         * faster than calling glReadPixels for each line. In case we want more speed
         * we should rerender it flipped in a FBO and read the data back from the FBO. */
        if(!srcIsUpsideDown) {
            GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, This->pbo));
            checkGLcall("glBindBufferARB");

            mem = GL_EXTCALL(glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, GL_READ_WRITE_ARB));
            checkGLcall("glMapBufferARB");
        }
    }

    /* TODO: Merge this with the palettization loop below for P8 targets */
    if(!srcIsUpsideDown) {
        UINT len, off;
        /* glReadPixels returns the image upside down, and there is no way to prevent this.
            Flip the lines in software */
        len = (local_rect.right - local_rect.left) * bpp;
        off = local_rect.left * bpp;

        row = HeapAlloc(GetProcessHeap(), 0, len);
        if(!row) {
            ERR("Out of memory\n");
            if (This->resource.format->id == WINED3DFMT_P8_UINT) HeapFree(GetProcessHeap(), 0, mem);
            LEAVE_GL();
            return;
        }

        top = mem + pitch * local_rect.top;
        bottom = mem + pitch * (local_rect.bottom - 1);
        for(i = 0; i < (local_rect.bottom - local_rect.top) / 2; i++) {
            memcpy(row, top + off, len);
            memcpy(top + off, bottom + off, len);
            memcpy(bottom + off, row, len);
            top += pitch;
            bottom -= pitch;
        }
        HeapFree(GetProcessHeap(), 0, row);

        /* Unmap the temp PBO buffer */
        if (This->flags & SFLAG_PBO)
        {
            GL_EXTCALL(glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB));
            GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0));
        }
    }

    LEAVE_GL();
    context_release(context);

    /* For P8 textures we need to perform an inverse palette lookup. This is done by searching for a palette
     * index which matches the RGB value. Note this isn't guaranteed to work when there are multiple entries for
     * the same color but we have no choice.
     * In case of P8 render targets, the index is stored in the alpha component so no conversion is needed.
     */
    if (This->resource.format->id == WINED3DFMT_P8_UINT && !primary_render_target_is_p8(device))
    {
        const PALETTEENTRY *pal = NULL;
        DWORD width = pitch / 3;
        int x, y, c;

        if(This->palette) {
            pal = This->palette->palents;
        } else {
            ERR("Palette is missing, cannot perform inverse palette lookup\n");
            HeapFree(GetProcessHeap(), 0, mem);
            return ;
        }

        for(y = local_rect.top; y < local_rect.bottom; y++) {
            for(x = local_rect.left; x < local_rect.right; x++) {
                /*                      start              lines            pixels      */
                const BYTE *blue = mem + y * pitch + x * (sizeof(BYTE) * 3);
                const BYTE *green = blue  + 1;
                const BYTE *red = green + 1;

                for(c = 0; c < 256; c++) {
                    if(*red   == pal[c].peRed   &&
                       *green == pal[c].peGreen &&
                       *blue  == pal[c].peBlue)
                    {
                        *((BYTE *) dest + y * width + x) = c;
                        break;
                    }
                }
            }
        }
        HeapFree(GetProcessHeap(), 0, mem);
    }
}

/* Read the framebuffer contents into a texture */
static void read_from_framebuffer_texture(IWineD3DSurfaceImpl *This, BOOL srgb)
{
    IWineD3DDeviceImpl *device = This->resource.device;
    const struct wined3d_gl_info *gl_info;
    struct wined3d_context *context;

    if (!surface_is_offscreen(This))
    {
        /* We would need to flip onscreen surfaces, but there's no efficient
         * way to do that here. It makes more sense for the caller to
         * explicitly go through sysmem. */
        ERR("Not supported for onscreen targets.\n");
        return;
    }

    /* Activate the surface to read from. In some situations it isn't the currently active target(e.g. backbuffer
     * locking during offscreen rendering). RESOURCELOAD is ok because glCopyTexSubImage2D isn't affected by any
     * states in the stateblock, and no driver was found yet that had bugs in that regard.
     */
    context = context_acquire(device, This);
    gl_info = context->gl_info;

    surface_prepare_texture(This, gl_info, srgb);
    surface_bind_and_dirtify(This, srgb);

    TRACE("Reading back offscreen render target %p.\n", This);

    ENTER_GL();

    glReadBuffer(device->offscreenBuffer);
    checkGLcall("glReadBuffer");

    glCopyTexSubImage2D(This->texture_target, This->texture_level,
            0, 0, 0, 0, This->currentDesc.Width, This->currentDesc.Height);
    checkGLcall("glCopyTexSubImage2D");

    LEAVE_GL();

    context_release(context);
}

/* Context activation is done by the caller. */
static void surface_prepare_texture_internal(IWineD3DSurfaceImpl *surface,
        const struct wined3d_gl_info *gl_info, BOOL srgb)
{
    DWORD alloc_flag = srgb ? SFLAG_SRGBALLOCATED : SFLAG_ALLOCATED;
    CONVERT_TYPES convert;
    struct wined3d_format format;

    if (surface->flags & alloc_flag) return;

    d3dfmt_get_conv(surface, TRUE, TRUE, &format, &convert);
    if (convert != NO_CONVERSION || format.convert) surface->flags |= SFLAG_CONVERTED;
    else surface->flags &= ~SFLAG_CONVERTED;

    surface_bind_and_dirtify(surface, srgb);
    surface_allocate_surface(surface, gl_info, &format, srgb);
    surface->flags |= alloc_flag;
}

/* Context activation is done by the caller. */
void surface_prepare_texture(IWineD3DSurfaceImpl *surface, const struct wined3d_gl_info *gl_info, BOOL srgb)
{
    if (surface->container.type == WINED3D_CONTAINER_TEXTURE)
    {
        IWineD3DBaseTextureImpl *texture = surface->container.u.texture;
        UINT sub_count = texture->baseTexture.level_count * texture->baseTexture.layer_count;
        UINT i;

        TRACE("surface %p is a subresource of texture %p.\n", surface, texture);

        for (i = 0; i < sub_count; ++i)
        {
            IWineD3DSurfaceImpl *s = (IWineD3DSurfaceImpl *)texture->baseTexture.sub_resources[i];
            surface_prepare_texture_internal(s, gl_info, srgb);
        }

        return;
    }

    surface_prepare_texture_internal(surface, gl_info, srgb);
}

static void surface_prepare_system_memory(IWineD3DSurfaceImpl *This)
{
    IWineD3DDeviceImpl *device = This->resource.device;
    const struct wined3d_gl_info *gl_info = &device->adapter->gl_info;

    /* Performance optimization: Count how often a surface is locked, if it is locked regularly do not throw away the system memory copy.
     * This avoids the need to download the surface from opengl all the time. The surface is still downloaded if the opengl texture is
     * changed
     */
    if (!(This->flags & SFLAG_DYNLOCK))
    {
        This->lockCount++;
        /* MAXLOCKCOUNT is defined in wined3d_private.h */
        if(This->lockCount > MAXLOCKCOUNT) {
            TRACE("Surface is locked regularly, not freeing the system memory copy any more\n");
            This->flags |= SFLAG_DYNLOCK;
        }
    }

    /* Create a PBO for dynamically locked surfaces but don't do it for converted or non-pow2 surfaces.
     * Also don't create a PBO for systemmem surfaces.
     */
    if (gl_info->supported[ARB_PIXEL_BUFFER_OBJECT] && (This->flags & SFLAG_DYNLOCK)
            && !(This->flags & (SFLAG_PBO | SFLAG_CONVERTED | SFLAG_NONPOW2))
            && (This->resource.pool != WINED3DPOOL_SYSTEMMEM))
    {
        GLenum error;
        struct wined3d_context *context;

        context = context_acquire(device, NULL);
        ENTER_GL();

        GL_EXTCALL(glGenBuffersARB(1, &This->pbo));
        error = glGetError();
        if (!This->pbo || error != GL_NO_ERROR)
            ERR("Failed to bind the PBO with error %s (%#x)\n", debug_glerror(error), error);

        TRACE("Attaching pbo=%#x to (%p)\n", This->pbo, This);

        GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, This->pbo));
        checkGLcall("glBindBufferARB");

        GL_EXTCALL(glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, This->resource.size + 4, This->resource.allocatedMemory, GL_STREAM_DRAW_ARB));
        checkGLcall("glBufferDataARB");

        GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0));
        checkGLcall("glBindBufferARB");

        /* We don't need the system memory anymore and we can't even use it for PBOs */
        if (!(This->flags & SFLAG_CLIENT))
        {
            HeapFree(GetProcessHeap(), 0, This->resource.heapMemory);
            This->resource.heapMemory = NULL;
        }
        This->resource.allocatedMemory = NULL;
        This->flags |= SFLAG_PBO;
        LEAVE_GL();
        context_release(context);
    }
    else if (!(This->resource.allocatedMemory || This->flags & SFLAG_PBO))
    {
        /* Whatever surface we have, make sure that there is memory allocated for the downloaded copy,
         * or a pbo to map
         */
        if(!This->resource.heapMemory) {
            This->resource.heapMemory = HeapAlloc(GetProcessHeap() ,0 , This->resource.size + RESOURCE_ALIGNMENT);
        }
        This->resource.allocatedMemory =
                (BYTE *)(((ULONG_PTR) This->resource.heapMemory + (RESOURCE_ALIGNMENT - 1)) & ~(RESOURCE_ALIGNMENT - 1));
        if (This->flags & SFLAG_INSYSMEM)
        {
            ERR("Surface without memory or pbo has SFLAG_INSYSMEM set!\n");
        }
    }
}

static HRESULT WINAPI IWineD3DSurfaceImpl_Map(IWineD3DSurface *iface,
        WINED3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;
    IWineD3DDeviceImpl *device = This->resource.device;
    const RECT *pass_rect = pRect;

    TRACE("iface %p, locked_rect %p, rect %s, flags %#x.\n",
            iface, pLockedRect, wine_dbgstr_rect(pRect), Flags);

    /* This is also done in the base class, but we have to verify this before loading any data from
     * gl into the sysmem copy. The PBO may be mapped, a different rectangle locked, the discard flag
     * may interfere, and all other bad things may happen
     */
    if (This->flags & SFLAG_LOCKED)
    {
        WARN("Surface is already locked, returning D3DERR_INVALIDCALL\n");
        return WINED3DERR_INVALIDCALL;
    }
    This->flags |= SFLAG_LOCKED;

    if (!(This->flags & SFLAG_LOCKABLE))
    {
        TRACE("Warning: trying to lock unlockable surf@%p\n", This);
    }

    if (Flags & WINED3DLOCK_DISCARD)
    {
        TRACE("WINED3DLOCK_DISCARD flag passed, marking SYSMEM as up to date.\n");
        surface_prepare_system_memory(This);
        surface_modify_location(This, SFLAG_INSYSMEM, TRUE);
        goto lock_end;
    }

    /* surface_load_location() does not check if the rectangle specifies
     * the full surface. Most callers don't need that, so do it here. */
    if (pRect && !pRect->top && !pRect->left
            && pRect->right == This->currentDesc.Width
            && pRect->bottom == This->currentDesc.Height)
    {
        pass_rect = NULL;
    }

    if (!(wined3d_settings.rendertargetlock_mode == RTL_DISABLE
            && ((This->container.type == WINED3D_CONTAINER_SWAPCHAIN) || This == device->render_targets[0])))
    {
        surface_load_location(This, SFLAG_INSYSMEM, pass_rect);
    }

lock_end:
    if (This->flags & SFLAG_PBO)
    {
        const struct wined3d_gl_info *gl_info;
        struct wined3d_context *context;

        context = context_acquire(device, NULL);
        gl_info = context->gl_info;

        ENTER_GL();
        GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, This->pbo));
        checkGLcall("glBindBufferARB");

        /* This shouldn't happen but could occur if some other function didn't handle the PBO properly */
        if(This->resource.allocatedMemory) {
            ERR("The surface already has PBO memory allocated!\n");
        }

        This->resource.allocatedMemory = GL_EXTCALL(glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, GL_READ_WRITE_ARB));
        checkGLcall("glMapBufferARB");

        /* Make sure the pbo isn't set anymore in order not to break non-pbo calls */
        GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0));
        checkGLcall("glBindBufferARB");

        LEAVE_GL();
        context_release(context);
    }

    if (Flags & (WINED3DLOCK_NO_DIRTY_UPDATE | WINED3DLOCK_READONLY)) {
        /* Don't dirtify */
    }
    else
    {
        surface_add_dirty_rect(This, pRect);

        if (This->container.type == WINED3D_CONTAINER_TEXTURE)
        {
            TRACE("Making container dirty.\n");
            IWineD3DBaseTexture_SetDirty((IWineD3DBaseTexture *)This->container.u.texture, TRUE);
        }
        else
        {
            TRACE("Surface is standalone, no need to dirty the container\n");
        }
    }

    return IWineD3DBaseSurfaceImpl_Map(iface, pLockedRect, pRect, Flags);
}

static void flush_to_framebuffer_drawpixels(IWineD3DSurfaceImpl *This,
        GLenum fmt, GLenum type, UINT bpp, const BYTE *mem)
{
    UINT pitch = IWineD3DSurface_GetPitch((IWineD3DSurface *) This);    /* target is argb, 4 byte */
    IWineD3DDeviceImpl *device = This->resource.device;
    const struct wined3d_gl_info *gl_info;
    struct wined3d_context *context;
    RECT rect;
    UINT w, h;

    if (This->flags & SFLAG_LOCKED)
        rect = This->lockedRect;
    else
        SetRect(&rect, 0, 0, This->currentDesc.Width, This->currentDesc.Height);

    mem += rect.top * pitch + rect.left * bpp;
    w = rect.right - rect.left;
    h = rect.bottom - rect.top;

    /* Activate the correct context for the render target */
    context = context_acquire(device, This);
    context_apply_blit_state(context, device);
    gl_info = context->gl_info;

    ENTER_GL();

    if (!surface_is_offscreen(This))
    {
        GLenum buffer = surface_get_gl_buffer(This);
        TRACE("Unlocking %#x buffer.\n", buffer);
        context_set_draw_buffer(context, buffer);

        surface_translate_drawable_coords(This, context->win_handle, &rect);
        glPixelZoom(1.0f, -1.0f);
    }
    else
    {
        /* Primary offscreen render target */
        TRACE("Offscreen render target.\n");
        context_set_draw_buffer(context, device->offscreenBuffer);

        glPixelZoom(1.0f, 1.0f);
    }

    glRasterPos3i(rect.left, rect.top, 1);
    checkGLcall("glRasterPos3i");

    /* Some drivers(radeon dri, others?) don't like exceptions during
     * glDrawPixels. If the surface is a DIB section, it might be in GDIMode
     * after ReleaseDC. Reading it will cause an exception, which x11drv will
     * catch to put the dib section in InSync mode, which leads to a crash
     * and a blocked x server on my radeon card.
     *
     * The following lines read the dib section so it is put in InSync mode
     * before glDrawPixels is called and the crash is prevented. There won't
     * be any interfering gdi accesses, because UnlockRect is called from
     * ReleaseDC, and the app won't use the dc any more afterwards.
     */
    if ((This->flags & SFLAG_DIBSECTION) && !(This->flags & SFLAG_PBO))
    {
        volatile BYTE read;
        read = This->resource.allocatedMemory[0];
    }

    /* If not fullscreen, we need to skip a number of bytes to find the next row of data */
    glPixelStorei(GL_UNPACK_ROW_LENGTH, This->currentDesc.Width);

    if (This->flags & SFLAG_PBO)
    {
        GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, This->pbo));
        checkGLcall("glBindBufferARB");
    }

    glDrawPixels(w, h, fmt, type, mem);
    checkGLcall("glDrawPixels");

    if (This->flags & SFLAG_PBO)
    {
        GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0));
        checkGLcall("glBindBufferARB");
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    checkGLcall("glPixelStorei(GL_UNPACK_ROW_LENGTH, 0)");

    LEAVE_GL();
    context_release(context);
}

static HRESULT WINAPI IWineD3DSurfaceImpl_Unmap(IWineD3DSurface *iface)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;
    IWineD3DDeviceImpl *device = This->resource.device;
    BOOL fullsurface;

    if (!(This->flags & SFLAG_LOCKED))
    {
        WARN("trying to Unlock an unlocked surf@%p\n", This);
        return WINEDDERR_NOTLOCKED;
    }

    if (This->flags & SFLAG_PBO)
    {
        const struct wined3d_gl_info *gl_info;
        struct wined3d_context *context;

        TRACE("Freeing PBO memory\n");

        context = context_acquire(device, NULL);
        gl_info = context->gl_info;

        ENTER_GL();
        GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, This->pbo));
        GL_EXTCALL(glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB));
        GL_EXTCALL(glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0));
        checkGLcall("glUnmapBufferARB");
        LEAVE_GL();
        context_release(context);

        This->resource.allocatedMemory = NULL;
    }

    TRACE("(%p) : dirtyfied(%d)\n", This, This->flags & (SFLAG_INDRAWABLE | SFLAG_INTEXTURE) ? 0 : 1);

    if (This->flags & (SFLAG_INDRAWABLE | SFLAG_INTEXTURE))
    {
        TRACE("(%p) : Not Dirtified so nothing to do, return now\n", This);
        goto unlock_end;
    }

    if (This->container.type == WINED3D_CONTAINER_SWAPCHAIN
            || (device->render_targets && This == device->render_targets[0]))
    {
        if(wined3d_settings.rendertargetlock_mode == RTL_DISABLE) {
            static BOOL warned = FALSE;
            if(!warned) {
                ERR("The application tries to write to the render target, but render target locking is disabled\n");
                warned = TRUE;
            }
            goto unlock_end;
        }

        if (!This->dirtyRect.left && !This->dirtyRect.top
                && This->dirtyRect.right == This->currentDesc.Width
                && This->dirtyRect.bottom == This->currentDesc.Height)
        {
            fullsurface = TRUE;
        } else {
            /* TODO: Proper partial rectangle tracking */
            fullsurface = FALSE;
            This->flags |= SFLAG_INSYSMEM;
        }

        surface_load_location(This, SFLAG_INDRAWABLE, fullsurface ? NULL : &This->dirtyRect);

        if (!fullsurface)
        {
            /* Partial rectangle tracking is not commonly implemented, it is
             * only done for render targets. Overwrite the flags to bring
             * them back into a sane state. INSYSMEM was set before to tell
             * surface_load_location() where to read the rectangle from.
             * Indrawable is set because all modifications from the partial
             * sysmem copy are written back to the drawable, thus the surface
             * is merged again in the drawable. The sysmem copy is not fully
             * up to date because only a subrectangle was read in Map(). */
            This->flags &= ~SFLAG_INSYSMEM;
            This->flags |= SFLAG_INDRAWABLE;
        }

        This->dirtyRect.left   = This->currentDesc.Width;
        This->dirtyRect.top    = This->currentDesc.Height;
        This->dirtyRect.right  = 0;
        This->dirtyRect.bottom = 0;
    }
    else if (This->resource.format->flags & (WINED3DFMT_FLAG_DEPTH | WINED3DFMT_FLAG_STENCIL))
    {
        FIXME("Depth Stencil buffer locking is not implemented\n");
    }

    unlock_end:
    This->flags &= ~SFLAG_LOCKED;
    memset(&This->lockedRect, 0, sizeof(RECT));

    /* Overlays have to be redrawn manually after changes with the GL implementation */
    if(This->overlay_dest) {
        IWineD3DSurface_DrawOverlay(iface);
    }
    return WINED3D_OK;
}

static void surface_release_client_storage(IWineD3DSurfaceImpl *surface)
{
    struct wined3d_context *context;

    context = context_acquire(surface->resource.device, NULL);

    ENTER_GL();
    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
    if (surface->texture_name)
    {
        surface_bind_and_dirtify(surface, FALSE);
        glTexImage2D(surface->texture_target, surface->texture_level,
                GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    }
    if (surface->texture_name_srgb)
    {
        surface_bind_and_dirtify(surface, TRUE);
        glTexImage2D(surface->texture_target, surface->texture_level,
                GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    }
    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);

    LEAVE_GL();
    context_release(context);

    surface_modify_location(surface, SFLAG_INSRGBTEX, FALSE);
    surface_modify_location(surface, SFLAG_INTEXTURE, FALSE);
    surface_force_reload(surface);
}

static HRESULT WINAPI IWineD3DSurfaceImpl_GetDC(IWineD3DSurface *iface, HDC *pHDC)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;
    WINED3DLOCKED_RECT lock;
    HRESULT hr;
    RGBQUAD col[256];

    TRACE("(%p)->(%p)\n",This,pHDC);

    if (This->flags & SFLAG_USERPTR)
    {
        ERR("Not supported on surfaces with an application-provided surfaces\n");
        return WINEDDERR_NODC;
    }

    /* Give more detailed info for ddraw */
    if (This->flags & SFLAG_DCINUSE)
        return WINEDDERR_DCALREADYCREATED;

    /* Can't GetDC if the surface is locked */
    if (This->flags & SFLAG_LOCKED)
        return WINED3DERR_INVALIDCALL;

    memset(&lock, 0, sizeof(lock)); /* To be sure */

    /* Create a DIB section if there isn't a hdc yet */
    if (!This->hDC)
    {
        if (This->flags & SFLAG_CLIENT)
        {
            surface_load_location(This, SFLAG_INSYSMEM, NULL);
            surface_release_client_storage(This);
        }
        hr = IWineD3DBaseSurfaceImpl_CreateDIBSection(iface);
        if(FAILED(hr)) return WINED3DERR_INVALIDCALL;

        /* Use the dib section from now on if we are not using a PBO */
        if (!(This->flags & SFLAG_PBO))
            This->resource.allocatedMemory = This->dib.bitmap_data;
    }

    /* Map the surface */
    hr = IWineD3DSurface_Map(iface, &lock, NULL, 0);

    /* Sync the DIB with the PBO. This can't be done earlier because Map()
     * activates the allocatedMemory. */
    if (This->flags & SFLAG_PBO)
        memcpy(This->dib.bitmap_data, This->resource.allocatedMemory, This->dib.bitmap_size);

    if (FAILED(hr))
    {
        ERR("IWineD3DSurface_Map failed, hr %#x.\n", hr);
        /* keep the dib section */
        return hr;
    }

    if (This->resource.format->id == WINED3DFMT_P8_UINT
            || This->resource.format->id == WINED3DFMT_P8_UINT_A8_UNORM)
    {
        /* GetDC on palettized formats is unsupported in D3D9, and the method is missing in
            D3D8, so this should only be used for DX <=7 surfaces (with non-device palettes) */
        unsigned int n;
        const PALETTEENTRY *pal = NULL;

        if(This->palette) {
            pal = This->palette->palents;
        } else {
            IWineD3DSurfaceImpl *dds_primary;
            IWineD3DSwapChainImpl *swapchain;
            swapchain = (IWineD3DSwapChainImpl *)This->resource.device->swapchains[0];
            dds_primary = swapchain->front_buffer;
            if (dds_primary && dds_primary->palette)
                pal = dds_primary->palette->palents;
        }

        if (pal) {
            for (n=0; n<256; n++) {
                col[n].rgbRed   = pal[n].peRed;
                col[n].rgbGreen = pal[n].peGreen;
                col[n].rgbBlue  = pal[n].peBlue;
                col[n].rgbReserved = 0;
            }
            SetDIBColorTable(This->hDC, 0, 256, col);
        }
    }

    *pHDC = This->hDC;
    TRACE("returning %p\n",*pHDC);
    This->flags |= SFLAG_DCINUSE;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DSurfaceImpl_ReleaseDC(IWineD3DSurface *iface, HDC hDC)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;

    TRACE("(%p)->(%p)\n",This,hDC);

    if (!(This->flags & SFLAG_DCINUSE))
        return WINEDDERR_NODC;

    if (This->hDC !=hDC) {
        WARN("Application tries to release an invalid DC(%p), surface dc is %p\n", hDC, This->hDC);
        return WINEDDERR_NODC;
    }

    if ((This->flags & SFLAG_PBO) && This->resource.allocatedMemory)
    {
        /* Copy the contents of the DIB over to the PBO */
        memcpy(This->resource.allocatedMemory, This->dib.bitmap_data, This->dib.bitmap_size);
    }

    /* we locked first, so unlock now */
    IWineD3DSurface_Unmap(iface);

    This->flags &= ~SFLAG_DCINUSE;

    return WINED3D_OK;
}

/* ******************************************************
   IWineD3DSurface Internal (No mapping to directx api) parts follow
   ****************************************************** */

HRESULT d3dfmt_get_conv(IWineD3DSurfaceImpl *This, BOOL need_alpha_ck,
        BOOL use_texturing, struct wined3d_format *format, CONVERT_TYPES *convert)
{
    BOOL colorkey_active = need_alpha_ck && (This->CKeyFlags & WINEDDSD_CKSRCBLT);
    IWineD3DDeviceImpl *device = This->resource.device;
    const struct wined3d_gl_info *gl_info = &device->adapter->gl_info;
    BOOL blit_supported = FALSE;

    /* Copy the default values from the surface. Below we might perform fixups */
    /* TODO: get rid of color keying desc fixups by using e.g. a table. */
    *format = *This->resource.format;
    *convert = NO_CONVERSION;

    /* Ok, now look if we have to do any conversion */
    switch (This->resource.format->id)
    {
        case WINED3DFMT_P8_UINT:
            /* Below the call to blit_supported is disabled for Wine 1.2
             * because the function isn't operating correctly yet. At the
             * moment 8-bit blits are handled in software and if certain GL
             * extensions are around, surface conversion is performed at
             * upload time. The blit_supported call recognizes it as a
             * destination fixup. This type of upload 'fixup' and 8-bit to
             * 8-bit blits need to be handled by the blit_shader.
             * TODO: get rid of this #if 0. */
#if 0
            blit_supported = device->blitter->blit_supported(&device->adapter->gl_info, BLIT_OP_BLIT,
                    &rect, This->resource.usage, This->resource.pool, This->resource.format,
                    &rect, This->resource.usage, This->resource.pool, This->resource.format);
#endif
            blit_supported = gl_info->supported[EXT_PALETTED_TEXTURE] || gl_info->supported[ARB_FRAGMENT_PROGRAM];

            /* Use conversion when the blit_shader backend supports it. It only supports this in case of
             * texturing. Further also use conversion in case of color keying.
             * Paletted textures can be emulated using shaders but only do that for 2D purposes e.g. situations
             * in which the main render target uses p8. Some games like GTA Vice City use P8 for texturing which
             * conflicts with this.
             */
            if (!((blit_supported && device->render_targets && This == device->render_targets[0]))
                    || colorkey_active || !use_texturing)
            {
                format->glFormat = GL_RGBA;
                format->glInternal = GL_RGBA;
                format->glType = GL_UNSIGNED_BYTE;
                format->conv_byte_count = 4;
                if (colorkey_active)
                    *convert = CONVERT_PALETTED_CK;
                else
                    *convert = CONVERT_PALETTED;
            }
            break;

        case WINED3DFMT_B2G3R3_UNORM:
            /* **********************
                GL_UNSIGNED_BYTE_3_3_2
                ********************** */
            if (colorkey_active) {
                /* This texture format will never be used.. So do not care about color keying
                    up until the point in time it will be needed :-) */
                FIXME(" ColorKeying not supported in the RGB 332 format !\n");
            }
            break;

        case WINED3DFMT_B5G6R5_UNORM:
            if (colorkey_active)
            {
                *convert = CONVERT_CK_565;
                format->glFormat = GL_RGBA;
                format->glInternal = GL_RGB5_A1;
                format->glType = GL_UNSIGNED_SHORT_5_5_5_1;
                format->conv_byte_count = 2;
            }
            break;

        case WINED3DFMT_B5G5R5X1_UNORM:
            if (colorkey_active)
            {
                *convert = CONVERT_CK_5551;
                format->glFormat = GL_BGRA;
                format->glInternal = GL_RGB5_A1;
                format->glType = GL_UNSIGNED_SHORT_1_5_5_5_REV;
                format->conv_byte_count = 2;
            }
            break;

        case WINED3DFMT_B8G8R8_UNORM:
            if (colorkey_active)
            {
                *convert = CONVERT_CK_RGB24;
                format->glFormat = GL_RGBA;
                format->glInternal = GL_RGBA8;
                format->glType = GL_UNSIGNED_INT_8_8_8_8;
                format->conv_byte_count = 4;
            }
            break;

        case WINED3DFMT_B8G8R8X8_UNORM:
            if (colorkey_active)
            {
                *convert = CONVERT_RGB32_888;
                format->glFormat = GL_RGBA;
                format->glInternal = GL_RGBA8;
                format->glType = GL_UNSIGNED_INT_8_8_8_8;
                format->conv_byte_count = 4;
            }
            break;

        default:
            break;
    }

    return WINED3D_OK;
}

void d3dfmt_p8_init_palette(IWineD3DSurfaceImpl *This, BYTE table[256][4], BOOL colorkey)
{
    IWineD3DDeviceImpl *device = This->resource.device;
    IWineD3DPaletteImpl *pal = This->palette;
    BOOL index_in_alpha = FALSE;
    unsigned int i;

    /* Old games like StarCraft, C&C, Red Alert and others use P8 render targets.
     * Reading back the RGB output each lockrect (each frame as they lock the whole screen)
     * is slow. Further RGB->P8 conversion is not possible because palettes can have
     * duplicate entries. Store the color key in the unused alpha component to speed the
     * download up and to make conversion unneeded. */
    index_in_alpha = primary_render_target_is_p8(device);

    if (!pal)
    {
        UINT dxVersion = ((IWineD3DImpl *)device->wined3d)->dxVersion;

        /* In DirectDraw the palette is a property of the surface, there are no such things as device palettes. */
        if (dxVersion <= 7)
        {
            ERR("This code should never get entered for DirectDraw!, expect problems\n");
            if (index_in_alpha)
            {
                /* Guarantees that memory representation remains correct after sysmem<->texture transfers even if
                 * there's no palette at this time. */
                for (i = 0; i < 256; i++) table[i][3] = i;
            }
        }
        else
        {
            /* Direct3D >= 8 palette usage style: P8 textures use device palettes, palette entry format is A8R8G8B8,
             * alpha is stored in peFlags and may be used by the app if D3DPTEXTURECAPS_ALPHAPALETTE device
             * capability flag is present (wine does advertise this capability) */
            for (i = 0; i < 256; ++i)
            {
                table[i][0] = device->palettes[device->currentPalette][i].peRed;
                table[i][1] = device->palettes[device->currentPalette][i].peGreen;
                table[i][2] = device->palettes[device->currentPalette][i].peBlue;
                table[i][3] = device->palettes[device->currentPalette][i].peFlags;
            }
        }
    }
    else
    {
        TRACE("Using surface palette %p\n", pal);
        /* Get the surface's palette */
        for (i = 0; i < 256; ++i)
        {
            table[i][0] = pal->palents[i].peRed;
            table[i][1] = pal->palents[i].peGreen;
            table[i][2] = pal->palents[i].peBlue;

            /* When index_in_alpha is set the palette index is stored in the
             * alpha component. In case of a readback we can then read
             * GL_ALPHA. Color keying is handled in BltOverride using a
             * GL_ALPHA_TEST using GL_NOT_EQUAL. In case of index_in_alpha the
             * color key itself is passed to glAlphaFunc in other cases the
             * alpha component of pixels that should be masked away is set to 0. */
            if (index_in_alpha)
            {
                table[i][3] = i;
            }
            else if (colorkey && (i >= This->SrcBltCKey.dwColorSpaceLowValue)
                    && (i <= This->SrcBltCKey.dwColorSpaceHighValue))
            {
                table[i][3] = 0x00;
            }
            else if (pal->flags & WINEDDPCAPS_ALPHA)
            {
                table[i][3] = pal->palents[i].peFlags;
            }
            else
            {
                table[i][3] = 0xFF;
            }
        }
    }
}

static HRESULT d3dfmt_convert_surface(const BYTE *src, BYTE *dst, UINT pitch, UINT width,
        UINT height, UINT outpitch, CONVERT_TYPES convert, IWineD3DSurfaceImpl *This)
{
    const BYTE *source;
    BYTE *dest;
    TRACE("(%p)->(%p),(%d,%d,%d,%d,%p)\n", src, dst, pitch, height, outpitch, convert,This);

    switch (convert) {
        case NO_CONVERSION:
        {
            memcpy(dst, src, pitch * height);
            break;
        }
        case CONVERT_PALETTED:
        case CONVERT_PALETTED_CK:
        {
            BYTE table[256][4];
            unsigned int x, y;

            d3dfmt_p8_init_palette(This, table, (convert == CONVERT_PALETTED_CK));

            for (y = 0; y < height; y++)
            {
                source = src + pitch * y;
                dest = dst + outpitch * y;
                /* This is an 1 bpp format, using the width here is fine */
                for (x = 0; x < width; x++) {
                    BYTE color = *source++;
                    *dest++ = table[color][0];
                    *dest++ = table[color][1];
                    *dest++ = table[color][2];
                    *dest++ = table[color][3];
                }
            }
        }
        break;

        case CONVERT_CK_565:
        {
            /* Converting the 565 format in 5551 packed to emulate color-keying.

              Note : in all these conversion, it would be best to average the averaging
                      pixels to get the color of the pixel that will be color-keyed to
                      prevent 'color bleeding'. This will be done later on if ever it is
                      too visible.

              Note2: Nvidia documents say that their driver does not support alpha + color keying
                     on the same surface and disables color keying in such a case
            */
            unsigned int x, y;
            const WORD *Source;
            WORD *Dest;

            TRACE("Color keyed 565\n");

            for (y = 0; y < height; y++) {
                Source = (const WORD *)(src + y * pitch);
                Dest = (WORD *) (dst + y * outpitch);
                for (x = 0; x < width; x++ ) {
                    WORD color = *Source++;
                    *Dest = ((color & 0xFFC0) | ((color & 0x1F) << 1));
                    if ((color < This->SrcBltCKey.dwColorSpaceLowValue) ||
                        (color > This->SrcBltCKey.dwColorSpaceHighValue)) {
                        *Dest |= 0x0001;
                    }
                    Dest++;
                }
            }
        }
        break;

        case CONVERT_CK_5551:
        {
            /* Converting X1R5G5B5 format to R5G5B5A1 to emulate color-keying. */
            unsigned int x, y;
            const WORD *Source;
            WORD *Dest;
            TRACE("Color keyed 5551\n");
            for (y = 0; y < height; y++) {
                Source = (const WORD *)(src + y * pitch);
                Dest = (WORD *) (dst + y * outpitch);
                for (x = 0; x < width; x++ ) {
                    WORD color = *Source++;
                    *Dest = color;
                    if ((color < This->SrcBltCKey.dwColorSpaceLowValue) ||
                        (color > This->SrcBltCKey.dwColorSpaceHighValue)) {
                        *Dest |= (1 << 15);
                    }
                    else {
                        *Dest &= ~(1 << 15);
                    }
                    Dest++;
                }
            }
        }
        break;

        case CONVERT_CK_RGB24:
        {
            /* Converting R8G8B8 format to R8G8B8A8 with color-keying. */
            unsigned int x, y;
            for (y = 0; y < height; y++)
            {
                source = src + pitch * y;
                dest = dst + outpitch * y;
                for (x = 0; x < width; x++) {
                    DWORD color = ((DWORD)source[0] << 16) + ((DWORD)source[1] << 8) + (DWORD)source[2] ;
                    DWORD dstcolor = color << 8;
                    if ((color < This->SrcBltCKey.dwColorSpaceLowValue) ||
                        (color > This->SrcBltCKey.dwColorSpaceHighValue)) {
                        dstcolor |= 0xff;
                    }
                    *(DWORD*)dest = dstcolor;
                    source += 3;
                    dest += 4;
                }
            }
        }
        break;

        case CONVERT_RGB32_888:
        {
            /* Converting X8R8G8B8 format to R8G8B8A8 with color-keying. */
            unsigned int x, y;
            for (y = 0; y < height; y++)
            {
                source = src + pitch * y;
                dest = dst + outpitch * y;
                for (x = 0; x < width; x++) {
                    DWORD color = 0xffffff & *(const DWORD*)source;
                    DWORD dstcolor = color << 8;
                    if ((color < This->SrcBltCKey.dwColorSpaceLowValue) ||
                        (color > This->SrcBltCKey.dwColorSpaceHighValue)) {
                        dstcolor |= 0xff;
                    }
                    *(DWORD*)dest = dstcolor;
                    source += 4;
                    dest += 4;
                }
            }
        }
        break;

        default:
            ERR("Unsupported conversion type %#x.\n", convert);
    }
    return WINED3D_OK;
}

BOOL palette9_changed(IWineD3DSurfaceImpl *This)
{
    IWineD3DDeviceImpl *device = This->resource.device;

    if (This->palette || (This->resource.format->id != WINED3DFMT_P8_UINT
            && This->resource.format->id != WINED3DFMT_P8_UINT_A8_UNORM))
    {
        /* If a ddraw-style palette is attached assume no d3d9 palette change.
         * Also the palette isn't interesting if the surface format isn't P8 or A8P8
         */
        return FALSE;
    }

    if (This->palette9)
    {
        if (!memcmp(This->palette9, device->palettes[device->currentPalette], sizeof(PALETTEENTRY) * 256))
        {
            return FALSE;
        }
    } else {
        This->palette9 = HeapAlloc(GetProcessHeap(), 0, sizeof(PALETTEENTRY) * 256);
    }
    memcpy(This->palette9, device->palettes[device->currentPalette], sizeof(PALETTEENTRY) * 256);
    return TRUE;
}

static HRESULT WINAPI IWineD3DSurfaceImpl_LoadTexture(IWineD3DSurface *iface, BOOL srgb_mode) {
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;
    DWORD flag = srgb_mode ? SFLAG_INSRGBTEX : SFLAG_INTEXTURE;

    TRACE("iface %p, srgb %#x.\n", iface, srgb_mode);

    if (!(This->flags & flag))
    {
        TRACE("Reloading because surface is dirty\n");
    }
    /* Reload if either the texture and sysmem have different ideas about the
     * color key, or the actual key values changed. */
    else if (!(This->flags & SFLAG_GLCKEY) != !(This->CKeyFlags & WINEDDSD_CKSRCBLT)
            || ((This->CKeyFlags & WINEDDSD_CKSRCBLT)
            && (This->glCKey.dwColorSpaceLowValue != This->SrcBltCKey.dwColorSpaceLowValue
            || This->glCKey.dwColorSpaceHighValue != This->SrcBltCKey.dwColorSpaceHighValue)))
    {
        TRACE("Reloading because of color keying\n");
        /* To perform the color key conversion we need a sysmem copy of
         * the surface. Make sure we have it
         */

        surface_load_location(This, SFLAG_INSYSMEM, NULL);
        /* Make sure the texture is reloaded because of the color key change, this kills performance though :( */
        /* TODO: This is not necessarily needed with hw palettized texture support */
        surface_modify_location(This, SFLAG_INSYSMEM, TRUE);
    } else {
        TRACE("surface is already in texture\n");
        return WINED3D_OK;
    }

    /* Resources are placed in system RAM and do not need to be recreated when a device is lost.
     *  These resources are not bound by device size or format restrictions. Because of this,
     *  these resources cannot be accessed by the Direct3D device nor set as textures or render targets.
     *  However, these resources can always be created, locked, and copied.
     */
    if (This->resource.pool == WINED3DPOOL_SCRATCH )
    {
        FIXME("(%p) Operation not supported for scratch textures\n",This);
        return WINED3DERR_INVALIDCALL;
    }

    surface_load_location(This, flag, NULL /* no partial locking for textures yet */);

    if (!(This->flags & SFLAG_DONOTFREE))
    {
        HeapFree(GetProcessHeap(), 0, This->resource.heapMemory);
        This->resource.allocatedMemory = NULL;
        This->resource.heapMemory = NULL;
        surface_modify_location(This, SFLAG_INSYSMEM, FALSE);
    }

    return WINED3D_OK;
}

/* Context activation is done by the caller. */
static void WINAPI IWineD3DSurfaceImpl_BindTexture(IWineD3DSurface *iface, BOOL srgb)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;

    TRACE("iface %p, srgb %#x.\n", iface, srgb);

    if (This->container.type == WINED3D_CONTAINER_TEXTURE)
    {
        TRACE("Passing to container.\n");
        IWineD3DBaseTexture_BindTexture((IWineD3DBaseTexture *)This->container.u.texture, srgb);
    }
    else
    {
        GLuint *name;

        TRACE("(%p) : Binding surface\n", This);

        name = srgb ? &This->texture_name_srgb : &This->texture_name;

        ENTER_GL();

        if (!This->texture_level)
        {
            if (!*name) {
                glGenTextures(1, name);
                checkGLcall("glGenTextures");
                TRACE("Surface %p given name %d\n", This, *name);

                glBindTexture(This->texture_target, *name);
                checkGLcall("glBindTexture");
                glTexParameteri(This->texture_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                checkGLcall("glTexParameteri(dimension, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE)");
                glTexParameteri(This->texture_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                checkGLcall("glTexParameteri(dimension, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE)");
                glTexParameteri(This->texture_target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                checkGLcall("glTexParameteri(dimension, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE)");
                glTexParameteri(This->texture_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                checkGLcall("glTexParameteri(dimension, GL_TEXTURE_MIN_FILTER, GL_NEAREST)");
                glTexParameteri(This->texture_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                checkGLcall("glTexParameteri(dimension, GL_TEXTURE_MAG_FILTER, GL_NEAREST)");
            }
            /* This is where we should be reducing the amount of GLMemoryUsed */
        } else if (*name) {
            /* Mipmap surfaces should have a base texture container */
            ERR("Mipmap surface has a glTexture bound to it!\n");
        }

        glBindTexture(This->texture_target, *name);
        checkGLcall("glBindTexture");

        LEAVE_GL();
    }
}

static HRESULT WINAPI IWineD3DSurfaceImpl_SetFormat(IWineD3DSurface *iface, enum wined3d_format_id format)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;
    HRESULT hr;

    TRACE("(%p) : Calling base function first\n", This);
    hr = IWineD3DBaseSurfaceImpl_SetFormat(iface, format);
    if (SUCCEEDED(hr))
    {
        This->flags &= ~(SFLAG_ALLOCATED | SFLAG_SRGBALLOCATED);
        TRACE("(%p) : glFormat %d, glFormatInternal %d, glType %d\n", This, This->resource.format->glFormat,
                This->resource.format->glInternal, This->resource.format->glType);
    }
    return hr;
}

static HRESULT WINAPI IWineD3DSurfaceImpl_SetMem(IWineD3DSurface *iface, void *Mem) {
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *) iface;

    TRACE("iface %p, mem %p.\n", iface, Mem);

    if (This->flags & (SFLAG_LOCKED | SFLAG_DCINUSE))
    {
        WARN("Surface is locked or the HDC is in use\n");
        return WINED3DERR_INVALIDCALL;
    }

    if(Mem && Mem != This->resource.allocatedMemory) {
        void *release = NULL;

        /* Do I have to copy the old surface content? */
        if (This->flags & SFLAG_DIBSECTION)
        {
            SelectObject(This->hDC, This->dib.holdbitmap);
            DeleteDC(This->hDC);
            /* Release the DIB section */
            DeleteObject(This->dib.DIBsection);
            This->dib.bitmap_data = NULL;
            This->resource.allocatedMemory = NULL;
            This->hDC = NULL;
            This->flags &= ~SFLAG_DIBSECTION;
        }
        else if (!(This->flags & SFLAG_USERPTR))
        {
            release = This->resource.heapMemory;
            This->resource.heapMemory = NULL;
        }
        This->resource.allocatedMemory = Mem;
        This->flags |= SFLAG_USERPTR;

        /* Now the surface memory is most up do date. Invalidate drawable and texture */
        surface_modify_location(This, SFLAG_INSYSMEM, TRUE);

        /* For client textures opengl has to be notified */
        if (This->flags & SFLAG_CLIENT)
            surface_release_client_storage(This);

        /* Now free the old memory if any */
        HeapFree(GetProcessHeap(), 0, release);
    }
    else if (This->flags & SFLAG_USERPTR)
    {
        /* Map and GetDC will re-create the dib section and allocated memory. */
        This->resource.allocatedMemory = NULL;
        /* HeapMemory should be NULL already */
        if (This->resource.heapMemory)
            ERR("User pointer surface has heap memory allocated.\n");
        This->flags &= ~(SFLAG_USERPTR | SFLAG_INSYSMEM);

        if (This->flags & SFLAG_CLIENT)
            surface_release_client_storage(This);

        surface_prepare_system_memory(This);
        surface_modify_location(This, SFLAG_INSYSMEM, TRUE);
    }
    return WINED3D_OK;
}

void flip_surface(IWineD3DSurfaceImpl *front, IWineD3DSurfaceImpl *back) {

    /* Flip the surface contents */
    /* Flip the DC */
    {
        HDC tmp;
        tmp = front->hDC;
        front->hDC = back->hDC;
        back->hDC = tmp;
    }

    /* Flip the DIBsection */
    {
        HBITMAP tmp;
        BOOL hasDib = front->flags & SFLAG_DIBSECTION;
        tmp = front->dib.DIBsection;
        front->dib.DIBsection = back->dib.DIBsection;
        back->dib.DIBsection = tmp;

        if (back->flags & SFLAG_DIBSECTION) front->flags |= SFLAG_DIBSECTION;
        else front->flags &= ~SFLAG_DIBSECTION;
        if (hasDib) back->flags |= SFLAG_DIBSECTION;
        else back->flags &= ~SFLAG_DIBSECTION;
    }

    /* Flip the surface data */
    {
        void* tmp;

        tmp = front->dib.bitmap_data;
        front->dib.bitmap_data = back->dib.bitmap_data;
        back->dib.bitmap_data = tmp;

        tmp = front->resource.allocatedMemory;
        front->resource.allocatedMemory = back->resource.allocatedMemory;
        back->resource.allocatedMemory = tmp;

        tmp = front->resource.heapMemory;
        front->resource.heapMemory = back->resource.heapMemory;
        back->resource.heapMemory = tmp;
    }

    /* Flip the PBO */
    {
        GLuint tmp_pbo = front->pbo;
        front->pbo = back->pbo;
        back->pbo = tmp_pbo;
    }

    /* client_memory should not be different, but just in case */
    {
        BOOL tmp;
        tmp = front->dib.client_memory;
        front->dib.client_memory = back->dib.client_memory;
        back->dib.client_memory = tmp;
    }

    /* Flip the opengl texture */
    {
        GLuint tmp;

        tmp = back->texture_name;
        back->texture_name = front->texture_name;
        front->texture_name = tmp;

        tmp = back->texture_name_srgb;
        back->texture_name_srgb = front->texture_name_srgb;
        front->texture_name_srgb = tmp;

        resource_unload((IWineD3DResourceImpl *)back);
        resource_unload((IWineD3DResourceImpl *)front);
    }

    {
        DWORD tmp_flags = back->flags;
        back->flags = front->flags;
        front->flags = tmp_flags;
    }
}

static HRESULT WINAPI IWineD3DSurfaceImpl_Flip(IWineD3DSurface *iface, IWineD3DSurface *override, DWORD Flags) {
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;
    IWineD3DSwapChainImpl *swapchain = NULL;

    TRACE("(%p)->(%p,%x)\n", This, override, Flags);

    /* Flipping is only supported on RenderTargets and overlays*/
    if( !(This->resource.usage & (WINED3DUSAGE_RENDERTARGET | WINED3DUSAGE_OVERLAY)) ) {
        WARN("Tried to flip a non-render target, non-overlay surface\n");
        return WINEDDERR_NOTFLIPPABLE;
    }

    if(This->resource.usage & WINED3DUSAGE_OVERLAY) {
        flip_surface(This, (IWineD3DSurfaceImpl *) override);

        /* Update the overlay if it is visible */
        if(This->overlay_dest) {
            return IWineD3DSurface_DrawOverlay((IWineD3DSurface *) This);
        } else {
            return WINED3D_OK;
        }
    }

    if(override) {
        /* DDraw sets this for the X11 surfaces, so don't confuse the user
         * FIXME("(%p) Target override is not supported by now\n", This);
         * Additionally, it isn't really possible to support triple-buffering
         * properly on opengl at all
         */
    }

    if (This->container.type != WINED3D_CONTAINER_SWAPCHAIN)
    {
        ERR("Flipped surface is not on a swapchain\n");
        return WINEDDERR_NOTFLIPPABLE;
    }
    swapchain = This->container.u.swapchain;

    /* Just overwrite the swapchain presentation interval. This is ok because only ddraw apps can call Flip,
     * and only d3d8 and d3d9 apps specify the presentation interval
     */
    if (!(Flags & (WINEDDFLIP_NOVSYNC | WINEDDFLIP_INTERVAL2 | WINEDDFLIP_INTERVAL3 | WINEDDFLIP_INTERVAL4)))
    {
        /* Most common case first to avoid wasting time on all the other cases */
        swapchain->presentParms.PresentationInterval = WINED3DPRESENT_INTERVAL_ONE;
    } else if(Flags & WINEDDFLIP_NOVSYNC) {
        swapchain->presentParms.PresentationInterval = WINED3DPRESENT_INTERVAL_IMMEDIATE;
    } else if(Flags & WINEDDFLIP_INTERVAL2) {
        swapchain->presentParms.PresentationInterval = WINED3DPRESENT_INTERVAL_TWO;
    } else if(Flags & WINEDDFLIP_INTERVAL3) {
        swapchain->presentParms.PresentationInterval = WINED3DPRESENT_INTERVAL_THREE;
    } else {
        swapchain->presentParms.PresentationInterval = WINED3DPRESENT_INTERVAL_FOUR;
    }

    /* Flipping a OpenGL surface -> Use WineD3DDevice::Present */
    return IWineD3DSwapChain_Present((IWineD3DSwapChain *)swapchain,
            NULL, NULL, swapchain->win_handle, NULL, 0);
}

/* Does a direct frame buffer -> texture copy. Stretching is done
 * with single pixel copy calls
 */
static void fb_copy_to_texture_direct(IWineD3DSurfaceImpl *dst_surface, IWineD3DSurfaceImpl *src_surface,
        const RECT *src_rect, const RECT *dst_rect_in, WINED3DTEXTUREFILTERTYPE Filter)
{
    IWineD3DDeviceImpl *device = dst_surface->resource.device;
    float xrel, yrel;
    UINT row;
    struct wined3d_context *context;
    BOOL upsidedown = FALSE;
    RECT dst_rect = *dst_rect_in;

    /* Make sure that the top pixel is always above the bottom pixel, and keep a separate upside down flag
     * glCopyTexSubImage is a bit picky about the parameters we pass to it
     */
    if(dst_rect.top > dst_rect.bottom) {
        UINT tmp = dst_rect.bottom;
        dst_rect.bottom = dst_rect.top;
        dst_rect.top = tmp;
        upsidedown = TRUE;
    }

    context = context_acquire(device, src_surface);
    context_apply_blit_state(context, device);
    surface_internal_preload(dst_surface, SRGB_RGB);
    ENTER_GL();

    /* Bind the target texture */
    glBindTexture(dst_surface->texture_target, dst_surface->texture_name);
    checkGLcall("glBindTexture");
    if (surface_is_offscreen(src_surface))
    {
        TRACE("Reading from an offscreen target\n");
        upsidedown = !upsidedown;
        glReadBuffer(device->offscreenBuffer);
    }
    else
    {
        glReadBuffer(surface_get_gl_buffer(src_surface));
    }
    checkGLcall("glReadBuffer");

    xrel = (float) (src_rect->right - src_rect->left) / (float) (dst_rect.right - dst_rect.left);
    yrel = (float) (src_rect->bottom - src_rect->top) / (float) (dst_rect.bottom - dst_rect.top);

    if ((xrel - 1.0f < -eps) || (xrel - 1.0f > eps))
    {
        FIXME("Doing a pixel by pixel copy from the framebuffer to a texture, expect major performance issues\n");

        if(Filter != WINED3DTEXF_NONE && Filter != WINED3DTEXF_POINT) {
            ERR("Texture filtering not supported in direct blit\n");
        }
    }
    else if ((Filter != WINED3DTEXF_NONE && Filter != WINED3DTEXF_POINT)
            && ((yrel - 1.0f < -eps) || (yrel - 1.0f > eps)))
    {
        ERR("Texture filtering not supported in direct blit\n");
    }

    if (upsidedown
            && !((xrel - 1.0f < -eps) || (xrel - 1.0f > eps))
            && !((yrel - 1.0f < -eps) || (yrel - 1.0f > eps)))
    {
        /* Upside down copy without stretching is nice, one glCopyTexSubImage call will do */

        glCopyTexSubImage2D(dst_surface->texture_target, dst_surface->texture_level,
                dst_rect.left /*xoffset */, dst_rect.top /* y offset */,
                src_rect->left, src_surface->currentDesc.Height - src_rect->bottom,
                dst_rect.right - dst_rect.left, dst_rect.bottom - dst_rect.top);
    } else {
        UINT yoffset = src_surface->currentDesc.Height - src_rect->top + dst_rect.top - 1;
        /* I have to process this row by row to swap the image,
         * otherwise it would be upside down, so stretching in y direction
         * doesn't cost extra time
         *
         * However, stretching in x direction can be avoided if not necessary
         */
        for(row = dst_rect.top; row < dst_rect.bottom; row++) {
            if ((xrel - 1.0f < -eps) || (xrel - 1.0f > eps))
            {
                /* Well, that stuff works, but it's very slow.
                 * find a better way instead
                 */
                UINT col;

                for (col = dst_rect.left; col < dst_rect.right; ++col)
                {
                    glCopyTexSubImage2D(dst_surface->texture_target, dst_surface->texture_level,
                            dst_rect.left + col /* x offset */, row /* y offset */,
                            src_rect->left + col * xrel, yoffset - (int) (row * yrel), 1, 1);
                }
            }
            else
            {
                glCopyTexSubImage2D(dst_surface->texture_target, dst_surface->texture_level,
                        dst_rect.left /* x offset */, row /* y offset */,
                        src_rect->left, yoffset - (int) (row * yrel), dst_rect.right - dst_rect.left, 1);
            }
        }
    }
    checkGLcall("glCopyTexSubImage2D");

    LEAVE_GL();
    context_release(context);

    /* The texture is now most up to date - If the surface is a render target and has a drawable, this
     * path is never entered
     */
    surface_modify_location(dst_surface, SFLAG_INTEXTURE, TRUE);
}

/* Uses the hardware to stretch and flip the image */
static void fb_copy_to_texture_hwstretch(IWineD3DSurfaceImpl *dst_surface, IWineD3DSurfaceImpl *src_surface,
        const RECT *src_rect, const RECT *dst_rect_in, WINED3DTEXTUREFILTERTYPE Filter)
{
    IWineD3DDeviceImpl *device = dst_surface->resource.device;
    GLuint src, backup = 0;
    IWineD3DSwapChainImpl *src_swapchain = NULL;
    float left, right, top, bottom; /* Texture coordinates */
    UINT fbwidth = src_surface->currentDesc.Width;
    UINT fbheight = src_surface->currentDesc.Height;
    struct wined3d_context *context;
    GLenum drawBuffer = GL_BACK;
    GLenum texture_target;
    BOOL noBackBufferBackup;
    BOOL src_offscreen;
    BOOL upsidedown = FALSE;
    RECT dst_rect = *dst_rect_in;

    TRACE("Using hwstretch blit\n");
    /* Activate the Proper context for reading from the source surface, set it up for blitting */
    context = context_acquire(device, src_surface);
    context_apply_blit_state(context, device);
    surface_internal_preload(dst_surface, SRGB_RGB);

    src_offscreen = surface_is_offscreen(src_surface);
    noBackBufferBackup = src_offscreen && wined3d_settings.offscreen_rendering_mode == ORM_FBO;
    if (!noBackBufferBackup && !src_surface->texture_name)
    {
        /* Get it a description */
        surface_internal_preload(src_surface, SRGB_RGB);
    }
    ENTER_GL();

    /* Try to use an aux buffer for drawing the rectangle. This way it doesn't need restoring.
     * This way we don't have to wait for the 2nd readback to finish to leave this function.
     */
    if (context->aux_buffers >= 2)
    {
        /* Got more than one aux buffer? Use the 2nd aux buffer */
        drawBuffer = GL_AUX1;
    }
    else if ((!src_offscreen || device->offscreenBuffer == GL_BACK) && context->aux_buffers >= 1)
    {
        /* Only one aux buffer, but it isn't used (Onscreen rendering, or non-aux orm)? Use it! */
        drawBuffer = GL_AUX0;
    }

    if(noBackBufferBackup) {
        glGenTextures(1, &backup);
        checkGLcall("glGenTextures");
        glBindTexture(GL_TEXTURE_2D, backup);
        checkGLcall("glBindTexture(GL_TEXTURE_2D, backup)");
        texture_target = GL_TEXTURE_2D;
    } else {
        /* Backup the back buffer and copy the source buffer into a texture to draw an upside down stretched quad. If
         * we are reading from the back buffer, the backup can be used as source texture
         */
        texture_target = src_surface->texture_target;
        glBindTexture(texture_target, src_surface->texture_name);
        checkGLcall("glBindTexture(texture_target, src_surface->texture_name)");
        glEnable(texture_target);
        checkGLcall("glEnable(texture_target)");

        /* For now invalidate the texture copy of the back buffer. Drawable and sysmem copy are untouched */
        src_surface->flags &= ~SFLAG_INTEXTURE;
    }

    /* Make sure that the top pixel is always above the bottom pixel, and keep a separate upside down flag
     * glCopyTexSubImage is a bit picky about the parameters we pass to it
     */
    if(dst_rect.top > dst_rect.bottom) {
        UINT tmp = dst_rect.bottom;
        dst_rect.bottom = dst_rect.top;
        dst_rect.top = tmp;
        upsidedown = TRUE;
    }

    if (src_offscreen)
    {
        TRACE("Reading from an offscreen target\n");
        upsidedown = !upsidedown;
        glReadBuffer(device->offscreenBuffer);
    }
    else
    {
        glReadBuffer(surface_get_gl_buffer(src_surface));
    }

    /* TODO: Only back up the part that will be overwritten */
    glCopyTexSubImage2D(texture_target, 0,
                        0, 0 /* read offsets */,
                        0, 0,
                        fbwidth,
                        fbheight);

    checkGLcall("glCopyTexSubImage2D");

    /* No issue with overriding these - the sampler is dirty due to blit usage */
    glTexParameteri(texture_target, GL_TEXTURE_MAG_FILTER,
            wined3d_gl_mag_filter(magLookup, Filter));
    checkGLcall("glTexParameteri");
    glTexParameteri(texture_target, GL_TEXTURE_MIN_FILTER,
            wined3d_gl_min_mip_filter(minMipLookup, Filter, WINED3DTEXF_NONE));
    checkGLcall("glTexParameteri");

    if (src_surface->container.type == WINED3D_CONTAINER_SWAPCHAIN)
        src_swapchain = src_surface->container.u.swapchain;
    if (!src_swapchain || src_surface == src_swapchain->back_buffers[0])
    {
        src = backup ? backup : src_surface->texture_name;
    }
    else
    {
        glReadBuffer(GL_FRONT);
        checkGLcall("glReadBuffer(GL_FRONT)");

        glGenTextures(1, &src);
        checkGLcall("glGenTextures(1, &src)");
        glBindTexture(GL_TEXTURE_2D, src);
        checkGLcall("glBindTexture(GL_TEXTURE_2D, src)");

        /* TODO: Only copy the part that will be read. Use src_rect->left, src_rect->bottom as origin, but with the width watch
         * out for power of 2 sizes
         */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, src_surface->pow2Width,
                src_surface->pow2Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        checkGLcall("glTexImage2D");
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0,
                            0, 0 /* read offsets */,
                            0, 0,
                            fbwidth,
                            fbheight);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        checkGLcall("glTexParameteri");
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        checkGLcall("glTexParameteri");

        glReadBuffer(GL_BACK);
        checkGLcall("glReadBuffer(GL_BACK)");

        if(texture_target != GL_TEXTURE_2D) {
            glDisable(texture_target);
            glEnable(GL_TEXTURE_2D);
            texture_target = GL_TEXTURE_2D;
        }
    }
    checkGLcall("glEnd and previous");

    left = src_rect->left;
    right = src_rect->right;

    if (!upsidedown)
    {
        top = src_surface->currentDesc.Height - src_rect->top;
        bottom = src_surface->currentDesc.Height - src_rect->bottom;
    }
    else
    {
        top = src_surface->currentDesc.Height - src_rect->bottom;
        bottom = src_surface->currentDesc.Height - src_rect->top;
    }

    if (src_surface->flags & SFLAG_NORMCOORD)
    {
        left /= src_surface->pow2Width;
        right /= src_surface->pow2Width;
        top /= src_surface->pow2Height;
        bottom /= src_surface->pow2Height;
    }

    /* draw the source texture stretched and upside down. The correct surface is bound already */
    glTexParameteri(texture_target, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(texture_target, GL_TEXTURE_WRAP_T, GL_CLAMP);

    context_set_draw_buffer(context, drawBuffer);
    glReadBuffer(drawBuffer);

    glBegin(GL_QUADS);
        /* bottom left */
        glTexCoord2f(left, bottom);
        glVertex2i(0, 0);

        /* top left */
        glTexCoord2f(left, top);
        glVertex2i(0, dst_rect.bottom - dst_rect.top);

        /* top right */
        glTexCoord2f(right, top);
        glVertex2i(dst_rect.right - dst_rect.left, dst_rect.bottom - dst_rect.top);

        /* bottom right */
        glTexCoord2f(right, bottom);
        glVertex2i(dst_rect.right - dst_rect.left, 0);
    glEnd();
    checkGLcall("glEnd and previous");

    if (texture_target != dst_surface->texture_target)
    {
        glDisable(texture_target);
        glEnable(dst_surface->texture_target);
        texture_target = dst_surface->texture_target;
    }

    /* Now read the stretched and upside down image into the destination texture */
    glBindTexture(texture_target, dst_surface->texture_name);
    checkGLcall("glBindTexture");
    glCopyTexSubImage2D(texture_target,
                        0,
                        dst_rect.left, dst_rect.top, /* xoffset, yoffset */
                        0, 0, /* We blitted the image to the origin */
                        dst_rect.right - dst_rect.left, dst_rect.bottom - dst_rect.top);
    checkGLcall("glCopyTexSubImage2D");

    if(drawBuffer == GL_BACK) {
        /* Write the back buffer backup back */
        if(backup) {
            if(texture_target != GL_TEXTURE_2D) {
                glDisable(texture_target);
                glEnable(GL_TEXTURE_2D);
                texture_target = GL_TEXTURE_2D;
            }
            glBindTexture(GL_TEXTURE_2D, backup);
            checkGLcall("glBindTexture(GL_TEXTURE_2D, backup)");
        }
        else
        {
            if (texture_target != src_surface->texture_target)
            {
                glDisable(texture_target);
                glEnable(src_surface->texture_target);
                texture_target = src_surface->texture_target;
            }
            glBindTexture(src_surface->texture_target, src_surface->texture_name);
            checkGLcall("glBindTexture(src_surface->texture_target, src_surface->texture_name)");
        }

        glBegin(GL_QUADS);
            /* top left */
            glTexCoord2f(0.0f, 0.0f);
            glVertex2i(0, fbheight);

            /* bottom left */
            glTexCoord2f(0.0f, (float)fbheight / (float)src_surface->pow2Height);
            glVertex2i(0, 0);

            /* bottom right */
            glTexCoord2f((float)fbwidth / (float)src_surface->pow2Width,
                    (float)fbheight / (float)src_surface->pow2Height);
            glVertex2i(fbwidth, 0);

            /* top right */
            glTexCoord2f((float)fbwidth / (float)src_surface->pow2Width, 0.0f);
            glVertex2i(fbwidth, fbheight);
        glEnd();
    }
    glDisable(texture_target);
    checkGLcall("glDisable(texture_target)");

    /* Cleanup */
    if (src != src_surface->texture_name && src != backup)
    {
        glDeleteTextures(1, &src);
        checkGLcall("glDeleteTextures(1, &src)");
    }
    if(backup) {
        glDeleteTextures(1, &backup);
        checkGLcall("glDeleteTextures(1, &backup)");
    }

    LEAVE_GL();

    if (wined3d_settings.strict_draw_ordering) wglFlush(); /* Flush to ensure ordering across contexts. */

    context_release(context);

    /* The texture is now most up to date - If the surface is a render target and has a drawable, this
     * path is never entered
     */
    surface_modify_location(dst_surface, SFLAG_INTEXTURE, TRUE);
}

/* Until the blit_shader is ready, define some prototypes here. */
static BOOL fbo_blit_supported(const struct wined3d_gl_info *gl_info, enum blit_operation blit_op,
        const RECT *src_rect, DWORD src_usage, WINED3DPOOL src_pool, const struct wined3d_format *src_format,
        const RECT *dst_rect, DWORD dst_usage, WINED3DPOOL dst_pool, const struct wined3d_format *dst_format);

/* Front buffer coordinates are always full screen coordinates, but our GL
 * drawable is limited to the window's client area. The sysmem and texture
 * copies do have the full screen size. Note that GL has a bottom-left
 * origin, while D3D has a top-left origin. */
void surface_translate_drawable_coords(IWineD3DSurfaceImpl *surface, HWND window, RECT *rect)
{
    UINT drawable_height;

    if (surface->container.type == WINED3D_CONTAINER_SWAPCHAIN
            && surface == surface->container.u.swapchain->front_buffer)
    {
        POINT offset = {0, 0};
        RECT windowsize;

        ScreenToClient(window, &offset);
        OffsetRect(rect, offset.x, offset.y);

        GetClientRect(window, &windowsize);
        drawable_height = windowsize.bottom - windowsize.top;
    }
    else
    {
        drawable_height = surface->currentDesc.Height;
    }

    rect->top = drawable_height - rect->top;
    rect->bottom = drawable_height - rect->bottom;
}

static BOOL surface_is_full_rect(IWineD3DSurfaceImpl *surface, const RECT *r)
{
    if ((r->left && r->right) || abs(r->right - r->left) != surface->currentDesc.Width)
        return FALSE;
    if ((r->top && r->bottom) || abs(r->bottom - r->top) != surface->currentDesc.Height)
        return FALSE;
    return TRUE;
}

/* blit between surface locations. onscreen on different swapchains is not supported.
 * depth / stencil is not supported. */
static void surface_blt_fbo(IWineD3DDeviceImpl *device, const WINED3DTEXTUREFILTERTYPE filter,
        IWineD3DSurfaceImpl *src_surface, DWORD src_location, const RECT *src_rect_in,
        IWineD3DSurfaceImpl *dst_surface, DWORD dst_location, const RECT *dst_rect_in)
{
    const struct wined3d_gl_info *gl_info;
    struct wined3d_context *context;
    RECT src_rect, dst_rect;
    GLenum gl_filter;

    TRACE("device %p, filter %s,\n", device, debug_d3dtexturefiltertype(filter));
    TRACE("src_surface %p, src_location %s, src_rect %s,\n",
            src_surface, debug_surflocation(src_location), wine_dbgstr_rect(src_rect_in));
    TRACE("dst_surface %p, dst_location %s, dst_rect %s.\n",
            dst_surface, debug_surflocation(dst_location), wine_dbgstr_rect(dst_rect_in));

    src_rect = *src_rect_in;
    dst_rect = *dst_rect_in;

    switch (filter)
    {
        case WINED3DTEXF_LINEAR:
            gl_filter = GL_LINEAR;
            break;

        default:
            FIXME("Unsupported filter mode %s (%#x).\n", debug_d3dtexturefiltertype(filter), filter);
        case WINED3DTEXF_NONE:
        case WINED3DTEXF_POINT:
            gl_filter = GL_NEAREST;
            break;
    }

    if (src_location == SFLAG_INDRAWABLE && surface_is_offscreen(src_surface))
        src_location = SFLAG_INTEXTURE;
    if (dst_location == SFLAG_INDRAWABLE && surface_is_offscreen(dst_surface))
        dst_location = SFLAG_INTEXTURE;

    /* Make sure the locations are up-to-date. Loading the destination
     * surface isn't required if the entire surface is overwritten. (And is
     * in fact harmful if we're being called by surface_load_location() with
     * the purpose of loading the destination surface.) */
    surface_load_location(src_surface, src_location, NULL);
    if (!surface_is_full_rect(dst_surface, &dst_rect))
        surface_load_location(dst_surface, dst_location, NULL);

    if (src_location == SFLAG_INDRAWABLE) context = context_acquire(device, src_surface);
    else if (dst_location == SFLAG_INDRAWABLE) context = context_acquire(device, dst_surface);
    else context = context_acquire(device, NULL);

    if (!context->valid)
    {
        context_release(context);
        WARN("Invalid context, skipping blit.\n");
        return;
    }

    gl_info = context->gl_info;

    if (src_location == SFLAG_INDRAWABLE)
    {
        GLenum buffer = surface_get_gl_buffer(src_surface);

        TRACE("Source surface %p is onscreen.\n", src_surface);

        surface_translate_drawable_coords(src_surface, context->win_handle, &src_rect);

        ENTER_GL();
        context_bind_fbo(context, GL_READ_FRAMEBUFFER, NULL);
        glReadBuffer(buffer);
        checkGLcall("glReadBuffer()");
    }
    else
    {
        TRACE("Source surface %p is offscreen.\n", src_surface);
        ENTER_GL();
        context_apply_fbo_state_blit(context, GL_READ_FRAMEBUFFER, src_surface, NULL, src_location);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        checkGLcall("glReadBuffer()");
    }
    LEAVE_GL();

    if (dst_location == SFLAG_INDRAWABLE)
    {
        GLenum buffer = surface_get_gl_buffer(dst_surface);

        TRACE("Destination surface %p is onscreen.\n", dst_surface);

        surface_translate_drawable_coords(dst_surface, context->win_handle, &dst_rect);

        ENTER_GL();
        context_bind_fbo(context, GL_DRAW_FRAMEBUFFER, NULL);
        context_set_draw_buffer(context, buffer);
    }
    else
    {
        TRACE("Destination surface %p is offscreen.\n", dst_surface);

        ENTER_GL();
        context_apply_fbo_state_blit(context, GL_DRAW_FRAMEBUFFER, dst_surface, NULL, dst_location);
        context_set_draw_buffer(context, GL_COLOR_ATTACHMENT0);
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    IWineD3DDeviceImpl_MarkStateDirty(device, STATE_RENDER(WINED3DRS_COLORWRITEENABLE));
    IWineD3DDeviceImpl_MarkStateDirty(device, STATE_RENDER(WINED3DRS_COLORWRITEENABLE1));
    IWineD3DDeviceImpl_MarkStateDirty(device, STATE_RENDER(WINED3DRS_COLORWRITEENABLE2));
    IWineD3DDeviceImpl_MarkStateDirty(device, STATE_RENDER(WINED3DRS_COLORWRITEENABLE3));

    glDisable(GL_SCISSOR_TEST);
    IWineD3DDeviceImpl_MarkStateDirty(device, STATE_RENDER(WINED3DRS_SCISSORTESTENABLE));

    gl_info->fbo_ops.glBlitFramebuffer(src_rect.left, src_rect.top, src_rect.right, src_rect.bottom,
            dst_rect.left, dst_rect.top, dst_rect.right, dst_rect.bottom, GL_COLOR_BUFFER_BIT, gl_filter);
    checkGLcall("glBlitFramebuffer()");

    LEAVE_GL();

    if (wined3d_settings.strict_draw_ordering) wglFlush(); /* Flush to ensure ordering across contexts. */

    context_release(context);
}

static void surface_blt_to_drawable(IWineD3DDeviceImpl *device,
        WINED3DTEXTUREFILTERTYPE filter, BOOL color_key,
        IWineD3DSurfaceImpl *src_surface, const RECT *src_rect_in,
        IWineD3DSurfaceImpl *dst_surface, const RECT *dst_rect_in)
{
    IWineD3DSwapChainImpl *swapchain = NULL;
    struct wined3d_context *context;
    RECT src_rect, dst_rect;

    src_rect = *src_rect_in;
    dst_rect = *dst_rect_in;

    if (dst_surface->container.type == WINED3D_CONTAINER_SWAPCHAIN)
        swapchain = dst_surface->container.u.swapchain;

    /* Make sure the surface is up-to-date. This should probably use
     * surface_load_location() and worry about the destination surface too,
     * unless we're overwriting it completely. */
    surface_internal_preload(src_surface, SRGB_RGB);

    /* Activate the destination context, set it up for blitting */
    context = context_acquire(device, dst_surface);
    context_apply_blit_state(context, device);

    if (!surface_is_offscreen(dst_surface))
        surface_translate_drawable_coords(dst_surface, context->win_handle, &dst_rect);

    device->blitter->set_shader((IWineD3DDevice *)device, src_surface);

    ENTER_GL();

    if (color_key)
    {
        glEnable(GL_ALPHA_TEST);
        checkGLcall("glEnable(GL_ALPHA_TEST)");

        /* When the primary render target uses P8, the alpha component
         * contains the palette index. Which means that the colorkey is one of
         * the palette entries. In other cases pixels that should be masked
         * away have alpha set to 0. */
        if (primary_render_target_is_p8(device))
            glAlphaFunc(GL_NOTEQUAL, (float)src_surface->SrcBltCKey.dwColorSpaceLowValue / 256.0f);
        else
            glAlphaFunc(GL_NOTEQUAL, 0.0f);
        checkGLcall("glAlphaFunc");
    }
    else
    {
        glDisable(GL_ALPHA_TEST);
        checkGLcall("glDisable(GL_ALPHA_TEST)");
    }

    draw_textured_quad(src_surface, &src_rect, &dst_rect, filter);

    if (color_key)
    {
        glDisable(GL_ALPHA_TEST);
        checkGLcall("glDisable(GL_ALPHA_TEST)");
    }

    LEAVE_GL();

    /* Leave the opengl state valid for blitting */
    device->blitter->unset_shader((IWineD3DDevice *)device);

    if (wined3d_settings.strict_draw_ordering || (swapchain
            && (dst_surface == swapchain->front_buffer || swapchain->num_contexts > 1)))
        wglFlush(); /* Flush to ensure ordering across contexts. */

    context_release(context);
}

/* Do not call while under the GL lock. */
HRESULT surface_color_fill(IWineD3DSurfaceImpl *s, const RECT *rect, const WINED3DCOLORVALUE *color)
{
    IWineD3DDeviceImpl *device = s->resource.device;
    const struct blit_shader *blitter;

    blitter = wined3d_select_blitter(&device->adapter->gl_info, BLIT_OP_COLOR_FILL,
            NULL, 0, 0, NULL, rect, s->resource.usage, s->resource.pool, s->resource.format);
    if (!blitter)
    {
        FIXME("No blitter is capable of performing the requested color fill operation.\n");
        return WINED3DERR_INVALIDCALL;
    }

    return blitter->color_fill(device, s, rect, color);
}

/* Not called from the VTable */
/* Do not call while under the GL lock. */
static HRESULT IWineD3DSurfaceImpl_BltOverride(IWineD3DSurfaceImpl *dst_surface, const RECT *DestRect,
        IWineD3DSurfaceImpl *src_surface, const RECT *SrcRect, DWORD Flags, const WINEDDBLTFX *DDBltFx,
        WINED3DTEXTUREFILTERTYPE Filter)
{
    IWineD3DDeviceImpl *device = dst_surface->resource.device;
    const struct wined3d_gl_info *gl_info = &device->adapter->gl_info;
    IWineD3DSwapChainImpl *srcSwapchain = NULL, *dstSwapchain = NULL;
    RECT dst_rect, src_rect;

    TRACE("dst_surface %p, dst_rect %s, src_surface %p, src_rect %s, flags %#x, blt_fx %p, filter %s.\n",
            dst_surface, wine_dbgstr_rect(DestRect), src_surface, wine_dbgstr_rect(SrcRect),
            Flags, DDBltFx, debug_d3dtexturefiltertype(Filter));

    /* Get the swapchain. One of the surfaces has to be a primary surface */
    if (dst_surface->resource.pool == WINED3DPOOL_SYSTEMMEM)
    {
        WARN("Destination is in sysmem, rejecting gl blt\n");
        return WINED3DERR_INVALIDCALL;
    }

    if (dst_surface->container.type == WINED3D_CONTAINER_SWAPCHAIN)
        dstSwapchain = dst_surface->container.u.swapchain;

    if (src_surface)
    {
        if (src_surface->resource.pool == WINED3DPOOL_SYSTEMMEM)
        {
            WARN("Src is in sysmem, rejecting gl blt\n");
            return WINED3DERR_INVALIDCALL;
        }

        if (src_surface->container.type == WINED3D_CONTAINER_SWAPCHAIN)
            srcSwapchain = src_surface->container.u.swapchain;
    }

    /* Early sort out of cases where no render target is used */
    if (!dstSwapchain && !srcSwapchain
            && src_surface != device->render_targets[0]
            && dst_surface != device->render_targets[0])
    {
        TRACE("No surface is render target, not using hardware blit.\n");
        return WINED3DERR_INVALIDCALL;
    }

    /* No destination color keying supported */
    if(Flags & (WINEDDBLT_KEYDEST | WINEDDBLT_KEYDESTOVERRIDE)) {
        /* Can we support that with glBlendFunc if blitting to the frame buffer? */
        TRACE("Destination color key not supported in accelerated Blit, falling back to software\n");
        return WINED3DERR_INVALIDCALL;
    }

    surface_get_rect(dst_surface, DestRect, &dst_rect);
    if (src_surface) surface_get_rect(src_surface, SrcRect, &src_rect);

    /* The only case where both surfaces on a swapchain are supported is a back buffer -> front buffer blit on the same swapchain */
    if (dstSwapchain && dstSwapchain == srcSwapchain && dstSwapchain->back_buffers
            && dst_surface == dstSwapchain->front_buffer
            && src_surface == dstSwapchain->back_buffers[0])
    {
        /* Half-Life does a Blt from the back buffer to the front buffer,
         * Full surface size, no flags... Use present instead
         *
         * This path will only be entered for d3d7 and ddraw apps, because d3d8/9 offer no way to blit TO the front buffer
         */

        /* Check rects - IWineD3DDevice_Present doesn't handle them */
        while(1)
        {
            TRACE("Looking if a Present can be done...\n");
            /* Source Rectangle must be full surface */
            if (src_rect.left || src_rect.top
                    || src_rect.right != src_surface->currentDesc.Width
                    || src_rect.bottom != src_surface->currentDesc.Height)
            {
                TRACE("No, Source rectangle doesn't match\n");
                break;
            }

            /* No stretching may occur */
            if(src_rect.right != dst_rect.right - dst_rect.left ||
               src_rect.bottom != dst_rect.bottom - dst_rect.top) {
                TRACE("No, stretching is done\n");
                break;
            }

            /* Destination must be full surface or match the clipping rectangle */
            if (dst_surface->clipper && ((IWineD3DClipperImpl *)dst_surface->clipper)->hWnd)
            {
                RECT cliprect;
                POINT pos[2];
                GetClientRect(((IWineD3DClipperImpl *)dst_surface->clipper)->hWnd, &cliprect);
                pos[0].x = dst_rect.left;
                pos[0].y = dst_rect.top;
                pos[1].x = dst_rect.right;
                pos[1].y = dst_rect.bottom;
                MapWindowPoints(GetDesktopWindow(), ((IWineD3DClipperImpl *)dst_surface->clipper)->hWnd, pos, 2);

                if(pos[0].x != cliprect.left  || pos[0].y != cliprect.top   ||
                   pos[1].x != cliprect.right || pos[1].y != cliprect.bottom)
                {
                    TRACE("No, dest rectangle doesn't match(clipper)\n");
                    TRACE("Clip rect at %s\n", wine_dbgstr_rect(&cliprect));
                    TRACE("Blt dest: %s\n", wine_dbgstr_rect(&dst_rect));
                    break;
                }
            }
            else if (dst_rect.left || dst_rect.top
                    || dst_rect.right != dst_surface->currentDesc.Width
                    || dst_rect.bottom != dst_surface->currentDesc.Height)
            {
                TRACE("No, dest rectangle doesn't match(surface size)\n");
                break;
            }

            TRACE("Yes\n");

            /* These flags are unimportant for the flag check, remove them */
            if (!(Flags & ~(WINEDDBLT_DONOTWAIT | WINEDDBLT_WAIT)))
            {
                WINED3DSWAPEFFECT orig_swap = dstSwapchain->presentParms.SwapEffect;

                /* The idea behind this is that a glReadPixels and a glDrawPixels call
                    * take very long, while a flip is fast.
                    * This applies to Half-Life, which does such Blts every time it finished
                    * a frame, and to Prince of Persia 3D, which uses this to draw at least the main
                    * menu. This is also used by all apps when they do windowed rendering
                    *
                    * The problem is that flipping is not really the same as copying. After a
                    * Blt the front buffer is a copy of the back buffer, and the back buffer is
                    * untouched. Therefore it's necessary to override the swap effect
                    * and to set it back after the flip.
                    *
                    * Windowed Direct3D < 7 apps do the same. The D3D7 sdk demos are nice
                    * testcases.
                    */

                dstSwapchain->presentParms.SwapEffect = WINED3DSWAPEFFECT_COPY;
                dstSwapchain->presentParms.PresentationInterval = WINED3DPRESENT_INTERVAL_IMMEDIATE;

                TRACE("Full screen back buffer -> front buffer blt, performing a flip instead\n");
                IWineD3DSwapChain_Present((IWineD3DSwapChain *)dstSwapchain,
                        NULL, NULL, dstSwapchain->win_handle, NULL, 0);

                dstSwapchain->presentParms.SwapEffect = orig_swap;

                return WINED3D_OK;
            }
            break;
        }

        TRACE("Unsupported blit between buffers on the same swapchain\n");
        return WINED3DERR_INVALIDCALL;
    } else if(dstSwapchain && dstSwapchain == srcSwapchain) {
        FIXME("Implement hardware blit between two surfaces on the same swapchain\n");
        return WINED3DERR_INVALIDCALL;
    } else if(dstSwapchain && srcSwapchain) {
        FIXME("Implement hardware blit between two different swapchains\n");
        return WINED3DERR_INVALIDCALL;
    }
    else if (dstSwapchain)
    {
        /* Handled with regular texture -> swapchain blit */
        if (src_surface == device->render_targets[0])
            TRACE("Blit from active render target to a swapchain\n");
    }
    else if (srcSwapchain && dst_surface == device->render_targets[0])
    {
        FIXME("Implement blit from a swapchain to the active render target\n");
        return WINED3DERR_INVALIDCALL;
    }

    if ((srcSwapchain || src_surface == device->render_targets[0]) && !dstSwapchain)
    {
        /* Blit from render target to texture */
        BOOL stretchx;

        /* P8 read back is not implemented */
        if (src_surface->resource.format->id == WINED3DFMT_P8_UINT
                || dst_surface->resource.format->id == WINED3DFMT_P8_UINT)
        {
            TRACE("P8 read back not supported by frame buffer to texture blit\n");
            return WINED3DERR_INVALIDCALL;
        }

        if(Flags & (WINEDDBLT_KEYSRC | WINEDDBLT_KEYSRCOVERRIDE)) {
            TRACE("Color keying not supported by frame buffer to texture blit\n");
            return WINED3DERR_INVALIDCALL;
            /* Destination color key is checked above */
        }

        if(dst_rect.right - dst_rect.left != src_rect.right - src_rect.left) {
            stretchx = TRUE;
        } else {
            stretchx = FALSE;
        }

        /* Blt is a pretty powerful call, while glCopyTexSubImage2D is not. glCopyTexSubImage cannot
         * flip the image nor scale it.
         *
         * -> If the app asks for a unscaled, upside down copy, just perform one glCopyTexSubImage2D call
         * -> If the app wants a image width an unscaled width, copy it line per line
         * -> If the app wants a image that is scaled on the x axis, and the destination rectangle is smaller
         *    than the frame buffer, draw an upside down scaled image onto the fb, read it back and restore the
         *    back buffer. This is slower than reading line per line, thus not used for flipping
         * -> If the app wants a scaled image with a dest rect that is bigger than the fb, it has to be copied
         *    pixel by pixel
         *
         * If EXT_framebuffer_blit is supported that can be used instead. Note that EXT_framebuffer_blit implies
         * FBO support, so it doesn't really make sense to try and make it work with different offscreen rendering
         * backends.
         */
        if (fbo_blit_supported(gl_info, BLIT_OP_BLIT,
                &src_rect, src_surface->resource.usage, src_surface->resource.pool, src_surface->resource.format,
                &dst_rect, dst_surface->resource.usage, dst_surface->resource.pool, dst_surface->resource.format))
        {
            surface_blt_fbo(device, Filter,
                    src_surface, SFLAG_INDRAWABLE, &src_rect,
                    dst_surface, SFLAG_INDRAWABLE, &dst_rect);
            surface_modify_location(dst_surface, SFLAG_INDRAWABLE, TRUE);
        }
        else if (!stretchx || dst_rect.right - dst_rect.left > src_surface->currentDesc.Width
                || dst_rect.bottom - dst_rect.top > src_surface->currentDesc.Height)
        {
            TRACE("No stretching in x direction, using direct framebuffer -> texture copy\n");
            fb_copy_to_texture_direct(dst_surface, src_surface, &src_rect, &dst_rect, Filter);
        } else {
            TRACE("Using hardware stretching to flip / stretch the texture\n");
            fb_copy_to_texture_hwstretch(dst_surface, src_surface, &src_rect, &dst_rect, Filter);
        }

        if (!(dst_surface->flags & SFLAG_DONOTFREE))
        {
            HeapFree(GetProcessHeap(), 0, dst_surface->resource.heapMemory);
            dst_surface->resource.allocatedMemory = NULL;
            dst_surface->resource.heapMemory = NULL;
        }
        else
        {
            dst_surface->flags &= ~SFLAG_INSYSMEM;
        }

        return WINED3D_OK;
    }
    else if (src_surface)
    {
        /* Blit from offscreen surface to render target */
        DWORD oldCKeyFlags = src_surface->CKeyFlags;
        WINEDDCOLORKEY oldBltCKey = src_surface->SrcBltCKey;

        TRACE("Blt from surface %p to rendertarget %p\n", src_surface, dst_surface);

        if (!(Flags & (WINEDDBLT_KEYSRC | WINEDDBLT_KEYSRCOVERRIDE))
                && fbo_blit_supported(gl_info, BLIT_OP_BLIT,
                        &src_rect, src_surface->resource.usage, src_surface->resource.pool,
                        src_surface->resource.format,
                        &dst_rect, dst_surface->resource.usage, dst_surface->resource.pool,
                        dst_surface->resource.format))
        {
            TRACE("Using surface_blt_fbo.\n");
            /* The source is always a texture, but never the currently active render target, and the texture
             * contents are never upside down. */
            surface_blt_fbo(device, Filter,
                    src_surface, SFLAG_INDRAWABLE, &src_rect,
                    dst_surface, SFLAG_INDRAWABLE, &dst_rect);
            surface_modify_location(dst_surface, SFLAG_INDRAWABLE, TRUE);
            return WINED3D_OK;
        }

        if (!(Flags & (WINEDDBLT_KEYSRC | WINEDDBLT_KEYSRCOVERRIDE))
                && arbfp_blit.blit_supported(gl_info, BLIT_OP_BLIT,
                        &src_rect, src_surface->resource.usage, src_surface->resource.pool,
                        src_surface->resource.format,
                        &dst_rect, dst_surface->resource.usage, dst_surface->resource.pool,
                        dst_surface->resource.format))
        {
            return arbfp_blit_surface(device, src_surface, &src_rect, dst_surface, &dst_rect, BLIT_OP_BLIT, Filter);
        }

        if (!device->blitter->blit_supported(gl_info, BLIT_OP_BLIT,
                &src_rect, src_surface->resource.usage, src_surface->resource.pool, src_surface->resource.format,
                &dst_rect, dst_surface->resource.usage, dst_surface->resource.pool, dst_surface->resource.format))
        {
            FIXME("Unsupported blit operation falling back to software\n");
            return WINED3DERR_INVALIDCALL;
        }

        /* Color keying: Check if we have to do a color keyed blt,
         * and if not check if a color key is activated.
         *
         * Just modify the color keying parameters in the surface and restore them afterwards
         * The surface keeps track of the color key last used to load the opengl surface.
         * PreLoad will catch the change to the flags and color key and reload if necessary.
         */
        if(Flags & WINEDDBLT_KEYSRC) {
            /* Use color key from surface */
        } else if(Flags & WINEDDBLT_KEYSRCOVERRIDE) {
            /* Use color key from DDBltFx */
            src_surface->CKeyFlags |= WINEDDSD_CKSRCBLT;
            src_surface->SrcBltCKey = DDBltFx->ddckSrcColorkey;
        } else {
            /* Do not use color key */
            src_surface->CKeyFlags &= ~WINEDDSD_CKSRCBLT;
        }

        surface_blt_to_drawable(device, Filter, Flags & (WINEDDBLT_KEYSRC | WINEDDBLT_KEYSRCOVERRIDE),
                src_surface, &src_rect, dst_surface, &dst_rect);

        /* Restore the color key parameters */
        src_surface->CKeyFlags = oldCKeyFlags;
        src_surface->SrcBltCKey = oldBltCKey;

        surface_modify_location(dst_surface, SFLAG_INDRAWABLE, TRUE);

        return WINED3D_OK;
    }
    else
    {
        /* Source-Less Blit to render target */
        if (Flags & WINEDDBLT_COLORFILL)
        {
            WINED3DCOLORVALUE color;

            TRACE("Colorfill\n");

            /* The color as given in the Blt function is in the surface format. */
            if (!surface_convert_color_to_float(dst_surface, DDBltFx->u5.dwFillColor, &color))
                return WINED3DERR_INVALIDCALL;

            return surface_color_fill(dst_surface, &dst_rect, &color);
        }
    }

    /* Default: Fall back to the generic blt. Not an error, a TRACE is enough */
    TRACE("Didn't find any usable render target setup for hw blit, falling back to software\n");
    return WINED3DERR_INVALIDCALL;
}

static HRESULT IWineD3DSurfaceImpl_BltZ(IWineD3DSurfaceImpl *This, const RECT *DestRect,
        IWineD3DSurface *src_surface, const RECT *src_rect, DWORD Flags, const WINEDDBLTFX *DDBltFx)
{
    IWineD3DDeviceImpl *device = This->resource.device;
    float depth;

    if (Flags & WINEDDBLT_DEPTHFILL)
    {
        switch (This->resource.format->id)
        {
            case WINED3DFMT_D16_UNORM:
                depth = (float) DDBltFx->u5.dwFillDepth / (float) 0x0000ffff;
                break;
            case WINED3DFMT_S1_UINT_D15_UNORM:
                depth = (float) DDBltFx->u5.dwFillDepth / (float) 0x00007fff;
                break;
            case WINED3DFMT_D24_UNORM_S8_UINT:
            case WINED3DFMT_X8D24_UNORM:
                depth = (float) DDBltFx->u5.dwFillDepth / (float) 0x00ffffff;
                break;
            case WINED3DFMT_D32_UNORM:
                depth = (float) DDBltFx->u5.dwFillDepth / (float) 0xffffffff;
                break;
            default:
                depth = 0.0f;
                ERR("Unexpected format for depth fill: %s.\n", debug_d3dformat(This->resource.format->id));
        }

        return IWineD3DDevice_Clear((IWineD3DDevice *)device, DestRect ? 1 : 0, DestRect,
                WINED3DCLEAR_ZBUFFER, 0x00000000, depth, 0x00000000);
    }

    FIXME("(%p): Unsupp depthstencil blit\n", This);
    return WINED3DERR_INVALIDCALL;
}

static HRESULT WINAPI IWineD3DSurfaceImpl_Blt(IWineD3DSurface *iface, const RECT *DestRect,
        IWineD3DSurface *src_surface, const RECT *SrcRect, DWORD Flags,
        const WINEDDBLTFX *DDBltFx, WINED3DTEXTUREFILTERTYPE Filter)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;
    IWineD3DSurfaceImpl *src = (IWineD3DSurfaceImpl *)src_surface;
    IWineD3DDeviceImpl *device = This->resource.device;

    TRACE("iface %p, dst_rect %s, src_surface %p, src_rect %s, flags %#x, fx %p, filter %s.\n",
            iface, wine_dbgstr_rect(DestRect), src_surface, wine_dbgstr_rect(SrcRect),
            Flags, DDBltFx, debug_d3dtexturefiltertype(Filter));
    TRACE("Usage is %s.\n", debug_d3dusage(This->resource.usage));

    if ((This->flags & SFLAG_LOCKED) || (src && (src->flags & SFLAG_LOCKED)))
    {
        WARN(" Surface is busy, returning DDERR_SURFACEBUSY\n");
        return WINEDDERR_SURFACEBUSY;
    }

    /* Accessing the depth stencil is supposed to fail between a BeginScene and EndScene pair,
     * except depth blits, which seem to work
     */
    if (This == device->depth_stencil || (src && src == device->depth_stencil))
    {
        if (device->inScene && !(Flags & WINEDDBLT_DEPTHFILL))
        {
            TRACE("Attempt to access the depth stencil surface in a BeginScene / EndScene pair, returning WINED3DERR_INVALIDCALL\n");
            return WINED3DERR_INVALIDCALL;
        }
        else if (SUCCEEDED(IWineD3DSurfaceImpl_BltZ(This, DestRect, src_surface, SrcRect, Flags, DDBltFx)))
        {
            TRACE("Z Blit override handled the blit\n");
            return WINED3D_OK;
        }
    }

    /* Special cases for RenderTargets */
    if ((This->resource.usage & WINED3DUSAGE_RENDERTARGET)
            || (src && (src->resource.usage & WINED3DUSAGE_RENDERTARGET)))
    {
        if (SUCCEEDED(IWineD3DSurfaceImpl_BltOverride(This, DestRect, src, SrcRect, Flags, DDBltFx, Filter)))
            return WINED3D_OK;
    }

    /* For the rest call the X11 surface implementation.
     * For RenderTargets this should be implemented OpenGL accelerated in BltOverride,
     * other Blts are rather rare. */
    return IWineD3DBaseSurfaceImpl_Blt(iface, DestRect, src_surface, SrcRect, Flags, DDBltFx, Filter);
}

static HRESULT WINAPI IWineD3DSurfaceImpl_BltFast(IWineD3DSurface *iface, DWORD dstx, DWORD dsty,
        IWineD3DSurface *src_surface, const RECT *rsrc, DWORD trans)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *)iface;
    IWineD3DSurfaceImpl *src = (IWineD3DSurfaceImpl *)src_surface;
    IWineD3DDeviceImpl *device = This->resource.device;

    TRACE("iface %p, dst_x %u, dst_y %u, src_surface %p, src_rect %s, flags %#x.\n",
            iface, dstx, dsty, src_surface, wine_dbgstr_rect(rsrc), trans);

    if ((This->flags & SFLAG_LOCKED) || (src->flags & SFLAG_LOCKED))
    {
        WARN(" Surface is busy, returning DDERR_SURFACEBUSY\n");
        return WINEDDERR_SURFACEBUSY;
    }

    if (device->inScene && (This == device->depth_stencil || src == device->depth_stencil))
    {
        TRACE("Attempt to access the depth stencil surface in a BeginScene / EndScene pair, returning WINED3DERR_INVALIDCALL\n");
        return WINED3DERR_INVALIDCALL;
    }

    /* Special cases for RenderTargets */
    if ((This->resource.usage & WINED3DUSAGE_RENDERTARGET)
            || (src->resource.usage & WINED3DUSAGE_RENDERTARGET))
    {

        RECT SrcRect, DstRect;
        DWORD Flags=0;

        surface_get_rect(src, rsrc, &SrcRect);

        DstRect.left = dstx;
        DstRect.top=dsty;
        DstRect.right = dstx + SrcRect.right - SrcRect.left;
        DstRect.bottom = dsty + SrcRect.bottom - SrcRect.top;

        /* Convert BltFast flags into Btl ones because it is called from SurfaceImpl_Blt as well */
        if(trans & WINEDDBLTFAST_SRCCOLORKEY)
            Flags |= WINEDDBLT_KEYSRC;
        if(trans & WINEDDBLTFAST_DESTCOLORKEY)
            Flags |= WINEDDBLT_KEYDEST;
        if(trans & WINEDDBLTFAST_WAIT)
            Flags |= WINEDDBLT_WAIT;
        if(trans & WINEDDBLTFAST_DONOTWAIT)
            Flags |= WINEDDBLT_DONOTWAIT;

        if (SUCCEEDED(IWineD3DSurfaceImpl_BltOverride(This,
                &DstRect, src, &SrcRect, Flags, NULL, WINED3DTEXF_POINT)))
            return WINED3D_OK;
    }

    return IWineD3DBaseSurfaceImpl_BltFast(iface, dstx, dsty, src_surface, rsrc, trans);
}

static HRESULT WINAPI IWineD3DSurfaceImpl_RealizePalette(IWineD3DSurface *iface)
{
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *) iface;
    RGBQUAD col[256];
    IWineD3DPaletteImpl *pal = This->palette;
    unsigned int n;
    TRACE("(%p)\n", This);

    if (!pal) return WINED3D_OK;

    if (This->resource.format->id == WINED3DFMT_P8_UINT
            || This->resource.format->id == WINED3DFMT_P8_UINT_A8_UNORM)
    {
        if (This->resource.usage & WINED3DUSAGE_RENDERTARGET)
        {
            /* Make sure the texture is up to date. This call doesn't do
             * anything if the texture is already up to date. */
            surface_load_location(This, SFLAG_INTEXTURE, NULL);

            /* We want to force a palette refresh, so mark the drawable as not being up to date */
            surface_modify_location(This, SFLAG_INDRAWABLE, FALSE);
        }
        else
        {
            if (!(This->flags & SFLAG_INSYSMEM))
            {
                TRACE("Palette changed with surface that does not have an up to date system memory copy.\n");
                surface_load_location(This, SFLAG_INSYSMEM, NULL);
            }
            TRACE("Dirtifying surface\n");
            surface_modify_location(This, SFLAG_INSYSMEM, TRUE);
        }
    }

    if (This->flags & SFLAG_DIBSECTION)
    {
        TRACE("(%p): Updating the hdc's palette\n", This);
        for (n=0; n<256; n++) {
            col[n].rgbRed   = pal->palents[n].peRed;
            col[n].rgbGreen = pal->palents[n].peGreen;
            col[n].rgbBlue  = pal->palents[n].peBlue;
            col[n].rgbReserved = 0;
        }
        SetDIBColorTable(This->hDC, 0, 256, col);
    }

    /* Propagate the changes to the drawable when we have a palette. */
    if (This->resource.usage & WINED3DUSAGE_RENDERTARGET)
        surface_load_location(This, SFLAG_INDRAWABLE, NULL);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DSurfaceImpl_PrivateSetup(IWineD3DSurface *iface) {
    /** Check against the maximum texture sizes supported by the video card **/
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *) iface;
    const struct wined3d_gl_info *gl_info = &This->resource.device->adapter->gl_info;
    unsigned int pow2Width, pow2Height;

    This->texture_name = 0;
    This->texture_target = GL_TEXTURE_2D;

    /* Non-power2 support */
    if (gl_info->supported[ARB_TEXTURE_NON_POWER_OF_TWO] || gl_info->supported[WINED3D_GL_NORMALIZED_TEXRECT])
    {
        pow2Width = This->currentDesc.Width;
        pow2Height = This->currentDesc.Height;
    }
    else
    {
        /* Find the nearest pow2 match */
        pow2Width = pow2Height = 1;
        while (pow2Width < This->currentDesc.Width) pow2Width <<= 1;
        while (pow2Height < This->currentDesc.Height) pow2Height <<= 1;
    }
    This->pow2Width  = pow2Width;
    This->pow2Height = pow2Height;

    if (pow2Width > This->currentDesc.Width || pow2Height > This->currentDesc.Height)
    {
        /* TODO: Add support for non power two compressed textures. */
        if (This->resource.format->flags & WINED3DFMT_FLAG_COMPRESSED)
        {
            FIXME("(%p) Compressed non-power-two textures are not supported w(%d) h(%d)\n",
                  This, This->currentDesc.Width, This->currentDesc.Height);
            return WINED3DERR_NOTAVAILABLE;
        }
    }

    if (pow2Width != This->currentDesc.Width
            || pow2Height != This->currentDesc.Height)
    {
        This->flags |= SFLAG_NONPOW2;
    }

    TRACE("%p\n", This);
    if ((This->pow2Width > gl_info->limits.texture_size || This->pow2Height > gl_info->limits.texture_size)
            && !(This->resource.usage & (WINED3DUSAGE_RENDERTARGET | WINED3DUSAGE_DEPTHSTENCIL)))
    {
        /* one of three options
        1: Do the same as we do with nonpow 2 and scale the texture, (any texture ops would require the texture to be scaled which is potentially slow)
        2: Set the texture to the maximum size (bad idea)
        3:    WARN and return WINED3DERR_NOTAVAILABLE;
        4: Create the surface, but allow it to be used only for DirectDraw Blts. Some apps(e.g. Swat 3) create textures with a Height of 16 and a Width > 3000 and blt 16x16 letter areas from them to the render target.
        */
        if(This->resource.pool == WINED3DPOOL_DEFAULT || This->resource.pool == WINED3DPOOL_MANAGED)
        {
            WARN("(%p) Unable to allocate a surface which exceeds the maximum OpenGL texture size\n", This);
            return WINED3DERR_NOTAVAILABLE;
        }

        /* We should never use this surface in combination with OpenGL! */
        TRACE("(%p) Creating an oversized surface: %ux%u\n", This, This->pow2Width, This->pow2Height);
    }
    else
    {
        /* Don't use ARB_TEXTURE_RECTANGLE in case the surface format is P8 and EXT_PALETTED_TEXTURE
           is used in combination with texture uploads (RTL_READTEX/RTL_TEXTEX). The reason is that EXT_PALETTED_TEXTURE
           doesn't work in combination with ARB_TEXTURE_RECTANGLE.
        */
        if (This->flags & SFLAG_NONPOW2 && gl_info->supported[ARB_TEXTURE_RECTANGLE]
                && !(This->resource.format->id == WINED3DFMT_P8_UINT
                && gl_info->supported[EXT_PALETTED_TEXTURE]
                && wined3d_settings.rendertargetlock_mode == RTL_READTEX))
        {
            This->texture_target = GL_TEXTURE_RECTANGLE_ARB;
            This->pow2Width  = This->currentDesc.Width;
            This->pow2Height = This->currentDesc.Height;
            This->flags &= ~(SFLAG_NONPOW2 | SFLAG_NORMCOORD);
        }
    }

    switch (wined3d_settings.offscreen_rendering_mode)
    {
        case ORM_FBO:
            This->get_drawable_size = get_drawable_size_fbo;
            break;

        case ORM_BACKBUFFER:
            This->get_drawable_size = get_drawable_size_backbuffer;
            break;

        default:
            ERR("Unhandled offscreen rendering mode %#x.\n", wined3d_settings.offscreen_rendering_mode);
            return WINED3DERR_INVALIDCALL;
    }

    This->flags |= SFLAG_INSYSMEM;

    return WINED3D_OK;
}

/* GL locking is done by the caller */
static void surface_depth_blt(IWineD3DSurfaceImpl *This, const struct wined3d_gl_info *gl_info,
        GLuint texture, GLsizei w, GLsizei h, GLenum target)
{
    IWineD3DDeviceImpl *device = This->resource.device;
    GLint compare_mode = GL_NONE;
    struct blt_info info;
    GLint old_binding = 0;
    RECT rect;

    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_VIEWPORT_BIT);

    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glViewport(0, 0, w, h);

    SetRect(&rect, 0, h, w, 0);
    surface_get_blt_info(target, &rect, w, h, &info);
    GL_EXTCALL(glActiveTextureARB(GL_TEXTURE0_ARB));
    glGetIntegerv(info.binding, &old_binding);
    glBindTexture(info.bind_target, texture);
    if (gl_info->supported[ARB_SHADOW])
    {
        glGetTexParameteriv(info.bind_target, GL_TEXTURE_COMPARE_MODE_ARB, &compare_mode);
        if (compare_mode != GL_NONE) glTexParameteri(info.bind_target, GL_TEXTURE_COMPARE_MODE_ARB, GL_NONE);
    }

    device->shader_backend->shader_select_depth_blt((IWineD3DDevice *)device,
            info.tex_type, &This->ds_current_size);

    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord3fv(info.coords[0]);
    glVertex2f(-1.0f, -1.0f);
    glTexCoord3fv(info.coords[1]);
    glVertex2f(1.0f, -1.0f);
    glTexCoord3fv(info.coords[2]);
    glVertex2f(-1.0f, 1.0f);
    glTexCoord3fv(info.coords[3]);
    glVertex2f(1.0f, 1.0f);
    glEnd();

    if (compare_mode != GL_NONE) glTexParameteri(info.bind_target, GL_TEXTURE_COMPARE_MODE_ARB, compare_mode);
    glBindTexture(info.bind_target, old_binding);

    glPopAttrib();

    device->shader_backend->shader_deselect_depth_blt((IWineD3DDevice *)device);
}

void surface_modify_ds_location(IWineD3DSurfaceImpl *surface,
        DWORD location, UINT w, UINT h)
{
    TRACE("surface %p, new location %#x, w %u, h %u.\n", surface, location, w, h);

    if (location & ~SFLAG_DS_LOCATIONS)
        FIXME("Invalid location (%#x) specified.\n", location);

    surface->ds_current_size.cx = w;
    surface->ds_current_size.cy = h;
    surface->flags &= ~SFLAG_DS_LOCATIONS;
    surface->flags |= location;
}

/* Context activation is done by the caller. */
void surface_load_ds_location(IWineD3DSurfaceImpl *surface, struct wined3d_context *context, DWORD location)
{
    IWineD3DDeviceImpl *device = surface->resource.device;
    const struct wined3d_gl_info *gl_info = context->gl_info;

    TRACE("surface %p, new location %#x.\n", surface, location);

    /* TODO: Make this work for modes other than FBO */
    if (wined3d_settings.offscreen_rendering_mode != ORM_FBO) return;

    if (!(surface->flags & location))
    {
        surface->ds_current_size.cx = 0;
        surface->ds_current_size.cy = 0;
    }

    if (surface->ds_current_size.cx == surface->currentDesc.Width
            && surface->ds_current_size.cy == surface->currentDesc.Height)
    {
        TRACE("Location (%#x) is already up to date.\n", location);
        return;
    }

    if (surface->current_renderbuffer)
    {
        FIXME("Not supported with fixed up depth stencil.\n");
        return;
    }

    if (!(surface->flags & SFLAG_LOCATIONS))
    {
        FIXME("No up to date depth stencil location.\n");
        surface->flags |= location;
        return;
    }

    if (location == SFLAG_DS_OFFSCREEN)
    {
        GLint old_binding = 0;
        GLenum bind_target;
        GLsizei w, h;

        /* The render target is allowed to be smaller than the depth/stencil
         * buffer, so the onscreen depth/stencil buffer is potentially smaller
         * than the offscreen surface. Don't overwrite the offscreen surface
         * with undefined data. */
        w = min(surface->currentDesc.Width, context->swapchain->presentParms.BackBufferWidth);
        h = min(surface->currentDesc.Height, context->swapchain->presentParms.BackBufferHeight);

        TRACE("Copying onscreen depth buffer to depth texture.\n");

        ENTER_GL();

        if (!device->depth_blt_texture)
        {
            glGenTextures(1, &device->depth_blt_texture);
        }

        /* Note that we use depth_blt here as well, rather than glCopyTexImage2D
         * directly on the FBO texture. That's because we need to flip. */
        context_bind_fbo(context, GL_FRAMEBUFFER, NULL);
        if (surface->texture_target == GL_TEXTURE_RECTANGLE_ARB)
        {
            glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB, &old_binding);
            bind_target = GL_TEXTURE_RECTANGLE_ARB;
        }
        else
        {
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_binding);
            bind_target = GL_TEXTURE_2D;
        }
        glBindTexture(bind_target, device->depth_blt_texture);
        glCopyTexImage2D(bind_target, surface->texture_level, surface->resource.format->glInternal, 0, 0, w, h, 0);
        glTexParameteri(bind_target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(bind_target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(bind_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(bind_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(bind_target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(bind_target, GL_DEPTH_TEXTURE_MODE_ARB, GL_LUMINANCE);
        glBindTexture(bind_target, old_binding);

        /* Setup the destination */
        if (!device->depth_blt_rb)
        {
            gl_info->fbo_ops.glGenRenderbuffers(1, &device->depth_blt_rb);
            checkGLcall("glGenRenderbuffersEXT");
        }
        if (device->depth_blt_rb_w != w || device->depth_blt_rb_h != h)
        {
            gl_info->fbo_ops.glBindRenderbuffer(GL_RENDERBUFFER, device->depth_blt_rb);
            checkGLcall("glBindRenderbufferEXT");
            gl_info->fbo_ops.glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
            checkGLcall("glRenderbufferStorageEXT");
            device->depth_blt_rb_w = w;
            device->depth_blt_rb_h = h;
        }

        context_bind_fbo(context, GL_FRAMEBUFFER, &context->dst_fbo);
        gl_info->fbo_ops.glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, device->depth_blt_rb);
        checkGLcall("glFramebufferRenderbufferEXT");
        context_attach_depth_stencil_fbo(context, GL_FRAMEBUFFER, surface, FALSE);

        /* Do the actual blit */
        surface_depth_blt(surface, gl_info, device->depth_blt_texture, w, h, bind_target);
        checkGLcall("depth_blt");

        if (context->current_fbo) context_bind_fbo(context, GL_FRAMEBUFFER, &context->current_fbo->id);
        else context_bind_fbo(context, GL_FRAMEBUFFER, NULL);

        LEAVE_GL();

        if (wined3d_settings.strict_draw_ordering) wglFlush(); /* Flush to ensure ordering across contexts. */
    }
    else if (location == SFLAG_DS_ONSCREEN)
    {
        TRACE("Copying depth texture to onscreen depth buffer.\n");

        ENTER_GL();

        context_bind_fbo(context, GL_FRAMEBUFFER, NULL);
        surface_depth_blt(surface, gl_info, surface->texture_name,
                surface->currentDesc.Width, surface->currentDesc.Height, surface->texture_target);
        checkGLcall("depth_blt");

        if (context->current_fbo) context_bind_fbo(context, GL_FRAMEBUFFER, &context->current_fbo->id);

        LEAVE_GL();

        if (wined3d_settings.strict_draw_ordering) wglFlush(); /* Flush to ensure ordering across contexts. */
    }
    else
    {
        ERR("Invalid location (%#x) specified.\n", location);
    }

    surface->flags |= location;
    surface->ds_current_size.cx = surface->currentDesc.Width;
    surface->ds_current_size.cy = surface->currentDesc.Height;
}

void surface_modify_location(IWineD3DSurfaceImpl *surface, DWORD flag, BOOL persistent)
{
    IWineD3DSurfaceImpl *overlay;

    TRACE("surface %p, location %s, persistent %#x.\n",
            surface, debug_surflocation(flag), persistent);

    if (wined3d_settings.offscreen_rendering_mode == ORM_FBO)
    {
        if (surface_is_offscreen(surface))
        {
            /* With ORM_FBO, SFLAG_INTEXTURE and SFLAG_INDRAWABLE are the same for offscreen targets. */
            if (flag & (SFLAG_INTEXTURE | SFLAG_INDRAWABLE)) flag |= (SFLAG_INTEXTURE | SFLAG_INDRAWABLE);
        }
        else
        {
            TRACE("Surface %p is an onscreen surface.\n", surface);
        }
    }

    if (persistent)
    {
        if (((surface->flags & SFLAG_INTEXTURE) && !(flag & SFLAG_INTEXTURE))
                || ((surface->flags & SFLAG_INSRGBTEX) && !(flag & SFLAG_INSRGBTEX)))
        {
            if (surface->container.type == WINED3D_CONTAINER_TEXTURE)
            {
                TRACE("Passing to container.\n");
                IWineD3DBaseTexture_SetDirty((IWineD3DBaseTexture *)surface->container.u.texture, TRUE);
            }
        }
        surface->flags &= ~SFLAG_LOCATIONS;
        surface->flags |= flag;

        /* Redraw emulated overlays, if any */
        if (flag & SFLAG_INDRAWABLE && !list_empty(&surface->overlays))
        {
            LIST_FOR_EACH_ENTRY(overlay, &surface->overlays, IWineD3DSurfaceImpl, overlay_entry)
            {
                IWineD3DSurface_DrawOverlay((IWineD3DSurface *)overlay);
            }
        }
    }
    else
    {
        if ((surface->flags & (SFLAG_INTEXTURE | SFLAG_INSRGBTEX)) && (flag & (SFLAG_INTEXTURE | SFLAG_INSRGBTEX)))
        {
            if (surface->container.type == WINED3D_CONTAINER_TEXTURE)
            {
                TRACE("Passing to container\n");
                IWineD3DBaseTexture_SetDirty((IWineD3DBaseTexture *)surface->container.u.texture, TRUE);
            }
        }
        surface->flags &= ~flag;
    }

    if (!(surface->flags & SFLAG_LOCATIONS))
    {
        ERR("Surface %p does not have any up to date location.\n", surface);
    }
}

HRESULT surface_load_location(IWineD3DSurfaceImpl *surface, DWORD flag, const RECT *rect)
{
    IWineD3DDeviceImpl *device = surface->resource.device;
    const struct wined3d_gl_info *gl_info = &device->adapter->gl_info;
    BOOL drawable_read_ok = surface_is_offscreen(surface);
    struct wined3d_format format;
    CONVERT_TYPES convert;
    int width, pitch, outpitch;
    BYTE *mem;
    BOOL in_fbo = FALSE;

    TRACE("surface %p, location %s, rect %s.\n", surface, debug_surflocation(flag), wine_dbgstr_rect(rect));

    if (surface->resource.usage & WINED3DUSAGE_DEPTHSTENCIL)
    {
        if (flag == SFLAG_INTEXTURE)
        {
            struct wined3d_context *context = context_acquire(device, NULL);
            surface_load_ds_location(surface, context, SFLAG_DS_OFFSCREEN);
            context_release(context);
            return WINED3D_OK;
        }
        else
        {
            FIXME("Unimplemented location %s for depth/stencil buffers.\n", debug_surflocation(flag));
            return WINED3DERR_INVALIDCALL;
        }
    }

    if (wined3d_settings.offscreen_rendering_mode == ORM_FBO)
    {
        if (surface_is_offscreen(surface))
        {
            /* With ORM_FBO, SFLAG_INTEXTURE and SFLAG_INDRAWABLE are the same for offscreen targets.
             * Prefer SFLAG_INTEXTURE. */
            if (flag == SFLAG_INDRAWABLE) flag = SFLAG_INTEXTURE;
            drawable_read_ok = FALSE;
            in_fbo = TRUE;
        }
        else
        {
            TRACE("Surface %p is an onscreen surface.\n", surface);
        }
    }

    if (surface->flags & flag)
    {
        TRACE("Location already up to date\n");
        return WINED3D_OK;
    }

    if (!(surface->flags & SFLAG_LOCATIONS))
    {
        ERR("Surface %p does not have any up to date location.\n", surface);
        surface->flags |= SFLAG_LOST;
        return WINED3DERR_DEVICELOST;
    }

    if (flag == SFLAG_INSYSMEM)
    {
        surface_prepare_system_memory(surface);

        /* Download the surface to system memory */
        if (surface->flags & (SFLAG_INTEXTURE | SFLAG_INSRGBTEX))
        {
            struct wined3d_context *context = NULL;

            if (!device->isInDraw) context = context_acquire(device, NULL);

            surface_bind_and_dirtify(surface, !(surface->flags & SFLAG_INTEXTURE));
            surface_download_data(surface, gl_info);

            if (context) context_release(context);
        }
        else
        {
            /* Note: It might be faster to download into a texture first. */
            read_from_framebuffer(surface, rect, surface->resource.allocatedMemory,
                    IWineD3DSurface_GetPitch((IWineD3DSurface *)surface));
        }
    }
    else if (flag == SFLAG_INDRAWABLE)
    {
        if (wined3d_settings.rendertargetlock_mode == RTL_READTEX)
            surface_load_location(surface, SFLAG_INTEXTURE, NULL);

        if (surface->flags & SFLAG_INTEXTURE)
        {
            RECT r;

            surface_get_rect(surface, rect, &r);
            surface_blt_to_drawable(device, WINED3DTEXF_POINT, FALSE, surface, &r, surface, &r);
        }
        else
        {
            int byte_count;
            if ((surface->flags & SFLAG_LOCATIONS) == SFLAG_INSRGBTEX)
            {
                /* This needs a shader to convert the srgb data sampled from the GL texture into RGB
                 * values, otherwise we get incorrect values in the target. For now go the slow way
                 * via a system memory copy
                 */
                surface_load_location(surface, SFLAG_INSYSMEM, rect);
            }

            d3dfmt_get_conv(surface, FALSE /* We need color keying */,
                    FALSE /* We won't use textures */, &format, &convert);

            /* The width is in 'length' not in bytes */
            width = surface->currentDesc.Width;
            pitch = IWineD3DSurface_GetPitch((IWineD3DSurface *)surface);

            /* Don't use PBOs for converted surfaces. During PBO conversion we look at SFLAG_CONVERTED
             * but it isn't set (yet) in all cases it is getting called. */
            if ((convert != NO_CONVERSION) && (surface->flags & SFLAG_PBO))
            {
                struct wined3d_context *context = NULL;

                TRACE("Removing the pbo attached to surface %p.\n", surface);

                if (!device->isInDraw) context = context_acquire(device, NULL);
                surface_remove_pbo(surface, gl_info);
                if (context) context_release(context);
            }

            if ((convert != NO_CONVERSION) && surface->resource.allocatedMemory)
            {
                int height = surface->currentDesc.Height;
                byte_count = format.conv_byte_count;

                /* Stick to the alignment for the converted surface too, makes it easier to load the surface */
                outpitch = width * byte_count;
                outpitch = (outpitch + device->surface_alignment - 1) & ~(device->surface_alignment - 1);

                mem = HeapAlloc(GetProcessHeap(), 0, outpitch * height);
                if(!mem) {
                    ERR("Out of memory %d, %d!\n", outpitch, height);
                    return WINED3DERR_OUTOFVIDEOMEMORY;
                }
                d3dfmt_convert_surface(surface->resource.allocatedMemory, mem, pitch,
                        width, height, outpitch, convert, surface);

                surface->flags |= SFLAG_CONVERTED;
            }
            else
            {
                surface->flags &= ~SFLAG_CONVERTED;
                mem = surface->resource.allocatedMemory;
                byte_count = format.byte_count;
            }

            flush_to_framebuffer_drawpixels(surface, format.glFormat, format.glType, byte_count, mem);

            /* Don't delete PBO memory */
            if ((mem != surface->resource.allocatedMemory) && !(surface->flags & SFLAG_PBO))
                HeapFree(GetProcessHeap(), 0, mem);
        }
    }
    else /* if(flag & (SFLAG_INTEXTURE | SFLAG_INSRGBTEX)) */
    {
        const DWORD attach_flags = WINED3DFMT_FLAG_FBO_ATTACHABLE | WINED3DFMT_FLAG_FBO_ATTACHABLE_SRGB;

        if (drawable_read_ok && (surface->flags & SFLAG_INDRAWABLE))
        {
            read_from_framebuffer_texture(surface, flag == SFLAG_INSRGBTEX);
        }
        else if (surface->flags & (SFLAG_INSRGBTEX | SFLAG_INTEXTURE)
                && (surface->resource.format->flags & attach_flags) == attach_flags
                && fbo_blit_supported(gl_info, BLIT_OP_BLIT,
                        NULL, surface->resource.usage, surface->resource.pool, surface->resource.format,
                        NULL, surface->resource.usage, surface->resource.pool, surface->resource.format))
        {
            DWORD src_location = flag == SFLAG_INSRGBTEX ? SFLAG_INTEXTURE : SFLAG_INSRGBTEX;
            RECT rect = {0, 0, surface->currentDesc.Width, surface->currentDesc.Height};

            surface_blt_fbo(surface->resource.device, WINED3DTEXF_POINT,
                    surface, src_location, &rect, surface, flag, &rect);
        }
        else
        {
            /* Upload from system memory */
            BOOL srgb = flag == SFLAG_INSRGBTEX;
            struct wined3d_context *context = NULL;

            d3dfmt_get_conv(surface, TRUE /* We need color keying */,
                    TRUE /* We will use textures */, &format, &convert);

            if (srgb)
            {
                if ((surface->flags & (SFLAG_INTEXTURE | SFLAG_INSYSMEM)) == SFLAG_INTEXTURE)
                {
                    /* Performance warning... */
                    FIXME("Downloading RGB surface %p to reload it as sRGB.\n", surface);
                    surface_load_location(surface, SFLAG_INSYSMEM, rect);
                }
            }
            else
            {
                if ((surface->flags & (SFLAG_INSRGBTEX | SFLAG_INSYSMEM)) == SFLAG_INSRGBTEX)
                {
                    /* Performance warning... */
                    FIXME("Downloading sRGB surface %p to reload it as RGB.\n", surface);
                    surface_load_location(surface, SFLAG_INSYSMEM, rect);
                }
            }
            if (!(surface->flags & SFLAG_INSYSMEM))
            {
                WARN("Trying to load a texture from sysmem, but SFLAG_INSYSMEM is not set.\n");
                /* Lets hope we get it from somewhere... */
                surface_load_location(surface, SFLAG_INSYSMEM, rect);
            }

            if (!device->isInDraw) context = context_acquire(device, NULL);

            surface_prepare_texture(surface, gl_info, srgb);
            surface_bind_and_dirtify(surface, srgb);

            if (surface->CKeyFlags & WINEDDSD_CKSRCBLT)
            {
                surface->flags |= SFLAG_GLCKEY;
                surface->glCKey = surface->SrcBltCKey;
            }
            else surface->flags &= ~SFLAG_GLCKEY;

            /* The width is in 'length' not in bytes */
            width = surface->currentDesc.Width;
            pitch = IWineD3DSurface_GetPitch((IWineD3DSurface *)surface);

            /* Don't use PBOs for converted surfaces. During PBO conversion we look at SFLAG_CONVERTED
             * but it isn't set (yet) in all cases it is getting called. */
            if ((convert != NO_CONVERSION || format.convert) && (surface->flags & SFLAG_PBO))
            {
                TRACE("Removing the pbo attached to surface %p.\n", surface);
                surface_remove_pbo(surface, gl_info);
            }

            if (format.convert)
            {
                /* This code is entered for texture formats which need a fixup. */
                int height = surface->currentDesc.Height;

                /* Stick to the alignment for the converted surface too, makes it easier to load the surface */
                outpitch = width * format.conv_byte_count;
                outpitch = (outpitch + device->surface_alignment - 1) & ~(device->surface_alignment - 1);

                mem = HeapAlloc(GetProcessHeap(), 0, outpitch * height);
                if(!mem) {
                    ERR("Out of memory %d, %d!\n", outpitch, height);
                    if (context) context_release(context);
                    return WINED3DERR_OUTOFVIDEOMEMORY;
                }
                format.convert(surface->resource.allocatedMemory, mem, pitch, width, height);
            }
            else if (convert != NO_CONVERSION && surface->resource.allocatedMemory)
            {
                /* This code is only entered for color keying fixups */
                int height = surface->currentDesc.Height;

                /* Stick to the alignment for the converted surface too, makes it easier to load the surface */
                outpitch = width * format.conv_byte_count;
                outpitch = (outpitch + device->surface_alignment - 1) & ~(device->surface_alignment - 1);

                mem = HeapAlloc(GetProcessHeap(), 0, outpitch * height);
                if(!mem) {
                    ERR("Out of memory %d, %d!\n", outpitch, height);
                    if (context) context_release(context);
                    return WINED3DERR_OUTOFVIDEOMEMORY;
                }
                d3dfmt_convert_surface(surface->resource.allocatedMemory, mem, pitch,
                        width, height, outpitch, convert, surface);
            }
            else
            {
                mem = surface->resource.allocatedMemory;
            }

            /* Make sure the correct pitch is used */
            ENTER_GL();
            glPixelStorei(GL_UNPACK_ROW_LENGTH, width);
            LEAVE_GL();

            if (mem || (surface->flags & SFLAG_PBO))
                surface_upload_data(surface, gl_info, &format, srgb, mem);

            /* Restore the default pitch */
            ENTER_GL();
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            LEAVE_GL();

            if (context) context_release(context);

            /* Don't delete PBO memory */
            if ((mem != surface->resource.allocatedMemory) && !(surface->flags & SFLAG_PBO))
                HeapFree(GetProcessHeap(), 0, mem);
        }
    }

    if (!rect) surface->flags |= flag;

    if (in_fbo && (surface->flags & (SFLAG_INTEXTURE | SFLAG_INDRAWABLE)))
    {
        /* With ORM_FBO, SFLAG_INTEXTURE and SFLAG_INDRAWABLE are the same for offscreen targets. */
        surface->flags |= (SFLAG_INTEXTURE | SFLAG_INDRAWABLE);
    }

    return WINED3D_OK;
}

static WINED3DSURFTYPE WINAPI IWineD3DSurfaceImpl_GetImplType(IWineD3DSurface *iface) {
    return SURFACE_OPENGL;
}

static HRESULT WINAPI IWineD3DSurfaceImpl_DrawOverlay(IWineD3DSurface *iface) {
    IWineD3DSurfaceImpl *This = (IWineD3DSurfaceImpl *) iface;
    HRESULT hr;

    /* If there's no destination surface there is nothing to do */
    if(!This->overlay_dest) return WINED3D_OK;

    /* Blt calls ModifyLocation on the dest surface, which in turn calls DrawOverlay to
     * update the overlay. Prevent an endless recursion. */
    if (This->overlay_dest->flags & SFLAG_INOVERLAYDRAW)
        return WINED3D_OK;

    This->overlay_dest->flags |= SFLAG_INOVERLAYDRAW;
    hr = IWineD3DSurfaceImpl_Blt((IWineD3DSurface *)This->overlay_dest,
            &This->overlay_destrect, iface, &This->overlay_srcrect,
            WINEDDBLT_WAIT, NULL, WINED3DTEXF_LINEAR);
    This->overlay_dest->flags &= ~SFLAG_INOVERLAYDRAW;

    return hr;
}

BOOL surface_is_offscreen(IWineD3DSurfaceImpl *surface)
{
    IWineD3DSwapChainImpl *swapchain = surface->container.u.swapchain;

    /* Not on a swapchain - must be offscreen */
    if (surface->container.type != WINED3D_CONTAINER_SWAPCHAIN) return TRUE;

    /* The front buffer is always onscreen */
    if (surface == swapchain->front_buffer) return FALSE;

    /* If the swapchain is rendered to an FBO, the backbuffer is
     * offscreen, otherwise onscreen */
    return swapchain->render_to_fbo;
}

const IWineD3DSurfaceVtbl IWineD3DSurface_Vtbl =
{
    /* IUnknown */
    IWineD3DBaseSurfaceImpl_QueryInterface,
    IWineD3DBaseSurfaceImpl_AddRef,
    IWineD3DSurfaceImpl_Release,
    /* IWineD3DResource */
    IWineD3DBaseSurfaceImpl_GetParent,
    IWineD3DBaseSurfaceImpl_SetPrivateData,
    IWineD3DBaseSurfaceImpl_GetPrivateData,
    IWineD3DBaseSurfaceImpl_FreePrivateData,
    IWineD3DBaseSurfaceImpl_SetPriority,
    IWineD3DBaseSurfaceImpl_GetPriority,
    IWineD3DSurfaceImpl_PreLoad,
    IWineD3DSurfaceImpl_UnLoad,
    IWineD3DBaseSurfaceImpl_GetType,
    /* IWineD3DSurface */
    IWineD3DBaseSurfaceImpl_GetDesc,
    IWineD3DSurfaceImpl_Map,
    IWineD3DSurfaceImpl_Unmap,
    IWineD3DSurfaceImpl_GetDC,
    IWineD3DSurfaceImpl_ReleaseDC,
    IWineD3DSurfaceImpl_Flip,
    IWineD3DSurfaceImpl_Blt,
    IWineD3DBaseSurfaceImpl_GetBltStatus,
    IWineD3DBaseSurfaceImpl_GetFlipStatus,
    IWineD3DBaseSurfaceImpl_IsLost,
    IWineD3DBaseSurfaceImpl_Restore,
    IWineD3DSurfaceImpl_BltFast,
    IWineD3DBaseSurfaceImpl_GetPalette,
    IWineD3DBaseSurfaceImpl_SetPalette,
    IWineD3DSurfaceImpl_RealizePalette,
    IWineD3DBaseSurfaceImpl_SetColorKey,
    IWineD3DBaseSurfaceImpl_GetPitch,
    IWineD3DSurfaceImpl_SetMem,
    IWineD3DBaseSurfaceImpl_SetOverlayPosition,
    IWineD3DBaseSurfaceImpl_GetOverlayPosition,
    IWineD3DBaseSurfaceImpl_UpdateOverlayZOrder,
    IWineD3DBaseSurfaceImpl_UpdateOverlay,
    IWineD3DBaseSurfaceImpl_SetClipper,
    IWineD3DBaseSurfaceImpl_GetClipper,
    /* Internal use: */
    IWineD3DSurfaceImpl_LoadTexture,
    IWineD3DSurfaceImpl_BindTexture,
    IWineD3DBaseSurfaceImpl_GetData,
    IWineD3DSurfaceImpl_SetFormat,
    IWineD3DSurfaceImpl_PrivateSetup,
    IWineD3DSurfaceImpl_GetImplType,
    IWineD3DSurfaceImpl_DrawOverlay
};

static HRESULT ffp_blit_alloc(IWineD3DDevice *iface) { return WINED3D_OK; }
/* Context activation is done by the caller. */
static void ffp_blit_free(IWineD3DDevice *iface) { }

/* This function is used in case of 8bit paletted textures using GL_EXT_paletted_texture */
/* Context activation is done by the caller. */
static void ffp_blit_p8_upload_palette(IWineD3DSurfaceImpl *surface, const struct wined3d_gl_info *gl_info)
{
    BYTE table[256][4];
    BOOL colorkey_active = (surface->CKeyFlags & WINEDDSD_CKSRCBLT) ? TRUE : FALSE;

    d3dfmt_p8_init_palette(surface, table, colorkey_active);

    TRACE("Using GL_EXT_PALETTED_TEXTURE for 8-bit paletted texture support\n");
    ENTER_GL();
    GL_EXTCALL(glColorTableEXT(surface->texture_target, GL_RGBA, 256, GL_RGBA, GL_UNSIGNED_BYTE, table));
    LEAVE_GL();
}

/* Context activation is done by the caller. */
static HRESULT ffp_blit_set(IWineD3DDevice *iface, IWineD3DSurfaceImpl *surface)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *) iface;
    const struct wined3d_gl_info *gl_info = &device->adapter->gl_info;
    enum complex_fixup fixup = get_complex_fixup(surface->resource.format->color_fixup);

    /* When EXT_PALETTED_TEXTURE is around, palette conversion is done by the GPU
     * else the surface is converted in software at upload time in LoadLocation.
     */
    if(fixup == COMPLEX_FIXUP_P8 && gl_info->supported[EXT_PALETTED_TEXTURE])
        ffp_blit_p8_upload_palette(surface, gl_info);

    ENTER_GL();
    glEnable(surface->texture_target);
    checkGLcall("glEnable(surface->texture_target)");
    LEAVE_GL();
    return WINED3D_OK;
}

/* Context activation is done by the caller. */
static void ffp_blit_unset(IWineD3DDevice *iface)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *) iface;
    const struct wined3d_gl_info *gl_info = &device->adapter->gl_info;

    ENTER_GL();
    glDisable(GL_TEXTURE_2D);
    checkGLcall("glDisable(GL_TEXTURE_2D)");
    if (gl_info->supported[ARB_TEXTURE_CUBE_MAP])
    {
        glDisable(GL_TEXTURE_CUBE_MAP_ARB);
        checkGLcall("glDisable(GL_TEXTURE_CUBE_MAP_ARB)");
    }
    if (gl_info->supported[ARB_TEXTURE_RECTANGLE])
    {
        glDisable(GL_TEXTURE_RECTANGLE_ARB);
        checkGLcall("glDisable(GL_TEXTURE_RECTANGLE_ARB)");
    }
    LEAVE_GL();
}

static BOOL ffp_blit_supported(const struct wined3d_gl_info *gl_info, enum blit_operation blit_op,
        const RECT *src_rect, DWORD src_usage, WINED3DPOOL src_pool, const struct wined3d_format *src_format,
        const RECT *dst_rect, DWORD dst_usage, WINED3DPOOL dst_pool, const struct wined3d_format *dst_format)
{
    enum complex_fixup src_fixup;

    if (blit_op == BLIT_OP_COLOR_FILL)
    {
        if (!(dst_usage & WINED3DUSAGE_RENDERTARGET))
        {
            TRACE("Color fill not supported\n");
            return FALSE;
        }

        return TRUE;
    }

    src_fixup = get_complex_fixup(src_format->color_fixup);
    if (TRACE_ON(d3d_surface) && TRACE_ON(d3d))
    {
        TRACE("Checking support for fixup:\n");
        dump_color_fixup_desc(src_format->color_fixup);
    }

    if (blit_op != BLIT_OP_BLIT)
    {
        TRACE("Unsupported blit_op=%d\n", blit_op);
        return FALSE;
     }

    if (!is_identity_fixup(dst_format->color_fixup))
    {
        TRACE("Destination fixups are not supported\n");
        return FALSE;
    }

    if (src_fixup == COMPLEX_FIXUP_P8 && gl_info->supported[EXT_PALETTED_TEXTURE])
    {
        TRACE("P8 fixup supported\n");
        return TRUE;
    }

    /* We only support identity conversions. */
    if (is_identity_fixup(src_format->color_fixup))
    {
        TRACE("[OK]\n");
        return TRUE;
    }

    TRACE("[FAILED]\n");
    return FALSE;
}

/* Do not call while under the GL lock. */
static HRESULT ffp_blit_color_fill(IWineD3DDeviceImpl *device, IWineD3DSurfaceImpl *dst_surface,
        const RECT *dst_rect, const WINED3DCOLORVALUE *color)
{
    const RECT draw_rect = {0, 0, dst_surface->currentDesc.Width, dst_surface->currentDesc.Height};

    return device_clear_render_targets(device, 1 /* rt_count */, &dst_surface, 1 /* rect_count */,
            dst_rect, &draw_rect, WINED3DCLEAR_TARGET, color, 0.0f /* depth */, 0 /* stencil */);
}

const struct blit_shader ffp_blit =  {
    ffp_blit_alloc,
    ffp_blit_free,
    ffp_blit_set,
    ffp_blit_unset,
    ffp_blit_supported,
    ffp_blit_color_fill
};

static HRESULT cpu_blit_alloc(IWineD3DDevice *iface)
{
    return WINED3D_OK;
}

/* Context activation is done by the caller. */
static void cpu_blit_free(IWineD3DDevice *iface)
{
}

/* Context activation is done by the caller. */
static HRESULT cpu_blit_set(IWineD3DDevice *iface, IWineD3DSurfaceImpl *surface)
{
    return WINED3D_OK;
}

/* Context activation is done by the caller. */
static void cpu_blit_unset(IWineD3DDevice *iface)
{
}

static BOOL cpu_blit_supported(const struct wined3d_gl_info *gl_info, enum blit_operation blit_op,
        const RECT *src_rect, DWORD src_usage, WINED3DPOOL src_pool, const struct wined3d_format *src_format,
        const RECT *dst_rect, DWORD dst_usage, WINED3DPOOL dst_pool, const struct wined3d_format *dst_format)
{
    if (blit_op == BLIT_OP_COLOR_FILL)
    {
        return TRUE;
    }

    return FALSE;
}

/* Do not call while under the GL lock. */
static HRESULT cpu_blit_color_fill(IWineD3DDeviceImpl *device, IWineD3DSurfaceImpl *dst_surface,
        const RECT *dst_rect, const WINED3DCOLORVALUE *color)
{
    WINEDDBLTFX BltFx;

    memset(&BltFx, 0, sizeof(BltFx));
    BltFx.dwSize = sizeof(BltFx);
    BltFx.u5.dwFillColor = wined3d_format_convert_from_float(dst_surface->resource.format, color);
    return IWineD3DBaseSurfaceImpl_Blt((IWineD3DSurface*)dst_surface, dst_rect,
            NULL, NULL, WINEDDBLT_COLORFILL, &BltFx, WINED3DTEXF_POINT);
}

const struct blit_shader cpu_blit =  {
    cpu_blit_alloc,
    cpu_blit_free,
    cpu_blit_set,
    cpu_blit_unset,
    cpu_blit_supported,
    cpu_blit_color_fill
};

static BOOL fbo_blit_supported(const struct wined3d_gl_info *gl_info, enum blit_operation blit_op,
        const RECT *src_rect, DWORD src_usage, WINED3DPOOL src_pool, const struct wined3d_format *src_format,
        const RECT *dst_rect, DWORD dst_usage, WINED3DPOOL dst_pool, const struct wined3d_format *dst_format)
{
    if ((wined3d_settings.offscreen_rendering_mode != ORM_FBO) || !gl_info->fbo_ops.glBlitFramebuffer)
        return FALSE;

    /* We only support blitting. Things like color keying / color fill should
     * be handled by other blitters.
     */
    if (blit_op != BLIT_OP_BLIT)
        return FALSE;

    /* Source and/or destination need to be on the GL side */
    if (src_pool == WINED3DPOOL_SYSTEMMEM || dst_pool == WINED3DPOOL_SYSTEMMEM)
        return FALSE;

    if (!((src_format->flags & WINED3DFMT_FLAG_FBO_ATTACHABLE) || (src_usage & WINED3DUSAGE_RENDERTARGET))
            && ((dst_format->flags & WINED3DFMT_FLAG_FBO_ATTACHABLE) || (dst_usage & WINED3DUSAGE_RENDERTARGET)))
        return FALSE;

    if (!(src_format->id == dst_format->id
            || (is_identity_fixup(src_format->color_fixup)
            && is_identity_fixup(dst_format->color_fixup))))
        return FALSE;

    return TRUE;
}
