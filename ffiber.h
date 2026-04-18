#ifndef FFIBER_H
#define FFIBER_H

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <queue>
#include <vector>

extern "C" {
void switch_context(void **old_stack, void *new_stack);
}

namespace ff {

namespace util {
struct allocator_malloc {
    static void *allocate(size_t size) { return malloc(size); }
    static void deallocate(void *ptr) { free(ptr); }
};
} // namespace util

struct counter {
    std::atomic<int> value{0};
};

struct job {
    void (*func)(void *);
    void *data;
};

struct context {
    void *rsp;
    static void make(context *cx, void *stack_memory, uint32_t stack_size,
                     void (*func)());
};

struct fiber {
    context cx;
    job current_job;
    enum { idle, running, suspended, ready } state = idle;
    int ix;
    counter *waiting_for = nullptr;
};

extern void fiber_entry_point();

// --- internal scheduler (not part of the public API) ---
namespace detail {

struct scheduler {
    template <typename Allocator = util::allocator_malloc>
    void init(size_t fiber_count, size_t stack_size) {
        _fibers.resize(fiber_count);
        for (size_t i = 0; i < fiber_count; i++) {
            _fibers[i] =
                fiber{context{nullptr}, job{nullptr, nullptr}, fiber::idle};

            void *allocated = Allocator::allocate(stack_size);
            _stacks.push_back(allocated);
            context::make(&_fibers[i].cx, allocated, stack_size,
                          fiber_entry_point);
            _fibers[i].ix = (int)i;
            _free_fibers.push_back((int)i);
        }
    }

    void kick(job j) { _jobs.push(j); }

    void kickntrack(job j, counter *c) {
        c->value.fetch_add(1);
        kick(j);
    }

    void step();

    context get_context() { return _context; }
    void add_yield(int ix) { _yielded.push_back(ix); }
    void add_waiting(int ix) { _waiting.push_back(ix); }
    void add_free_fiber(int ix) { _free_fibers.push_back(ix); }

    template <typename Allocator = util::allocator_malloc> void shutdown_all() {
        for (void *s : _stacks)
            Allocator::deallocate(s);
    }

  private:
    context _context;
    std::vector<fiber> _fibers;
    std::vector<int> _yielded;
    std::vector<int> _waiting;
    std::vector<int> _free_fibers;
    std::vector<void *> _stacks;
    std::queue<job> _jobs;
};

} // namespace detail

namespace globals {
extern thread_local fiber *current_fiber;
extern thread_local detail::scheduler *current_scheduler;
} // namespace globals

// --- public free-function API ---

template <typename Allocator = util::allocator_malloc>
void init(size_t fiber_count, size_t stack_size) {
    if (!globals::current_scheduler)
        globals::current_scheduler = new detail::scheduler();
    globals::current_scheduler->init<Allocator>(fiber_count, stack_size);
}

inline void kick(job j) { globals::current_scheduler->kick(j); }

inline void kickntrack(job j, counter *c) {
    globals::current_scheduler->kickntrack(j, c);
}

inline void step() { globals::current_scheduler->step(); }

void yield();
void wait(counter *c);

template <typename Allocator = util::allocator_malloc> inline void shutdown() {
    globals::current_scheduler->shutdown_all<Allocator>();
    delete globals::current_scheduler;
    globals::current_scheduler = nullptr;
    globals::current_fiber = nullptr;
}
} // namespace ff

#define FF_JOB(x, setdata)                                                     \
    {                                                                          \
        .func = [](void *data) x,                                              \
        .data = setdata,                                                       \
    }

#endif // FFIBER_H
