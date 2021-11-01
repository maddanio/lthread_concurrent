#include <thread>
#include <iostream>
#include <stdio.h>
#include <optional>

#include <lthread.h>
#include <lthread_int.h>
#include <lthread_cond.h>

static constexpr size_t n_iter = 200000;
size_t count = 0;

class timer_t
{
public:
    timer_t(const char* s) : _s{s}, _start{_lthread_usec_now()} {}
    ~timer_t()
    {
        auto end = _lthread_usec_now();
        auto d = end - _start;
        std::cerr << _s << " took " << (d * 1e-6) << "s" << std::endl;
    }
private:
    const char* _s;
    uint64_t _start;
};

void bench_thread()
{
    timer_t timer{"thread"};
    count = 0;
    for (size_t i = 0; i < n_iter; ++i)
    {
        std::thread t{[]{++count;}};
        t.join();
    }
    if (count != n_iter)
        std::cerr << "fail " << count << std::endl;
}

void bench_lthread()
{
    timer_t timer{"lthread"};
    count = 0;
    lthread_run([](void*){
        for (size_t i = 0; i < n_iter; ++i)
        {
            lthread_spawn([](void*){fprintf(stderr, "count %lu\n", ++count);}, NULL);
            //lthread_yield();
        }
    }, 0, 0, 1);
    if (count != n_iter)
        std::cerr << "fail " << count << std::endl;
}

template<typename value_t>
class generator_t
{
public:
    using push_fun_t = std::function<void(value_t)>;
    using fun_t = std::function<void(push_fun_t)>;
    generator_t(fun_t fun)
    : _fun{std::move(fun)}
    {
        lthread_cond_create(&_consume_cond);
        lthread_cond_create(&_produce_cond);
        lthread_spawn(
            [](void* arg){
                auto self = (generator_t*)arg;
                self->_fun([self](value_t value){
                    self->push(std::move(value));
                });
                self->push(std::nullopt);
            },
            this
        );
    }
    std::optional<value_t> pull()
    {
        lthread_cond_wait(_consume_cond, 0);
        if (_value)
            fprintf(stderr, "pull %lu\n", *_value);
        auto result = std::move(_value);
        lthread_cond_signal(_produce_cond);
        return result;
    }
    ~generator_t()
    {
        lthread_cond_free(_consume_cond);
        lthread_cond_free(_produce_cond);
    }
private:
    void push(std::optional<value_t> value)
    {
        if (value)
            fprintf(stderr, "push %lu\n", *value);
        _value = std::move(value);
        lthread_cond_signal(_consume_cond);
        if (_value)
            lthread_cond_wait(_produce_cond, 0);
    }
    lthread_cond* _consume_cond;
    lthread_cond* _produce_cond;
    fun_t _fun;
    std::optional<value_t> _value;
};

void bench_lthread_generator()
{
    timer_t timer{"lthread generator"};
    lthread_run([](void*){
        generator_t<size_t> generator{[](auto push){
            for (size_t i = 0; i < n_iter; ++i)
                push(n_iter - i - 1);
        }};
        for (size_t i = 0; i < n_iter; ++i)
        {
            auto value = generator.pull();
            auto target = n_iter - i - 1;
            if (target != value)
            {
                if (value)
                    fprintf(stderr, "fail %lu != %lu\n", target, *value);
                else
                    fprintf(stderr, "fail %lu != -\n", target);
            }
        }
        generator.pull();
    }, 0, 0, 2);
}

int main()
{
    //bench_thread();
    bench_lthread();
    //bench_lthread_generator();
    return 0;
}
