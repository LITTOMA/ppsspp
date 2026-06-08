#include "3DS/GraphicsContext3DS.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "Common/GPU/thin3d.h"
#include "Common/GPU/DataFormat.h"
#include "Common/Log.h"
#include "3DS/GPU3DS.h"
#include "Platform3DS.h"

namespace {

static constexpr int TOP_W = 400;
static constexpr int TOP_H = 240;
static constexpr int MIN_TEX_SIZE = 8;

static void Log3DSGPUStats(const char *line) {
	mkdir("sdmc:/3ds", 0755);
	mkdir("sdmc:/3ds/PPSSPP", 0755);
	int fd = open("sdmc:/3ds/PPSSPP/gpu.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0) {
		return;
	}
	write(fd, line, strlen(line));
	write(fd, "\n", 1);
	close(fd);
}

struct Vertex2D {
	float x;
	float y;
	float z;
	float u;
	float v;
	uint32_t rgba;
};

static int NextPow2(int value) {
	int out = MIN_TEX_SIZE;
	while (out < value) {
		out <<= 1;
	}
	return out;
}

static int Morton8(int x, int y) {
	return (x & 1) | ((y & 1) << 1) |
		((x & 2) << 1) | ((y & 2) << 2) |
		((x & 4) << 2) | ((y & 4) << 3);
}

static int TiledTexOffset(int x, int y, int width) {
	const int tileX = x >> 3;
	const int tileY = y >> 3;
	const int tilesPerRow = width >> 3;
	return ((tileY * tilesPerRow + tileX) << 6) + Morton8(x & 7, y & 7);
}

class ShaderModule3DS : public Draw::ShaderModule {
public:
	ShaderModule3DS(ShaderStage stage, const uint8_t *data, size_t dataSize) : stage_(stage) {
		if (data && dataSize) {
			source_.assign(reinterpret_cast<const char *>(data), dataSize);
		}
	}

	ShaderStage GetStage() const override {
		return stage_;
	}

	bool UsesTexture() const {
		return source_.find("tex") != std::string::npos || source_.find("texture") != std::string::npos;
	}

private:
	ShaderStage stage_;
	std::string source_;
};

class State3DS : public Draw::BlendState {
};

class SamplerState3DS : public Draw::SamplerState {
};

class DepthStencilState3DS : public Draw::DepthStencilState {
};

class RasterState3DS : public Draw::RasterState {
};

class InputLayout3DS : public Draw::InputLayout {
};

class Buffer3DS : public Draw::Buffer {
public:
	explicit Buffer3DS(size_t size) : data_(size) {
	}

	void Update(const uint8_t *data, size_t offset, size_t size) {
		if (offset + size > data_.size()) {
			data_.resize(offset + size);
		}
		memcpy(data_.data() + offset, data, size);
	}

	const uint8_t *Data() const {
		return data_.data();
	}

private:
	std::vector<uint8_t> data_;
};

class Framebuffer3DS : public Draw::Framebuffer {
public:
	Framebuffer3DS(int width, int height, int layers, int msaa, const char *tag) : tag_(tag ? tag : "3ds_fbo") {
		width_ = width;
		height_ = height;
		layers_ = layers;
		multiSampleLevel_ = msaa;
		pixels_.resize(width * height, 0xFF000000);
	}

	uint32_t *Pixels() {
		return pixels_.data();
	}

	const char *Tag() const override {
		return tag_.c_str();
	}

private:
	std::string tag_;
	std::vector<uint32_t> pixels_;
};

class Texture3DS : public Draw::Texture {
public:
	explicit Texture3DS(const Draw::TextureDesc &desc) : swizzle_(desc.swizzle) {
		width_ = desc.width;
		height_ = desc.height;
		depth_ = desc.depth;
		format_ = desc.format;
		Update(desc.initData.empty() ? nullptr : desc.initData.data(), desc.initDataCallback, std::max(1, (int)desc.initData.size()));
	}

	~Texture3DS() override {
		P3DS_GPU_DestroyTexture(gpuTexture_);
	}

	void Update(const uint8_t *const *data, Draw::TextureCallback initDataCallback, int numLevels) {
		const size_t bpp = Draw::DataFormatSizeInBytes(format_);
		if (width_ <= 0 || height_ <= 0 || bpp == 0) {
			return;
		}

		data_.assign(width_ * height_ * bpp, 0);
		const uint32_t byteStride = (uint32_t)(width_ * bpp);
		if (initDataCallback) {
			const uint8_t *init = data && numLevels > 0 ? data[0] : nullptr;
			initDataCallback(data_.data(), init, width_, height_, std::max(1, depth_), byteStride, byteStride * height_);
		} else if (data && numLevels > 0 && data[0]) {
			memcpy(data_.data(), data[0], data_.size());
		}
		UploadGPUTexture();
	}

	uint32_t Sample(float u, float v) const {
		if (data_.empty() || width_ <= 0 || height_ <= 0) {
			return 0xFFFFFFFF;
		}

		u = std::clamp(u, 0.0f, 1.0f);
		v = std::clamp(v, 0.0f, 1.0f);
		const int x = std::min(width_ - 1, std::max(0, (int)floorf(u * (float)width_)));
		const int y = std::min(height_ - 1, std::max(0, (int)floorf(v * (float)height_)));

		if (format_ == Draw::DataFormat::R8_UNORM) {
			const uint8_t value = data_[y * width_ + x];
			if (swizzle_ == Draw::TextureSwizzle::R8_AS_ALPHA || swizzle_ == Draw::TextureSwizzle::R8_AS_PREMUL_ALPHA) {
				return 0x00FFFFFF | ((uint32_t)value << 24);
			}
			return 0xFF000000 | ((uint32_t)value << 16) | ((uint32_t)value << 8) | value;
		}

		const uint8_t *p = data_.data() + (y * width_ + x) * Draw::DataFormatSizeInBytes(format_);
		switch (format_) {
		case Draw::DataFormat::R8G8B8A8_UNORM:
		case Draw::DataFormat::R8G8B8A8_UNORM_SRGB:
			return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
		case Draw::DataFormat::B8G8R8A8_UNORM:
		case Draw::DataFormat::B8G8R8A8_UNORM_SRGB:
			return (uint32_t)p[2] | ((uint32_t)p[1] << 8) | ((uint32_t)p[0] << 16) | ((uint32_t)p[3] << 24);
		case Draw::DataFormat::R8G8B8_UNORM:
			return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | 0xFF000000;
		default:
			return 0xFFFFFFFF;
		}
	}

	bool Ready() const {
		return texReady_;
	}

	P3DS_GPUTexture *Tex() {
		return texReady_ ? gpuTexture_ : nullptr;
	}

	int TexWidth() const {
		return texW_;
	}

	int TexHeight() const {
		return texH_;
	}

	bool IsR8() const {
		return format_ == Draw::DataFormat::R8_UNORM;
	}

	Draw::DataFormat Format() const {
		return format_;
	}

	Draw::TextureSwizzle Swizzle() const {
		return swizzle_;
	}

	int Width() const {
		return width_;
	}

	int Height() const {
		return height_;
	}

private:
	uint32_t SourcePixel(int x, int y) const {
		if (x < 0 || y < 0 || x >= width_ || y >= height_ || data_.empty()) {
			return 0;
		}

		if (format_ == Draw::DataFormat::R8_UNORM) {
			const uint8_t value = data_[y * width_ + x];
			if (swizzle_ == Draw::TextureSwizzle::R8_AS_ALPHA || swizzle_ == Draw::TextureSwizzle::R8_AS_PREMUL_ALPHA) {
				return 0x00FFFFFF | ((uint32_t)value << 24);
			}
			return 0xFF000000 | ((uint32_t)value << 16) | ((uint32_t)value << 8) | value;
		}

		const uint8_t *p = data_.data() + (y * width_ + x) * Draw::DataFormatSizeInBytes(format_);
		switch (format_) {
		case Draw::DataFormat::R8G8B8A8_UNORM:
		case Draw::DataFormat::R8G8B8A8_UNORM_SRGB:
			return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
		case Draw::DataFormat::B8G8R8A8_UNORM:
		case Draw::DataFormat::B8G8R8A8_UNORM_SRGB:
			return (uint32_t)p[2] | ((uint32_t)p[1] << 8) | ((uint32_t)p[0] << 16) | ((uint32_t)p[3] << 24);
		case Draw::DataFormat::R8G8B8_UNORM:
			return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | 0xFF000000;
		default:
			return 0xFFFFFFFF;
		}
	}

	void UploadGPUTexture() {
		P3DS_GPU_DestroyTexture(gpuTexture_);
		gpuTexture_ = nullptr;
		texReady_ = false;

		texW_ = NextPow2(width_);
		texH_ = NextPow2(height_);
		if (texW_ > 2048 || texH_ > 2048) {
			return;
		}

		std::vector<uint8_t> tiled((size_t)texW_ * (size_t)texH_ * 4, 0);
		for (int y = 0; y < texH_; ++y) {
			for (int x = 0; x < texW_; ++x) {
				const uint32_t rgba = SourcePixel(x, y);
				uint8_t *dst = tiled.data() + TiledTexOffset(x, y, texW_) * 4;
				dst[0] = (uint8_t)((rgba >> 24) & 0xFF);
				dst[1] = (uint8_t)((rgba >> 16) & 0xFF);
				dst[2] = (uint8_t)((rgba >> 8) & 0xFF);
				dst[3] = (uint8_t)(rgba & 0xFF);
			}
		}
		gpuTexture_ = P3DS_GPU_CreateTexture(width_, height_, texW_, texH_, tiled.data());
		texReady_ = gpuTexture_ != nullptr;
	}

	Draw::TextureSwizzle swizzle_;
	std::vector<uint8_t> data_;
	P3DS_GPUTexture *gpuTexture_ = nullptr;
	bool texReady_ = false;
	int texW_ = 0;
	int texH_ = 0;
};

class Pipeline3DS : public Draw::Pipeline {
public:
	explicit Pipeline3DS(const Draw::PipelineDesc &desc, const char *tag) : tag_(tag ? tag : "3ds_pipe") {
		(void)desc;
	}

private:
	std::string tag_;
};

static uint8_t Chan(uint32_t c, int shift) {
	return (uint8_t)((c >> shift) & 0xFF);
}

static uint32_t PremulModulate(uint32_t tex, uint32_t vert, bool textured) {
	const int va = Chan(vert, 24);
	const int vr = Chan(vert, 0);
	const int vg = Chan(vert, 8);
	const int vb = Chan(vert, 16);

	if (!textured) {
		const int r = vr * va / 255;
		const int g = vg * va / 255;
		const int b = vb * va / 255;
		return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)va << 24);
	}

	const int ta = Chan(tex, 24);
	const int tr = Chan(tex, 0);
	const int tg = Chan(tex, 8);
	const int tb = Chan(tex, 16);
	const int a = ta * va / 255;
	const int r = tr * vr * va / (255 * 255);
	const int g = tg * vg * va / (255 * 255);
	const int b = tb * vb * va / (255 * 255);
	return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

static uint32_t BlendOver(uint32_t dst, uint32_t src) {
	const int sa = Chan(src, 24);
	const int inv = 255 - sa;
	const int r = std::min(255, (int)Chan(src, 0) + Chan(dst, 0) * inv / 255);
	const int g = std::min(255, (int)Chan(src, 8) + Chan(dst, 8) * inv / 255);
	const int b = std::min(255, (int)Chan(src, 16) + Chan(dst, 16) * inv / 255);
	const int a = std::min(255, sa + Chan(dst, 24) * inv / 255);
	return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

static float Edge(float ax, float ay, float bx, float by, float px, float py) {
	return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

class DrawContext3DS : public Draw::DrawContext {
public:
	DrawContext3DS() {
		gpuReady_ = P3DS_GPU_Init();

		shaderLanguageDesc_.Init(GLSL_1xx);
		targetWidth_ = TOP_W;
		targetHeight_ = TOP_H;

		caps_ = {};
		caps_.vendor = Draw::GPUVendor::VENDOR_UNKNOWN;
		caps_.maxTextureSize = 1024;
		caps_.coordConvention = CoordConvention::OpenGL;
		caps_.preferredDepthBufferFormat = Draw::DataFormat::D16;
		caps_.preferredShadowMapFormatLow = Draw::DataFormat::D16;
		caps_.preferredShadowMapFormatHigh = Draw::DataFormat::D24_S8;
		caps_.framebufferCopySupported = false;
		caps_.framebufferBlitSupported = false;
		caps_.blendMinMaxSupported = true;
		caps_.presentMaxInterval = 1;
		caps_.presentInstantModeChange = true;
		caps_.presentModesSupported = Draw::PresentMode::FIFO | Draw::PresentMode::IMMEDIATE;
		caps_.multiSampleLevelsMask = 1;
		caps_.deviceName = "Nintendo 3DS Citro2D thin3d";

		backbuffer_.assign(TOP_W * TOP_H, 0xFF000000);
		SetScissorRect(0, 0, TOP_W, TOP_H);
		std::fill(std::begin(worldViewProj_), std::end(worldViewProj_), 0.0f);
		worldViewProj_[0] = worldViewProj_[5] = worldViewProj_[10] = worldViewProj_[15] = 1.0f;
	}

	~DrawContext3DS() override {
		DestroyPresets();
		P3DS_GPU_Shutdown();
	}

	const Draw::DeviceCaps &GetDeviceCaps() const override {
		return caps_;
	}

	uint32_t GetDataFormatSupport(Draw::DataFormat fmt) const override {
		switch (fmt) {
		case Draw::DataFormat::R8_UNORM:
		case Draw::DataFormat::R8G8B8_UNORM:
		case Draw::DataFormat::R8G8B8A8_UNORM:
		case Draw::DataFormat::R8G8B8A8_UNORM_SRGB:
		case Draw::DataFormat::B8G8R8A8_UNORM:
		case Draw::DataFormat::B8G8R8A8_UNORM_SRGB:
			return Draw::FMT_TEXTURE | Draw::FMT_INPUTLAYOUT | Draw::FMT_RENDERTARGET;
		default:
			return 0;
		}
	}

	uint32_t GetSupportedShaderLanguages() const override {
		return GLSL_1xx;
	}

	Draw::DepthStencilState *CreateDepthStencilState(const Draw::DepthStencilStateDesc &desc) override {
		(void)desc;
		return new DepthStencilState3DS();
	}

	Draw::BlendState *CreateBlendState(const Draw::BlendStateDesc &desc) override {
		(void)desc;
		return new State3DS();
	}

	Draw::SamplerState *CreateSamplerState(const Draw::SamplerStateDesc &desc) override {
		(void)desc;
		return new SamplerState3DS();
	}

	Draw::RasterState *CreateRasterState(const Draw::RasterStateDesc &desc) override {
		(void)desc;
		return new RasterState3DS();
	}

	Draw::InputLayout *CreateInputLayout(const Draw::InputLayoutDesc &desc) override {
		(void)desc;
		return new InputLayout3DS();
	}

	Draw::ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize, const char *tag = "thin3d") override {
		(void)language;
		(void)tag;
		return new ShaderModule3DS(stage, data, dataSize);
	}

	Draw::Pipeline *CreateGraphicsPipeline(const Draw::PipelineDesc &desc, const char *tag) override {
		return new Pipeline3DS(desc, tag);
	}

	Draw::Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override {
		(void)usageFlags;
		return new Buffer3DS(size);
	}

	Draw::Texture *CreateTexture(const Draw::TextureDesc &desc) override {
		return new Texture3DS(desc);
	}

	Draw::Framebuffer *CreateFramebuffer(const Draw::FramebufferDesc &desc) override {
		return new Framebuffer3DS(desc.width, desc.height, desc.numLayers, desc.multiSampleLevel, desc.tag);
	}

	void UpdateBuffer(Draw::Buffer *buffer, const uint8_t *data, size_t offset, size_t size, Draw::UpdateBufferFlags flags) override {
		(void)flags;
		static_cast<Buffer3DS *>(buffer)->Update(data, offset, size);
	}

	void UpdateTextureLevels(Draw::Texture *texture, const uint8_t **data, Draw::TextureCallback initDataCallback, int numLevels) override {
		static_cast<Texture3DS *>(texture)->Update(data, initDataCallback, numLevels);
	}

	void CopyFramebufferImage(Draw::Framebuffer *src, int level, int x, int y, int z, Draw::Framebuffer *dst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, Draw::Aspect aspects, const char *tag) override {
		(void)src; (void)level; (void)x; (void)y; (void)z; (void)dst; (void)dstLevel; (void)dstX; (void)dstY; (void)dstZ; (void)width; (void)height; (void)depth; (void)aspects; (void)tag;
	}

	bool BlitFramebuffer(Draw::Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Draw::Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, Draw::Aspect aspects, Draw::FBBlitFilter filter, const char *tag) override {
		(void)src; (void)srcX1; (void)srcY1; (void)srcX2; (void)srcY2; (void)dst; (void)dstX1; (void)dstY1; (void)dstX2; (void)dstY2; (void)aspects; (void)filter; (void)tag;
		return false;
	}

	void BindFramebufferAsRenderTarget(Draw::Framebuffer *fbo, const Draw::RenderPassInfo &rp, const char *tag) override {
		(void)tag;
		currentFramebuffer_ = static_cast<Framebuffer3DS *>(fbo);
		if (rp.color == Draw::RPAction::CLEAR) {
			Clear(Draw::Aspect::COLOR_BIT, rp.clearColor, rp.clearDepth, rp.clearStencil);
		}
	}

	void BindFramebufferAsTexture(Draw::Framebuffer *fbo, int binding, Draw::Aspect aspect, int layer) override {
		(void)fbo; (void)binding; (void)aspect; (void)layer;
	}

	void GetFramebufferDimensions(Draw::Framebuffer *fbo, int *w, int *h) override {
		if (fbo) {
			if (w) *w = fbo->Width();
			if (h) *h = fbo->Height();
		} else {
			if (w) *w = TOP_W;
			if (h) *h = TOP_H;
		}
	}

	void SetScissorRect(int left, int top, int width, int height) override {
		const int newX = std::clamp(left, 0, TOP_W);
		const int newY = std::clamp(top, 0, TOP_H);
		const int newW = std::clamp(width, 0, TOP_W - newX);
		const int newH = std::clamp(height, 0, TOP_H - newY);
		if (newX == scissorX_ && newY == scissorY_ && newW == scissorW_ && newH == scissorH_) {
			return;
		}
		scissorX_ = newX;
		scissorY_ = newY;
		scissorW_ = newW;
		scissorH_ = newH;
		if (frameActive_) {
			P3DS_GPU_SetScissor(scissorX_, scissorY_, scissorW_, scissorH_);
		}
	}

	void SetViewport(const Draw::Viewport &viewport) override {
		viewport_ = viewport;
	}

	void SetBlendFactor(float color[4]) override {
		(void)color;
	}

	void SetStencilParams(uint8_t refValue, uint8_t writeMask, uint8_t compareMask) override {
		(void)refValue; (void)writeMask; (void)compareMask;
	}

	void BindSamplerStates(int start, int count, Draw::SamplerState **state) override {
		(void)start; (void)count; (void)state;
	}

	void BindTextures(int start, int count, Draw::Texture **textures, Draw::TextureBindFlags flags = Draw::TextureBindFlags::NONE) override {
		(void)flags;
		for (int i = 0; i < count && start + i < (int)Draw::MAX_TEXTURE_SLOTS; ++i) {
			boundTextures_[start + i] = static_cast<Texture3DS *>(textures[i]);
		}
	}

	void BindVertexBuffer(Draw::Buffer *vertexBuffer, int offset) override {
		(void)vertexBuffer; (void)offset;
	}

	void BindIndexBuffer(Draw::Buffer *indexBuffer, int offset) override {
		(void)indexBuffer; (void)offset;
	}

	void BindNativeTexture(int sampler, void *nativeTexture) override {
		(void)sampler; (void)nativeTexture;
	}

	void UpdateDynamicUniformBuffer(const void *ub, size_t size) override {
		if (ub && size >= sizeof(worldViewProj_)) {
			memcpy(worldViewProj_, ub, sizeof(worldViewProj_));
		}
	}

	void Invalidate(InvalidationFlags flags) override {
		(void)flags;
	}

	void BindPipeline(Draw::Pipeline *pipeline) override {
		currentPipeline_ = static_cast<Pipeline3DS *>(pipeline);
	}

	void Draw(int vertexCount, int offset) override {
		(void)vertexCount; (void)offset;
	}

	void DrawIndexed(int vertexCount, int offset) override {
		(void)vertexCount; (void)offset;
	}

	void DrawUP(const void *vdata, int vertexCount) override {
		const auto *vertices = static_cast<const Vertex2D *>(vdata);
		if (TryDrawNonAxisTriangleListUP(vertices, vertexCount)) {
			return;
		}
		for (int i = 0; i + 2 < vertexCount;) {
			if (i + 5 < vertexCount && TryDrawQuadUP(vertices + i)) {
				i += 6;
				continue;
			}
			RasterTriangle(vertices[i], vertices[i + 1], vertices[i + 2]);
			i += 3;
		}
	}

	void DrawIndexedUP(const void *vdata, int vertexCount, const void *idata, int indexCount) override {
		(void)vertexCount;
		const auto *vertices = static_cast<const Vertex2D *>(vdata);
		const auto *indices = static_cast<const uint16_t *>(idata);
		for (int i = 0; i + 2 < indexCount;) {
			if (i + 5 < indexCount && TryDrawIndexedQuad(vertices, indices + i)) {
				i += 6;
				continue;
			}
			RasterTriangle(vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]]);
			i += 3;
		}
	}

	void DrawIndexedClippedBatchUP(const void *vdata, int vertexCount, const void *idata, int indexCount, Slice<Draw::ClippedDraw> draws, const void *dynUniforms, size_t size) override {
		(void)vertexCount;
		UpdateDynamicUniformBuffer(dynUniforms, size);
		const auto *vertices = static_cast<const Vertex2D *>(vdata);
		const auto *indices = static_cast<const uint16_t *>(idata);
		for (size_t d = 0; d < draws.size(); ++d) {
			const Draw::ClippedDraw &draw = draws[d];
			BindPipeline(draw.pipeline);
			Texture3DS *oldTexture = boundTextures_[0];
			boundTextures_[0] = static_cast<Texture3DS *>(draw.bindTexture);
			SetScissorRect(draw.clipx, draw.clipy, draw.clipw, draw.cliph);
			for (int i = 0; i + 2 < draw.indexCount;) {
				const int base = draw.indexOffset + i;
				if (i + 5 < draw.indexCount && TryDrawIndexedQuad(vertices, indices + base)) {
					i += 6;
					continue;
				}
				RasterTriangle(vertices[indices[base]], vertices[indices[base + 1]], vertices[indices[base + 2]]);
				i += 3;
			}
			boundTextures_[0] = oldTexture;
		}
		SetScissorRect(0, 0, TOP_W, TOP_H);
	}

	void BeginFrame(Draw::DebugFlags debugFlags) override {
		(void)debugFlags;
		++frameCount_;
		if (frameCount_ == 1) {
			remove("sdmc:/3ds/PPSSPP/gpu.log");
		}
		statsTexturedQuadsFrame_ = 0;
		statsR8QuadsFrame_ = 0;
		statsSolidQuadsFrame_ = 0;
		statsSolidTrianglesFrame_ = 0;
		statsTexturedTrianglesFrame_ = 0;
		statsTexturedTriangleMissFrame_ = 0;
		statsTextureNotReadyFrame_ = 0;
		statsTextureDrawFailFrame_ = 0;
		statsMissTexW_ = 0;
		statsMissTexH_ = 0;
		statsMissTexPW_ = 0;
		statsMissTexPH_ = 0;
		statsMissTexFormat_ = -1;
		statsMissTexSwizzle_ = -1;
		currentFramebuffer_ = nullptr;
		if (gpuReady_) {
			P3DS_GPU_BeginFrame(0xFF101820);
			frameActive_ = true;
		} else {
			std::fill(backbuffer_.begin(), backbuffer_.end(), 0xFF101820);
		}
		SetScissorRect(0, 0, TOP_W, TOP_H);
	}

	void EndFrame() override {
	}

	void Present(Draw::PresentMode presentMode) override {
		(void)presentMode;
		if (frameActive_) {
#if 1
			if ((frameCount_ % 60) == 0) {
				char line[256]{};
				snprintf(line, sizeof(line), "frame=%d solidQuad=%d texturedQuad=%d r8Quad=%d solidTri=%d texturedTri=%d texturedTriMiss=%d notReady=%d drawFail=%d missTex=%dx%d/%dx%d fmt=%d swz=%d",
					frameCount_, statsSolidQuadsFrame_, statsTexturedQuadsFrame_, statsR8QuadsFrame_, statsSolidTrianglesFrame_,
					statsTexturedTrianglesFrame_, statsTexturedTriangleMissFrame_, statsTextureNotReadyFrame_, statsTextureDrawFailFrame_,
					statsMissTexW_, statsMissTexH_, statsMissTexPW_, statsMissTexPH_, statsMissTexFormat_, statsMissTexSwizzle_);
				Log3DSGPUStats(line);
			}
#endif
			P3DS_GPU_EndFrame();
			frameActive_ = false;
			return;
		}

		uint8_t *fb = P3DS_GetTopFramebuffer();
		if (fb) {
			for (int x = 0; x < TOP_W; ++x) {
				for (int y = 0; y < TOP_H; ++y) {
					const uint32_t rgba = backbuffer_[y * TOP_W + x];
					const int offset = 3 * ((TOP_H - 1 - y) + x * TOP_H);
					fb[offset + 0] = Chan(rgba, 16);
					fb[offset + 1] = Chan(rgba, 8);
					fb[offset + 2] = Chan(rgba, 0);
				}
			}
		}
		P3DS_WaitVBlank();
		P3DS_Present();
	}

	Draw::PresentMode GetCurrentPresentMode() const override {
		return Draw::PresentMode::FIFO;
	}

	void Clear(Draw::Aspect aspects, uint32_t colorval, float depthVal, int stencilVal) override {
		(void)depthVal; (void)stencilVal;
		if (!(aspects & Draw::Aspect::COLOR_BIT)) {
			return;
		}
		uint32_t color = colorval | 0xFF000000;
		if (currentFramebuffer_) {
			std::fill(currentFramebuffer_->Pixels(), currentFramebuffer_->Pixels() + currentFramebuffer_->Width() * currentFramebuffer_->Height(), color);
		} else if (frameActive_) {
			P3DS_GPU_Clear(color);
		} else {
			std::fill(backbuffer_.begin(), backbuffer_.end(), color);
		}
	}

	std::string GetInfoString(Draw::InfoField info) const override {
		switch (info) {
		case Draw::InfoField::APINAME:
			return "3DS Citro2D thin3d";
		case Draw::InfoField::VENDORSTRING:
			return "devkitARM/libctru";
		default:
			return "";
		}
	}

	uint64_t GetNativeObject(Draw::NativeObject obj, void *srcObject = nullptr) override {
		(void)obj; (void)srcObject;
		return 0;
	}

	void HandleEvent(Draw::Event ev, int width, int height, void *param1 = nullptr, void *param2 = nullptr) override {
		(void)ev; (void)width; (void)height; (void)param1; (void)param2;
	}

	void SetInvalidationCallback(InvalidationCallback callback) override {
		invalidationCallback_ = callback;
	}

	int GetFrameCount() override {
		return frameCount_;
	}

private:
	struct ScreenVertex {
		float x;
		float y;
		float u;
		float v;
		float r;
		float g;
		float b;
		float a;
	};

	uint32_t *TargetPixels() {
		return currentFramebuffer_ ? currentFramebuffer_->Pixels() : backbuffer_.data();
	}

	int TargetWidth() const {
		return currentFramebuffer_ ? currentFramebuffer_->Width() : TOP_W;
	}

	int TargetHeight() const {
		return currentFramebuffer_ ? currentFramebuffer_->Height() : TOP_H;
	}

	ScreenVertex Transform(const Vertex2D &v) const {
		const float cx = v.x * worldViewProj_[0] + v.y * worldViewProj_[4] + v.z * worldViewProj_[8] + worldViewProj_[12];
		const float cy = v.x * worldViewProj_[1] + v.y * worldViewProj_[5] + v.z * worldViewProj_[9] + worldViewProj_[13];
		const float cw = v.x * worldViewProj_[3] + v.y * worldViewProj_[7] + v.z * worldViewProj_[11] + worldViewProj_[15];
		const float invW = cw != 0.0f ? 1.0f / cw : 1.0f;
		const float ndcX = cx * invW;
		const float ndcY = cy * invW;
		ScreenVertex out{};
		out.x = (ndcX * 0.5f + 0.5f) * (float)TargetWidth();
		out.y = (0.5f - ndcY * 0.5f) * (float)TargetHeight();
		out.u = v.u;
		out.v = v.v;
		out.r = (float)Chan(v.rgba, 0);
		out.g = (float)Chan(v.rgba, 8);
		out.b = (float)Chan(v.rgba, 16);
		out.a = (float)Chan(v.rgba, 24);
		return out;
	}

	static uint32_t PackColor(const ScreenVertex &v) {
		const int r = std::clamp((int)(v.r + 0.5f), 0, 255);
		const int g = std::clamp((int)(v.g + 0.5f), 0, 255);
		const int b = std::clamp((int)(v.b + 0.5f), 0, 255);
		const int a = std::clamp((int)(v.a + 0.5f), 0, 255);
		return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
	}

	void TransformVertices(const Vertex2D *vertices, int vertexCount) {
		transformedScratch_.resize(std::max(0, vertexCount));
		for (int i = 0; i < vertexCount; ++i) {
			transformedScratch_[i] = Transform(vertices[i]);
		}
	}

	static bool Near(float a, float b, float eps = 0.75f) {
		return fabsf(a - b) <= eps;
	}

	static bool SameVertex(const ScreenVertex &a, const ScreenVertex &b) {
		return Near(a.x, b.x) && Near(a.y, b.y) &&
			fabsf(a.u - b.u) < 0.0001f && fabsf(a.v - b.v) < 0.0001f &&
			Near(a.r, b.r, 0.5f) && Near(a.g, b.g, 0.5f) && Near(a.b, b.b, 0.5f) && Near(a.a, b.a, 0.5f);
	}

	static bool IsAxisAlignedQuad(const ScreenVertex *quad) {
		float minX = quad[0].x;
		float maxX = quad[0].x;
		float minY = quad[0].y;
		float maxY = quad[0].y;
		for (int i = 1; i < 4; ++i) {
			minX = std::min(minX, quad[i].x);
			maxX = std::max(maxX, quad[i].x);
			minY = std::min(minY, quad[i].y);
			maxY = std::max(maxY, quad[i].y);
		}
		bool seen[4]{};
		for (int i = 0; i < 4; ++i) {
			const bool left = Near(quad[i].x, minX);
			const bool right = Near(quad[i].x, maxX);
			const bool top = Near(quad[i].y, minY);
			const bool bottom = Near(quad[i].y, maxY);
			int corner = -1;
			if (left && top) corner = 0;
			else if (right && top) corner = 1;
			else if (left && bottom) corner = 2;
			else if (right && bottom) corner = 3;
			if (corner < 0 || seen[corner]) {
				return false;
			}
			seen[corner] = true;
		}
		return seen[0] && seen[1] && seen[2] && seen[3];
	}

	bool TryDrawNonAxisTriangleListUP(const Vertex2D *vertices, int vertexCount) {
		if (vertexCount < 12 || (vertexCount % 3) != 0 || !frameActive_ || currentFramebuffer_ || !currentPipeline_) {
			return false;
		}

		ScreenVertex first[6]{};
		ScreenVertex unique[4]{};
		int count = 0;
		for (int i = 0; i < 6; ++i) {
			first[i] = Transform(vertices[i]);
			bool found = false;
			for (int j = 0; j < count; ++j) {
				if (SameVertex(unique[j], first[i])) {
					found = true;
					break;
				}
			}
			if (!found) {
				if (count == 4) {
					return false;
				}
				unique[count++] = first[i];
			}
		}
		if (count != 4 || IsAxisAlignedQuad(unique)) {
			return false;
		}

		TransformVertices(vertices, vertexCount);
		return TryDrawTransformedTriangleList(transformedScratch_.data(), (int)transformedScratch_.size());
	}

	bool TryDrawQuadUP(const Vertex2D *vertices) {
		ScreenVertex transformed[6]{};
		ScreenVertex unique[4]{};
		int count = 0;
		for (int i = 0; i < 6; ++i) {
			ScreenVertex v = Transform(vertices[i]);
			transformed[i] = v;
			bool found = false;
			for (int j = 0; j < count; ++j) {
				if (SameVertex(unique[j], v)) {
					found = true;
					break;
				}
			}
			if (!found) {
				if (count == 4) {
					return false;
				}
				unique[count++] = v;
			}
		}
		if (count != 4) {
			return false;
		}
		if (TryDrawAxisAlignedQuad(unique)) {
			return true;
		}
		return TryDrawTransformedTriangleList(transformed, 6);
	}

	bool TryDrawIndexedQuad(const Vertex2D *vertices, const uint16_t *indices) {
		ScreenVertex transformed[6]{};
		ScreenVertex unique[4]{};
		int count = 0;
		for (int i = 0; i < 6; ++i) {
			ScreenVertex v = Transform(vertices[indices[i]]);
			transformed[i] = v;
			bool found = false;
			for (int j = 0; j < count; ++j) {
				if (SameVertex(unique[j], v)) {
					found = true;
					break;
				}
			}
			if (!found) {
				if (count == 4) {
					return false;
				}
				unique[count++] = v;
			}
		}
		if (count != 4) {
			return false;
		}
		if (TryDrawAxisAlignedQuad(unique)) {
			return true;
		}
		return TryDrawTransformedTriangleList(transformed, 6);
	}

	bool TryDrawTransformedTriangleList(const ScreenVertex *vertices, int vertexCount) {
		if (!frameActive_ || currentFramebuffer_ || !currentPipeline_) {
			return false;
		}

		Texture3DS *texture = boundTextures_[0];
		for (int i = 0; i + 2 < vertexCount; i += 3) {
			const ScreenVertex &a = vertices[i];
			const ScreenVertex &b = vertices[i + 1];
			const ScreenVertex &c = vertices[i + 2];
			if (!texture) {
				++statsSolidTrianglesFrame_;
				P3DS_GPU_DrawTriangle(a.x, a.y, PackColor(a), b.x, b.y, PackColor(b), c.x, c.y, PackColor(c));
			} else if (!texture->Ready() || !texture->Tex()) {
				++statsTextureNotReadyFrame_;
			} else if (P3DS_GPU_DrawTexturedTriangle(texture->Tex(),
				a.x, a.y, a.u, a.v, PackColor(a),
				b.x, b.y, b.u, b.v, PackColor(b),
				c.x, c.y, c.u, c.v, PackColor(c))) {
				++statsTexturedTrianglesFrame_;
			} else {
				++statsTextureDrawFailFrame_;
			}
		}
		statsTexturedTriangleMissFrame_ = statsTextureNotReadyFrame_ + statsTextureDrawFailFrame_;
		return true;
	}

	bool TryDrawAxisAlignedQuad(const ScreenVertex *quad) {
		if (!frameActive_ || currentFramebuffer_) {
			return false;
		}

		float minX = quad[0].x;
		float maxX = quad[0].x;
		float minY = quad[0].y;
		float maxY = quad[0].y;
		for (int i = 1; i < 4; ++i) {
			minX = std::min(minX, quad[i].x);
			maxX = std::max(maxX, quad[i].x);
			minY = std::min(minY, quad[i].y);
			maxY = std::max(maxY, quad[i].y);
		}
		if (maxX - minX <= 0.25f || maxY - minY <= 0.25f) {
			return true;
		}

		ScreenVertex corners[4]{};
		bool seen[4]{};
		for (int i = 0; i < 4; ++i) {
			const bool left = Near(quad[i].x, minX);
			const bool right = Near(quad[i].x, maxX);
			const bool top = Near(quad[i].y, minY);
			const bool bottom = Near(quad[i].y, maxY);
			int corner = -1;
			if (left && top) corner = 0;
			else if (right && top) corner = 1;
			else if (left && bottom) corner = 2;
			else if (right && bottom) corner = 3;
			if (corner < 0 || seen[corner]) {
				return false;
			}
			corners[corner] = quad[i];
			seen[corner] = true;
		}
		if (!seen[0] || !seen[1] || !seen[2] || !seen[3]) {
			return false;
		}

		Texture3DS *texture = boundTextures_[0];
		if (!texture) {
			++statsSolidQuadsFrame_;
			return P3DS_GPU_DrawRectangle(minX, minY, maxX - minX, maxY - minY,
				PackColor(corners[0]), PackColor(corners[1]), PackColor(corners[2]), PackColor(corners[3]));
		}
		if (!texture->Ready() || !texture->Tex()) {
			return false;
		}

		const float uvEps = 0.0025f;
		if (fabsf(corners[0].u - corners[2].u) > uvEps ||
			fabsf(corners[1].u - corners[3].u) > uvEps ||
			fabsf(corners[0].v - corners[1].v) > uvEps ||
			fabsf(corners[2].v - corners[3].v) > uvEps) {
			return false;
		}

		const float rawU0 = corners[0].u;
		const float rawU1 = corners[1].u;
		const float rawV0 = corners[0].v;
		const float rawV1 = corners[2].v;
		const float u0 = std::min(rawU0, rawU1);
		const float u1 = std::max(rawU0, rawU1);
		const float v0 = std::min(rawV0, rawV1);
		const float v1 = std::max(rawV0, rawV1);
		if (u1 <= u0 || v1 <= v0) {
			return false;
		}

		++statsTexturedQuadsFrame_;
		if (texture->IsR8()) {
			++statsR8QuadsFrame_;
		}
		return P3DS_GPU_DrawImage(texture->Tex(), minX, minY, maxX - minX, maxY - minY,
			u0, v0, u1, v1, PackColor(corners[0]), PackColor(corners[1]), PackColor(corners[2]), PackColor(corners[3]));
	}

	void RasterTriangle(const Vertex2D &va, const Vertex2D &vb, const Vertex2D &vc) {
		if (!currentPipeline_) {
			return;
		}

		const ScreenVertex a = Transform(va);
		const ScreenVertex b = Transform(vb);
		const ScreenVertex c = Transform(vc);
		RasterTriangle(a, b, c);
	}

	void RasterTriangle(const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c) {
		if (!currentPipeline_) {
			return;
		}

		if (frameActive_ && !currentFramebuffer_) {
			Texture3DS *texture = boundTextures_[0];
			if (!texture) {
				++statsSolidTrianglesFrame_;
				P3DS_GPU_DrawTriangle(a.x, a.y, PackColor(a), b.x, b.y, PackColor(b), c.x, c.y, PackColor(c));
			} else {
				if (!texture->Ready() || !texture->Tex()) {
					++statsTextureNotReadyFrame_;
					if (statsMissTexFormat_ < 0) {
						statsMissTexW_ = texture->Width();
						statsMissTexH_ = texture->Height();
						statsMissTexPW_ = texture->TexWidth();
						statsMissTexPH_ = texture->TexHeight();
						statsMissTexFormat_ = (int)texture->Format();
						statsMissTexSwizzle_ = (int)texture->Swizzle();
					}
				} else if (P3DS_GPU_DrawTexturedTriangle(texture->Tex(),
					a.x, a.y, a.u, a.v, PackColor(a),
					b.x, b.y, b.u, b.v, PackColor(b),
					c.x, c.y, c.u, c.v, PackColor(c))) {
					++statsTexturedTrianglesFrame_;
				} else {
					++statsTextureDrawFailFrame_;
				}
				statsTexturedTriangleMissFrame_ = statsTextureNotReadyFrame_ + statsTextureDrawFailFrame_;
			}
			return;
		}

		const float area = Edge(a.x, a.y, b.x, b.y, c.x, c.y);
		if (fabsf(area) < 0.0001f) {
			return;
		}

		const int targetW = TargetWidth();
		const int targetH = TargetHeight();
		const int minX = std::max(scissorX_, std::max(0, (int)floorf(std::min({a.x, b.x, c.x}))));
		const int minY = std::max(scissorY_, std::max(0, (int)floorf(std::min({a.y, b.y, c.y}))));
		const int maxX = std::min(scissorX_ + scissorW_, std::min(targetW, (int)ceilf(std::max({a.x, b.x, c.x}))));
		const int maxY = std::min(scissorY_ + scissorH_, std::min(targetH, (int)ceilf(std::max({a.y, b.y, c.y}))));
		if (minX >= maxX || minY >= maxY) {
			return;
		}

		Texture3DS *texture = boundTextures_[0];
		const bool textured = texture != nullptr;
		uint32_t *target = TargetPixels();
		for (int y = minY; y < maxY; ++y) {
			for (int x = minX; x < maxX; ++x) {
				const float px = (float)x + 0.5f;
				const float py = (float)y + 0.5f;
				const float w0 = Edge(b.x, b.y, c.x, c.y, px, py) / area;
				const float w1 = Edge(c.x, c.y, a.x, a.y, px, py) / area;
				const float w2 = Edge(a.x, a.y, b.x, b.y, px, py) / area;
				if (w0 < -0.0001f || w1 < -0.0001f || w2 < -0.0001f) {
					continue;
				}

				const float u = a.u * w0 + b.u * w1 + c.u * w2;
				const float v = a.v * w0 + b.v * w1 + c.v * w2;
				const int r = std::clamp((int)(a.r * w0 + b.r * w1 + c.r * w2 + 0.5f), 0, 255);
				const int g = std::clamp((int)(a.g * w0 + b.g * w1 + c.g * w2 + 0.5f), 0, 255);
				const int bl = std::clamp((int)(a.b * w0 + b.b * w1 + c.b * w2 + 0.5f), 0, 255);
				const int al = std::clamp((int)(a.a * w0 + b.a * w1 + c.a * w2 + 0.5f), 0, 255);
				const uint32_t vertexColor = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)bl << 16) | ((uint32_t)al << 24);
				const uint32_t texColor = textured ? texture->Sample(u, v) : 0xFFFFFFFF;
				const uint32_t src = PremulModulate(texColor, vertexColor, textured);
				uint32_t &dst = target[y * targetW + x];
				dst = BlendOver(dst, src);
			}
		}
	}

	Draw::DeviceCaps caps_{};
	std::vector<uint32_t> backbuffer_;
	bool gpuReady_ = false;
	bool frameActive_ = false;
	Framebuffer3DS *currentFramebuffer_ = nullptr;
	Pipeline3DS *currentPipeline_ = nullptr;
	Texture3DS *boundTextures_[Draw::MAX_TEXTURE_SLOTS]{};
	std::vector<ScreenVertex> transformedScratch_;
	Draw::Viewport viewport_{0, 0, (float)TOP_W, (float)TOP_H, 0.0f, 1.0f};
	InvalidationCallback invalidationCallback_{};
	float worldViewProj_[16]{};
	int scissorX_ = 0;
	int scissorY_ = 0;
	int scissorW_ = TOP_W;
	int scissorH_ = TOP_H;
	int frameCount_ = 0;
	int statsTexturedQuadsFrame_ = 0;
	int statsR8QuadsFrame_ = 0;
	int statsSolidQuadsFrame_ = 0;
	int statsSolidTrianglesFrame_ = 0;
	int statsTexturedTrianglesFrame_ = 0;
	int statsTexturedTriangleMissFrame_ = 0;
	int statsTextureNotReadyFrame_ = 0;
	int statsTextureDrawFailFrame_ = 0;
	int statsMissTexW_ = 0;
	int statsMissTexH_ = 0;
	int statsMissTexPW_ = 0;
	int statsMissTexPH_ = 0;
	int statsMissTexFormat_ = -1;
	int statsMissTexSwizzle_ = -1;
};

}  // namespace

GraphicsContext3DS::GraphicsContext3DS() : draw_(new DrawContext3DS()) {
}

GraphicsContext3DS::~GraphicsContext3DS() = default;

void GraphicsContext3DS::Shutdown() {
	draw_.reset();
}

void GraphicsContext3DS::Resize() {
}

Draw::DrawContext *GraphicsContext3DS::GetDrawContext() {
	return draw_.get();
}
