#include "pthread_wiiu.h"
#include <errno.h>
#include <string.h>
#include <coreinit/thread.h>

// providing NULL as pthread_attr_t pointer during pthread_create will use the following default values
#define WIIU_PTHREAD_DEFAULT_STACK_SIZE         131072                          // in bytes
#define WIIU_PTHREAD_DEFAULT_PRIORITY           15                              // 0=highest, 31=lowest
#define WIIU_PTHREAD_DEFAULT_FLAGS              OS_THREAD_ATTRIB_AFFINITY_CPU1  // core 0, 1, 2 or any, core1 is the "main" core
#define WIIU_PTHREAD_FAST_MUTEX_AND_COND        1                               // mutex and cond var with a fast path


#if WIIU_FAST_MUTEX_AND_COND
#include <coreinit/fastmutex.h>
#include <coreinit/fastcondition.h>
typedef OSFastMutex os_mutex_t
typedef OSFastCondition os_cond_t
#define ptwum_mutex_init(Mutex)         OSFastMutex_Init(Mutex, NULL)
#define ptwum_mutex_lock                OSFastMutex_Lock
#define ptwum_mutex_trylock             OSFastMutex_TryLock
#define ptwum_mutex_unlock              OSFastMutex_Unlock
#define ptwum_cond_init(Cond)           OSFastCond_Init(Cond, NULL)
#define ptwum_cond_signal               OSFastCond_Signal
#define ptwum_cond_broadcast            OSFastCond_Signal
#define ptwum_cond_wait                 OSFastCond_Wait
#else
#include <coreinit/mutex.h>
#include <coreinit/condition.h>
typedef OSMutex os_mutex_t;
typedef OSCondition os_cond_t;
#define ptwum_mutex_init                OSInitMutex
#define ptwum_mutex_lock                OSLockMutex
#define ptwum_mutex_trylock             OSTryLockMutex
#define ptwum_mutex_unlock              OSUnlockMutex
#define ptwum_cond_init                 OSInitCond
#define ptwum_cond_signal               OSSignalCond
#define ptwum_cond_broadcast            OSSignalCond
#define ptwum_cond_wait                 OSWaitCond
#endif

#include <coreinit/memdefaultheap.h>
#define ptwum_memalloc(Size, Align)     MEMAllocFromDefaultHeapEx(Size, Align)
#define ptwum_memfree(Ptr)              MEMFreeToDefaultHeap(Ptr)
#define ptwum_align_size(_v, _s)        _v += (_v & (_s-1)) ? _s - (_v & (_s-1)) : 0


typedef struct pthread_wiiu_footer
{
    pthread_attr_t_wiiu     creation_data;
    uint8_t*                stack_base;
    uint32_t                unusedu[5];
//---
    OSThread                thread_handle;

} pthread_wiiu_footer;


static void pthread_wiiu_dealloc(OSThread* t, void* stack)
{
    pthread_wiiu_footer* footer = (pthread_wiiu_footer*) ((uint8_t*) t - (uintptr_t) (&((pthread_wiiu_footer*)NULL)->thread_handle));
    ptwum_memfree(footer->stack_base);
}

static int pthread_wiiu_start(int arg, const char** start_routine)
{
    // horrible casting just to silence the pedantic compilers
    // we're running on a 32 bit system where passing any pointers as integers is fine (including function pointers)
    return (int) ((intptr_t) ((void* (*)(void*)) ((uintptr_t) start_routine)) ((void*)arg));
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr_, void *(*start_routine)(void*), void *arg)
{
    pthread_attr_t_wiiu* attr = (pthread_attr_t_wiiu*) attr_;
    uint8_t flags = attr_ ? attr->flags : WIIU_PTHREAD_DEFAULT_FLAGS;
    uint8_t priority = attr_ ? attr->priority : WIIU_PTHREAD_DEFAULT_PRIORITY;
    uint32_t stack_size = attr_ ? attr->stack_size : WIIU_PTHREAD_DEFAULT_STACK_SIZE;
    uint32_t footer_size = sizeof(pthread_wiiu_footer);
    ptwum_align_size(stack_size, 32);
    ptwum_align_size(footer_size, 32);

    uint8_t* stack_base = ptwum_memalloc(stack_size + footer_size, 64);
    if(stack_base)
    {
        memset(stack_base, 0, stack_size + footer_size);
        pthread_wiiu_footer* footer = (pthread_wiiu_footer*) (stack_base + stack_size);
        footer->creation_data.flags = flags;
        footer->creation_data.priority = priority;
        footer->creation_data.stack_size = stack_size;
        footer->stack_base = stack_base;

        if(OSCreateThread(&footer->thread_handle,
            pthread_wiiu_start,
            (int32_t) ((intptr_t) arg), (char*) ( (uintptr_t) start_routine),
            footer, stack_size,
            priority, flags
        ))
        {
            (*thread) = (pthread_t) ((uintptr_t) &footer->thread_handle);
            OSSetThreadDeallocator(&footer->thread_handle, pthread_wiiu_dealloc);
            OSResumeThread(&footer->thread_handle);
            return 0;
        }
    }

    // error creating something
    if(stack_base)
    ptwum_memfree(stack_base);
    *thread = 0;
    return 1;
}

// shortcuts for creating custom wiiu threads in a more non-portable way :)

int pthread_create_wiiu_core(pthread_t* thread, void *(*start_routine)(void*), void *arg, uint32_t coreid)
{
    pthread_attr_t_wiiu threadsetup = {
        .flags = (coreid <= 2) ? 1 << coreid : WIIU_PTHREAD_DEFAULT_FLAGS,
        .priority = WIIU_PTHREAD_DEFAULT_PRIORITY,
        .stack_size = WIIU_PTHREAD_DEFAULT_STACK_SIZE
    };
    return pthread_create(thread, (const pthread_attr_t*) &threadsetup, start_routine, arg);
}

int pthread_create_wiiu(pthread_t* thread, void *(*start_routine)(void*), void *arg,
                        uint32_t stack_size, uint32_t coreid, uint32_t priority)
{
    pthread_attr_t_wiiu threadsetup = {
        .flags = (coreid <= 2) ? 1 << coreid : WIIU_PTHREAD_DEFAULT_FLAGS,
        .priority = (priority < 32) ? priority : WIIU_PTHREAD_DEFAULT_PRIORITY,
        .stack_size = stack_size ? stack_size : WIIU_PTHREAD_DEFAULT_STACK_SIZE
    };
    return pthread_create(thread, (const pthread_attr_t*) &threadsetup, start_routine, arg);
}

int pthread_join(pthread_t t, void** retval) { return !OSJoinThread((OSThread*) ((uintptr_t) t), (int*) retval); }
int pthread_cancel(pthread_t t) { OSCancelThread((OSThread*) ((uintptr_t) t)); return 0; }




// mutex

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr)
{
    os_mutex_t* osmutex = (os_mutex_t*) ptwum_memalloc(sizeof(os_mutex_t), 32);
    if(osmutex)
    {
        ptwum_mutex_init(osmutex);
        *((os_mutex_t**) mutex) = osmutex;
        return 0;
    }
    else
    {   *((int*) mutex) = 0;
        return 1;
    }
}

int pthread_mutex_destroy(pthread_mutex_t* mutex)
{
    os_mutex_t* osmutex = *((os_mutex_t**) mutex);
    if(osmutex) ptwum_memfree(osmutex);
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* mutex) { return ptwum_mutex_trylock(*((os_mutex_t**)mutex)) ? 0 : EBUSY; }
int pthread_mutex_lock(pthread_mutex_t* mutex) { ptwum_mutex_lock(*((os_mutex_t**)mutex)); return 0; }
int pthread_mutex_unlock(pthread_mutex_t* mutex) { ptwum_mutex_unlock(*((os_mutex_t**)mutex)); return 0; }



// condition variable

int pthread_cond_signal(pthread_cond_t* cond) { ptwum_cond_signal(*((os_cond_t**)cond)); return 0; }
int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) { ptwum_cond_wait(*((os_cond_t**)cond), *((os_mutex_t**)mutex)); return 0; }
int pthread_cond_broadcast(pthread_cond_t* cond) { ptwum_cond_broadcast(*((os_cond_t**)cond)); return 0; }

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr)
{
    os_cond_t* oscond = (os_cond_t*) ptwum_memalloc(sizeof(os_cond_t), 32);
    if(oscond)
    {
        ptwum_cond_init(oscond);
        *((os_cond_t**) cond) = oscond;
        return 0;
    }
    else
    {   *((int*) cond) = 0;
        return 1;
    }
}

int pthread_cond_destroy(pthread_cond_t* cond)
{
    os_cond_t* oscond = *((os_cond_t**) cond);
    if(oscond) ptwum_memfree(oscond);
    return 0;
}



// semaphore

#include "semaphore.h"

int sem_init(sem_t* sem, int pshared, unsigned int value)
{   OSInitSemaphore(sem, value);
    return 0;
}
int sem_post(sem_t* sem) { return OSSignalSemaphore(sem); }
int sem_wait(sem_t* sem) { return OSWaitSemaphore(sem); }
int sem_destroy(sem_t* sem) { return 0; }

int sem_getvalue(sem_t* sem, int* sval)
{   *sval = OSGetSemaphoreCount(sem);
    return 0;
}
