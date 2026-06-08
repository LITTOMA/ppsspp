#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "Common/Audio/AudioBackend.h"
#include "Common/File/Path.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "3DS/GraphicsContext3DS.h"
#include "Platform3DS.h"

static constexpr int TOP_W = 400;
static constexpr int TOP_H = 240;

static bool g_exitRequested = false;
static bool g_romfsReady = false;

static void RuntimeLog(const char *fmt, ...) {
	char line[1024]{};
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);

	P3DS_DebugString(line);
	P3DS_DebugString("\n");

	mkdir("sdmc:/3ds", 0755);
	mkdir("sdmc:/3ds/PPSSPP", 0755);
	int fd = open("sdmc:/3ds/PPSSPP/runtime.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0) {
		return;
	}
	write(fd, line, strlen(line));
	write(fd, "\n", 1);
	close(fd);
}

static bool EnsureDirectory(const std::string &path) {
	if (path.empty()) {
		return false;
	}

	std::string current;
	size_t start = 0;
	const size_t colon = path.find(':');
	if (colon != std::string::npos && colon + 1 < path.size() && path[colon + 1] == '/') {
		current = path.substr(0, colon + 1);
		start = colon + 2;
	} else if (path.front() == '/') {
		current = "/";
		start = 1;
	}

	while (start < path.size()) {
		const size_t end = path.find('/', start);
		const std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
		if (!part.empty()) {
			if (current.empty() || current == "/") {
				current += part;
			} else {
				current += "/";
				current += part;
			}
			if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
				return false;
			}
		}
		if (end == std::string::npos) {
			break;
		}
		start = end + 1;
	}

	return true;
}

static void Ensure3DSDirectories() {
	EnsureDirectory("sdmc:/3ds/PPSSPP/cache");
	EnsureDirectory("sdmc:/PSP/GAME");
	EnsureDirectory("sdmc:/PSP/SAVEDATA");
	EnsureDirectory("sdmc:/PSP/SYSTEM");
	EnsureDirectory("sdmc:/ISO");
}

static void ProbeRomFSFile(const char *path) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		RuntimeLog("romfs-probe: open %s failed errno=%d", path, errno);
		return;
	}

	struct stat st {};
	if (fstat(fd, &st) == 0) {
		RuntimeLog("romfs-probe: %s size=%lld", path, (long long)st.st_size);
	} else {
		RuntimeLog("romfs-probe: fstat %s failed errno=%d", path, errno);
	}

	unsigned char buf[4096];
	ssize_t got = read(fd, buf, 32);
	if (got > 0) {
		char hex[3 * 32 + 1] {};
		for (ssize_t i = 0; i < got && i < 32; ++i) {
			snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02x ", buf[i]);
		}
		RuntimeLog("romfs-probe: first %d bytes %s", (int)got, hex);
	} else {
		RuntimeLog("romfs-probe: initial read failed got=%d errno=%d", (int)got, errno);
	}

	if (lseek(fd, 0, SEEK_SET) >= 0) {
		uint32_t fnv = 2166136261u;
		long long total = 0;
		for (;;) {
			got = read(fd, buf, sizeof(buf));
			if (got <= 0) {
				break;
			}
			total += got;
			for (ssize_t i = 0; i < got; ++i) {
				fnv ^= buf[i];
				fnv *= 16777619u;
			}
		}
		RuntimeLog("romfs-probe: read_total=%lld fnv1a=%08x final=%d errno=%d", total, fnv, (int)got, errno);
	} else {
		RuntimeLog("romfs-probe: lseek reset failed errno=%d", errno);
	}

	close(fd);
}

static void ProbeVFSFile(const char *path) {
	size_t size = 0;
	uint8_t *data = g_VFS.ReadFile(path, &size);
	if (!data) {
		RuntimeLog("vfs-probe: %s missing", path);
		return;
	}
	uint32_t fnv = 2166136261u;
	for (size_t i = 0; i < size; ++i) {
		fnv ^= data[i];
		fnv *= 16777619u;
	}
	RuntimeLog("vfs-probe: %s size=%u fnv1a=%08x first=%02x%02x%02x%02x", path, (unsigned)size, fnv,
		size > 0 ? data[0] : 0, size > 1 ? data[1] : 0, size > 2 ? data[2] : 0, size > 3 ? data[3] : 0);
	delete[] data;
}

static void SendKey(uint32_t p3dsKey, InputKeyCode nativeKey, uint32_t down, uint32_t up) {
	if (down & p3dsKey) {
		NativeKey(KeyInput(DEVICE_ID_PAD_0, nativeKey, KeyInputFlags::DOWN));
	}
	if (up & p3dsKey) {
		NativeKey(KeyInput(DEVICE_ID_PAD_0, nativeKey, KeyInputFlags::UP));
	}
}

static void Pump3DSInput() {
	P3DS_ScanInput();
	const uint32_t down = P3DS_KeysDown();
	const uint32_t up = P3DS_KeysUp();
	const uint32_t held = P3DS_KeysHeld();
	const bool inGame = GetUIState() == UISTATE_INGAME;

	SendKey(P3DS_KEY_DUP, NKCODE_DPAD_UP, down, up);
	SendKey(P3DS_KEY_DDOWN, NKCODE_DPAD_DOWN, down, up);
	SendKey(P3DS_KEY_DLEFT, NKCODE_DPAD_LEFT, down, up);
	SendKey(P3DS_KEY_DRIGHT, NKCODE_DPAD_RIGHT, down, up);
	SendKey(P3DS_KEY_A, inGame ? NKCODE_BUTTON_A : NKCODE_DPAD_CENTER, down, up);
	SendKey(P3DS_KEY_B, inGame ? NKCODE_BUTTON_B : NKCODE_BACK, down, up);
	SendKey(P3DS_KEY_X, NKCODE_BUTTON_X, down, up);
	SendKey(P3DS_KEY_Y, NKCODE_BUTTON_Y, down, up);
	SendKey(P3DS_KEY_L, NKCODE_BUTTON_L1, down, up);
	SendKey(P3DS_KEY_R, NKCODE_BUTTON_R1, down, up);
	SendKey(P3DS_KEY_SELECT, NKCODE_BUTTON_SELECT, down, up);
	SendKey(P3DS_KEY_START, NKCODE_BUTTON_START, down, up);

	if ((held & (P3DS_KEY_START | P3DS_KEY_SELECT)) == (P3DS_KEY_START | P3DS_KEY_SELECT) && (down & (P3DS_KEY_START | P3DS_KEY_SELECT))) {
		g_exitRequested = true;
	}

	int dx = 0;
	int dy = 0;
	P3DS_ReadCircle(&dx, &dy);
	AxisInput axes[2]{};
	axes[0].deviceId = DEVICE_ID_PAD_0;
	axes[0].axisId = JOYSTICK_AXIS_X;
	axes[0].value = std::max(-1.0f, std::min(1.0f, (float)dx / 156.0f));
	axes[1].deviceId = DEVICE_ID_PAD_0;
	axes[1].axisId = JOYSTICK_AXIS_Y;
	axes[1].value = std::max(-1.0f, std::min(1.0f, (float)-dy / 156.0f));
	NativeAxis(axes, 2);
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	if (!P3DS_Init()) {
		return 1;
	}

	Ensure3DSDirectories();
	remove("sdmc:/3ds/PPSSPP/runtime.log");
	RuntimeLog("init: ppsspp official ui host");

	g_romfsReady = P3DS_InitRomFS();
	RuntimeLog("init: romfs %s", g_romfsReady ? "ready" : "failed");
	if (g_romfsReady) {
		ProbeRomFSFile("romfs:/font_atlas.zim");
	}

	const char *nativeArgv[] = { "PPSSPP.3dsx" };
	NativeInit(1, nativeArgv, "sdmc:/3ds/PPSSPP", "romfs:/", "sdmc:/3ds/PPSSPP/cache");
	g_Config.iGPUBackend = (int)GPUBackend::NINTENDO_3DS;
	g_Config.sFailedGPUBackends.clear();
	SetGPUBackend(GPUBackend::NINTENDO_3DS, "citro3d");
	RuntimeLog("init: backend %s device=%s", GPUBackendToString(GetGPUBackend()).c_str(), GetGPUBackendDevice().c_str());
	ProbeVFSFile("asciifont_atlas.zim");
	ProbeVFSFile("asciifont_atlas.meta");
	ProbeVFSFile("ui_images/images.svg");
	ProbeVFSFile("ui_images/bg.png");
	ProbeVFSFile("Roboto_Condensed-Regular.ttf");
	g_Config.iUIScaleFactor = -8;
	Native_UpdateScreenScale(TOP_W, TOP_H, 1.0f);

	GraphicsContext3DS graphics;
	if (!NativeInitGraphics(&graphics)) {
		RuntimeLog("error: NativeInitGraphics failed");
		NativeShutdown();
		graphics.Shutdown();
		if (g_romfsReady) {
			P3DS_ExitRomFS();
		}
		P3DS_Shutdown();
		return 2;
	}

	RuntimeLog("init: NativeInitGraphics ok");
	while (P3DS_AptMainLoop() && !g_exitRequested) {
		Pump3DSInput();
		NativeFrame(&graphics);
	}

	RuntimeLog("shutdown");
	NativeShutdownGraphics();
	NativeShutdown();
	graphics.Shutdown();
	if (g_romfsReady) {
		P3DS_ExitRomFS();
	}
	P3DS_Shutdown();
	return 0;
}

AudioBackend *System_CreateAudioBackend() {
	return nullptr;
}

void System_Toast(std::string_view text) {
	RuntimeLog("toast: %.*s", (int)text.size(), text.data());
}

void System_ShowKeyboard() {
}

void System_Vibrate(int length_ms) {
	(void)length_ms;
}

void System_LaunchUrl(LaunchUrlType urlType, std::string_view url) {
	(void)urlType;
	RuntimeLog("launch-url ignored: %.*s", (int)url.size(), url.data());
}

bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int64_t param3, int64_t param4) {
	(void)requestId;
	(void)param1;
	(void)param2;
	(void)param3;
	(void)param4;
	if (type == SystemRequestType::EXIT_APP) {
		g_exitRequested = true;
		return true;
	}
	return false;
}

PermissionStatus System_GetPermissionStatus(SystemPermission permission) {
	(void)permission;
	return PERMISSION_STATUS_GRANTED;
}

void System_AskForPermission(SystemPermission permission) {
	(void)permission;
}

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
		return "Nintendo 3DS";
	case SYSPROP_SYSTEMBUILD:
		return "libctru";
	case SYSPROP_LANGREGION:
		return "en_US";
	case SYSPROP_CPUINFO:
		return "ARM11 MPCore";
	case SYSPROP_BOARDNAME:
		return "CTR";
	case SYSPROP_BUILD_VERSION:
		return PPSSPP_GIT_VERSION;
	case SYSPROP_USER_DOCUMENTS_DIR:
		return "sdmc:/";
	case SYSPROP_GPUDRIVER_VERSION:
		return "3DS software thin3d";
	case SYSPROP_COMPUTER_NAME:
		return "Nintendo 3DS";
	default:
		return "";
	}
}

std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) {
	(void)prop;
	return {};
}

int64_t System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_SYSTEMVERSION:
		return 11;
	case SYSPROP_DISPLAY_XRES:
		return TOP_W;
	case SYSPROP_DISPLAY_YRES:
		return TOP_H;
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60;
	case SYSPROP_DISPLAY_LOGICAL_DPI:
	case SYSPROP_DISPLAY_DPI:
		return 96;
	case SYSPROP_DISPLAY_COUNT:
		return 1;
	case SYSPROP_DEVICE_TYPE:
		return DEVICE_TYPE_MOBILE;
	case SYSPROP_AUDIO_SAMPLE_RATE:
	case SYSPROP_AUDIO_OPTIMAL_SAMPLE_RATE:
		return 44100;
	case SYSPROP_AUDIO_FRAMES_PER_BUFFER:
	case SYSPROP_AUDIO_OPTIMAL_FRAMES_PER_BUFFER:
		return 512;
	case SYSPROP_KEYBOARD_LAYOUT:
		return KEYBOARD_LAYOUT_QWERTY;
	case SYSPROP_BATTERY_PERCENTAGE:
		return -1;
	default:
		return -1;
	}
}

float System_GetPropertyFloat(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60.0f;
	case SYSPROP_DISPLAY_SAFE_INSET_LEFT:
	case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
	case SYSPROP_DISPLAY_SAFE_INSET_TOP:
	case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM:
	case SYSPROP_DISPLAY_HAS_CAMERA_CUTOUT:
		return 0.0f;
	default:
		return -1.0f;
	}
}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
	case SYSPROP_CAN_JIT:
	case SYSPROP_SKIP_UI:
	case SYSPROP_SUPPORTS_HTTPS:
	case SYSPROP_HAS_FILE_BROWSER:
	case SYSPROP_HAS_FOLDER_BROWSER:
	case SYSPROP_HAS_IMAGE_BROWSER:
	case SYSPROP_HAS_KEYBOARD:
	case SYSPROP_KEYBOARD_IS_SOFT:
	case SYSPROP_HAS_ACCELEROMETER:
	case SYSPROP_HAS_OPEN_DIRECTORY:
	case SYSPROP_HAS_LOGIN_DIALOG:
	case SYSPROP_HAS_TEXT_INPUT_DIALOG:
	case SYSPROP_HAS_TEXT_CLIPBOARD:
	case SYSPROP_CAN_CREATE_SHORTCUT:
	case SYSPROP_CAN_SHOW_FILE:
	case SYSPROP_SUPPORTS_PERMISSIONS:
	case SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE:
	case SYSPROP_SUPPORTS_OPEN_FILE_IN_EDITOR:
	case SYSPROP_ANDROID_SCOPED_STORAGE:
	case SYSPROP_DEBUGGER_PRESENT:
	case SYSPROP_HAS_DEBUGGER:
	case SYSPROP_LIMITED_FILE_BROWSING:
	case SYSPROP_OK_BUTTON_LEFT:
	case SYSPROP_CAN_READ_BATTERY_PERCENTAGE:
	case SYSPROP_ENOUGH_RAM_FOR_FULL_ISO:
	case SYSPROP_HAS_TRASH_BIN:
	case SYSPROP_USE_IAP:
	case SYSPROP_USE_APP_STORE:
	case SYSPROP_SUPPORTS_SHARE_TEXT:
		return false;
	default:
		return false;
	}
}

void System_Notify(SystemNotification notification) {
	(void)notification;
}

std::vector<std::string> System_GetCameraDeviceList() {
	return {};
}

bool System_AudioRecordingIsAvailable() {
	return false;
}

bool System_AudioRecordingState() {
	return false;
}
