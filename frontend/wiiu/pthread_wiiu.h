#pragma once

// our compiler actually provides <pthread.h> but not the implementation afaik
#ifndef _POSIX_THREADS
#define _POSIX_THREADS
#endif
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// provide a pointer to this structure casted as pthread_attr_t during pthread_create to use additional options on WiiU
typedef struct pthread_attr_t_wiiu
{
    uint8_t     flags;      // bitmask of OS_THREAD_ATTRIB_...
    uint8_t     priority;   // 0=highest....31=lowest
    uint32_t    stack_size; // in bytes, 0 to use WIIU_PTHREAD_DEFAULT_STACK_SIZE

} pthread_attr_t_wiiu;


// shortcuts for creating custom wiiu threads in a more non-portable way :)

int pthread_create_wiiu_core(pthread_t* thread, void *(*start_routine)(void*), void *arg, uint32_t coreid);

int pthread_create_wiiu(pthread_t* thread, void *(*start_routine)(void*), void *arg,
                        uint32_t stack_size, uint32_t coreid, uint32_t priority);

#ifdef __cplusplus
}
#endif
