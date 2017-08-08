/**************************************************************************
 * 
 * Copyright 1999-2006 Brian Paul
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


/**
 * @file
 * 
 * Thread, mutex, condition variable, barrier, semaphore and
 * thread-specific data functions.
 */


#ifndef OS_THREAD_H_
#define OS_THREAD_H_


#include "pipe/p_compiler.h"
#include "util/u_debug.h" /* for assert */
#include "util/u_thread.h"


#define pipe_mutex_assert_locked(mutex) \
   __pipe_mutex_assert_locked(&(mutex))

static inline void
__pipe_mutex_assert_locked(mtx_t *mutex)
{
#ifdef DEBUG
   /* NOTE: this would not work for recursive mutexes, but
    * mtx_t doesn't support those
    */
   int ret = mtx_trylock(mutex);
   assert(ret == thrd_busy);
   if (ret == thrd_success)
      mtx_unlock(mutex);
#endif
}


/*
 * pipe_barrier
 */

#if (defined(PIPE_OS_LINUX) || defined(PIPE_OS_BSD) || defined(PIPE_OS_SOLARIS) || defined(PIPE_OS_HURD)) && (!defined(PIPE_OS_ANDROID) || ANDROID_API_LEVEL >= 24)

typedef pthread_barrier_t pipe_barrier;

static inline void pipe_barrier_init(pipe_barrier *barrier, unsigned count)
{
   pthread_barrier_init(barrier, NULL, count);
}

static inline void pipe_barrier_destroy(pipe_barrier *barrier)
{
   pthread_barrier_destroy(barrier);
}

static inline void pipe_barrier_wait(pipe_barrier *barrier)
{
   pthread_barrier_wait(barrier);
}


#else /* If the OS doesn't have its own, implement barriers using a mutex and a condvar */

typedef struct {
   unsigned count;
   unsigned waiters;
   uint64_t sequence;
   mtx_t mutex;
   cnd_t condvar;
} pipe_barrier;

static inline void pipe_barrier_init(pipe_barrier *barrier, unsigned count)
{
   barrier->count = count;
   barrier->waiters = 0;
   barrier->sequence = 0;
   (void) mtx_init(&barrier->mutex, mtx_plain);
   cnd_init(&barrier->condvar);
}

static inline void pipe_barrier_destroy(pipe_barrier *barrier)
{
   assert(barrier->waiters == 0);
   mtx_destroy(&barrier->mutex);
   cnd_destroy(&barrier->condvar);
}

static inline void pipe_barrier_wait(pipe_barrier *barrier)
{
   mtx_lock(&barrier->mutex);

   assert(barrier->waiters < barrier->count);
   barrier->waiters++;

   if (barrier->waiters < barrier->count) {
      uint64_t sequence = barrier->sequence;

      do {
         cnd_wait(&barrier->condvar, &barrier->mutex);
      } while (sequence == barrier->sequence);
   } else {
      barrier->waiters = 0;
      barrier->sequence++;
      cnd_broadcast(&barrier->condvar);
   }

   mtx_unlock(&barrier->mutex);
}


#endif


/*
 * Semaphores
 */

typedef struct
{
   mtx_t mutex;
   cnd_t cond;
   int counter;
} pipe_semaphore;


static inline void
pipe_semaphore_init(pipe_semaphore *sema, int init_val)
{
   (void) mtx_init(&sema->mutex, mtx_plain);
   cnd_init(&sema->cond);
   sema->counter = init_val;
}

static inline void
pipe_semaphore_destroy(pipe_semaphore *sema)
{
   mtx_destroy(&sema->mutex);
   cnd_destroy(&sema->cond);
}

/** Signal/increment semaphore counter */
static inline void
pipe_semaphore_signal(pipe_semaphore *sema)
{
   mtx_lock(&sema->mutex);
   sema->counter++;
   cnd_signal(&sema->cond);
   mtx_unlock(&sema->mutex);
}

/** Wait for semaphore counter to be greater than zero */
static inline void
pipe_semaphore_wait(pipe_semaphore *sema)
{
   mtx_lock(&sema->mutex);
   while (sema->counter <= 0) {
      cnd_wait(&sema->cond, &sema->mutex);
   }
   sema->counter--;
   mtx_unlock(&sema->mutex);
}



/*
 * Thread-specific data.
 */

typedef struct {
   tss_t key;
   int initMagic;
} pipe_tsd;


#define PIPE_TSD_INIT_MAGIC 0xff8adc98


static inline void
pipe_tsd_init(pipe_tsd *tsd)
{
   if (tss_create(&tsd->key, NULL/*free*/) != 0) {
      exit(-1);
   }
   tsd->initMagic = PIPE_TSD_INIT_MAGIC;
}

static inline void *
pipe_tsd_get(pipe_tsd *tsd)
{
   if (tsd->initMagic != (int) PIPE_TSD_INIT_MAGIC) {
      pipe_tsd_init(tsd);
   }
   return tss_get(tsd->key);
}

static inline void
pipe_tsd_set(pipe_tsd *tsd, void *value)
{
   if (tsd->initMagic != (int) PIPE_TSD_INIT_MAGIC) {
      pipe_tsd_init(tsd);
   }
   if (tss_set(tsd->key, value) != 0) {
      exit(-1);
   }
}



/*
 * Thread statistics.
 */

/* Return the time of the current thread's CPU time clock. */
static inline int64_t
pipe_current_thread_get_time_nano(void)
{
#if defined(HAVE_PTHREAD)
   return u_thread_get_time_nano(pthread_self());
#else
   return 0;
#endif
}

#endif /* OS_THREAD_H_ */
