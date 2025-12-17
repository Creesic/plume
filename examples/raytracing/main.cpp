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
#include "shaders/rtShadersMetal.metallib.h"
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

    // Ray tracing resources (TODO: implement)
    // std::unique_ptr<RenderAccelerationStructure> blas;
    // std::unique_ptr<RenderAccelerationStructure> tlas;
    // std::unique_ptr<RenderPipeline> rtPipeline;
    // std::unique_ptr<RenderTexture> outputTexture;
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

void initializeRenderResources(RTContext& ctx, RenderInterface* renderInterface) {
    ctx.device = renderInterface->createDevice();
    ctx.commandQueue = ctx.device->createCommandQueue(RenderCommandListType::DIRECT);
    ctx.commandFence = ctx.device->createCommandFence();
    ctx.swapChain = ctx.commandQueue->createSwapChain(ctx.renderWindow, BufferCount, SwapchainFormat, 2);
    ctx.swapChain->resize();
    ctx.commandList = ctx.commandQueue->createCommandList();
    ctx.acquireSemaphore = ctx.device->createCommandSemaphore();

    createFramebuffers(ctx);

    // Check ray tracing support (from device capabilities, not interface)
    const auto& caps = ctx.device->getCapabilities();
    if (!caps.raytracing) {
        std::cerr << "WARNING: Ray tracing is not supported on this device" << std::endl;
        std::cerr << "The example will show a placeholder color instead" << std::endl;
    } else {
        std::cout << "Ray tracing supported!" << std::endl;
        // TODO: Initialize ray tracing resources
        // - Create BLAS with triangle geometry
        // - Create TLAS with single instance
        // - Create RT pipeline
        // - Create output texture
    }
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
    }
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
    ctx.commandList->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(swapChainTexture, RenderTextureLayout::COLOR_WRITE));

    const RenderFramebuffer* framebuffer = ctx.framebuffers[imageIndex].get();
    ctx.commandList->setFramebuffer(framebuffer);

    const uint32_t width = ctx.swapChain->getWidth();
    const uint32_t height = ctx.swapChain->getHeight();
    const RenderViewport viewport(0.0f, 0.0f, float(width), float(height));
    const RenderRect scissor(0, 0, width, height);

    ctx.commandList->setViewports(viewport);
    ctx.commandList->setScissors(scissor);

    // TODO: Once RT is implemented:
    // 1. Bind RT pipeline
    // 2. Bind TLAS
    // 3. Dispatch rays to output texture
    // 4. Copy output texture to swapchain

    // For now, clear to a purple-ish color to indicate RT example
    RenderColor clearColor(0.4f, 0.2f, 0.6f, 1.0f);
    ctx.commandList->clearColor(0, clearColor);

    ctx.commandList->barriers(RenderBarrierStage::NONE, RenderTextureBarrier(swapChainTexture, RenderTextureLayout::PRESENT));
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
