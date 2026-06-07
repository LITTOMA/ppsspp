#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct P3DS_GPUTexture P3DS_GPUTexture;

bool P3DS_GPU_Init(void);
void P3DS_GPU_Shutdown(void);
void P3DS_GPU_BeginFrame(uint32_t clearColor);
void P3DS_GPU_EndFrame(void);
void P3DS_GPU_SetScissor(int x, int y, int w, int h);
void P3DS_GPU_Clear(uint32_t color);

P3DS_GPUTexture *P3DS_GPU_CreateTexture(int width, int height, int texWidth, int texHeight, const void *tiledRGBA8);
void P3DS_GPU_DestroyTexture(P3DS_GPUTexture *texture);

bool P3DS_GPU_DrawTriangle(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float x2, float y2, uint32_t c2);
bool P3DS_GPU_DrawRectangle(float x, float y, float w, float h, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3);
bool P3DS_GPU_DrawImage(P3DS_GPUTexture *texture, float x, float y, float w, float h,
	float u0, float v0, float u1, float v1, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3);
bool P3DS_GPU_DrawTexturedTriangle(P3DS_GPUTexture *texture,
	float x0, float y0, float u0, float v0, uint32_t c0,
	float x1, float y1, float u1, float v1, uint32_t c1,
	float x2, float y2, float u2, float v2, uint32_t c2);

#ifdef __cplusplus
}
#endif
