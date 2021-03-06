/*
 * Copyright 2014 Henri Verbeet for CodeWeavers
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
#include <math.h>
#include "d2d1.h"
#include "wincrypt.h"
#include "wine/test.h"
#include "initguid.h"
#include "dwrite.h"
#include "wincodec.h"

struct figure
{
    unsigned int *spans;
    unsigned int spans_size;
    unsigned int span_count;
};

static void set_point(D2D1_POINT_2F *point, float x, float y)
{
    point->x = x;
    point->y = y;
}

static void set_quadratic(D2D1_QUADRATIC_BEZIER_SEGMENT *quadratic, float x1, float y1, float x2, float y2)
{
    quadratic->point1.x = x1;
    quadratic->point1.y = y1;
    quadratic->point2.x = x2;
    quadratic->point2.y = y2;
}

static void set_rect(D2D1_RECT_F *rect, float left, float top, float right, float bottom)
{
    rect->left = left;
    rect->top = top;
    rect->right = right;
    rect->bottom = bottom;
}

static void set_rect_u(D2D1_RECT_U *rect, UINT32 left, UINT32 top, UINT32 right, UINT32 bottom)
{
    rect->left = left;
    rect->top = top;
    rect->right = right;
    rect->bottom = bottom;
}

static void set_color(D2D1_COLOR_F *color, float r, float g, float b, float a)
{
    color->r = r;
    color->g = g;
    color->b = b;
    color->a = a;
}

static void set_size_u(D2D1_SIZE_U *size, unsigned int w, unsigned int h)
{
    size->width = w;
    size->height = h;
}

static void set_matrix_identity(D2D1_MATRIX_3X2_F *matrix)
{
    matrix->_11 = 1.0f;
    matrix->_12 = 0.0f;
    matrix->_21 = 0.0f;
    matrix->_22 = 1.0f;
    matrix->_31 = 0.0f;
    matrix->_32 = 0.0f;
}

static void rotate_matrix(D2D1_MATRIX_3X2_F *matrix, float theta)
{
    float sin_theta, cos_theta, tmp_11, tmp_12;

    sin_theta = sinf(theta);
    cos_theta = cosf(theta);
    tmp_11 = matrix->_11;
    tmp_12 = matrix->_12;

    matrix->_11 = cos_theta * tmp_11 + sin_theta * matrix->_21;
    matrix->_12 = cos_theta * tmp_12 + sin_theta * matrix->_22;
    matrix->_21 = -sin_theta * tmp_11 + cos_theta * matrix->_21;
    matrix->_22 = -sin_theta * tmp_12 + cos_theta * matrix->_22;
}

static void scale_matrix(D2D1_MATRIX_3X2_F *matrix, float x, float y)
{
    matrix->_11 *= x;
    matrix->_12 *= x;
    matrix->_21 *= y;
    matrix->_22 *= y;
}

static void translate_matrix(D2D1_MATRIX_3X2_F *matrix, float x, float y)
{
    matrix->_31 += x * matrix->_11 + y * matrix->_21;
    matrix->_32 += x * matrix->_12 + y * matrix->_22;
}

static BOOL compare_sha1(void *data, unsigned int pitch, unsigned int bpp,
        unsigned int w, unsigned int h, const char *ref_sha1)
{
    static const char hex_chars[] = "0123456789abcdef";
    HCRYPTPROV provider;
    BYTE hash_data[20];
    HCRYPTHASH hash;
    unsigned int i;
    char sha1[41];
    BOOL ret;

    ret = CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
    ok(ret, "Failed to acquire crypt context.\n");
    ret = CryptCreateHash(provider, CALG_SHA1, 0, 0, &hash);
    ok(ret, "Failed to create hash.\n");

    for (i = 0; i < h; ++i)
    {
        if (!(ret = CryptHashData(hash, (BYTE *)data + pitch * i, w * bpp, 0)))
            break;
    }
    ok(ret, "Failed to hash data.\n");

    i = sizeof(hash_data);
    ret = CryptGetHashParam(hash, HP_HASHVAL, hash_data, &i, 0);
    ok(ret, "Failed to get hash value.\n");
    ok(i == sizeof(hash_data), "Got unexpected hash size %u.\n", i);

    ret = CryptDestroyHash(hash);
    ok(ret, "Failed to destroy hash.\n");
    ret = CryptReleaseContext(provider, 0);
    ok(ret, "Failed to release crypt context.\n");

    for (i = 0; i < 20; ++i)
    {
        sha1[i * 2] = hex_chars[hash_data[i] >> 4];
        sha1[i * 2 + 1] = hex_chars[hash_data[i] & 0xf];
    }
    sha1[40] = 0;

    return !strcmp(ref_sha1, (char *)sha1);
}

static BOOL compare_surface(IDXGISurface *surface, const char *ref_sha1)
{
    D3D10_MAPPED_TEXTURE2D mapped_texture;
    D3D10_TEXTURE2D_DESC texture_desc;
    DXGI_SURFACE_DESC surface_desc;
    ID3D10Resource *src_resource;
    ID3D10Texture2D *texture;
    ID3D10Device *device;
    HRESULT hr;
    BOOL ret;

    hr = IDXGISurface_GetDevice(surface, &IID_ID3D10Device, (void **)&device);
    ok(SUCCEEDED(hr), "Failed to get device, hr %#x.\n", hr);
    hr = IDXGISurface_QueryInterface(surface, &IID_ID3D10Resource, (void **)&src_resource);
    ok(SUCCEEDED(hr), "Failed to query resource interface, hr %#x.\n", hr);

    hr = IDXGISurface_GetDesc(surface, &surface_desc);
    ok(SUCCEEDED(hr), "Failed to get surface desc, hr %#x.\n", hr);
    texture_desc.Width = surface_desc.Width;
    texture_desc.Height = surface_desc.Height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = surface_desc.Format;
    texture_desc.SampleDesc = surface_desc.SampleDesc;
    texture_desc.Usage = D3D10_USAGE_STAGING;
    texture_desc.BindFlags = 0;
    texture_desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    texture_desc.MiscFlags = 0;
    hr = ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    ID3D10Device_CopyResource(device, (ID3D10Resource *)texture, src_resource);
    hr = ID3D10Texture2D_Map(texture, 0, D3D10_MAP_READ, 0, &mapped_texture);
    ok(SUCCEEDED(hr), "Failed to map texture, hr %#x.\n", hr);
    ret = compare_sha1(mapped_texture.pData, mapped_texture.RowPitch, 4,
            texture_desc.Width, texture_desc.Height, ref_sha1);
    ID3D10Texture2D_Unmap(texture, 0);

    ID3D10Texture2D_Release(texture);
    ID3D10Resource_Release(src_resource);
    ID3D10Device_Release(device);

    return ret;
}

static void serialize_figure(struct figure *figure)
{
    static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned int i, j, k, span;
    char output[76];
    char t[3];
    char *p;

    for (i = 0, j = 0, k = 0, p = output; i < figure->span_count; ++i)
    {
        span = figure->spans[i];
        while (span)
        {
            t[j] = span & 0x7f;
            if (span > 0x7f)
                t[j] |= 0x80;
            span >>= 7;
            if (++j == 3)
            {
                p[0] = lookup[(t[0] & 0xfc) >> 2];
                p[1] = lookup[((t[0] & 0x03) << 4) | ((t[1] & 0xf0) >> 4)];
                p[2] = lookup[((t[1] & 0x0f) << 2) | ((t[2] & 0xc0) >> 6)];
                p[3] = lookup[t[2] & 0x3f];
                p += 4;
                if (++k == 19)
                {
                    trace("%.76s\n", output);
                    p = output;
                    k = 0;
                }
                j = 0;
            }
        }
    }
    if (j)
    {
        for (i = j; i < 3; ++i)
            t[i] = 0;
        p[0] = lookup[(t[0] & 0xfc) >> 2];
        p[1] = lookup[((t[0] & 0x03) << 4) | ((t[1] & 0xf0) >> 4)];
        p[2] = lookup[((t[1] & 0x0f) << 2) | ((t[2] & 0xc0) >> 6)];
        p[3] = lookup[t[2] & 0x3f];
        ++k;
    }
    if (k)
        trace("%.*s\n", k * 4, output);
}

static void figure_add_span(struct figure *figure, unsigned int span)
{
    if (figure->span_count == figure->spans_size)
    {
        figure->spans_size *= 2;
        figure->spans = HeapReAlloc(GetProcessHeap(), 0, figure->spans,
                figure->spans_size * sizeof(*figure->spans));
    }

    figure->spans[figure->span_count++] = span;
}

static void deserialize_span(struct figure *figure, unsigned int *current, unsigned int *shift, unsigned int c)
{
    *current |= (c & 0x7f) << *shift;
    if (c & 0x80)
    {
        *shift += 7;
        return;
    }

    if (*current)
        figure_add_span(figure, *current);
    *current = 0;
    *shift = 0;
}

static void deserialize_figure(struct figure *figure, const BYTE *s)
{
    static const BYTE lookup[] =
    {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3e, 0xff, 0xff, 0xff, 0x3f,
        0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
        0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
        0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0xff, 0xff, 0xff, 0xff, 0xff,
    };
    unsigned int current = 0, shift = 0;
    const BYTE *ptr;
    BYTE x, y;

    figure->span_count = 0;
    figure->spans_size = 64;
    figure->spans = HeapAlloc(GetProcessHeap(), 0, figure->spans_size * sizeof(*figure->spans));

    for (ptr = s; *ptr; ptr += 4)
    {
        x = lookup[ptr[0]];
        y = lookup[ptr[1]];
        deserialize_span(figure, &current, &shift, ((x & 0x3f) << 2) | ((y & 0x3f) >> 4));
        x = lookup[ptr[2]];
        deserialize_span(figure, &current, &shift, ((y & 0x0f) << 4) | ((x & 0x3f) >> 2));
        y = lookup[ptr[3]];
        deserialize_span(figure, &current, &shift, ((x & 0x03) << 6) | (y & 0x3f));
    }
}

static BOOL compare_figure(IDXGISurface *surface, unsigned int x, unsigned int y,
        unsigned int w, unsigned int h, DWORD prev, unsigned int max_diff, const char *ref)
{
    D3D10_MAPPED_TEXTURE2D mapped_texture;
    D3D10_TEXTURE2D_DESC texture_desc;
    struct figure ref_figure, figure;
    DXGI_SURFACE_DESC surface_desc;
    unsigned int i, j, span, diff;
    ID3D10Resource *src_resource;
    ID3D10Texture2D *texture;
    ID3D10Device *device;
    HRESULT hr;

    hr = IDXGISurface_GetDevice(surface, &IID_ID3D10Device, (void **)&device);
    ok(SUCCEEDED(hr), "Failed to get device, hr %#x.\n", hr);
    hr = IDXGISurface_QueryInterface(surface, &IID_ID3D10Resource, (void **)&src_resource);
    ok(SUCCEEDED(hr), "Failed to query resource interface, hr %#x.\n", hr);

    hr = IDXGISurface_GetDesc(surface, &surface_desc);
    ok(SUCCEEDED(hr), "Failed to get surface desc, hr %#x.\n", hr);
    texture_desc.Width = surface_desc.Width;
    texture_desc.Height = surface_desc.Height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = surface_desc.Format;
    texture_desc.SampleDesc = surface_desc.SampleDesc;
    texture_desc.Usage = D3D10_USAGE_STAGING;
    texture_desc.BindFlags = 0;
    texture_desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    texture_desc.MiscFlags = 0;
    hr = ID3D10Device_CreateTexture2D(device, &texture_desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    ID3D10Device_CopyResource(device, (ID3D10Resource *)texture, src_resource);
    hr = ID3D10Texture2D_Map(texture, 0, D3D10_MAP_READ, 0, &mapped_texture);
    ok(SUCCEEDED(hr), "Failed to map texture, hr %#x.\n", hr);

    figure.span_count = 0;
    figure.spans_size = 64;
    figure.spans = HeapAlloc(GetProcessHeap(), 0, figure.spans_size * sizeof(*figure.spans));

    for (i = 0, span = 0; i < h; ++i)
    {
        const DWORD *row = (DWORD *)((BYTE *)mapped_texture.pData + (y + i) * mapped_texture.RowPitch + x * 4);
        for (j = 0; j < w; ++j, ++span)
        {
            if ((i || j) && prev != row[j])
            {
                figure_add_span(&figure, span);
                prev = row[j];
                span = 0;
            }
        }
    }
    if (span)
        figure_add_span(&figure, span);

    deserialize_figure(&ref_figure, (BYTE *)ref);
    span = w * h;
    for (i = 0; i < ref_figure.span_count; ++i)
    {
        span -= ref_figure.spans[i];
    }
    if (span)
        figure_add_span(&ref_figure, span);

    for (i = 0, j = 0, diff = 0; i < figure.span_count && j < ref_figure.span_count;)
    {
        if (figure.spans[i] == ref_figure.spans[j])
        {
            if ((i ^ j) & 1)
                diff += ref_figure.spans[j];
            ++i;
            ++j;
        }
        else if (figure.spans[i] > ref_figure.spans[j])
        {
            if ((i ^ j) & 1)
                diff += ref_figure.spans[j];
            figure.spans[i] -= ref_figure.spans[j];
            ++j;
        }
        else
        {
            if ((i ^ j) & 1)
                diff += figure.spans[i];
            ref_figure.spans[j] -= figure.spans[i];
            ++i;
        }
    }
    if (diff > max_diff)
        serialize_figure(&figure);

    HeapFree(GetProcessHeap(), 0, ref_figure.spans);
    HeapFree(GetProcessHeap(), 0, figure.spans);
    ID3D10Texture2D_Unmap(texture, 0);

    ID3D10Texture2D_Release(texture);
    ID3D10Resource_Release(src_resource);
    ID3D10Device_Release(device);

    return diff <= max_diff;
}

static ID3D10Device1 *create_device(void)
{
    ID3D10Device1 *device;

    if (SUCCEEDED(D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL,
            D3D10_CREATE_DEVICE_BGRA_SUPPORT, D3D10_FEATURE_LEVEL_10_0, D3D10_1_SDK_VERSION, &device)))
        return device;
    if (SUCCEEDED(D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_WARP, NULL,
            D3D10_CREATE_DEVICE_BGRA_SUPPORT, D3D10_FEATURE_LEVEL_10_0, D3D10_1_SDK_VERSION, &device)))
        return device;
    if (SUCCEEDED(D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_REFERENCE, NULL,
            D3D10_CREATE_DEVICE_BGRA_SUPPORT, D3D10_FEATURE_LEVEL_10_0, D3D10_1_SDK_VERSION, &device)))
        return device;

    return NULL;
}

static IDXGISwapChain *create_swapchain(ID3D10Device1 *device, HWND window, BOOL windowed)
{
    IDXGISwapChain *swapchain;
    DXGI_SWAP_CHAIN_DESC desc;
    IDXGIDevice *dxgi_device;
    IDXGIAdapter *adapter;
    IDXGIFactory *factory;
    HRESULT hr;

    hr = ID3D10Device1_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    ok(SUCCEEDED(hr), "Failed to get DXGI device, hr %#x.\n", hr);
    hr = IDXGIDevice_GetAdapter(dxgi_device, &adapter);
    ok(SUCCEEDED(hr), "Failed to get adapter, hr %#x.\n", hr);
    IDXGIDevice_Release(dxgi_device);
    hr = IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory, (void **)&factory);
    ok(SUCCEEDED(hr), "Failed to get factory, hr %#x.\n", hr);
    IDXGIAdapter_Release(adapter);

    desc.BufferDesc.Width = 640;
    desc.BufferDesc.Height = 480;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = windowed;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    desc.Flags = 0;

    hr = IDXGIFactory_CreateSwapChain(factory, (IUnknown *)device, &desc, &swapchain);
    ok(SUCCEEDED(hr), "Failed to create swapchain, hr %#x.\n", hr);
    IDXGIFactory_Release(factory);

    return swapchain;
}

static ID2D1RenderTarget *create_render_target_desc(IDXGISurface *surface, const D2D1_RENDER_TARGET_PROPERTIES *desc)
{
    ID2D1RenderTarget *render_target;
    ID2D1Factory *factory;
    HRESULT hr;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&factory);
    ok(SUCCEEDED(hr), "Failed to create factory, hr %#x.\n", hr);
    hr = ID2D1Factory_CreateDxgiSurfaceRenderTarget(factory, surface, desc, &render_target);
    ok(SUCCEEDED(hr), "Failed to create render target, hr %#x.\n", hr);
    ID2D1Factory_Release(factory);

    return render_target;
}

static ID2D1RenderTarget *create_render_target(IDXGISurface *surface)
{
    D2D1_RENDER_TARGET_PROPERTIES desc;

    desc.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    desc.pixelFormat.format = DXGI_FORMAT_UNKNOWN;
    desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    desc.dpiX = 0.0f;
    desc.dpiY = 0.0f;
    desc.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    desc.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    return create_render_target_desc(surface, &desc);
}

static void test_clip(void)
{
    IDXGISwapChain *swapchain;
    D2D1_MATRIX_3X2_F matrix;
    D2D1_SIZE_U pixel_size;
    ID2D1RenderTarget *rt;
    ID3D10Device1 *device;
    IDXGISurface *surface;
    D2D1_POINT_2F point;
    D2D1_COLOR_F color;
    float dpi_x, dpi_y;
    D2D1_RECT_F rect;
    D2D1_SIZE_F size;
    HWND window;
    HRESULT hr;
    BOOL match;
    static const D2D1_MATRIX_3X2_F identity =
    {
        1.0f, 0.0f,
        0.0f, 1.0f,
        0.0f, 0.0f,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d2d1_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_IDXGISurface, (void **)&surface);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);
    rt = create_render_target(surface);
    ok(!!rt, "Failed to create render target.\n");

    ID2D1RenderTarget_GetDpi(rt, &dpi_x, &dpi_y);
    ok(dpi_x == 96.0f, "Got unexpected dpi_x %.8e.\n", dpi_x);
    ok(dpi_y == 96.0f, "Got unexpected dpi_x %.8e.\n", dpi_y);
    size = ID2D1RenderTarget_GetSize(rt);
    ok(size.width == 640.0f, "Got unexpected width %.8e.\n", size.width);
    ok(size.height == 480.0f, "Got unexpected height %.8e.\n", size.height);
    pixel_size = ID2D1RenderTarget_GetPixelSize(rt);
    ok(pixel_size.width == 640, "Got unexpected width %u.\n", pixel_size.width);
    ok(pixel_size.height == 480, "Got unexpected height %u.\n", pixel_size.height);

    ID2D1RenderTarget_GetTransform(rt, &matrix);
    ok(!memcmp(&matrix, &identity, sizeof(matrix)),
            "Got unexpected matrix {%.8e, %.8e, %.8e, %.8e, %.8e, %.8e}.\n",
            matrix._11, matrix._12, matrix._21, matrix._22, matrix._31, matrix._32);

    ID2D1RenderTarget_BeginDraw(rt);

    set_color(&color, 1.0f, 1.0f, 0.0f, 1.0f);
    ID2D1RenderTarget_Clear(rt, &color);

    ID2D1RenderTarget_SetDpi(rt, 48.0f, 192.0f);
    ID2D1RenderTarget_GetDpi(rt, &dpi_x, &dpi_y);
    ok(dpi_x == 48.0f, "Got unexpected dpi_x %.8e.\n", dpi_x);
    ok(dpi_y == 192.0f, "Got unexpected dpi_x %.8e.\n", dpi_y);
    size = ID2D1RenderTarget_GetSize(rt);
    ok(size.width == 1280.0f, "Got unexpected width %.8e.\n", size.width);
    ok(size.height == 240.0f, "Got unexpected height %.8e.\n", size.height);
    pixel_size = ID2D1RenderTarget_GetPixelSize(rt);
    ok(pixel_size.width == 640, "Got unexpected width %u.\n", pixel_size.width);
    ok(pixel_size.height == 480, "Got unexpected height %u.\n", pixel_size.height);

    /* The effective clip rect is the intersection of all currently pushed
     * clip rects. Clip rects are in DIPs. */
    set_rect(&rect, 0.0f, 0.0f, 1280.0f, 80.0f);
    ID2D1RenderTarget_PushAxisAlignedClip(rt, &rect, D2D1_ANTIALIAS_MODE_ALIASED);
    set_rect(&rect, 0.0f, 0.0f, 426.0f, 240.0f);
    ID2D1RenderTarget_PushAxisAlignedClip(rt, &rect, D2D1_ANTIALIAS_MODE_ALIASED);

    set_color(&color, 0.0f, 1.0f, 0.0f, 1.0f);
    ID2D1RenderTarget_Clear(rt, &color);
    ID2D1RenderTarget_PopAxisAlignedClip(rt);
    ID2D1RenderTarget_PopAxisAlignedClip(rt);

    ID2D1RenderTarget_SetDpi(rt, 0.0f, 0.0f);
    ID2D1RenderTarget_GetDpi(rt, &dpi_x, &dpi_y);
    ok(dpi_x == 96.0f, "Got unexpected dpi_x %.8e.\n", dpi_x);
    ok(dpi_y == 96.0f, "Got unexpected dpi_y %.8e.\n", dpi_y);

    /* Transformations apply to clip rects, the effective clip rect is the
     * (axis-aligned) bounding box of the transformed clip rect. */
    set_point(&point, 320.0f, 240.0f);
    D2D1MakeRotateMatrix(30.0f, point, &matrix);
    ID2D1RenderTarget_SetTransform(rt, &matrix);
    set_rect(&rect, 215.0f, 208.0f, 425.0f, 272.0f);
    ID2D1RenderTarget_PushAxisAlignedClip(rt, &rect, D2D1_ANTIALIAS_MODE_ALIASED);
    set_color(&color, 1.0f, 1.0f, 1.0f, 1.0f);
    ID2D1RenderTarget_Clear(rt, &color);
    ID2D1RenderTarget_PopAxisAlignedClip(rt);

    /* Transformations are applied when pushing the clip rect, transformations
     * set afterwards have no effect on the current clip rect. This includes
     * SetDpi(). */
    ID2D1RenderTarget_SetTransform(rt, &identity);
    set_rect(&rect, 427.0f, 320.0f, 640.0f, 480.0f);
    ID2D1RenderTarget_PushAxisAlignedClip(rt, &rect, D2D1_ANTIALIAS_MODE_ALIASED);
    ID2D1RenderTarget_SetTransform(rt, &matrix);
    ID2D1RenderTarget_SetDpi(rt, 48.0f, 192.0f);
    set_color(&color, 1.0f, 0.0f, 0.0f, 1.0f);
    ID2D1RenderTarget_Clear(rt, &color);
    ID2D1RenderTarget_PopAxisAlignedClip(rt);

    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "035a44d4198d6e422e9de6185b5b2c2bac5e33c9");
    ok(match, "Surface does not match.\n");

    ID2D1RenderTarget_Release(rt);
    IDXGISurface_Release(surface);
    IDXGISwapChain_Release(swapchain);
    ID3D10Device1_Release(device);
    DestroyWindow(window);
}

static void test_state_block(void)
{
    IDWriteRenderingParams *text_rendering_params1, *text_rendering_params2;
    D2D1_DRAWING_STATE_DESCRIPTION drawing_state;
    ID2D1DrawingStateBlock *state_block;
    IDWriteFactory *dwrite_factory;
    IDXGISwapChain *swapchain;
    ID2D1RenderTarget *rt;
    ID3D10Device1 *device;
    IDXGISurface *surface;
    ID2D1Factory *factory;
    ULONG refcount;
    HWND window;
    HRESULT hr;
    static const D2D1_MATRIX_3X2_F identity =
    {
        1.0f, 0.0f,
        0.0f, 1.0f,
        0.0f, 0.0f,
    };
    static const D2D1_MATRIX_3X2_F transform1 =
    {
        1.0f, 2.0f,
        3.0f, 4.0f,
        5.0f, 6.0f,
    };
    static const D2D1_MATRIX_3X2_F transform2 =
    {
        7.0f,  8.0f,
        9.0f,  10.0f,
        11.0f, 12.0f,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d2d1_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_IDXGISurface, (void **)&surface);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);
    rt = create_render_target(surface);
    ok(!!rt, "Failed to create render target.\n");
    ID2D1RenderTarget_GetFactory(rt, &factory);
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **)&dwrite_factory);
    ok(SUCCEEDED(hr), "Failed to create dwrite factory, hr %#x.\n", hr);
    hr = IDWriteFactory_CreateRenderingParams(dwrite_factory, &text_rendering_params1);
    ok(SUCCEEDED(hr), "Failed to create dwrite rendering params, hr %#x.\n", hr);
    IDWriteFactory_Release(dwrite_factory);

    drawing_state.antialiasMode = ID2D1RenderTarget_GetAntialiasMode(rt);
    ok(drawing_state.antialiasMode == D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            "Got unexpected antialias mode %#x.\n", drawing_state.antialiasMode);
    drawing_state.textAntialiasMode = ID2D1RenderTarget_GetTextAntialiasMode(rt);
    ok(drawing_state.textAntialiasMode == D2D1_TEXT_ANTIALIAS_MODE_DEFAULT,
            "Got unexpected text antialias mode %#x.\n", drawing_state.textAntialiasMode);
    ID2D1RenderTarget_GetTags(rt, &drawing_state.tag1, &drawing_state.tag2);
    ok(!drawing_state.tag1 && !drawing_state.tag2, "Got unexpected tags %08x%08x:%08x%08x.\n",
            (unsigned int)(drawing_state.tag1 >> 32), (unsigned int)(drawing_state.tag1),
            (unsigned int)(drawing_state.tag2 >> 32), (unsigned int)(drawing_state.tag2));
    ID2D1RenderTarget_GetTransform(rt, &drawing_state.transform);
    ok(!memcmp(&drawing_state.transform, &identity, sizeof(drawing_state.transform)),
            "Got unexpected matrix {%.8e, %.8e, %.8e, %.8e, %.8e, %.8e}.\n",
            drawing_state.transform._11, drawing_state.transform._12, drawing_state.transform._21,
            drawing_state.transform._22, drawing_state.transform._31, drawing_state.transform._32);
    ID2D1RenderTarget_GetTextRenderingParams(rt, &text_rendering_params2);
    ok(!text_rendering_params2, "Got unexpected text rendering params %p.\n", text_rendering_params2);

    hr = ID2D1Factory_CreateDrawingStateBlock(factory, NULL, NULL, &state_block);
    ok(SUCCEEDED(hr), "Failed to create drawing state block, hr %#x\n", hr);
    ID2D1DrawingStateBlock_GetDescription(state_block, &drawing_state);
    ok(drawing_state.antialiasMode == D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            "Got unexpected antialias mode %#x.\n", drawing_state.antialiasMode);
    ok(drawing_state.textAntialiasMode == D2D1_TEXT_ANTIALIAS_MODE_DEFAULT,
            "Got unexpected text antialias mode %#x.\n", drawing_state.textAntialiasMode);
    ok(!drawing_state.tag1 && !drawing_state.tag2, "Got unexpected tags %08x%08x:%08x%08x.\n",
            (unsigned int)(drawing_state.tag1 >> 32), (unsigned int)(drawing_state.tag1),
            (unsigned int)(drawing_state.tag2 >> 32), (unsigned int)(drawing_state.tag2));
    ok(!memcmp(&drawing_state.transform, &identity, sizeof(drawing_state.transform)),
            "Got unexpected matrix {%.8e, %.8e, %.8e, %.8e, %.8e, %.8e}.\n",
            drawing_state.transform._11, drawing_state.transform._12, drawing_state.transform._21,
            drawing_state.transform._22, drawing_state.transform._31, drawing_state.transform._32);
    ID2D1DrawingStateBlock_GetTextRenderingParams(state_block, &text_rendering_params2);
    ok(!text_rendering_params2, "Got unexpected text rendering params %p.\n", text_rendering_params2);
    ID2D1DrawingStateBlock_Release(state_block);

    drawing_state.antialiasMode = D2D1_ANTIALIAS_MODE_ALIASED;
    drawing_state.textAntialiasMode = D2D1_TEXT_ANTIALIAS_MODE_ALIASED;
    drawing_state.tag1 = 0xdead;
    drawing_state.tag2 = 0xbeef;
    drawing_state.transform = transform1;
    hr = ID2D1Factory_CreateDrawingStateBlock(factory, &drawing_state, text_rendering_params1, &state_block);
    ok(SUCCEEDED(hr), "Failed to create drawing state block, hr %#x\n", hr);

    ID2D1DrawingStateBlock_GetDescription(state_block, &drawing_state);
    ok(drawing_state.antialiasMode == D2D1_ANTIALIAS_MODE_ALIASED,
            "Got unexpected antialias mode %#x.\n", drawing_state.antialiasMode);
    ok(drawing_state.textAntialiasMode == D2D1_TEXT_ANTIALIAS_MODE_ALIASED,
            "Got unexpected text antialias mode %#x.\n", drawing_state.textAntialiasMode);
    ok(drawing_state.tag1 == 0xdead && drawing_state.tag2 == 0xbeef, "Got unexpected tags %08x%08x:%08x%08x.\n",
            (unsigned int)(drawing_state.tag1 >> 32), (unsigned int)(drawing_state.tag1),
            (unsigned int)(drawing_state.tag2 >> 32), (unsigned int)(drawing_state.tag2));
    ok(!memcmp(&drawing_state.transform, &transform1, sizeof(drawing_state.transform)),
            "Got unexpected matrix {%.8e, %.8e, %.8e, %.8e, %.8e, %.8e}.\n",
            drawing_state.transform._11, drawing_state.transform._12, drawing_state.transform._21,
            drawing_state.transform._22, drawing_state.transform._31, drawing_state.transform._32);
    ID2D1DrawingStateBlock_GetTextRenderingParams(state_block, &text_rendering_params2);
    ok(text_rendering_params2 == text_rendering_params1, "Got unexpected text rendering params %p, expected %p.\n",
            text_rendering_params2, text_rendering_params1);
    IDWriteRenderingParams_Release(text_rendering_params2);

    ID2D1RenderTarget_RestoreDrawingState(rt, state_block);

    drawing_state.antialiasMode = ID2D1RenderTarget_GetAntialiasMode(rt);
    ok(drawing_state.antialiasMode == D2D1_ANTIALIAS_MODE_ALIASED,
            "Got unexpected antialias mode %#x.\n", drawing_state.antialiasMode);
    drawing_state.textAntialiasMode = ID2D1RenderTarget_GetTextAntialiasMode(rt);
    ok(drawing_state.textAntialiasMode == D2D1_TEXT_ANTIALIAS_MODE_ALIASED,
            "Got unexpected text antialias mode %#x.\n", drawing_state.textAntialiasMode);
    ID2D1RenderTarget_GetTags(rt, &drawing_state.tag1, &drawing_state.tag2);
    ok(drawing_state.tag1 == 0xdead && drawing_state.tag2 == 0xbeef, "Got unexpected tags %08x%08x:%08x%08x.\n",
            (unsigned int)(drawing_state.tag1 >> 32), (unsigned int)(drawing_state.tag1),
            (unsigned int)(drawing_state.tag2 >> 32), (unsigned int)(drawing_state.tag2));
    ID2D1RenderTarget_GetTransform(rt, &drawing_state.transform);
    ok(!memcmp(&drawing_state.transform, &transform1, sizeof(drawing_state.transform)),
            "Got unexpected matrix {%.8e, %.8e, %.8e, %.8e, %.8e, %.8e}.\n",
            drawing_state.transform._11, drawing_state.transform._12, drawing_state.transform._21,
            drawing_state.transform._22, drawing_state.transform._31, drawing_state.transform._32);
    ID2D1RenderTarget_GetTextRenderingParams(rt, &text_rendering_params2);
    ok(text_rendering_params2 == text_rendering_params1, "Got unexpected text rendering params %p, expected %p.\n",
            text_rendering_params2, text_rendering_params1);
    IDWriteRenderingParams_Release(text_rendering_params2);

    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ID2D1RenderTarget_SetTextAntialiasMode(rt, D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    ID2D1RenderTarget_SetTags(rt, 1, 2);
    ID2D1RenderTarget_SetTransform(rt, &transform2);
    ID2D1RenderTarget_SetTextRenderingParams(rt, NULL);

    drawing_state.antialiasMode = ID2D1RenderTarget_GetAntialiasMode(rt);
    ok(drawing_state.antialiasMode == D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            "Got unexpected antialias mode %#x.\n", drawing_state.antialiasMode);
    drawing_state.textAntialiasMode = ID2D1RenderTarget_GetTextAntialiasMode(rt);
    ok(drawing_state.textAntialiasMode == D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE,
            "Got unexpected text antialias mode %#x.\n", drawing_state.textAntialiasMode);
    ID2D1RenderTarget_GetTags(rt, &drawing_state.tag1, &drawing_state.tag2);
    ok(drawing_state.tag1 == 1 && drawing_state.tag2 == 2, "Got unexpected tags %08x%08x:%08x%08x.\n",
            (unsigned int)(drawing_state.tag1 >> 32), (unsigned int)(drawing_state.tag1),
            (unsigned int)(drawing_state.tag2 >> 32), (unsigned int)(drawing_state.tag2));
    ID2D1RenderTarget_GetTransform(rt, &drawing_state.transform);
    ok(!memcmp(&drawing_state.transform, &transform2, sizeof(drawing_state.transform)),
            "Got unexpected matrix {%.8e, %.8e, %.8e, %.8e, %.8e, %.8e}.\n",
            drawing_state.transform._11, drawing_state.transform._12, drawing_state.transform._21,
            drawing_state.transform._22, drawing_state.transform._31, drawing_state.transform._32);
    ID2D1RenderTarget_GetTextRenderingParams(rt, &text_rendering_params2);
    ok(!text_rendering_params2, "Got unexpected text rendering params %p.\n", text_rendering_params2);

    ID2D1RenderTarget_SaveDrawingState(rt, state_block);

    ID2D1DrawingStateBlock_GetDescription(state_block, &drawing_state);
    ok(drawing_state.antialiasMode == D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            "Got unexpected antialias mode %#x.\n", drawing_state.antialiasMode);
    ok(drawing_state.textAntialiasMode == D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE,
            "Got unexpected text antialias mode %#x.\n", drawing_state.textAntialiasMode);
    ok(drawing_state.tag1 == 1 && drawing_state.tag2 == 2, "Got unexpected tags %08x%08x:%08x%08x.\n",
            (unsigned int)(drawing_state.tag1 >> 32), (unsigned int)(drawing_state.tag1),
            (unsigned int)(drawing_state.tag2 >> 32), (unsigned int)(drawing_state.tag2));
    ok(!memcmp(&drawing_state.transform, &transform2, sizeof(drawing_state.transform)),
            "Got unexpected matrix {%.8e, %.8e, %.8e, %.8e, %.8e, %.8e}.\n",
            drawing_state.transform._11, drawing_state.transform._12, drawing_state.transform._21,
            drawing_state.transform._22, drawing_state.transform._31, drawing_state.transform._32);
    ID2D1DrawingStateBlock_GetTextRenderingParams(state_block, &text_rendering_params2);
    ok(!text_rendering_params2, "Got unexpected text rendering params %p.\n", text_rendering_params2);

    drawing_state.antialiasMode = D2D1_ANTIALIAS_MODE_ALIASED;
    drawing_state.textAntialiasMode = D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE;
    drawing_state.tag1 = 3;
    drawing_state.tag2 = 4;
    drawing_state.transform = transform1;
    ID2D1DrawingStateBlock_SetDescription(state_block, &drawing_state);
    ID2D1DrawingStateBlock_SetTextRenderingParams(state_block, text_rendering_params1);

    ID2D1DrawingStateBlock_GetDescription(state_block, &drawing_state);
    ok(drawing_state.antialiasMode == D2D1_ANTIALIAS_MODE_ALIASED,
            "Got unexpected antialias mode %#x.\n", drawing_state.antialiasMode);
    ok(drawing_state.textAntialiasMode == D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE,
            "Got unexpected text antialias mode %#x.\n", drawing_state.textAntialiasMode);
    ok(drawing_state.tag1 == 3 && drawing_state.tag2 == 4, "Got unexpected tags %08x%08x:%08x%08x.\n",
            (unsigned int)(drawing_state.tag1 >> 32), (unsigned int)(drawing_state.tag1),
            (unsigned int)(drawing_state.tag2 >> 32), (unsigned int)(drawing_state.tag2));
    ok(!memcmp(&drawing_state.transform, &transform1, sizeof(drawing_state.transform)),
            "Got unexpected matrix {%.8e, %.8e, %.8e, %.8e, %.8e, %.8e}.\n",
            drawing_state.transform._11, drawing_state.transform._12, drawing_state.transform._21,
            drawing_state.transform._22, drawing_state.transform._31, drawing_state.transform._32);
    ID2D1DrawingStateBlock_GetTextRenderingParams(state_block, &text_rendering_params2);
    ok(text_rendering_params2 == text_rendering_params1, "Got unexpected text rendering params %p, expected %p.\n",
            text_rendering_params2, text_rendering_params1);
    IDWriteRenderingParams_Release(text_rendering_params2);

    ID2D1DrawingStateBlock_Release(state_block);

    refcount = IDWriteRenderingParams_Release(text_rendering_params1);
    ok(!refcount, "Rendering params %u references left.\n", refcount);
    ID2D1Factory_Release(factory);
    ID2D1RenderTarget_Release(rt);
    IDXGISurface_Release(surface);
    IDXGISwapChain_Release(swapchain);
    ID3D10Device1_Release(device);
    DestroyWindow(window);
}

static void test_color_brush(void)
{
    D2D1_MATRIX_3X2_F matrix, tmp_matrix;
    D2D1_BRUSH_PROPERTIES brush_desc;
    D2D1_COLOR_F color, tmp_color;
    ID2D1SolidColorBrush *brush;
    IDXGISwapChain *swapchain;
    ID2D1RenderTarget *rt;
    ID3D10Device1 *device;
    IDXGISurface *surface;
    D2D1_RECT_F rect;
    float opacity;
    HWND window;
    HRESULT hr;
    BOOL match;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d2d1_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_IDXGISurface, (void **)&surface);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);
    rt = create_render_target(surface);
    ok(!!rt, "Failed to create render target.\n");

    ID2D1RenderTarget_SetDpi(rt, 192.0f, 48.0f);
    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_ALIASED);

    set_color(&color, 0.0f, 0.0f, 0.0f, 0.0f);
    hr = ID2D1RenderTarget_CreateSolidColorBrush(rt, &color, NULL, &brush);
    ok(SUCCEEDED(hr), "Failed to create brush, hr %#x.\n", hr);
    opacity = ID2D1SolidColorBrush_GetOpacity(brush);
    ok(opacity == 1.0f, "Got unexpected opacity %.8e.\n", opacity);
    set_matrix_identity(&matrix);
    ID2D1SolidColorBrush_GetTransform(brush, &tmp_matrix);
    ok(!memcmp(&tmp_matrix, &matrix, sizeof(matrix)),
            "Got unexpected matrix {%.8e, %.8e, %.8e, %.8e, %.8e, %.8e}.\n",
            tmp_matrix._11, tmp_matrix._12, tmp_matrix._21,
            tmp_matrix._22, tmp_matrix._31, tmp_matrix._32);
    tmp_color = ID2D1SolidColorBrush_GetColor(brush);
    ok(!memcmp(&tmp_color, &color, sizeof(color)),
            "Got unexpected color {%.8e, %.8e, %.8e, %.8e}.\n",
            tmp_color.r, tmp_color.g, tmp_color.b, tmp_color.a);
    ID2D1SolidColorBrush_Release(brush);

    set_color(&color, 0.0f, 1.0f, 0.0f, 0.8f);
    brush_desc.opacity = 0.3f;
    set_matrix_identity(&matrix);
    scale_matrix(&matrix, 2.0f, 2.0f);
    brush_desc.transform = matrix;
    hr = ID2D1RenderTarget_CreateSolidColorBrush(rt, &color, &brush_desc, &brush);
    ok(SUCCEEDED(hr), "Failed to create brush, hr %#x.\n", hr);
    opacity = ID2D1SolidColorBrush_GetOpacity(brush);
    ok(opacity == 0.3f, "Got unexpected opacity %.8e.\n", opacity);
    ID2D1SolidColorBrush_GetTransform(brush, &tmp_matrix);
    ok(!memcmp(&tmp_matrix, &matrix, sizeof(matrix)),
            "Got unexpected matrix {%.8e, %.8e, %.8e, %.8e, %.8e, %.8e}.\n",
            tmp_matrix._11, tmp_matrix._12, tmp_matrix._21,
            tmp_matrix._22, tmp_matrix._31, tmp_matrix._32);
    tmp_color = ID2D1SolidColorBrush_GetColor(brush);
    ok(!memcmp(&tmp_color, &color, sizeof(color)),
            "Got unexpected color {%.8e, %.8e, %.8e, %.8e}.\n",
            tmp_color.r, tmp_color.g, tmp_color.b, tmp_color.a);

    ID2D1RenderTarget_BeginDraw(rt);

    set_color(&color, 0.0f, 0.0f, 1.0f, 1.0f);
    ID2D1RenderTarget_Clear(rt, &color);

    ID2D1SolidColorBrush_SetOpacity(brush, 1.0f);
    set_rect(&rect, 40.0f, 120.0f, 120.0f, 360.0f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)brush);

    set_matrix_identity(&matrix);
    scale_matrix(&matrix, 0.5f, 2.0f);
    translate_matrix(&matrix, 320.0f, 240.0f);
    rotate_matrix(&matrix, M_PI / 4.0f);
    ID2D1RenderTarget_SetTransform(rt, &matrix);
    set_color(&color, 1.0f, 0.0f, 0.0f, 0.625f);
    ID2D1SolidColorBrush_SetColor(brush, &color);
    ID2D1SolidColorBrush_SetOpacity(brush, 0.75f);
    set_rect(&rect, -80.0f, -60.0f, 80.0f, 60.0f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)brush);

    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "6d1218fca5e21fb7e287b3a439d60dbc251f5ceb");
    ok(match, "Surface does not match.\n");

    ID2D1SolidColorBrush_Release(brush);
    ID2D1RenderTarget_Release(rt);
    IDXGISurface_Release(surface);
    IDXGISwapChain_Release(swapchain);
    ID3D10Device1_Release(device);
    DestroyWindow(window);
}

static void test_bitmap_brush(void)
{
    D2D1_BITMAP_INTERPOLATION_MODE interpolation_mode;
    D2D1_MATRIX_3X2_F matrix, tmp_matrix;
    D2D1_BITMAP_PROPERTIES bitmap_desc;
    ID2D1Bitmap *bitmap, *tmp_bitmap;
    D2D1_RECT_F src_rect, dst_rect;
    D2D1_EXTEND_MODE extend_mode;
    IDXGISwapChain *swapchain;
    ID2D1BitmapBrush *brush;
    ID2D1RenderTarget *rt;
    ID3D10Device1 *device;
    IDXGISurface *surface;
    D2D1_COLOR_F color;
    D2D1_SIZE_U size;
    unsigned int i;
    ULONG refcount;
    float opacity;
    HWND window;
    HRESULT hr;
    BOOL match;

    static const struct
    {
        D2D1_EXTEND_MODE extend_mode_x;
        D2D1_EXTEND_MODE extend_mode_y;
        float translate_x;
        float translate_y;
        D2D1_RECT_F rect;
    }
    extend_mode_tests[] =
    {
        {D2D1_EXTEND_MODE_MIRROR, D2D1_EXTEND_MODE_MIRROR, -7.0f, 1.0f, {-4.0f,  0.0f, -8.0f,  4.0f}},
        {D2D1_EXTEND_MODE_WRAP,   D2D1_EXTEND_MODE_MIRROR, -3.0f, 1.0f, {-4.0f,  4.0f,  0.0f,  0.0f}},
        {D2D1_EXTEND_MODE_CLAMP,  D2D1_EXTEND_MODE_MIRROR,  1.0f, 1.0f, { 4.0f,  0.0f,  0.0f,  4.0f}},
        {D2D1_EXTEND_MODE_MIRROR, D2D1_EXTEND_MODE_WRAP,   -7.0f, 5.0f, {-8.0f,  8.0f, -4.0f,  4.0f}},
        {D2D1_EXTEND_MODE_WRAP,   D2D1_EXTEND_MODE_WRAP,   -3.0f, 5.0f, { 0.0f,  4.0f, -4.0f,  8.0f}},
        {D2D1_EXTEND_MODE_CLAMP,  D2D1_EXTEND_MODE_WRAP,    1.0f, 5.0f, { 0.0f,  8.0f,  4.0f,  4.0f}},
        {D2D1_EXTEND_MODE_MIRROR, D2D1_EXTEND_MODE_CLAMP,  -7.0f, 9.0f, {-4.0f,  8.0f, -8.0f, 12.0f}},
        {D2D1_EXTEND_MODE_WRAP,   D2D1_EXTEND_MODE_CLAMP,  -3.0f, 9.0f, {-4.0f, 12.0f,  0.0f,  8.0f}},
        {D2D1_EXTEND_MODE_CLAMP,  D2D1_EXTEND_MODE_CLAMP,   1.0f, 9.0f, { 4.0f,  8.0f,  0.0f, 12.0f}},
    };
    static const DWORD bitmap_data[] =
    {
        0xffff0000, 0xffffff00, 0xff00ff00, 0xff00ffff,
        0xff0000ff, 0xffff00ff, 0xff000000, 0xff7f7f7f,
        0xffffffff, 0xffffffff, 0xffffffff, 0xff000000,
        0xffffffff, 0xff000000, 0xff000000, 0xff000000,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d2d1_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_IDXGISurface, (void **)&surface);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);
    rt = create_render_target(surface);
    ok(!!rt, "Failed to create render target.\n");

    ID2D1RenderTarget_SetDpi(rt, 192.0f, 48.0f);
    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_ALIASED);

    set_size_u(&size, 4, 4);
    bitmap_desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    bitmap_desc.dpiX = 96.0f;
    bitmap_desc.dpiY = 96.0f;
    hr = ID2D1RenderTarget_CreateBitmap(rt, size, bitmap_data, 4 * sizeof(*bitmap_data), &bitmap_desc, &bitmap);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);

    /* Creating a brush with a NULL bitmap crashes on Vista, but works fine on
     * Windows 7+. */
    hr = ID2D1RenderTarget_CreateBitmapBrush(rt, bitmap, NULL, NULL, &brush);
    ok(SUCCEEDED(hr), "Failed to create brush, hr %#x.\n", hr);
    ID2D1BitmapBrush_GetBitmap(brush, &tmp_bitmap);
    ok(tmp_bitmap == bitmap, "Got unexpected bitmap %p, expected %p.\n", tmp_bitmap, bitmap);
    ID2D1Bitmap_Release(tmp_bitmap);
    opacity = ID2D1BitmapBrush_GetOpacity(brush);
    ok(opacity == 1.0f, "Got unexpected opacity %.8e.\n", opacity);
    set_matrix_identity(&matrix);
    ID2D1BitmapBrush_GetTransform(brush, &tmp_matrix);
    ok(!memcmp(&tmp_matrix, &matrix, sizeof(matrix)),
            "Got unexpected matrix {%.8e, %.8e, %.8e, %.8e, %.8e, %.8e}.\n",
            tmp_matrix._11, tmp_matrix._12, tmp_matrix._21,
            tmp_matrix._22, tmp_matrix._31, tmp_matrix._32);
    extend_mode = ID2D1BitmapBrush_GetExtendModeX(brush);
    ok(extend_mode == D2D1_EXTEND_MODE_CLAMP, "Got unexpected extend mode %#x.\n", extend_mode);
    extend_mode = ID2D1BitmapBrush_GetExtendModeY(brush);
    ok(extend_mode == D2D1_EXTEND_MODE_CLAMP, "Got unexpected extend mode %#x.\n", extend_mode);
    interpolation_mode = ID2D1BitmapBrush_GetInterpolationMode(brush);
    ok(interpolation_mode == D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            "Got unexpected interpolation mode %#x.\n", interpolation_mode);
    ID2D1BitmapBrush_Release(brush);

    hr = ID2D1RenderTarget_CreateBitmapBrush(rt, bitmap, NULL, NULL, &brush);
    ok(SUCCEEDED(hr), "Failed to create brush, hr %#x.\n", hr);
    set_matrix_identity(&matrix);
    translate_matrix(&matrix, 40.0f, 120.0f);
    scale_matrix(&matrix, 20.0f, 60.0f);
    ID2D1BitmapBrush_SetTransform(brush, &matrix);
    ID2D1BitmapBrush_SetInterpolationMode(brush, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

    ID2D1RenderTarget_BeginDraw(rt);

    set_color(&color, 0.0f, 0.0f, 1.0f, 1.0f);
    ID2D1RenderTarget_Clear(rt, &color);

    set_rect(&dst_rect, 40.0f, 120.0f, 120.0f, 360.0f);
    ID2D1RenderTarget_FillRectangle(rt, &dst_rect, (ID2D1Brush *)brush);

    set_matrix_identity(&matrix);
    scale_matrix(&matrix, 0.5f, 2.0f);
    translate_matrix(&matrix, 320.0f, 240.0f);
    rotate_matrix(&matrix, M_PI / 4.0f);
    ID2D1RenderTarget_SetTransform(rt, &matrix);
    set_matrix_identity(&matrix);
    translate_matrix(&matrix, -80.0f, -60.0f);
    scale_matrix(&matrix, 40.0f, 30.0f);
    ID2D1BitmapBrush_SetTransform(brush, &matrix);
    ID2D1BitmapBrush_SetOpacity(brush, 0.75f);
    set_rect(&dst_rect, -80.0f, -60.0f, 80.0f, 60.0f);
    ID2D1RenderTarget_FillRectangle(rt, &dst_rect, (ID2D1Brush *)brush);

    set_matrix_identity(&matrix);
    translate_matrix(&matrix, 200.0f, 120.0f);
    scale_matrix(&matrix, 20.0f, 60.0f);
    ID2D1RenderTarget_SetTransform(rt, &matrix);
    ID2D1RenderTarget_DrawBitmap(rt, bitmap, NULL, 0.25f,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, NULL);
    set_rect(&dst_rect, -4.0f, 12.0f, -8.0f, 8.0f);
    ID2D1RenderTarget_DrawBitmap(rt, bitmap, &dst_rect, 0.75f,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, NULL);
    set_rect(&dst_rect, 0.0f, 8.0f, 4.0f, 12.0f);
    set_rect(&src_rect, 2.0f, 1.0f, 4.0f, 3.0f);
    ID2D1RenderTarget_DrawBitmap(rt, bitmap, &dst_rect, 1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &src_rect);

    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "393636185359a550d459e1e5f0e25411814f724c");
    ok(match, "Surface does not match.\n");

    ID2D1RenderTarget_BeginDraw(rt);

    ID2D1RenderTarget_Clear(rt, &color);

    ID2D1BitmapBrush_SetOpacity(brush, 1.0f);
    for (i = 0; i < sizeof(extend_mode_tests) / sizeof(*extend_mode_tests); ++i)
    {
        ID2D1BitmapBrush_SetExtendModeX(brush, extend_mode_tests[i].extend_mode_x);
        extend_mode = ID2D1BitmapBrush_GetExtendModeX(brush);
        ok(extend_mode == extend_mode_tests[i].extend_mode_x,
                "Test %u: Got unexpected extend mode %#x, expected %#x.\n",
                i, extend_mode, extend_mode_tests[i].extend_mode_x);
        ID2D1BitmapBrush_SetExtendModeY(brush, extend_mode_tests[i].extend_mode_y);
        extend_mode = ID2D1BitmapBrush_GetExtendModeY(brush);
        ok(extend_mode == extend_mode_tests[i].extend_mode_y,
                "Test %u: Got unexpected extend mode %#x, expected %#x.\n",
                i, extend_mode, extend_mode_tests[i].extend_mode_y);
        set_matrix_identity(&matrix);
        translate_matrix(&matrix, extend_mode_tests[i].translate_x, extend_mode_tests[i].translate_y);
        scale_matrix(&matrix, 0.5f, 0.5f);
        ID2D1BitmapBrush_SetTransform(brush, &matrix);
        ID2D1RenderTarget_FillRectangle(rt, &extend_mode_tests[i].rect, (ID2D1Brush *)brush);
    }

    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "b4b775afecdae2d26642001f4faff73663bb8b31");
    ok(match, "Surface does not match.\n");

    ID2D1BitmapBrush_Release(brush);
    refcount = ID2D1Bitmap_Release(bitmap);
    ok(!refcount, "Bitmap has %u references left.\n", refcount);
    ID2D1RenderTarget_Release(rt);
    IDXGISurface_Release(surface);
    IDXGISwapChain_Release(swapchain);
    ID3D10Device1_Release(device);
    DestroyWindow(window);
}

static void fill_geometry_sink(ID2D1GeometrySink *sink)
{
    D2D1_POINT_2F point;

    set_point(&point, 15.0f,  20.0f);
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    set_point(&point, 55.0f,  20.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 55.0f, 220.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 25.0f, 220.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 25.0f, 100.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 75.0f, 100.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 75.0f, 300.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point,  5.0f, 300.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point,  5.0f,  60.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 45.0f,  60.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 45.0f, 180.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 35.0f, 180.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 35.0f, 140.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 65.0f, 140.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 65.0f, 260.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 15.0f, 260.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);

    set_point(&point, 155.0f, 300.0f);
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    set_point(&point, 155.0f, 160.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point,  85.0f, 160.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point,  85.0f, 300.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 120.0f, 300.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 120.0f,  20.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 155.0f,  20.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 155.0f, 160.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point,  85.0f, 160.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point,  85.0f,  20.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 120.0f,  20.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 120.0f, 300.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);

    set_point(&point, 165.0f,  20.0f);
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    set_point(&point, 165.0f, 300.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 235.0f, 300.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 235.0f,  20.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    set_point(&point, 225.0f,  60.0f);
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    set_point(&point, 225.0f, 260.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 175.0f, 260.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 175.0f,  60.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    set_point(&point, 215.0f, 220.0f);
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    set_point(&point, 185.0f, 220.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 185.0f, 100.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 215.0f, 100.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    set_point(&point, 195.0f, 180.0f);
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    set_point(&point, 205.0f, 180.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 205.0f, 140.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    set_point(&point, 195.0f, 140.0f);
    ID2D1GeometrySink_AddLine(sink, point);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
}

static void fill_geometry_sink_bezier(ID2D1GeometrySink *sink)
{
    D2D1_QUADRATIC_BEZIER_SEGMENT quadratic;
    D2D1_POINT_2F point;

    set_point(&point, 5.0f, 160.0f);
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    set_quadratic(&quadratic, 40.0f, 160.0f, 40.0f,  20.0f);
    ID2D1GeometrySink_AddQuadraticBezier(sink, &quadratic);
    set_quadratic(&quadratic, 40.0f, 160.0f, 75.0f, 160.0f);
    ID2D1GeometrySink_AddQuadraticBezier(sink, &quadratic);
    set_quadratic(&quadratic, 40.0f, 160.0f, 40.0f, 300.0f);
    ID2D1GeometrySink_AddQuadraticBezier(sink, &quadratic);
    set_quadratic(&quadratic, 40.0f, 160.0f,  5.0f, 160.0f);
    ID2D1GeometrySink_AddQuadraticBezier(sink, &quadratic);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);

    set_point(&point, 20.0f, 160.0f);
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    set_quadratic(&quadratic, 20.0f,  80.0f, 40.0f,  80.0f);
    ID2D1GeometrySink_AddQuadraticBezier(sink, &quadratic);
    set_quadratic(&quadratic, 60.0f,  80.0f, 60.0f, 160.0f);
    ID2D1GeometrySink_AddQuadraticBezier(sink, &quadratic);
    set_quadratic(&quadratic, 60.0f, 240.0f, 40.0f, 240.0f);
    ID2D1GeometrySink_AddQuadraticBezier(sink, &quadratic);
    set_quadratic(&quadratic, 20.0f, 240.0f, 20.0f, 160.0f);
    ID2D1GeometrySink_AddQuadraticBezier(sink, &quadratic);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
}

static void test_path_geometry(void)
{
    ID2D1TransformedGeometry *transformed_geometry;
    D2D1_MATRIX_3X2_F matrix, tmp_matrix;
    ID2D1GeometrySink *sink, *tmp_sink;
    D2D1_POINT_2F point = {0.0f, 0.0f};
    ID2D1SolidColorBrush *brush;
    ID2D1PathGeometry *geometry;
    ID2D1Geometry *tmp_geometry;
    IDXGISwapChain *swapchain;
    ID2D1RenderTarget *rt;
    ID3D10Device1 *device;
    IDXGISurface *surface;
    ID2D1Factory *factory;
    D2D1_COLOR_F color;
    ULONG refcount;
    UINT32 count;
    HWND window;
    HRESULT hr;
    BOOL match;

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d2d1_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_IDXGISurface, (void **)&surface);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);
    rt = create_render_target(surface);
    ok(!!rt, "Failed to create render target.\n");
    ID2D1RenderTarget_GetFactory(rt, &factory);

    ID2D1RenderTarget_SetDpi(rt, 192.0f, 48.0f);
    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_ALIASED);
    set_color(&color, 0.890f, 0.851f, 0.600f, 1.0f);
    hr = ID2D1RenderTarget_CreateSolidColorBrush(rt, &color, NULL, &brush);
    ok(SUCCEEDED(hr), "Failed to create brush, hr %#x.\n", hr);

    /* Close() when closed. */
    hr = ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ok(SUCCEEDED(hr), "Failed to create path geometry, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(SUCCEEDED(hr), "Failed to open geometry sink, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    hr = ID2D1GeometrySink_Close(sink);
    ok(SUCCEEDED(hr), "Failed to close geometry sink, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get figure count, hr %#x.\n", hr);
    ok(!count, "Got unexpected figure count %u.\n", count);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get segment count, hr %#x.\n", hr);
    ok(!count, "Got unexpected segment count %u.\n", count);
    hr = ID2D1GeometrySink_Close(sink);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    ID2D1GeometrySink_Release(sink);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get figure count, hr %#x.\n", hr);
    ok(!count, "Got unexpected figure count %u.\n", count);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get segment count, hr %#x.\n", hr);
    ok(!count, "Got unexpected segment count %u.\n", count);
    ID2D1PathGeometry_Release(geometry);

    /* Open() when closed. */
    hr = ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ok(SUCCEEDED(hr), "Failed to create path geometry, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(SUCCEEDED(hr), "Failed to open geometry sink, hr %#x.\n", hr);
    hr = ID2D1GeometrySink_Close(sink);
    ok(SUCCEEDED(hr), "Failed to close geometry sink, hr %#x.\n", hr);
    ID2D1GeometrySink_Release(sink);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get figure count, hr %#x.\n", hr);
    ok(!count, "Got unexpected figure count %u.\n", count);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get segment count, hr %#x.\n", hr);
    ok(!count, "Got unexpected segment count %u.\n", count);
    ID2D1PathGeometry_Release(geometry);

    /* Open() when open. */
    hr = ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ok(SUCCEEDED(hr), "Failed to create path geometry, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(SUCCEEDED(hr), "Failed to open geometry sink, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &tmp_sink);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    hr = ID2D1GeometrySink_Close(sink);
    ok(SUCCEEDED(hr), "Failed to close geometry sink, hr %#x.\n", hr);
    ID2D1GeometrySink_Release(sink);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get figure count, hr %#x.\n", hr);
    ok(!count, "Got unexpected figure count %u.\n", count);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get segment count, hr %#x.\n", hr);
    ok(!count, "Got unexpected segment count %u.\n", count);
    ID2D1PathGeometry_Release(geometry);

    /* BeginFigure() without EndFigure(). */
    hr = ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ok(SUCCEEDED(hr), "Failed to create path geometry, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(SUCCEEDED(hr), "Failed to open geometry sink, hr %#x.\n", hr);
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    hr = ID2D1GeometrySink_Close(sink);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    hr = ID2D1GeometrySink_Close(sink);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    ID2D1GeometrySink_Release(sink);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    ID2D1PathGeometry_Release(geometry);

    /* EndFigure() without BeginFigure(). */
    hr = ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ok(SUCCEEDED(hr), "Failed to create path geometry, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(SUCCEEDED(hr), "Failed to open geometry sink, hr %#x.\n", hr);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    hr = ID2D1GeometrySink_Close(sink);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    ID2D1GeometrySink_Release(sink);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    ID2D1PathGeometry_Release(geometry);

    /* BeginFigure()/EndFigure() mismatch. */
    hr = ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ok(SUCCEEDED(hr), "Failed to create path geometry, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(SUCCEEDED(hr), "Failed to open geometry sink, hr %#x.\n", hr);
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    hr = ID2D1GeometrySink_Close(sink);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    hr = ID2D1GeometrySink_Close(sink);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    ID2D1GeometrySink_Release(sink);
    ID2D1PathGeometry_Release(geometry);

    /* AddLine() outside BeginFigure()/EndFigure(). */
    hr = ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ok(SUCCEEDED(hr), "Failed to create path geometry, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(SUCCEEDED(hr), "Failed to open geometry sink, hr %#x.\n", hr);
    ID2D1GeometrySink_AddLine(sink, point);
    hr = ID2D1GeometrySink_Close(sink);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    ID2D1GeometrySink_AddLine(sink, point);
    ID2D1GeometrySink_Release(sink);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(hr == D2DERR_WRONG_STATE, "Got unexpected hr %#x.\n", hr);
    ID2D1PathGeometry_Release(geometry);

    /* Empty figure. */
    hr = ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ok(SUCCEEDED(hr), "Failed to create path geometry, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(SUCCEEDED(hr), "Failed to open geometry sink, hr %#x.\n", hr);
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    hr = ID2D1GeometrySink_Close(sink);
    ok(SUCCEEDED(hr), "Failed to close geometry sink, hr %#x.\n", hr);
    ID2D1GeometrySink_Release(sink);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get figure count, hr %#x.\n", hr);
    ok(count == 1, "Got unexpected figure count %u.\n", count);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get segment count, hr %#x.\n", hr);
    ok(count == 1, "Got unexpected segment count %u.\n", count);
    ID2D1PathGeometry_Release(geometry);

    hr = ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ok(SUCCEEDED(hr), "Failed to create path geometry, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(SUCCEEDED(hr), "Failed to open geometry sink, hr %#x.\n", hr);
    /* The fillmode that's used is the last one set before the sink is closed. */
    ID2D1GeometrySink_SetFillMode(sink, D2D1_FILL_MODE_WINDING);
    fill_geometry_sink(sink);
    ID2D1GeometrySink_SetFillMode(sink, D2D1_FILL_MODE_ALTERNATE);
    hr = ID2D1GeometrySink_Close(sink);
    ok(SUCCEEDED(hr), "Failed to close geometry sink, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get figure count, hr %#x.\n", hr);
    ok(count == 6, "Got unexpected figure count %u.\n", count);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get segment count, hr %#x.\n", hr);
    /* Intersections don't create extra segments. */
    ok(count == 44, "Got unexpected segment count %u.\n", count);
    ID2D1GeometrySink_SetFillMode(sink, D2D1_FILL_MODE_WINDING);
    ID2D1GeometrySink_Release(sink);

    set_matrix_identity(&matrix);
    translate_matrix(&matrix, 80.0f, 640.0f);
    scale_matrix(&matrix, 1.0f, -1.0f);
    hr = ID2D1Factory_CreateTransformedGeometry(factory, (ID2D1Geometry *)geometry, &matrix, &transformed_geometry);
    ok(SUCCEEDED(hr), "Failed to create transformed geometry, hr %#x.\n", hr);

    ID2D1TransformedGeometry_GetSourceGeometry(transformed_geometry, &tmp_geometry);
    ok(tmp_geometry == (ID2D1Geometry *)geometry,
            "Got unexpected source geometry %p, expected %p.\n", tmp_geometry, geometry);
    ID2D1Geometry_Release(tmp_geometry);
    ID2D1TransformedGeometry_GetTransform(transformed_geometry, &tmp_matrix);
    ok(!memcmp(&tmp_matrix, &matrix, sizeof(matrix)),
            "Got unexpected matrix {%.8e, %.8e, %.8e, %.8e, %.8e, %.8e}.\n",
            tmp_matrix._11, tmp_matrix._12, tmp_matrix._21,
            tmp_matrix._22, tmp_matrix._31, tmp_matrix._32);

    ID2D1RenderTarget_BeginDraw(rt);
    set_color(&color, 0.396f, 0.180f, 0.537f, 1.0f);
    ID2D1RenderTarget_Clear(rt, &color);
    ID2D1RenderTarget_FillGeometry(rt, (ID2D1Geometry *)geometry, (ID2D1Brush *)brush, NULL);
    ID2D1RenderTarget_FillGeometry(rt, (ID2D1Geometry *)transformed_geometry, (ID2D1Brush *)brush, NULL);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "3aace1b22aae111cb577614fed16e4eb1650dba5");
    ok(match, "Surface does not match.\n");
    ID2D1TransformedGeometry_Release(transformed_geometry);
    ID2D1PathGeometry_Release(geometry);

    hr = ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ok(SUCCEEDED(hr), "Failed to create path geometry, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(SUCCEEDED(hr), "Failed to open geometry sink, hr %#x.\n", hr);
    fill_geometry_sink(sink);
    ID2D1GeometrySink_SetFillMode(sink, D2D1_FILL_MODE_WINDING);
    hr = ID2D1GeometrySink_Close(sink);
    ok(SUCCEEDED(hr), "Failed to close geometry sink, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get figure count, hr %#x.\n", hr);
    ok(count == 6, "Got unexpected figure count %u.\n", count);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get segment count, hr %#x.\n", hr);
    ok(count == 44, "Got unexpected segment count %u.\n", count);
    ID2D1GeometrySink_Release(sink);

    set_matrix_identity(&matrix);
    translate_matrix(&matrix, 320.0f, 320.0f);
    scale_matrix(&matrix, -1.0f, 1.0f);
    hr = ID2D1Factory_CreateTransformedGeometry(factory, (ID2D1Geometry *)geometry, &matrix, &transformed_geometry);
    ok(SUCCEEDED(hr), "Failed to create transformed geometry, hr %#x.\n", hr);

    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &color);
    ID2D1RenderTarget_FillGeometry(rt, (ID2D1Geometry *)geometry, (ID2D1Brush *)brush, NULL);
    ID2D1RenderTarget_FillGeometry(rt, (ID2D1Geometry *)transformed_geometry, (ID2D1Brush *)brush, NULL);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "bfb40a1f007694fa07dbd3b854f3f5d9c3e1d76b");
    ok(match, "Surface does not match.\n");
    ID2D1TransformedGeometry_Release(transformed_geometry);
    ID2D1PathGeometry_Release(geometry);

    hr = ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ok(SUCCEEDED(hr), "Failed to create path geometry, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(SUCCEEDED(hr), "Failed to open geometry sink, hr %#x.\n", hr);
    fill_geometry_sink_bezier(sink);
    hr = ID2D1GeometrySink_Close(sink);
    ok(SUCCEEDED(hr), "Failed to close geometry sink, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get figure count, hr %#x.\n", hr);
    ok(count == 2, "Got unexpected figure count %u.\n", count);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get segment count, hr %#x.\n", hr);
    ok(count == 10, "Got unexpected segment count %u.\n", count);
    ID2D1GeometrySink_Release(sink);

    set_matrix_identity(&matrix);
    scale_matrix(&matrix, 0.5f, 2.0f);
    translate_matrix(&matrix, 240.0f, -33.0f);
    rotate_matrix(&matrix, M_PI / 4.0f);
    scale_matrix(&matrix, 2.0f, 0.5f);
    hr = ID2D1Factory_CreateTransformedGeometry(factory, (ID2D1Geometry *)geometry, &matrix, &transformed_geometry);
    ok(SUCCEEDED(hr), "Failed to create transformed geometry, hr %#x.\n", hr);

    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &color);
    ID2D1RenderTarget_FillGeometry(rt, (ID2D1Geometry *)geometry, (ID2D1Brush *)brush, NULL);
    ID2D1RenderTarget_FillGeometry(rt, (ID2D1Geometry *)transformed_geometry, (ID2D1Brush *)brush, NULL);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_figure(surface, 0, 0, 160, 160, 0xff652e89, 64,
            "7xoCngECngECngECngECngECngECngECnQEEnAEEnAEEnAEEnAEEmwEGmgEGmgEGmgEGmQEImAEI"
            "lAEECASLAQgKCIEBDQoMew8KD3YQDBByEgwSbhMOEmwUDhRpFBAUZxUQFWUVEhVjFhIWYRYUFl8X"
            "FBddFxYWXRYYFlsXGBdaFhoWWRYcFlgVHhVXFSAVVhQiFFUUIxRVEyYTVBIoElQRKhFUECwQUxAu"
            "EFIOMg5SDTQNUgs4C1IJPAlRCEAIUAZEBlAESARQAU4BTgJQAkgGUAY/C1ALMhNQEyoTUBMyC1AL"
            "PwZQBkgCUAJOAU4BUARIBFAGRAZQCEAIUQk8CVILOAtSDTQNUg4yDlIQLhBTECwQVBEqEVQSKBJU"
            "EyYTVBQjFFYUIhRWFSAVVxUeFVgWHBZZFhoWWhcYF1sWGBZcFxYWXhcUF18WFBZhFhIWYxUSFWUV"
            "EBVnFBAUaRQOFGsTDhJvEgwSchAMEHYPCg96DQoMggEICgiLAQQIBJQBCJgBCJkBBpoBBpoBBpoB"
            "BpsBBJwBBJwBBJwBBJwBBJ0BAp4BAp4BAp4BAp4BAp4BAp4BAp4BAgAA");
    todo_wine ok(match, "Figure does not match.\n");
    match = compare_figure(surface, 160, 0, 320, 160, 0xff652e89, 64,
            "4VIBwAIBWgHlAQFYAecBAVYB6QEBVAHrAQEjDCMB7AECHhQeAu0BAxoYGgPvAQMWHhYD8QEDFCAU"
            "A/MBBBAkEAT0AQUOJw0F9QEGCioKBvcBBggsCAb4AQgFLgUI+QEJATIBCfsBCAIwAgj8AQcFLAUH"
            "/QEFCCgIBf4BBAwiDAT/AQIQHBAClwISlwIBPgGAAgI8Av8BAzwD/QEEPAT7AQY6BvkBBzoH+AEI"
            "OAj3AQk4CfYBCTgK9AELNgvzAQw2DPIBDDYM8QEONA7wAQ40DvABDjQO7wEPNA/uAQ80D+4BEDIQ"
            "7QERMhHsAREyEewBETIR7AERMhHsAREyEewBETIR7AERMhHsAREyEewBETIR7AERMhHsAREyEewB"
            "ETIR7AERMhHsAREyEe0BEDIQ7gEQMw/uAQ80D+4BDzQP7wEONA7wAQ40DvEBDDYM8gEMNgzzAQs2"
            "C/QBCzcK9QEJOAn3AQg4CfcBBzoH+QEGOgb7AQU6BfwBBDwE/QEDPAP/AQE+AZkCDpkCAhIYEgKA"
            "AgMNIA0D/wEFCSYJBf4BBgYqBgf8AQgDLgMI+wFG+gEIAzADCPkBBwYuBgf3AQYKKgoG9gEFDCgM"
            "BfUBBBAlDwTzAQQSIhIE8QEDFh4WA/ABAhkaGQLvAQIcFhwC7QECIBAgAusBASgEKAHpAQFWAecB"
            "AVgB5QEBWgHAAgEA");
    todo_wine ok(match, "Figure does not match.\n");
    ID2D1TransformedGeometry_Release(transformed_geometry);
    ID2D1PathGeometry_Release(geometry);

    hr = ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ok(SUCCEEDED(hr), "Failed to create path geometry, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_Open(geometry, &sink);
    ok(SUCCEEDED(hr), "Failed to open geometry sink, hr %#x.\n", hr);
    fill_geometry_sink_bezier(sink);
    ID2D1GeometrySink_SetFillMode(sink, D2D1_FILL_MODE_WINDING);
    hr = ID2D1GeometrySink_Close(sink);
    ok(SUCCEEDED(hr), "Failed to close geometry sink, hr %#x.\n", hr);
    hr = ID2D1PathGeometry_GetFigureCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get figure count, hr %#x.\n", hr);
    ok(count == 2, "Got unexpected figure count %u.\n", count);
    hr = ID2D1PathGeometry_GetSegmentCount(geometry, &count);
    ok(SUCCEEDED(hr), "Failed to get segment count, hr %#x.\n", hr);
    ok(count == 10, "Got unexpected segment count %u.\n", count);
    ID2D1GeometrySink_Release(sink);

    set_matrix_identity(&matrix);
    scale_matrix(&matrix, 0.5f, 2.0f);
    translate_matrix(&matrix, 127.0f, 80.0f);
    rotate_matrix(&matrix, M_PI / -4.0f);
    scale_matrix(&matrix, 2.0f, 0.5f);
    hr = ID2D1Factory_CreateTransformedGeometry(factory, (ID2D1Geometry *)geometry, &matrix, &transformed_geometry);
    ok(SUCCEEDED(hr), "Failed to create transformed geometry, hr %#x.\n", hr);

    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &color);
    ID2D1RenderTarget_FillGeometry(rt, (ID2D1Geometry *)geometry, (ID2D1Brush *)brush, NULL);
    ID2D1RenderTarget_FillGeometry(rt, (ID2D1Geometry *)transformed_geometry, (ID2D1Brush *)brush, NULL);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_figure(surface, 0, 0, 160, 160, 0xff652e89, 64,
            "7xoCngECngECngECngECngECngECngECnQEEnAEEnAEEnAEEnAEEmwEGmgEGmgEGmgEGmQEImAEI"
            "lAEQiwEagQEjeyh2LHIwbjNsNmk4ZzplPGM+YUBfQl1DXURbRlpGWUhYSFdKVkpVS1VMVExUTFRM"
            "U05STlJOUk5STlFQUFBQUFBQTlRIXD9mMnYqdjJmP1xIVE5QUFBQUFBQUU5STlJOUk5STlNMVExU"
            "TFRMVEtWSlZKV0hYSFlGWkZbRFxDXkJfQGE+YzxlOmc4aTZrM28wcix2KHojggEaiwEQlAEImAEI"
            "mQEGmgEGmgEGmgEGmwEEnAEEnAEEnAEEnAEEnQECngECngECngECngECngECngECngEC");
    ok(match, "Figure does not match.\n");
    match = compare_figure(surface, 160, 0, 320, 160, 0xff652e89, 64,
            "4VIBwAIBWgHlAQFYAecBAVYB6QEBVAHrAQIhDiIB7QECHRUdAu4BAhkaGQPvAQMWHhYD8QEEEyET"
            "A/MBBBAkEAT1AQUMKA0F9QEGCioKBvcBBwctBwb5AQgELwQI+QEJATIBCfsBRP0BQ/0BQv8BQf8B"
            "QIECP4ACQIACQf4BQ/wBRPsBRvoBR/gBSPcBSvYBS/QBTPMBTvIBTvIBT/ABUPABUe4BUu4BUu4B"
            "U+0BU+wBVOwBVOwBVOwBVOwBVesBVesBVesBVesBVOwBVOwBVOwBVO0BU+0BU+0BUu4BUu8BUe8B"
            "UPEBT/EBTvIBTvMBTPUBS/UBSvcBSfcBSPkBRvsBRP0BQ/4BQf8BQIECP4ACQIACQf4BQv4BQ/wB"
            "RPsBCQEyAQn6AQgELwQI+AEHBy0GB/cBBgoqCgb2AQUMKA0F9AEEECUPBPMBBBIiEwPxAQMWHhYD"
            "8AECGRoZA+4BAh0VHQLsAQIhDiIB6wEBVAHpAQFWAecBAVgB5QEBWgHAAgEA");
    ok(match, "Figure does not match.\n");
    ID2D1TransformedGeometry_Release(transformed_geometry);
    ID2D1PathGeometry_Release(geometry);

    ID2D1SolidColorBrush_Release(brush);
    ID2D1RenderTarget_Release(rt);
    refcount = ID2D1Factory_Release(factory);
    ok(!refcount, "Factory has %u references left.\n", refcount);
    IDXGISurface_Release(surface);
    IDXGISwapChain_Release(swapchain);
    ID3D10Device1_Release(device);
    DestroyWindow(window);
}

static void test_bitmap_formats(void)
{
    D2D1_BITMAP_PROPERTIES bitmap_desc;
    IDXGISwapChain *swapchain;
    D2D1_SIZE_U size = {4, 4};
    ID2D1RenderTarget *rt;
    ID3D10Device1 *device;
    IDXGISurface *surface;
    ID2D1Bitmap *bitmap;
    unsigned int i, j;
    HWND window;
    HRESULT hr;

    static const struct
    {
        DXGI_FORMAT format;
        DWORD mask;
    }
    bitmap_formats[] =
    {
        {DXGI_FORMAT_R32G32B32A32_FLOAT,    0x8a},
        {DXGI_FORMAT_R16G16B16A16_FLOAT,    0x8a},
        {DXGI_FORMAT_R16G16B16A16_UNORM,    0x8a},
        {DXGI_FORMAT_R8G8B8A8_TYPELESS,     0x00},
        {DXGI_FORMAT_R8G8B8A8_UNORM,        0x0a},
        {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,   0x8a},
        {DXGI_FORMAT_R8G8B8A8_UINT,         0x00},
        {DXGI_FORMAT_R8G8B8A8_SNORM,        0x00},
        {DXGI_FORMAT_R8G8B8A8_SINT,         0x00},
        {DXGI_FORMAT_A8_UNORM,              0x06},
        {DXGI_FORMAT_B8G8R8A8_UNORM,        0x0a},
        {DXGI_FORMAT_B8G8R8X8_UNORM,        0x88},
        {DXGI_FORMAT_B8G8R8A8_TYPELESS,     0x00},
        {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,   0x8a},
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d2d1_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_IDXGISurface, (void **)&surface);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);
    rt = create_render_target(surface);
    ok(!!rt, "Failed to create render target.\n");

    bitmap_desc.dpiX = 96.0f;
    bitmap_desc.dpiY = 96.0f;
    for (i = 0; i < sizeof(bitmap_formats) / sizeof(*bitmap_formats); ++i)
    {
        for (j = 0; j < 4; ++j)
        {
            if ((bitmap_formats[i].mask & (0x80 | (1u << j))) == (0x80 | (1u << j)))
                continue;

            bitmap_desc.pixelFormat.format = bitmap_formats[i].format;
            bitmap_desc.pixelFormat.alphaMode = j;
            hr = ID2D1RenderTarget_CreateBitmap(rt, size, NULL, 0, &bitmap_desc, &bitmap);
            if (bitmap_formats[i].mask & (1u << j))
                ok(hr == S_OK, "Got unexpected hr %#x, for format %#x/%#x.\n",
                        hr, bitmap_formats[i].format, j);
            else
                ok(hr == D2DERR_UNSUPPORTED_PIXEL_FORMAT, "Got unexpected hr %#x, for format %#x/%#x.\n",
                        hr, bitmap_formats[i].format, j);
            if (SUCCEEDED(hr))
                ID2D1Bitmap_Release(bitmap);
        }
    }

    ID2D1RenderTarget_Release(rt);
    IDXGISurface_Release(surface);
    IDXGISwapChain_Release(swapchain);
    ID3D10Device1_Release(device);
    DestroyWindow(window);
}

static void test_alpha_mode(void)
{
    D2D1_RENDER_TARGET_PROPERTIES rt_desc;
    D2D1_BITMAP_PROPERTIES bitmap_desc;
    ID2D1SolidColorBrush *color_brush;
    ID2D1BitmapBrush *bitmap_brush;
    IDXGISwapChain *swapchain;
    ID2D1RenderTarget *rt;
    ID3D10Device1 *device;
    IDXGISurface *surface;
    ID2D1Bitmap *bitmap;
    D2D1_COLOR_F color;
    D2D1_RECT_F rect;
    D2D1_SIZE_U size;
    ULONG refcount;
    HWND window;
    HRESULT hr;
    BOOL match;

    static const DWORD bitmap_data[] =
    {
        0x7f7f0000, 0x7f7f7f00, 0x7f007f00, 0x7f007f7f,
        0x7f00007f, 0x7f7f007f, 0x7f000000, 0x7f404040,
        0x7f7f7f7f, 0x7f7f7f7f, 0x7f7f7f7f, 0x7f000000,
        0x7f7f7f7f, 0x7f000000, 0x7f000000, 0x7f000000,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d2d1_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_IDXGISurface, (void **)&surface);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);
    rt = create_render_target(surface);
    ok(!!rt, "Failed to create render target.\n");

    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_ALIASED);

    set_size_u(&size, 4, 4);
    bitmap_desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    bitmap_desc.dpiX = 96.0f / 40.0f;
    bitmap_desc.dpiY = 96.0f / 30.0f;
    hr = ID2D1RenderTarget_CreateBitmap(rt, size, bitmap_data, 4 * sizeof(*bitmap_data), &bitmap_desc, &bitmap);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);

    hr = ID2D1RenderTarget_CreateBitmapBrush(rt, bitmap, NULL, NULL, &bitmap_brush);
    ok(SUCCEEDED(hr), "Failed to create brush, hr %#x.\n", hr);
    ID2D1BitmapBrush_SetInterpolationMode(bitmap_brush, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    ID2D1BitmapBrush_SetExtendModeX(bitmap_brush, D2D1_EXTEND_MODE_WRAP);
    ID2D1BitmapBrush_SetExtendModeY(bitmap_brush, D2D1_EXTEND_MODE_WRAP);

    set_color(&color, 0.0f, 1.0f, 0.0f, 0.75f);
    hr = ID2D1RenderTarget_CreateSolidColorBrush(rt, &color, NULL, &color_brush);
    ok(SUCCEEDED(hr), "Failed to create brush, hr %#x.\n", hr);

    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, NULL);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "48c41aff3a130a17ee210866b2ab7d36763934d5");
    ok(match, "Surface does not match.\n");

    ID2D1RenderTarget_BeginDraw(rt);
    set_color(&color, 1.0f, 0.0f, 0.0f, 0.25f);
    ID2D1RenderTarget_Clear(rt, &color);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "6487e683730fb5a77c1911388d00b04664c5c4e4");
    ok(match, "Surface does not match.\n");

    ID2D1RenderTarget_BeginDraw(rt);
    set_color(&color, 0.0f, 0.0f, 1.0f, 0.75f);
    ID2D1RenderTarget_Clear(rt, &color);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "7a35ba09e43cbaf591388ff1ef8de56157630c98");
    ok(match, "Surface does not match.\n");

    ID2D1RenderTarget_BeginDraw(rt);

    set_rect(&rect,   0.0f,   0.0f, 160.0f, 120.0f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)bitmap_brush);
    set_rect(&rect, 160.0f,   0.0f, 320.0f, 120.0f);
    ID2D1BitmapBrush_SetOpacity(bitmap_brush, 0.75f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)bitmap_brush);
    set_rect(&rect, 320.0f,   0.0f, 480.0f, 120.0f);
    ID2D1BitmapBrush_SetOpacity(bitmap_brush, 0.25f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)bitmap_brush);

    ID2D1Bitmap_Release(bitmap);
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    hr = ID2D1RenderTarget_CreateBitmap(rt, size, bitmap_data, 4 * sizeof(*bitmap_data), &bitmap_desc, &bitmap);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);
    ID2D1BitmapBrush_SetBitmap(bitmap_brush, bitmap);

    set_rect(&rect,   0.0f, 120.0f, 160.0f, 240.0f);
    ID2D1BitmapBrush_SetOpacity(bitmap_brush, 1.0f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)bitmap_brush);
    set_rect(&rect, 160.0f, 120.0f, 320.0f, 240.0f);
    ID2D1BitmapBrush_SetOpacity(bitmap_brush, 0.75f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)bitmap_brush);
    set_rect(&rect, 320.0f, 120.0f, 480.0f, 240.0f);
    ID2D1BitmapBrush_SetOpacity(bitmap_brush, 0.25f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)bitmap_brush);

    set_rect(&rect,   0.0f, 240.0f, 160.0f, 360.0f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)color_brush);
    set_rect(&rect, 160.0f, 240.0f, 320.0f, 360.0f);
    ID2D1SolidColorBrush_SetOpacity(color_brush, 0.75f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)color_brush);
    set_rect(&rect, 320.0f, 240.0f, 480.0f, 360.0f);
    ID2D1SolidColorBrush_SetOpacity(color_brush, 0.25f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)color_brush);

    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "14f8ac64b70966c7c3c6281c59aaecdb17c3b16a");
    ok(match, "Surface does not match.\n");

    ID2D1RenderTarget_Release(rt);
    rt_desc.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    rt_desc.pixelFormat.format = DXGI_FORMAT_UNKNOWN;
    rt_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    rt_desc.dpiX = 0.0f;
    rt_desc.dpiY = 0.0f;
    rt_desc.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    rt_desc.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    rt = create_render_target_desc(surface, &rt_desc);
    ok(!!rt, "Failed to create render target.\n");

    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_ALIASED);

    ID2D1Bitmap_Release(bitmap);
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    hr = ID2D1RenderTarget_CreateBitmap(rt, size, bitmap_data, 4 * sizeof(*bitmap_data), &bitmap_desc, &bitmap);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);
    ID2D1BitmapBrush_SetBitmap(bitmap_brush, bitmap);

    ID2D1BitmapBrush_Release(bitmap_brush);
    hr = ID2D1RenderTarget_CreateBitmapBrush(rt, bitmap, NULL, NULL, &bitmap_brush);
    ok(SUCCEEDED(hr), "Failed to create brush, hr %#x.\n", hr);
    ID2D1BitmapBrush_SetInterpolationMode(bitmap_brush, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    ID2D1BitmapBrush_SetExtendModeX(bitmap_brush, D2D1_EXTEND_MODE_WRAP);
    ID2D1BitmapBrush_SetExtendModeY(bitmap_brush, D2D1_EXTEND_MODE_WRAP);

    ID2D1SolidColorBrush_Release(color_brush);
    set_color(&color, 0.0f, 1.0f, 0.0f, 0.75f);
    hr = ID2D1RenderTarget_CreateSolidColorBrush(rt, &color, NULL, &color_brush);
    ok(SUCCEEDED(hr), "Failed to create brush, hr %#x.\n", hr);

    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, NULL);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "b44510bf2d2e61a8d7c0ad862de49a471f1fd13f");
    ok(match, "Surface does not match.\n");

    ID2D1RenderTarget_BeginDraw(rt);
    set_color(&color, 1.0f, 0.0f, 0.0f, 0.25f);
    ID2D1RenderTarget_Clear(rt, &color);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "2184f4a9198fc1de09ac85301b7a03eebadd9b81");
    ok(match, "Surface does not match.\n");

    ID2D1RenderTarget_BeginDraw(rt);
    set_color(&color, 0.0f, 0.0f, 1.0f, 0.75f);
    ID2D1RenderTarget_Clear(rt, &color);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "6527ec83b4039c895b50f9b3e144fe0cf90d1889");
    ok(match, "Surface does not match.\n");

    ID2D1RenderTarget_BeginDraw(rt);

    set_rect(&rect,   0.0f,   0.0f, 160.0f, 120.0f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)bitmap_brush);
    set_rect(&rect, 160.0f,   0.0f, 320.0f, 120.0f);
    ID2D1BitmapBrush_SetOpacity(bitmap_brush, 0.75f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)bitmap_brush);
    set_rect(&rect, 320.0f,   0.0f, 480.0f, 120.0f);
    ID2D1BitmapBrush_SetOpacity(bitmap_brush, 0.25f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)bitmap_brush);

    ID2D1Bitmap_Release(bitmap);
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    hr = ID2D1RenderTarget_CreateBitmap(rt, size, bitmap_data, 4 * sizeof(*bitmap_data), &bitmap_desc, &bitmap);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);
    ID2D1BitmapBrush_SetBitmap(bitmap_brush, bitmap);

    set_rect(&rect,   0.0f, 120.0f, 160.0f, 240.0f);
    ID2D1BitmapBrush_SetOpacity(bitmap_brush, 1.0f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)bitmap_brush);
    set_rect(&rect, 160.0f, 120.0f, 320.0f, 240.0f);
    ID2D1BitmapBrush_SetOpacity(bitmap_brush, 0.75f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)bitmap_brush);
    set_rect(&rect, 320.0f, 120.0f, 480.0f, 240.0f);
    ID2D1BitmapBrush_SetOpacity(bitmap_brush, 0.25f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)bitmap_brush);

    set_rect(&rect,   0.0f, 240.0f, 160.0f, 360.0f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)color_brush);
    set_rect(&rect, 160.0f, 240.0f, 320.0f, 360.0f);
    ID2D1SolidColorBrush_SetOpacity(color_brush, 0.75f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)color_brush);
    set_rect(&rect, 320.0f, 240.0f, 480.0f, 360.0f);
    ID2D1SolidColorBrush_SetOpacity(color_brush, 0.25f);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)color_brush);

    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);
    match = compare_surface(surface, "465f5a3190d7bde408b3206b4be939fb22f8a3d6");
    ok(match, "Surface does not match.\n");

    refcount = ID2D1Bitmap_Release(bitmap);
    ok(refcount == 1, "Bitmap has %u references left.\n", refcount);
    ID2D1SolidColorBrush_Release(color_brush);
    ID2D1BitmapBrush_Release(bitmap_brush);
    ID2D1RenderTarget_Release(rt);
    IDXGISurface_Release(surface);
    IDXGISwapChain_Release(swapchain);
    ID3D10Device1_Release(device);
    DestroyWindow(window);
}

static void test_shared_bitmap(void)
{
    IDXGISwapChain *swapchain1, *swapchain2;
    IWICBitmap *wic_bitmap1, *wic_bitmap2;
    D2D1_RENDER_TARGET_PROPERTIES desc;
    D2D1_BITMAP_PROPERTIES bitmap_desc;
    IDXGISurface *surface1, *surface2;
    ID2D1Factory *factory1, *factory2;
    ID3D10Device1 *device1, *device2;
    IWICImagingFactory *wic_factory;
    ID2D1Bitmap *bitmap1, *bitmap2;
    ID2D1RenderTarget *rt1, *rt2;
    D2D1_SIZE_U size = {4, 4};
    HWND window1, window2;
    HRESULT hr;

    if (!(device1 = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }

    window1 = CreateWindowA("static", "d2d1_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    window2 = CreateWindowA("static", "d2d1_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain1 = create_swapchain(device1, window1, TRUE);
    swapchain2 = create_swapchain(device1, window2, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain1, 0, &IID_IDXGISurface, (void **)&surface1);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);
    hr = IDXGISwapChain_GetBuffer(swapchain2, 0, &IID_IDXGISurface, (void **)&surface2);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
            &IID_IWICImagingFactory, (void **)&wic_factory);
    ok(SUCCEEDED(hr), "Failed to create WIC imaging factory, hr %#x.\n", hr);
    hr = IWICImagingFactory_CreateBitmap(wic_factory, 640, 480,
            &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &wic_bitmap1);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);
    hr = IWICImagingFactory_CreateBitmap(wic_factory, 640, 480,
            &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &wic_bitmap2);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);
    IWICImagingFactory_Release(wic_factory);

    desc.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    desc.pixelFormat.format = DXGI_FORMAT_UNKNOWN;
    desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    desc.dpiX = 0.0f;
    desc.dpiY = 0.0f;
    desc.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    desc.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    bitmap_desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmap_desc.dpiX = 96.0f;
    bitmap_desc.dpiY = 96.0f;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&factory1);
    ok(SUCCEEDED(hr), "Failed to create factory, hr %#x.\n", hr);
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&factory2);
    ok(SUCCEEDED(hr), "Failed to create factory, hr %#x.\n", hr);

    /* DXGI surface render targets with the same device and factory. */
    hr = ID2D1Factory_CreateDxgiSurfaceRenderTarget(factory1, surface1, &desc, &rt1);
    ok(SUCCEEDED(hr), "Failed to create render target, hr %#x.\n", hr);
    hr = ID2D1RenderTarget_CreateBitmap(rt1, size, NULL, 0, &bitmap_desc, &bitmap1);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);

    hr = ID2D1Factory_CreateDxgiSurfaceRenderTarget(factory1, surface2, &desc, &rt2);
    ok(SUCCEEDED(hr), "Failed to create render target, hr %#x.\n", hr);
    hr = ID2D1RenderTarget_CreateSharedBitmap(rt2, &IID_ID2D1Bitmap, bitmap1, NULL, &bitmap2);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);
    ID2D1Bitmap_Release(bitmap2);
    hr = ID2D1RenderTarget_CreateSharedBitmap(rt2, &IID_IUnknown, bitmap1, NULL, &bitmap2);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ID2D1RenderTarget_Release(rt2);

    /* DXGI surface render targets with the same device but different factories. */
    hr = ID2D1Factory_CreateDxgiSurfaceRenderTarget(factory2, surface2, &desc, &rt2);
    ok(SUCCEEDED(hr), "Failed to create render target, hr %#x.\n", hr);
    hr = ID2D1RenderTarget_CreateSharedBitmap(rt2, &IID_ID2D1Bitmap, bitmap1, NULL, &bitmap2);
    ok(hr == D2DERR_WRONG_FACTORY, "Got unexpected hr %#x.\n", hr);
    ID2D1RenderTarget_Release(rt2);

    /* DXGI surface render targets with different devices but the same factory. */
    IDXGISurface_Release(surface2);
    IDXGISwapChain_Release(swapchain2);
    device2 = create_device();
    ok(!!device2, "Failed to create device.\n");
    swapchain2 = create_swapchain(device2, window2, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain2, 0, &IID_IDXGISurface, (void **)&surface2);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);

    hr = ID2D1Factory_CreateDxgiSurfaceRenderTarget(factory1, surface2, &desc, &rt2);
    ok(SUCCEEDED(hr), "Failed to create render target, hr %#x.\n", hr);
    hr = ID2D1RenderTarget_CreateSharedBitmap(rt2, &IID_ID2D1Bitmap, bitmap1, NULL, &bitmap2);
    ok(hr == D2DERR_UNSUPPORTED_OPERATION, "Got unexpected hr %#x.\n", hr);
    ID2D1RenderTarget_Release(rt2);

    /* DXGI surface render targets with different devices and different factories. */
    hr = ID2D1Factory_CreateDxgiSurfaceRenderTarget(factory2, surface2, &desc, &rt2);
    ok(SUCCEEDED(hr), "Failed to create render target, hr %#x.\n", hr);
    hr = ID2D1RenderTarget_CreateSharedBitmap(rt2, &IID_ID2D1Bitmap, bitmap1, NULL, &bitmap2);
    ok(hr == D2DERR_WRONG_FACTORY, "Got unexpected hr %#x.\n", hr);
    ID2D1RenderTarget_Release(rt2);

    /* DXGI surface render target and WIC bitmap render target, same factory. */
    hr = ID2D1Factory_CreateWicBitmapRenderTarget(factory1, wic_bitmap2, &desc, &rt2);
    ok(SUCCEEDED(hr), "Failed to create render target, hr %#x.\n", hr);
    hr = ID2D1RenderTarget_CreateSharedBitmap(rt2, &IID_ID2D1Bitmap, bitmap1, NULL, &bitmap2);
    ok(hr == D2DERR_UNSUPPORTED_OPERATION, "Got unexpected hr %#x.\n", hr);
    ID2D1RenderTarget_Release(rt2);

    /* WIC bitmap render targets on different D2D factories. */
    ID2D1Bitmap_Release(bitmap1);
    ID2D1RenderTarget_Release(rt1);
    hr = ID2D1Factory_CreateWicBitmapRenderTarget(factory1, wic_bitmap1, &desc, &rt1);
    ok(SUCCEEDED(hr), "Failed to create render target, hr %#x.\n", hr);
    hr = ID2D1RenderTarget_CreateBitmap(rt1, size, NULL, 0, &bitmap_desc, &bitmap1);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);

    hr = ID2D1Factory_CreateWicBitmapRenderTarget(factory2, wic_bitmap2, &desc, &rt2);
    ok(SUCCEEDED(hr), "Failed to create render target, hr %#x.\n", hr);
    hr = ID2D1RenderTarget_CreateSharedBitmap(rt2, &IID_ID2D1Bitmap, bitmap1, NULL, &bitmap2);
    ok(hr == D2DERR_WRONG_FACTORY, "Got unexpected hr %#x.\n", hr);
    ID2D1RenderTarget_Release(rt2);

    /* WIC bitmap render targets on the same D2D factory. */
    hr = ID2D1Factory_CreateWicBitmapRenderTarget(factory1, wic_bitmap2, &desc, &rt2);
    ok(SUCCEEDED(hr), "Failed to create render target, hr %#x.\n", hr);
    hr = ID2D1RenderTarget_CreateSharedBitmap(rt2, &IID_ID2D1Bitmap, bitmap1, NULL, &bitmap2);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);
    ID2D1Bitmap_Release(bitmap2);
    ID2D1RenderTarget_Release(rt2);

    ID2D1Bitmap_Release(bitmap1);
    ID2D1RenderTarget_Release(rt1);
    ID2D1Factory_Release(factory2);
    ID2D1Factory_Release(factory1);
    IWICBitmap_Release(wic_bitmap2);
    IWICBitmap_Release(wic_bitmap1);
    IDXGISurface_Release(surface2);
    IDXGISurface_Release(surface1);
    IDXGISwapChain_Release(swapchain2);
    IDXGISwapChain_Release(swapchain1);
    ID3D10Device1_Release(device2);
    ID3D10Device1_Release(device1);
    DestroyWindow(window2);
    DestroyWindow(window1);
    CoUninitialize();
}

static void test_bitmap_updates(void)
{
    D2D1_BITMAP_PROPERTIES bitmap_desc;
    IDXGISwapChain *swapchain;
    ID2D1RenderTarget *rt;
    ID3D10Device1 *device;
    IDXGISurface *surface;
    D2D1_RECT_U dst_rect;
    ID2D1Bitmap *bitmap;
    D2D1_COLOR_F color;
    D2D1_RECT_F rect;
    D2D1_SIZE_U size;
    HWND window;
    HRESULT hr;
    BOOL match;

    static const DWORD bitmap_data[] =
    {
        0xffff0000, 0xffffff00, 0xff00ff00, 0xff00ffff,
        0xff0000ff, 0xffff00ff, 0xff000000, 0xff7f7f7f,
        0xffffffff, 0xffffffff, 0xffffffff, 0xff000000,
        0xffffffff, 0xff000000, 0xff000000, 0xff000000,
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device, skipping tests.\n");
        return;
    }
    window = CreateWindowA("static", "d2d1_test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 640, 480, NULL, NULL, NULL, NULL);
    swapchain = create_swapchain(device, window, TRUE);
    hr = IDXGISwapChain_GetBuffer(swapchain, 0, &IID_IDXGISurface, (void **)&surface);
    ok(SUCCEEDED(hr), "Failed to get buffer, hr %#x.\n", hr);
    rt = create_render_target(surface);
    ok(!!rt, "Failed to create render target.\n");

    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_ALIASED);

    ID2D1RenderTarget_BeginDraw(rt);
    set_color(&color, 0.0f, 0.0f, 1.0f, 1.0f);
    ID2D1RenderTarget_Clear(rt, &color);

    set_size_u(&size, 4, 4);
    bitmap_desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmap_desc.dpiX = 96.0f;
    bitmap_desc.dpiY = 96.0f;
    hr = ID2D1RenderTarget_CreateBitmap(rt, size, NULL, 0, &bitmap_desc, &bitmap);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);

    set_rect(&rect, 0.0f, 0.0f, 320.0f, 240.0f);
    ID2D1RenderTarget_DrawBitmap(rt, bitmap, &rect, 1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, NULL);

    ID2D1Bitmap_Release(bitmap);

    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    hr = ID2D1RenderTarget_CreateBitmap(rt, size, NULL, 0, &bitmap_desc, &bitmap);
    ok(SUCCEEDED(hr), "Failed to create bitmap, hr %#x.\n", hr);

    set_rect(&rect, 0.0f, 240.0f, 320.0f, 480.0f);
    ID2D1RenderTarget_DrawBitmap(rt, bitmap, &rect, 1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, NULL);

    set_rect_u(&dst_rect, 1, 1, 3, 3);
    hr = ID2D1Bitmap_CopyFromMemory(bitmap, &dst_rect, bitmap_data, 4 * sizeof(*bitmap_data));
    ok(SUCCEEDED(hr), "Failed to update bitmap, hr %#x.\n", hr);
    set_rect_u(&dst_rect, 0, 3, 3, 4);
    hr = ID2D1Bitmap_CopyFromMemory(bitmap, &dst_rect, &bitmap_data[6], 4 * sizeof(*bitmap_data));
    ok(SUCCEEDED(hr), "Failed to update bitmap, hr %#x.\n", hr);
    set_rect_u(&dst_rect, 0, 0, 4, 1);
    hr = ID2D1Bitmap_CopyFromMemory(bitmap, &dst_rect, &bitmap_data[10], 4 * sizeof(*bitmap_data));
    ok(SUCCEEDED(hr), "Failed to update bitmap, hr %#x.\n", hr);
    set_rect_u(&dst_rect, 0, 1, 1, 3);
    hr = ID2D1Bitmap_CopyFromMemory(bitmap, &dst_rect, &bitmap_data[2], sizeof(*bitmap_data));
    ok(SUCCEEDED(hr), "Failed to update bitmap, hr %#x.\n", hr);
    set_rect_u(&dst_rect, 4, 4, 3, 1);
    hr = ID2D1Bitmap_CopyFromMemory(bitmap, &dst_rect, bitmap_data, sizeof(*bitmap_data));
    ok(SUCCEEDED(hr), "Failed to update bitmap, hr %#x.\n", hr);
    set_rect(&rect, 320.0f, 240.0f, 640.0f, 480.0f);
    ID2D1RenderTarget_DrawBitmap(rt, bitmap, &rect, 1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, NULL);

    hr = ID2D1Bitmap_CopyFromMemory(bitmap, NULL, bitmap_data, 4 * sizeof(*bitmap_data));
    ok(SUCCEEDED(hr), "Failed to update bitmap, hr %#x.\n", hr);
    set_rect(&rect, 320.0f, 0.0f, 640.0f, 240.0f);
    ID2D1RenderTarget_DrawBitmap(rt, bitmap, &rect, 1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, NULL);

    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ok(SUCCEEDED(hr), "Failed to end draw, hr %#x.\n", hr);

    match = compare_surface(surface, "cb8136c91fbbdc76bb83b8c09edc1907b0a5d0a6");
    ok(match, "Surface does not match.\n");

    ID2D1Bitmap_Release(bitmap);
    ID2D1RenderTarget_Release(rt);
    IDXGISurface_Release(surface);
    IDXGISwapChain_Release(swapchain);
    ID3D10Device1_Release(device);
    DestroyWindow(window);
}

START_TEST(d2d1)
{
    test_clip();
    test_state_block();
    test_color_brush();
    test_bitmap_brush();
    test_path_geometry();
    test_bitmap_formats();
    test_alpha_mode();
    test_shared_bitmap();
    test_bitmap_updates();
}
