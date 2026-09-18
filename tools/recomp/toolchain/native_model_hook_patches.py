"""Narrow hooks carrying immutable native mesh draws through the RT64 workload."""

PATCHES = {
    'src/rhi/rt64_render_hooks.h': [('namespace RT64 {', '''namespace RT64 {
    struct State;
    struct DisplayList;
    struct Workload;
    struct NativeMeshDraw {
        const Workload *workload = nullptr;
        uint32_t id = 0, vertexIndex = 0, viewProjIndex = 0, fbWidth = 0, fbHeight = 0;
        RenderRect scissor;
        RenderViewport viewport;
        float screenScale[2] = {1, 1}, screenOffset[2] = {};
        bool depthCompare = false, depthWrite = false;
    };
    using NativeMeshClassify = uint32_t(State *, const DisplayList *);
    using NativeMeshRender = bool(RenderCommandList *, RenderFramebuffer *, const NativeMeshDraw &);
    void SetNativeMeshHooks(NativeMeshClassify *, NativeMeshRender *);
    NativeMeshClassify *GetNativeMeshClassify();
    NativeMeshRender *GetNativeMeshRender();''')],
    'src/rhi/rt64_render_hooks.cpp': [('    static RenderHookInit *init = nullptr;', '''    static NativeMeshClassify *nativeClassify = nullptr;
    static NativeMeshRender *nativeRender = nullptr;
    void SetNativeMeshHooks(NativeMeshClassify *classify, NativeMeshRender *render) {
        nativeClassify = classify; nativeRender = render;
    }
    NativeMeshClassify *GetNativeMeshClassify() { return nativeClassify; }
    NativeMeshRender *GetNativeMeshRender() { return nativeRender; }
    static RenderHookInit *init = nullptr;''')],
    'src/hle/rt64_draw_call.h': [('    struct DrawCall {', '''    struct DrawCall {
        uint32_t nativeMeshId = 0;''')],
    'src/gbi/rt64_gbi_f3dex2.cpp': [
        ('#include "rt64_gbi_f3dex2.h"', '#include "rt64_gbi_f3dex2.h"\n#include "rhi/rt64_render_hooks.h"'),
        ('''        void tri1(State *state, DisplayList **dl) {
            state->rsp->drawIndexedTri((*dl)->p0(17, 7), (*dl)->p0(9, 7), (*dl)->p0(1, 7));
        }''', '''        void tri1(State *state, DisplayList **dl) {
            auto *hook = GetNativeMeshClassify();
            const uint32_t nativeId = hook ? hook(state, *dl) : 0;
            if (nativeId) {
                state->flush();
                state->drawCall.nativeMeshId = nativeId;
            }
            state->rsp->drawIndexedTri((*dl)->p0(17, 7), (*dl)->p0(9, 7), (*dl)->p0(1, 7));
            if (nativeId) {
                state->flush();
                state->drawCall.nativeMeshId = 0;
            }
        }''')],
    'src/render/rt64_framebuffer_renderer_call.h': [
        ('#include <stdint.h>', '#include <stdint.h>\n#include "rhi/rt64_render_hooks.h"'),
        ('        Type type;', '        Type type;\n        NativeMeshDraw nativeMesh;')],
    'src/render/rt64_framebuffer_renderer.cpp': [
        ('                const GameCall &call = proj.gameCalls[d];', '''                const GameCall &call = proj.gameCalls[d];
                instanceDrawCall.nativeMesh = {};
                if (call.callDesc.nativeMeshId) {
                    auto &native = instanceDrawCall.nativeMesh;
                    native.id = call.callDesc.nativeMeshId;
                    native.workload = p.curWorkload;
                    native.vertexIndex = drawData.faceIndices[call.meshDesc.faceIndicesStart];
                    native.viewProjIndex = proj.transformsIndex;
                    native.fbWidth = p.targetWidth / p.resolutionScale.x;
                    native.fbHeight = p.targetHeight / p.resolutionScale.y;
                    native.depthCompare = call.callDesc.otherMode.zCmp();
                    native.depthWrite = call.callDesc.otherMode.zUpd();
                }'''),
        ('                // A new pass must be started if decals are required and something wrote to the depth buffer before this call.', '''                if (drawCall.nativeMesh.id != 0 && GetNativeMeshRender() != nullptr) {
                    switchToDepthWrite();
                    auto native = drawCall.nativeMesh;
                    native.scissor = triangles.scissor;
                    native.viewport = framebuffer.viewport;
                    native.screenScale[0] = triangles.screenScale.x;
                    native.screenScale[1] = triangles.screenScale.y;
                    native.screenOffset[0] = triangles.screenOffset.x;
                    native.screenOffset[1] = triangles.screenOffset.y;
                    if (GetNativeMeshRender()(worker->commandList.get(), fbStorage->colorDepthWrite.get(), native)) {
                        switchToGraphicsPipeline();
                        continue;
                    }
                }

                // A new pass must be started if decals are required and something wrote to the depth buffer before this call.''')],
}
