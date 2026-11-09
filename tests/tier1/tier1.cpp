#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
#include <new>
#include <plume_d3d12.h>
#include <thread>
#include <wrl/client.h>
using namespace plume;
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr)
{
    if (FAILED(hr))
    {
        std::printf("HRESULT %lx\n", hr);
        std::abort();
    }
}
static ComPtr<ID3DBlob> compile(const char *source, const char *target)
{
    ComPtr<ID3DBlob> blob, errors;
    HRESULT hr =
        D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &blob, &errors);
    if (errors)
        std::printf("%s\n", (char *)errors->GetBufferPointer());
    check(hr);
    return blob;
}
static unsigned serializedLayouts = 0;
static HRESULT inspectRootSignature(const D3D12_ROOT_SIGNATURE_DESC *desc, D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob,
                                    ID3DBlob **errors)
{
    // Inspect the exact root signature Plume serializes, including stage visibility.
    unsigned samplers[2] = {}, views[2] = {}, cost = 0;
    for (UINT i = 0; i < desc->NumParameters; ++i)
    {
        const auto &p = desc->pParameters[i];
        cost += p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS    ? p.Constants.Num32BitValues
                : p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ? 1
                                                                                : 2;
        if (p.ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            continue;
        for (UINT j = 0; j < p.DescriptorTable.NumDescriptorRanges; ++j)
        {
            const auto &r = p.DescriptorTable.pDescriptorRanges[j];
            assert(r.NumDescriptors != UINT_MAX);
            for (unsigned stage = 0; stage < 2; ++stage)
            {
                auto visibility = stage ? D3D12_SHADER_VISIBILITY_PIXEL : D3D12_SHADER_VISIBILITY_VERTEX;
                if (p.ShaderVisibility != D3D12_SHADER_VISIBILITY_ALL && p.ShaderVisibility != visibility)
                    continue;
                (r.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER ? samplers[stage] : views[stage]) += r.NumDescriptors;
            }
        }
    }
    assert(cost <= 64 && samplers[0] <= 16 && samplers[1] <= 16 && views[0] <= 128 && views[1] <= 128);
    ++serializedLayouts;
    HRESULT hr = D3D12SerializeRootSignature(desc, version, blob, errors);
    if (SUCCEEDED(hr))
    {
        ComPtr<ID3D12RootSignatureDeserializer> decoded;
        check(D3D12CreateRootSignatureDeserializer((*blob)->GetBufferPointer(), (*blob)->GetBufferSize(), IID_PPV_ARGS(&decoded)));
        assert(decoded->GetRootSignatureDesc()->NumParameters == desc->NumParameters);
    }
    return hr;
}
// Compile the production implementation here so its exact serialized signatures can be inspected.
#define D3D12SerializeRootSignature inspectRootSignature
#include "../../plume_d3d12.cpp"
#undef D3D12SerializeRootSignature

int main()
{
    D3D12Interface renderer;
    auto device = renderer.createDevice("");
    assert(device);
    auto *native = static_cast<D3D12Device *>(device.get());
    auto queueBase = device->createCommandQueue(RenderCommandListType::DIRECT);
    auto *queue = static_cast<D3D12CommandQueue *>(queueBase.get());
    auto commands = queue->createCommandList();
    auto secondCommands = queue->createCommandList();
    auto *cmd = static_cast<D3D12CommandList *>(commands.get());
    auto resource = [&](D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_DESC desc, D3D12_RESOURCE_STATES state) {
        ComPtr<ID3D12Resource> result;
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = heapType;
        check(native->d3d->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&result)));
        return result;
    };
    auto bufferDesc = [](UINT64 size) {
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = size;
        d.Height = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return d;
    };
    D3D12_RESOURCE_DESC td = {};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = td.Height = td.DepthOrArraySize = td.MipLevels = td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    auto a = resource(D3D12_HEAP_TYPE_DEFAULT, td, D3D12_RESOURCE_STATE_COPY_DEST);
    auto b = resource(D3D12_HEAP_TYPE_DEFAULT, td, D3D12_RESOURCE_STATE_COPY_DEST);
    auto upload = resource(D3D12_HEAP_TYPE_UPLOAD, bufferDesc(1024), D3D12_RESOURCE_STATE_GENERIC_READ);
    void *mapped;
    check(upload->Map(0, nullptr, &mapped));
    std::memset(mapped, 64, 4);
    std::memset((char *)mapped + 512, 192, 4);
    upload->Unmap(0, nullptr);
    auto cb = resource(D3D12_HEAP_TYPE_UPLOAD, bufferDesc(256), D3D12_RESOURCE_STATE_GENERIC_READ);
    check(cb->Map(0, nullptr, &mapped));
    *(float *)mapped = .5f;
    cb->Unmap(0, nullptr);
    td.Width = 400;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    auto output = resource(D3D12_HEAP_TYPE_DEFAULT, td, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto readback0 = resource(D3D12_HEAP_TYPE_READBACK, bufferDesc(1792), D3D12_RESOURCE_STATE_COPY_DEST);
    auto readback1 = resource(D3D12_HEAP_TYPE_READBACK, bufferDesc(1792), D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12DescriptorHeapAllocator rtvHeap(native, 1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    native->d3d->CreateRenderTargetView(output.Get(), nullptr, rtvHeap.getCPUHandleAt(0));
    RenderDescriptorRange textures(RenderDescriptorRangeType::TEXTURE, 0, 20);
    RenderDescriptorRange psSamplers(RenderDescriptorRangeType::SAMPLER, 0, 16), vsSamplers(RenderDescriptorRangeType::SAMPLER, 0, 4);
    RenderDescriptorSetDesc sets[6] = {{&textures, 1, false, 0, true},
                                       {&textures, 1, false, 0, true},
                                       {&textures, 1, false, 0, true},
                                       {&psSamplers, 1, false, 0, true, RenderShaderVisibility::PIXEL},
                                       {},
                                       {&vsSamplers, 1, false, 0, true, RenderShaderVisibility::VERTEX}};
    RenderPushConstantRange push(0, 4, 0, 4, RenderShaderStageFlag::PIXEL);
    RenderRootDescriptorDesc root;
    root.type = RenderRootDescriptorType::CONSTANT_BUFFER;
    root.shaderRegister = 1;
    root.registerSpace = 4;
    RenderPipelineLayoutDesc ld(&push, 1, sets, 6);
    ld.rootDescriptorDescs = &root;
    ld.rootDescriptorDescsCount = 1;
    auto layout = device->createPipelineLayout(ld);
    assert(layout);
    auto *nativeLayout = static_cast<D3D12PipelineLayout *>(layout.get());
    auto table = device->createDescriptorSet(sets[0]);
    auto psTable = device->createDescriptorSet(sets[3]);
    auto vsTable = device->createDescriptorSet(sets[5]);
    auto *t = static_cast<D3D12DescriptorSet *>(table.get());
    auto *ps = static_cast<D3D12DescriptorSet *>(psTable.get());
    auto *vs = static_cast<D3D12DescriptorSet *>(vsTable.get());
    RenderDescriptorRange storeRange(RenderDescriptorRangeType::TEXTURE, 0, 65536);
    auto store = device->createDescriptorSet(RenderDescriptorSetDesc(&storeRange, 1, false, 0, true));
    auto *s = static_cast<D3D12DescriptorSet *>(store.get());
    assert(!(s->viewHeap->heap->GetDesc().Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    s->setSRV(0, a.Get(), &srv);
    s->setSRV(1, b.Get(), &srv);
    for (unsigned i = 0; i < 20; ++i)
        t->copyDescriptors(i, s, 0, 1);
    auto outOfRange = [&](uint32_t destination, uint32_t source) {
        bool caught = false;
        try
        {
            t->copyDescriptors(destination, s, source, 1);
        }
        catch (const std::out_of_range &)
        {
            caught = true;
        }
        assert(caught);
    };
    outOfRange(20, 0);
    outOfRange(0, 65536);
    bool caught = false;
    try
    {
        t->copyDescriptors(0, ps, 0, 1);
    }
    catch (const std::invalid_argument &)
    {
        caught = true;
    }
    assert(caught);
    RenderSamplerDesc sd;
    auto sampler = device->createSampler(sd);
    for (unsigned i = 0; i < 16; ++i)
        ps->setSampler(i, sampler.get());
    for (unsigned i = 0; i < 4; ++i)
        vs->setSampler(i, sampler.get());
    {
        RenderDescriptorRange oneTexture(RenderDescriptorRangeType::TEXTURE, 0, 1);
        RenderDescriptorSetDesc desc(&oneTexture, 1, false, 0, true);
        alignas(D3D12DescriptorSet) unsigned char storage[sizeof(D3D12DescriptorSet)];
        auto *first = new (storage) D3D12DescriptorSet(native, desc);
        first->setSRV(0, a.Get(), &srv);
        const uint64_t firstVersion = first->contentVersion.load(std::memory_order_relaxed);
        t->copyDescriptors(0, first, 0, 1);
        const uint64_t firstTableVersion = t->contentVersion.load(std::memory_order_relaxed);
        first->~D3D12DescriptorSet();
        auto *second = new (storage) D3D12DescriptorSet(native, desc);
        second->setSRV(0, b.Get(), &srv);
        assert(second->contentVersion.load(std::memory_order_relaxed) != firstVersion);
        t->copyDescriptors(0, second, 0, 1);
        assert(t->contentVersion.load(std::memory_order_relaxed) != firstTableVersion);
        second->~D3D12DescriptorSet();

        RenderDescriptorRange constantBuffer(RenderDescriptorRangeType::CONSTANT_BUFFER, 0, 1);
        D3D12DescriptorSet cbv(native, RenderDescriptorSetDesc(&constantBuffer, 1, false, 0, true));
        auto version = cbv.contentVersion.load(std::memory_order_relaxed);
        cbv.setCBV(0, cb.Get(), 256);
        assert(cbv.contentVersion.load(std::memory_order_relaxed) != version &&
               cbv.descriptorContentVersions[0] == cbv.contentVersion.load(std::memory_order_relaxed));
        RenderDescriptorRange readWrite(RenderDescriptorRangeType::READ_WRITE_FORMATTED_BUFFER, 0, 1);
        D3D12DescriptorSet uav(native, RenderDescriptorSetDesc(&readWrite, 1, false, 0, true));
        version = uav.contentVersion.load(std::memory_order_relaxed);
        D3D12_UNORDERED_ACCESS_VIEW_DESC nullUAV = {};
        nullUAV.Format = DXGI_FORMAT_R32_FLOAT;
        nullUAV.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        nullUAV.Buffer.NumElements = 1;
        uav.setUAV(0, nullptr, &nullUAV);
        assert(uav.contentVersion.load(std::memory_order_relaxed) != version &&
               uav.descriptorContentVersions[0] == uav.contentVersion.load(std::memory_order_relaxed));
    }
    {
        constexpr unsigned slots = 2048, workers = 8;
        RenderDescriptorRange range(RenderDescriptorRangeType::TEXTURE, 0, slots);
        D3D12DescriptorSet concurrent(native, RenderDescriptorSetDesc(&range, 1, false, 0, true));
        for (unsigned round = 0; round < 4; ++round)
        {
            std::atomic<unsigned> ready{0};
            std::atomic<bool> start{false};
            std::thread writers[workers];
            for (unsigned worker = 0; worker < workers; ++worker)
            {
                writers[worker] = std::thread([&, worker] {
                    ready.fetch_add(1, std::memory_order_relaxed);
                    while (!start.load(std::memory_order_acquire))
                        std::this_thread::yield();
                    for (unsigned slot = worker; slot < slots; slot += workers)
                        concurrent.setSRV(slot, a.Get(), &srv);
                });
            }
            while (ready.load(std::memory_order_relaxed) != workers)
                std::this_thread::yield();
            start.store(true, std::memory_order_release);
            for (auto &writer : writers)
                writer.join();
            std::unordered_set<uint64_t> unique(concurrent.descriptorContentVersions.begin(), concurrent.descriptorContentVersions.end());
            assert(unique.size() == slots && !unique.count(0));
            assert(unique.count(concurrent.contentVersion.load(std::memory_order_relaxed)) == 1);
        }
    }
    const char *vsrc =
        R"(Texture2D<float4> tex[20]:register(t0,space2); SamplerState smp[4]:register(s0,space5); struct O{float4 p:SV_Position; nointerpolation float c:COLOR;}; O main(uint i:SV_VertexID){O o;o.p=float4(i==1?3:-1,i==2?3:-1,0,1);o.c=tex[0].SampleLevel(smp[0],float2(.5,.5),0).r;return o;})";
    const char *psrc =
        R"(Texture2D<float4> tex[20]:register(t0,space0); Texture2D<float4> stable[20]:register(t0,space1); SamplerState smp[16]:register(s0,space3); cbuffer P:register(b0,space4){float bias;} cbuffer B:register(b1,space4){float factor;} float4 main(float4 p:SV_Position,nointerpolation float c:COLOR):SV_Target{return float4(tex[0].Sample(smp[0],float2(.5,.5)).r*factor+bias,stable[0].Sample(smp[0],float2(.5,.5)).r,c,1);})";
    auto vb = compile(vsrc, "vs_5_1"), pb = compile(psrc, "ps_5_1");
    auto vertex = device->createShader(vb->GetBufferPointer(), vb->GetBufferSize(), "main", RenderShaderFormat::DXBC);
    auto pixel = device->createShader(pb->GetBufferPointer(), pb->GetBufferSize(), "main", RenderShaderFormat::DXBC);
    RenderGraphicsPipelineDesc pd;
    pd.pipelineLayout = layout.get();
    pd.vertexShader = vertex.get();
    pd.pixelShader = pixel.get();
    pd.renderTargetCount = 1;
    pd.renderTargetBlend[0] = RenderBlendDesc::Copy();
    pd.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
    pd.renderTargetFormat[0] = RenderFormat::R8G8B8A8_UNORM;
    auto pipeline = device->createGraphicsPipeline(pd);
    assert(pipeline);
    // A small host layout exercises the game -> blit/ImGui -> game signature switch.
    RenderDescriptorRange smallTexture(RenderDescriptorRangeType::TEXTURE, 0, 1), smallSampler(RenderDescriptorRangeType::SAMPLER, 0, 1);
    RenderDescriptorSetDesc smallSets[] = {{&smallTexture, 1, false, 0, true},
                                           {&smallSampler, 1, false, 0, true, RenderShaderVisibility::PIXEL}};
    auto smallLayout = device->createPipelineLayout(RenderPipelineLayoutDesc(nullptr, 0, smallSets, 2));
    assert(smallLayout);
    auto smallTextureSet = device->createDescriptorSet(smallSets[0]), smallSamplerSet = device->createDescriptorSet(smallSets[1]);
    static_cast<D3D12DescriptorSet *>(smallTextureSet.get())->copyDescriptors(0, s, 1, 1);
    smallSamplerSet->setSampler(0, sampler.get());
    smallSets[0].cpuOnly = smallSets[1].cpuOnly = false;
    auto legacyTextureSet = device->createDescriptorSet(smallSets[0]), legacySamplerSet = device->createDescriptorSet(smallSets[1]);
    static_cast<D3D12DescriptorSet *>(legacyTextureSet.get())->copyDescriptors(0, s, 1, 1);
    legacySamplerSet->setSampler(0, sampler.get());
    auto smallVSBlob = compile(R"(float4 main(uint i:SV_VertexID):SV_Position{return float4(i==1?3:-1,i==2?3:-1,0,1);})", "vs_5_1");
    auto smallPSBlob = compile(
        R"(Texture2D<float4> tex:register(t0,space0);SamplerState smp:register(s0,space1);float4 main():SV_Target{return tex.Sample(smp,float2(.5,.5));})",
        "ps_5_1");
    auto smallVS = device->createShader(smallVSBlob->GetBufferPointer(), smallVSBlob->GetBufferSize(), "main", RenderShaderFormat::DXBC);
    auto smallPS = device->createShader(smallPSBlob->GetBufferPointer(), smallPSBlob->GetBufferSize(), "main", RenderShaderFormat::DXBC);
    pd.pipelineLayout = smallLayout.get();
    pd.vertexShader = smallVS.get();
    pd.pixelShader = smallPS.get();
    auto smallPipeline = device->createGraphicsPipeline(pd);
    assert(smallPipeline);
    auto fence = device->createCommandFence();
    auto transition = [&](ID3D12Resource *r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER x = {};
        x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        x.Transition.pResource = r;
        x.Transition.StateBefore = before;
        x.Transition.StateAfter = after;
        x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->d3d->ResourceBarrier(1, &x);
    };
    for (unsigned frame = 0; frame < 3; ++frame)
    {
        cmd = static_cast<D3D12CommandList *>((frame == 1 ? secondCommands : commands).get());
        auto readback = frame == 1 ? readback1 : readback0;
        cmd->begin();
        assert(cmd->graphicsSnapshots.empty());
        if (frame == 0)
        {
            for (unsigned i = 0; i < 2; ++i)
            {
                D3D12_TEXTURE_COPY_LOCATION dst = {};
                dst.pResource = i ? b.Get() : a.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION src = {};
                src.pResource = upload.Get();
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint.Offset = i * 512;
                src.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 256};
                cmd->d3d->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                transition(dst.pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
            }
        }
        else
            transition(output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->setGraphicsPipelineLayout(layout.get());
        cmd->setPipeline(pipeline.get());
        const float bias = 32.f / 255;
        cmd->setGraphicsPushConstants(0, &bias);
        cmd->d3d->SetGraphicsRootConstantBufferView(nativeLayout->rootDescriptorRootIndicesAndTypes[0].first, cb->GetGPUVirtualAddress());
        auto rtv = rtvHeap.getCPUHandleAt(0);
        cmd->d3d->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        s->setSRV(0, a.Get(), &srv);
        for (unsigned i = 0; i < 20; ++i)
            t->copyDescriptors(i, s, 0, 1);
        cmd->setGraphicsDescriptorSet(table.get(), 1);
        cmd->setGraphicsDescriptorSet(table.get(), 2);
        // Mutating this source must not change stable tables even when a page rolls over.
        for (unsigned i = 0; i < 20; ++i)
            t->copyDescriptors(i, s, 1, 1);
        for (unsigned draw = 0; draw < 400; ++draw)
        {
            if (draw == 200)
            {
                D3D12_VIEWPORT vp = {200, 0, 1, 1, 0, 1};
                D3D12_RECT sc = {200, 0, 201, 1};
                cmd->d3d->RSSetViewports(1, &vp);
                cmd->d3d->RSSetScissorRects(1, &sc);
                cmd->setGraphicsPipelineLayout(smallLayout.get());
                cmd->setPipeline(smallPipeline.get());
                cmd->setGraphicsDescriptorSet(smallTextureSet.get(), 0);
                cmd->setGraphicsDescriptorSet(smallSamplerSet.get(), 1);
                cmd->drawInstanced(3, 1, 0, 0);
                cmd->setGraphicsDescriptorSet(legacyTextureSet.get(), 0);
                cmd->setGraphicsDescriptorSet(legacySamplerSet.get(), 1);
                cmd->drawInstanced(3, 1, 0, 0);
                cmd->setGraphicsDescriptorSet(smallTextureSet.get(), 0);
                cmd->setGraphicsDescriptorSet(smallSamplerSet.get(), 1);
                cmd->drawInstanced(3, 1, 0, 0);
                cmd->setGraphicsPipelineLayout(layout.get());
                cmd->setPipeline(pipeline.get());
                cmd->setGraphicsPushConstants(0, &bias);
                cmd->d3d->SetGraphicsRootConstantBufferView(nativeLayout->rootDescriptorRootIndicesAndTypes[0].first,
                                                            cb->GetGPUVirtualAddress());
                s->setSRV(0, a.Get(), &srv);
                for (unsigned i = 0; i < 20; ++i)
                    t->copyDescriptors(i, s, 0, 1);
                cmd->setGraphicsDescriptorSet(table.get(), 1);
                cmd->setGraphicsDescriptorSet(table.get(), 2);
                for (unsigned i = 0; i < 20; ++i)
                    t->copyDescriptors(i, s, 1, 1);
            }
            // Replacing a resource at the same global slot must invalidate cached table contents.
            s->setSRV(0, draw % 2 ? b.Get() : a.Get(), &srv);
            t->copyDescriptors(0, s, 0, 1);
            ps->setSampler(0, sampler.get()); // Force sampler snapshot rollover despite otherwise stable sampler bindings.
            cmd->setGraphicsDescriptorSet(table.get(), 0);
            cmd->setGraphicsDescriptorSet(psTable.get(), 3);
            cmd->setGraphicsDescriptorSet(vsTable.get(), 5);
            // An external heap switch invalidates every root table; Plume must restore snapshots.
            if (draw == 175)
            {
                ID3D12DescriptorHeap *heaps[] = {native->viewHeapAllocator->heap, native->samplerHeapAllocator->heap};
                cmd->d3d->SetDescriptorHeaps(2, heaps);
                cmd->notifyDescriptorHeapWasChangedExternally();
                cmd->checkDescriptorHeaps();
            }
            D3D12_VIEWPORT vp = {float(draw), 0, 1, 1, 0, 1};
            D3D12_RECT sc = {LONG(draw), 0, LONG(draw + 1), 1};
            cmd->d3d->RSSetViewports(1, &vp);
            cmd->d3d->RSSetScissorRects(1, &sc);
            cmd->drawInstanced(3, 1, 0, 0);
        }
        assert(cmd->snapshotPages.size() >= 4);
        for (auto &page : cmd->snapshotPages)
            assert(page->samplers->heap->GetDesc().NumDescriptors <= 2048);
        transition(output.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 400, 1, 1, 1792};
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = output.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cmd->d3d->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        cmd->end();
        if (frame == 0)
            continue;
        const RenderCommandList *lists[] = {commands.get(), secondCommands.get()};
        queue->executeCommandLists(lists, frame == 1 ? 2 : 1, nullptr, 0, nullptr, 0, fence.get());
        queue->waitForCommandFence(fence.get());
        for (unsigned result = 0; result < (frame == 1 ? 2u : 1u); ++result)
        {
            auto rb = result ? readback1 : readback0;
            check(rb->Map(0, nullptr, &mapped));
            auto bytes = (unsigned char *)mapped;
            for (unsigned i = 0; i < 400; ++i)
            {
                unsigned expected = i % 2 ? 128 : 64;
                if (std::abs(int(bytes[i * 4]) - int(expected)) > 1 || bytes[i * 4 + 1] != 64 || bytes[i * 4 + 2] != 64)
                {
                    std::printf("frame %u result %u pixel %u: %u,%u,%u expected %u,64,64\n", frame, result, i, bytes[i * 4],
                                bytes[i * 4 + 1], bytes[i * 4 + 2], expected);
                    return 1;
                }
            }
            rb->Unmap(0, nullptr);
        }
    }
    for (unsigned repetition = 0; repetition < 3; ++repetition)
    {
        auto benchCommands = queue->createCommandList();
        auto *bench = static_cast<D3D12CommandList *>(benchCommands.get());
        std::unique_ptr<RenderDescriptorSet> destinations[5];
        unsigned indices[] = {0, 1, 2, 3, 5};
        for (unsigned i = 0; i < 5; ++i)
            destinations[i] = device->createDescriptorSet(sets[indices[i]]);
        bench->begin();
        bench->setGraphicsPipelineLayout(layout.get());
        auto start = std::chrono::steady_clock::now();
        for (unsigned draw = 0; draw < 1000; ++draw)
        {
            for (unsigned dimension = 0; dimension < 3; ++dimension)
            {
                auto *dst = static_cast<D3D12DescriptorSet *>(destinations[dimension].get());
                for (unsigned slot = 0; slot < 20; ++slot)
                    dst->copyDescriptors(slot, s, 0, 1);
            }
            for (unsigned stage = 3; stage < 5; ++stage)
            {
                auto *dst = static_cast<D3D12DescriptorSet *>(destinations[stage].get());
                auto *source = stage == 3 ? ps : vs;
                for (unsigned slot = 0; slot < (stage == 3 ? 16u : 4u); ++slot)
                    dst->copyDescriptors(slot, source, slot, 1);
            }
            for (unsigned i = 0; i < 5; ++i)
                bench->setGraphicsDescriptorSet(destinations[i].get(), indices[i]);
        }
        assert(bench->snapshotPages.size() == 1);
        assert(bench->snapshotPages[0]->viewsUsed == 60);
        assert(bench->snapshotPages[0]->samplersUsed == 20);
        const uint64_t unchangedVersion =
            static_cast<D3D12DescriptorSet *>(destinations[0].get())->contentVersion.load(std::memory_order_relaxed);
        static_cast<D3D12DescriptorSet *>(destinations[0].get())->copyDescriptors(0, s, 0, 1);
        assert(static_cast<D3D12DescriptorSet *>(destinations[0].get())->contentVersion.load(std::memory_order_relaxed) ==
               unchangedVersion);
        auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("Repeated 80-descriptor / 5-table bindings: %.3f ms / 1000 draws, %zu heap pages\n", elapsed,
                    bench->snapshotPages.size());
        bench->end();
    }
    assert(serializedLayouts >= 1);
    std::printf("Tier1 bounded root signatures, 1200 rendered snapshot mutations, two concurrently recorded lists, sampler paging, "
                "host/legacy layout switches, external heap rebind and fence-safe reuse passed on %s\n",
                device->getDescription().name.c_str());
}
