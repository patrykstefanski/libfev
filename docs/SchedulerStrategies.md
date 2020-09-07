# Scheduler strategies

The main role of libfev's schedulers is to schedule runnable fibers. libfev implements few
strategies to achieve that. They can be split into 2 categories: work sharing and work stealing.

## Work sharing

Work sharing schedulers use a single, global queue for runnable fibers, the queue is shared by all
workers.

The main advantage of these strategies is that fibers are scheduled fairly (FIFO). However, since
the queue is shared, a high contention on the queue is possible.

Note that libfev implements cooperative multitasking, so it is possible that some fibers will get
much more CPU time that others.

### work-sharing-bounded-mpmc

A [bounded MPMC queue](http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue)
is used for the queue implementation. Since the queue is bounded, there may be not enough space for
all runnable fibers. In that case, an additional fallback queue is used, which is protected by a
global mutex. The fallback queue is based on an intrusive list.

### work-sharing-locking

This strategy uses an intrusive linked list for its global queue implementation. The list is
protected by a global mutex.

### work-sharing-simple-mpmc

This strategy implements a simple lock-free queue based on
[Michael Scott's queue](https://www.cs.rochester.edu/u/scott/papers/1996_PODC_queues.pdf).

It needs to allocate memory for each node. A memory pool is used for the allocation. The nodes are
never released to the underlying operating system.

If an allocation fails, the whole process is aborted. This is the only strategy that can fail due to
failed allocation in user space.

## Work stealing

These strategies use a queue per worker for runnable fibers. The scheduling is in most cases
independent. A fiber that becomes runnable (for example a newly spawned fiber) is pushed to the
current worker's queue. If a worker is out of work, it tries to steal some fibers from other workers
before going to sleep.

The main advantage is that a contention of the queues is minimized, and thus these strategies should
scale better. However, it is possible that some fibers will be scheduled more often than others, and
thus the scheduling is not fair.

### work-stealing-bounded-mpmc

A [bounded MPMC queue](http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue)
per worker is created. If a queue is full, the fiber is pushed to a global fallback queue based on
an intrusive list. The fallback queue is protected by a global mutex.

### work-stealing-bounded-spmc

A bounded SPMC queue per worker is created. Similarly to work-stealing-bounded-mpmc, if a queue is
full, the fiber is pushed to a global fallback queue, which is protected by a global mutex.

### work-stealing-locking

An intrusive list per worker is created. Each list is protected by its own lock.

The queue's lock strategy can be controlled by **FEV_SCHED_STEAL_LOCKING_LOCK** option. Currently,
a mutex or a spinlock can be used as the queue's lock.

## Further reading

* https://tokio.rs/blog/2019-10-scheduler
* http://www.cs.columbia.edu/~aho/cs6998/reports/12-12-11_DeshpandeSponslerWeiss_GO.pdf
