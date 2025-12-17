// Ray tracing "Hello Triangle" example
// Demonstrates basic ray tracing with Plume RHI

#include "plume_render_interface.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <cmath>

// Shader blobs
#ifdef _WIN64
#include "shaders/rtShaders.hlsl.dxil.h"
#endif
#ifdef __APPLE__
#include "shaders/rtShaders.metallib.h"
#endif

namespace plume {
    // Forward declarations for interface creation
    extern std::unique_ptr<RenderInterface> CreateMetalInterface();
    extern std::unique_ptr<RenderInterface> CreateD3D12Interface();
    extern std::unique_ptr<RenderInterface> CreateVulkanInterface();
}

using namespace plume;

static const uint32_t BufferCount = 2;
static const RenderFormat SwapchainFormat = RenderFormat::B8G8R8A8_UNORM;

// Camera constants matching shader
struct CameraConstants {
    float viewInverse[16];
    float projInverse[16];
    uint32_t width;
    uint32_t height;
    uint32_t frameIndex;
    uint32_t padding;
};

// Triangle vertex data (simple triangle in front of camera)
struct Vertex {
    float position[3];
};

// Simple identity matrix helper
void setIdentity(float* m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// Simple perspective inverse matrix for camera
void setPerspectiveInverse(float* m, float fovY, float aspect, float nearZ, float farZ) {
    float tanHalfFov = tanf(fovY * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = tanHalfFov * aspect;
    m[5] = tanHalfFov;
    m[11] = 1.0f;
    m[14] = (nearZ - farZ) / (2.0f * farZ * nearZ);
    m[15] = (nearZ + farZ) / (2.0f * farZ * nearZ);
}

// Simple look-at inverse (view inverse) matrix
void setViewInverse(float* m, float eyeX, float eyeY, float eyeZ) {
    setIdentity(m);
    m[12] = eyeX;
    m[13] = eyeY;
    m[14] = eyeZ;
}

struct RTContext {
    const RenderInterface* renderInterface = nullptr;
    std::string apiName;
    RenderWindow renderWindow = {};
    std::unique_ptr<RenderDevice> device;
    std::unique_ptr<RenderCommandQueue> commandQueue;
    std::unique_ptr<RenderCommandList> commandList;
    std::unique_ptr<RenderSwapChain> swapChain;
    std::unique_ptr<RenderCommandSemaphore> acquireSemaphore;
    std::vector<std::unique_ptr<RenderCommandSemaphore>> releaseSemaphores;
    std::unique_ptr<RenderCommandFence> commandFence;
    std::vector<std::unique_ptr<RenderFramebuffer>> framebuffers;

    // Ray tracing resources
    bool rtSupported = false;
    std::unique_ptr<RenderBuffer> vertexBuffer;
    std::unique_ptr<RenderBuffer> indexBuffer;
    std::unique_ptr<RenderBuffer> blasScratchBuffer;
    std::unique_ptr<RenderBuffer> tlasScratchBuffer;
    std::unique_ptr<RenderBuffer> instanceBuffer;
    std::unique_ptr<RenderBuffer> sbtBuffer;
    std::unique_ptr<RenderAccelerationStructure> blas;
    std::unique_ptr<RenderAccelerationStructure> tlas;
    std::unique_ptr<RenderShader> rtShader;
    std::unique_ptr<RenderPipeline> rtPipeline;
    std::unique_ptr<RenderPipelineLayout> rtPipelineLayout;
    std::unique_ptr<RenderTexture> outputTexture;
    std::unique_ptr<RenderTextureView> outputTextureView;
    std::unique_ptr<RenderDescriptorSet> descriptorSet;
    std::unique_ptr<RenderBuffer> constantBuffer;

    RenderShaderBindingTableInfo sbtInfo;
    uint32_t frameIndex = 0;
};

void createFramebuffers(RTContext& ctx) {
    ctx.framebuffers.clear();
    for (uint32_t i = 0; i < ctx.swapChain->getTextureCount(); i++) {
        const RenderTexture* colorAttachment = ctx.swapChain->getTexture(i);
        RenderFramebufferDesc fbDesc;
        fbDesc.colorAttachments = &colorAttachment;
        fbDesc.colorAttachmentsCount = 1;
        fbDesc.depthAttachment = nullptr;
        auto framebuffer = ctx.device->createFramebuffer(fbDesc);
        ctx.framebuffers.push_back(std::move(framebuffer));
    }
}

void createOutputTexture(RTContext& ctx, uint32_t width, uint32_t height) {
    RenderTextureDesc texDesc = RenderTextureDesc::Texture2D(
        width, height, 1, RenderFormat::R8G8B8A8_UNORM,
        RenderTextureFlag::STORAGE | RenderTextureFlag::UNORDERED_ACCESS
    );

    ctx.outputTexture = ctx.device->createTexture(texDesc);
    ctx.outputTexture->setName("RT Output Texture");

    RenderTextureViewDesc viewDesc;
    viewDesc.format = RenderFormat::R8G8B8A8_UNORM;
    viewDesc.dimension = RenderTextureViewDimension::TEXTURE_2D;
    viewDesc.mipLevels = 1;
    viewDesc.mipSlice = 0;
    ctx.outputTextureView = ctx.outputTexture->createTextureView(viewDesc);
}

void initializeRayTracing(RTContext& ctx) {
    const auto& caps = ctx.device->getCapabilities();
    ctx.rtSupported = caps.raytracing;

    if (!ctx.rtSupported) {
        std::cerr << "WARNING: Ray tracing is not supported on this device" << std::endl;
        std::cerr << "The example will show a placeholder color instead" << std::endl;
        return;
    }

    std::cout << "Ray tracing supported! Initializing RT resources..." << std::endl;

    // 1. Create geometry buffers - simple triangle
    Vertex vertices[] = {
        {{ 0.0f,  0.5f, 2.0f}},  // Top
        {{-0.5f, -0.5f, 2.0f}},  // Bottom left
        {{ 0.5f, -0.5f, 2.0f}}   // Bottom right
    };

    uint32_t indices[] = {0, 1, 2};

    // Create vertex buffer
    RenderBufferDesc vbDesc = RenderBufferDesc::VertexBuffer(sizeof(vertices), RenderHeapType::UPLOAD);
    vbDesc.flags |= RenderBufferFlag::ACCELERATION_STRUCTURE_INPUT;
    ctx.vertexBuffer = ctx.device->createBuffer(vbDesc);
    ctx.vertexBuffer->setName("Triangle Vertices");

    void* vbData = ctx.vertexBuffer->map();
    memcpy(vbData, vertices, sizeof(vertices));
    ctx.vertexBuffer->unmap();

    // Create index buffer
    RenderBufferDesc ibDesc = RenderBufferDesc::IndexBuffer(sizeof(indices), RenderHeapType::UPLOAD);
    ibDesc.flags |= RenderBufferFlag::ACCELERATION_STRUCTURE_INPUT;
    ctx.indexBuffer = ctx.device->createBuffer(ibDesc);
    ctx.indexBuffer->setName("Triangle Indices");

    void* ibData = ctx.indexBuffer->map();
    memcpy(ibData, indices, sizeof(indices));
    ctx.indexBuffer->unmap();

    // 2. Create BLAS
    RenderBottomLevelASMesh mesh;
    mesh.vertexBuffer = ctx.vertexBuffer->at(0);
    mesh.vertexFormat = RenderFormat::R32G32B32_FLOAT;
    mesh.vertexStride = sizeof(Vertex);
    mesh.vertexCount = 3;
    mesh.indexBuffer = ctx.indexBuffer->at(0);
    mesh.indexFormat = RenderFormat::R32_UINT;
    mesh.indexCount = 3;
    mesh.isOpaque = true;

    RenderBottomLevelASBuildInfo blasBuildInfo;
    ctx.device->setBottomLevelASBuildInfo(blasBuildInfo, &mesh, 1, false, true);

    RenderAccelerationStructureDesc blasDesc;
    blasDesc.type = RenderAccelerationStructureType::BOTTOM_LEVEL;
    blasDesc.size = blasBuildInfo.accelerationStructureSize;
    ctx.blas = ctx.device->createAccelerationStructure(blasDesc);

    // Create scratch buffer for BLAS build
    RenderBufferDesc scratchDesc = RenderBufferDesc::DefaultBuffer(blasBuildInfo.scratchSize);
    scratchDesc.flags = RenderBufferFlag::STORAGE | RenderBufferFlag::ACCELERATION_STRUCTURE_SCRATCH;
    ctx.blasScratchBuffer = ctx.device->createBuffer(scratchDesc);
    ctx.blasScratchBuffer->setName("BLAS Scratch");

    // Build BLAS
    ctx.commandList->begin();
    ctx.commandList->buildBottomLevelAS(ctx.blas.get(), ctx.blasScratchBuffer->at(0), blasBuildInfo);
    ctx.commandList->end();

    const RenderCommandList* cmdList = ctx.commandList.get();
    ctx.commandQueue->executeCommandLists(&cmdList, 1, nullptr, 0, nullptr, 0, ctx.commandFence.get());
    ctx.commandQueue->waitForCommandFence(ctx.commandFence.get());

    // 3. Create TLAS with single instance
    // Identity transform (row-major 3x4)
    RenderAffineTransform transform;
    transform.m[0][0] = 1.0f; transform.m[0][1] = 0.0f; transform.m[0][2] = 0.0f; transform.m[0][3] = 0.0f;
    transform.m[1][0] = 0.0f; transform.m[1][1] = 1.0f; transform.m[1][2] = 0.0f; transform.m[1][3] = 0.0f;
    transform.m[2][0] = 0.0f; transform.m[2][1] = 0.0f; transform.m[2][2] = 1.0f; transform.m[2][3] = 0.0f;

    RenderTopLevelASInstance instance;
    // For the BLAS reference, we use a RenderBufferReference with the AS pointer cast to RenderBuffer*.
    // This is how the cross-platform API bridges Metal's separate AS objects.
    instance.bottomLevelAS = RenderBufferReference(reinterpret_cast<const RenderBuffer*>(ctx.blas.get()), 0);
    instance.transform = transform;
    instance.instanceID = 0;
    instance.instanceMask = 0xFF;
    instance.instanceContributionToHitGroupIndex = 0;
    instance.cullDisable = false;

    RenderTopLevelASBuildInfo tlasBuildInfo;
    ctx.device->setTopLevelASBuildInfo(tlasBuildInfo, &instance, 1, false, true);

    RenderAccelerationStructureDesc tlasDesc;
    tlasDesc.type = RenderAccelerationStructureType::TOP_LEVEL;
    tlasDesc.size = tlasBuildInfo.accelerationStructureSize;
    ctx.tlas = ctx.device->createAccelerationStructure(tlasDesc);

    // Create instance buffer
    RenderBufferDesc instanceBufDesc = RenderBufferDesc::UploadBuffer(tlasBuildInfo.instancesBufferData.size());
    instanceBufDesc.flags |= RenderBufferFlag::ACCELERATION_STRUCTURE_INPUT;
    ctx.instanceBuffer = ctx.device->createBuffer(instanceBufDesc);
    ctx.instanceBuffer->setName("Instance Buffer");

    // Copy instance data
    void* instanceData = ctx.instanceBuffer->map();
    memcpy(instanceData, tlasBuildInfo.instancesBufferData.data(), tlasBuildInfo.instancesBufferData.size());
    ctx.instanceBuffer->unmap();

    // Create scratch buffer for TLAS build
    RenderBufferDesc tlasScratchDesc = RenderBufferDesc::DefaultBuffer(tlasBuildInfo.scratchSize);
    tlasScratchDesc.flags = RenderBufferFlag::STORAGE | RenderBufferFlag::ACCELERATION_STRUCTURE_SCRATCH;
    ctx.tlasScratchBuffer = ctx.device->createBuffer(tlasScratchDesc);
    ctx.tlasScratchBuffer->setName("TLAS Scratch");

    // Build TLAS
    ctx.commandList->begin();
    ctx.commandList->buildTopLevelAS(ctx.tlas.get(), ctx.tlasScratchBuffer->at(0), ctx.instanceBuffer->at(0), tlasBuildInfo);
    ctx.commandList->end();

    ctx.commandQueue->executeCommandLists(&cmdList, 1, nullptr, 0, nullptr, 0, ctx.commandFence.get());
    ctx.commandQueue->waitForCommandFence(ctx.commandFence.get());

    // 4. Create output texture
    createOutputTexture(ctx, ctx.swapChain->getWidth(), ctx.swapChain->getHeight());

    // 5. Create constant buffer
    RenderBufferDesc cbDesc = RenderBufferDesc::UploadBuffer(sizeof(CameraConstants));
    cbDesc.flags |= RenderBufferFlag::CONSTANT;
    ctx.constantBuffer = ctx.device->createBuffer(cbDesc);
    ctx.constantBuffer->setName("Camera Constants");

    // 6. Create RT shader
#ifdef __APPLE__
    ctx.rtShader = ctx.device->createShader(rtShadersBlobMetalLib, rtShadersBlobMetalLib_size, nullptr, RenderShaderFormat::METAL);
#elif defined(_WIN64)
    ctx.rtShader = ctx.device->createShader(rtShaders_blob, rtShaders_size, nullptr, RenderShaderFormat::DXIL);
#endif
    ctx.rtShader->setName("RT Shader Library");

    // 7. Create pipeline layout with descriptors for: output texture (UAV), TLAS (SRV), constants (CBV)
    std::vector<RenderDescriptorRange> ranges;
    ranges.push_back(RenderDescriptorRange(RenderDescriptorRangeType::READ_WRITE_TEXTURE, 0, 1)); // u0: output texture
    ranges.push_back(RenderDescriptorRange(RenderDescriptorRangeType::ACCELERATION_STRUCTURE, 1, 1)); // t0: TLAS
    ranges.push_back(RenderDescriptorRange(RenderDescriptorRangeType::CONSTANT_BUFFER, 2, 1)); // b0: constants

    RenderDescriptorSetDesc descSetDesc(ranges.data(), static_cast<uint32_t>(ranges.size()));
    ctx.descriptorSet = ctx.device->createDescriptorSet(descSetDesc);

    // Bind resources to descriptor set
    ctx.descriptorSet->setTexture(0, ctx.outputTexture.get(), RenderTextureLayout::GENERAL, ctx.outputTextureView.get());
    ctx.descriptorSet->setAccelerationStructure(1, ctx.tlas.get());
    ctx.descriptorSet->setBuffer(2, ctx.constantBuffer.get(), sizeof(CameraConstants));

    // Create pipeline layout
    RenderPipelineLayoutDesc layoutDesc;
    layoutDesc.descriptorSetDescsCount = 1;
    layoutDesc.descriptorSetDescs = &descSetDesc;
    ctx.rtPipelineLayout = ctx.device->createPipelineLayout(layoutDesc);

    // 8. Create RT pipeline
    RenderRaytracingPipelineLibrarySymbol symbols[] = {
        RenderRaytracingPipelineLibrarySymbol("RayGen", RenderRaytracingPipelineLibrarySymbolType::RAYGEN, "RayGen"),
        RenderRaytracingPipelineLibrarySymbol("ClosestHit", RenderRaytracingPipelineLibrarySymbolType::CLOSEST_HIT, "ClosestHit"),
        RenderRaytracingPipelineLibrarySymbol("Miss", RenderRaytracingPipelineLibrarySymbolType::MISS, "Miss")
    };

    RenderRaytracingPipelineLibrary library;
    library.shader = ctx.rtShader.get();
    library.symbols = symbols;
    library.symbolsCount = 3;

    RenderRaytracingPipelineHitGroup hitGroup;
    hitGroup.hitGroupName = "HitGroup";
    hitGroup.closestHitName = "ClosestHit";
    hitGroup.anyHitName = nullptr;
    hitGroup.intersectionName = nullptr;

    RenderRaytracingPipelineDesc rtPipelineDesc;
    rtPipelineDesc.libraries = &library;
    rtPipelineDesc.librariesCount = 1;
    rtPipelineDesc.hitGroups = &hitGroup;
    rtPipelineDesc.hitGroupsCount = 1;
    rtPipelineDesc.pipelineLayout = ctx.rtPipelineLayout.get();
    rtPipelineDesc.maxPayloadSize = sizeof(float) * 4; // RayPayload: float3 color + uint depth
    rtPipelineDesc.maxAttributeSize = sizeof(float) * 2; // Barycentrics
    rtPipelineDesc.maxRecursionDepth = 1;

    ctx.rtPipeline = ctx.device->createRaytracingPipeline(rtPipelineDesc);
    ctx.rtPipeline->setName("RT Pipeline");

    // 9. Build Shader Binding Table
    RenderPipelineProgram raygenProgram = ctx.rtPipeline->getProgram("RayGen");
    RenderPipelineProgram missProgram = ctx.rtPipeline->getProgram("Miss");
    RenderPipelineProgram hitGroupProgram = ctx.rtPipeline->getProgram("HitGroup");

    RenderShaderBindingGroup raygenGroup(&raygenProgram, 1);
    RenderShaderBindingGroup missGroup(&missProgram, 1);
    RenderShaderBindingGroup hitGroupGroup(&hitGroupProgram, 1);

    RenderShaderBindingGroups sbtGroups(raygenGroup, missGroup, hitGroupGroup);
    ctx.device->setShaderBindingTableInfo(ctx.sbtInfo, sbtGroups, ctx.rtPipeline.get(), nullptr, 0);

    // Create SBT buffer and upload data
    RenderBufferDesc sbtBufDesc = RenderBufferDesc::UploadBuffer(ctx.sbtInfo.tableBufferData.size());
    sbtBufDesc.flags |= RenderBufferFlag::SHADER_BINDING_TABLE;
    ctx.sbtBuffer = ctx.device->createBuffer(sbtBufDesc);
    ctx.sbtBuffer->setName("Shader Binding Table");

    void* sbtData = ctx.sbtBuffer->map();
    memcpy(sbtData, ctx.sbtInfo.tableBufferData.data(), ctx.sbtInfo.tableBufferData.size());
    ctx.sbtBuffer->unmap();

    std::cout << "Ray tracing initialized successfully!" << std::endl;
}

void initializeRenderResources(RTContext& ctx, RenderInterface* renderInterface) {
    ctx.device = renderInterface->createDevice();
    ctx.commandQueue = ctx.device->createCommandQueue(RenderCommandListType::DIRECT);
    ctx.commandFence = ctx.device->createCommandFence();
    ctx.swapChain = ctx.commandQueue->createSwapChain(ctx.renderWindow, BufferCount, SwapchainFormat, 2);
    ctx.swapChain->resize();
    ctx.commandList = ctx.commandQueue->createCommandList();
    ctx.acquireSemaphore = ctx.device->createCommandSemaphore();

    createFramebuffers(ctx);
    initializeRayTracing(ctx);
}

void createContext(RTContext& ctx, RenderInterface* renderInterface, RenderWindow window, const std::string& apiName) {
    ctx.renderInterface = renderInterface;
    ctx.renderWindow = window;
    ctx.apiName = apiName;
    initializeRenderResources(ctx, const_cast<RenderInterface*>(renderInterface));
}

void resize(RTContext& ctx, int width, int height) {
    std::cout << "Resizing to " << width << "x" << height << std::endl;
    if (ctx.swapChain) {
        ctx.framebuffers.clear();
        bool resized = ctx.swapChain->resize();
        if (!resized) {
            std::cerr << "Failed to resize swap chain" << std::endl;
            return;
        }
        createFramebuffers(ctx);

        // Recreate output texture at new size
        if (ctx.rtSupported) {
            createOutputTexture(ctx, ctx.swapChain->getWidth(), ctx.swapChain->getHeight());
            ctx.descriptorSet->setTexture(0, ctx.outputTexture.get(), RenderTextureLayout::GENERAL, ctx.outputTextureView.get());
        }
    }
}

void updateCameraConstants(RTContext& ctx) {
    const uint32_t width = ctx.swapChain->getWidth();
    const uint32_t height = ctx.swapChain->getHeight();

    CameraConstants constants;

    // Simple camera at origin looking down +Z
    setViewInverse(constants.viewInverse, 0.0f, 0.0f, 0.0f);

    // Perspective with 60 degree FOV
    float aspect = static_cast<float>(width) / static_cast<float>(height);
    setPerspectiveInverse(constants.projInverse, 3.14159f / 3.0f, aspect, 0.1f, 1000.0f);

    constants.width = width;
    constants.height = height;
    constants.frameIndex = ctx.frameIndex++;
    constants.padding = 0;

    void* cbData = ctx.constantBuffer->map();
    memcpy(cbData, &constants, sizeof(constants));
    ctx.constantBuffer->unmap();
}

void render(RTContext& ctx) {
    static int counter = 0;
    if (counter++ % 60 == 0) {
        std::cout << "Rendering frame " << counter << " using " << ctx.apiName << " backend" << std::endl;
    }

    // Acquire the next swapchain image
    uint32_t imageIndex = 0;
    ctx.swapChain->acquireTexture(ctx.acquireSemaphore.get(), &imageIndex);

    ctx.commandList->begin();

    RenderTexture* swapChainTexture = ctx.swapChain->getTexture(imageIndex);

    if (ctx.rtSupported) {
        // Update camera constants
        updateCameraConstants(ctx);

        // Transition output texture to general for compute write
        ctx.commandList->barriers(RenderBarrierStage::COMPUTE,
            RenderTextureBarrier(ctx.outputTexture.get(), RenderTextureLayout::GENERAL));

        // Set up raytracing state
        ctx.commandList->setRaytracingPipelineLayout(ctx.rtPipelineLayout.get());
        ctx.commandList->setPipeline(ctx.rtPipeline.get());
        ctx.commandList->setRaytracingDescriptorSet(ctx.descriptorSet.get(), 0);

        // Trace rays
        const uint32_t width = ctx.swapChain->getWidth();
        const uint32_t height = ctx.swapChain->getHeight();
        ctx.commandList->traceRays(width, height, 1, ctx.sbtBuffer->at(0), ctx.sbtInfo.groups);

        // Transition textures for copy
        ctx.commandList->barriers(RenderBarrierStage::COPY,
            RenderTextureBarrier(ctx.outputTexture.get(), RenderTextureLayout::COPY_SOURCE));
        ctx.commandList->barriers(RenderBarrierStage::COPY,
            RenderTextureBarrier(swapChainTexture, RenderTextureLayout::COPY_DEST));

        // Copy output to swapchain
        ctx.commandList->copyTexture(swapChainTexture, ctx.outputTexture.get());

        // Transition swapchain for present
        ctx.commandList->barriers(RenderBarrierStage::NONE,
            RenderTextureBarrier(swapChainTexture, RenderTextureLayout::PRESENT));
    } else {
        // Fallback: just clear to purple
        ctx.commandList->barriers(RenderBarrierStage::GRAPHICS,
            RenderTextureBarrier(swapChainTexture, RenderTextureLayout::COLOR_WRITE));

        const RenderFramebuffer* framebuffer = ctx.framebuffers[imageIndex].get();
        ctx.commandList->setFramebuffer(framebuffer);

        const uint32_t width = ctx.swapChain->getWidth();
        const uint32_t height = ctx.swapChain->getHeight();
        const RenderViewport viewport(0.0f, 0.0f, float(width), float(height));
        const RenderRect scissor(0, 0, width, height);

        ctx.commandList->setViewports(viewport);
        ctx.commandList->setScissors(scissor);

        RenderColor clearColor(0.4f, 0.2f, 0.6f, 1.0f);
        ctx.commandList->clearColor(0, clearColor);

        ctx.commandList->barriers(RenderBarrierStage::NONE,
            RenderTextureBarrier(swapChainTexture, RenderTextureLayout::PRESENT));
    }

    ctx.commandList->end();

    // Create semaphores if needed
    while (ctx.releaseSemaphores.size() < ctx.swapChain->getTextureCount()) {
        ctx.releaseSemaphores.emplace_back(ctx.device->createCommandSemaphore());
    }

    const RenderCommandList* cmdList = ctx.commandList.get();
    RenderCommandSemaphore* waitSemaphore = ctx.acquireSemaphore.get();
    RenderCommandSemaphore* signalSemaphore = ctx.releaseSemaphores[imageIndex].get();

    ctx.commandQueue->executeCommandLists(&cmdList, 1, &waitSemaphore, 1, &signalSemaphore, 1, ctx.commandFence.get());
    ctx.swapChain->present(imageIndex, &signalSemaphore, 1);
    ctx.commandQueue->waitForCommandFence(ctx.commandFence.get());
}

std::unique_ptr<RenderInterface> CreateRenderInterface(std::string& apiName) {
#if defined(__APPLE__)
    apiName = "Metal";
    return CreateMetalInterface();
#elif defined(_WIN32)
    apiName = "D3D12";
    return CreateD3D12Interface();
#else
    apiName = "Vulkan";
    return CreateVulkanInterface();
#endif
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    uint32_t flags = SDL_WINDOW_RESIZABLE;
#if defined(__APPLE__)
    flags |= SDL_WINDOW_METAL;
#endif

    SDL_Window* window = SDL_CreateWindow(
        "Plume Ray Tracing - Hello Triangle",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, flags
    );

    if (!window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);

    std::string apiName;
    auto renderInterface = CreateRenderInterface(apiName);
    if (!renderInterface) {
        std::cerr << "Failed to create render interface" << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    RTContext ctx;
#if defined(__linux__)
    createContext(ctx, renderInterface.get(), { wmInfo.info.x11.display, wmInfo.info.x11.window }, apiName);
#elif defined(__APPLE__)
    SDL_MetalView view = SDL_Metal_CreateView(window);
    createContext(ctx, renderInterface.get(), { wmInfo.info.cocoa.window, SDL_Metal_GetLayer(view) }, apiName);
#elif defined(_WIN32)
    createContext(ctx, renderInterface.get(), { wmInfo.info.win.window }, apiName);
#endif

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE)
                        running = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        resize(ctx, event.window.data1, event.window.data2);
                    }
                    break;
            }
        }
        render(ctx);
    }

    // Cleanup: transition swapchain out of present state
    uint32_t imageIndex = 0;
    if (!ctx.swapChain->isEmpty() && ctx.swapChain->acquireTexture(ctx.acquireSemaphore.get(), &imageIndex)) {
        RenderTexture* swapChainTexture = ctx.swapChain->getTexture(imageIndex);
        ctx.commandList->begin();
        ctx.commandList->barriers(RenderBarrierStage::NONE, RenderTextureBarrier(swapChainTexture, RenderTextureLayout::COLOR_WRITE));
        ctx.commandList->end();
        const RenderCommandList* cmdList = ctx.commandList.get();
        RenderCommandSemaphore* waitSemaphore = ctx.acquireSemaphore.get();
        ctx.commandQueue->executeCommandLists(&cmdList, 1, &waitSemaphore, 1, nullptr, 0, ctx.commandFence.get());
        ctx.commandQueue->waitForCommandFence(ctx.commandFence.get());
    }

#if defined(__APPLE__)
    SDL_Metal_DestroyView(view);
#endif
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
