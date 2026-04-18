#ifndef FFIBER_H
#define FFIBER_H

#include <atomic>
#include <cassert>
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
    static void deallocate(void *ptr, size_t /*size*/) { free(ptr); }
};

#ifdef __linux__
struct allocator_mmap {
    static void *allocate(size_t size);
    static void deallocate(void *ptr, size_t size);
};
#endif

} // namespace util

struct counter {
    std::atomic<int> value{0};
};

struct job {
    void (*func)(void *);
    void *data;
    counter *decrements = nullptr;
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
        if (!_stacks.empty()) {
            for (const auto &f : _fibers)
                assert(f.state == fiber::idle &&
                       "init() called while fibers are still active");
            for (void *s : _stacks)
                Allocator::deallocate(s, /*old stack size*/ _stack_size);
            _stacks.clear();
            _fibers.clear();
            _free_fibers.clear();
            _yielded.clear();
            _waiting.clear();
            while (!_jobs.empty())
                _jobs.pop();
        }

        assert(fiber_count > 0 && "fiber_count must be > 0");
        assert(stack_size >= 128 && "stack_size is unreasonably small");

        _fibers.resize(fiber_count);
        for (size_t i = 0; i < fiber_count; i++) {
            _fibers[i] =
                fiber{context{nullptr}, job{nullptr, nullptr}, fiber::idle};

            void *allocated = Allocator::allocate(stack_size);
            assert(allocated != nullptr && "stack allocation failed");

            _stacks.push_back(allocated);
            context::make(&_fibers[i].cx, allocated, stack_size,
                          fiber_entry_point);
            _fibers[i].ix = (int)i;
            _free_fibers.push_back((int)i);
        }
        _stack_size = stack_size;
    }

    void kick(job j) {
        assert(j.func != nullptr && "kicked a job with null func");
        _jobs.push(j);
    }

    void kickntrack(job j, counter *c) {
        assert(c != nullptr && "kickntrack called with null counter");
        assert(j.func != nullptr && "kickntrack called with null func");
        c->value.fetch_add(1);
        j.decrements = c;
        kick(j);
    }

    void step();

    context &get_context() { return _context; }

    void add_yield(int ix) {
        assert(ix >= 0 && (size_t)ix < _fibers.size() &&
               "add_yield: ix out of bounds");
        assert(_fibers[ix].state == fiber::ready &&
               "add_yield: fiber not in ready state");
        _yielded.push_back(ix);
    }

    void add_waiting(int ix) {
        assert(ix >= 0 && (size_t)ix < _fibers.size() &&
               "add_waiting: ix out of bounds");
        assert(_fibers[ix].state == fiber::suspended &&
               "add_waiting: fiber not suspended");
        _waiting.push_back(ix);
    }

    void add_free_fiber(int ix) {
        assert(ix >= 0 && (size_t)ix < _fibers.size() &&
               "add_free_fiber: ix out of bounds");
        assert(_fibers[ix].state == fiber::idle &&
               "add_free_fiber: fiber not idle");
        _free_fibers.push_back(ix);
    }

    void *get_stack(int ix) {
        assert(ix >= 0 && (size_t)ix < _stacks.size() &&
               "get_stack: ix out of bounds");
        return _stacks[ix];
    }

    size_t get_stack_size() {
        assert(_stack_size > 0 && "get_stack_size called before init()");
        return _stack_size;
    }

    template <typename Allocator = util::allocator_malloc> void shutdown_all() {
        for (const auto &f : _fibers)
            assert(f.state == fiber::idle &&
                   "shutdown_all() called with non-idle fibers");

        for (void *s : _stacks)
            Allocator::deallocate(s, _stack_size);

        _fibers.clear();
        _stacks.clear();
        _yielded.clear();
        _waiting.clear();
        _free_fibers.clear();
        while (!_jobs.empty())
            _jobs.pop();
        _stack_size = 0;
    }

  private:
    context _context;
    std::vector<fiber> _fibers;
    std::vector<int> _yielded;
    std::vector<int> _waiting;
    std::vector<int> _free_fibers;
    std::vector<void *> _stacks;
    std::queue<job> _jobs;
    size_t _stack_size = 0;
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

inline void kick(job j) {
    assert(globals::current_scheduler != nullptr &&
           "kick() called before init()");
    globals::current_scheduler->kick(j);
}

inline void kickntrack(job j, counter *c) {
    assert(globals::current_scheduler != nullptr &&
           "kickntrack() called before init()");
    globals::current_scheduler->kickntrack(j, c);
}

inline void step() {
    assert(globals::current_scheduler != nullptr &&
           "step() called before init()");
    globals::current_scheduler->step();
}

void yield();
void wait(counter *c);

template <typename Allocator = util::allocator_malloc> inline void shutdown() {
    assert(globals::current_scheduler != nullptr &&
           "shutdown() called before init()");
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
