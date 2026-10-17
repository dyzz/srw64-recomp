#!/usr/bin/env python3
"""ICU's mutexes initialise once through std::call_once, which on the PS Vita's newlib goes
to a pthread_once that fails (std::system_error, "Unknown error"). Here the same once-only
initialisation with an atomic, in the copy build_icu.sh builds (idempotent)."""
import sys
from pathlib import Path

path = Path(sys.argv[1]) / "source/common/umutex.cpp"
text = path.read_text()
if "srw64_umtx_once" in text:
    sys.exit(0)

def replace(old: str, new: str, count: int = 1) -> None:
    global text
    if text.count(old) != count:
        raise SystemExit(f"umutex.cpp differs: {old[:60]!r}")
    text = text.replace(old, new)

replace('#include "umutex.h"\n', '#include "umutex.h"\n\n#include <atomic>\n#include <thread>\n')
replace('''    pInitFlag->~once_flag();
    pInitFlag = new(&initFlag) std::once_flag();
    return true;
}''', '''    pInitFlag->~once_flag();
    pInitFlag = new(&initFlag) std::once_flag();
    srw64InitState.store(0, std::memory_order_release);
    return true;
}''')
replace('''std::once_flag *pInitFlag = &initFlag;
''', '''std::once_flag *pInitFlag = &initFlag;
// 0 before umtx_init, 1 while one thread runs it, 2 after (srw64_umtx_once).
std::atomic<int> srw64InitState{0};
''')
replace('''    ucln_common_registerCleanup(UCLN_COMMON_MUTEX, umtx_cleanup);
}
U_CDECL_END
''', '''    ucln_common_registerCleanup(UCLN_COMMON_MUTEX, umtx_cleanup);
}
U_CDECL_END

// std::call_once(*pInitFlag, umtx_init) without pthread_once.
static void srw64_umtx_once() {
    if (srw64InitState.load(std::memory_order_acquire) == 2) return;
    int expected = 0;
    if (srw64InitState.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        umtx_init();
        srw64InitState.store(2, std::memory_order_release);
        return;
    }
    while (srw64InitState.load(std::memory_order_acquire) != 2) std::this_thread::yield();
}
''')
replace("std::call_once(*pInitFlag, umtx_init);", "srw64_umtx_once();", count=text.count("std::call_once(*pInitFlag, umtx_init);"))
path.write_text(text)
print("umutex.cpp: std::call_once replaced")
