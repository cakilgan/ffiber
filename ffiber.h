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

// namespace
namespace ff {

namespace util {
struct allocator_malloc {
    static void *allocate(size_t size) { return malloc(size); }
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

struct scheduler {
    template <typename Allocator = util::allocator_malloc>
    void init(size_t fiber_count, size_t stack_size) {
        _fibers.resize(fiber_count);
        for (size_t i{0}; i < fiber_count; i++) {
            _fibers[i] =
                fiber{context{nullptr}, job{nullptr, nullptr}, fiber::idle};
            context::make(&_fibers[i].cx, Allocator::allocate(stack_size),
                          stack_size, fiber_entry_point);
            _fibers[i].ix = i;
            _free_fibers.push_back(i);
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

  private:
    context _context;
    std::vector<fiber> _fibers;

    std::vector<int> _yielded;
    std::vector<int> _waiting;
    std::vector<int> _free_fibers;

    std::queue<job> _jobs;
};

void yield();
void wait(counter *c);

extern thread_local fiber *g_current_fiber;
extern thread_local scheduler *g_current_scheduler;

} // namespace ff
#endif // FFIBER_H
