#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "Common/CPUDetect.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/File/VFS/VFS.h"
#include "Common/GraphicsContext.h"
#include "Common/Log/LogManager.h"
#include "Common/Profiler/Profiler.h"
#include "Common/StringUtils.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/CoreParameter.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceUtility.h"
#include "Core/System.h"
#include "Core/Util/PathUtil.h"
#include "Platform3DS.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"

static constexpr int TOP_W = 400;
static constexpr int TOP_H = 240;
static constexpr int PSP_W = 480;
static constexpr int PSP_H = 272;
static constexpr int PSP_SCREEN_H_ON_3DS = 226;

static bool g_exitRequested = false;
static bool g_romfsReady = false;

struct BootEntry {
	std::string path;
	std::string label;
};

static void RuntimeLog(const char *fmt, ...) {
	char line[1024]{};
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);

	P3DS_DebugString(line);
	P3DS_DebugString("\n");

	int fd = open("sdmc:/3ds/PPSSPP/runtime.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0) {
		return;
	}

	write(fd, line, strlen(line));
	write(fd, "\n", 1);
	close(fd);
}

static std::string Lower(std::string value) {
	for (char &c : value) {
		c = (char)std::tolower((unsigned char)c);
	}
	return value;
}

static std::string Trim(std::string value) {
	while (!value.empty() && std::isspace((unsigned char)value.back())) {
		value.pop_back();
	}
	size_t first = 0;
	while (first < value.size() && std::isspace((unsigned char)value[first])) {
		++first;
	}
	return first == 0 ? value : value.substr(first);
}

static bool HasBootableExtension(const std::string &name) {
	const std::string lower = Lower(name);
	return endsWith(lower, ".pbp") || endsWith(lower, ".elf") || endsWith(lower, ".prx") ||
		endsWith(lower, ".iso") || endsWith(lower, ".cso");
}

static bool IsDirectory(const std::string &path) {
	struct stat st {};
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
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
	EnsureDirectory("sdmc:/PPSSPP");
}

static std::string BaseName(const std::string &path) {
	size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

static void ScanBootFiles(const std::string &dir, int depth, std::vector<BootEntry> *entries) {
	if (depth < 0 || entries->size() >= 256) {
		return;
	}

	DIR *d = opendir(dir.c_str());
	if (!d) {
		return;
	}

	while (dirent *ent = readdir(d)) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
			continue;
		}

		std::string child = dir;
		if (!endsWith(child, "/")) {
			child += "/";
		}
		child += ent->d_name;

		if (IsDirectory(child)) {
			ScanBootFiles(child, depth - 1, entries);
		} else if (HasBootableExtension(ent->d_name)) {
			entries->push_back({ child, BaseName(child) });
			if (entries->size() >= 256) {
				break;
			}
		}
	}

	closedir(d);
}

static std::vector<BootEntry> FindBootEntries() {
	std::vector<BootEntry> entries;
	ScanBootFiles("sdmc:/PSP/GAME", 3, &entries);
	ScanBootFiles("sdmc:/ISO", 1, &entries);
	ScanBootFiles("sdmc:/3ds/PPSSPP", 2, &entries);
	ScanBootFiles("sdmc:/PPSSPP", 2, &entries);
	std::sort(entries.begin(), entries.end(), [](const BootEntry &a, const BootEntry &b) {
		return Lower(a.path) < Lower(b.path);
	});
	return entries;
}

static bool ReadAutobootEntry(BootEntry *entry) {
	FILE *f = fopen("sdmc:/3ds/PPSSPP/autoboot.txt", "rb");
	if (!f) {
		RuntimeLog("autoboot: none");
		return false;
	}

	char buf[1024]{};
	size_t read = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	if (read == 0) {
		return false;
	}

	std::string path = Trim(std::string(buf, read));
	if (!HasBootableExtension(path) || !File::Exists(Path(path))) {
		RuntimeLog("autoboot: invalid '%s'", path.c_str());
		return false;
	}

	*entry = { path, BaseName(path) };
	RuntimeLog("autoboot: %s", path.c_str());
	return true;
}

static void BottomStatus(const char *title, const char *line1 = "", const char *line2 = "", const char *line3 = "") {
	P3DS_BottomClear();
	printf("\x1b[1;1H%s", title);
	printf("\x1b[3;1H%s", line1);
	printf("\x1b[4;1H%s", line2);
	printf("\x1b[5;1H%s", line3);
}

static void DrawMenu(const std::vector<BootEntry> &entries, int selected, int top) {
	P3DS_BottomClear();
	printf("\x1b[1;1HPPSSPP 3DS");
	printf("\x1b[2;1HA Launch  B Rescan  START Exit");

	if (entries.empty()) {
		printf("\x1b[5;1HNo PSP content found.");
		printf("\x1b[7;1HPlace files under:");
		printf("\x1b[8;1Hsdmc:/PSP/GAME");
		printf("\x1b[9;1Hsdmc:/ISO");
		printf("\x1b[10;1Hsdmc:/3ds/PPSSPP");
		return;
	}

	const int visible = 20;
	for (int i = 0; i < visible && top + i < (int)entries.size(); ++i) {
		const BootEntry &entry = entries[top + i];
		std::string label = entry.label;
		if (label.size() > 27) {
			label = label.substr(0, 24) + "...";
		}
		printf("\x1b[%d;1H%c %s", 4 + i, top + i == selected ? '>' : ' ', label.c_str());
	}
}

static void WriteTopPixel(int x, int y, u8 r, u8 g, u8 b) {
	u8 *fb = P3DS_GetTopFramebuffer();

	if (!fb || x < 0 || x >= TOP_W || y < 0 || y >= TOP_H) {
		return;
	}

	const int offset = 3 * ((TOP_H - 1 - y) + x * TOP_H);
	fb[offset + 0] = b;
	fb[offset + 1] = g;
	fb[offset + 2] = r;
}

static void ClearTop(u8 r, u8 g, u8 b) {
	for (int x = 0; x < TOP_W; ++x) {
		for (int y = 0; y < TOP_H; ++y) {
			WriteTopPixel(x, y, r, g, b);
		}
	}
	P3DS_Present();
}

static u32 ReadFramebufferRGBA(const GPUDebugBuffer &buf, int x, int y) {
	const u8 *data = buf.GetData();
	const int stride = (int)buf.GetStride();
	if (!data || x < 0 || y < 0 || x >= stride || y >= (int)buf.GetHeight()) {
		return 0xFF000000;
	}

	const GPUDebugBufferFormat fmt = buf.GetFormat();
	switch (fmt) {
	case GPU_DBG_FORMAT_8888: {
		const u32 px = ((const u32 *)data)[y * stride + x];
		return px;
	}
	case GPU_DBG_FORMAT_8888_BGRA: {
		const u32 px = ((const u32 *)data)[y * stride + x];
		return ((px & 0x000000FF) << 16) | (px & 0x0000FF00) | ((px & 0x00FF0000) >> 16) | (px & 0xFF000000);
	}
	case GPU_DBG_FORMAT_565: {
		const u16 px = ((const u16 *)data)[y * stride + x];
		return RGB565ToRGBA8888(px);
	}
	case GPU_DBG_FORMAT_5551: {
		const u16 px = ((const u16 *)data)[y * stride + x];
		return RGBA5551ToRGBA8888(px);
	}
	case GPU_DBG_FORMAT_4444: {
		const u16 px = ((const u16 *)data)[y * stride + x];
		return RGBA4444ToRGBA8888(px);
	}
	case GPU_DBG_FORMAT_888_RGB: {
		const u8 *p = data + (y * stride + x) * 3;
		return p[0] | (p[1] << 8) | (p[2] << 16) | 0xFF000000;
	}
	default:
		return 0xFF000000;
	}
}

static void RenderSoftwareFramebuffer() {
	if (!gpu) {
		ClearTop(12, 16, 24);
		return;
	}

	GPUDebugBuffer buf;
	if (!gpu->GetOutputFramebuffer(buf) || !buf.GetData()) {
		ClearTop(12, 16, 24);
		return;
	}

	const int srcW = std::min((int)buf.GetStride(), PSP_W);
	const int srcH = std::min((int)buf.GetHeight(), PSP_H);
	const int yOffset = (TOP_H - PSP_SCREEN_H_ON_3DS) / 2;

	for (int x = 0; x < TOP_W; ++x) {
		for (int y = 0; y < TOP_H; ++y) {
			if (y < yOffset || y >= yOffset + PSP_SCREEN_H_ON_3DS) {
				WriteTopPixel(x, y, 0, 0, 0);
				continue;
			}

			const int sx = x * srcW / TOP_W;
			const int sy = (y - yOffset) * srcH / PSP_SCREEN_H_ON_3DS;
			const u32 rgba = ReadFramebufferRGBA(buf, sx, sy);
			WriteTopPixel(x, y, rgba & 0xFF, (rgba >> 8) & 0xFF, (rgba >> 16) & 0xFF);
		}
	}

	P3DS_Present();
}

static u32 Map3DSButtons(uint32_t held) {
	u32 buttons = 0;
	if (held & P3DS_KEY_DUP) buttons |= CTRL_UP;
	if (held & P3DS_KEY_DDOWN) buttons |= CTRL_DOWN;
	if (held & P3DS_KEY_DLEFT) buttons |= CTRL_LEFT;
	if (held & P3DS_KEY_DRIGHT) buttons |= CTRL_RIGHT;
	if (held & P3DS_KEY_A) buttons |= CTRL_CROSS;
	if (held & P3DS_KEY_B) buttons |= CTRL_CIRCLE;
	if (held & P3DS_KEY_X) buttons |= CTRL_TRIANGLE;
	if (held & P3DS_KEY_Y) buttons |= CTRL_SQUARE;
	if (held & P3DS_KEY_L) buttons |= CTRL_LTRIGGER;
	if (held & P3DS_KEY_R) buttons |= CTRL_RTRIGGER;
	if (held & P3DS_KEY_START) buttons |= CTRL_START;
	if (held & P3DS_KEY_SELECT) buttons |= CTRL_SELECT;
	return buttons;
}

static void UpdatePSPInput() {
	int dx = 0;
	int dy = 0;
	P3DS_ReadCircle(&dx, &dy);

	uint32_t held = P3DS_KeysHeld();
	u32 pspButtons = Map3DSButtons(held);
	__CtrlUpdateButtons(pspButtons, CTRL_MASK_USER & ~pspButtons);

	float x = std::clamp((float)dx / 156.0f, -1.0f, 1.0f);
	float y = std::clamp((float)dy / 156.0f, -1.0f, 1.0f);
	if (std::abs(x) < 0.12f) x = 0.0f;
	if (std::abs(y) < 0.12f) y = 0.0f;
	__CtrlSetAnalogXY(CTRL_STICK_LEFT, x, y);
}

static void InitDisplayGlobals() {
	g_display.pixel_xres = PSP_W;
	g_display.pixel_yres = PSP_H;
	g_display.dp_xres = PSP_W;
	g_display.dp_yres = PSP_H;
	g_display.dpi_scale_x = 1.0f;
	g_display.dpi_scale_y = 1.0f;
	g_display.pixel_in_dps_x = 1.0f;
	g_display.pixel_in_dps_y = 1.0f;
	g_display.dpi_scale_real_x = 1.0f;
	g_display.dpi_scale_real_y = 1.0f;
	g_display.display_hz = 60.0f;
	g_display.rotation = DisplayRotation::ROTATE_0;
}

static void Apply3DSConfig() {
	g_Config.currentDirectory = Path("sdmc:/");
	g_Config.defaultCurrentDirectory = Path("sdmc:/");
	g_Config.internalDataDirectory = Path("sdmc:/3ds/PPSSPP");
	g_Config.memStickDirectory = Path("sdmc:/");
	g_Config.flash0Directory = Path("sdmc:/3ds/PPSSPP/assets/flash0");
	g_Config.appCacheDirectory = Path("sdmc:/3ds/PPSSPP/cache");
	g_Config.iCpuCore = (int)CPUCore::INTERPRETER;
	g_Config.bFastMemory = false;
	g_Config.bFuncReplacements = true;
	g_Config.bSoftwareRendering = true;
	g_Config.bSoftwareRenderingJit = false;
	g_Config.bVertexDecoderJit = false;
	g_Config.bEnableSound = false;
	g_Config.bEnableLogging = true;
	g_Config.bEnableWlan = false;
	g_Config.bEnableNetworkChat = false;
	g_Config.bDiscordRichPresence = false;
	g_Config.bMemStickInserted = true;
	g_Config.iMemStickSizeGB = 16;
	g_Config.iInternalResolution = 1;
	g_Config.iSkipGPUReadbackMode = (int)SkipGPUReadbackMode::NO_SKIP;
	g_Config.iLanguage = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
	g_Config.iTimeFormat = PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
	g_Config.iDateFormat = PSP_SYSTEMPARAM_DATE_FORMAT_YYYYMMDD;
	g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CROSS;
	g_Config.iFirmwareVersion = PSP_DEFAULT_FIRMWARE;
	g_Config.iPSPModel = PSP_MODEL_FAT;
	g_Config.iGameVolume = VOLUMEHI_FULL;
	g_Config.iReverbVolume = VOLUMEHI_FULL;
	g_Config.sNickName = "3DS";
	g_Config.sMACAddress = "12:34:56:78:9A:BC";
	g_Config.sReportHost.clear();
}

static bool InitPPSSPPPlatform(std::string *error) {
	PROFILE_INIT();
	TimeInit();
	SetCurrentThreadName("Main");
	InitDisplayGlobals();

	g_Config.Init();
	g_Config.RestoreDefaults(RestoreSettingsBits::SETTINGS | RestoreSettingsBits::CONTROLS, true);
	Apply3DSConfig();

	Ensure3DSDirectories();
	remove("sdmc:/3ds/PPSSPP/runtime.log");
	RuntimeLog("init: directories ready");
	File::CreateFullPath(g_Config.internalDataDirectory);
	File::CreateFullPath(g_Config.appCacheDirectory);

	if (g_romfsReady) {
		g_VFS.Register("", new DirectoryReader(Path("romfs:/")));
	}
	g_VFS.Register("", new DirectoryReader(Path("sdmc:/3ds/PPSSPP/assets")));
	g_VFS.Register("", new DirectoryReader(Path("sdmc:/PPSSPP/assets")));
	g_VFS.Register("", new DirectoryReader(Path("assets")));

	g_Config.SetSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));
	CreateSysDirectories();

	g_logManager.Init(&g_Config.bEnableLogging);
	g_Config.Load();
	Apply3DSConfig();
	g_Config.SetSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_logManager.SetFileLogPath(GetSysDirectory(DIRECTORY_DUMP) / "log.txt");
	g_logManager.EnableOutput(LogOutput::Printf);

	g_threadManager.Init(std::max(cpu_info.num_cores, 1), std::max(cpu_info.logical_cpu_count, 1));
	ResetUIState();
	UpdateUIState(UISTATE_MENU);
	RuntimeLog("init: ppsspp platform ready");

	if (error) {
		error->clear();
	}
	return true;
}

static void ShutdownPPSSPPPlatform() {
	g_threadManager.Teardown();
	g_logManager.Shutdown();
	g_VFS.Clear();
}

static bool BootGame(const std::string &path, std::string *error) {
	RuntimeLog("boot: start %s", path.c_str());
	CoreParameter coreParameter {};
	coreParameter.cpuCore = CPUCore::INTERPRETER;
	coreParameter.gpuCore = GPUCORE_SOFTWARE;
	coreParameter.graphicsContext = nullptr;
	coreParameter.enableSound = false;
	coreParameter.fileToStart = Path(path);
	coreParameter.startBreak = false;
	coreParameter.headLess = true;
	coreParameter.renderScaleFactor = 1;
	coreParameter.renderWidth = PSP_W;
	coreParameter.renderHeight = PSP_H;
	coreParameter.pixelWidth = PSP_W;
	coreParameter.pixelHeight = PSP_H;
	coreParameter.fastForward = false;
	coreParameter.updateRecent = false;
	coreParameter.fpsLimit = FPSLimit::NORMAL;

	UpdateUIState(UISTATE_INGAME);
	if (!PSP_InitStart(coreParameter)) {
		RuntimeLog("boot: PSP_InitStart failed");
		if (error) {
			*error = "PSP_InitStart failed.";
		}
		return false;
	}

	int bootFrames = 0;
	while (P3DS_AptMainLoop()) {
		P3DS_ScanInput();
		if ((P3DS_KeysDown() & P3DS_KEY_START) && (P3DS_KeysHeld() & P3DS_KEY_SELECT)) {
			if (error) {
				*error = "Boot cancelled.";
			}
			return false;
		}

		BootState state = PSP_InitUpdate(error);
		if ((bootFrames++ % 60) == 0) {
			RuntimeLog("boot: state=%d coreState=%d", (int)state, (int)coreState);
		}
		if (state == BootState::Complete) {
			coreState = CORE_RUNNING_CPU;
			System_Notify(SystemNotification::BOOT_DONE);
			RuntimeLog("boot: complete");
			BottomStatus("Running PPSSPP core", BaseName(path).c_str(), "START+SELECT Exit", "Software GPU -> top screen");
			return true;
		}
		if (state == BootState::Failed) {
			RuntimeLog("boot: failed %s", error ? error->c_str() : "");
			return false;
		}

		BottomStatus("Booting PSP content", BaseName(path).c_str(), "Please wait...", "START+SELECT Cancel");
		P3DS_WaitVBlank();
	}

	if (error) {
		*error = "APT loop stopped during boot.";
	}
	return false;
}

static void RunGameLoop(const std::string &path) {
	int statusFrame = 0;
	RuntimeLog("run: enter %s", path.c_str());
	while (P3DS_AptMainLoop() && !g_exitRequested) {
		P3DS_ScanInput();
		const uint32_t held = P3DS_KeysHeld();
		if ((held & P3DS_KEY_START) && (held & P3DS_KEY_SELECT)) {
			break;
		}

		UpdatePSPInput();
		PSP_UpdateDebugStats(false);

		const DisplayLayoutConfig &layout = g_Config.GetDisplayLayoutConfig(DeviceOrientation::Landscape);
		if (gpu) {
			gpu->BeginHostFrame(layout);
		}

		PSP_RunLoopWhileState();
		if (coreState == CORE_NEXTFRAME) {
			coreState = CORE_RUNNING_CPU;
		} else if (coreState == CORE_POWERDOWN || coreState == CORE_RUNTIME_ERROR) {
			break;
		}

		if (gpu) {
			gpu->EndHostFrame();
		}

		RenderSoftwareFramebuffer();
		P3DS_WaitVBlank();

		if ((statusFrame++ % 60) == 0) {
			RuntimeLog("run: frame=%d coreState=%d", statusFrame, (int)coreState);
			BottomStatus("PPSSPP running", BaseName(path).c_str(), "3DS buttons mapped to PSP", "START+SELECT Exit");
		}
	}

	if (PSP_IsInited()) {
		PSP_Shutdown(true);
	}
	RuntimeLog("run: exit");
	UpdateUIState(UISTATE_MENU);
}

static bool SelectBootEntry(std::vector<BootEntry> *entries, BootEntry *selectedEntry) {
	int selected = 0;
	int top = 0;

	while (P3DS_AptMainLoop()) {
		DrawMenu(*entries, selected, top);
		P3DS_WaitVBlank();
		P3DS_ScanInput();
		const uint32_t down = P3DS_KeysDown();

		if (down & P3DS_KEY_START) {
			return false;
		}
		if (down & P3DS_KEY_B) {
			*entries = FindBootEntries();
			selected = 0;
			top = 0;
			continue;
		}
		if (entries->empty()) {
			continue;
		}
		if (down & P3DS_KEY_DUP) {
			selected = std::max(0, selected - 1);
		}
		if (down & P3DS_KEY_DDOWN) {
			selected = std::min((int)entries->size() - 1, selected + 1);
		}
		if (down & P3DS_KEY_L) {
			selected = std::max(0, selected - 10);
		}
		if (down & P3DS_KEY_R) {
			selected = std::min((int)entries->size() - 1, selected + 10);
		}
		if (selected < top) {
			top = selected;
		}
		if (selected >= top + 20) {
			top = selected - 19;
		}
		if (down & P3DS_KEY_A) {
			*selectedEntry = (*entries)[selected];
			return true;
		}
	}

	return false;
}

int main(int argc, char **argv) {
	P3DS_Init();
	ClearTop(12, 16, 24);

	if (P3DS_InitRomFS()) {
		g_romfsReady = true;
	}

	std::string error;
	if (!InitPPSSPPPlatform(&error)) {
		BottomStatus("PPSSPP init failed", error.c_str(), "START Exit");
		while (P3DS_AptMainLoop()) {
			P3DS_ScanInput();
			if (P3DS_KeysDown() & P3DS_KEY_START) {
				break;
			}
			P3DS_WaitVBlank();
		}
	} else {
		std::vector<BootEntry> entries = FindBootEntries();
		if (argc > 1 && argv[1] && HasBootableExtension(argv[1])) {
			entries.insert(entries.begin(), { argv[1], BaseName(argv[1]) });
		}

		BootEntry selected;
		if (ReadAutobootEntry(&selected)) {
			ClearTop(0, 0, 0);
			error.clear();
			if (BootGame(selected.path, &error)) {
				RunGameLoop(selected.path);
			} else {
				BottomStatus("Autoboot failed", BaseName(selected.path).c_str(), error.c_str(), "START Exit");
				while (P3DS_AptMainLoop()) {
					P3DS_ScanInput();
					if (P3DS_KeysDown() & P3DS_KEY_START) {
						break;
					}
					P3DS_WaitVBlank();
				}
			}
			g_exitRequested = true;
		}
		while (!g_exitRequested && SelectBootEntry(&entries, &selected)) {
			ClearTop(0, 0, 0);
			error.clear();
			if (BootGame(selected.path, &error)) {
				RunGameLoop(selected.path);
			} else {
				BottomStatus("Boot failed", BaseName(selected.path).c_str(), error.c_str(), "B Menu  START Exit");
				while (P3DS_AptMainLoop()) {
					P3DS_ScanInput();
					const uint32_t down = P3DS_KeysDown();
					if (down & P3DS_KEY_START) {
						g_exitRequested = true;
						break;
					}
					if (down & P3DS_KEY_B) {
						break;
					}
					P3DS_WaitVBlank();
				}
			}
			if (g_exitRequested) {
				break;
			}
			entries = FindBootEntries();
		}

		ShutdownPPSSPPPlatform();
	}

	if (g_romfsReady) {
		P3DS_ExitRomFS();
	}
	P3DS_Shutdown();
	return 0;
}

void NativeGetAppInfo(std::string *app_dir_name, std::string *app_nice_name, bool *landscape, std::string *version) {
	*app_dir_name = "PPSSPP";
	*app_nice_name = "PPSSPP";
	*landscape = true;
	*version = PPSSPP_GIT_VERSION;
}

bool NativeIsAtTopLevel() {
	return GetUIState() == UISTATE_MENU;
}

void NativeFrame(GraphicsContext *graphicsContext) {
	(void)graphicsContext;
}

void NativeResized() {
}

bool NativeSaveSecret(std::string_view nameOfSecret, std::string_view data) {
	(void)nameOfSecret;
	(void)data;
	return false;
}

std::string NativeLoadSecret(std::string_view nameOfSecret) {
	(void)nameOfSecret;
	return "";
}

AudioBackend *System_CreateAudioBackend() {
	return nullptr;
}

void System_Toast(std::string_view text) {
	(void)text;
}

void System_ShowKeyboard() {
}

void System_Vibrate(int length_ms) {
	(void)length_ms;
}

void System_LaunchUrl(LaunchUrlType urlType, std::string_view url) {
	(void)urlType;
	(void)url;
}

void System_RunOnMainThread(std::function<void()> func) {
	if (func) {
		func();
	}
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
		return false;
	case SYSPROP_SKIP_UI:
		return true;
	case SYSPROP_SUPPORTS_HTTPS:
	case SYSPROP_HAS_FILE_BROWSER:
	case SYSPROP_HAS_FOLDER_BROWSER:
	case SYSPROP_HAS_IMAGE_BROWSER:
	case SYSPROP_HAS_KEYBOARD:
	case SYSPROP_KEYBOARD_IS_SOFT:
	case SYSPROP_HAS_TEXT_INPUT_DIALOG:
	case SYSPROP_HAS_TEXT_CLIPBOARD:
	case SYSPROP_CAN_SHOW_FILE:
	case SYSPROP_SUPPORTS_PERMISSIONS:
	case SYSPROP_DEBUGGER_PRESENT:
	case SYSPROP_HAS_DEBUGGER:
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

void System_PostUIMessage(UIMessage message, std::string_view param) {
	(void)message;
	(void)param;
}

void System_AudioGetDebugStats(char *buf, size_t bufSize) {
	if (buf && bufSize > 0) {
		buf[0] = '\0';
	}
}

void System_AudioClear() {
}

void System_AudioPushSamples(const s32 *audio, int numSamples, float volume) {
	(void)audio;
	(void)numSamples;
	(void)volume;
}
