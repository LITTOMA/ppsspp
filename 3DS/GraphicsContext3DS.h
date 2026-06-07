#pragma once

#include <memory>

#include "Common/GraphicsContext.h"

namespace Draw {
class DrawContext;
}

class GraphicsContext3DS : public GraphicsContext {
public:
	GraphicsContext3DS();
	~GraphicsContext3DS() override;

	void Shutdown() override;
	void Resize() override;
	Draw::DrawContext *GetDrawContext() override;

private:
	std::unique_ptr<Draw::DrawContext> draw_;
};
