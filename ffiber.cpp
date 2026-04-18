#include "ffiber.h"

namespace ff {

thread_local fiber *globals::current_fiber = nullptr;
thread_local detail::scheduler *globals::current_scheduler = nullptr;

static constexpr uint64_t CANARY_VALUE = 0xDEADC0DEDEADC0DE;

void context::make(context *ctx, void *stack_mem, uint32_t stack_size,
                   void (*func)()) {
    assert(ctx != nullptr && "context::make: ctx is null");
    assert(stack_mem != nullptr && "context::make: stack_mem is null");
    assert(stack_size >= 128 &&
           "context::make: stack_size is unreasonably small");
    assert(func != nullptr && "context::make: func is null");

    uint64_t *bottom = (uint64_t *)stack_mem;
    *bottom = CANARY_VALUE;

    uint64_t *stack = (uint64_t *)((char *)stack_mem + stack_size);
    *(--stack) = 0;
    *(--stack) = (uint64_t)func;
    for (int i = 0; i < 6; ++i)
        *(--stack) = 0;
    ctx->rsp = stack;
}

void detail::scheduler::step() {
    assert(globals::current_scheduler != nullptr && "step: scheduler is null");

    globals::current_scheduler = this;

    for (size_t i = 0; i < _waiting.size();) {
        int ix = _waiting[i];
        assert(ix >= 0 && (size_t)ix < _fibers.size() &&
               "step: waiting ix out of bounds");

        fiber &f = _fibers[ix];
        assert(f.state == fiber::suspended &&
               "step: waiting fiber not suspended");

        if (f.waiting_for && f.waiting_for->value.load() <= 0) {
            f.state = fiber::ready;
            _yielded.push_back(ix);
            _waiting[i] = _waiting.back();
            _waiting.pop_back();
        } else {
            i++;
        }
    }

    std::vector<int> to_run;
    to_run.swap(_yielded);

    for (int ix : to_run) {
        assert(ix >= 0 && (size_t)ix < _fibers.size() &&
               "step: yielded ix out of bounds");

        fiber &f = _fibers[ix];
        assert(f.state == fiber::ready &&
               "step: yielded fiber not in ready state");

        uint64_t *canary = (uint64_t *)_stacks[ix];
        assert(*canary == CANARY_VALUE && "stack overflow detected on fiber");

        f.state = fiber::running;
        globals::current_fiber = &f;
        switch_context(&_context.rsp, f.cx.rsp);

        assert(*canary == CANARY_VALUE && "stack overflow detected on fiber");
    }

    while (!_jobs.empty() && !_free_fibers.empty()) {
        int ix = _free_fibers.back();
        assert(ix >= 0 && (size_t)ix < _fibers.size() &&
               "step: free fiber ix out of bounds");

        fiber *idle_f = &_fibers[ix];
        assert(idle_f->state == fiber::idle && "step: free fiber not idle");

        uint64_t *canary = (uint64_t *)_stacks[ix];
        assert(*canary == CANARY_VALUE && "stack overflow on idle fiber");

        _free_fibers.pop_back();
        idle_f->current_job = _jobs.front();
        assert(idle_f->current_job.func != nullptr &&
               "step: dequeued job has null func");
        _jobs.pop();

        idle_f->state = fiber::running;
        globals::current_fiber = idle_f;
        switch_context(&_context.rsp, idle_f->cx.rsp);

        assert(*canary == CANARY_VALUE && "stack overflow after job dispatch");
    }
}

void yield() {
    fiber *self = globals::current_fiber;
    detail::scheduler *sched = globals::current_scheduler;

    assert(self != nullptr && "yield() called outside a fiber");
    assert(sched != nullptr && "yield() called without a scheduler");
    assert(self->state == fiber::running &&
           "yield() called on non-running fiber");

    self->state = fiber::ready;
    sched->add_yield(self->ix);
    switch_context(&self->cx.rsp, sched->get_context().rsp);

    assert(self->state == fiber::running &&
           "fiber resumed in wrong state after yield");
}

void wait(counter *c) {
    fiber *self = globals::current_fiber;
    detail::scheduler *sched = globals::current_scheduler;

    assert(self != nullptr && "wait() called outside a fiber");
    assert(sched != nullptr && "wait() called without a scheduler");
    assert(c != nullptr && "wait() called with null counter");
    assert(self->state == fiber::running &&
           "wait() called on non-running fiber");

    if (c->value.load() <= 0)
        return;

    self->waiting_for = c;
    self->state = fiber::suspended;
    sched->add_waiting(self->ix);
    switch_context(&self->cx.rsp, sched->get_context().rsp);

    assert(self->state == fiber::running &&
           "fiber resumed in wrong state after wait");

    while (c->value.load() > 0) {
        self->waiting_for = c;
        self->state = fiber::suspended;
        sched->add_waiting(self->ix);
        switch_context(&self->cx.rsp, sched->get_context().rsp);
        assert(self->state == fiber::running &&
               "fiber resumed in wrong state in wait loop");
    }

    self->waiting_for = nullptr;
    self->state = fiber::running;
}

void fiber_entry_point() {
    while (true) {
        fiber *self = globals::current_fiber;
        detail::scheduler *sched = globals::current_scheduler;

        assert(self != nullptr && "fiber_entry_point: current_fiber is null");
        assert(sched != nullptr &&
               "fiber_entry_point: current_scheduler is null");
        assert(self->state == fiber::running &&
               "fiber_entry_point: fiber not running");

        if (self->current_job.func) {
            self->current_job.func(self->current_job.data);

            if (self->current_job.decrements) {
                int prev = self->current_job.decrements->value.fetch_sub(1);
                assert(prev > 0 && "counter decremented below zero — "
                                   "double-free or mismatched kickntrack/wait");
            }

            self->current_job = {nullptr, nullptr};
            self->waiting_for = nullptr;
            self->state = fiber::idle;

            sched->add_free_fiber(self->ix);
        }

        switch_context(&self->cx.rsp, sched->get_context().rsp);
    }
}

} // namespace ff

#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
void *ff::util::allocator_mmap::allocate(size_t size) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    void *mem = mmap(nullptr, size + page, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
        return nullptr;
    mprotect(mem, page, PROT_NONE); // guard page
    return (char *)mem + page;
}
void ff::util::allocator_mmap::deallocate(void *ptr, size_t size) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    munmap((char *)ptr - page, size + page);
}
#endif
