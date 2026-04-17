# ffiber

Lightweight stackful fiber/job system for C++17. Run many small tasks concurrently without blocking threads.

## How it works

Each fiber has its own stack. When a job calls `wait()`, the fiber suspends and the scheduler immediately picks up another job — no thread is ever blocked. Context switches happen entirely in user space with no kernel involvement.

## Usage

```cpp
#include "ffiber.h"

ff::counter done;

void my_job(void *data) {
    // do work
    done.value.fetch_sub(1);
}

void my_waiter(void *) {
    ff::wait(&done);
    // all jobs finished
}

int main() {
    ff::init(64, 64 * 1024);  // 64 fibers, 64KB stack each

    ff::kick({my_waiter, nullptr});
    ff::kickntrack({my_job, nullptr}, &done);
    ff::kickntrack({my_job, nullptr}, &done);
    ff::kickntrack({my_job, nullptr}, &done);

    while (true) ff::step();  // drive the scheduler

    ff::shutdown();
}
```

## API

```cpp
ff::init(fiber_count, stack_size);   // initialize the scheduler for this thread
ff::kick(job);                        // submit a job
ff::kickntrack(job, &counter);        // submit a job and increment a counter
ff::step();                           // process one scheduler tick
ff::shutdown();                       // free all resources

ff::yield();                          // suspend current fiber, resume later
ff::wait(&counter);                   // suspend until counter reaches zero
```

## Fiber count

Each suspended or yielded fiber holds onto its fiber slot until it finishes. As a rule of thumb, allocate at least as many fibers as you expect to have concurrently suspended jobs. 128–256 fibers per thread is a reasonable starting point.

Stack memory usage: `fiber_count * stack_size`. 128 fibers at 64KB = 8MB.

## Custom allocator

```cpp
struct my_allocator {
    static void *allocate(size_t size)  { return my_alloc(size); }
    static void  deallocate(void *ptr)  { my_free(ptr); }
};

ff::init<my_allocator>(64, 64 * 1024);
ff::shutdown<my_allocator>();
```

## Multi-threading

Each thread gets its own isolated scheduler via `thread_local`. Call `ff::init` and `ff::step` on each worker thread independently. There is no job stealing between threads.

```cpp
void worker(int id) {
    ff::init(64, 64 * 1024);
    // kick and step jobs...
    ff::shutdown();
}

std::thread t1(worker, 0);
std::thread t2(worker, 1);
```

## Building

Just drop `ffiber.h` and `ffiber.cpp` into your project. You also need `switch_context`, a small assembly routine for your target platform that saves and restores registers and swaps the stack pointer.

Currently supported platforms:
- x86-64 Linux (System V ABI)
- x86-64 macOS (System V ABI)

Windows support (Microsoft ABI) is not yet included.

## License

MIT
