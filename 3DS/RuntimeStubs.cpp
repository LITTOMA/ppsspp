#include <string>

#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/ShaderTranslation.h"
#include "Common/VR/PPSSPPVR.h"

GLExtensions gl_extensions{};

bool GLExtensions::VersionGEThan(int major, int minor, int sub) {
	if (ver[0] > major)
		return true;
	if (ver[0] < major)
		return false;
	if (ver[1] > minor)
		return true;
	if (ver[1] < minor)
		return false;
	return ver[2] >= sub;
}

int GLExtensions::GLSLVersion() {
	return 0;
}

void ShaderTranslationInit() {
}

void ShaderTranslationShutdown() {
}

bool TranslateShader(std::string *dst, ShaderLanguage destLang, const ShaderLanguageDesc &desc, TranslatedShaderMetadata *destMetadata, std::string src, ShaderLanguage srcLang, ShaderStage stage, std::string *errorMessage) {
	(void)destLang;
	(void)desc;
	(void)destMetadata;
	(void)srcLang;
	(void)stage;
	if (dst)
		*dst = src;
	if (errorMessage)
		errorMessage->clear();
	return true;
}

bool IsVREnabled() {
	return false;
}

void InitVROnAndroid(void *vm, void *activity, const char *system, int version, const char *name) {
	(void)vm;
	(void)activity;
	(void)system;
	(void)version;
	(void)name;
}

void EnterVR(bool firstStart) {
	(void)firstStart;
}

void GetVRResolutionPerEye(int *width, int *height) {
	if (width)
		*width = 0;
	if (height)
		*height = 0;
}

void SetVRCallbacks(void (*axis)(const AxisInput *axis, size_t count), bool (*key)(const KeyInput &key), void (*touch)(const TouchInput &touch)) {
	(void)axis;
	(void)key;
	(void)touch;
}

void SetVRAppMode(VRAppMode mode) {
	(void)mode;
}

void UpdateVRInput(bool haptics, float dp_xscale, float dp_yscale) {
	(void)haptics;
	(void)dp_xscale;
	(void)dp_yscale;
}

bool UpdateVRAxis(const AxisInput *axes, size_t count) {
	(void)axes;
	(void)count;
	return false;
}

bool UpdateVRKeys(const KeyInput &key) {
	(void)key;
	return false;
}

void PreprocessStepVR(void *step) {
	(void)step;
}

void SetVRCompat(VRCompatFlag flag, long value) {
	(void)flag;
	(void)value;
}

void *BindVRFramebuffer() {
	return nullptr;
}

bool StartVRRender() {
	return false;
}

void FinishVRRender() {
}

void PreVRFrameRender(int fboIndex) {
	(void)fboIndex;
}

void PostVRFrameRender() {
}

int GetVRFBOIndex() {
	return 0;
}

int GetVRPassesCount() {
	return 1;
}

bool IsPassthroughSupported() {
	return false;
}

bool IsBigScreenVRMode() {
	return false;
}

bool IsFlatVRGame() {
	return false;
}

bool IsFlatVRScene() {
	return false;
}

bool IsGameVRScene() {
	return false;
}

bool IsImmersiveVRMode() {
	return false;
}

bool Is2DVRObject(float *projMatrix, bool ortho) {
	(void)projMatrix;
	(void)ortho;
	return false;
}

void UpdateVRParams(float *projMatrix) {
	(void)projMatrix;
}

void UpdateVRProjection(float *projMatrix, float *output) {
	(void)projMatrix;
	(void)output;
}

void UpdateVRView(float *leftEye, float *rightEye) {
	(void)leftEye;
	(void)rightEye;
}

void UpdateVRViewMatrices() {
}
