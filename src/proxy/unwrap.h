// The swapchain's backbuffer is handed to the game as a small wrapper (texture.cpp) whose GetDesc reports the
// logical size; the game sizes many of its own targets from it. Anything that passes a resource back to the
// real D3D objects must unwrap it first. The check is one vtable-pointer compare.
#pragma once
#include <d3d11.h>

namespace goh4k {

extern void* g_wrappedTextureVtbl;
ID3D11Resource* RealOfWrappedTexture(ID3D11Resource* r);

inline ID3D11Resource* UnwrapResource(ID3D11Resource* r) {
    return (r && g_wrappedTextureVtbl && *(void**)r == g_wrappedTextureVtbl) ? RealOfWrappedTexture(r) : r;
}

}  // namespace goh4k

using goh4k::UnwrapResource;
