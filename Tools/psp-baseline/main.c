#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>

PSP_MODULE_INFO("PPSSPP3DSBASE", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(1024);

static const struct {
	const char *name;
	unsigned int mask;
} buttonNames[] = {
	{ "UP", PSP_CTRL_UP },
	{ "DOWN", PSP_CTRL_DOWN },
	{ "LEFT", PSP_CTRL_LEFT },
	{ "RIGHT", PSP_CTRL_RIGHT },
	{ "CROSS", PSP_CTRL_CROSS },
	{ "CIRCLE", PSP_CTRL_CIRCLE },
	{ "SQUARE", PSP_CTRL_SQUARE },
	{ "TRIANGLE", PSP_CTRL_TRIANGLE },
	{ "L", PSP_CTRL_LTRIGGER },
	{ "R", PSP_CTRL_RTRIGGER },
	{ "START", PSP_CTRL_START },
	{ "SELECT", PSP_CTRL_SELECT },
};

static void PrintButtonState(unsigned int buttons) {
	for (unsigned int i = 0; i < sizeof(buttonNames) / sizeof(buttonNames[0]); ++i) {
		pspDebugScreenPrintf("%-8s %s\n", buttonNames[i].name, (buttons & buttonNames[i].mask) ? "ON" : ".");
	}
}

static void PrintBar(const char *label, int value, int minValue, int maxValue, int width) {
	if (value < minValue) {
		value = minValue;
	}
	if (value > maxValue) {
		value = maxValue;
	}

	int range = maxValue - minValue;
	int pos = range > 0 ? ((value - minValue) * width) / range : 0;
	if (pos >= width) {
		pos = width - 1;
	}

	pspDebugScreenPrintf("%s [", label);
	for (int i = 0; i < width; ++i) {
		pspDebugScreenPrintf(i == pos ? "|" : "-");
	}
	pspDebugScreenPrintf("] %d\n", value);
}

static void PrintSpinner(unsigned int frame) {
	static const char spinner[] = "|/-\\";
	int pos = (frame / 4) % 40;

	pspDebugScreenPrintf("heartbeat %c [", spinner[(frame / 8) & 3]);
	for (int i = 0; i < 40; ++i) {
		pspDebugScreenPrintf(i == pos ? "*" : ".");
	}
	pspDebugScreenPrintf("]\n");
}

int main(void) {
	pspDebugScreenInit();
	pspDebugScreenSetBackColor(0xff000000);
	pspDebugScreenSetTextColor(0xffffffff);

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

	unsigned int frame = 0;
	unsigned int oldButtons = 0;
	unsigned int toggles = 0;

	for (;;) {
		SceCtrlData pad;
		sceCtrlReadBufferPositive(&pad, 1);

		unsigned int pressed = pad.Buttons & ~oldButtons;
		if (pressed & PSP_CTRL_TRIANGLE) {
			toggles++;
		}
		if ((pad.Buttons & (PSP_CTRL_START | PSP_CTRL_SELECT)) == (PSP_CTRL_START | PSP_CTRL_SELECT)) {
			break;
		}

		pspDebugScreenSetXY(0, 0);
		pspDebugScreenClear();
		pspDebugScreenPrintf("PPSSPP 3DS PSP BASELINE - STABLE\n");
		pspDebugScreenPrintf("frame        %u\n", frame);
		pspDebugScreenPrintf("buttons      0x%08x\n", pad.Buttons);
		pspDebugScreenPrintf("pressed      0x%08x\n", pressed);
		pspDebugScreenPrintf("triangle toggles %u\n", toggles);
		pspDebugScreenPrintf("exit         START+SELECT\n\n");

		PrintBar("analog lx", pad.Lx, 0, 255, 32);
		PrintBar("analog ly", pad.Ly, 0, 255, 32);
		PrintSpinner(frame);
		pspDebugScreenPrintf("\n");
		PrintButtonState(pad.Buttons);

		sceDisplayWaitVblankStart();
		oldButtons = pad.Buttons;
		frame++;
	}

	sceKernelExitGame();
	return 0;
}
