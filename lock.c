#include "lock.h"

pthread_mutex_t lock;

pthread_mutex_t *get_lock()
{
	return &lock;
}
