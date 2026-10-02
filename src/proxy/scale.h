// 2x rendering rules (M3). The game believes its screen is the "logical" size (its swapchain request, e.g.
// 1920x1080); screen-sized render/depth/UAV targets and their 1/2, 1/4, 1/8 fractions are created at
// Scale x that size and tagged, and viewports/scissors/copy boxes are scaled while such targets are used.
#pragma once
#include "common.h"
#include <atomic>

namespace goh4k {

struct ScaleTag {
    UINT scale;     // 2 for a scaled resource
    UINT logicalW;  // the size the game asked for
    UINT logicalH;
};

extern std::atomic<UINT> g_logicalW, g_logicalH;  // game's logical screen (swapchain) size; 0 = unknown

// 2x is configured (Scale > 1): hooks are installed at load.
inline bool ScaleConfigured() { return g_cfg.scale > 1; }

// 2x is active right now. Set by the swapchain: when the game asks for a resolution whose 2x doesn't fit the
// monitor (e.g. it is still set to native 4K), the mod steps aside completely (rendering and mouse) until a
// resolution that fits is chosen. Starts active so the desktop the game sees at startup is the logical one.
extern std::atomic<bool> g_scaleActive;
inline bool Scaling() { return g_cfg.scale > 1 && g_scaleActive.load(std::memory_order_relaxed); }
void SetScaleActive(bool active, const char* why);

// 1 for untagged resources/views. Views are tagged at creation (cheap lookup); untagged views fall back
// to their resource.
UINT ScaleOf(ID3D11Resource* r, ScaleTag* tag = nullptr);
UINT ScaleOfView(ID3D11View* v, ScaleTag* tag = nullptr);
void TagResource(ID3D11Resource* r, const ScaleTag& t);
void TagView(ID3D11View* v);  // copies the resource's scale onto the view

// Creation rule: does the game's texture description get the 2x treatment?
bool WantScaled(const D3D11_TEXTURE2D_DESC& d, bool initialData);
// A render/depth/UAV target the game created at exactly the real screen size (it read a real-sized desc):
// tagged as 2x of the logical screen without resizing.
bool IsRealScreenTarget(const D3D11_TEXTURE2D_DESC& d);
void LogScaledCreate(const D3D11_TEXTURE2D_DESC& logical);

// Things the M3 rules can't fix yet: logged once per kind with the resources involved.
void NoteOnce(const char* kind, const std::string& detail);

}  // namespace goh4k
