//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

// D3D11 backend for Feature Level 11_0 hardware that has neither Vulkan nor
// D3D12 drivers. Shaders are DXBC (Shader Model 5.0, every register in space 0).
//
// D3D11 has one immediate context and no command lists we can rely on, so a
// RenderCommandList records closures and RenderCommandQueue replays them on the
// immediate context, under the device's context mutex. Nothing reaches the GPU
// before executeCommandLists, which is what D3D12 and Vulkan promise as well.
//
// Buffers keep their bytes in CPU memory, as D3D12 upload and readback heaps do.
// At replay a draw copies the ranges it reads into GPU mirrors (vertex/index,
// structured) or into per-slot dynamic constant buffers, once per execution.
// Copies between textures and buffers become UpdateSubresource and staging
// reads; reads stall the replay until the GPU has finished the source.
//
// D3D11 views cannot swizzle components, so a view with a component mapping
// samples a shadow texture that a small pass refreshes whenever the source
// texture was written after the last refresh.

#include "plume_render_interface.h"
#include "plume_d3d11_shaders.h"

#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace plume {
    namespace {
        // Conversions.

        DXGI_FORMAT toDXGI(RenderFormat format) {
            switch (format) {
            case RenderFormat::R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_TYPELESS;
            case RenderFormat::R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case RenderFormat::R32G32B32A32_UINT: return DXGI_FORMAT_R32G32B32A32_UINT;
            case RenderFormat::R32G32B32A32_SINT: return DXGI_FORMAT_R32G32B32A32_SINT;
            case RenderFormat::R32G32B32_TYPELESS: return DXGI_FORMAT_R32G32B32_TYPELESS;
            case RenderFormat::R32G32B32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
            case RenderFormat::R32G32B32_UINT: return DXGI_FORMAT_R32G32B32_UINT;
            case RenderFormat::R32G32B32_SINT: return DXGI_FORMAT_R32G32B32_SINT;
            case RenderFormat::R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
            case RenderFormat::R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case RenderFormat::R16G16B16A16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
            case RenderFormat::R16G16B16A16_UINT: return DXGI_FORMAT_R16G16B16A16_UINT;
            case RenderFormat::R16G16B16A16_SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
            case RenderFormat::R16G16B16A16_SINT: return DXGI_FORMAT_R16G16B16A16_SINT;
            case RenderFormat::R32G32_TYPELESS: return DXGI_FORMAT_R32G32_TYPELESS;
            case RenderFormat::R32G32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
            case RenderFormat::R32G32_UINT: return DXGI_FORMAT_R32G32_UINT;
            case RenderFormat::R32G32_SINT: return DXGI_FORMAT_R32G32_SINT;
            case RenderFormat::R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
            case RenderFormat::R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case RenderFormat::R8G8B8A8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
            case RenderFormat::R8G8B8A8_SNORM: return DXGI_FORMAT_R8G8B8A8_SNORM;
            case RenderFormat::R8G8B8A8_SINT: return DXGI_FORMAT_R8G8B8A8_SINT;
            case RenderFormat::B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case RenderFormat::R16G16_TYPELESS: return DXGI_FORMAT_R16G16_TYPELESS;
            case RenderFormat::R16G16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
            case RenderFormat::R16G16_UNORM: return DXGI_FORMAT_R16G16_UNORM;
            case RenderFormat::R16G16_UINT: return DXGI_FORMAT_R16G16_UINT;
            case RenderFormat::R16G16_SNORM: return DXGI_FORMAT_R16G16_SNORM;
            case RenderFormat::R16G16_SINT: return DXGI_FORMAT_R16G16_SINT;
            case RenderFormat::R32_TYPELESS: return DXGI_FORMAT_R32_TYPELESS;
            case RenderFormat::D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
            case RenderFormat::D32_FLOAT_S8_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
            case RenderFormat::R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
            case RenderFormat::R32_UINT: return DXGI_FORMAT_R32_UINT;
            case RenderFormat::R32_SINT: return DXGI_FORMAT_R32_SINT;
            case RenderFormat::R8G8_TYPELESS: return DXGI_FORMAT_R8G8_TYPELESS;
            case RenderFormat::R8G8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
            case RenderFormat::R8G8_UINT: return DXGI_FORMAT_R8G8_UINT;
            case RenderFormat::R8G8_SNORM: return DXGI_FORMAT_R8G8_SNORM;
            case RenderFormat::R8G8_SINT: return DXGI_FORMAT_R8G8_SINT;
            case RenderFormat::R16_TYPELESS: return DXGI_FORMAT_R16_TYPELESS;
            case RenderFormat::R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
            case RenderFormat::D16_UNORM: return DXGI_FORMAT_D16_UNORM;
            case RenderFormat::R16_UNORM: return DXGI_FORMAT_R16_UNORM;
            case RenderFormat::R16_UINT: return DXGI_FORMAT_R16_UINT;
            case RenderFormat::R16_SNORM: return DXGI_FORMAT_R16_SNORM;
            case RenderFormat::R16_SINT: return DXGI_FORMAT_R16_SINT;
            case RenderFormat::R8_TYPELESS: return DXGI_FORMAT_R8_TYPELESS;
            case RenderFormat::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
            case RenderFormat::R8_UINT: return DXGI_FORMAT_R8_UINT;
            case RenderFormat::R8_SNORM: return DXGI_FORMAT_R8_SNORM;
            case RenderFormat::R8_SINT: return DXGI_FORMAT_R8_SINT;
            case RenderFormat::BC1_TYPELESS: return DXGI_FORMAT_BC1_TYPELESS;
            case RenderFormat::BC1_UNORM: return DXGI_FORMAT_BC1_UNORM;
            case RenderFormat::BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
            case RenderFormat::BC2_TYPELESS: return DXGI_FORMAT_BC2_TYPELESS;
            case RenderFormat::BC2_UNORM: return DXGI_FORMAT_BC2_UNORM;
            case RenderFormat::BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_UNORM_SRGB;
            case RenderFormat::BC3_TYPELESS: return DXGI_FORMAT_BC3_TYPELESS;
            case RenderFormat::BC3_UNORM: return DXGI_FORMAT_BC3_UNORM;
            case RenderFormat::BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
            case RenderFormat::BC4_TYPELESS: return DXGI_FORMAT_BC4_TYPELESS;
            case RenderFormat::BC4_UNORM: return DXGI_FORMAT_BC4_UNORM;
            case RenderFormat::BC4_SNORM: return DXGI_FORMAT_BC4_SNORM;
            case RenderFormat::BC5_TYPELESS: return DXGI_FORMAT_BC5_TYPELESS;
            case RenderFormat::BC5_UNORM: return DXGI_FORMAT_BC5_UNORM;
            case RenderFormat::BC5_SNORM: return DXGI_FORMAT_BC5_SNORM;
            case RenderFormat::BC6H_TYPELESS: return DXGI_FORMAT_BC6H_TYPELESS;
            case RenderFormat::BC6H_UF16: return DXGI_FORMAT_BC6H_UF16;
            case RenderFormat::BC6H_SF16: return DXGI_FORMAT_BC6H_SF16;
            case RenderFormat::BC7_TYPELESS: return DXGI_FORMAT_BC7_TYPELESS;
            case RenderFormat::BC7_UNORM: return DXGI_FORMAT_BC7_UNORM;
            case RenderFormat::BC7_UNORM_SRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
            default: return DXGI_FORMAT_UNKNOWN;
            }
        }

        // Depth textures are typeless so they can also be sampled.
        DXGI_FORMAT toResourceFormat(RenderFormat format) {
            switch (format) {
            case RenderFormat::D32_FLOAT_S8_UINT: return DXGI_FORMAT_R32G8X24_TYPELESS;
            case RenderFormat::D32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
            case RenderFormat::D16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
            default: return toDXGI(format);
            }
        }

        DXGI_FORMAT toShaderResourceFormat(RenderFormat format, uint32_t planeSlice) {
            switch (format) {
            case RenderFormat::D32_FLOAT_S8_UINT:
                return (planeSlice == 1) ? DXGI_FORMAT_X32_TYPELESS_G8X24_UINT : DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
            case RenderFormat::D32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
            case RenderFormat::D16_UNORM: return DXGI_FORMAT_R16_UNORM;
            default: return toDXGI(format);
            }
        }

        D3D11_BLEND toD3D11(RenderBlend blend) {
            switch (blend) {
            case RenderBlend::ZERO: return D3D11_BLEND_ZERO;
            case RenderBlend::ONE: return D3D11_BLEND_ONE;
            case RenderBlend::SRC_COLOR: return D3D11_BLEND_SRC_COLOR;
            case RenderBlend::INV_SRC_COLOR: return D3D11_BLEND_INV_SRC_COLOR;
            case RenderBlend::SRC_ALPHA: return D3D11_BLEND_SRC_ALPHA;
            case RenderBlend::INV_SRC_ALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
            case RenderBlend::DEST_ALPHA: return D3D11_BLEND_DEST_ALPHA;
            case RenderBlend::INV_DEST_ALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
            case RenderBlend::DEST_COLOR: return D3D11_BLEND_DEST_COLOR;
            case RenderBlend::INV_DEST_COLOR: return D3D11_BLEND_INV_DEST_COLOR;
            case RenderBlend::SRC_ALPHA_SAT: return D3D11_BLEND_SRC_ALPHA_SAT;
            case RenderBlend::BLEND_FACTOR: return D3D11_BLEND_BLEND_FACTOR;
            case RenderBlend::INV_BLEND_FACTOR: return D3D11_BLEND_INV_BLEND_FACTOR;
            case RenderBlend::SRC1_COLOR: return D3D11_BLEND_SRC1_COLOR;
            case RenderBlend::INV_SRC1_COLOR: return D3D11_BLEND_INV_SRC1_COLOR;
            case RenderBlend::SRC1_ALPHA: return D3D11_BLEND_SRC1_ALPHA;
            case RenderBlend::INV_SRC1_ALPHA: return D3D11_BLEND_INV_SRC1_ALPHA;
            default: return D3D11_BLEND_ONE;
            }
        }

        D3D11_BLEND_OP toD3D11(RenderBlendOperation operation) {
            switch (operation) {
            case RenderBlendOperation::SUBTRACT: return D3D11_BLEND_OP_SUBTRACT;
            case RenderBlendOperation::REV_SUBTRACT: return D3D11_BLEND_OP_REV_SUBTRACT;
            case RenderBlendOperation::MIN: return D3D11_BLEND_OP_MIN;
            case RenderBlendOperation::MAX: return D3D11_BLEND_OP_MAX;
            default: return D3D11_BLEND_OP_ADD;
            }
        }

        D3D11_LOGIC_OP toD3D11(RenderLogicOperation operation) {
            switch (operation) {
            case RenderLogicOperation::CLEAR: return D3D11_LOGIC_OP_CLEAR;
            case RenderLogicOperation::SET: return D3D11_LOGIC_OP_SET;
            case RenderLogicOperation::COPY: return D3D11_LOGIC_OP_COPY;
            case RenderLogicOperation::COPY_INVERTED: return D3D11_LOGIC_OP_COPY_INVERTED;
            case RenderLogicOperation::INVERT: return D3D11_LOGIC_OP_INVERT;
            case RenderLogicOperation::AND: return D3D11_LOGIC_OP_AND;
            case RenderLogicOperation::NAND: return D3D11_LOGIC_OP_NAND;
            case RenderLogicOperation::OR: return D3D11_LOGIC_OP_OR;
            case RenderLogicOperation::NOR: return D3D11_LOGIC_OP_NOR;
            case RenderLogicOperation::XOR: return D3D11_LOGIC_OP_XOR;
            case RenderLogicOperation::EQUIV: return D3D11_LOGIC_OP_EQUIV;
            case RenderLogicOperation::AND_REVERSE: return D3D11_LOGIC_OP_AND_REVERSE;
            case RenderLogicOperation::AND_INVERTED: return D3D11_LOGIC_OP_AND_INVERTED;
            case RenderLogicOperation::OR_REVERSE: return D3D11_LOGIC_OP_OR_REVERSE;
            case RenderLogicOperation::OR_INVERTED: return D3D11_LOGIC_OP_OR_INVERTED;
            default: return D3D11_LOGIC_OP_NOOP;
            }
        }

        D3D11_COMPARISON_FUNC toD3D11(RenderComparisonFunction function) {
            switch (function) {
            case RenderComparisonFunction::LESS: return D3D11_COMPARISON_LESS;
            case RenderComparisonFunction::EQUAL: return D3D11_COMPARISON_EQUAL;
            case RenderComparisonFunction::LESS_EQUAL: return D3D11_COMPARISON_LESS_EQUAL;
            case RenderComparisonFunction::GREATER: return D3D11_COMPARISON_GREATER;
            case RenderComparisonFunction::NOT_EQUAL: return D3D11_COMPARISON_NOT_EQUAL;
            case RenderComparisonFunction::GREATER_EQUAL: return D3D11_COMPARISON_GREATER_EQUAL;
            case RenderComparisonFunction::ALWAYS: return D3D11_COMPARISON_ALWAYS;
            default: return D3D11_COMPARISON_NEVER;
            }
        }

        D3D11_STENCIL_OP toD3D11(RenderStencilOp operation) {
            switch (operation) {
            case RenderStencilOp::ZERO: return D3D11_STENCIL_OP_ZERO;
            case RenderStencilOp::REPLACE: return D3D11_STENCIL_OP_REPLACE;
            case RenderStencilOp::INCREMENT_AND_CLAMP: return D3D11_STENCIL_OP_INCR_SAT;
            case RenderStencilOp::DECREMENT_AND_CLAMP: return D3D11_STENCIL_OP_DECR_SAT;
            case RenderStencilOp::INVERT: return D3D11_STENCIL_OP_INVERT;
            case RenderStencilOp::INCREMENT_AND_WRAP: return D3D11_STENCIL_OP_INCR;
            case RenderStencilOp::DECREMENT_AND_WRAP: return D3D11_STENCIL_OP_DECR;
            default: return D3D11_STENCIL_OP_KEEP;
            }
        }

        D3D11_TEXTURE_ADDRESS_MODE toD3D11(RenderTextureAddressMode mode) {
            switch (mode) {
            case RenderTextureAddressMode::MIRROR: return D3D11_TEXTURE_ADDRESS_MIRROR;
            case RenderTextureAddressMode::CLAMP: return D3D11_TEXTURE_ADDRESS_CLAMP;
            case RenderTextureAddressMode::BORDER: return D3D11_TEXTURE_ADDRESS_BORDER;
            case RenderTextureAddressMode::MIRROR_ONCE: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
            default: return D3D11_TEXTURE_ADDRESS_WRAP;
            }
        }

        D3D11_PRIMITIVE_TOPOLOGY toD3D11(RenderPrimitiveTopology topology) {
            switch (topology) {
            case RenderPrimitiveTopology::POINT_LIST: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
            case RenderPrimitiveTopology::LINE_LIST: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            case RenderPrimitiveTopology::LINE_STRIP: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
            case RenderPrimitiveTopology::TRIANGLE_STRIP: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            default: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            }
        }

        D3D11_FILTER toFilter(const RenderSamplerDesc &desc) {
            if (desc.anisotropyEnabled) {
                return desc.comparisonEnabled ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
            }

            uint32_t filter = 0;
            filter |= (desc.mipmapMode == RenderMipmapMode::LINEAR) ? 0x1 : 0x0;
            filter |= (desc.magFilter == RenderFilter::LINEAR) ? 0x4 : 0x0;
            filter |= (desc.minFilter == RenderFilter::LINEAR) ? 0x10 : 0x0;
            filter |= desc.comparisonEnabled ? 0x80 : 0x0;
            return D3D11_FILTER(filter);
        }

        std::string Utf16ToUtf8(const wchar_t *value) {
            const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
            std::string result(size > 0 ? size - 1 : 0, '\0');
            WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
            return result;
        }

        uint32_t mipExtent(uint32_t extent, uint32_t mip) {
            return std::max(extent >> mip, 1u);
        }

        // Format bytes per block row and rows of blocks for a width/height in texels.
        uint32_t blockRowBytes(RenderFormat format, uint32_t width) {
            const uint32_t block = RenderFormatBlockWidth(format);
            return ((width + block - 1) / block) * RenderFormatSize(format);
        }

        uint32_t blockRows(RenderFormat format, uint32_t height) {
            const uint32_t block = RenderFormatBlockWidth(format);
            return (height + block - 1) / block;
        }

        struct InternalConstants {
            float color[4];
            float depth;
            uint32_t slice;
            uint32_t mip;
            uint32_t pad;
            uint32_t mapping[4];
        };
    }

    struct D3D11Device;
    struct D3D11Replay;

    // A buffer's bytes live in `data`; GPU mirrors receive the ranges draws read.
    struct D3D11Buffer : RenderBuffer {
        struct Mirror {
            ComPtr<ID3D11Buffer> buffer;
            uint64_t epoch = 0;
            std::map<uint64_t, uint64_t> ranges; // Synced [begin, end) in this epoch.
        };

        D3D11Device *device = nullptr;
        RenderBufferDesc desc;
        std::vector<uint8_t> data;
        Mirror geometry;
        std::map<uint32_t, Mirror> structured; // By stride.
        std::map<std::tuple<uint32_t, uint64_t, uint64_t>, ComPtr<ID3D11ShaderResourceView>> structuredViews;

        D3D11Buffer(D3D11Device *device, const RenderBufferDesc &desc) : device(device), desc(desc) {
            // Slack lets a mirror round its size up to a stride or 16 bytes.
            data.resize(size_t((desc.size + 255) & ~uint64_t(255)));
        }

        void *map(uint32_t, const RenderRange *) override { return data.data(); }
        void unmap(uint32_t, const RenderRange *) override { }
        std::unique_ptr<RenderBufferFormattedView> createBufferFormattedView(RenderFormat) override { return std::make_unique<RenderBufferFormattedView>(); }
        void setName(const std::string &) override { }
        uint64_t getDeviceAddress() const override { return 0; }

        // A replayed write to `data` leaves every mirror stale.
        void invalidateMirrors() {
            geometry.ranges.clear();
            for (auto &entry : structured) entry.second.ranges.clear();
        }
    };

    struct D3D11Texture : RenderTexture {
        D3D11Device *device = nullptr;
        RenderTextureDesc desc;
        ComPtr<ID3D11Resource> resource;
        ComPtr<ID3D11ShaderResourceView> srv;
        std::map<uint32_t, ComPtr<ID3D11RenderTargetView>> rtvs; // By mip << 16 | slice.
        ComPtr<ID3D11DepthStencilView> dsv;
        ComPtr<ID3D11DepthStencilView> dsvReadOnly;
        // Bumped by every replayed write; swizzled views compare against it.
        uint64_t version = 1;

        D3D11Texture(D3D11Device *device, const RenderTextureDesc &desc) : device(device), desc(desc) { }
        bool create();
        ID3D11RenderTargetView *renderTarget(uint32_t mip, uint32_t slice, RenderFormat viewFormat);
        ID3D11DepthStencilView *depthStencil(bool readOnly);
        std::unique_ptr<RenderTextureView> createTextureView(const RenderTextureViewDesc &desc) const override;
        void setName(const std::string &) override { }

        uint32_t subresource(uint32_t mip, uint32_t slice) const {
            return D3D11CalcSubresource(mip, slice, desc.mipLevels);
        }
    };

    struct D3D11TextureView : RenderTextureView {
        D3D11Texture *texture = nullptr;
        RenderTextureViewDesc desc;
        ComPtr<ID3D11ShaderResourceView> srv;
        // A component mapping D3D11 cannot express: srv reads `shadow`, which
        // refresh() rebuilds from the texture through `source`.
        bool swizzled = false;
        uint32_t mapping[4] = {};
        ComPtr<ID3D11Texture2D> shadow;
        ComPtr<ID3D11ShaderResourceView> source;
        std::vector<ComPtr<ID3D11RenderTargetView>> shadowTargets; // By mip * slices + slice.
        uint64_t shadowVersion = 0;

        bool create(D3D11Texture *texture, const RenderTextureViewDesc &desc);
        void refresh(D3D11Replay &replay);
    };

    struct D3D11Sampler : RenderSampler {
        ComPtr<ID3D11SamplerState> d3d;
    };

    struct D3D11Shader : RenderShader {
        D3D11Device *device = nullptr;
        std::vector<uint8_t> bytecode;
        std::mutex mutex;
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11GeometryShader> gs;
        ComPtr<ID3D11PixelShader> ps;

        void setName(const std::string &) override { }
        ID3D11VertexShader *vertex();
        ID3D11GeometryShader *geometry();
        ID3D11PixelShader *pixel();
    };

    struct D3D11PipelineLayout : RenderPipelineLayout {
        std::vector<RenderPushConstantRange> pushConstantRanges;
    };

    struct D3D11GraphicsPipeline : RenderPipeline {
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11GeometryShader> gs;
        ComPtr<ID3D11PixelShader> ps;
        ComPtr<ID3D11InputLayout> inputLayout;
        ComPtr<ID3D11BlendState> blend;
        ComPtr<ID3D11RasterizerState> rasterizer;
        ComPtr<ID3D11DepthStencilState> depthStencil;
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        UINT stencilReference = 0;

        void setName(const std::string &) override { }
        RenderPipelineProgram getProgram(const std::string &) const override { return {}; }
    };

    struct D3D11Descriptor {
        RenderDescriptorRangeType type = RenderDescriptorRangeType::UNKNOWN;
        uint32_t binding = 0;
        ID3D11ShaderResourceView *srv = nullptr;
        D3D11TextureView *swizzled = nullptr;
        ID3D11SamplerState *sampler = nullptr;
        D3D11Buffer *buffer = nullptr;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint32_t stride = 0;
    };

    struct D3D11DescriptorSet : RenderDescriptorSet {
        std::vector<D3D11Descriptor> descriptors;

        void setBuffer(uint32_t index, const RenderBuffer *buffer, uint64_t bufferSize, const RenderBufferStructuredView *structuredView, const RenderBufferFormattedView *, uint64_t bufferOffset) override {
            D3D11Descriptor &descriptor = descriptors[index];
            descriptor.buffer = const_cast<D3D11Buffer *>(static_cast<const D3D11Buffer *>(buffer));
            descriptor.offset = bufferOffset + (structuredView ? uint64_t(structuredView->firstElement) * structuredView->structureByteStride : 0);
            descriptor.size = bufferSize;
            descriptor.stride = structuredView ? structuredView->structureByteStride : 0;
        }

        void setTexture(uint32_t index, const RenderTexture *texture, RenderTextureLayout, const RenderTextureView *textureView) override {
            D3D11Descriptor &descriptor = descriptors[index];
            const auto *view = static_cast<const D3D11TextureView *>(textureView);
            descriptor.swizzled = (view && view->swizzled) ? const_cast<D3D11TextureView *>(view) : nullptr;
            descriptor.srv = view ? view->srv.Get() : texture ? static_cast<const D3D11Texture *>(texture)->srv.Get() : nullptr;
        }

        void setSampler(uint32_t index, const RenderSampler *sampler) override {
            descriptors[index].sampler = sampler ? static_cast<const D3D11Sampler *>(sampler)->d3d.Get() : nullptr;
        }

        void setAccelerationStructure(uint32_t, const RenderAccelerationStructure *) override { }
    };

    struct D3D11Framebuffer : RenderFramebuffer {
        std::vector<D3D11Texture *> colors;
        std::vector<const D3D11TextureView *> colorViews;
        D3D11Texture *depth = nullptr;
        bool depthReadOnly = false;
        uint32_t width = 0;
        uint32_t height = 0;

        uint32_t getWidth() const override { return width; }
        uint32_t getHeight() const override { return height; }
    };

    struct D3D11CommandFence : RenderCommandFence {
        ComPtr<ID3D11Query> query;
        bool pending = false;
    };

    struct D3D11CommandSemaphore : RenderCommandSemaphore { };

    struct D3D11QueryPool : RenderQueryPool {
        D3D11Device *device = nullptr;
        RenderQueryType type = RenderQueryType::OCCLUSION;
        std::vector<ComPtr<ID3D11Query>> queries;
        std::vector<uint64_t> results;

        void queryResults(uint32_t queryCount) override { queryResultsRange(0, queryCount ? queryCount : getCount()); }
        bool queryResultsRange(uint32_t firstQuery, uint32_t queryCount) override;
        const uint64_t *getResults() const override { return results.data(); }
        uint32_t getCount() const override { return uint32_t(results.size()); }
    };

    // State a replay accumulates; applied as a whole before the next draw.
    struct D3D11Replay {
        D3D11Device *device = nullptr;
        ID3D11DeviceContext *context = nullptr;
        uint64_t epoch = 0;
        bool dirty = true;
        const D3D11GraphicsPipeline *pipeline = nullptr;
        const D3D11PipelineLayout *layout = nullptr;
        std::vector<D3D11Descriptor> sets[8];
        std::vector<uint8_t> pushConstants;
        struct Vertex { D3D11Buffer *buffer = nullptr; uint64_t offset = 0; uint32_t size = 0; uint32_t stride = 0; } vertices[16];
        uint32_t vertexCount = 0;
        struct Index { D3D11Buffer *buffer = nullptr; uint64_t offset = 0; uint32_t size = 0; DXGI_FORMAT format = DXGI_FORMAT_R32_UINT; } indices;
        const D3D11Framebuffer *framebuffer = nullptr;
        std::vector<D3D11_VIEWPORT> viewports;
        std::vector<D3D11_RECT> scissors;
        float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        void prepareDraw();
        void bindTargets(const D3D11Framebuffer *target);
        void markTargetsWritten();
        void bindConstantBuffer(uint32_t slot, const uint8_t *bytes, uint64_t size);
        void syncMirror(D3D11Buffer::Mirror &mirror, const D3D11Buffer &buffer, uint64_t begin, uint64_t end);
    };

    struct D3D11CommandList : RenderCommandList {
        D3D11Device *device = nullptr;
        std::vector<std::function<void(D3D11Replay &)>> commands;
        // The layout at this point of recording; push constant sizes come from it.
        const D3D11PipelineLayout *recordedLayout = nullptr;

        void begin() override { commands.clear(); recordedLayout = nullptr; }
        void end() override { }
        void barriers(RenderBarrierStages, const RenderBufferBarrier *, uint32_t, const RenderTextureBarrier *, uint32_t) override { }
        void dispatch(uint32_t, uint32_t, uint32_t) override { assert(false && "D3D11 backend has no compute pipelines."); }
        void traceRays(uint32_t, uint32_t, uint32_t, RenderBufferReference, const RenderShaderBindingGroupsInfo &) override { assert(false); }
        void drawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation) override;
        void drawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation) override;
        void setPipeline(const RenderPipeline *pipeline) override;
        void setComputePipelineLayout(const RenderPipelineLayout *) override { }
        void setComputePushConstants(uint32_t, const void *, uint32_t, uint32_t) override { }
        void setComputeDescriptorSet(RenderDescriptorSet *, uint32_t) override { }
        void setGraphicsPipelineLayout(const RenderPipelineLayout *pipelineLayout) override;
        void setGraphicsPushConstants(uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) override;
        void setGraphicsDescriptorSet(RenderDescriptorSet *descriptorSet, uint32_t setIndex) override;
        void setGraphicsRootDescriptor(RenderBufferReference, uint32_t) override { assert(false && "D3D11 backend has no root descriptors."); }
        void setRaytracingPipelineLayout(const RenderPipelineLayout *) override { }
        void setRaytracingPushConstants(uint32_t, const void *, uint32_t, uint32_t) override { }
        void setRaytracingDescriptorSet(RenderDescriptorSet *, uint32_t) override { }
        void setIndexBuffer(const RenderIndexBufferView *view) override;
        void setVertexBuffers(uint32_t startSlot, const RenderVertexBufferView *views, uint32_t viewCount, const RenderInputSlot *inputSlots) override;
        void setViewports(const RenderViewport *viewports, uint32_t count) override;
        void setScissors(const RenderRect *scissorRects, uint32_t count) override;
        void setFramebuffer(const RenderFramebuffer *framebuffer) override;
        void setDepthBias(float, float, float) override { }
        void setBlendFactor(const float *rgba) override;
        void clearColor(uint32_t attachmentIndex, RenderColor colorValue, const RenderRect *clearRects, uint32_t clearRectsCount) override;
        void clearDepthStencil(bool clearDepth, bool clearStencil, float depthValue, uint32_t stencilValue, const RenderRect *clearRects, uint32_t clearRectsCount) override;
        void copyBufferRegion(RenderBufferReference dstBuffer, RenderBufferReference srcBuffer, uint64_t size) override;
        void copyTextureRegion(const RenderTextureCopyLocation &dstLocation, const RenderTextureCopyLocation &srcLocation, uint32_t dstX, uint32_t dstY, uint32_t dstZ, const RenderBox *srcBox) override;
        void copyBuffer(const RenderBuffer *dstBuffer, const RenderBuffer *srcBuffer) override;
        void copyTexture(const RenderTexture *dstTexture, const RenderTexture *srcTexture) override;
        void resolveTexture(const RenderTexture *dstTexture, const RenderTexture *srcTexture) override;
        void resolveTextureRegion(const RenderTexture *dstTexture, uint32_t, uint32_t, const RenderTexture *srcTexture, const RenderRect *, RenderResolveMode) override { resolveTexture(dstTexture, srcTexture); }
        void buildBottomLevelAS(const RenderAccelerationStructure *, RenderBufferReference, const RenderBottomLevelASBuildInfo &) override { assert(false); }
        void buildTopLevelAS(const RenderAccelerationStructure *, RenderBufferReference, RenderBufferReference, const RenderTopLevelASBuildInfo &) override { assert(false); }
        void discardTexture(const RenderTexture *) override { }
        void resetQueryPool(const RenderQueryPool *, uint32_t, uint32_t) override { }
        void writeTimestamp(const RenderQueryPool *, uint32_t) override { }
        void beginQuery(const RenderQueryPool *queryPool, uint32_t queryIndex) override;
        void endQuery(const RenderQueryPool *queryPool, uint32_t queryIndex) override;
    };

    struct D3D11SwapChain;

    struct D3D11CommandQueue : RenderCommandQueue {
        D3D11Device *device = nullptr;

        std::unique_ptr<RenderCommandList> createCommandList() override;
        std::unique_ptr<RenderSwapChain> createSwapChain(const RenderSwapChainDesc &desc) override;
        void executeCommandLists(const RenderCommandList **commandLists, uint32_t commandListCount, RenderCommandSemaphore **, uint32_t, RenderCommandSemaphore **, uint32_t, RenderCommandFence *signalFence) override;
        void waitForCommandFence(RenderCommandFence *fence) override;
        bool pollCommandFence(RenderCommandFence *fence) override;
        void waitForIdle() override;
    };

    struct D3D11Pool : RenderPool {
        RenderDevice *device = nullptr;
        std::unique_ptr<RenderBuffer> createBuffer(const RenderBufferDesc &desc) override { return device->createBuffer(desc); }
        std::unique_ptr<RenderTexture> createTexture(const RenderTextureDesc &desc) override { return device->createTexture(desc); }
    };

    struct D3D11Interface;

    struct D3D11Device : RenderDevice {
        D3D11Interface *renderInterface = nullptr;
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<ID3D11Device> d3d;
        ComPtr<ID3D11Device1> d3d1;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D11InfoQueue> infoQueue;
        bool logicOps = false;
        // Every use of the immediate context holds this.
        std::recursive_mutex contextMutex;
        uint64_t epoch = 0;
        RenderDeviceCapabilities capabilities;
        RenderDeviceDescription description;

        // Per-slot constant buffers that draws write with WRITE_DISCARD.
        struct DynamicConstants {
            ComPtr<ID3D11Buffer> buffer;
            uint32_t size = 0;
            const void *source = nullptr;
            uint64_t sourceOffset = 0;
            uint64_t sourceEpoch = 0;
        } constants[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];

        // Internal passes: rect clears and component swizzles.
        ComPtr<ID3D11VertexShader> fullscreenVS;
        ComPtr<ID3D11PixelShader> clearPS;
        ComPtr<ID3D11PixelShader> swizzlePS;
        ComPtr<ID3D11Buffer> internalConstants;
        ComPtr<ID3D11RasterizerState> internalRasterizer;
        ComPtr<ID3D11BlendState> internalBlend;
        ComPtr<ID3D11DepthStencilState> internalDepthStencil[4]; // Bit 0 depth, bit 1 stencil.

        D3D11Device(D3D11Interface *renderInterface);
        bool isValid() const { return d3d != nullptr; }
        void drainMessages(const char *where);
        void internalPass(InternalConstants constants, ID3D11RenderTargetView *target, ID3D11DepthStencilView *depth,
            const D3D11_VIEWPORT &viewport, const D3D11_RECT &scissor, ID3D11PixelShader *pixelShader,
            ID3D11ShaderResourceView *source, uint32_t depthStencilIndex, uint32_t stencilReference);
        void waitForGpu();

        std::unique_ptr<RenderDescriptorSet> createDescriptorSet(const RenderDescriptorSetDesc &desc) override;
        std::unique_ptr<RenderShader> createShader(const void *data, uint64_t size, const char *entryPointName, RenderShaderFormat format) override;
        std::unique_ptr<RenderSampler> createSampler(const RenderSamplerDesc &desc) override;
        std::unique_ptr<RenderPipeline> createComputePipeline(const RenderComputePipelineDesc &) override { return nullptr; }
        std::unique_ptr<RenderPipeline> createGraphicsPipeline(const RenderGraphicsPipelineDesc &desc) override;
        std::unique_ptr<RenderPipeline> createRaytracingPipeline(const RenderRaytracingPipelineDesc &, const RenderPipeline *) override { return nullptr; }
        std::unique_ptr<RenderCommandQueue> createCommandQueue(RenderCommandListType type) override;
        std::unique_ptr<RenderBuffer> createBuffer(const RenderBufferDesc &desc) override { return std::make_unique<D3D11Buffer>(this, desc); }
        std::unique_ptr<RenderTexture> createTexture(const RenderTextureDesc &desc) override;
        std::unique_ptr<RenderAccelerationStructure> createAccelerationStructure(const RenderAccelerationStructureDesc &) override { return nullptr; }
        std::unique_ptr<RenderPool> createPool(const RenderPoolDesc &) override;
        std::unique_ptr<RenderPipelineLayout> createPipelineLayout(const RenderPipelineLayoutDesc &desc) override;
        std::unique_ptr<RenderCommandFence> createCommandFence() override;
        std::unique_ptr<RenderCommandSemaphore> createCommandSemaphore() override { return std::make_unique<D3D11CommandSemaphore>(); }
        std::unique_ptr<RenderFramebuffer> createFramebuffer(const RenderFramebufferDesc &desc) override;
        std::unique_ptr<RenderQueryPool> createQueryPool(uint32_t queryCount, RenderQueryType type) override;
        void setBottomLevelASBuildInfo(RenderBottomLevelASBuildInfo &, const RenderBottomLevelASMesh *, uint32_t, bool, bool) override { }
        void setTopLevelASBuildInfo(RenderTopLevelASBuildInfo &, const RenderTopLevelASInstance *, uint32_t, bool, bool) override { }
        void setShaderBindingTableInfo(RenderShaderBindingTableInfo &, const RenderShaderBindingGroups &, const RenderPipeline *, RenderDescriptorSet **, uint32_t) override { }
        const RenderDeviceCapabilities &getCapabilities() const override { return capabilities; }
        const RenderDeviceDescription &getDescription() const override { return description; }
        RenderSampleCounts getSampleCountsSupported(RenderFormat format) const override;
        bool beginCapture() override { return false; }
        bool endCapture() override { return false; }
    };

    struct D3D11Interface : RenderInterface {
        ComPtr<IDXGIFactory1> dxgiFactory;
        RenderInterfaceCapabilities capabilities;
        std::vector<std::string> deviceNames;
        bool debugLayer = false;
        bool allowTearing = false;

        D3D11Interface();
        std::unique_ptr<RenderDevice> createDevice(const std::string &preferredDeviceName) override;
        const std::vector<std::string> &getDeviceNames() const override { return deviceNames; }
        const RenderInterfaceCapabilities &getCapabilities() const override { return capabilities; }
    };

    struct D3D11SwapChain : RenderSwapChain {
        D3D11Device *device = nullptr;
        HWND window = nullptr;
        ComPtr<IDXGISwapChain> swapChain;
        RenderFormat format = RenderFormat::B8G8R8A8_UNORM;
        UINT flags = 0;
        bool flipModel = false;
        bool vsync = true;
        uint32_t width = 0;
        uint32_t height = 0;
        // D3D11 always renders to buffer 0; the runtime rotates the rest.
        std::unique_ptr<D3D11Texture> texture;

        bool create(const RenderSwapChainDesc &desc);
        void acquireBuffer();
        bool present(uint32_t textureIndex, RenderCommandSemaphore **, uint32_t) override;
        void wait() override { }
        bool resize() override;
        bool needsResize() const override;
        void setVsyncEnabled(bool vsyncEnabled) override { vsync = vsyncEnabled; }
        bool isVsyncEnabled() const override { return vsync; }
        uint32_t getWidth() const override { return width; }
        uint32_t getHeight() const override { return height; }
        RenderTexture *getTexture(uint32_t) override { return texture.get(); }
        uint32_t getTextureCount() const override { return 1; }
        bool acquireTexture(RenderCommandSemaphore *, uint32_t *textureIndex) override { *textureIndex = 0; return true; }
        RenderWindow getWindow() const override { return window; }
        bool isEmpty() const override { return (width == 0) || (height == 0); }
        uint32_t getRefreshRate() const override { return 0; }
    };

    // D3D11Texture

    bool D3D11Texture::create() {
        UINT bindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (desc.flags & RenderTextureFlag::RENDER_TARGET) bindFlags |= D3D11_BIND_RENDER_TARGET;
        if (desc.flags & RenderTextureFlag::DEPTH_TARGET) bindFlags |= D3D11_BIND_DEPTH_STENCIL;
        if (desc.flags & RenderTextureFlag::UNORDERED_ACCESS) bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        desc.mipLevels = std::max(desc.mipLevels, 1u);
        desc.arraySize = std::max(desc.arraySize, 1u);
        desc.depth = std::max(desc.depth, 1u);
        HRESULT res;
        if (desc.dimension == RenderTextureDimension::TEXTURE_3D) {
            D3D11_TEXTURE3D_DESC textureDesc = {};
            textureDesc.Width = desc.width;
            textureDesc.Height = desc.height;
            textureDesc.Depth = desc.depth;
            textureDesc.MipLevels = desc.mipLevels;
            textureDesc.Format = toResourceFormat(desc.format);
            textureDesc.Usage = D3D11_USAGE_DEFAULT;
            textureDesc.BindFlags = bindFlags;
            ComPtr<ID3D11Texture3D> texture;
            res = device->d3d->CreateTexture3D(&textureDesc, nullptr, texture.GetAddressOf());
            resource = texture;
        }
        else {
            D3D11_TEXTURE2D_DESC textureDesc = {};
            textureDesc.Width = desc.width;
            textureDesc.Height = std::max(desc.height, 1u);
            textureDesc.MipLevels = desc.mipLevels;
            textureDesc.ArraySize = desc.arraySize;
            textureDesc.Format = toResourceFormat(desc.format);
            textureDesc.SampleDesc.Count = std::max(desc.multisampling.sampleCount, 1u);
            textureDesc.Usage = D3D11_USAGE_DEFAULT;
            textureDesc.BindFlags = bindFlags;
            textureDesc.MiscFlags = (desc.flags & RenderTextureFlag::CUBE) ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
            ComPtr<ID3D11Texture2D> texture;
            res = device->d3d->CreateTexture2D(&textureDesc, nullptr, texture.GetAddressOf());
            resource = texture;
        }

        if (FAILED(res)) {
            fprintf(stderr, "D3D11: texture creation failed (%ux%u format %u) with error code 0x%lX.\n", desc.width, desc.height, uint32_t(desc.format), res);
            return false;
        }

        RenderTextureViewDesc viewDesc;
        viewDesc.format = desc.format;
        viewDesc.dimension = (desc.flags & RenderTextureFlag::CUBE) ? RenderTextureViewDimension::TEXTURE_CUBE : RenderTextureDimensionToView(desc.dimension);
        D3D11TextureView view;
        if (!view.create(this, viewDesc)) {
            return false;
        }

        srv = view.srv;
        return true;
    }

    ID3D11RenderTargetView *D3D11Texture::renderTarget(uint32_t mip, uint32_t slice, RenderFormat viewFormat) {
        const uint32_t key = (mip << 16) | slice;
        auto it = rtvs.find(key);
        if (it != rtvs.end()) {
            return it->second.Get();
        }

        D3D11_RENDER_TARGET_VIEW_DESC viewDesc = {};
        viewDesc.Format = toDXGI((viewFormat != RenderFormat::UNKNOWN) ? viewFormat : desc.format);
        if (desc.dimension == RenderTextureDimension::TEXTURE_3D) {
            viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.MipSlice = mip;
            viewDesc.Texture3D.FirstWSlice = slice;
            viewDesc.Texture3D.WSize = 1;
        }
        else if (desc.multisampling.sampleCount > 1) {
            viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
        }
        else {
            viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.MipSlice = mip;
            viewDesc.Texture2DArray.FirstArraySlice = slice;
            viewDesc.Texture2DArray.ArraySize = 1;
        }

        ComPtr<ID3D11RenderTargetView> view;
        const HRESULT res = device->d3d->CreateRenderTargetView(resource.Get(), &viewDesc, view.GetAddressOf());
        if (FAILED(res)) {
            fprintf(stderr, "D3D11: render target view creation failed with error code 0x%lX.\n", res);
            return nullptr;
        }

        rtvs[key] = view;
        return view.Get();
    }

    ID3D11DepthStencilView *D3D11Texture::depthStencil(bool readOnly) {
        ComPtr<ID3D11DepthStencilView> &view = readOnly ? dsvReadOnly : dsv;
        if (view == nullptr) {
            D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
            viewDesc.Format = toDXGI(desc.format);
            viewDesc.ViewDimension = (desc.multisampling.sampleCount > 1) ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
            if (readOnly) {
                viewDesc.Flags = D3D11_DSV_READ_ONLY_DEPTH | (RenderFormatIsStencil(desc.format) ? D3D11_DSV_READ_ONLY_STENCIL : 0);
            }

            const HRESULT res = device->d3d->CreateDepthStencilView(resource.Get(), &viewDesc, view.GetAddressOf());
            if (FAILED(res)) {
                fprintf(stderr, "D3D11: depth stencil view creation failed with error code 0x%lX.\n", res);
            }
        }

        return view.Get();
    }

    std::unique_ptr<RenderTextureView> D3D11Texture::createTextureView(const RenderTextureViewDesc &viewDesc) const {
        auto view = std::make_unique<D3D11TextureView>();
        if (!view->create(const_cast<D3D11Texture *>(this), viewDesc)) {
            return nullptr;
        }

        return view;
    }

    // D3D11TextureView

    bool D3D11TextureView::create(D3D11Texture *texture, const RenderTextureViewDesc &viewDesc) {
        this->texture = texture;
        this->desc = viewDesc;
        const RenderTextureDesc &textureDesc = texture->desc;
        const RenderFormat format = (viewDesc.format != RenderFormat::UNKNOWN) ? viewDesc.format : textureDesc.format;
        const RenderComponentMapping &m = viewDesc.componentMapping;
        const RenderSwizzle swizzles[4] = { m.r, m.g, m.b, m.a };
        for (uint32_t i = 0; i < 4; i++) {
            mapping[i] = uint32_t(swizzles[i]);
            // R in the red channel etc. is the identity too.
            swizzled |= (mapping[i] != 0) && (mapping[i] != 3 + i);
        }

        const uint32_t mipLevels = std::min(viewDesc.mipLevels, textureDesc.mipLevels - std::min(viewDesc.mipSlice, textureDesc.mipLevels - 1));
        const bool cube = (viewDesc.dimension == RenderTextureViewDimension::TEXTURE_CUBE);
        const bool multisampled = textureDesc.multisampling.sampleCount > 1;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = toShaderResourceFormat(format, viewDesc.planeSlice);
        if (viewDesc.dimension == RenderTextureViewDimension::TEXTURE_3D) {
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
            srvDesc.Texture3D.MostDetailedMip = viewDesc.mipSlice;
            srvDesc.Texture3D.MipLevels = mipLevels;
        }
        else if (cube) {
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MostDetailedMip = viewDesc.mipSlice;
            srvDesc.TextureCube.MipLevels = mipLevels;
        }
        else if (multisampled) {
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        }
        else {
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            srvDesc.Texture2DArray.MostDetailedMip = viewDesc.mipSlice;
            srvDesc.Texture2DArray.MipLevels = mipLevels;
            srvDesc.Texture2DArray.FirstArraySlice = viewDesc.arrayIndex;
            srvDesc.Texture2DArray.ArraySize = std::min(viewDesc.arraySize, textureDesc.arraySize - viewDesc.arrayIndex);
            if ((srvDesc.Texture2DArray.ArraySize == 1) && (viewDesc.arrayIndex == 0)) {
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MostDetailedMip = viewDesc.mipSlice;
                srvDesc.Texture2D.MipLevels = mipLevels;
            }
        }

        ID3D11Device *d3d = texture->device->d3d.Get();
        if (!swizzled || (viewDesc.dimension == RenderTextureViewDimension::TEXTURE_3D) || multisampled) {
            // ponytail: a 3D or multisampled view ignores its component mapping; nothing maps one yet.
            const HRESULT res = d3d->CreateShaderResourceView(texture->resource.Get(), &srvDesc, srv.GetAddressOf());
            swizzled = false;
            if (FAILED(res)) {
                fprintf(stderr, "D3D11: shader resource view creation failed (format %u) with error code 0x%lX.\n", uint32_t(format), res);
                return false;
            }

            return true;
        }

        // The shadow keeps every mip and slice; its format holds the source's precision.
        const DXGI_FORMAT sourceFormat = toShaderResourceFormat(format, viewDesc.planeSlice);
        DXGI_FORMAT shadowFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        switch (sourceFormat) {
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            shadowFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
            break;
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            shadowFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            break;
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16G16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            shadowFormat = DXGI_FORMAT_R16G16B16A16_UNORM;
            break;
        default:
            break;
        }

        D3D11_TEXTURE2D_DESC shadowDesc = {};
        shadowDesc.Width = textureDesc.width;
        shadowDesc.Height = textureDesc.height;
        shadowDesc.MipLevels = textureDesc.mipLevels;
        shadowDesc.ArraySize = textureDesc.arraySize;
        shadowDesc.Format = shadowFormat;
        shadowDesc.SampleDesc.Count = 1;
        shadowDesc.Usage = D3D11_USAGE_DEFAULT;
        shadowDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        shadowDesc.MiscFlags = (textureDesc.flags & RenderTextureFlag::CUBE) ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
        HRESULT res = d3d->CreateTexture2D(&shadowDesc, nullptr, shadow.GetAddressOf());
        if (FAILED(res)) {
            fprintf(stderr, "D3D11: swizzle shadow creation failed with error code 0x%lX.\n", res);
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC shadowView = srvDesc;
        shadowView.Format = shadowFormat;
        res = d3d->CreateShaderResourceView(shadow.Get(), &shadowView, srv.GetAddressOf());
        if (FAILED(res)) {
            fprintf(stderr, "D3D11: swizzle shadow view creation failed with error code 0x%lX.\n", res);
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC sourceDesc = {};
        sourceDesc.Format = sourceFormat;
        sourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        sourceDesc.Texture2DArray.MipLevels = textureDesc.mipLevels;
        sourceDesc.Texture2DArray.ArraySize = textureDesc.arraySize;
        res = d3d->CreateShaderResourceView(texture->resource.Get(), &sourceDesc, source.GetAddressOf());
        if (FAILED(res)) {
            fprintf(stderr, "D3D11: swizzle source view creation failed with error code 0x%lX.\n", res);
            return false;
        }

        for (uint32_t mip = 0; mip < textureDesc.mipLevels; mip++) {
            for (uint32_t slice = 0; slice < textureDesc.arraySize; slice++) {
                D3D11_RENDER_TARGET_VIEW_DESC targetDesc = {};
                targetDesc.Format = shadowFormat;
                targetDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                targetDesc.Texture2DArray.MipSlice = mip;
                targetDesc.Texture2DArray.FirstArraySlice = slice;
                targetDesc.Texture2DArray.ArraySize = 1;
                ComPtr<ID3D11RenderTargetView> target;
                res = d3d->CreateRenderTargetView(shadow.Get(), &targetDesc, target.GetAddressOf());
                if (FAILED(res)) {
                    fprintf(stderr, "D3D11: swizzle shadow target creation failed with error code 0x%lX.\n", res);
                    return false;
                }

                shadowTargets.emplace_back(target);
            }
        }

        return true;
    }

    void D3D11TextureView::refresh(D3D11Replay &replay) {
        if (shadowVersion == texture->version) {
            return;
        }

        const RenderTextureDesc &textureDesc = texture->desc;
        InternalConstants constants = {};
        std::memcpy(constants.mapping, mapping, sizeof(mapping));
        for (uint32_t mip = 0; mip < textureDesc.mipLevels; mip++) {
            D3D11_VIEWPORT viewport = { 0.0f, 0.0f, float(mipExtent(textureDesc.width, mip)), float(mipExtent(textureDesc.height, mip)), 0.0f, 1.0f };
            D3D11_RECT scissor = { 0, 0, LONG(viewport.Width), LONG(viewport.Height) };
            for (uint32_t slice = 0; slice < textureDesc.arraySize; slice++) {
                constants.slice = slice;
                constants.mip = mip;
                replay.device->internalPass(constants, shadowTargets[mip * textureDesc.arraySize + slice].Get(), nullptr,
                    viewport, scissor, replay.device->swizzlePS.Get(), source.Get(), 0, 0);
            }
        }

        shadowVersion = texture->version;
        replay.dirty = true;
    }

    // D3D11Shader

    ID3D11VertexShader *D3D11Shader::vertex() {
        std::lock_guard<std::mutex> lock(mutex);
        if (vs == nullptr) device->d3d->CreateVertexShader(bytecode.data(), bytecode.size(), nullptr, vs.GetAddressOf());
        return vs.Get();
    }

    ID3D11GeometryShader *D3D11Shader::geometry() {
        std::lock_guard<std::mutex> lock(mutex);
        if (gs == nullptr) device->d3d->CreateGeometryShader(bytecode.data(), bytecode.size(), nullptr, gs.GetAddressOf());
        return gs.Get();
    }

    ID3D11PixelShader *D3D11Shader::pixel() {
        std::lock_guard<std::mutex> lock(mutex);
        if (ps == nullptr) device->d3d->CreatePixelShader(bytecode.data(), bytecode.size(), nullptr, ps.GetAddressOf());
        return ps.Get();
    }

    // D3D11QueryPool

    bool D3D11QueryPool::queryResultsRange(uint32_t firstQuery, uint32_t queryCount) {
        // ponytail: timestamps need a disjoint query around them; no D3D11 caller reads them yet.
        if ((type != RenderQueryType::OCCLUSION) || (firstQuery + queryCount > queries.size())) {
            return false;
        }

        std::lock_guard<std::recursive_mutex> lock(device->contextMutex);
        for (uint32_t i = firstQuery; i < firstQuery + queryCount; i++) {
            UINT64 samples = 0;
            HRESULT res;
            while ((res = device->context->GetData(queries[i].Get(), &samples, sizeof(samples), 0)) == S_FALSE) {
                std::this_thread::yield();
            }

            if (FAILED(res)) {
                return false;
            }

            results[i] = samples;
        }

        return true;
    }

    // D3D11Replay

    void D3D11Replay::syncMirror(D3D11Buffer::Mirror &mirror, const D3D11Buffer &buffer, uint64_t begin, uint64_t end) {
        if (mirror.epoch != epoch) {
            mirror.epoch = epoch;
            mirror.ranges.clear();
        }

        end = std::min<uint64_t>(end, buffer.data.size());
        if (begin >= end) {
            return;
        }

        auto it = mirror.ranges.upper_bound(begin);
        if ((it != mirror.ranges.begin()) && (std::prev(it)->second >= end)) {
            return;
        }

        D3D11_BOX box = { UINT(begin), 0, 0, UINT(end), 1, 1 };
        context->UpdateSubresource(mirror.buffer.Get(), 0, &box, buffer.data.data() + begin, 0, 0);

        // Merge [begin, end) with the ranges it touches.
        it = mirror.ranges.upper_bound(begin);
        if ((it != mirror.ranges.begin()) && (std::prev(it)->second >= begin)) {
            --it;
            begin = it->first;
        }

        while ((it != mirror.ranges.end()) && (it->first <= end)) {
            end = std::max(end, it->second);
            it = mirror.ranges.erase(it);
        }

        mirror.ranges[begin] = end;
    }

    void D3D11Replay::bindConstantBuffer(uint32_t slot, const uint8_t *bytes, uint64_t size) {
        D3D11Device::DynamicConstants &constants = device->constants[slot];
        const uint32_t bufferSize = uint32_t(std::max<uint64_t>((size + 15) & ~uint64_t(15), 16));
        if (constants.size < bufferSize) {
            D3D11_BUFFER_DESC bufferDesc = {};
            bufferDesc.ByteWidth = std::max(bufferSize, 256u);
            bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
            bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            constants.buffer.Reset();
            device->d3d->CreateBuffer(&bufferDesc, nullptr, constants.buffer.GetAddressOf());
            constants.size = bufferDesc.ByteWidth;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(context->Map(constants.buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, bytes, size_t(size));
            context->Unmap(constants.buffer.Get(), 0);
        }

        ID3D11Buffer *buffer = constants.buffer.Get();
        context->VSSetConstantBuffers(slot, 1, &buffer);
        context->GSSetConstantBuffers(slot, 1, &buffer);
        context->PSSetConstantBuffers(slot, 1, &buffer);
    }

    void D3D11Replay::bindTargets(const D3D11Framebuffer *target) {
        ID3D11RenderTargetView *views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        ID3D11DepthStencilView *depthView = nullptr;
        uint32_t count = 0;
        if (target != nullptr) {
            count = uint32_t(target->colors.size());
            for (uint32_t i = 0; i < count; i++) {
                const D3D11TextureView *view = target->colorViews[i];
                views[i] = target->colors[i]->renderTarget(view ? view->desc.mipSlice : 0, view ? view->desc.arrayIndex : 0,
                    view ? view->desc.format : RenderFormat::UNKNOWN);
            }

            if (target->depth != nullptr) {
                depthView = target->depth->depthStencil(target->depthReadOnly);
            }
        }

        context->OMSetRenderTargets(count, views, depthView);
    }

    void D3D11Replay::markTargetsWritten() {
        if (framebuffer == nullptr) {
            return;
        }

        for (D3D11Texture *texture : framebuffer->colors) {
            texture->version++;
        }

        if ((framebuffer->depth != nullptr) && !framebuffer->depthReadOnly) {
            framebuffer->depth->version++;
        }
    }

    void D3D11Replay::prepareDraw() {
        if (!dirty) {
            return;
        }

        // Swizzled views refresh first: their passes overwrite the state applied below.
        for (std::vector<D3D11Descriptor> &set : sets) {
            for (D3D11Descriptor &descriptor : set) {
                if (descriptor.swizzled != nullptr) {
                    descriptor.swizzled->refresh(*this);
                }
            }
        }

        dirty = false;
        // Last draw's textures may be this draw's targets: unbind them first.
        ID3D11ShaderResourceView *noViews[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        context->VSSetShaderResources(0, 16, noViews);
        context->GSSetShaderResources(0, 16, noViews);
        context->PSSetShaderResources(0, 16, noViews);
        bindTargets(framebuffer);
        if (pipeline != nullptr) {
            context->IASetInputLayout(pipeline->inputLayout.Get());
            context->IASetPrimitiveTopology(pipeline->topology);
            context->VSSetShader(pipeline->vs.Get(), nullptr, 0);
            context->GSSetShader(pipeline->gs.Get(), nullptr, 0);
            context->PSSetShader(pipeline->ps.Get(), nullptr, 0);
            context->OMSetBlendState(pipeline->blend.Get(), blendFactor, 0xFFFFFFFF);
            context->OMSetDepthStencilState(pipeline->depthStencil.Get(), pipeline->stencilReference);
            context->RSSetState(pipeline->rasterizer.Get());
        }

        context->RSSetViewports(UINT(viewports.size()), viewports.data());
        context->RSSetScissorRects(UINT(scissors.size()), scissors.data());

        ID3D11Buffer *vertexBuffers[16] = {};
        UINT strides[16] = {};
        UINT offsets[16] = {};
        for (uint32_t i = 0; i < vertexCount; i++) {
            Vertex &vertex = vertices[i];
            if (vertex.buffer == nullptr) {
                continue;
            }

            D3D11Buffer &buffer = *vertex.buffer;
            if (buffer.geometry.buffer == nullptr) {
                D3D11_BUFFER_DESC bufferDesc = {};
                bufferDesc.ByteWidth = UINT(buffer.data.size());
                bufferDesc.Usage = D3D11_USAGE_DEFAULT;
                bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;
                device->d3d->CreateBuffer(&bufferDesc, nullptr, buffer.geometry.buffer.GetAddressOf());
            }

            syncMirror(buffer.geometry, buffer, vertex.offset, vertex.offset + vertex.size);
            vertexBuffers[i] = buffer.geometry.buffer.Get();
            strides[i] = vertex.stride;
            offsets[i] = UINT(vertex.offset);
        }

        context->IASetVertexBuffers(0, 16, vertexBuffers, strides, offsets);
        if (indices.buffer != nullptr) {
            D3D11Buffer &buffer = *indices.buffer;
            if (buffer.geometry.buffer == nullptr) {
                D3D11_BUFFER_DESC bufferDesc = {};
                bufferDesc.ByteWidth = UINT(buffer.data.size());
                bufferDesc.Usage = D3D11_USAGE_DEFAULT;
                bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;
                device->d3d->CreateBuffer(&bufferDesc, nullptr, buffer.geometry.buffer.GetAddressOf());
            }

            syncMirror(buffer.geometry, buffer, indices.offset, indices.offset + indices.size);
            context->IASetIndexBuffer(buffer.geometry.buffer.Get(), indices.format, UINT(indices.offset));
        }

        // Every register is in space 0, so a descriptor's binding is its slot.
        ID3D11ShaderResourceView *views[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        ID3D11SamplerState *samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
        uint32_t viewCount = 0;
        uint32_t samplerCount = 0;
        for (std::vector<D3D11Descriptor> &set : sets) {
            for (D3D11Descriptor &descriptor : set) {
                switch (descriptor.type) {
                case RenderDescriptorRangeType::TEXTURE:
                    if (descriptor.binding < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT) {
                        views[descriptor.binding] = descriptor.srv;
                        viewCount = std::max(viewCount, descriptor.binding + 1);
                    }
                    break;
                case RenderDescriptorRangeType::SAMPLER:
                    if (descriptor.binding < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT) {
                        samplers[descriptor.binding] = descriptor.sampler;
                        samplerCount = std::max(samplerCount, descriptor.binding + 1);
                    }
                    break;
                case RenderDescriptorRangeType::CONSTANT_BUFFER:
                    if ((descriptor.buffer != nullptr) && (descriptor.binding < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)) {
                        const D3D11Buffer &buffer = *descriptor.buffer;
                        const uint64_t size = descriptor.size ? descriptor.size : buffer.desc.size - descriptor.offset;
                        D3D11Device::DynamicConstants &constants = device->constants[descriptor.binding];
                        const bool unchanged = (constants.source == &buffer) && (constants.sourceOffset == descriptor.offset) && (constants.sourceEpoch == epoch);
                        if (!unchanged) {
                            bindConstantBuffer(descriptor.binding, buffer.data.data() + descriptor.offset, std::min<uint64_t>(size, 65536));
                            constants.source = &buffer;
                            constants.sourceOffset = descriptor.offset;
                            constants.sourceEpoch = epoch;
                        }
                    }
                    break;
                case RenderDescriptorRangeType::STRUCTURED_BUFFER:
                    if ((descriptor.buffer != nullptr) && (descriptor.stride != 0) && (descriptor.binding < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)) {
                        D3D11Buffer &buffer = *descriptor.buffer;
                        D3D11Buffer::Mirror &mirror = buffer.structured[descriptor.stride];
                        if (mirror.buffer == nullptr) {
                            D3D11_BUFFER_DESC bufferDesc = {};
                            bufferDesc.ByteWidth = UINT(buffer.data.size() / descriptor.stride * descriptor.stride);
                            bufferDesc.Usage = D3D11_USAGE_DEFAULT;
                            bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
                            bufferDesc.StructureByteStride = descriptor.stride;
                            device->d3d->CreateBuffer(&bufferDesc, nullptr, mirror.buffer.GetAddressOf());
                        }

                        const uint64_t size = descriptor.size ? descriptor.size : buffer.desc.size - descriptor.offset;
                        syncMirror(mirror, buffer, descriptor.offset, descriptor.offset + size);
                        ComPtr<ID3D11ShaderResourceView> &view = buffer.structuredViews[{ descriptor.stride, descriptor.offset, size }];
                        if (view == nullptr) {
                            D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
                            viewDesc.Format = DXGI_FORMAT_UNKNOWN;
                            viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                            viewDesc.Buffer.FirstElement = UINT(descriptor.offset / descriptor.stride);
                            viewDesc.Buffer.NumElements = UINT(size / descriptor.stride);
                            device->d3d->CreateShaderResourceView(mirror.buffer.Get(), &viewDesc, view.GetAddressOf());
                        }

                        views[descriptor.binding] = view.Get();
                        viewCount = std::max(viewCount, descriptor.binding + 1);
                    }
                    break;
                default:
                    break;
                }
            }
        }

        context->VSSetShaderResources(0, viewCount, views);
        context->GSSetShaderResources(0, viewCount, views);
        context->PSSetShaderResources(0, viewCount, views);
        context->VSSetSamplers(0, samplerCount, samplers);
        context->GSSetSamplers(0, samplerCount, samplers);
        context->PSSetSamplers(0, samplerCount, samplers);
        if ((layout != nullptr) && !layout->pushConstantRanges.empty() && !pushConstants.empty()) {
            const RenderPushConstantRange &range = layout->pushConstantRanges[0];
            device->constants[range.binding].source = nullptr;
            bindConstantBuffer(range.binding, pushConstants.data(), pushConstants.size());
        }
    }

    // D3D11CommandList

    void D3D11CommandList::drawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation) {
        commands.emplace_back([=](D3D11Replay &r) {
            r.prepareDraw();
            r.context->DrawInstanced(vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
            r.markTargetsWritten();
        });
    }

    void D3D11CommandList::drawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation) {
        commands.emplace_back([=](D3D11Replay &r) {
            r.prepareDraw();
            r.context->DrawIndexedInstanced(indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
            r.markTargetsWritten();
        });
    }

    void D3D11CommandList::setPipeline(const RenderPipeline *pipeline) {
        const auto *native = static_cast<const D3D11GraphicsPipeline *>(pipeline);
        commands.emplace_back([=](D3D11Replay &r) { r.pipeline = native; r.dirty = true; });
    }

    void D3D11CommandList::setGraphicsPipelineLayout(const RenderPipelineLayout *pipelineLayout) {
        const auto *native = static_cast<const D3D11PipelineLayout *>(pipelineLayout);
        recordedLayout = native;
        commands.emplace_back([=](D3D11Replay &r) {
            // As with a D3D12 root signature, only a different layout drops the bindings.
            if (r.layout == native) return;
            r.layout = native;
            r.pushConstants.clear();
            for (std::vector<D3D11Descriptor> &set : r.sets) set.clear();
            r.dirty = true;
        });
    }

    void D3D11CommandList::setGraphicsPushConstants(uint32_t rangeIndex, const void *data, uint32_t offset, uint32_t size) {
        assert((recordedLayout != nullptr) && (rangeIndex < recordedLayout->pushConstantRanges.size()));
        // The bytes are captured now, as a root-constant write is.
        const uint32_t rangeSize = recordedLayout->pushConstantRanges[rangeIndex].size;
        size = std::min(size ? size : rangeSize, rangeSize - std::min(offset, rangeSize));
        const auto *bytes = static_cast<const uint8_t *>(data);
        commands.emplace_back([rangeSize, offset, captured = std::vector<uint8_t>(bytes, bytes + size)](D3D11Replay &r) {
            if (r.pushConstants.size() < rangeSize) r.pushConstants.resize(rangeSize);
            std::memcpy(r.pushConstants.data() + offset, captured.data(), captured.size());
            r.dirty = true;
        });
    }

    void D3D11CommandList::setGraphicsDescriptorSet(RenderDescriptorSet *descriptorSet, uint32_t setIndex) {
        // Descriptors are read now: Vulkan forbids changing a bound set before it runs.
        std::vector<D3D11Descriptor> descriptors = static_cast<D3D11DescriptorSet *>(descriptorSet)->descriptors;
        commands.emplace_back([setIndex, descriptors = std::move(descriptors)](D3D11Replay &r) {
            if (setIndex < 8) r.sets[setIndex] = descriptors;
            r.dirty = true;
        });
    }

    void D3D11CommandList::setIndexBuffer(const RenderIndexBufferView *view) {
        D3D11Replay::Index index;
        if (view != nullptr) {
            index.buffer = const_cast<D3D11Buffer *>(static_cast<const D3D11Buffer *>(view->buffer.ref));
            index.offset = view->buffer.offset;
            index.size = view->size;
            index.format = (view->format == RenderFormat::R16_UINT) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        }

        commands.emplace_back([=](D3D11Replay &r) { r.indices = index; r.dirty = true; });
    }

    void D3D11CommandList::setVertexBuffers(uint32_t startSlot, const RenderVertexBufferView *views, uint32_t viewCount, const RenderInputSlot *inputSlots) {
        std::vector<D3D11Replay::Vertex> vertices(viewCount);
        for (uint32_t i = 0; i < viewCount; i++) {
            vertices[i].buffer = const_cast<D3D11Buffer *>(static_cast<const D3D11Buffer *>(views[i].buffer.ref));
            vertices[i].offset = views[i].buffer.offset;
            vertices[i].size = views[i].size;
            vertices[i].stride = inputSlots ? inputSlots[i].stride : 0;
        }

        commands.emplace_back([startSlot, vertices = std::move(vertices)](D3D11Replay &r) {
            for (size_t i = 0; i < vertices.size() && startSlot + i < 16; i++) r.vertices[startSlot + i] = vertices[i];
            r.vertexCount = std::max(r.vertexCount, uint32_t(std::min<size_t>(startSlot + vertices.size(), 16)));
            r.dirty = true;
        });
    }

    void D3D11CommandList::setViewports(const RenderViewport *viewports, uint32_t count) {
        std::vector<D3D11_VIEWPORT> native(count);
        for (uint32_t i = 0; i < count; i++) {
            native[i] = { viewports[i].x, viewports[i].y, viewports[i].width, viewports[i].height, viewports[i].minDepth, viewports[i].maxDepth };
        }

        commands.emplace_back([native = std::move(native)](D3D11Replay &r) { r.viewports = native; r.dirty = true; });
    }

    void D3D11CommandList::setScissors(const RenderRect *scissorRects, uint32_t count) {
        std::vector<D3D11_RECT> native(count);
        for (uint32_t i = 0; i < count; i++) {
            native[i] = { scissorRects[i].left, scissorRects[i].top, scissorRects[i].right, scissorRects[i].bottom };
        }

        commands.emplace_back([native = std::move(native)](D3D11Replay &r) { r.scissors = native; r.dirty = true; });
    }

    void D3D11CommandList::setFramebuffer(const RenderFramebuffer *framebuffer) {
        const auto *native = static_cast<const D3D11Framebuffer *>(framebuffer);
        commands.emplace_back([=](D3D11Replay &r) { r.framebuffer = native; r.dirty = true; });
    }

    void D3D11CommandList::setBlendFactor(const float *rgba) {
        const std::array<float, 4> factor = { rgba[0], rgba[1], rgba[2], rgba[3] };
        commands.emplace_back([=](D3D11Replay &r) { std::memcpy(r.blendFactor, factor.data(), sizeof(r.blendFactor)); r.dirty = true; });
    }

    void D3D11CommandList::clearColor(uint32_t attachmentIndex, RenderColor colorValue, const RenderRect *clearRects, uint32_t clearRectsCount) {
        std::vector<D3D11_RECT> rects(clearRectsCount);
        for (uint32_t i = 0; i < clearRectsCount; i++) {
            rects[i] = { clearRects[i].left, clearRects[i].top, clearRects[i].right, clearRects[i].bottom };
        }

        commands.emplace_back([=, rects = std::move(rects)](D3D11Replay &r) {
            const D3D11Framebuffer *framebuffer = r.framebuffer;
            if ((framebuffer == nullptr) || (attachmentIndex >= framebuffer->colors.size())) return;
            D3D11Texture *texture = framebuffer->colors[attachmentIndex];
            const D3D11TextureView *view = framebuffer->colorViews[attachmentIndex];
            ID3D11RenderTargetView *target = texture->renderTarget(view ? view->desc.mipSlice : 0, view ? view->desc.arrayIndex : 0, view ? view->desc.format : RenderFormat::UNKNOWN);
            if (rects.empty()) {
                r.context->ClearRenderTargetView(target, colorValue.rgba);
            }
            else {
                InternalConstants constants = {};
                std::memcpy(constants.color, colorValue.rgba, sizeof(constants.color));
                const D3D11_VIEWPORT viewport = { 0.0f, 0.0f, float(framebuffer->width), float(framebuffer->height), 0.0f, 1.0f };
                for (const D3D11_RECT &rect : rects) {
                    r.device->internalPass(constants, target, nullptr, viewport, rect, r.device->clearPS.Get(), nullptr, 0, 0);
                }
            }

            texture->version++;
            r.dirty = true;
        });
    }

    void D3D11CommandList::clearDepthStencil(bool clearDepth, bool clearStencil, float depthValue, uint32_t stencilValue, const RenderRect *clearRects, uint32_t clearRectsCount) {
        std::vector<D3D11_RECT> rects(clearRectsCount);
        for (uint32_t i = 0; i < clearRectsCount; i++) {
            rects[i] = { clearRects[i].left, clearRects[i].top, clearRects[i].right, clearRects[i].bottom };
        }

        commands.emplace_back([=, rects = std::move(rects)](D3D11Replay &r) {
            const D3D11Framebuffer *framebuffer = r.framebuffer;
            if ((framebuffer == nullptr) || (framebuffer->depth == nullptr)) return;
            D3D11Texture *texture = framebuffer->depth;
            const bool stencil = clearStencil && RenderFormatIsStencil(texture->desc.format);
            ID3D11DepthStencilView *target = texture->depthStencil(false);
            if (rects.empty()) {
                const UINT flags = (clearDepth ? D3D11_CLEAR_DEPTH : 0) | (stencil ? D3D11_CLEAR_STENCIL : 0);
                if (flags != 0) r.context->ClearDepthStencilView(target, flags, depthValue, UINT8(stencilValue));
            }
            else {
                InternalConstants constants = {};
                constants.depth = depthValue;
                const D3D11_VIEWPORT viewport = { 0.0f, 0.0f, float(framebuffer->width), float(framebuffer->height), 0.0f, 1.0f };
                const uint32_t index = (clearDepth ? 1 : 0) | (stencil ? 2 : 0);
                for (const D3D11_RECT &rect : rects) {
                    r.device->internalPass(constants, nullptr, target, viewport, rect, nullptr, nullptr, index, stencilValue);
                }
            }

            texture->version++;
            r.dirty = true;
        });
    }

    void D3D11CommandList::copyBufferRegion(RenderBufferReference dstBuffer, RenderBufferReference srcBuffer, uint64_t size) {
        auto *dst = const_cast<D3D11Buffer *>(static_cast<const D3D11Buffer *>(dstBuffer.ref));
        const auto *src = static_cast<const D3D11Buffer *>(srcBuffer.ref);
        const uint64_t dstOffset = dstBuffer.offset;
        const uint64_t srcOffset = srcBuffer.offset;
        commands.emplace_back([=](D3D11Replay &r) {
            std::memmove(dst->data.data() + dstOffset, src->data.data() + srcOffset, size_t(size));
            dst->invalidateMirrors();
            r.dirty = true;
        });
    }

    void D3D11CommandList::copyBuffer(const RenderBuffer *dstBuffer, const RenderBuffer *srcBuffer) {
        copyBufferRegion(RenderBufferReference(dstBuffer), RenderBufferReference(srcBuffer), static_cast<const D3D11Buffer *>(srcBuffer)->desc.size);
    }

    namespace {
        // A CPU-readable copy of one subresource. Depth-stencil subresources copy whole.
        ComPtr<ID3D11Texture2D> stagingCopy(D3D11Replay &r, D3D11Texture &texture, uint32_t subresource, uint32_t mip) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = mipExtent(texture.desc.width, mip);
            desc.Height = mipExtent(texture.desc.height, mip);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = toResourceFormat(texture.desc.format);
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
            ComPtr<ID3D11Texture2D> staging;
            if (FAILED(r.device->d3d->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) {
                return nullptr;
            }

            r.context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture.resource.Get(), subresource, nullptr);
            return staging;
        }

        // Bytes per texel of a depth format's packed copy, and of the plane a footprint names.
        uint32_t depthTexelBytes(RenderFormat format) {
            return (format == RenderFormat::D32_FLOAT_S8_UINT) ? 8 : RenderFormatSize(format);
        }

        uint32_t depthPlaneOffset(RenderFormat format, uint32_t plane) {
            return ((format == RenderFormat::D32_FLOAT_S8_UINT) && (plane == 1)) ? 4 : 0;
        }
    }

    void D3D11CommandList::copyTextureRegion(const RenderTextureCopyLocation &dstLocation, const RenderTextureCopyLocation &srcLocation, uint32_t dstX, uint32_t dstY, uint32_t dstZ, const RenderBox *srcBox) {
        const RenderTextureCopyLocation dst = dstLocation;
        const RenderTextureCopyLocation src = srcLocation;
        const bool hasBox = srcBox != nullptr;
        const RenderBox box = hasBox ? *srcBox : RenderBox();
        commands.emplace_back([=](D3D11Replay &r) {
            r.dirty = true;
            if ((dst.type == RenderTextureCopyType::SUBRESOURCE) && (src.type == RenderTextureCopyType::SUBRESOURCE)) {
                auto *dstTexture = const_cast<D3D11Texture *>(static_cast<const D3D11Texture *>(dst.texture));
                const auto *srcTexture = static_cast<const D3D11Texture *>(src.texture);
                const UINT dstSub = dstTexture->subresource(dst.subresource.mipLevel, dst.subresource.arrayIndex);
                const UINT srcSub = srcTexture->subresource(src.subresource.mipLevel, src.subresource.arrayIndex);
                if (RenderFormatIsDepth(dstTexture->desc.format)) {
                    // Depth-stencil copies are whole subresources.
                    r.context->CopySubresourceRegion(dstTexture->resource.Get(), dstSub, 0, 0, 0, srcTexture->resource.Get(), srcSub, nullptr);
                }
                else {
                    D3D11_BOX nativeBox = { UINT(box.left), UINT(box.top), UINT(box.front), UINT(box.right), UINT(box.bottom), UINT(box.back) };
                    r.context->CopySubresourceRegion(dstTexture->resource.Get(), dstSub, dstX, dstY, dstZ, srcTexture->resource.Get(), srcSub, hasBox ? &nativeBox : nullptr);
                }

                dstTexture->version++;
            }
            else if (dst.type == RenderTextureCopyType::SUBRESOURCE) {
                // Upload: the footprint's bytes go straight from the buffer's memory.
                auto *texture = const_cast<D3D11Texture *>(static_cast<const D3D11Texture *>(dst.texture));
                const auto *buffer = static_cast<const D3D11Buffer *>(src.buffer);
                const auto &footprint = src.placedFootprint;
                const uint32_t rowPitch = blockRowBytes(footprint.format, footprint.rowWidth);
                const uint32_t depthPitch = rowPitch * blockRows(footprint.format, footprint.height);
                const uint8_t *bytes = buffer->data.data() + footprint.offset;
                const uint32_t mip = dst.subresource.mipLevel;
                const UINT subresource = texture->subresource(mip, dst.subresource.arrayIndex);
                if (RenderFormatIsDepth(texture->desc.format)) {
                    // ponytail: a staging round trip per depth upload; fine for CPU-locked surfaces.
                    ComPtr<ID3D11Texture2D> staging = stagingCopy(r, *texture, subresource, mip);
                    D3D11_MAPPED_SUBRESOURCE mapped = {};
                    if ((staging == nullptr) || FAILED(r.context->Map(staging.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mapped))) return;
                    const uint32_t texel = depthTexelBytes(texture->desc.format);
                    const uint32_t planeOffset = depthPlaneOffset(texture->desc.format, dst.subresource.planeIndex);
                    const uint32_t element = RenderFormatSize(footprint.format);
                    const uint32_t width = std::min(footprint.width, mipExtent(texture->desc.width, mip) - std::min(dstX, mipExtent(texture->desc.width, mip)));
                    const uint32_t height = std::min(footprint.height, mipExtent(texture->desc.height, mip) - std::min(dstY, mipExtent(texture->desc.height, mip)));
                    for (uint32_t y = 0; y < height; y++) {
                        uint8_t *row = static_cast<uint8_t *>(mapped.pData) + size_t(dstY + y) * mapped.RowPitch;
                        for (uint32_t x = 0; x < width; x++) {
                            std::memcpy(row + size_t(dstX + x) * texel + planeOffset, bytes + size_t(y) * rowPitch + size_t(x) * element, element);
                        }
                    }

                    r.context->Unmap(staging.Get(), 0);
                    r.context->CopySubresourceRegion(texture->resource.Get(), subresource, 0, 0, 0, staging.Get(), 0, nullptr);
                }
                else {
                    const uint32_t block = RenderFormatBlockWidth(texture->desc.format);
                    const uint32_t mipWidth = mipExtent(texture->desc.width, mip);
                    const uint32_t mipHeight = mipExtent(texture->desc.height, mip);
                    const uint32_t mipDepth = (texture->desc.dimension == RenderTextureDimension::TEXTURE_3D) ? mipExtent(texture->desc.depth, mip) : 1;
                    D3D11_BOX nativeBox;
                    nativeBox.left = dstX;
                    nativeBox.top = dstY;
                    nativeBox.front = dstZ;
                    nativeBox.right = std::min(dstX + (footprint.width + block - 1) / block * block, std::max(mipWidth, dstX + 1));
                    nativeBox.bottom = std::min(dstY + (footprint.height + block - 1) / block * block, std::max(mipHeight, dstY + 1));
                    nativeBox.back = std::min(dstZ + std::max(footprint.depth, 1u), mipDepth);
                    r.context->UpdateSubresource(texture->resource.Get(), subresource, &nativeBox, bytes, rowPitch, depthPitch);
                }

                texture->version++;
            }
            else if (src.type == RenderTextureCopyType::SUBRESOURCE) {
                // Readback: the replay waits for the GPU to finish the source.
                // ponytail: one staging texture per readback, read at once; a fence-time read would not stall.
                auto *texture = const_cast<D3D11Texture *>(static_cast<const D3D11Texture *>(src.texture));
                auto *buffer = const_cast<D3D11Buffer *>(static_cast<const D3D11Buffer *>(dst.buffer));
                const auto &footprint = dst.placedFootprint;
                const uint32_t mip = src.subresource.mipLevel;
                const UINT subresource = texture->subresource(mip, src.subresource.arrayIndex);
                ComPtr<ID3D11Texture2D> staging = stagingCopy(r, *texture, subresource, mip);
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                if ((staging == nullptr) || FAILED(r.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
                const uint32_t rowPitch = blockRowBytes(footprint.format, footprint.rowWidth);
                uint8_t *bytes = buffer->data.data() + footprint.offset;
                const uint32_t left = hasBox ? uint32_t(box.left) : 0;
                const uint32_t top = hasBox ? uint32_t(box.top) : 0;
                const uint32_t width = std::min(footprint.width, mipExtent(texture->desc.width, mip) - std::min(left, mipExtent(texture->desc.width, mip)));
                const uint32_t height = std::min(footprint.height, mipExtent(texture->desc.height, mip) - std::min(top, mipExtent(texture->desc.height, mip)));
                if (RenderFormatIsDepth(texture->desc.format)) {
                    const uint32_t texel = depthTexelBytes(texture->desc.format);
                    const uint32_t planeOffset = depthPlaneOffset(texture->desc.format, src.subresource.planeIndex);
                    const uint32_t element = RenderFormatSize(footprint.format);
                    for (uint32_t y = 0; y < height; y++) {
                        const uint8_t *row = static_cast<const uint8_t *>(mapped.pData) + size_t(top + y) * mapped.RowPitch;
                        for (uint32_t x = 0; x < width; x++) {
                            std::memcpy(bytes + size_t(y) * rowPitch + size_t(x) * element, row + size_t(left + x) * texel + planeOffset, element);
                        }
                    }
                }
                else {
                    const uint32_t rowBytes = blockRowBytes(footprint.format, width);
                    const uint32_t rowOffset = blockRowBytes(footprint.format, left);
                    const uint32_t block = RenderFormatBlockWidth(footprint.format);
                    for (uint32_t y = 0; y < blockRows(footprint.format, height); y++) {
                        std::memcpy(bytes + size_t(y) * rowPitch, static_cast<const uint8_t *>(mapped.pData) + size_t(top / block + y) * mapped.RowPitch + rowOffset, rowBytes);
                    }
                }

                r.context->Unmap(staging.Get(), 0);
                buffer->invalidateMirrors();
            }
        });
    }

    void D3D11CommandList::copyTexture(const RenderTexture *dstTexture, const RenderTexture *srcTexture) {
        auto *dst = const_cast<D3D11Texture *>(static_cast<const D3D11Texture *>(dstTexture));
        const auto *src = static_cast<const D3D11Texture *>(srcTexture);
        commands.emplace_back([=](D3D11Replay &r) {
            r.context->CopyResource(dst->resource.Get(), src->resource.Get());
            dst->version++;
            r.dirty = true;
        });
    }

    void D3D11CommandList::resolveTexture(const RenderTexture *dstTexture, const RenderTexture *srcTexture) {
        auto *dst = const_cast<D3D11Texture *>(static_cast<const D3D11Texture *>(dstTexture));
        const auto *src = static_cast<const D3D11Texture *>(srcTexture);
        commands.emplace_back([=](D3D11Replay &r) {
            r.context->ResolveSubresource(dst->resource.Get(), 0, src->resource.Get(), 0, toDXGI(dst->desc.format));
            dst->version++;
            r.dirty = true;
        });
    }

    void D3D11CommandList::beginQuery(const RenderQueryPool *queryPool, uint32_t queryIndex) {
        const auto *pool = static_cast<const D3D11QueryPool *>(queryPool);
        if (pool->type != RenderQueryType::OCCLUSION) return;
        ID3D11Query *query = pool->queries[queryIndex].Get();
        commands.emplace_back([=](D3D11Replay &r) { r.context->Begin(query); });
    }

    void D3D11CommandList::endQuery(const RenderQueryPool *queryPool, uint32_t queryIndex) {
        const auto *pool = static_cast<const D3D11QueryPool *>(queryPool);
        if (pool->type != RenderQueryType::OCCLUSION) return;
        ID3D11Query *query = pool->queries[queryIndex].Get();
        commands.emplace_back([=](D3D11Replay &r) { r.context->End(query); });
    }

    // D3D11CommandQueue

    std::unique_ptr<RenderCommandList> D3D11CommandQueue::createCommandList() {
        auto list = std::make_unique<D3D11CommandList>();
        list->device = device;
        return list;
    }

    std::unique_ptr<RenderSwapChain> D3D11CommandQueue::createSwapChain(const RenderSwapChainDesc &desc) {
        auto swapChain = std::make_unique<D3D11SwapChain>();
        swapChain->device = device;
        return swapChain->create(desc) ? std::move(swapChain) : nullptr;
    }

    void D3D11CommandQueue::executeCommandLists(const RenderCommandList **commandLists, uint32_t commandListCount, RenderCommandSemaphore **, uint32_t, RenderCommandSemaphore **, uint32_t, RenderCommandFence *signalFence) {
        std::lock_guard<std::recursive_mutex> lock(device->contextMutex);
        D3D11Replay replay;
        replay.device = device;
        replay.context = device->context.Get();
        replay.epoch = ++device->epoch;
        for (uint32_t i = 0; i < commandListCount; i++) {
            for (const auto &command : static_cast<const D3D11CommandList *>(commandLists[i])->commands) {
                command(replay);
            }
        }

        if (signalFence != nullptr) {
            auto *fence = static_cast<D3D11CommandFence *>(signalFence);
            device->context->End(fence->query.Get());
            fence->pending = true;
        }

        device->context->Flush();
        device->drainMessages("execute");
    }

    void D3D11CommandQueue::waitForCommandFence(RenderCommandFence *fence) {
        auto *native = static_cast<D3D11CommandFence *>(fence);
        while (native->pending && !pollCommandFence(fence)) {
            std::this_thread::yield();
        }
    }

    bool D3D11CommandQueue::pollCommandFence(RenderCommandFence *fence) {
        auto *native = static_cast<D3D11CommandFence *>(fence);
        if (!native->pending) {
            return false;
        }

        std::lock_guard<std::recursive_mutex> lock(device->contextMutex);
        BOOL done = FALSE;
        if (device->context->GetData(native->query.Get(), &done, sizeof(done), 0) == S_OK) {
            native->pending = false;
            return true;
        }

        return false;
    }

    void D3D11CommandQueue::waitForIdle() {
        device->waitForGpu();
    }

    // D3D11SwapChain

    bool D3D11SwapChain::create(const RenderSwapChainDesc &desc) {
        window = desc.renderWindow;
        format = (desc.format != RenderFormat::UNKNOWN) ? desc.format : RenderFormat::B8G8R8A8_UNORM;
        RECT rect = {};
        GetClientRect(window, &rect);
        width = uint32_t(std::max<LONG>(rect.right - rect.left, 0));
        height = uint32_t(std::max<LONG>(rect.bottom - rect.top, 0));
        IDXGIFactory1 *factory = device->renderInterface->dxgiFactory.Get();
        ComPtr<IDXGIFactory2> factory2;
        HRESULT res = E_FAIL;
        if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(factory2.GetAddressOf())))) {
            // Flip discard (Windows 10), then flip sequential (Windows 8).
            for (DXGI_SWAP_EFFECT effect : { DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL }) {
                DXGI_SWAP_CHAIN_DESC1 chainDesc = {};
                chainDesc.Width = std::max(width, 1u);
                chainDesc.Height = std::max(height, 1u);
                chainDesc.Format = toDXGI(format);
                chainDesc.SampleDesc.Count = 1;
                chainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                chainDesc.BufferCount = std::max(desc.textureCount, 2u);
                chainDesc.SwapEffect = effect;
                chainDesc.Flags = device->renderInterface->allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
                ComPtr<IDXGISwapChain1> chain;
                res = factory2->CreateSwapChainForHwnd(device->d3d.Get(), window, &chainDesc, nullptr, nullptr, chain.GetAddressOf());
                if (SUCCEEDED(res)) {
                    swapChain = chain;
                    flags = chainDesc.Flags;
                    flipModel = true;
                    break;
                }
            }
        }

        if (FAILED(res)) {
            // Windows 7: the blit model.
            DXGI_SWAP_CHAIN_DESC chainDesc = {};
            chainDesc.BufferDesc.Width = std::max(width, 1u);
            chainDesc.BufferDesc.Height = std::max(height, 1u);
            chainDesc.BufferDesc.Format = toDXGI(format);
            chainDesc.SampleDesc.Count = 1;
            chainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            chainDesc.BufferCount = 1;
            chainDesc.OutputWindow = window;
            chainDesc.Windowed = TRUE;
            chainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            res = factory->CreateSwapChain(device->d3d.Get(), &chainDesc, swapChain.GetAddressOf());
            if (FAILED(res)) {
                fprintf(stderr, "D3D11: swap chain creation failed with error code 0x%lX.\n", res);
                return false;
            }
        }

        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        if (desc.maxFrameLatency > 0) {
            ComPtr<IDXGIDevice1> dxgiDevice;
            if (SUCCEEDED(device->d3d.As(&dxgiDevice))) dxgiDevice->SetMaximumFrameLatency(desc.maxFrameLatency);
        }

        acquireBuffer();
        return true;
    }

    void D3D11SwapChain::acquireBuffer() {
        texture = std::make_unique<D3D11Texture>(device, RenderTextureDesc::ColorTarget(std::max(width, 1u), std::max(height, 1u), format));
        ComPtr<ID3D11Texture2D> buffer;
        swapChain->GetBuffer(0, IID_PPV_ARGS(buffer.GetAddressOf()));
        texture->resource = buffer;
    }

    bool D3D11SwapChain::present(uint32_t, RenderCommandSemaphore **, uint32_t) {
        std::lock_guard<std::recursive_mutex> lock(device->contextMutex);
        const UINT presentFlags = (!vsync && (flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        const HRESULT res = swapChain->Present(vsync ? 1 : 0, presentFlags);
        if (FAILED(res)) {
            fprintf(stderr, "D3D11: Present failed with error code 0x%lX.\n", res);
            return false;
        }

        // A flip-model present unbinds the back buffer from the pipeline.
        device->context->OMSetRenderTargets(0, nullptr, nullptr);
        return true;
    }

    bool D3D11SwapChain::needsResize() const {
        RECT rect = {};
        GetClientRect(window, &rect);
        return (uint32_t(std::max<LONG>(rect.right - rect.left, 0)) != width) || (uint32_t(std::max<LONG>(rect.bottom - rect.top, 0)) != height);
    }

    bool D3D11SwapChain::resize() {
        RECT rect = {};
        GetClientRect(window, &rect);
        const uint32_t newWidth = uint32_t(std::max<LONG>(rect.right - rect.left, 0));
        const uint32_t newHeight = uint32_t(std::max<LONG>(rect.bottom - rect.top, 0));
        if ((newWidth == 0) || (newHeight == 0)) {
            width = newWidth;
            height = newHeight;
            return true;
        }

        std::lock_guard<std::recursive_mutex> lock(device->contextMutex);
        // Every reference to the old buffer must be gone, the pipeline's included.
        device->context->OMSetRenderTargets(0, nullptr, nullptr);
        device->context->ClearState();
        device->context->Flush();
        texture->resource.Reset();
        texture->rtvs.clear();
        const HRESULT res = swapChain->ResizeBuffers(0, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, flags);
        if (FAILED(res)) {
            fprintf(stderr, "D3D11: ResizeBuffers failed with error code 0x%lX.\n", res);
            return false;
        }

        width = newWidth;
        height = newHeight;
        ComPtr<ID3D11Texture2D> buffer;
        swapChain->GetBuffer(0, IID_PPV_ARGS(buffer.GetAddressOf()));
        texture->resource = buffer;
        texture->desc.width = width;
        texture->desc.height = height;
        return true;
    }

    // D3D11Device

    D3D11Device::D3D11Device(D3D11Interface *renderInterface) {
        this->renderInterface = renderInterface;

        // The hardware adapter with the most dedicated memory.
        ComPtr<IDXGIAdapter1> option;
        for (UINT index = 0; renderInterface->dxgiFactory->EnumAdapters1(index, option.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; index++) {
            DXGI_ADAPTER_DESC1 adapterDesc;
            option->GetDesc1(&adapterDesc);
            if (adapterDesc.Flags & (DXGI_ADAPTER_FLAG_REMOTE | DXGI_ADAPTER_FLAG_SOFTWARE)) {
                continue;
            }

            const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
            const UINT flags = renderInterface->debugLayer ? D3D11_CREATE_DEVICE_DEBUG : 0;
            ComPtr<ID3D11Device> deviceOption;
            ComPtr<ID3D11DeviceContext> contextOption;
            HRESULT res = D3D11CreateDevice(option.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, 2, D3D11_SDK_VERSION, deviceOption.GetAddressOf(), nullptr, contextOption.GetAddressOf());
            if (res == E_INVALIDARG) {
                // Runtimes before D3D11.1 reject the 11_1 level.
                res = D3D11CreateDevice(option.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels + 1, 1, D3D11_SDK_VERSION, deviceOption.GetAddressOf(), nullptr, contextOption.GetAddressOf());
            }

            if (FAILED(res)) {
                continue;
            }

            if ((d3d == nullptr) || (adapterDesc.DedicatedVideoMemory > description.dedicatedVideoMemory)) {
                adapter = option;
                d3d = deviceOption;
                context = contextOption;
                description.name = Utf16ToUtf8(adapterDesc.Description);
                description.dedicatedVideoMemory = adapterDesc.DedicatedVideoMemory;
                description.vendor = RenderDeviceVendor(adapterDesc.VendorId);
                LARGE_INTEGER version = {};
                if (SUCCEEDED(option->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version))) {
                    description.driverVersion = version.QuadPart;
                }
            }
        }

        if (d3d == nullptr) {
            fprintf(stderr, "Unable to create a D3D11 device with Feature Level 11_0.\n");
            return;
        }

        if (SUCCEEDED(d3d.As(&d3d1))) {
            D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
            if (SUCCEEDED(d3d->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)))) {
                logicOps = options.OutputMergerLogicOp;
            }
        }

        if (renderInterface->debugLayer) {
            d3d.As(&infoQueue);
        }

        capabilities.geometryShader = true;
        capabilities.queryOutsideRenderPass = true;
        capabilities.maxTextureSize = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        capabilities.queryPools = true;

        d3d->CreateVertexShader(D3D11FullscreenVSBytes, sizeof(D3D11FullscreenVSBytes), nullptr, fullscreenVS.GetAddressOf());
        d3d->CreatePixelShader(D3D11ClearPSBytes, sizeof(D3D11ClearPSBytes), nullptr, clearPS.GetAddressOf());
        d3d->CreatePixelShader(D3D11SwizzlePSBytes, sizeof(D3D11SwizzlePSBytes), nullptr, swizzlePS.GetAddressOf());
        D3D11_BUFFER_DESC constantsDesc = {};
        constantsDesc.ByteWidth = sizeof(InternalConstants);
        constantsDesc.Usage = D3D11_USAGE_DEFAULT;
        constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        d3d->CreateBuffer(&constantsDesc, nullptr, internalConstants.GetAddressOf());
        D3D11_RASTERIZER_DESC rasterizerDesc = {};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = D3D11_CULL_NONE;
        rasterizerDesc.DepthClipEnable = TRUE;
        rasterizerDesc.ScissorEnable = TRUE;
        d3d->CreateRasterizerState(&rasterizerDesc, internalRasterizer.GetAddressOf());
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        d3d->CreateBlendState(&blendDesc, internalBlend.GetAddressOf());
        for (uint32_t i = 0; i < 4; i++) {
            D3D11_DEPTH_STENCIL_DESC depthDesc = {};
            depthDesc.DepthEnable = (i & 1) ? TRUE : FALSE;
            depthDesc.DepthWriteMask = (i & 1) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
            depthDesc.StencilEnable = (i & 2) ? TRUE : FALSE;
            depthDesc.StencilReadMask = 0xFF;
            depthDesc.StencilWriteMask = 0xFF;
            depthDesc.FrontFace = { D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS };
            depthDesc.BackFace = depthDesc.FrontFace;
            d3d->CreateDepthStencilState(&depthDesc, internalDepthStencil[i].GetAddressOf());
        }
    }

    void D3D11Device::drainMessages(const char *where) {
        if (infoQueue == nullptr) {
            return;
        }

        const UINT64 count = infoQueue->GetNumStoredMessages();
        for (UINT64 i = 0; i < count; i++) {
            SIZE_T length = 0;
            infoQueue->GetMessage(i, nullptr, &length);
            std::vector<uint8_t> storage(length);
            auto *message = reinterpret_cast<D3D11_MESSAGE *>(storage.data());
            if (SUCCEEDED(infoQueue->GetMessage(i, message, &length)) && (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)) {
                fprintf(stderr, "[D3D11] %s: id=%u %s\n", where, message->ID, message->pDescription);
            }
        }

        infoQueue->ClearStoredMessages();
    }

    void D3D11Device::internalPass(InternalConstants internal, ID3D11RenderTargetView *target, ID3D11DepthStencilView *depth,
        const D3D11_VIEWPORT &viewport, const D3D11_RECT &scissor, ID3D11PixelShader *pixelShader,
        ID3D11ShaderResourceView *source, uint32_t depthStencilIndex, uint32_t stencilReference)
    {
        // Targets first: a source still bound as a target cannot be read.
        context->OMSetRenderTargets(target ? 1 : 0, &target, depth);
        context->UpdateSubresource(internalConstants.Get(), 0, nullptr, &internal, 0, 0);
        ID3D11Buffer *buffer = internalConstants.Get();
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(fullscreenVS.Get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &buffer);
        context->GSSetShader(nullptr, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &buffer);
        context->PSSetShaderResources(0, 1, &source);
        context->OMSetBlendState(internalBlend.Get(), nullptr, 0xFFFFFFFF);
        context->OMSetDepthStencilState(internalDepthStencil[depthStencilIndex].Get(), stencilReference);
        context->RSSetState(internalRasterizer.Get());
        context->RSSetViewports(1, &viewport);
        context->RSSetScissorRects(1, &scissor);
        context->Draw(3, 0);
        ID3D11ShaderResourceView *none = nullptr;
        context->PSSetShaderResources(0, 1, &none);
        // The pass took constant slot 0 from the draws; the next one binds it again.
        constants[0].source = nullptr;
    }

    void D3D11Device::waitForGpu() {
        D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
        ComPtr<ID3D11Query> query;
        {
            std::lock_guard<std::recursive_mutex> lock(contextMutex);
            if (FAILED(d3d->CreateQuery(&queryDesc, query.GetAddressOf()))) return;
            context->End(query.Get());
            context->Flush();
        }

        for (;;) {
            std::lock_guard<std::recursive_mutex> lock(contextMutex);
            BOOL done = FALSE;
            if (context->GetData(query.Get(), &done, sizeof(done), 0) != S_FALSE) break;
            std::this_thread::yield();
        }
    }

    std::unique_ptr<RenderDescriptorSet> D3D11Device::createDescriptorSet(const RenderDescriptorSetDesc &desc) {
        auto set = std::make_unique<D3D11DescriptorSet>();
        for (uint32_t i = 0; i < desc.descriptorRangesCount; i++) {
            const RenderDescriptorRange &range = desc.descriptorRanges[i];
            const bool boundless = desc.lastRangeIsBoundless && (i + 1 == desc.descriptorRangesCount);
            const uint32_t count = boundless ? desc.boundlessRangeSize : range.count;
            for (uint32_t j = 0; j < count; j++) {
                D3D11Descriptor descriptor;
                descriptor.type = range.type;
                descriptor.binding = range.binding + j;
                if (range.immutableSampler != nullptr) {
                    descriptor.sampler = static_cast<const D3D11Sampler *>(range.immutableSampler[j])->d3d.Get();
                }

                set->descriptors.emplace_back(descriptor);
            }
        }

        return set;
    }

    std::unique_ptr<RenderShader> D3D11Device::createShader(const void *data, uint64_t size, const char *, RenderShaderFormat format) {
        if (format != RenderShaderFormat::DXBC) {
            fprintf(stderr, "D3D11: shaders must be DXBC.\n");
            return nullptr;
        }

        auto shader = std::make_unique<D3D11Shader>();
        shader->device = this;
        const auto *bytes = static_cast<const uint8_t *>(data);
        shader->bytecode.assign(bytes, bytes + size);
        return shader;
    }

    std::unique_ptr<RenderSampler> D3D11Device::createSampler(const RenderSamplerDesc &desc) {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = toFilter(desc);
        samplerDesc.AddressU = toD3D11(desc.addressU);
        samplerDesc.AddressV = toD3D11(desc.addressV);
        samplerDesc.AddressW = toD3D11(desc.addressW);
        samplerDesc.MipLODBias = desc.mipLODBias;
        samplerDesc.MaxAnisotropy = std::clamp(desc.maxAnisotropy, 1u, 16u);
        samplerDesc.ComparisonFunc = desc.comparisonEnabled ? toD3D11(desc.comparisonFunc) : D3D11_COMPARISON_NEVER;
        const float alpha = (desc.borderColor == RenderBorderColor::TRANSPARENT_BLACK) ? 0.0f : 1.0f;
        const float white = (desc.borderColor == RenderBorderColor::OPAQUE_WHITE) ? 1.0f : 0.0f;
        samplerDesc.BorderColor[0] = white;
        samplerDesc.BorderColor[1] = white;
        samplerDesc.BorderColor[2] = white;
        samplerDesc.BorderColor[3] = alpha;
        samplerDesc.MinLOD = desc.minLOD;
        samplerDesc.MaxLOD = desc.maxLOD;
        auto sampler = std::make_unique<D3D11Sampler>();
        const HRESULT res = d3d->CreateSamplerState(&samplerDesc, sampler->d3d.GetAddressOf());
        if (FAILED(res)) {
            fprintf(stderr, "D3D11: sampler creation failed with error code 0x%lX.\n", res);
            return nullptr;
        }

        return sampler;
    }

    std::unique_ptr<RenderPipeline> D3D11Device::createGraphicsPipeline(const RenderGraphicsPipelineDesc &desc) {
        auto pipeline = std::make_unique<D3D11GraphicsPipeline>();
        auto *vertexShader = const_cast<D3D11Shader *>(static_cast<const D3D11Shader *>(desc.vertexShader));
        auto *geometryShader = const_cast<D3D11Shader *>(static_cast<const D3D11Shader *>(desc.geometryShader));
        auto *pixelShader = const_cast<D3D11Shader *>(static_cast<const D3D11Shader *>(desc.pixelShader));
        if (vertexShader == nullptr) {
            return nullptr;
        }

        pipeline->vs = vertexShader->vertex();
        pipeline->gs = geometryShader ? geometryShader->geometry() : nullptr;
        pipeline->ps = pixelShader ? pixelShader->pixel() : nullptr;
        if ((pipeline->vs == nullptr) || (geometryShader && !pipeline->gs) || (pixelShader && !pipeline->ps)) {
            fprintf(stderr, "D3D11: shader creation failed.\n");
            drainMessages("shader");
            return nullptr;
        }

        HRESULT res;
        if (desc.inputElementsCount > 0) {
            std::vector<D3D11_INPUT_ELEMENT_DESC> elements(desc.inputElementsCount);
            for (uint32_t i = 0; i < desc.inputElementsCount; i++) {
                const RenderInputElement &element = desc.inputElements[i];
                bool instanced = false;
                for (uint32_t j = 0; j < desc.inputSlotsCount; j++) {
                    if (desc.inputSlots[j].index == element.slotIndex) {
                        instanced = desc.inputSlots[j].classification == RenderInputSlotClassification::PER_INSTANCE_DATA;
                    }
                }

                elements[i].SemanticName = element.semanticName;
                elements[i].SemanticIndex = element.semanticIndex;
                elements[i].Format = toDXGI(element.format);
                elements[i].InputSlot = element.slotIndex;
                elements[i].AlignedByteOffset = element.alignedByteOffset;
                elements[i].InputSlotClass = instanced ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
                elements[i].InstanceDataStepRate = instanced ? 1 : 0;
            }

            res = d3d->CreateInputLayout(elements.data(), UINT(elements.size()), vertexShader->bytecode.data(), vertexShader->bytecode.size(), pipeline->inputLayout.GetAddressOf());
            if (FAILED(res)) {
                fprintf(stderr, "D3D11: input layout creation failed with error code 0x%lX.\n", res);
                drainMessages("input layout");
                return nullptr;
            }
        }

        if (desc.logicOpEnabled && logicOps && (d3d1 != nullptr)) {
            D3D11_BLEND_DESC1 blendDesc = {};
            blendDesc.AlphaToCoverageEnable = desc.alphaToCoverageEnabled;
            blendDesc.IndependentBlendEnable = TRUE;
            for (uint32_t i = 0; i < std::max(desc.renderTargetCount, 1u); i++) {
                blendDesc.RenderTarget[i].LogicOpEnable = TRUE;
                blendDesc.RenderTarget[i].LogicOp = toD3D11(desc.logicOp);
                blendDesc.RenderTarget[i].RenderTargetWriteMask = desc.renderTargetBlend[i].renderTargetWriteMask;
            }

            ComPtr<ID3D11BlendState1> blend;
            res = d3d1->CreateBlendState1(&blendDesc, blend.GetAddressOf());
            pipeline->blend = blend;
        }
        else {
            // ponytail: without D3D11.1 logic ops a logic-op pipeline draws as a plain copy.
            D3D11_BLEND_DESC blendDesc = {};
            blendDesc.AlphaToCoverageEnable = desc.alphaToCoverageEnabled;
            blendDesc.IndependentBlendEnable = TRUE;
            for (uint32_t i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
                const RenderBlendDesc &blend = desc.renderTargetBlend[i];
                D3D11_RENDER_TARGET_BLEND_DESC &target = blendDesc.RenderTarget[i];
                target.BlendEnable = blend.blendEnabled && !desc.logicOpEnabled;
                target.SrcBlend = toD3D11(blend.srcBlend);
                target.DestBlend = toD3D11(blend.dstBlend);
                target.BlendOp = toD3D11(blend.blendOp);
                target.SrcBlendAlpha = toD3D11(blend.srcBlendAlpha);
                target.DestBlendAlpha = toD3D11(blend.dstBlendAlpha);
                target.BlendOpAlpha = toD3D11(blend.blendOpAlpha);
                target.RenderTargetWriteMask = (i < desc.renderTargetCount) ? blend.renderTargetWriteMask : D3D11_COLOR_WRITE_ENABLE_ALL;
            }

            res = d3d->CreateBlendState(&blendDesc, pipeline->blend.GetAddressOf());
        }

        if (FAILED(res)) {
            fprintf(stderr, "D3D11: blend state creation failed with error code 0x%lX.\n", res);
            return nullptr;
        }

        D3D11_RASTERIZER_DESC rasterizerDesc = {};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = (desc.cullMode == RenderCullMode::FRONT) ? D3D11_CULL_FRONT : (desc.cullMode == RenderCullMode::BACK) ? D3D11_CULL_BACK : D3D11_CULL_NONE;
        rasterizerDesc.FrontCounterClockwise = (desc.frontFace == RenderFrontFace::COUNTER_CLOCKWISE);
        rasterizerDesc.DepthBias = desc.depthBias;
        rasterizerDesc.DepthBiasClamp = desc.depthBiasClamp;
        rasterizerDesc.SlopeScaledDepthBias = desc.slopeScaledDepthBias;
        rasterizerDesc.DepthClipEnable = desc.depthClipEnabled;
        rasterizerDesc.ScissorEnable = TRUE;
        rasterizerDesc.MultisampleEnable = desc.multisampling.sampleCount > 1;
        res = d3d->CreateRasterizerState(&rasterizerDesc, pipeline->rasterizer.GetAddressOf());
        if (FAILED(res)) {
            fprintf(stderr, "D3D11: rasterizer state creation failed with error code 0x%lX.\n", res);
            return nullptr;
        }

        D3D11_DEPTH_STENCIL_DESC depthDesc = {};
        depthDesc.DepthEnable = desc.depthEnabled;
        depthDesc.DepthWriteMask = desc.depthWriteEnabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.DepthFunc = toD3D11(desc.depthFunction);
        depthDesc.StencilEnable = desc.stencilEnabled;
        depthDesc.StencilReadMask = UINT8(desc.stencilReadMask);
        depthDesc.StencilWriteMask = UINT8(desc.stencilWriteMask);
        auto face = [](const RenderStencilFaceDesc &stencil) {
            return D3D11_DEPTH_STENCILOP_DESC{ toD3D11(stencil.failOp), toD3D11(stencil.depthFailOp), toD3D11(stencil.passOp), toD3D11(stencil.compareFunction) };
        };
        depthDesc.FrontFace = face(desc.stencilFrontFace);
        depthDesc.BackFace = face(desc.stencilBackFace);
        res = d3d->CreateDepthStencilState(&depthDesc, pipeline->depthStencil.GetAddressOf());
        if (FAILED(res)) {
            fprintf(stderr, "D3D11: depth stencil state creation failed with error code 0x%lX.\n", res);
            return nullptr;
        }

        pipeline->topology = toD3D11(desc.primitiveTopology);
        pipeline->stencilReference = desc.stencilReference;
        return pipeline;
    }

    std::unique_ptr<RenderCommandQueue> D3D11Device::createCommandQueue(RenderCommandListType) {
        auto queue = std::make_unique<D3D11CommandQueue>();
        queue->device = this;
        return queue;
    }

    std::unique_ptr<RenderTexture> D3D11Device::createTexture(const RenderTextureDesc &desc) {
        auto texture = std::make_unique<D3D11Texture>(this, desc);
        return texture->create() ? std::move(texture) : nullptr;
    }

    std::unique_ptr<RenderPool> D3D11Device::createPool(const RenderPoolDesc &) {
        auto pool = std::make_unique<D3D11Pool>();
        pool->device = this;
        return pool;
    }

    std::unique_ptr<RenderPipelineLayout> D3D11Device::createPipelineLayout(const RenderPipelineLayoutDesc &desc) {
        auto layout = std::make_unique<D3D11PipelineLayout>();
        layout->pushConstantRanges.assign(desc.pushConstantRanges, desc.pushConstantRanges + desc.pushConstantRangesCount);
        return layout;
    }

    std::unique_ptr<RenderCommandFence> D3D11Device::createCommandFence() {
        auto fence = std::make_unique<D3D11CommandFence>();
        D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
        if (FAILED(d3d->CreateQuery(&queryDesc, fence->query.GetAddressOf()))) {
            return nullptr;
        }

        return fence;
    }

    std::unique_ptr<RenderFramebuffer> D3D11Device::createFramebuffer(const RenderFramebufferDesc &desc) {
        auto framebuffer = std::make_unique<D3D11Framebuffer>();
        for (uint32_t i = 0; i < desc.colorAttachmentsCount; i++) {
            auto *texture = const_cast<D3D11Texture *>(static_cast<const D3D11Texture *>(desc.colorAttachments[i]));
            framebuffer->colors.emplace_back(texture);
            framebuffer->colorViews.emplace_back(desc.colorAttachmentViews ? static_cast<const D3D11TextureView *>(desc.colorAttachmentViews[i]) : nullptr);
            framebuffer->width = texture->desc.width;
            framebuffer->height = texture->desc.height;
        }

        if (desc.depthAttachment != nullptr) {
            framebuffer->depth = const_cast<D3D11Texture *>(static_cast<const D3D11Texture *>(desc.depthAttachment));
            framebuffer->depthReadOnly = desc.depthAttachmentReadOnly;
            framebuffer->width = framebuffer->depth->desc.width;
            framebuffer->height = framebuffer->depth->desc.height;
        }

        return framebuffer;
    }

    std::unique_ptr<RenderQueryPool> D3D11Device::createQueryPool(uint32_t queryCount, RenderQueryType type) {
        auto pool = std::make_unique<D3D11QueryPool>();
        pool->device = this;
        pool->type = type;
        pool->results.resize(queryCount);
        if (type == RenderQueryType::OCCLUSION) {
            pool->queries.resize(queryCount);
            D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_OCCLUSION, 0 };
            for (auto &query : pool->queries) {
                if (FAILED(d3d->CreateQuery(&queryDesc, query.GetAddressOf()))) return nullptr;
            }
        }

        return pool;
    }

    RenderSampleCounts D3D11Device::getSampleCountsSupported(RenderFormat format) const {
        RenderSampleCounts counts = RenderSampleCount::COUNT_1;
        for (UINT count : { 2u, 4u, 8u }) {
            UINT quality = 0;
            if (SUCCEEDED(d3d->CheckMultisampleQualityLevels(toDXGI(format), count, &quality)) && (quality > 0)) {
                counts |= count;
            }
        }

        return counts;
    }

    // D3D11Interface

    D3D11Interface::D3D11Interface() {
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf())))) {
            return;
        }

        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(dxgiFactory.As(&factory5))) {
            BOOL tearing = FALSE;
            allowTearing = SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing;
        }

        capabilities.shaderFormat = RenderShaderFormat::DXBC;
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT index = 0; dxgiFactory->EnumAdapters1(index, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; index++) {
            DXGI_ADAPTER_DESC1 adapterDesc;
            adapter->GetDesc1(&adapterDesc);
            if ((adapterDesc.Flags & (DXGI_ADAPTER_FLAG_REMOTE | DXGI_ADAPTER_FLAG_SOFTWARE)) == 0) {
                deviceNames.emplace_back(Utf16ToUtf8(adapterDesc.Description));
            }
        }
    }

    std::unique_ptr<RenderDevice> D3D11Interface::createDevice(const std::string &) {
        auto device = std::make_unique<D3D11Device>(this);
        return device->isValid() ? std::move(device) : nullptr;
    }

    // Global creation function.

    std::unique_ptr<RenderInterface> CreateD3D11Interface(bool debugLayer) {
        auto created = std::make_unique<D3D11Interface>();
        if (created->dxgiFactory == nullptr) {
            return nullptr;
        }

        created->debugLayer = debugLayer;
        return created;
    }

    std::unique_ptr<RenderInterface> CreateD3D11Interface() {
        return CreateD3D11Interface(false);
    }
};
