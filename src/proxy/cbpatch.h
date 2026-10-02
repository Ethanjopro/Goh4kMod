// Constant-buffer patches for 2x mode (M4). Some shaders get screen-size values in pixels from the game's
// constant buffers (logical 1920x1080), e.g. copy.hlsl's Load(uv * src_size) or the sky's sun position.
// At shader creation the bytecode is reflected; variables named in the rule table get a patch. At draw
// time a patched copy of the bound constant buffer replaces the original for that one draw.
// Runtime toggle: "fix nocbpatch on" disables all patches (for before/after captures).
#pragma once
#include "common.h"
#include <vector>

namespace goh4k {

class WrappedContext;

struct CbPatchOp {
    UINT slot;        // constant buffer slot (register b#)
    UINT offset;      // byte offset of the variable in the buffer
    UINT firstComp;   // first float component to change (0..3 relative to offset)
    UINT numComps;    // number of float components
    bool multiply;    // true: value * scale, false: value / scale
    bool whenSource;  // condition: t0 is a scaled texture (else: the render target is scaled)
};

struct ShaderPatch {
    std::vector<CbPatchOp> ops;
    std::string what;  // for logs: "src_size(b0 c1.xy *2), ..."
};

// Reflects a pixel shader; returns a patch index (>= 0) and tags the shader, or -1 if nothing to patch.
int RegisterShaderPatches(ID3D11DeviceChild* shader, const void* code, SIZE_T length);
int ShaderPatchIndex(ID3D11DeviceChild* shader);  // -1 if untagged
const ShaderPatch& GetShaderPatch(int index);

// Constant-buffer content tracking (only for buffers a patched draw has used).
bool IsTrackedCb(ID3D11Resource* r);
void TrackCbUpdate(ID3D11Resource* dst, const D3D11_BOX* box, const void* data);
void TrackCbMapped(ID3D11Resource* r, const void* mapped);  // at Unmap: copy what the game wrote

// Draw-time patching: binds patched copies; Restore puts the game's buffers back.
class CbPatchScope {
public:
    CbPatchScope(WrappedContext* ctx, int patchIndex, UINT targetScale);
    ~CbPatchScope();

private:
    WrappedContext* m_ctx = nullptr;
    UINT m_slots[4] = {};
    ID3D11Buffer* m_saved[4] = {};
    int m_count = 0;
};

}  // namespace goh4k
