/**
 * @addtogroup Definition
 * @{
 * @file snowflake.h
 * @brief Snowflake ID with IPC accross processes.
 * 
 * Snowflake ID with IPC accross processes.
 *
 * This implementation generate a full 64 bits ID so timestamp is 42 bits
 * instead of 41 bits of original implementation.
 *
 * Sequence is sync accross multiple process with semaphore and shared memory,
 * each process register itself and the last one to exit properly will unlink
 * named semaphore and shared memory (of course if one crashes, the refcount
 * won't be decreased and it will be lying around).
 *
 * Passage of time use clock_gettime: CLOCK_MONOTONIC and a relative view of
 * the time, meaning that if one process becomes somehow much in advance than
 * others, the others will take that process timestamp as their wall clock. We
 * make sure that the time is going forward and that the biggest timestamp of
 * the sequence become the time of the process.
 *
 * Some value are configurable at compile time by defining following value :
 * - SF_EPOCH_MS   : Set the epoch of snowflake id in [ms], default to
 *                   Thu Jan 01 2026 00:00:00 GMT+0000.
 * - SF_NODE_BS    : Set the bit size for node id, default to 10.
 * - SF_SEQ_BS     : Set the bit size for sequence, default to 12.
 * - SF_S_NAME     : Name to use to create the name shm and semaphore, al
 *                   processes using the same name will share time and
 *                   sequence, default to "snowflake".
 * - SF_LOCK_SLEEP : Locking is done by sem_trywait, sleep of 1 [us] and try
 *                   again until this number of try have been done. Default
 *                   to 5000, so it wait 5 [ms] before failing.
 *
 * @author Etienne Bagnoud <etienne@artnum.ch>
 * @copyright Public domain
 */
#ifndef SNOWFLAKE_H__
#define SNOWFLAKE_H__ 1

#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#define SNOWFLAKE_INVALID                  0
#define SNOWFLAKE_FAIL_WAIT                5 /* 5 ms */
#define SNOWFLAKE_WALL_CLOCK_RESYNC_MS    10 /* 10 ms */

struct sf_seq {
    uint64_t timestamp;
    _Atomic(int32_t) refcount;
    uint16_t sequence;
};

typedef struct {
    uint64_t wall_clock;
    uint64_t init_clock;
    uint16_t node;
    int shm;
    sem_t *sem;
    struct sf_seq *seq;
} snowflake_ctx_t;

/**
 * Open a context to generate snowflake id.
 *
 * @param ctx     [in] An allocated ctx-
 * @param node_id [in] The current node id.
 *
 * @return true in case of success, false otherwise.
 */
bool     snowflake_open  (snowflake_ctx_t *ctx, uint16_t node_id); 
/**
 * Close a context.
 *
 * @param ctx [in] The ctx to close.
 */
void     snowflake_close (snowflake_ctx_t *ctx);
/**
 * Get one id
 *
 * @param ctx [in]  The snowflake ctx opened with \see snowflake_open
 * 
 * @return SNWOFLAKE_INVALID in case of a failure, an id otherwise.
 */
uint64_t snowflake_get   (snowflake_ctx_t *ctx);

#endif /* SNOWFLAKE_H__ 1 */
/** @} */

/**
 * @addtogroup Implementation
 * @{
 * @brief Use SNOWFLAKE_IMPLEMENTATION to build
 *
 * In on file source file that include snowflake, define
 * SNOWFLAKE_IMPLEMENTATION before including it in order to have the code
 * compiled into your project.
 */
#ifdef SNOWFLAKE_IMPLEMENTATION

#ifndef _POSIX_C_SOURCE
    #define _POSIX_C_SOURCE 200112L
#endif /* _POSIX_C_SOURCE 200112L */
#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif /* _GNU_SOURCE */

#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <sys/time.h>
#include <fcntl.h>           /* For O_* constants */
#include <time.h>
#include <errno.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

/**
 * Define the epoch, in ms, default to Thu Jan 01 2026 00:00:00 GMT+0000.
 */
#ifndef SF_EPOCH_MS
    #define SF_EPOCH_MS        1767225600000   
#endif /* SF_EPOCH_MS */

/**
 * Define the number of bits for node index. Default to 10.
 */
#ifndef SF_NODE_BS
    #define SF_NODE_BS         10
#endif /* SF_NODE_BS */

/**
 * Define the number of bits for sequence number. Default to 12.
 */
#ifndef SF_SEQ_BS
    #define SF_SEQ_BS          12
#endif /* SF_SEQ_BS */

/**
 * Semaphore and shared memory must have a name. That would be project name.
 * Read `man sem_overview` about named semaphore. Not that this name is
 * expanded with prefix "/" and postfix "_sem" and "_shm" for semaphore and
 * shared memory.
 */
#ifndef SF_S_NAME
    #define SF_S_NAME "snowflake" 
#endif /* SF_S_NAME */

/**
 * When locking the semaphore, it sleep for 1 us and try again. This value
 * fix the loop size. Default is 5ms (5000 * 1us) before giving up
 */
#ifndef SF_LOCK_SLEEP
    #define SF_LOCK_SLEEP      5000
#endif /* SF_LOCK_SLEEP */

#define SF_SEM_NAME        "/" SF_S_NAME "_sem"
#define SF_SHM_NAME        "/" SF_S_NAME "_shm"
#define SF_TIMESTAMP_BS    42
#define SF_NS_BS           (SF_SEQ_BS + SF_NODE_BS)

#define ONE_MS_USLEEP      1000

/* validate that sf_node_bs and sf_seq_bs is 22 */
#if SF_NS_BS > 22 || SF_NS_BS < 22
#error "SF_SEQ_BS + SF_NODE_BS must be 22"
#endif 

#define SF_TIMESTAMP_MASK  (((uint64_t)1 << SF_TIMESTAMP_BS) - 1)
#define SF_NODE_MASK       (((uint64_t)1 << SF_NODE_BS)      - 1)
#define SF_SEQ_MASK        (((uint64_t)1 << SF_SEQ_BS)       - 1)

static inline bool lock(sem_t *s) {
    uint16_t loop = 0;
restart:
    if (sem_trywait(s) == -1) {
        if (errno == EAGAIN) {
            usleep(1);
            if (++loop < SF_LOCK_SLEEP) {
                goto restart;
            }
        }
        return false;
    }
    return true;
}

static inline void unlock(sem_t *s) {
    sem_post(s);
}

bool snowflake_open(snowflake_ctx_t *ctx, uint16_t node_id) {
    assert(ctx != NULL);
    bool fail_must_sub = false;
    ctx->shm = -1;
    ctx->sem = SEM_FAILED;
    ctx->seq = MAP_FAILED;
    ctx->node = node_id;

    ctx->sem = sem_open(SF_SEM_NAME, O_CREAT | O_EXCL, 0660, 1);
    if (ctx->sem == SEM_FAILED) {
        if (errno == EEXIST) {
            ctx->sem = sem_open(SF_SEM_NAME, 0);
            if (ctx->sem == SEM_FAILED) {
                goto fail; 
            }
        } else {
            goto fail;
        }
    }

    if(!lock(ctx->sem)) {
        goto fail; 
    }

    bool created_here = false;
    ctx->shm = shm_open(SF_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0660);
    if (ctx->shm == -1) {
        if (errno == EEXIST) {
            ctx->shm = shm_open(SF_SHM_NAME, O_RDWR, 0660);
            if (ctx->shm == -1) {
                unlock(ctx->sem);
                goto fail;
            }
        } else {
            unlock(ctx->sem);
            goto fail;
        }
    } else {
        if(ftruncate(ctx->shm, sizeof(struct sf_seq)) == -1) {
            /* can't do anything if one of those fail */
            unlock(ctx->sem);
            goto fail;
        }
        created_here = true;
    }

    ctx->seq = (struct sf_seq *)mmap(NULL, sizeof(struct sf_seq),
                                     PROT_WRITE | PROT_READ, MAP_SHARED, 
                                     ctx->shm, 0);
    if (ctx->seq == MAP_FAILED) {
        unlock(ctx->sem);
        goto fail;
    }
    
    if (created_here) {
        memset(ctx->seq, 0, sizeof(struct sf_seq));
    }
    atomic_fetch_add(&ctx->seq->refcount, 1);

    fail_must_sub = true;
    unlock(ctx->sem); 

    struct timeval tv = {0};
    struct timespec mono = {0};
    if (gettimeofday(&tv, NULL) != 0 ||
        clock_gettime(CLOCK_MONOTONIC, &mono) != 0) 
    { 
        goto fail;
    }
    /* make it into ms */
    ctx->wall_clock = (uint64_t)tv.tv_sec * 1000 +
                      (uint64_t)tv.tv_usec / 1000;
    ctx->wall_clock -= SF_EPOCH_MS;
    ctx->init_clock = (uint64_t)mono.tv_sec * 1000 +
                      (uint64_t)mono.tv_nsec / 1000000;


    return true;

fail:
    if (fail_must_sub)          { atomic_fetch_sub(&ctx->seq->refcount, 1);  }
    if (ctx->seq != MAP_FAILED) { munmap(ctx->seq, sizeof(struct sf_seq));   } 
    if (ctx->shm != -1)         { close(ctx->shm);                           }
    if (ctx->sem != SEM_FAILED) { sem_close(ctx->sem);                       }
    return false;
}

void snowflake_close(snowflake_ctx_t *ctx) {
    if (!ctx) { return; }

    if (lock(ctx->sem)) {
        int32_t refcount = atomic_fetch_sub(&ctx->seq->refcount, 1);
        if (refcount <= 1) {
            sem_unlink(SF_SEM_NAME);
            shm_unlink(SF_SHM_NAME);
        }
        unlock(ctx->sem);
    }

    if (ctx->seq != MAP_FAILED) { munmap(ctx->seq, sizeof(struct sf_seq)); } 
    if (ctx->shm != -1)         { close(ctx->shm);                         }
    if (ctx->sem != SEM_FAILED) { sem_close(ctx->sem);                     }
        
    ctx->shm = -1;
    ctx->seq = MAP_FAILED;
    ctx->sem = SEM_FAILED;
}

uint64_t snowflake_get(snowflake_ctx_t *ctx) {
    struct timespec mono = {0};
    uint16_t infinite_loop_guard = 0;

restart:
    if (clock_gettime(CLOCK_MONOTONIC, &mono) != 0) { goto fail; }
    uint64_t ts = (uint64_t)mono.tv_sec * 1000 +
                  (uint64_t)mono.tv_nsec / 1000000;

    uint64_t wall = ctx->wall_clock + (ts - ctx->init_clock);
    struct timeval tv = {0};
    if (gettimeofday(&tv, NULL) == 0) {
        uint64_t wall_clock = (uint64_t)tv.tv_sec * 1000 +
                              (uint64_t)tv.tv_usec / 1000;
        wall_clock -= SF_EPOCH_MS;
        /* we are much late on current wall clock, so we bump time forward */
        if ((int64_t)(wall_clock - wall) > SNOWFLAKE_WALL_CLOCK_RESYNC_MS) {
            ctx->wall_clock = wall_clock;
            ctx->init_clock = ts;
            wall = wall_clock;
        }
    }

    uint16_t sequence = 0;
    if(!lock(ctx->sem)) {
        return SNOWFLAKE_INVALID;
    }

    /* check before modifying */
    if (ctx->seq->sequence + 1U > SF_SEQ_MASK && ctx->seq->timestamp >= wall) {
        unlock(ctx->sem);
        /* wait to the next [ms] */
        usleep(ONE_MS_USLEEP);
        if (infinite_loop_guard++ > SNOWFLAKE_FAIL_WAIT) { goto fail; }
        goto restart; 
    }

    /* we are in the clear get the sequence */
    if (ctx->seq->timestamp < wall) {
        ctx->seq->timestamp = wall;
        ctx->seq->sequence = 0;
    } else if (ctx->seq->timestamp > wall) {
        /* somehow we are behind in time, so set our time to the biggest known
         * timestampe for a sequence.
         */
        wall = ctx->seq->timestamp;
        ctx->wall_clock = ctx->seq->timestamp;
        ctx->init_clock = ts;
        sequence = ++ctx->seq->sequence;
    } else {
        sequence = ++ctx->seq->sequence;
    }
    unlock(ctx->sem);
    
    return (wall       & SF_TIMESTAMP_MASK) << (SF_NS_BS)  |
           (ctx->node  & SF_NODE_MASK)      << (SF_SEQ_BS) |
           (sequence   & SF_SEQ_MASK);
fail:
    return SNOWFLAKE_INVALID;
}

#endif /* SNOWFLAKE_IMPLEMENTATION */
/** @} */
