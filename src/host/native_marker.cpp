#define HLSL_CPU
#include "native_marker.hpp"
#include "presentation/image_mode.hpp"
#include "hle/rt64_state.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_workload.h"
#include "rhi/rt64_render_hooks.h"
#include "plume_metal.h"
#include "json/json.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace srw64::marker {
namespace {
using json = nlohmann::json;
std::vector<uint8_t> reference, vertices, indices;
std::filesystem::path output;
MTL::Device *device{};
MTL::Buffer *vertexBuffer{}, *indexBuffer{};
MTL::RenderPipelineState *pipeline{};
std::array<MTL::DepthStencilState *, 4> depthStates{};
MTL::PixelFormat colorFormat{}, depthFormat{};
NS::UInteger sampleCount{};
std::atomic<uint64_t> classified{};
std::atomic<uint64_t> original_models{};
uint64_t rendered{}, suppressed{};
std::ofstream draws;

std::vector<uint8_t> read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Missing native marker asset: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

uint32_t classify(RT64::State *state, const RT64::DisplayList *dl) {
    // Segment 4 is established by the game each time it calls a model. Never
    // identify an asset by its incidental heap address or by its color alone.
    const uint32_t base = state->rsp->fromSegmentedMasked(0x04000000);
    const auto pointer = reinterpret_cast<uintptr_t>(dl);
    const auto start = reinterpret_cast<uintptr_t>(state->RDRAM) + base;
    if (base > 0x800000 - reference.size() || pointer < start) return 0;
    const uintptr_t offset = pointer - start;
    constexpr std::array<uint32_t, 8> commands{0x1a90,0x1a98,0x1aa0,0x1aa8,0x1b18,0x1b20,0x1b28,0x1b30};
    if (std::find(commands.begin(), commands.end(), offset) == commands.end()) return 0;
    if (std::memcmp(state->RDRAM + base, reference.data(), reference.size())) return 0;
    // Decide while building the workload. Original mode must retain all eight
    // guest triangles, rather than merely skipping the native GPU draw later.
    // Image-mode changes drain submitted workloads before classification resumes.
    if (!replacement_enabled()) {
        if (offset == commands.front()) ++original_models;
        return 0;
    }
    ++classified;
    return offset == commands.front() ? 5600 : 5601;
}

// All float4 fields keep the CPU/Metal uniform layout explicit. hlsl++ stores
// rows; Metal interprets these bytes as columns, matching RT64's row-vector math.
struct Uniforms {
    float mvp[16], normalView[16];
    float viewportScale[4], viewportTranslate[4], resolution[4], screen[4];
};

void make_pipeline(MTL::Texture *color, MTL::Texture *depth) {
    if (pipeline && colorFormat == color->pixelFormat() && depthFormat == depth->pixelFormat()
        && sampleCount == color->sampleCount()) return;
    const char *source = R"(
        #include <metal_stdlib>
        using namespace metal;
        struct Vertex { packed_float3 position; packed_float3 normal; };
        struct Uniforms {
            float4x4 mvp, normalView;
            float4 viewportScale, viewportTranslate, resolution, screen;
        };
        struct V { float4 p [[position]]; float3 normal; };
        vertex V vs(uint i [[vertex_id]], const device Vertex *vertices [[buffer(0)]], constant Uniforms& u [[buffer(1)]]) {
            float4 p = u.mvp * float4(float3(vertices[i].position), 1);
            float3 screen = p.xyz / float3(p.w, -p.w, p.w) * u.viewportScale.xyz + u.viewportTranslate.xyz;
            float2 clip = (screen.xy - u.resolution.xy * .5f) / (u.resolution.xy * float2(.5f, -.5f));
            clip = clip * u.screen.xy + u.screen.zw;
            return {float4(clip * p.w, screen.z * p.w, p.w), (u.normalView * float4(float3(vertices[i].normal), 0)).xyz};
        }
        fragment float4 fs(V in [[stage_in]]) {
            // Faceted gold marker (original eight faces with rounded edges): each flat
            // facet reflects a bright sky or a warm ground by its orientation, and a
            // horizon band sweeps across facets and edges as the game spins it.
            float3 n = normalize(in.normal);
            float3 view = float3(0,0,1);
            float3 key = normalize(float3(-.55f,.8f,1.0f));
            float3 fill = normalize(float3(.75f,.1f,.5f));
            float3 r = reflect(-view, n);
            float sky = smoothstep(-.35f, .85f, r.y);
            float3 env = mix(float3(.55f,.38f,.08f), float3(1.0f,.96f,.80f), sky);
            env += float3(1.0f,.97f,.85f) * .5f * exp(-pow((r.y - .12f) / .07f, 2.0f));
            float diffuse = max(dot(n,key),0.0f);
            float broad = pow(max(dot(n,normalize(key+view)),0.0f),16.0f);
            float sharp = pow(max(dot(n,normalize(float3(-.35f,.65f,1.5f)+view)),0.0f),70.0f);
            float rim = pow(1.0f-abs(dot(n,view)),3.0f);
            float3 gold = float3(1.0f,.82f,.16f);
            float3 c = gold * (.14f + .36f*diffuse + .10f*max(dot(n,fill),0.0f)) + gold * env * .62f;
            c += float3(1.0f,.92f,.65f)*broad*.32f + float3(1.0f,.98f,.9f)*sharp*.7f;
            c += float3(.35f,.24f,.06f)*rim;
            return float4(saturate(c),1);
        }
    )";
    NS::Error *error{};
    auto *library = device->newLibrary(NS::String::string(source, NS::UTF8StringEncoding), nullptr, &error);
    if (!library) throw std::runtime_error(error ? error->localizedDescription()->utf8String() : "Native marker shader failed");
    auto *vs = library->newFunction(NS::String::string("vs", NS::UTF8StringEncoding));
    auto *fs = library->newFunction(NS::String::string("fs", NS::UTF8StringEncoding));
    auto *desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vs); desc->setFragmentFunction(fs);
    desc->colorAttachments()->object(0)->setPixelFormat(color->pixelFormat());
    desc->setDepthAttachmentPixelFormat(depth->pixelFormat());
    desc->setRasterSampleCount(color->sampleCount());
    auto *next = device->newRenderPipelineState(desc, &error);
    desc->release(); vs->release(); fs->release(); library->release();
    if (!next) throw std::runtime_error(error ? error->localizedDescription()->utf8String() : "Native marker pipeline failed");
    if (pipeline) pipeline->release();
    pipeline = next; colorFormat = color->pixelFormat(); depthFormat = depth->pixelFormat(); sampleCount = color->sampleCount();
}

bool render(plume::RenderCommandList *list, plume::RenderFramebuffer *framebuffer, const RT64::NativeMeshDraw& call) {
    if (call.id == 5601) { ++suppressed; return true; }
    if (call.id != 5600) return false;
    if (!device || !call.workload) throw std::runtime_error("Native marker missing immutable draw context");
    const auto& d = call.workload->drawData;
    const auto worldIndex = d.worldIndices.at(call.vertexIndex);
    const auto& world = d.lerpWorldTransforms.empty() ? d.worldTransforms.at(worldIndex) : d.lerpWorldTransforms.at(worldIndex);
    const auto& vp = d.modViewProjTransforms.empty() ? d.viewProjTransforms.at(call.viewProjIndex) : d.modViewProjTransforms.at(call.viewProjIndex);
    const auto& view = d.modViewTransforms.empty() ? d.viewTransforms.at(call.viewProjIndex) : d.modViewTransforms.at(call.viewProjIndex);
    const auto mvp = hlslpp::mul(world, vp);
    const auto normalView = hlslpp::transpose(hlslpp::inverse(hlslpp::mul(world, view)));
    const auto& viewport = d.rspViewports.at(call.viewProjIndex);
    Uniforms u{};
    hlslpp::store(mvp, u.mvp); hlslpp::store(normalView, u.normalView);
    hlslpp::store(viewport.scale, u.viewportScale); hlslpp::store(viewport.translate, u.viewportTranslate);
    u.resolution[0] = call.fbWidth; u.resolution[1] = call.fbHeight;
    u.screen[0] = call.screenScale[0]; u.screen[1] = call.screenScale[1];
    u.screen[2] = call.screenOffset[0]; u.screen[3] = call.screenOffset[1];
    const auto *fb = static_cast<const plume::MetalFramebuffer *>(framebuffer);
    if (fb->colorAttachments.size() != 1 || !fb->depthAttachment.getTexture())
        throw std::runtime_error("Native marker requires original scene color and depth attachments");
    auto *color = fb->colorAttachments[0].getTexture();
    auto *depth = fb->depthAttachment.getTexture();
    make_pipeline(color, depth);
    auto *command = static_cast<plume::MetalCommandList *>(list);
    command->endActiveRenderEncoder(); command->endActiveBlitEncoder();
    auto *pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto *attachment = pass->colorAttachments()->object(0);
    attachment->setTexture(color); attachment->setLoadAction(MTL::LoadActionLoad); attachment->setStoreAction(MTL::StoreActionStore);
    pass->depthAttachment()->setTexture(depth);
    pass->depthAttachment()->setLoadAction(MTL::LoadActionLoad); pass->depthAttachment()->setStoreAction(MTL::StoreActionStore);
    auto *encoder = command->mtl->renderCommandEncoder(pass);
    encoder->setLabel(NS::String::string("SRW64 native 5600 waterdrop", NS::UTF8StringEncoding));
    encoder->setRenderPipelineState(pipeline);
    encoder->setDepthStencilState(depthStates[(call.depthCompare ? 1 : 0) | (call.depthWrite ? 2 : 0)]);
    encoder->setViewport(MTL::Viewport{call.viewport.x, call.viewport.y, call.viewport.width, call.viewport.height, call.viewport.minDepth, call.viewport.maxDepth});
    const auto& s = call.scissor;
    encoder->setScissorRect(MTL::ScissorRect{NS::UInteger(s.left), NS::UInteger(s.top), NS::UInteger(s.right-s.left), NS::UInteger(s.bottom-s.top)});
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise); encoder->setCullMode(MTL::CullModeBack);
    encoder->setVertexBuffer(vertexBuffer, 0, 0); encoder->setVertexBytes(&u, sizeof(u), 1);
    encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, indices.size()/4, MTL::IndexTypeUInt32, indexBuffer, 0);
    encoder->endEncoding();
    ++rendered;
    if (rendered == 1 || rendered % 60 == 0) {
        const auto origin = hlslpp::mul(hlslpp::float4(0,0,0,1), mvp);
        const auto screen = (origin.xyz / hlslpp::float3(origin.w,-origin.w,origin.w)) * viewport.scale + viewport.translate;
        std::array<float,3> center{}; hlslpp::store(screen,center.data());
        json record = {{"workload",call.workload->workloadId},{"draw",rendered},{"resource",5600},
            {"image_mode",presentation::image_mode.current()},
            {"triangles",indices.size()/12},{"world_index",worldIndex},{"projection_index",call.viewProjIndex},
            {"center",center},{"world",std::vector<float>(u.mvp,u.mvp+16)},
            {"depth_compare",call.depthCompare},{"depth_write",call.depthWrite},{"shared_scene_depth",true},
            {"scissor",{s.left,s.top,s.right,s.bottom}}};
        draws << record.dump() << '\n'; draws.flush();
    }
    return true;
}
}

bool replacement_enabled() {
    // Standalone model probes have no image-mode controller and keep their
    // explicit model selection. Profiles couple it to the applied HD mode.
    return !reference.empty() && (!presentation::image_mode.enabled() || presentation::image_mode.current()==1);
}

void configure(const std::filesystem::path& directory) {
    const char *pack = std::getenv("SRW64_NATIVE_MARKER");
    if (!pack || !*pack) return;
    output = directory;
    const std::filesystem::path path(pack);
    reference = read(path / "reference.bin"); vertices = read(path / "vertices.bin"); indices = read(path / "indices.bin");
    if (reference.size() != 7048 || vertices.empty() || vertices.size()%24 || indices.empty() || indices.size()%12)
        throw std::runtime_error("Invalid native marker asset sizes");
    for (size_t i=0; i<indices.size(); i+=4) {
        uint32_t index; std::memcpy(&index,indices.data()+i,4);
        if (index >= vertices.size()/24) throw std::runtime_error("Native marker index out of range");
    }
    for (size_t i=0; i<vertices.size(); i+=4) {
        float value; std::memcpy(&value,vertices.data()+i,4);
        if (!std::isfinite(value)) throw std::runtime_error("Native marker non-finite vertex");
    }
    draws.open(output / "native-model-draws.jsonl");
    RT64::SetNativeMeshHooks(classify, render);
}

void metal_init(plume::RenderDevice *value) {
    if (reference.empty()) return;
    device = static_cast<plume::MetalDevice *>(value)->mtl;
    vertexBuffer = device->newBuffer(vertices.data(),vertices.size(),MTL::ResourceStorageModeShared);
    indexBuffer = device->newBuffer(indices.data(),indices.size(),MTL::ResourceStorageModeShared);
    if (!vertexBuffer || !indexBuffer) throw std::runtime_error("Native marker GPU allocation failed");
    for (size_t i=0;i<depthStates.size();++i) {
        auto *desc = MTL::DepthStencilDescriptor::alloc()->init();
        desc->setDepthCompareFunction((i&1) ? MTL::CompareFunctionLessEqual : MTL::CompareFunctionAlways);
        desc->setDepthWriteEnabled(i&2);
        depthStates[i] = device->newDepthStencilState(desc); desc->release();
    }
}

void shutdown() {
    if (reference.empty()) return;
    std::ofstream(output / "native-model-summary.json") << json({{"schema","srw64.native-marker-run.v1"},
        {"classified_triangles",classified.load()},{"native_draws",rendered},{"suppressed_triangles",suppressed},
        {"original_model_classifications",original_models.load()},
        {"vertices",vertices.size()/24},{"triangles",indices.size()/12},{"rdram_modified",false}}).dump(2) << '\n';
    draws.close();
    if (pipeline) pipeline->release(); pipeline=nullptr;
    if (vertexBuffer) vertexBuffer->release(); vertexBuffer=nullptr;
    if (indexBuffer) indexBuffer->release(); indexBuffer=nullptr;
    for(auto*& state:depthStates) { if(state)state->release();state=nullptr; }
    device=nullptr;
}
}
