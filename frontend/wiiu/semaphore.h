#pragma once
#include <coreinit/semaphore.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef OSSemaphore sem_t __attribute__ ((aligned (32)));

int sem_init(sem_t* sem, int pshared, unsigned int value);
int sem_post(sem_t* sem);
int sem_wait(sem_t* sem);
int sem_getvalue(sem_t* sem, int* sval);
int sem_destroy(sem_t* sem);

#ifdef __cplusplus
}
#endif
