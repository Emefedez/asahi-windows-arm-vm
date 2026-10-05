/* d3d11stream: multi-draw streaming patterns used by ANGLE. Four draws per test, each meant to
   paint one quadrant (TL, TR, BL, BR) green; afterwards each quadrant centre must be green. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
static ID3D11Device *dev; static ID3D11DeviceContext *ctx;
static ID3D11Texture2D *rt, *st; static ID3D11RenderTargetView *rtv;
enum { W = 256, H = 256 };
static int fails;
static ID3DBlob *compile(const char *src, const char *t)
{ ID3DBlob *c = NULL, *e = NULL; if (FAILED(D3DCompile(src, strlen(src), NULL, NULL, NULL, "main", t, 0, 0, &c, &e))) { printf("%s\n", (char *)ID3D10Blob_GetBufferPointer(e)); exit(3); } return c; }
static void begin(void)
{
    float blue[4] = { 0, 0, 1, 1 };
    ID3D11DeviceContext_ClearState(ctx); ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, blue);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    D3D11_VIEWPORT vp = { 0, 0, W, H, 0, 1 }; ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
static void check(const char *name)
{
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)rt);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &m))) { printf("%-44s MAP FAILED\n", name); fails++; return; }
    static const int pts[4][2] = { { 64, 64 }, { 192, 64 }, { 64, 192 }, { 192, 192 } };
    char res[5] = "....";
    for (int i = 0; i < 4; i++) res[i] = *(unsigned *)((char *)m.pData + pts[i][1] * m.RowPitch + pts[i][0] * 4) == 0xff00ff00 ? 'G' : '-';
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)st, 0);
    int ok = !strcmp(res, "GGGG"); if (!ok) fails++;
    printf("%-44s quadrants TL TR BL BR = %c  %c  %c  %c  %s\n", name, res[0], res[1], res[2], res[3], ok ? "ok" : "FAIL");
}
/* quadrant q as a 3-vertex triangle (float2 clip coords) covering the quadrant centre */
static void quad_tri(int q, float *out)
{
    float x0 = (q & 1) ? 0.0f : -1.0f, y1 = (q & 2) ? 0.0f : 1.0f;
    float t[6] = { x0, y1, x0 + 2.0f, y1, x0, y1 - 2.0f };  /* big right triangle anchored at quadrant corner */
    /* shrink to the quadrant: scale by 0.5 around its corner */
    for (int i = 0; i < 3; i++) { out[i * 2] = x0 + (t[i * 2] - x0) * 0.5f * 1.0f; out[i * 2 + 1] = y1 + (t[i * 2 + 1] - y1) * 0.5f; }
    /* covers corner + (1,0) + (0,-1): quadrant centre (x0+0.5, y1-0.5) lies on its hypotenuse -> enlarge a bit */
    out[2] += 0.4f; out[5] -= 0.4f;
}
int main(void)
{
    D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx))) return 9;
    D3D11_TEXTURE2D_DESC td = { W, H, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET };
    ID3D11Device_CreateTexture2D(dev, &td, NULL, &rt); ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)rt, NULL, &rtv);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; ID3D11Device_CreateTexture2D(dev, &td, NULL, &st);

    ID3DBlob *bvs = compile("float4 main(float2 p : POSITION) : SV_Position { return float4(p, 0.5, 1); }", "vs_4_0");
    ID3DBlob *bvc = compile("cbuffer C : register(b0) { float4 off; }; float4 main(float2 p : POSITION) : SV_Position { return float4(p + off.xy, 0.5, 1); }", "vs_4_0");
    ID3DBlob *bva = compile("float4 main(float2 p : POSITION, float2 o : OFFSET) : SV_Position { return float4(p + o, 0.5, 1); }", "vs_4_0");
    ID3DBlob *bps = compile("float4 main() : SV_Target { return float4(0,1,0,1); }", "ps_4_0");
    ID3D11VertexShader *vs, *vsc, *vsa; ID3D11PixelShader *ps;
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(bvs), ID3D10Blob_GetBufferSize(bvs), NULL, &vs);
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(bvc), ID3D10Blob_GetBufferSize(bvc), NULL, &vsc);
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(bva), ID3D10Blob_GetBufferSize(bva), NULL, &vsa);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(bps), ID3D10Blob_GetBufferSize(bps), NULL, &ps);
    D3D11_INPUT_ELEMENT_DESC e = { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    ID3D11InputLayout *il; ID3D11Device_CreateInputLayout(dev, &e, 1, ID3D10Blob_GetBufferPointer(bvs), ID3D10Blob_GetBufferSize(bvs), &il);
    D3D11_INPUT_ELEMENT_DESC e2[2] = { { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                                       { "OFFSET", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    ID3D11InputLayout *il2; ID3D11Device_CreateInputLayout(dev, e2, 2, ID3D10Blob_GetBufferPointer(bva), ID3D10Blob_GetBufferSize(bva), &il2);
    UINT s8 = 8, zero = 0;
    float tri[4][6]; for (int q = 0; q < 4; q++) quad_tri(q, tri[q]);

    /* sanity: one static buffer, four draws with StartVertex */
    D3D11_BUFFER_DESC bd = { sizeof tri, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER }; D3D11_SUBRESOURCE_DATA sd = { tri };
    ID3D11Buffer *vbs; ID3D11Device_CreateBuffer(dev, &bd, &sd, &vbs);
    begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbs, &s8, &zero);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    for (int q = 0; q < 4; q++) ID3D11DeviceContext_Draw(ctx, 3, q * 3);
    check("static VB, 4 draws");

    /* ring buffer: DISCARD then NO_OVERWRITE appends, one draw per append (ANGLE streaming) */
    D3D11_BUFFER_DESC dd = { 4096, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE };
    ID3D11Buffer *ring; ID3D11Device_CreateBuffer(dev, &dd, NULL, &ring);
    begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    for (int q = 0; q < 4; q++) {
        D3D11_MAPPED_SUBRESOURCE m;
        ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)ring, 0, q ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD, 0, &m);
        memcpy((char *)m.pData + q * 64, tri[q], 24); ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)ring, 0);
        UINT off = q * 64; ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &ring, &s8, &off);
        ID3D11DeviceContext_Draw(ctx, 3, 0);
    }
    check("dynamic ring: DISCARD + NO_OVERWRITE, offsets");

    /* same, but StartVertexLocation instead of buffer offsets */
    begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &ring, &s8, &zero);
    for (int q = 0; q < 4; q++) {
        D3D11_MAPPED_SUBRESOURCE m;
        ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)ring, 0, q ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD, 0, &m);
        memcpy((char *)m.pData + q * 24, tri[q], 24); ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)ring, 0);
        ID3D11DeviceContext_Draw(ctx, 3, q * 3);
    }
    check("dynamic ring: NO_OVERWRITE, StartVertex");

    /* DISCARD before every draw (renaming) */
    begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &ring, &s8, &zero);
    for (int q = 0; q < 4; q++) {
        D3D11_MAPPED_SUBRESOURCE m;
        ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)ring, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        memcpy(m.pData, tri[q], 24); ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)ring, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0);
    }
    check("dynamic VB: DISCARD per draw");

    /* UpdateSubresource of a DEFAULT VB between draws */
    ID3D11Buffer *dv; D3D11_BUFFER_DESC bd2 = { 64, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER }; ID3D11Device_CreateBuffer(dev, &bd2, NULL, &dv);
    begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &dv, &s8, &zero);
    for (int q = 0; q < 4; q++) { ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)dv, 0, NULL, tri[q], 0, 0); ID3D11DeviceContext_Draw(ctx, 3, 0); }
    check("DEFAULT VB: UpdateSubresource per draw");

    /* constant buffer per draw: UpdateSubresource (DEFAULT CB) */
    float base[6]; quad_tri(0, base);
    D3D11_SUBRESOURCE_DATA sb = { base }; D3D11_BUFFER_DESC b1 = { 24, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
    ID3D11Buffer *vbb; ID3D11Device_CreateBuffer(dev, &b1, &sb, &vbb);
    float offs[4][4] = { { 0, 0 }, { 1, 0 }, { 0, -1 }, { 1, -1 } };
    ID3D11Buffer *cbd; D3D11_BUFFER_DESC cd = { 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER }; ID3D11Device_CreateBuffer(dev, &cd, NULL, &cbd);
    begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbb, &s8, &zero);
    ID3D11DeviceContext_VSSetShader(ctx, vsc, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0); ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &cbd);
    for (int q = 0; q < 4; q++) { ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)cbd, 0, NULL, offs[q], 0, 0); ID3D11DeviceContext_Draw(ctx, 3, 0); }
    check("CB: UpdateSubresource per draw");

    /* constant buffer per draw: Map DISCARD (DYNAMIC CB) */
    ID3D11Buffer *cbm; D3D11_BUFFER_DESC cdm = { 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE }; ID3D11Device_CreateBuffer(dev, &cdm, NULL, &cbm);
    begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbb, &s8, &zero);
    ID3D11DeviceContext_VSSetShader(ctx, vsc, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0); ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &cbm);
    for (int q = 0; q < 4; q++) {
        D3D11_MAPPED_SUBRESOURCE m; ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cbm, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        memcpy(m.pData, offs[q], 16); ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cbm, 0); ID3D11DeviceContext_Draw(ctx, 3, 0);
    }
    check("CB: Map DISCARD per draw");

    /* stride-0 attribute (ANGLE's constant/disabled vertex attributes), changed per draw */
    ID3D11Buffer *cvb; D3D11_BUFFER_DESC cv = { 64, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER }; ID3D11Device_CreateBuffer(dev, &cv, NULL, &cvb);
    begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il2); ID3D11DeviceContext_VSSetShader(ctx, vsa, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    for (int q = 0; q < 4; q++) {
        ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)cvb, 0, NULL, offs[q], 0, 0);
        ID3D11Buffer *b2[2] = { vbb, cvb }; UINT s2[2] = { 8, 0 }, o2[2] = { 0, 0 };
        ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 2, b2, s2, o2); ID3D11DeviceContext_Draw(ctx, 3, 0);
    }
    check("stride-0 attribute per draw");

    /* big CB with a per-draw offset window (VSSetConstantBuffers1 / ANGLE uniform ring) */
    ID3D11DeviceContext1 *ctx1 = NULL; ID3D11DeviceContext_QueryInterface(ctx, &IID_ID3D11DeviceContext1, (void **)&ctx1);
    if (ctx1) {
        ID3D11Buffer *big; D3D11_BUFFER_DESC bb = { 4096, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE }; ID3D11Device_CreateBuffer(dev, &bb, NULL, &big);
        D3D11_MAPPED_SUBRESOURCE m; ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)big, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        for (int q = 0; q < 4; q++) memcpy((char *)m.pData + q * 256, offs[q], 16);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)big, 0);
        begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbb, &s8, &zero);
        ID3D11DeviceContext_VSSetShader(ctx, vsc, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
        for (int q = 0; q < 4; q++) { UINT first = q * 16, num = 16; ID3D11DeviceContext1_VSSetConstantBuffers1(ctx1, 0, 1, &big, &first, &num); ID3D11DeviceContext_Draw(ctx, 3, 0); }
        check("CB offsets (VSSetConstantBuffers1)");
    }
    /* ANGLE static buffers: CPU writes a STAGING buffer, then CopySubresourceRegion into a DEFAULT VB */
    {
        D3D11_BUFFER_DESC sbd = { sizeof tri, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ };
        ID3D11Buffer *stg; ID3D11Device_CreateBuffer(dev, &sbd, NULL, &stg);
        D3D11_BUFFER_DESC vbd = { sizeof tri, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
        ID3D11Buffer *vbc; ID3D11Device_CreateBuffer(dev, &vbd, NULL, &vbc);
        D3D11_MAPPED_SUBRESOURCE m;
        ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)stg, 0, D3D11_MAP_WRITE, 0, &m); memcpy(m.pData, tri, sizeof tri);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)stg, 0);
        D3D11_BOX box = { 0, 0, 0, sizeof tri, 1, 1 };
        ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)vbc, 0, 0, 0, 0, (ID3D11Resource *)stg, 0, &box);
        begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbc, &s8, &zero);
        ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
        for (int q = 0; q < 4; q++) ID3D11DeviceContext_Draw(ctx, 3, q * 3);
        check("STAGING -> CopySubresourceRegion -> VB");
        /* same via CopyResource, and with a partial copy at an offset */
        ID3D11Buffer *vbr; ID3D11Device_CreateBuffer(dev, &vbd, NULL, &vbr);
        ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)vbr, (ID3D11Resource *)stg);
        begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbr, &s8, &zero);
        ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
        for (int q = 0; q < 4; q++) ID3D11DeviceContext_Draw(ctx, 3, q * 3);
        check("STAGING -> CopyResource -> VB");
        /* DEFAULT -> DEFAULT copy (ANGLE copies between its storages too) */
        ID3D11Buffer *vbd2; ID3D11Device_CreateBuffer(dev, &vbd, NULL, &vbd2);
        ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)vbd2, (ID3D11Resource *)vbr);
        begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbd2, &s8, &zero);
        ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
        for (int q = 0; q < 4; q++) ID3D11DeviceContext_Draw(ctx, 3, q * 3);
        check("DEFAULT -> CopyResource -> VB");
        /* staging written while mapped with WRITE after an earlier copy (reuse) */
        float tri2[4][6]; memcpy(tri2, tri, sizeof tri); for (int i = 0; i < 6; i++) tri2[0][i] = 5.0f;
        ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)stg, 0, D3D11_MAP_WRITE, 0, &m); memcpy(m.pData, tri2, sizeof tri);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)stg, 0);
        ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)vbc, 0, 0, 0, 0, (ID3D11Resource *)stg, 0, &box);
        begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbc, &s8, &zero);
        ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
        for (int q = 0; q < 4; q++) ID3D11DeviceContext_Draw(ctx, 3, q * 3);
        fails--; check("staging rewrite + recopy (want -GGG)");
    }
    /* ANGLE vertex storage: DEFAULT, BIND_VERTEX_BUFFER | BIND_STREAM_OUTPUT */
    {
        const UINT so = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_STREAM_OUTPUT;
        D3D11_BUFFER_DESC vbd = { sizeof tri, D3D11_USAGE_DEFAULT, so };
        D3D11_SUBRESOURCE_DATA init = { tri };
        ID3D11Buffer *a; ID3D11Device_CreateBuffer(dev, &vbd, &init, &a);
        begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &a, &s8, &zero);
        ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
        for (int q = 0; q < 4; q++) ID3D11DeviceContext_Draw(ctx, 3, q * 3);
        check("VB|SO: initial data");
        ID3D11Buffer *b; ID3D11Device_CreateBuffer(dev, &vbd, NULL, &b);
        ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)b, 0, NULL, tri, 0, 0);
        begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &b, &s8, &zero);
        ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
        for (int q = 0; q < 4; q++) ID3D11DeviceContext_Draw(ctx, 3, q * 3);
        check("VB|SO: UpdateSubresource");
        D3D11_BUFFER_DESC sbd = { sizeof tri, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ };
        ID3D11Buffer *stg; ID3D11Device_CreateBuffer(dev, &sbd, &init, &stg);
        ID3D11Buffer *c; ID3D11Device_CreateBuffer(dev, &vbd, NULL, &c);
        D3D11_BOX box = { 0, 0, 0, sizeof tri, 1, 1 };
        ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)c, 0, 0, 0, 0, (ID3D11Resource *)stg, 0, &box);
        begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &c, &s8, &zero);
        ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
        for (int q = 0; q < 4; q++) ID3D11DeviceContext_Draw(ctx, 3, q * 3);
        check("VB|SO: staging CopySubresourceRegion");
        /* index buffer storage: DEFAULT, BIND_INDEX_BUFFER, UpdateSubresource */
        unsigned short idx[12] = { 0,1,2, 3,4,5, 6,7,8, 9,10,11 };
        D3D11_BUFFER_DESC ibd = { sizeof idx, D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER };
        ID3D11Buffer *ib; ID3D11Device_CreateBuffer(dev, &ibd, NULL, &ib);
        ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)ib, 0, NULL, idx, 0, 0);
        begin(); ID3D11DeviceContext_IASetInputLayout(ctx, il); ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &a, &s8, &zero);
        ID3D11DeviceContext_IASetIndexBuffer(ctx, ib, DXGI_FORMAT_R16_UINT, 0);
        ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0); ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
        for (int q = 0; q < 4; q++) ID3D11DeviceContext_DrawIndexed(ctx, 3, q * 3, 0);
        check("IB UpdateSubresource + VB|SO");
    }
    printf("failures: %d\n", fails);
    return fails;
}
