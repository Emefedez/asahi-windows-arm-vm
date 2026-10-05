/* d3d11fetch: vertex-fetch correctness tests. Every case draws a triangle that covers only the
   top-left part of a 256x256 target: pixel (40,40) must be green, pixel (200,200) must stay blue. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>

static ID3D11Device *dev; static ID3D11DeviceContext *ctx;
static ID3D11Texture2D *rt, *staging, *ds; static ID3D11RenderTargetView *rtv; static ID3D11DepthStencilView *dsv;
enum { W = 256, H = 256 };
static int fails;

static ID3DBlob *compile(const char *src, const char *target)
{
    ID3DBlob *code = NULL, *err = NULL;
    if (FAILED(D3DCompile(src, strlen(src), NULL, NULL, NULL, "main", target, 0, 0, &code, &err))) {
        printf("compile failed: %s\n", err ? (char *)ID3D10Blob_GetBufferPointer(err) : "?"); exit(3);
    }
    return code;
}
static ID3D11VertexShader *vs(const char *src, ID3DBlob **blob)
{
    ID3D11VertexShader *s; *blob = compile(src, "vs_4_0");
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(*blob), ID3D10Blob_GetBufferSize(*blob), NULL, &s); return s;
}
static ID3D11PixelShader *ps(const char *src)
{
    ID3D11PixelShader *s; ID3DBlob *b = compile(src, "ps_4_0");
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &s); return s;
}
static ID3D11Buffer *buf(const void *data, UINT size, UINT bind)
{
    D3D11_BUFFER_DESC bd = { size, D3D11_USAGE_DEFAULT, bind }; D3D11_SUBRESOURCE_DATA sd = { data };
    ID3D11Buffer *b; ID3D11Device_CreateBuffer(dev, &bd, &sd, &b); return b;
}
static ID3D11InputLayout *layout(const D3D11_INPUT_ELEMENT_DESC *e, UINT n, ID3DBlob *b)
{
    ID3D11InputLayout *il = NULL;
    HRESULT hr = ID3D11Device_CreateInputLayout(dev, e, n, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), &il);
    if (FAILED(hr)) printf("CreateInputLayout 0x%08lx\n", hr);
    return il;
}
static unsigned px(D3D11_MAPPED_SUBRESOURCE *m, int x, int y) { return *(unsigned *)((char *)m->pData + y * m->RowPitch + x * 4); }

static void begin(int depth)
{
    float blue[4] = { 0, 0, 1, 1 };
    ID3D11DeviceContext_ClearState(ctx);
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, blue);
    if (depth) ID3D11DeviceContext_ClearDepthStencilView(ctx, dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, depth ? dsv : NULL);
    D3D11_VIEWPORT vp = { 0, 0, W, H, 0, 1 };
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
static void check(const char *name)
{
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)rt);
    D3D11_MAPPED_SUBRESOURCE m;
    HRESULT hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m);
    if (FAILED(hr)) { printf("%-40s MAP FAILED 0x%08lx removed=0x%08lx\n", name, hr, ID3D11Device_GetDeviceRemovedReason(dev)); fails++; return; }
    unsigned in = px(&m, 40, 40), out = px(&m, 200, 200);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    int ok = in == 0xff00ff00 && out == 0xffff0000;
    if (!ok) fails++;
    printf("%-40s in=0x%08x out=0x%08x %s\n", name, in, out, ok ? "ok" : in == 0xffff0000 ? "FAIL (nothing drawn)" : "FAIL (wrong geometry/colour)");
}

/* top-left triangle in clip space */
#define TL0 -1.0f, 1.0f
#define TL1 0.2f, 1.0f
#define TL2 -1.0f, -0.2f

int main(void)
{
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx);
    if (FAILED(hr)) { printf("device 0x%08lx\n", hr); return 1; }
    D3D11_TEXTURE2D_DESC td = { W, H, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET };
    ID3D11Device_CreateTexture2D(dev, &td, NULL, &rt); ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)rt, NULL, &rtv);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Device_CreateTexture2D(dev, &td, NULL, &staging);
    D3D11_TEXTURE2D_DESC dd = { W, H, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL };
    ID3D11Device_CreateTexture2D(dev, &dd, NULL, &ds); ID3D11Device_CreateDepthStencilView(dev, (ID3D11Resource *)ds, NULL, &dsv);
    UINT zero = 0;

    ID3D11PixelShader *green = ps("float4 main() : SV_Target { return float4(0,1,0,1); }");
    ID3D11PixelShader *pscol = ps("float4 main(float4 p : SV_Position, float4 c : COLOR) : SV_Target { return c; }");

    /* 1. SV_VertexID only, no input layout */
    {
        ID3DBlob *b; ID3D11VertexShader *v = vs(
            "float4 main(uint id : SV_VertexID) : SV_Position {"
            " float2 t[3] = { float2(-1,1), float2(0.2,1), float2(-1,-0.2) }; return float4(t[id], 0.5, 1); }", &b);
        begin(0); ID3D11DeviceContext_VSSetShader(ctx, v, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, green, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0); check("SV_VertexID, no layout");
    }
    /* position-only vertex shader shared by several cases */
    ID3DBlob *bp; ID3D11VertexShader *vpos = vs("float4 main(float2 p : POSITION) : SV_Position { return float4(p, 0.5, 1); }", &bp);
    D3D11_INPUT_ELEMENT_DESC epos = { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    ID3D11InputLayout *ilpos = layout(&epos, 1, bp);
    float tri[] = { TL0, TL1, TL2 };
    ID3D11Buffer *vbtri = buf(tri, sizeof tri, D3D11_BIND_VERTEX_BUFFER);
    UINT s8 = 8;
    begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, ilpos); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbtri, &s8, &zero);
    ID3D11DeviceContext_VSSetShader(ctx, vpos, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, green, NULL, 0);
    ID3D11DeviceContext_Draw(ctx, 3, 0); check("float2 position, stride 8");

    /* 2. interleaved: [pad float][pos float2][color float4] stride 28, pos at offset 4 */
    {
        ID3DBlob *b; ID3D11VertexShader *v = vs(
            "struct O { float4 p : SV_Position; float4 c : COLOR; };"
            "O main(float2 p : POSITION, float4 c : COLOR) { O o; o.p = float4(p,0.5,1); o.c = c; return o; }", &b);
        D3D11_INPUT_ELEMENT_DESC e[2] = { { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 4, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                          { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        ID3D11InputLayout *il = layout(e, 2, b);
        float v7[] = { 9, TL0, 0,1,0,1,  9, TL1, 0,1,0,1,  9, TL2, 0,1,0,1 };
        ID3D11Buffer *vb = buf(v7, sizeof v7, D3D11_BIND_VERTEX_BUFFER); UINT s = 28;
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &s, &zero);
        ID3D11DeviceContext_VSSetShader(ctx, v, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, pscol, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0); check("interleaved stride 28, offsets 4/12");

        /* 3. same data, two streams: pos in slot 0 (stride 8), colour in slot 1 (stride 16) */
        D3D11_INPUT_ELEMENT_DESC e2[2] = { { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                           { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        ID3D11InputLayout *il2 = layout(e2, 2, b);
        float cols[] = { 0,1,0,1, 0,1,0,1, 0,1,0,1 };
        ID3D11Buffer *vbs[2] = { vbtri, buf(cols, sizeof cols, D3D11_BIND_VERTEX_BUFFER) }; UINT ss[2] = { 8, 16 }, oo[2] = { 0, 0 };
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il2); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 2, vbs, ss, oo);
        ID3D11DeviceContext_VSSetShader(ctx, v, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, pscol, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0); check("two streams (8 / 16)");

        /* 3b. streams in slots 1 and 3 (slot 0 unused) */
        D3D11_INPUT_ELEMENT_DESC e3[2] = { { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 3, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                           { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        ID3D11InputLayout *il3 = layout(e3, 2, b);
        ID3D11Buffer *vb4[4] = { NULL, vbs[1], NULL, vbtri }; UINT s4[4] = { 0, 16, 0, 8 }, o4[4] = { 0 };
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il3); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 4, vb4, s4, o4);
        ID3D11DeviceContext_VSSetShader(ctx, v, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, pscol, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0); check("streams in slots 3 and 1");

        /* 3c. layout element order differs from shader input order */
        D3D11_INPUT_ELEMENT_DESC e4[2] = { { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                           { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        ID3D11InputLayout *il4 = layout(e4, 2, b);
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il4); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 2, vbs, ss, oo);
        ID3D11DeviceContext_VSSetShader(ctx, v, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, pscol, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0); check("layout order != shader order");

        /* 3d. layout has an extra element the shader does not read (TEXCOORD5) */
        D3D11_INPUT_ELEMENT_DESC e5[3] = { { "TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                           { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                           { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        ID3D11InputLayout *il5 = layout(e5, 3, b);
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il5); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 2, vbs, ss, oo);
        ID3D11DeviceContext_VSSetShader(ctx, v, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, pscol, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0); check("layout with unused extra element");
    }
    /* 4. shader with an unused input between used ones (register gap) */
    {
        ID3DBlob *b; ID3D11VertexShader *v = vs(
            "struct O { float4 p : SV_Position; float4 c : COLOR; };"
            "O main(float4 a : TEXCOORD0, float4 unused : TEXCOORD1, float4 c : TEXCOORD2) { O o; o.p = float4(a.xy,0.5,1); o.c = c; return o; }", &b);
        D3D11_INPUT_ELEMENT_DESC e[3] = { { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                          { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                          { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        ID3D11InputLayout *il = layout(e, 3, b);
        float cols[] = { 0,1,0,1, 0,1,0,1, 0,1,0,1 };
        ID3D11Buffer *vbs[2] = { vbtri, buf(cols, sizeof cols, D3D11_BIND_VERTEX_BUFFER) }; UINT ss[2] = { 8, 16 }, oo[2] = { 0, 0 };
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 2, vbs, ss, oo);
        ID3D11DeviceContext_VSSetShader(ctx, v, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, pscol, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0); check("shader input gap (TEXCOORD0/1/2)");
    }
    /* 5. vertex buffer offset, start vertex */
    {
        float t2[] = { 5, 5, 5, 5, TL0, TL1, TL2 };  /* 16 bytes junk then the triangle */
        ID3D11Buffer *vb = buf(t2, sizeof t2, D3D11_BIND_VERTEX_BUFFER); UINT o16 = 16, o0 = 0;
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, ilpos); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &s8, &o16);
        ID3D11DeviceContext_VSSetShader(ctx, vpos, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, green, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0); check("vertex buffer offset 16");
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, ilpos); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &s8, &o0);
        ID3D11DeviceContext_VSSetShader(ctx, vpos, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, green, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 2); check("Draw StartVertexLocation 2");
        /* 6. indexed variants */
        unsigned short i16[] = { 7, 7, 2, 3, 4 }; unsigned i32[] = { 7, 2, 3, 4 }; unsigned short ib0[] = { 0, 1, 2 };
        ID3D11Buffer *b16 = buf(i16, sizeof i16, D3D11_BIND_INDEX_BUFFER), *b32 = buf(i32, sizeof i32, D3D11_BIND_INDEX_BUFFER), *bb = buf(ib0, sizeof ib0, D3D11_BIND_INDEX_BUFFER);
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, ilpos); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &s8, &o0);
        ID3D11DeviceContext_IASetIndexBuffer(ctx, b16, DXGI_FORMAT_R16_UINT, 0);
        ID3D11DeviceContext_VSSetShader(ctx, vpos, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, green, NULL, 0);
        ID3D11DeviceContext_DrawIndexed(ctx, 3, 2, 0); check("u16 indices, StartIndex 2");
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, ilpos); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &s8, &o0);
        ID3D11DeviceContext_IASetIndexBuffer(ctx, b16, DXGI_FORMAT_R16_UINT, 4);
        ID3D11DeviceContext_VSSetShader(ctx, vpos, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, green, NULL, 0);
        ID3D11DeviceContext_DrawIndexed(ctx, 3, 0, 0); check("u16 indices, index buffer offset 4");
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, ilpos); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &s8, &o0);
        ID3D11DeviceContext_IASetIndexBuffer(ctx, b32, DXGI_FORMAT_R32_UINT, 0);
        ID3D11DeviceContext_VSSetShader(ctx, vpos, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, green, NULL, 0);
        ID3D11DeviceContext_DrawIndexed(ctx, 3, 1, 0); check("u32 indices, StartIndex 1");
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, ilpos); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &s8, &o0);
        ID3D11DeviceContext_IASetIndexBuffer(ctx, bb, DXGI_FORMAT_R16_UINT, 0);
        ID3D11DeviceContext_VSSetShader(ctx, vpos, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, green, NULL, 0);
        ID3D11DeviceContext_DrawIndexed(ctx, 3, 0, 2); check("u16 indices, BaseVertex 2");
    }
    /* 7. non-float formats: R16G16_SNORM position, R8G8B8A8_UNORM colour */
    {
        ID3DBlob *b; ID3D11VertexShader *v = vs(
            "struct O { float4 p : SV_Position; float4 c : COLOR; };"
            "O main(float2 p : POSITION, float4 c : COLOR) { O o; o.p = float4(p,0.5,1); o.c = c; return o; }", &b);
        D3D11_INPUT_ELEMENT_DESC e[2] = { { "POSITION", 0, DXGI_FORMAT_R16G16_SNORM, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                          { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 4, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        ID3D11InputLayout *il = layout(e, 2, b);
        struct { short x, y; unsigned c; } v8[3] = { { -32767, 32767, 0xff00ff00 }, { 6553, 32767, 0xff00ff00 }, { -32767, -6553, 0xff00ff00 } };
        ID3D11Buffer *vb = buf(v8, sizeof v8, D3D11_BIND_VERTEX_BUFFER); UINT s = 8;
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &s, &zero);
        ID3D11DeviceContext_VSSetShader(ctx, v, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, pscol, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0); check("R16G16_SNORM pos + R8G8B8A8_UNORM col");
        D3D11_INPUT_ELEMENT_DESC e3[2] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                           { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        ID3D11InputLayout *il3 = layout(e3, 2, b);
        float v3[] = { TL0, 0.5f, 0,1,0,1,  TL1, 0.5f, 0,1,0,1,  TL2, 0.5f, 0,1,0,1 };
        ID3D11Buffer *vb3 = buf(v3, sizeof v3, D3D11_BIND_VERTEX_BUFFER); UINT s3 = 28;
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il3); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb3, &s3, &zero);
        ID3D11DeviceContext_VSSetShader(ctx, v, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, pscol, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0); check("R32G32B32_FLOAT pos (vec3) stride 28");
    }
    /* 8. instancing: per-instance offset moves the triangle; instance 1 of 2 lands top-left */
    {
        ID3DBlob *b; ID3D11VertexShader *v = vs(
            "float4 main(float2 p : POSITION, float2 o : OFFSET) : SV_Position { return float4(p + o, 0.5, 1); }", &b);
        D3D11_INPUT_ELEMENT_DESC e[2] = { { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                          { "OFFSET", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 } };
        ID3D11InputLayout *il = layout(e, 2, b);
        float offs[] = { 10, 10, 0, 0 };
        ID3D11Buffer *vbs[2] = { vbtri, buf(offs, sizeof offs, D3D11_BIND_VERTEX_BUFFER) }; UINT ss[2] = { 8, 8 }, oo[2] = { 0, 0 };
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 2, vbs, ss, oo);
        ID3D11DeviceContext_VSSetShader(ctx, v, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, green, NULL, 0);
        ID3D11DeviceContext_DrawInstanced(ctx, 3, 2, 0, 0); check("instanced (divisor 1)");
        float offs2[] = { 10, 10, 0, 0 };
        ID3D11Buffer *vbs2[2] = { vbtri, buf(offs2, sizeof offs2, D3D11_BIND_VERTEX_BUFFER) };
        begin(0); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 2, vbs2, ss, oo);
        ID3D11DeviceContext_VSSetShader(ctx, v, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, green, NULL, 0);
        ID3D11DeviceContext_DrawInstanced(ctx, 3, 1, 0, 1); check("instanced, StartInstanceLocation 1");
    }
    /* 9. depth buffer bound (default depth state: less, write) */
    begin(1); ID3D11DeviceContext_IASetInputLayout(ctx, ilpos); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbtri, &s8, &zero);
    ID3D11DeviceContext_VSSetShader(ctx, vpos, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, green, NULL, 0);
    ID3D11DeviceContext_Draw(ctx, 3, 0); check("depth D24S8 bound");
    printf("failures: %d\n", fails);
    return fails;
}
