// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2013
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#include <algorithm>
#include <iostream>
#include <vector>

#include <boost/smart_ptr/shared_array.hpp>
#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/servant_proxy.hpp>
#include <boost/asynchronous/scheduler/multiqueue_threadpool_scheduler.hpp>
#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/queue/any_queue_container.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/trackable_servant.hpp>
#include <boost/asynchronous/algorithm/parallel_for.hpp>

using namespace std;

#define SIZE 1000000
#define LOOP 100

float Foo(float f)
{
    return std::cos(std::tan(f*3.141592654 + 2.55756 * 0.42));
}

boost::chrono::high_resolution_clock::time_point servant_time;
double servant_intern=0.0;
long tpsize = 12;
long tasks = 48;

struct Servant : boost::asynchronous::trackable_servant<>
{
    typedef int simple_ctor;
    typedef int requires_weak_scheduler;
    Servant(boost::asynchronous::any_weak_scheduler<> scheduler)
        : boost::asynchronous::trackable_servant<>(scheduler,
                                               boost::asynchronous::make_shared_scheduler_proxy<
                                                   boost::asynchronous::multiqueue_threadpool_scheduler<
                                                           boost::asynchronous::lockfree_queue<>,
                                                           boost::asynchronous::default_find_position< boost::asynchronous::sequential_push_policy>,
                                                           boost::asynchronous::default_save_cpu_load<10,80000,1000>
                                                           //boost::asynchronous::no_cpu_load_saving
                                                       >>(tpsize,tasks/tpsize))
        , m_promise(new boost::promise<void>)
    {
    }
    ~Servant(){}

    // called when task done, in our thread
    void on_callback()
    {
        servant_intern += (boost::chrono::nanoseconds(boost::chrono::high_resolution_clock::now() - servant_time).count() / 1000);
        m_promise->set_value();
    }

    boost::shared_future<void> start_async_work(float a[], size_t n)
    {
        boost::shared_future<void> fu = m_promise->get_future();
        long tasksize = SIZE / tasks;
        servant_time = boost::chrono::high_resolution_clock::now();
        post_callback(
               [a,n,tasksize](){
                        return boost::asynchronous::parallel_for(a,a+n,
                                                                 [](float& i)
                                                                 {
                                                                    i = Foo(i);
                                                                 },tasksize,"",0);
                      },// work
               // the lambda calls Servant, just to show that all is safe, Servant is alive if this is called
               [this](boost::asynchronous::expected<void> /*res*/){
                            this->on_callback();
               }// callback functor.
               ,"",0,0
        );
        return fu;
    }
private:
// for testing
boost::shared_ptr<boost::promise<void> > m_promise;
};
class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant>
{
public:
    template <class Scheduler>
    ServantProxy(Scheduler s):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s)
    {}
    BOOST_ASYNC_FUTURE_MEMBER(start_async_work,0)
};
void ParallelAsyncPostCb(float a[], size_t n)
{
    auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<
                            boost::asynchronous::single_thread_scheduler<boost::asynchronous::lockfree_queue<>,
                                                                         boost::asynchronous::default_save_cpu_load<10,80000,1000>>>(tpsize);
    {
        ServantProxy proxy(scheduler);

        boost::shared_future<boost::shared_future<void> > fu = proxy.start_async_work(a,n);
        boost::shared_future<void> resfu = fu.get();
        resfu.get();
    }
}

void ParallelAsyncPostFuture(float a[], size_t n)
{
    auto scheduler =  boost::asynchronous::make_shared_scheduler_proxy<
            boost::asynchronous::multiqueue_threadpool_scheduler<
                    boost::asynchronous::lockfree_queue<>,
                    boost::asynchronous::default_find_position< boost::asynchronous::sequential_push_policy>,
                    boost::asynchronous::no_cpu_load_saving
                >>(tpsize,tasks/tpsize);
    // set processor affinity to improve cache usage. We start at core 0, until tpsize-1
    scheduler.processor_bind(0);

    long tasksize = SIZE / tasks;
    servant_time = boost::chrono::high_resolution_clock::now();
    auto fu = boost::asynchronous::post_future(scheduler,
               [a,n,tasksize]()
               {
                   return boost::asynchronous::parallel_for(a,a+n,
                                                            [](float& i)
                                                            {
                                                               i = Foo(i);
                                                            },tasksize,"",0);
               });
    fu.get();
    servant_intern += (boost::chrono::nanoseconds(boost::chrono::high_resolution_clock::now() - servant_time).count() / 1000);
}



void test(void(*pf)(float [], size_t ))
{
    boost::shared_array<float> a (new float[SIZE]);
    std::generate(a.get(), a.get()+SIZE,rand);
    (*pf)(a.get(),SIZE);
}

int main( int argc, const char *argv[] ) 
{   
    tpsize = (argc>1) ? strtol(argv[1],0,0) : boost::thread::hardware_concurrency();
    tasks = (argc>2) ? strtol(argv[2],0,0) : 500;
    std::cout << "tpsize=" << tpsize << std::endl;
    std::cout << "tasks=" << tasks << std::endl;
    for (int i=0;i<LOOP;++i)
    {     
        test(ParallelAsyncPostFuture);
    }
    printf ("%24s: time = %.1f usec\n","parallel async cb intern", servant_intern);
    return 0;
}
