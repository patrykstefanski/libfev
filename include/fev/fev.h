/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_FEV_H
#define FEV_FEV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <fev/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>

/* C extensions */

#ifdef __GNUC__
#define FEV_GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#if FEV_GCC_VERSION < 40000
#error Your GCC is too old, please upgrade it!
#endif
#else
#define FEV_GCC_VERSION 0
#endif

#ifdef __has_attribute
#define FEV_HAS_ATTRIBUTE(attr) __has_attribute(attr)
#else
#define FEV_HAS_ATTRIBUTE(attr) 0
#endif

#if FEV_HAS_ATTRIBUTE(__nonnull__) || FEV_GCC_VERSION
#define FEV_NONNULL(...) __attribute__((__nonnull__(__VA_ARGS__)))
#else
#define FEV_NONNULL(...) /* __attribute__((__nonnull__(__VA_ARGS__))) */
#endif

#if FEV_HAS_ATTRIBUTE(__noreturn__) || FEV_GCC_VERSION
#define FEV_NORETURN __attribute__((__noreturn__))
#else
#define FEV_NORETURN /* __attribute__((__noreturn__)) */
#endif

#if FEV_HAS_ATTRIBUTE(__pure__) || _FEV_GCC_VERSION
#define FEV_PURE __attribute__((__pure__))
#else
#define FEV_PURE /* __attribute__((__pure__)) */
#endif

/* Types */

struct fev_cond;
struct fev_fiber;
struct fev_fiber_attr;
struct fev_mutex;
struct fev_sched;
struct fev_sched_attr;
struct fev_sem;
struct fev_socket;

typedef void *(*fev_realloc_t)(void *ptr, size_t size);

/* The functions return 0 on success or a negative error code (such as -ENOMEM) on failure. */

/* Allocator */

/*
 * This allows the user to set a custom allocator for all allocations except fiber's stack
 * allocations (which use mmap() and you can set customs stacks in fiber's attributes).
 * fev_set_realloc() should be called before any other function. By default it points to libc's
 * realloc().
 */

FEV_PURE fev_realloc_t fev_get_realloc(void);

FEV_NONNULL(1) void fev_set_realloc(fev_realloc_t realloc_ptr);

/* Scheduler attributes */

FEV_NONNULL(1) int fev_sched_attr_create(struct fev_sched_attr **attr_ptr);

FEV_NONNULL(1) void fev_sched_attr_destroy(struct fev_sched_attr *attr);

FEV_NONNULL(1) FEV_PURE uint32_t fev_sched_attr_get_num_workers(const struct fev_sched_attr *attr);

FEV_NONNULL(1)
void fev_sched_attr_set_num_workers(struct fev_sched_attr *attr, uint32_t num_workers);

/* Scheduler */

FEV_NONNULL(1)
int fev_sched_create(struct fev_sched **sched_ptr, const struct fev_sched_attr *attr);

FEV_NONNULL(1) void fev_sched_destroy(struct fev_sched *sched);

FEV_NONNULL(1) int fev_sched_run(struct fev_sched *sched);

/* Fiber attributes */

FEV_NONNULL(1) int fev_fiber_attr_create(struct fev_fiber_attr **attr_ptr);

FEV_NONNULL(1) void fev_fiber_attr_destroy(struct fev_fiber_attr *attr);

FEV_NONNULL(1, 2, 3)
void fev_fiber_attr_get_stack(const struct fev_fiber_attr *attr, void **stack_addr,
                              size_t *stack_size);

FEV_NONNULL(1, 2)
int fev_fiber_attr_set_stack(struct fev_fiber_attr *attr, void *stack_addr, size_t stack_size);

FEV_NONNULL(1) FEV_PURE size_t fev_fiber_attr_get_stack_size(const struct fev_fiber_attr *attr);

FEV_NONNULL(1) int fev_fiber_attr_set_stack_size(struct fev_fiber_attr *attr, size_t stack_size);

FEV_NONNULL(1) FEV_PURE size_t fev_fiber_attr_get_guard_size(const struct fev_fiber_attr *attr);

FEV_NONNULL(1) int fev_fiber_attr_set_guard_size(struct fev_fiber_attr *attr, size_t guard_size);

FEV_NONNULL(1) FEV_PURE bool fev_fiber_attr_get_detached(const struct fev_fiber_attr *attr);

FEV_NONNULL(1) void fev_fiber_attr_set_detached(struct fev_fiber_attr *attr, bool detached);

/* Fiber */

/*
 * Creates a new fiber in 'sched'. The new fiber starts execution by calling start_routine(arg).
 *
 * If 'sched' is NULL, the fiber will be created in the current scheduler. Thus, NULL can only be
 * passed if the fiber is created inside of another fiber.
 *
 * If you are creating a fiber not in another fiber (e.g. in main()), you have to pass the scheduler
 * where it should be created and scheduled. Currently, the specified scheduler cannot be running
 * yet.
 *
 * Typically, you should do:
 * 1. Create a scheduler.
 * 2. Spawn initial fibers and specify that scheduler as 'sched' param.
 * 3. Run the scheduler.
 * 4. New fibers should be created/spawned in other fibers, where 'sched' param should be NULL.
 */
FEV_NONNULL(1, 3)
int fev_fiber_create(struct fev_fiber **fiber_ptr, struct fev_sched *sched,
                     void *(*start_routine)(void *), void *arg, const struct fev_fiber_attr *attr);

/* Creates a detached fiber in 'sched'. */
FEV_NONNULL(2)
int fev_fiber_spawn(struct fev_sched *sched, void *(*start_routine)(void *), void *arg);

/* Terminates the calling fiber. */
FEV_NORETURN void fev_fiber_exit(void *return_value);

/* Detaches 'fiber'. This can be only called from another fiber. */
FEV_NONNULL(1) int fev_fiber_detach(struct fev_fiber *fiber);

/* Joins 'fiber'. This can be only called from another fiber. */
FEV_NONNULL(1) int fev_fiber_join(struct fev_fiber *fiber, void **return_value_ptr);

/* Yields to the current scheduler, allowing another fiber to be scheduled. */
void fev_yield(void);

#if 0
FEV_NONNULL(1) void fev_sleep_for(const struct timespec *rel_time);

FEV_NONNULL(1) void fev_sleep_until(const struct timespec *abs_time);
#endif

/* Mutex */

/*
 * The mutex implementation uses handoff method and it is fair for lock()/unlock().
 * try_lock_for()/try_lock_until() can internally fail spuriously and thus are not fair.
 */

FEV_NONNULL(1) int fev_mutex_create(struct fev_mutex **mutex_ptr);

FEV_NONNULL(1) void fev_mutex_destroy(struct fev_mutex *mutex);

FEV_NONNULL(1) void fev_mutex_lock(struct fev_mutex *mutex);

/*
 * Acquires the mutex and returns true if the mutex can be locked. Returns false otherwise without
 * changing the state of the mutex.
 */
FEV_NONNULL(1) bool fev_mutex_try_lock(struct fev_mutex *mutex);

/*
 * fev_mutex_try_lock_for() and fev_mutex_try_lock_until() return 0 if the mutex is acquired
 * successfully within the specified timeout. Otherwise, it returns a _negative_ error code:
 * -ETIMEDOUT - If the specified timeout has expired.
 * -ENOMEM    - Insufficient memory exists to perform the operation (only if PEV_TIMERS is set to
 *              binheap).
 */

FEV_NONNULL(1, 2)
int fev_mutex_try_lock_for(struct fev_mutex *mutex, const struct timespec *rel_time);

FEV_NONNULL(1, 2)
int fev_mutex_try_lock_until(struct fev_mutex *mutex, const struct timespec *abs_time);

FEV_NONNULL(1) void fev_mutex_unlock(struct fev_mutex *mutex);

/* Condition variable */

/* _wait() functions can fail spuriously, be sure to recheck the condition. */

FEV_NONNULL(1) int fev_cond_create(struct fev_cond **cond_ptr);

FEV_NONNULL(1) void fev_cond_destroy(struct fev_cond *cond);

FEV_NONNULL(1) void fev_cond_notify_one(struct fev_cond *cond);

FEV_NONNULL(1) void fev_cond_notify_all(struct fev_cond *cond);

FEV_NONNULL(1, 2)
void fev_cond_wait(struct fev_cond *cond, struct fev_mutex *mutex);

FEV_NONNULL(1, 2, 3)
int fev_cond_wait_for(struct fev_cond *cond, struct fev_mutex *mutex,
                      const struct timespec *rel_time);

FEV_NONNULL(1, 2, 3)
int fev_cond_wait_until(struct fev_cond *cond, struct fev_mutex *mutex,
                        const struct timespec *abs_time);

/* Semaphore */

FEV_NONNULL(1) int fev_sem_create(struct fev_sem **sem_ptr, int32_t value);

FEV_NONNULL(1) void fev_sem_destroy(struct fev_sem *sem);

FEV_NONNULL(1) void fev_sem_post(struct fev_sem *sem);

FEV_NONNULL(1) void fev_sem_wait(struct fev_sem *sem);

FEV_NONNULL(1, 2) int fev_sem_wait_for(struct fev_sem *sem, const struct timespec *rel_time);

FEV_NONNULL(1, 2) int fev_sem_wait_until(struct fev_sem *sem, const struct timespec *abs_time);

/* Socket */

FEV_NONNULL(1) int fev_socket_create(struct fev_socket **socket_ptr);

FEV_NONNULL(1) void fev_socket_destroy(struct fev_socket *socket);

FEV_NONNULL(1) FEV_PURE int fev_socket_native_handle(const struct fev_socket *socket);

FEV_NONNULL(1)
int fev_socket_set_opt(struct fev_socket *socket, int level, int option_name,
                       const void *option_value, socklen_t option_len);

FEV_NONNULL(1) int fev_socket_open(struct fev_socket *socket, int domain, int type, int protocol);

FEV_NONNULL(1) int fev_socket_close(struct fev_socket *socket);

FEV_NONNULL(1, 2)
int fev_socket_bind(struct fev_socket *socket, const struct sockaddr *address,
                    socklen_t address_len);

FEV_NONNULL(1) int fev_socket_listen(struct fev_socket *socket, int backlog);

FEV_NONNULL(1, 2)
int fev_socket_accept(struct fev_socket *socket, struct fev_socket *new_socket,
                      struct sockaddr *address, socklen_t *address_len);

FEV_NONNULL(1, 2, 5)
int fev_socket_try_accept_for(struct fev_socket *socket, struct fev_socket *new_socket,
                              struct sockaddr *address, socklen_t *address_len,
                              const struct timespec *rel_time);

FEV_NONNULL(1, 2, 5)
int fev_socket_try_accept_until(struct fev_socket *socket, struct fev_socket *new_socket,
                                struct sockaddr *address, socklen_t *address_len,
                                const struct timespec *abs_time);

FEV_NONNULL(1, 2)
int fev_socket_connect(struct fev_socket *socket, struct sockaddr *address, socklen_t address_len);

FEV_NONNULL(1, 2, 4)
int fev_socket_try_connect_for(struct fev_socket *socket, struct sockaddr *address,
                               socklen_t address_len, const struct timespec *rel_time);

FEV_NONNULL(1, 2, 4)
int fev_socket_try_connect_until(struct fev_socket *socket, struct sockaddr *address,
                                 socklen_t address_len, const struct timespec *abs_time);

FEV_NONNULL(1, 2)
ssize_t fev_socket_read(struct fev_socket *socket, void *buffer, size_t size);

FEV_NONNULL(1, 2)
ssize_t fev_socket_try_read_for(struct fev_socket *socket, void *buffer, size_t size,
                                const struct timespec *rel_time);

FEV_NONNULL(1, 2)
ssize_t fev_socket_try_read_until(struct fev_socket *socket, void *buffer, size_t size,
                                  const struct timespec *abs_time);

FEV_NONNULL(1, 2)
ssize_t fev_socket_write(struct fev_socket *socket, const void *buffer, size_t size);

FEV_NONNULL(1, 2)
ssize_t fev_socket_try_write_for(struct fev_socket *socket, const void *buffer, size_t size,
                                 const struct timespec *rel_time);

FEV_NONNULL(1, 2)
ssize_t fev_socket_try_write_until(struct fev_socket *socket, const void *buffer, size_t size,
                                   const struct timespec *abs_time);

#ifdef __cplusplus
}
#endif

#endif /* !FEV_FEV_H */
