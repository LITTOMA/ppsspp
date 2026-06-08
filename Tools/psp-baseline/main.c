#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspaudiolib.h>
#include <pspaudio.h>
#include <psptypes.h>

#include <stdlib.h>

PSP_MODULE_INFO("PPSSPP3DSBASE", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define BUF_WIDTH 512
#define SCR_WIDTH 480
#define SCR_HEIGHT 272
#define RECT_W 68.0f
#define RECT_H 44.0f

typedef struct {
	unsigned int color;
	float x;
	float y;
	float z;
} ColorVertex;

typedef struct {
	short left;
	short right;
} AudioSample;

static unsigned int __attribute__((aligned(16))) guList[262144];
static volatile int exitRequested = 0;
static volatile int audioEnabled = 1;
static volatile unsigned int audioPhase = 0;
static unsigned int vramOffset = 0;

static int ExitCallback(int arg1, int arg2, void *common) {
	(void)arg1;
	(void)arg2;
	(void)common;
	exitRequested = 1;
	return 0;
}

static int CallbackThread(SceSize args, void *argp) {
	(void)args;
	(void)argp;

	int cbid = sceKernelCreateCallback("Exit Callback", ExitCallback, NULL);
	sceKernelRegisterExitCallback(cbid);
	sceKernelSleepThreadCB();
	return 0;
}

static void SetupCallbacks(void) {
	int thid = sceKernelCreateThread("callback_thread", CallbackThread, 0x11, 0xFA0, 0, 0);
	if (thid >= 0) {
		sceKernelStartThread(thid, 0, 0);
	}
}

static unsigned int PsmBytesPerPixel(int psm) {
	switch (psm) {
	case GU_PSM_5650:
	case GU_PSM_5551:
	case GU_PSM_4444:
	case GU_PSM_T16:
		return 2;
	case GU_PSM_8888:
	default:
		return 4;
	}
}

static void *GetStaticVramBuffer(unsigned int width, unsigned int height, int psm) {
	unsigned int size = width * height * PsmBytesPerPixel(psm);
	void *ptr = (void *)vramOffset;
	vramOffset += (size + 15) & ~15;
	return ptr;
}

static void AudioCallback(void *buf, unsigned int length, void *userdata) {
	(void)userdata;

	AudioSample *samples = (AudioSample *)buf;
	for (unsigned int i = 0; i < length; ++i) {
		short sample = 0;
		if (audioEnabled) {
			sample = (audioPhase & 64) ? 1200 : -1200;
			audioPhase++;
		}
		samples[i].left = sample;
		samples[i].right = sample;
	}
}

static void InitGu(void **drawBuffer) {
	sceGuInit();

	void *fbp0 = GetStaticVramBuffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_8888);
	void *fbp1 = GetStaticVramBuffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_8888);
	void *zbp = GetStaticVramBuffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_4444);
	*drawBuffer = fbp0;

	sceGuStart(GU_DIRECT, guList);
	sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_WIDTH);
	sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, fbp1, BUF_WIDTH);
	sceGuDepthBuffer(zbp, BUF_WIDTH);
	sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
	sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
	sceGuDepthRange(65535, 0);
	sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuDisable(GU_DEPTH_TEST);
	sceGuShadeModel(GU_SMOOTH);
	sceGuFinish();
	sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);

	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);
}

static void ClampPosition(float *x, float *y) {
	if (*x < 0.0f) {
		*x = 0.0f;
	}
	if (*y < 52.0f) {
		*y = 52.0f;
	}
	if (*x > SCR_WIDTH - RECT_W) {
		*x = SCR_WIDTH - RECT_W;
	}
	if (*y > SCR_HEIGHT - RECT_H) {
		*y = SCR_HEIGHT - RECT_H;
	}
}

static unsigned int RectColor(unsigned int buttons, unsigned int frame) {
	if (buttons & PSP_CTRL_CROSS) {
		return 0xff44ccff;
	}
	if (buttons & PSP_CTRL_CIRCLE) {
		return 0xff4466ff;
	}
	if (buttons & PSP_CTRL_SQUARE) {
		return 0xffffaa44;
	}
	if (buttons & PSP_CTRL_TRIANGLE) {
		return 0xff66ff66;
	}
	return 0xff40c0ff | ((frame & 0x3f) << 16);
}

static void PrintButton(const char *name, unsigned int buttons, unsigned int mask) {
	pspDebugScreenPrintf("%s:%c ", name, (buttons & mask) ? '1' : '.');
}

int main(void) {
	SetupCallbacks();
	pspDebugScreenInit();

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

	pspAudioInit();
	pspAudioSetChannelCallback(0, AudioCallback, NULL);

	void *drawBuffer = NULL;
	InitGu(&drawBuffer);

	float rectX = 206.0f;
	float rectY = 132.0f;
	int autoMove = 1;
	unsigned int frame = 0;
	unsigned int oldButtons = 0;

	while (!exitRequested) {
		SceCtrlData pad;
		sceCtrlReadBufferPositive(&pad, 1);
		unsigned int pressed = pad.Buttons & ~oldButtons;

		if (pressed & PSP_CTRL_TRIANGLE) {
			audioEnabled = !audioEnabled;
		}
		if (pressed & PSP_CTRL_START) {
			autoMove = !autoMove;
		}
		if (pressed & PSP_CTRL_SELECT) {
			rectX = 206.0f;
			rectY = 132.0f;
		}

		float dx = 0.0f;
		float dy = 0.0f;
		int analogX = (int)pad.Lx - 128;
		int analogY = (int)pad.Ly - 128;
		if (analogX < -18 || analogX > 18) {
			dx += analogX / 34.0f;
		}
		if (analogY < -18 || analogY > 18) {
			dy += analogY / 34.0f;
		}
		if (pad.Buttons & PSP_CTRL_LEFT) {
			dx -= 2.0f;
		}
		if (pad.Buttons & PSP_CTRL_RIGHT) {
			dx += 2.0f;
		}
		if (pad.Buttons & PSP_CTRL_UP) {
			dy -= 2.0f;
		}
		if (pad.Buttons & PSP_CTRL_DOWN) {
			dy += 2.0f;
		}
		if (autoMove) {
			dx += ((frame / 45) & 1) ? -0.35f : 0.35f;
		}

		rectX += dx;
		rectY += dy;
		ClampPosition(&rectX, &rectY);

		sceGuStart(GU_DIRECT, guList);
		sceGuClearColor(0xff201408);
		sceGuClearDepth(0);
		sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

		ColorVertex *vertices = (ColorVertex *)sceGuGetMemory(2 * sizeof(ColorVertex));
		vertices[0].color = RectColor(pad.Buttons, frame);
		vertices[0].x = rectX;
		vertices[0].y = rectY;
		vertices[0].z = 0.0f;
		vertices[1].color = 0xffffffff;
		vertices[1].x = rectX + RECT_W;
		vertices[1].y = rectY + RECT_H;
		vertices[1].z = 0.0f;
		sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, vertices);

		sceGuFinish();
		sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);

		pspDebugScreenSetOffset((int)drawBuffer);
		pspDebugScreenSetBackColor(0xff201408);
		pspDebugScreenSetTextColor(0xffffffff);
		pspDebugScreenSetXY(0, 0);
		pspDebugScreenPrintf("PPSSPP 3DS PSP BASELINE\n");
		pspDebugScreenPrintf("frame:%u  buttons:0x%08x  audio:%s  auto:%s\n",
			frame, pad.Buttons, audioEnabled ? "on " : "off", autoMove ? "on " : "off");
		pspDebugScreenPrintf("analog Lx:%3d Ly:%3d  rect:%3d,%3d\n", pad.Lx, pad.Ly, (int)rectX, (int)rectY);
		PrintButton("UP", pad.Buttons, PSP_CTRL_UP);
		PrintButton("DN", pad.Buttons, PSP_CTRL_DOWN);
		PrintButton("LT", pad.Buttons, PSP_CTRL_LEFT);
		PrintButton("RT", pad.Buttons, PSP_CTRL_RIGHT);
		pspDebugScreenPrintf("\n");
		PrintButton("X", pad.Buttons, PSP_CTRL_CROSS);
		PrintButton("O", pad.Buttons, PSP_CTRL_CIRCLE);
		PrintButton("SQ", pad.Buttons, PSP_CTRL_SQUARE);
		PrintButton("TR", pad.Buttons, PSP_CTRL_TRIANGLE);
		PrintButton("L", pad.Buttons, PSP_CTRL_LTRIGGER);
		PrintButton("R", pad.Buttons, PSP_CTRL_RTRIGGER);
		pspDebugScreenPrintf("\nSTART:auto  SELECT:center  TRIANGLE:audio\n");

		sceDisplayWaitVblankStart();
		drawBuffer = sceGuSwapBuffers();
		oldButtons = pad.Buttons;
		frame++;
	}

	pspAudioEnd();
	sceGuTerm();
	sceKernelExitGame();
	return 0;
}
