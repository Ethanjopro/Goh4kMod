// NVIDIA HBAO+ handling for 2x mode; see hbao.cpp.
#pragma once
#include "common.h"

namespace goh4k {

void FindHbaoModule();                         // once the game's device exists (HBAO+ is loaded by then)
bool FromHbao(void* returnAddress);            // was this call made by HBAO+'s code?
bool IsOurContext(ID3D11DeviceContext* ctx);  // device.cpp

}  // namespace goh4k
