#include "Platform3DS.h"

#include <3ds.h>
#include <string.h>

static PrintConsole bottom_console;
static bool romfs_ready;

static uint32_t ConvertKeys(uint32_t keys) {
	uint32_t out = 0;
	if (keys & KEY_A) out |= P3DS_KEY_A;
	if (keys & KEY_B) out |= P3DS_KEY_B;
	if (keys & KEY_X) out |= P3DS_KEY_X;
	if (keys & KEY_Y) out |= P3DS_KEY_Y;
	if (keys & KEY_L) out |= P3DS_KEY_L;
	if (keys & KEY_R) out |= P3DS_KEY_R;
	if (keys & KEY_START) out |= P3DS_KEY_START;
	if (keys & KEY_SELECT) out |= P3DS_KEY_SELECT;
	if (keys & KEY_DUP) out |= P3DS_KEY_DUP;
	if (keys & KEY_DDOWN) out |= P3DS_KEY_DDOWN;
	if (keys & KEY_DLEFT) out |= P3DS_KEY_DLEFT;
	if (keys & KEY_DRIGHT) out |= P3DS_KEY_DRIGHT;
	return out;
}

bool P3DS_Init(void) {
	gfxInitDefault();
	gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
	gfxSetDoubleBuffering(GFX_TOP, true);
	consoleInit(GFX_BOTTOM, &bottom_console);
	return true;
}

void P3DS_Shutdown(void) {
	gfxExit();
}

bool P3DS_AptMainLoop(void) {
	return aptMainLoop();
}

bool P3DS_InitRomFS(void) {
	if (romfsInit() == 0) {
		romfs_ready = true;
		return true;
	}
	return false;
}

void P3DS_ExitRomFS(void) {
	if (romfs_ready) {
		romfsExit();
		romfs_ready = false;
	}
}

void P3DS_BottomClear(void) {
	consoleSelect(&bottom_console);
	consoleClear();
}

uint8_t *P3DS_GetTopFramebuffer(void) {
	uint16_t width = 0;
	uint16_t height = 0;
	(void)width;
	(void)height;
	return gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &width, &height);
}

void P3DS_Present(void) {
	gfxFlushBuffers();
	gfxSwapBuffers();
}

void P3DS_WaitVBlank(void) {
	gspWaitForVBlank();
}

void P3DS_DebugString(const char *message) {
	if (message) {
		svcOutputDebugString(message, strlen(message));
	}
}

void P3DS_ScanInput(void) {
	hidScanInput();
}

uint32_t P3DS_KeysDown(void) {
	return ConvertKeys(hidKeysDown());
}

uint32_t P3DS_KeysUp(void) {
	return ConvertKeys(hidKeysUp());
}

uint32_t P3DS_KeysHeld(void) {
	return ConvertKeys(hidKeysHeld());
}

void P3DS_ReadCircle(int *dx, int *dy) {
	circlePosition pos;
	hidCircleRead(&pos);
	if (dx) *dx = pos.dx;
	if (dy) *dy = pos.dy;
}
