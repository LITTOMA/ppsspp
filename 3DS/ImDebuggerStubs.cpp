#include "Common/CommonTypes.h"
#include "UI/ImDebugger/ImCommand.h"

class GPUCommon;
class MIPSDebugInterface;
struct MIPSState;
namespace Draw {
class DrawContext;
}

class ImDebugger {
public:
	ImDebugger();
	~ImDebugger();

	void Frame(MIPSDebugInterface *mipsDebug, GPUCommon *gpuDebug, Draw::DrawContext *draw);
	void Snapshot(MIPSState *mips);
	void SnapshotGPU(GPUCommon *gpu);
	void PostCmd(ImCommand cmd);
	void DeviceLost();
};

ImDebugger::ImDebugger() {
}

ImDebugger::~ImDebugger() {
}

void ImDebugger::Frame(MIPSDebugInterface *mipsDebug, GPUCommon *gpuDebug, Draw::DrawContext *draw) {
	(void)mipsDebug;
	(void)gpuDebug;
	(void)draw;
}

void ImDebugger::Snapshot(MIPSState *mips) {
	(void)mips;
}

void ImDebugger::SnapshotGPU(GPUCommon *gpu) {
	(void)gpu;
}

void ImDebugger::PostCmd(ImCommand cmd) {
	(void)cmd;
}

void ImDebugger::DeviceLost() {
}
