/*
 * Copyright 2011 Joakim Sindholt <opensource@zhasha.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include "device9.h"
#include "query9.h"
#include "nine_helpers.h"
#include "pipe/p_context.h"
#include "util/u_math.h"
#include "nine_dump.h"

#define DBG_CHANNEL DBG_QUERY

#define QUERY_TYPE_MAP_CASE(a, b) case D3DQUERYTYPE_##a: return PIPE_QUERY_##b
static inline unsigned
d3dquerytype_to_pipe_query(D3DQUERYTYPE type)
{
    switch (type) {
    QUERY_TYPE_MAP_CASE(EVENT, GPU_FINISHED);
    QUERY_TYPE_MAP_CASE(OCCLUSION, OCCLUSION_COUNTER);
    QUERY_TYPE_MAP_CASE(TIMESTAMP, TIMESTAMP);
    QUERY_TYPE_MAP_CASE(TIMESTAMPDISJOINT, TIMESTAMP_DISJOINT);
    QUERY_TYPE_MAP_CASE(TIMESTAMPFREQ, TIMESTAMP_DISJOINT);
    QUERY_TYPE_MAP_CASE(VERTEXSTATS, PIPELINE_STATISTICS);
    case D3DQUERYTYPE_VCACHE:
    case D3DQUERYTYPE_RESOURCEMANAGER:
    case D3DQUERYTYPE_PIPELINETIMINGS:
    case D3DQUERYTYPE_INTERFACETIMINGS:
    case D3DQUERYTYPE_VERTEXTIMINGS:
    case D3DQUERYTYPE_PIXELTIMINGS:
    case D3DQUERYTYPE_BANDWIDTHTIMINGS:
    case D3DQUERYTYPE_CACHEUTILIZATION:
       return PIPE_QUERY_TYPES;
    default:
        return ~0;
    }
}

#define GET_DATA_SIZE_CASE9(a)    case D3DQUERYTYPE_##a: return sizeof(D3DDEVINFO_D3D9##a)
#define GET_DATA_SIZE_CASE1(a)    case D3DQUERYTYPE_##a: return sizeof(D3DDEVINFO_##a)
#define GET_DATA_SIZE_CASE2(a, b) case D3DQUERYTYPE_##a: return sizeof(D3DDEVINFO_##b)
#define GET_DATA_SIZE_CASET(a, b) case D3DQUERYTYPE_##a: return sizeof(b)
static INLINE DWORD
nine_query_result_size(D3DQUERYTYPE type)
{
    switch (type) {
    GET_DATA_SIZE_CASE1(VCACHE);
    GET_DATA_SIZE_CASE1(RESOURCEMANAGER);
    GET_DATA_SIZE_CASE2(VERTEXSTATS, D3DVERTEXSTATS);
    GET_DATA_SIZE_CASET(EVENT, BOOL);
    GET_DATA_SIZE_CASET(OCCLUSION, DWORD);
    GET_DATA_SIZE_CASET(TIMESTAMP, UINT64);
    GET_DATA_SIZE_CASET(TIMESTAMPDISJOINT, BOOL);
    GET_DATA_SIZE_CASET(TIMESTAMPFREQ, UINT64);
    GET_DATA_SIZE_CASE9(PIPELINETIMINGS);
    GET_DATA_SIZE_CASE9(INTERFACETIMINGS);
    GET_DATA_SIZE_CASE2(VERTEXTIMINGS, D3D9STAGETIMINGS);
    GET_DATA_SIZE_CASE2(PIXELTIMINGS, D3D9STAGETIMINGS);
    GET_DATA_SIZE_CASE9(BANDWIDTHTIMINGS);
    GET_DATA_SIZE_CASE9(CACHEUTILIZATION);
    /* GET_DATA_SIZE_CASE1(MEMORYPRESSURE); Win7 only */
    default:
        assert(0);
        return 0;
    }
}

HRESULT
nine_is_query_supported(D3DQUERYTYPE type)
{
    const unsigned ptype = d3dquerytype_to_pipe_query(type);

    user_assert(ptype != ~0, D3DERR_INVALIDCALL);

    if (ptype == PIPE_QUERY_TYPES) {
        DBG("Query type %u (%s) not supported.\n",
            type, nine_D3DQUERYTYPE_to_str(type));
        return D3DERR_NOTAVAILABLE;
    }
    return D3D_OK;
}

HRESULT
NineQuery9_ctor( struct NineQuery9 *This,
                 struct NineUnknownParams *pParams,
                 D3DQUERYTYPE Type )
{
    struct pipe_context *pipe = pParams->device->pipe;
    const unsigned ptype = d3dquerytype_to_pipe_query(Type);
    HRESULT hr;

    DBG("This=%p pParams=%p Type=%d\n", This, pParams, Type);

    hr = NineUnknown_ctor(&This->base, pParams);
    if (FAILED(hr))
        return hr;

    This->state = NINE_QUERY_STATE_FRESH;
    This->type = Type;

    user_assert(ptype != ~0, D3DERR_INVALIDCALL);

    if (ptype < PIPE_QUERY_TYPES) {
        This->pq = pipe->create_query(pipe, ptype, 0);
        if (!This->pq)
            return E_OUTOFMEMORY;
    } else {
        DBG("Returning dummy NineQuery9 for %s.\n",
            nine_D3DQUERYTYPE_to_str(Type));
    }

    This->instant =
        Type == D3DQUERYTYPE_EVENT ||
        Type == D3DQUERYTYPE_RESOURCEMANAGER ||
        Type == D3DQUERYTYPE_TIMESTAMP ||
        Type == D3DQUERYTYPE_TIMESTAMPFREQ ||
        Type == D3DQUERYTYPE_VCACHE ||
        Type == D3DQUERYTYPE_VERTEXSTATS;

    This->result_size = nine_query_result_size(Type);

    return D3D_OK;
}

void
NineQuery9_dtor( struct NineQuery9 *This )
{
    struct pipe_context *pipe = This->base.device->pipe;

    if (This->pq) {
        if (This->state == NINE_QUERY_STATE_RUNNING)
            pipe->end_query(pipe, This->pq);
        pipe->destroy_query(pipe, This->pq);
    }

    NineUnknown_dtor(&This->base);
}

D3DQUERYTYPE WINAPI
NineQuery9_GetType( struct NineQuery9 *This )
{
    return This->type;
}

DWORD WINAPI
NineQuery9_GetDataSize( struct NineQuery9 *This )
{
    return This->result_size;
}

HRESULT WINAPI
NineQuery9_Issue( struct NineQuery9 *This,
                  DWORD dwIssueFlags )
{
    struct pipe_context *pipe = This->base.device->pipe;

    DBG("This=%p dwIssueFlags=%d\n", This, dwIssueFlags);

    user_assert((dwIssueFlags == D3DISSUE_BEGIN && !This->instant) ||
                (dwIssueFlags == 0) ||
                (dwIssueFlags == D3DISSUE_END), D3DERR_INVALIDCALL);

    if (!This->pq) {
        DBG("Issued dummy query.\n");
        return D3D_OK;
    }

    if (dwIssueFlags == D3DISSUE_BEGIN) {
        if (This->state == NINE_QUERY_STATE_RUNNING) {
	    pipe->end_query(pipe, This->pq);
	}
        pipe->begin_query(pipe, This->pq);
        This->state = NINE_QUERY_STATE_RUNNING;
    } else {
        if (This->state != NINE_QUERY_STATE_RUNNING &&
            This->type != D3DQUERYTYPE_EVENT &&
            This->type != D3DQUERYTYPE_TIMESTAMP)
            pipe->begin_query(pipe, This->pq);
        pipe->end_query(pipe, This->pq);
        This->state = NINE_QUERY_STATE_ENDED;
    }
    return D3D_OK;
}

union nine_query_result
{
    D3DDEVINFO_D3DVERTEXSTATS vertexstats;
    D3DDEVINFO_D3D9BANDWIDTHTIMINGS bandwidth;
    D3DDEVINFO_VCACHE vcache;
    D3DDEVINFO_RESOURCEMANAGER rm;
    D3DDEVINFO_D3D9PIPELINETIMINGS pipe;
    D3DDEVINFO_D3D9STAGETIMINGS stage;
    D3DDEVINFO_D3D9INTERFACETIMINGS iface;
    D3DDEVINFO_D3D9CACHEUTILIZATION cacheu;
    DWORD dw;
    BOOL b;
    UINT64 u64;
};

HRESULT WINAPI
NineQuery9_GetData( struct NineQuery9 *This,
                    void *pData,
                    DWORD dwSize,
                    DWORD dwGetDataFlags )
{
    struct pipe_context *pipe = This->base.device->pipe;
    boolean ok = !This->pq;
    unsigned i;
    union pipe_query_result presult;
    union nine_query_result nresult;

    DBG("This=%p pData=%p dwSize=%d dwGetDataFlags=%d\n",
        This, pData, dwSize, dwGetDataFlags);

    user_assert(This->state != NINE_QUERY_STATE_RUNNING, D3DERR_INVALIDCALL);
    user_assert(dwSize == 0 || pData, D3DERR_INVALIDCALL);
    user_assert(dwGetDataFlags == 0 ||
                dwGetDataFlags == D3DGETDATA_FLUSH, D3DERR_INVALIDCALL);

    if (!This->pq) {
        DBG("No pipe query available.\n");
        if (!dwSize)
           return S_OK;
    }
    if (This->state == NINE_QUERY_STATE_FRESH)
        return S_OK;

    if (!ok) {
        ok = pipe->get_query_result(pipe, This->pq, FALSE, &presult);
        if (!ok) {
            if (dwGetDataFlags) {
                if (This->state != NINE_QUERY_STATE_FLUSHED)
                    pipe->flush(pipe, NULL, 0);
                This->state = NINE_QUERY_STATE_FLUSHED;
            }
            return S_FALSE;
        }
    }
    if (!dwSize)
        return S_OK;

    switch (This->type) {
    case D3DQUERYTYPE_EVENT:
        nresult.b = presult.b;
        break;
    case D3DQUERYTYPE_OCCLUSION:
        nresult.dw = presult.u64;
        break;
    case D3DQUERYTYPE_TIMESTAMP:
        nresult.u64 = presult.u64;
        break;
    case D3DQUERYTYPE_TIMESTAMPDISJOINT:
        nresult.b = presult.timestamp_disjoint.disjoint;
        break;
    case D3DQUERYTYPE_TIMESTAMPFREQ:
        nresult.u64 = presult.timestamp_disjoint.frequency;
        break;
    case D3DQUERYTYPE_VERTEXSTATS:
        nresult.vertexstats.NumRenderedTriangles =
            presult.pipeline_statistics.c_invocations;
        nresult.vertexstats.NumExtraClippingTriangles =
            presult.pipeline_statistics.c_primitives;
        break;
    /* Thse might be doable with driver-specific queries; dummy for now. */
    case D3DQUERYTYPE_BANDWIDTHTIMINGS:
        nresult.bandwidth.MaxBandwidthUtilized = 1.0f;
        nresult.bandwidth.FrontEndUploadMemoryUtilizedPercent = 0.5f;
        nresult.bandwidth.VertexRateUtilizedPercent = 0.75f;
        nresult.bandwidth.TriangleSetupRateUtilizedPercent = 0.75f;
        nresult.bandwidth.FillRateUtilizedPercent = 1.0f;
        break;
    case D3DQUERYTYPE_VERTEXTIMINGS:
    case D3DQUERYTYPE_PIXELTIMINGS:
        nresult.stage.MemoryProcessingPercent = 0.5f;
        nresult.stage.ComputationProcessingPercent = 0.5f;
        break;
    case D3DQUERYTYPE_VCACHE:
        /* Are we supposed to fill this in ? */
        nresult.vcache.Pattern = MAKEFOURCC('C', 'A', 'C', 'H');
        nresult.vcache.OptMethod = 1;
        nresult.vcache.CacheSize = 32 << 10;
        nresult.vcache.MagicNumber = 0xdeadcafe;
        break;
    case D3DQUERYTYPE_RESOURCEMANAGER:
        /* We could record some of these in the device ... */
        for (i = 0; i < D3DRTYPECOUNT; ++i) {
            nresult.rm.stats[i].bThrashing = FALSE;
            nresult.rm.stats[i].ApproxBytesDownloaded = 0;
            nresult.rm.stats[i].NumEvicts = 0;
            nresult.rm.stats[i].NumVidCreates = 0;
            nresult.rm.stats[i].LastPri = 0;
            nresult.rm.stats[i].NumUsed = 1;
            nresult.rm.stats[i].NumUsedInVidMem = 1;
            nresult.rm.stats[i].WorkingSet = 1;
            nresult.rm.stats[i].WorkingSetBytes = 1 << 20;
            nresult.rm.stats[i].TotalManaged = 1;
            nresult.rm.stats[i].TotalBytes = 1 << 20;
        }
        break;
    case D3DQUERYTYPE_PIPELINETIMINGS:
        nresult.pipe.VertexProcessingTimePercent = 0.4f;
        nresult.pipe.PixelProcessingTimePercent = 0.4f;
        nresult.pipe.OtherGPUProcessingTimePercent = 0.15f;
        nresult.pipe.GPUIdleTimePercent = 0.05f;
        break;
    case D3DQUERYTYPE_INTERFACETIMINGS:
        nresult.iface.WaitingForGPUToUseApplicationResourceTimePercent = 0.0f;
        nresult.iface.WaitingForGPUToAcceptMoreCommandsTimePercent = 0.0f;
        nresult.iface.WaitingForGPUToStayWithinLatencyTimePercent = 0.0f;
        nresult.iface.WaitingForGPUExclusiveResourceTimePercent = 0.0f;
        nresult.iface.WaitingForGPUOtherTimePercent = 0.0f;
        break;
    case D3DQUERYTYPE_CACHEUTILIZATION:
        nresult.cacheu.TextureCacheHitRate = 0.9f;
        nresult.cacheu.PostTransformVertexCacheHitRate = 0.3f;
        break;
    default:
        assert(0);
        break;
    }
    memcpy(pData, &nresult, MIN2(sizeof(nresult), dwSize));

    return S_OK;
}

IDirect3DQuery9Vtbl NineQuery9_vtable = {
    (void *)NineUnknown_QueryInterface,
    (void *)NineUnknown_AddRef,
    (void *)NineUnknown_Release,
    (void *)NineUnknown_GetDevice, /* actually part of Query9 iface */
    (void *)NineQuery9_GetType,
    (void *)NineQuery9_GetDataSize,
    (void *)NineQuery9_Issue,
    (void *)NineQuery9_GetData
};

static const GUID *NineQuery9_IIDs[] = {
    &IID_IDirect3DQuery9,
    &IID_IUnknown,
    NULL
};

HRESULT
NineQuery9_new( struct NineDevice9 *pDevice,
                struct NineQuery9 **ppOut,
                D3DQUERYTYPE Type )
{
    NINE_DEVICE_CHILD_NEW(Query9, ppOut, pDevice, Type);
}
