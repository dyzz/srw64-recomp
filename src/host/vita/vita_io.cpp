// newlib's file calls, wrapped (-Wl,--wrap, src/host/CMakeLists.txt) where they look up a
// path that is not there: on the console nothing comes of it, but Vita3K reports every such
// lookup as an error, and a clean Vita3K log is how the port is checked (psv-port.md, V2).
#include <fcntl.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <reent.h>

extern "C" {
int __real__open_r(struct _reent* reent, const char* file, int flags, int mode);
int __real__rename_r(struct _reent* reent, const char* from, const char* to);
int __real_sceKernelChangeThreadPriority(SceUID thread, int priority);

// newlib asks whether the path is a folder before it opens it, so a file about to be made is
// looked up missing first: the system's call makes it, then newlib opens it as it was asked.
int __wrap__open_r(struct _reent* reent, const char* file, int flags, int mode) {
    if ((flags & O_CREAT) && !(flags & O_EXCL)) {
        const SceUID made = sceIoOpen(file, SCE_O_WRONLY | SCE_O_CREAT | ((flags & O_TRUNC) ? SCE_O_TRUNC : 0), 0666);
        if (made >= 0) sceIoClose(made);
    }
    return __real__open_r(reent, file, flags, mode);
}

// newlib removes the destination before every rename, there or not. The system's rename
// alone does when nothing is in the way; otherwise newlib's goes as before.
int __wrap__rename_r(struct _reent* reent, const char* from, const char* to) {
    if (sceIoRename(from, to) >= 0) return 0;
    return __real__rename_r(reent, from, to);
}

// SDL's audio thread asks for one step above its current priority, read with
// sceKernelGetThreadInfo, which Vita3K leaves unimplemented: the structure stays as the stack
// had it, and the priority asked for is whatever was there. A priority is 64-191, or one
// relative to the default (0x10000100 and about it); anything else is left as it was.
int __wrap_sceKernelChangeThreadPriority(SceUID thread, int priority) {
    const bool absolute = priority >= 64 && priority <= 191;
    const bool relative = (unsigned(priority) & 0xF0000000u) == 0x10000000u;
    if (!absolute && !relative) return 0;
    return __real_sceKernelChangeThreadPriority(thread, priority);
}
}
