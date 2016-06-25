#ifndef __MEMORY_H__
#define __MEMORY_H__

#ifdef __cplusplus
extern "C" {
#endif

signed int __patchSVC();
signed int __flushCaches();
void FlushInvalidateCache();
signed int ReprotectMemory(unsigned int* addr, unsigned int pages, unsigned int mode);

#ifdef __cplusplus
}
#endif

#endif //__MEMORY_H__