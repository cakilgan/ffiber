#include "ffiber.h"
namespace ff {
thread_local fiber *g_current_fiber = nullptr;
thread_local scheduler *g_current_scheduler = nullptr;

void context::make(context *ctx, void *stack_mem, uint32_t stack_size,
                   void (*func)()) {
    uint64_t *stack = (uint64_t *)((char *)stack_mem + stack_size);

    *(--stack) = 0;

    *(--stack) = (uint64_t)func;

    for (int i = 0; i < 6; ++i)
        *(--stack) = 0;

    ctx->rsp = stack;
}

void scheduler::step() {
    g_current_scheduler = this;

    for (size_t i = 0; i < _waiting.size();) {
        size_t ix = _waiting[i];
        fiber &f = _fibers[ix];

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

    for (size_t ix : to_run) {
        fiber &f = _fibers[ix];
        f.state = fiber::running;
        g_current_fiber = &f;

        switch_context(&_context.rsp, f.cx.rsp);
    }

    while (!_jobs.empty() && !_free_fibers.empty()) {
        fiber *idle_f = &_fibers[_free_fibers.back()];
        _free_fibers.pop_back();

        if (!idle_f)
            break;

        idle_f->current_job = _jobs.front();
        _jobs.pop();
        idle_f->state = fiber::running;

        g_current_fiber = idle_f;
        g_current_scheduler = this;

        switch_context(&_context.rsp, idle_f->cx.rsp);
    }
    g_current_scheduler = nullptr;
}
void yield() {
    fiber *self = g_current_fiber;
    scheduler *sched = g_current_scheduler;

    if (!self || !sched)
        return;

    self->state = fiber::ready;
    sched->add_yield(self->ix);

    switch_context(&self->cx.rsp, sched->get_context().rsp);

    self->state = fiber::running;
}
void wait(counter *c) {
    fiber *self = g_current_fiber;
    scheduler *sched = g_current_scheduler;

    if (!self || !sched)
        return;

    if (c->value.load() <= 0)
        return;

    self->waiting_for = c;
    self->state = fiber::suspended;
    sched->add_waiting(self->ix);

    switch_context(&self->cx.rsp, sched->get_context().rsp);

    self->waiting_for = nullptr;
    self->state = fiber::running;
}

void fiber_entry_point() {
    while (true) {
        fiber *self = g_current_fiber;
        scheduler *sched = g_current_scheduler;

        if (self->current_job.func) {
            self->current_job.func(self->current_job.data);

            self->current_job = {nullptr, nullptr};
            self->waiting_for = nullptr;
            self->state = fiber::idle;

            sched->add_free_fiber(self->ix);
        }

        switch_context(&self->cx.rsp, sched->get_context().rsp);
    }
}
} // namespace ff
