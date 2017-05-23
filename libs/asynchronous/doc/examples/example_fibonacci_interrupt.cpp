#include <iostream>
#include <future>

#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/scheduler/multiqueue_threadpool_scheduler.hpp>
#include <boost/asynchronous/continuation_task.hpp>

#include <boost/asynchronous/servant_proxy.hpp>
#include <boost/asynchronous/trackable_servant.hpp>

using namespace std;

namespace
{
template<typename R>
bool is_ready(std::future<R> const& f)
{ return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }

// a simnple, single-threaded fibonacci function used for cutoff
long serial_fib( long n ) {

    if( n<2 )
        return n;
    else
    {
        // check if interrupted
        // it is inefficient to test it at each call but we just want to show how it's done
        // we left checks for interrput only every few calls as exercise to the reader...
        if (n%10==0)
        {
            boost::this_thread::interruption_point();
        }
        return serial_fib(n-1)+serial_fib(n-2);
    }
}

// our recursive fibonacci tasks. Needs to inherit continuation_task<value type returned by this task>
struct fib_task : public boost::asynchronous::continuation_task<long>
{
    fib_task(long n,long cutoff):n_(n),cutoff_(cutoff){}
    void operator()()const
    {
        // the result of this task, will be either set directly if < cutoff, otherwise when taks is ready
        boost::asynchronous::continuation_result<long> task_res = this_task_result();
        if (n_<cutoff_)
        {
            // n < cutoff => execute ourselves
            task_res.set_value(serial_fib(n_));
        }
        else
        {
            // n> cutoff, create 2 new tasks and when both are done, set our result (res(task1) + res(task2))
            boost::asynchronous::create_continuation(
                        // called when subtasks are done, set our result
                        [task_res](std::tuple<std::future<long>,std::future<long> > res)
                        {
                            if (!boost::asynchronous::is_ready(std::get<0>(res)) || !boost::asynchronous::is_ready(std::get<1>(res)))
                            {
                                // oh we got interrupted and have no value, give up
                                return;
                            }
                            long r = std::get<0>(res).get() + std::get<1>(res).get();
                            task_res.set_value(r);
                        },
                        // recursive tasks
                        fib_task(n_-1,cutoff_),
                        fib_task(n_-2,cutoff_));
        }
    }
    long n_;
    long cutoff_;
};

struct Servant : boost::asynchronous::trackable_servant<>
{
    // optional, ctor is simple enough not to be posted
    typedef int simple_ctor;
    Servant(boost::asynchronous::any_weak_scheduler<> scheduler, int threads)
        : boost::asynchronous::trackable_servant<>(scheduler,
                                               // threadpool and a simple lockfree_queue queue
                                               boost::asynchronous::make_shared_scheduler_proxy<
                                                   boost::asynchronous::multiqueue_threadpool_scheduler<
                                                           boost::asynchronous::lockfree_queue<>>>(threads))
        // for testing purpose
        , m_promise(new std::promise<long>)
    {
    }
    // called when task done, in our thread
    void on_callback(long res)
    {
        //std::cout << "Callback in single-thread scheduler with value:" << res << std::endl;
        // inform test caller
        m_promise->set_value(res);
    }
    // call to this is posted and executes in our (safe) single-thread scheduler
    std::tuple<std::future<long>, boost::asynchronous::any_interruptible> calc_fibonacci(long n,long cutoff)
    {
        // for testing purpose
        std::future<long> fu = m_promise->get_future();
        // start long tasks in threadpool (first lambda) and callback in our thread
        boost::asynchronous::any_interruptible interruptible =
        interruptible_post_callback(
                [n,cutoff]()
                {
                     // a top-level continuation is the first one in a recursive serie.
                     // Its result will be passed to callback
                     return boost::asynchronous::top_level_continuation<long>(fib_task(n,cutoff));
                 }// work
               ,
               // callback with fibonacci result.
               [this](boost::asynchronous::expected<long> res){
                            std::cout << "called CB of interruptible_post_callback" << std::endl;
                            std::cout << "future has value: " << std::boolalpha << res.has_value() << std::endl; //false
                            std::cout << "future has exception: " << std::boolalpha << res.has_exception() << std::endl; //true, task_aborted_exception
                            if (res.has_value())
                            {
                                std::cout << "calling on_callback of interruptible_post_callback" << std::endl;
                                this->on_callback(res.get());
                            }
                            else
                            {
                                std::cout << "Oops no nalue, give up" << std::endl;
                            }
               }// callback functor.
        );
        return std::make_tuple(std::move(fu),interruptible);
    }
private:
// for testing
std::shared_ptr<std::promise<long> > m_promise;
};
class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant>
{
public:
    template <class Scheduler>
    ServantProxy(Scheduler s, int threads):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s,threads)
    {}
    // caller will get a future
    BOOST_ASYNC_FUTURE_MEMBER(calc_fibonacci)
};

}

void example_fibonacci_interrupt(long fibo_val,long cutoff, int threads)
{
    std::cout << "example_fibonacci_interrupt" << std::endl;
    {
        // a single-threaded world, where Servant will live.
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<
                                boost::asynchronous::single_thread_scheduler<
                                     boost::asynchronous::lockfree_queue<>>>();
        {
            ServantProxy proxy(scheduler,threads);
            std::future<std::tuple<std::future<long>, boost::asynchronous::any_interruptible>  > fu = proxy.calc_fibonacci(fibo_val,cutoff);
            std::tuple<std::future<long>, boost::asynchronous::any_interruptible>  resfu = std::move(fu.get());
            // ok we decide it takes too long, interrupt
            boost::this_thread::sleep(boost::posix_time::milliseconds(30));
            std::get<1>(resfu).interrupt();
            boost::this_thread::sleep(boost::posix_time::milliseconds(1000));
            // as we interrupt we might have no result
            bool has_value = is_ready(std::get<0>(resfu));
            std::cout << "do we have a value? " << std::boolalpha << has_value << std::endl;
            if (has_value)
            {
                std::cout << "value: " << std::get<0>(resfu).get() << std::endl;
            }
        }
    }
    std::cout << "end example_fibonacci_interrupt \n" << std::endl;
}

