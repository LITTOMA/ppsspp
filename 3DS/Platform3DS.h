#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	P3DS_KEY_A = 1u << 0,
	P3DS_KEY_B = 1u << 1,
	P3DS_KEY_X = 1u << 2,
	P3DS_KEY_Y = 1u << 3,
	P3DS_KEY_L = 1u << 4,
	P3DS_KEY_R = 1u << 5,
	P3DS_KEY_START = 1u << 6,
	P3DS_KEY_SELECT = 1u << 7,
	P3DS_KEY_DUP = 1u << 8,
	P3DS_KEY_DDOWN = 1u << 9,
	P3DS_KEY_DLEFT = 1u << 10,
	P3DS_KEY_DRIGHT = 1u << 11,
};

bool P3DS_Init(void);
void P3DS_Shutdown(void);
bool P3DS_AptMainLoop(void);

bool P3DS_InitRomFS(void);
void P3DS_ExitRomFS(void);

void P3DS_BottomClear(void);
uint8_t *P3DS_GetTopFramebuffer(void);
void P3DS_Present(void);
void P3DS_WaitVBlank(void);
void P3DS_DebugString(const char *message);

void P3DS_ScanInput(void);
uint32_t P3DS_KeysDown(void);
uint32_t P3DS_KeysUp(void);
uint32_t P3DS_KeysHeld(void);
void P3DS_ReadCircle(int *dx, int *dy);

#ifdef __cplusplus
}
#endif
