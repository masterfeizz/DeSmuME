#include <3ds.h>
#include "svchax.h"

s32 __patchSVC() {
    __asm__ volatile("cpsid aif");

    u32*  svc_access_control = *(*(u32***)0xFFFF9000 + 0x22) - 0x6;
    svc_access_control[0]=0xFFFFFFFE;
    svc_access_control[1]=0xFFFFFFFF;
    svc_access_control[2]=0xFFFFFFFF;
    svc_access_control[3]=0x3FFFFFFF;

    return 0;
}


s32 __flushCaches() {
    __asm__ volatile(
        "cpsid aif\n\t"
        "mov r0, #0\n\t"
        "mcr p15, 0, r0, c7, c5, 0\n\t"
        "mov r0, #0\n\t"
        "mcr p15, 0, r0, c7, c10, 0\n\t"
    );

    return 0;
}

void FlushInvalidateCache() {
        svcBackdoor(__flushCaches);
}

Result ReprotectMemory(u32* addr, u32 pages, u32 mode) {
    if(!__ctr_svchax_srv)
        return 0xFFFFFFFF;

    Handle processHandle;
    svcDuplicateHandle(&processHandle, 0xFFFF8001);
    return svcControlProcessMemory(processHandle, (u32)addr, 0x0, pages*0x1000, MEMOP_PROT, mode);
}