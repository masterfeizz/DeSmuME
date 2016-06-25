#include "common.h"

#ifndef USE_LWP_LOCK

#ifndef mutex_t
typedef int mutex_t;
#endif

void _FAT_lock_init(mutex_t *mutex)
{
	return;
}

void _FAT_lock_deinit(mutex_t *mutex)
{
	return;
}

void _FAT_lock(mutex_t *mutex)
{
	return;
}

void _FAT_unlock(mutex_t *mutex)
{
	return;
}

#endif // USE_LWP_LOCK
