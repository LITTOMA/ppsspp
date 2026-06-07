#include "GPU3DS.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdlib.h>
#include <string.h>

#include "gpu3ds_textured_shbin.h"

struct P3DS_GPUTexture {
	C3D_Tex tex[4];
	bool texValid[4];
	int width;
	int height;
	int texWidth;
	int texHeight;
	int pagesX;
	int pagesY;
};

typedef struct {
	float x;
	float y;
	float z;
	float u;
	float v;
	float r;
	float g;
	float b;
	float a;
} P3DS_GPUVertex;

enum {
	TEXTURED_BATCH_MAX_VERTS = 65536,
};

static C3D_RenderTarget *top_target;
static bool gpu_ready;
static bool frame_active;
static bool custom_state_active;
static bool custom_batch_active;
static bool scissor_full = true;
static int clip_x = 0;
static int clip_y = 0;
static int clip_w = 400;
static int clip_h = 240;
static P3DS_GPUVertex *textured_batch_vertices;
static int textured_batch_offset;
static int textured_batch_count;
static DVLB_s *textured_dvlb;
static shaderProgram_s textured_program;
static int textured_projection_loc;
static C3D_Mtx textured_projection;
static P3DS_GPUTexture *custom_texture;
static P3DS_GPUTexture *white_texture;
static int custom_page = -1;

static int morton8(int x, int y) {
	return (x & 1) | ((y & 1) << 1) |
		((x & 2) << 1) | ((y & 2) << 2) |
		((x & 4) << 2) | ((y & 4) << 3);
}

static int tiled_offset(int x, int y, int width) {
	const int tileX = x >> 3;
	const int tileY = y >> 3;
	const int tilesPerRow = width >> 3;
	return ((tileY * tilesPerRow + tileX) << 6) + morton8(x & 7, y & 7);
}

static int clamp_page_coord(float value, int size, int pages) {
	int pixel = (int)(value * (float)size);
	if (pixel < 0) {
		pixel = 0;
	}
	if (pixel >= size) {
		pixel = size - 1;
	}
	int page = pixel / 1024;
	if (page < 0) {
		page = 0;
	}
	if (page >= pages) {
		page = pages - 1;
	}
	return page;
}

static float color_r(uint32_t color) { return (float)(color & 0xFF) / 255.0f; }
static float color_g(uint32_t color) { return (float)((color >> 8) & 0xFF) / 255.0f; }
static float color_b(uint32_t color) { return (float)((color >> 16) & 0xFF) / 255.0f; }
static float color_a(uint32_t color) { return (float)((color >> 24) & 0xFF) / 255.0f; }

static bool init_textured_pipeline(void) {
	textured_dvlb = DVLB_ParseFile((u32 *)gpu3ds_textured_shbin, gpu3ds_textured_shbin_size);
	if (!textured_dvlb) {
		return false;
	}

	shaderProgramInit(&textured_program);
	shaderProgramSetVsh(&textured_program, &textured_dvlb->DVLE[0]);
	textured_projection_loc = shaderInstanceGetUniformLocation(textured_program.vertexShader, "projection");
	Mtx_OrthoTilt(&textured_projection, 0.0f, 400.0f, 240.0f, 0.0f, 0.0f, 1.0f, true);
	return true;
}

static void shutdown_textured_pipeline(void) {
	if (textured_dvlb) {
		shaderProgramFree(&textured_program);
		DVLB_Free(textured_dvlb);
	}
	textured_dvlb = NULL;
	textured_projection_loc = -1;
	custom_state_active = false;
}

static void apply_c3d_scissor(void) {
	if (scissor_full) {
		C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	} else {
		C3D_SetScissor(GPU_SCISSOR_NORMAL, clip_x, clip_y, clip_x + clip_w, clip_y + clip_h);
	}
}

static void flush_textured_batch(void) {
	if (!custom_batch_active) {
		return;
	}
	C3D_BufInfo *bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, textured_batch_vertices + textured_batch_offset, sizeof(P3DS_GPUVertex), 3, 0x210);
	if (textured_batch_count > 0) {
		C3D_DrawArrays(GPU_TRIANGLES, 0, textured_batch_count);
	}
	textured_batch_offset += textured_batch_count;
	textured_batch_count = 0;
	custom_batch_active = false;
}

static void restore_c2d_state(void) {
	if (!custom_state_active || !frame_active || !top_target) {
		return;
	}
	flush_textured_batch();
	C2D_Prepare();
	C2D_SceneBegin(top_target);
	apply_c3d_scissor();
	custom_state_active = false;
	custom_texture = NULL;
	custom_page = -1;
}

static bool prepare_textured_pipeline(P3DS_GPUTexture *texture, int page) {
	if (!frame_active || !texture || !textured_dvlb || page < 0 || page >= 4 || !texture->texValid[page]) {
		return false;
	}

	if (custom_state_active && custom_texture == texture && custom_page == page) {
		return true;
	}
	flush_textured_batch();
	C3D_BindProgram(&textured_program);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, textured_projection_loc, &textured_projection);

	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3);
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2);
	AttrInfo_AddLoader(attrInfo, 2, GPU_FLOAT, 4);

	C3D_TexBind(0, &texture->tex[page]);
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);

	C3D_CullFace(GPU_CULL_NONE);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	custom_state_active = true;
	custom_texture = texture;
	custom_page = page;
	return true;
}

static bool begin_textured_batch(P3DS_GPUTexture *texture, int page) {
	if (!prepare_textured_pipeline(texture, page)) {
		return false;
	}
	if (!custom_batch_active) {
		custom_batch_active = true;
	}
	return true;
}

static void append_textured_vertex(float x, float y, float z, float u, float v, float r, float g, float b, float a) {
	if (!custom_batch_active || !textured_batch_vertices) {
		return;
	}
	if (textured_batch_offset + textured_batch_count >= TEXTURED_BATCH_MAX_VERTS) {
		C3D_BufInfo *bufInfo = C3D_GetBufInfo();
		BufInfo_Init(bufInfo);
		BufInfo_Add(bufInfo, textured_batch_vertices + textured_batch_offset, sizeof(P3DS_GPUVertex), 3, 0x210);
		if (textured_batch_count > 0) {
			C3D_DrawArrays(GPU_TRIANGLES, 0, textured_batch_count);
		}
		textured_batch_offset += textured_batch_count;
		textured_batch_count = 0;
	}
	if (textured_batch_offset >= TEXTURED_BATCH_MAX_VERTS) {
		return;
	}
	P3DS_GPUVertex *vertex = &textured_batch_vertices[textured_batch_offset + textured_batch_count++];
	vertex->x = x;
	vertex->y = y;
	vertex->z = z;
	vertex->u = u;
	vertex->v = v;
	vertex->r = r;
	vertex->g = g;
	vertex->b = b;
	vertex->a = a;
}

static void send_textured_vertex(const P3DS_GPUTexture *texture, int pageX, int pageY, float x, float y, float u, float v, uint32_t color) {
	const int pageW = texture->texWidth / texture->pagesX;
	const int pageH = texture->texHeight / texture->pagesY;
	const float srcX = u * (float)texture->width - (float)(pageX * pageW);
	const float srcY = v * (float)texture->height - (float)(pageY * pageH);
	const float tu = srcX / (float)pageW;
	const float tv = 1.0f - srcY / (float)pageH;
	append_textured_vertex(x, y, 0.5f, tu, tv, color_r(color), color_g(color), color_b(color), color_a(color));
}

typedef struct {
	float x;
	float y;
	float u;
	float v;
	float r;
	float g;
	float b;
	float a;
} P3DS_TriVert;

static P3DS_TriVert make_tri_vert(float x, float y, float u, float v, uint32_t color) {
	P3DS_TriVert out;
	out.x = x;
	out.y = y;
	out.u = u;
	out.v = v;
	out.r = color_r(color);
	out.g = color_g(color);
	out.b = color_b(color);
	out.a = color_a(color);
	return out;
}

static P3DS_TriVert lerp_tri_vert(P3DS_TriVert a, P3DS_TriVert b, float t) {
	P3DS_TriVert out;
	out.x = a.x + (b.x - a.x) * t;
	out.y = a.y + (b.y - a.y) * t;
	out.u = a.u + (b.u - a.u) * t;
	out.v = a.v + (b.v - a.v) * t;
	out.r = a.r + (b.r - a.r) * t;
	out.g = a.g + (b.g - a.g) * t;
	out.b = a.b + (b.b - a.b) * t;
	out.a = a.a + (b.a - a.a) * t;
	return out;
}

static void send_tri_vert_f(const P3DS_GPUTexture *texture, int pageX, int pageY, const P3DS_TriVert *v) {
	const int pageW = texture->texWidth / texture->pagesX;
	const int pageH = texture->texHeight / texture->pagesY;
	const float srcX = v->u * (float)texture->width - (float)(pageX * pageW);
	const float srcY = v->v * (float)texture->height - (float)(pageY * pageH);
	const float tu = srcX / (float)pageW;
	const float tv = 1.0f - srcY / (float)pageH;
	append_textured_vertex(v->x, v->y, 0.5f, tu, tv, v->r, v->g, v->b, v->a);
}

static float tri_vert_coord(P3DS_TriVert v, int axis) {
	switch (axis) {
	case 0: return v.x;
	case 1: return v.y;
	case 2: return v.u;
	default: return v.v;
	}
}

static int clip_tri_polygon(const P3DS_TriVert *in, int inCount, P3DS_TriVert *out, int axis, bool keepGreater, float value) {
	int outCount = 0;
	if (inCount <= 0) {
		return 0;
	}
	P3DS_TriVert prev = in[inCount - 1];
	float prevCoord = tri_vert_coord(prev, axis);
	bool prevInside = keepGreater ? prevCoord >= value : prevCoord <= value;
	for (int i = 0; i < inCount; ++i) {
		P3DS_TriVert cur = in[i];
		float curCoord = tri_vert_coord(cur, axis);
		bool curInside = keepGreater ? curCoord >= value : curCoord <= value;
		if (curInside != prevInside) {
			const float denom = curCoord - prevCoord;
			const float t = denom != 0.0f ? (value - prevCoord) / denom : 0.0f;
			out[outCount++] = lerp_tri_vert(prev, cur, t);
		}
		if (curInside) {
			out[outCount++] = cur;
		}
		prev = cur;
		prevCoord = curCoord;
		prevInside = curInside;
	}
	return outCount;
}

static bool draw_clipped_textured_triangle_page(P3DS_GPUTexture *texture, int pageX, int pageY, const P3DS_TriVert *tri) {
	const int pageW = texture->texWidth / texture->pagesX;
	const int pageH = texture->texHeight / texture->pagesY;
	const float uMin = (float)(pageX * pageW) / (float)texture->width;
	const float uMax = (float)((pageX + 1) * pageW) / (float)texture->width;
	const float vMin = (float)(pageY * pageH) / (float)texture->height;
	const float vMax = (float)((pageY + 1) * pageH) / (float)texture->height;
	P3DS_TriVert bufA[16];
	P3DS_TriVert bufB[16];
	bufA[0] = tri[0];
	bufA[1] = tri[1];
	bufA[2] = tri[2];
	int count = 3;
	count = clip_tri_polygon(bufA, count, bufB, 0, true, (float)clip_x);
	count = clip_tri_polygon(bufB, count, bufA, 0, false, (float)(clip_x + clip_w));
	count = clip_tri_polygon(bufA, count, bufB, 1, true, (float)clip_y);
	count = clip_tri_polygon(bufB, count, bufA, 1, false, (float)(clip_y + clip_h));
	count = clip_tri_polygon(bufA, count, bufB, 2, true, uMin);
	count = clip_tri_polygon(bufB, count, bufA, 2, false, uMax);
	count = clip_tri_polygon(bufA, count, bufB, 3, true, vMin);
	count = clip_tri_polygon(bufB, count, bufA, 3, false, vMax);
	if (count < 3) {
		return false;
	}
	const int page = pageY * texture->pagesX + pageX;
	if (!begin_textured_batch(texture, page)) {
		return false;
	}
	for (int i = 1; i + 1 < count; ++i) {
		send_tri_vert_f(texture, pageX, pageY, &bufA[0]);
		send_tri_vert_f(texture, pageX, pageY, &bufA[i]);
		send_tri_vert_f(texture, pageX, pageY, &bufA[i + 1]);
	}
	return true;
}

bool P3DS_GPU_Init(void) {
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 2)) {
		C3D_Fini();
		return false;
	}
	textured_batch_vertices = (P3DS_GPUVertex *)linearAlloc(sizeof(P3DS_GPUVertex) * TEXTURED_BATCH_MAX_VERTS);
	if (!textured_batch_vertices) {
		C2D_Fini();
		C3D_Fini();
		return false;
	}
	C2D_Prepare();
	C2D_SetTintMode(C2D_TintMult);
	top_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	gpu_ready = top_target != NULL && init_textured_pipeline();
	if (gpu_ready) {
		uint8_t white[8 * 8 * 4];
		memset(white, 0xFF, sizeof(white));
		white_texture = P3DS_GPU_CreateTexture(1, 1, 8, 8, white);
		gpu_ready = white_texture != NULL;
	}
	if (!gpu_ready) {
		P3DS_GPU_DestroyTexture(white_texture);
		white_texture = NULL;
		linearFree(textured_batch_vertices);
		textured_batch_vertices = NULL;
		shutdown_textured_pipeline();
		C2D_Fini();
		C3D_Fini();
	}
	return gpu_ready;
}

void P3DS_GPU_Shutdown(void) {
	if (top_target) {
		C3D_RenderTargetDelete(top_target);
		top_target = NULL;
	}
	if (gpu_ready) {
		P3DS_GPU_DestroyTexture(white_texture);
		white_texture = NULL;
		shutdown_textured_pipeline();
		linearFree(textured_batch_vertices);
		textured_batch_vertices = NULL;
		C2D_Fini();
		C3D_Fini();
	}
	gpu_ready = false;
	frame_active = false;
}

void P3DS_GPU_BeginFrame(uint32_t clearColor) {
	if (!gpu_ready || !top_target) {
		return;
	}
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	frame_active = true;
	C3D_RenderTargetClear(top_target, C3D_CLEAR_ALL, clearColor, 0);
	C3D_FrameDrawOn(top_target);
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	custom_state_active = false;
	custom_batch_active = false;
	scissor_full = true;
	clip_x = 0;
	clip_y = 0;
	clip_w = 400;
	clip_h = 240;
	textured_batch_offset = 0;
	textured_batch_count = 0;
	custom_texture = NULL;
	custom_page = -1;
}

void P3DS_GPU_EndFrame(void) {
	if (!frame_active) {
		return;
	}
	flush_textured_batch();
	custom_state_active = false;
	custom_texture = NULL;
	custom_page = -1;
	C3D_FrameEnd(0);
	frame_active = false;
}

void P3DS_GPU_SetScissor(int x, int y, int w, int h) {
	if (!frame_active) {
		return;
	}
	if (x == clip_x && y == clip_y && w == clip_w && h == clip_h) {
		return;
	}
	clip_x = x;
	clip_y = y;
	clip_w = w;
	clip_h = h;
	if (x <= 0 && y <= 0 && w >= 400 && h >= 240) {
		scissor_full = true;
		clip_x = 0;
		clip_y = 0;
		clip_w = 400;
		clip_h = 240;
	} else {
		scissor_full = false;
	}
	if (custom_state_active) {
		return;
	}
	apply_c3d_scissor();
}

void P3DS_GPU_Clear(uint32_t color) {
	if (frame_active && top_target) {
		flush_textured_batch();
		C3D_RenderTargetClear(top_target, C3D_CLEAR_ALL, color, 0);
		C3D_FrameDrawOn(top_target);
	}
}

P3DS_GPUTexture *P3DS_GPU_CreateTexture(int width, int height, int texWidth, int texHeight, const void *tiledRGBA8) {
	if (!gpu_ready || !tiledRGBA8 || width <= 0 || height <= 0 || texWidth <= 0 || texHeight <= 0) {
		return NULL;
	}
	if (texWidth > 2048 || texHeight > 2048) {
		return NULL;
	}

	P3DS_GPUTexture *texture = (P3DS_GPUTexture *)calloc(1, sizeof(P3DS_GPUTexture));
	if (!texture) {
		return NULL;
	}

	texture->width = width;
	texture->height = height;
	texture->texWidth = texWidth;
	texture->texHeight = texHeight;
	texture->pagesX = (texWidth + 1023) / 1024;
	texture->pagesY = (texHeight + 1023) / 1024;
	if (texture->pagesX < 1) texture->pagesX = 1;
	if (texture->pagesY < 1) texture->pagesY = 1;
	if (texture->pagesX * texture->pagesY > 4) {
		free(texture);
		return NULL;
	}

	for (int py = 0; py < texture->pagesY; ++py) {
		for (int px = 0; px < texture->pagesX; ++px) {
			const int page = py * texture->pagesX + px;
			const int pageW = texWidth / texture->pagesX;
			const int pageH = texHeight / texture->pagesY;
			uint8_t *pageData = (uint8_t *)malloc((size_t)pageW * (size_t)pageH * 4);
			if (!pageData) {
				P3DS_GPU_DestroyTexture(texture);
				return NULL;
			}
			for (int y = 0; y < pageH; ++y) {
				for (int x = 0; x < pageW; ++x) {
					const int srcX = px * pageW + x;
					const int srcY = py * pageH + y;
					const uint8_t *src = (const uint8_t *)tiledRGBA8 + tiled_offset(srcX, srcY, texWidth) * 4;
					uint8_t *dst = pageData + tiled_offset(x, y, pageW) * 4;
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
					dst[3] = src[3];
				}
			}

			if (!C3D_TexInit(&texture->tex[page], (u16)pageW, (u16)pageH, GPU_RGBA8)) {
				free(pageData);
				P3DS_GPU_DestroyTexture(texture);
				return NULL;
			}
			texture->texValid[page] = true;
			C3D_TexSetFilter(&texture->tex[page], GPU_NEAREST, GPU_NEAREST);
			C3D_TexSetWrap(&texture->tex[page], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
			C3D_TexUpload(&texture->tex[page], pageData);
			C3D_TexFlush(&texture->tex[page]);
			free(pageData);
		}
	}
	return texture;
}

void P3DS_GPU_DestroyTexture(P3DS_GPUTexture *texture) {
	if (!texture) {
		return;
	}
	for (int i = 0; i < 4; ++i) {
		if (texture->texValid[i]) {
			C3D_TexDelete(&texture->tex[i]);
		}
	}
	free(texture);
}

bool P3DS_GPU_DrawTriangle(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2) {
	if (!frame_active || !white_texture) {
		return false;
	}
	return P3DS_GPU_DrawTexturedTriangle(white_texture,
		x0, y0, 0.5f, 0.5f, c0,
		x1, y1, 0.5f, 0.5f, c1,
		x2, y2, 0.5f, 0.5f, c2);
}

bool P3DS_GPU_DrawRectangle(float x, float y, float w, float h, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3) {
	if (!frame_active || !white_texture) {
		return false;
	}
	return P3DS_GPU_DrawImage(white_texture, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, c0, c1, c2, c3);
}

bool P3DS_GPU_DrawImage(P3DS_GPUTexture *texture, float x, float y, float w, float h,
	float u0, float v0, float u1, float v1, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3) {
	if (!frame_active || !texture || u1 <= u0 || v1 <= v0) {
		return false;
	}

	const float x0 = x;
	const float y0 = y;
	const float x1 = x + w;
	const float y1 = y + h;
	const float cx0 = x0 > (float)clip_x ? x0 : (float)clip_x;
	const float cy0 = y0 > (float)clip_y ? y0 : (float)clip_y;
	const float cx1 = x1 < (float)(clip_x + clip_w) ? x1 : (float)(clip_x + clip_w);
	const float cy1 = y1 < (float)(clip_y + clip_h) ? y1 : (float)(clip_y + clip_h);
	if (cx1 <= cx0 || cy1 <= cy0) {
		return true;
	}

	const float invW = w != 0.0f ? 1.0f / w : 0.0f;
	const float invH = h != 0.0f ? 1.0f / h : 0.0f;
	const float cu0 = u0 + (cx0 - x0) * invW * (u1 - u0);
	const float cu1 = u0 + (cx1 - x0) * invW * (u1 - u0);
	const float cv0 = v0 + (cy0 - y0) * invH * (v1 - v0);
	const float cv1 = v0 + (cy1 - y0) * invH * (v1 - v0);

	const int pageX0 = clamp_page_coord(cu0, texture->width, texture->pagesX);
	const int pageX1 = clamp_page_coord(cu1 - 0.5f / (float)texture->width, texture->width, texture->pagesX);
	const int pageY0 = clamp_page_coord(cv0, texture->height, texture->pagesY);
	const int pageY1 = clamp_page_coord(cv1 - 0.5f / (float)texture->height, texture->height, texture->pagesY);
	if (pageX0 == pageX1 && pageY0 == pageY1) {
		const int page = pageY0 * texture->pagesX + pageX0;
		if (!begin_textured_batch(texture, page)) {
			return false;
		}
		send_textured_vertex(texture, pageX0, pageY0, cx0, cy0, cu0, cv0, c0);
		send_textured_vertex(texture, pageX0, pageY0, cx1, cy0, cu1, cv0, c1);
		send_textured_vertex(texture, pageX0, pageY0, cx0, cy1, cu0, cv1, c2);
		send_textured_vertex(texture, pageX0, pageY0, cx1, cy0, cu1, cv0, c1);
		send_textured_vertex(texture, pageX0, pageY0, cx1, cy1, cu1, cv1, c3);
		send_textured_vertex(texture, pageX0, pageY0, cx0, cy1, cu0, cv1, c2);
		return true;
	}

	const bool a = P3DS_GPU_DrawTexturedTriangle(texture,
		cx0, cy0, cu0, cv0, c0,
		cx1, cy0, cu1, cv0, c1,
		cx0, cy1, cu0, cv1, c2);
	const bool b = P3DS_GPU_DrawTexturedTriangle(texture,
		cx1, cy0, cu1, cv0, c1,
		cx1, cy1, cu1, cv1, c3,
		cx0, cy1, cu0, cv1, c2);
	return a || b;
}

bool P3DS_GPU_DrawTexturedTriangle(P3DS_GPUTexture *texture,
	float x0, float y0, float u0, float v0, uint32_t c0,
	float x1, float y1, float u1, float v1, uint32_t c1,
	float x2, float y2, float u2, float v2, uint32_t c2) {
	if (!texture) {
		return false;
	}
	const int pageX0 = clamp_page_coord(u0, texture->width, texture->pagesX);
	const int pageX1 = clamp_page_coord(u1, texture->width, texture->pagesX);
	const int pageX2 = clamp_page_coord(u2, texture->width, texture->pagesX);
	const int pageY0 = clamp_page_coord(v0, texture->height, texture->pagesY);
	const int pageY1 = clamp_page_coord(v1, texture->height, texture->pagesY);
	const int pageY2 = clamp_page_coord(v2, texture->height, texture->pagesY);
	if (pageX0 != pageX1 || pageX0 != pageX2 || pageY0 != pageY1 || pageY0 != pageY2) {
		const int minPageX = pageX0 < pageX1 ? (pageX0 < pageX2 ? pageX0 : pageX2) : (pageX1 < pageX2 ? pageX1 : pageX2);
		const int maxPageX = pageX0 > pageX1 ? (pageX0 > pageX2 ? pageX0 : pageX2) : (pageX1 > pageX2 ? pageX1 : pageX2);
		const int minPageY = pageY0 < pageY1 ? (pageY0 < pageY2 ? pageY0 : pageY2) : (pageY1 < pageY2 ? pageY1 : pageY2);
		const int maxPageY = pageY0 > pageY1 ? (pageY0 > pageY2 ? pageY0 : pageY2) : (pageY1 > pageY2 ? pageY1 : pageY2);
		P3DS_TriVert tri[3];
		tri[0] = make_tri_vert(x0, y0, u0, v0, c0);
		tri[1] = make_tri_vert(x1, y1, u1, v1, c1);
		tri[2] = make_tri_vert(x2, y2, u2, v2, c2);
		bool drew = false;
		for (int py = minPageY; py <= maxPageY; ++py) {
			for (int px = minPageX; px <= maxPageX; ++px) {
				drew = draw_clipped_textured_triangle_page(texture, px, py, tri) || drew;
			}
		}
		return drew;
	}
	const int page = pageY0 * texture->pagesX + pageX0;
	if (!begin_textured_batch(texture, page)) {
		return false;
	}

	send_textured_vertex(texture, pageX0, pageY0, x0, y0, u0, v0, c0);
	send_textured_vertex(texture, pageX0, pageY0, x1, y1, u1, v1, c1);
	send_textured_vertex(texture, pageX0, pageY0, x2, y2, u2, v2, c2);
	return true;
}
