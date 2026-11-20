#pragma once
// The audio microcode a task runs: the recompiled one, or the native one (audio_hle.cpp).
#include "librecomp/rsp.hpp"

namespace srw64::audio_hle {
// For one audio task, on ultramodern's task thread: the function that runs it.
RspUcodeFunc* microcode(const OSTask* task);
}
