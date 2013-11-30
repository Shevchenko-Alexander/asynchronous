
#include <iostream>
#include <tuple>
#include <utility>

#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/queue/threadsafe_list.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/scheduler/multiqueue_threadpool_scheduler.hpp>
#include <boost/asynchronous/scheduler/detail/any_continuation.hpp>

#include <boost/asynchronous/servant_proxy.h>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/trackable_servant.hpp>
#include <boost/asynchronous/continuation_task.hpp>

#include "test_common.hpp"
#include <boost/test/unit_test.hpp>
using namespace boost::asynchronous::test;
namespace
{
// main thread id
boost::thread::id main_thread_id;
std::vector<boost::thread::id> tpids;
struct some_exception : virtual std::exception
{
};
// dummy task started by the main algorithm
// could be a lambda too of course
struct sub_task
{
    int operator()()const
    {
        BOOST_CHECK_MESSAGE(contains_id(tpids.begin(),tpids.end(),boost::this_thread::get_id()),"sub_task executed in the wrong thread");
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"sub_task in main thread.");
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        throw some_exception();
    }
};

// our main algo task. Needs to inherit continuation_task<value type returned by this task>
struct main_task : public boost::asynchronous::continuation_task<long>
{
    void operator()()const
    {
        BOOST_CHECK_MESSAGE(contains_id(tpids.begin(),tpids.end(),boost::this_thread::get_id()),"main_task executed in the wrong thread");
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"main_task in main thread.");

        // the result of this task
        boost::asynchronous::continuation_result<long> task_res = this_task_result();

        // we start calculation, then while doing this we see new tasks which can be posted and done concurrently to us
        // when all are done, we will set the result
        // to post tasks, we need a scheduler
        boost::asynchronous::any_weak_scheduler<> weak_scheduler = boost::asynchronous::get_thread_scheduler<>();
        boost::asynchronous::any_shared_scheduler<> locked_scheduler = weak_scheduler.lock();
        if (!locked_scheduler.is_valid())
            // ok, we are shutting down, ok give up
            return;
        // simulate algo work
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        // let's say we just found a subtask
        boost::future<int> fu1 = boost::asynchronous::post_future(locked_scheduler,sub_task());
        // simulate more algo work
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        // let's say we just found a subtask
        boost::future<int> fu2 = boost::asynchronous::post_future(locked_scheduler,sub_task());
        // simulate algo work
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        // let's say we just found a subtask
        boost::future<int> fu3 = boost::asynchronous::post_future(locked_scheduler,sub_task());

        // our algo is now done, wrap all and return
        boost::asynchronous::create_continuation(
                    // called when subtasks are done, set our result
                    [task_res](std::tuple<boost::future<int>,boost::future<int>,boost::future<int> > res)
                    {
                        try{
                            long r = std::get<0>(res).get() + std::get<1>(res).get()+ std::get<2>(res).get();
                            task_res.set_value(r);
                        }
                        catch(some_exception& e)
                        {
                            task_res.set_exception(boost::copy_exception(e));
                        }
                    },
                    // future results of recursive tasks
                    std::move(fu1),std::move(fu2),std::move(fu3));
    }
};

struct Servant : boost::asynchronous::trackable_servant<>
{
    // optional, ctor is simple enough not to be posted
    typedef int simple_ctor;
    Servant(boost::asynchronous::any_weak_scheduler<> scheduler)
        : boost::asynchronous::trackable_servant<>(scheduler,
                                               // threadpool and a simple threadsafe_list queue
                                               boost::asynchronous::create_shared_scheduler_proxy(
                                                   new boost::asynchronous::multiqueue_threadpool_scheduler<
                                                           boost::asynchronous::threadsafe_list<> >(6)))
        // for testing purpose
        , m_promise(new boost::promise<long>)
    {
    }
    // called when task done, in our thread
    void on_callback(long res)
    {
        // inform test caller
        m_promise->set_value(res);
    }
    // call to this is posted and executes in our (safe) single-thread scheduler
    boost::shared_future<long> calc_algo()
    {
        // for testing purpose
        boost::shared_future<long> fu = m_promise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp =get_worker();
        tpids = tp.thread_ids();
        // start long tasks in threadpool (first lambda) and callback in our thread
        post_callback(
                []()
                {
                     // a top-level continuation is the first one in a recursive serie.
                     // Its result will be passed to callback
                     return boost::asynchronous::top_level_continuation<long>(main_task());
                 }// work
               ,
               // callback with result.
               [this](boost::future<long> res){
                            BOOST_CHECK_MESSAGE(!contains_id(tpids.begin(),tpids.end(),boost::this_thread::get_id()),"algo callback executed in the wrong thread(pool)");
                            BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                            BOOST_CHECK_MESSAGE(!res.has_value(),"callback has an unexpected value.");
                            BOOST_CHECK_MESSAGE(res.has_exception(),"callback has no exception.");
                            try{
                                this->on_callback(res.get());
                            }
                            catch(std::exception& e)
                            {
                                this->on_callback(-1);
                            }
               }// callback functor.
        );
        return fu;
    }
private:
// for testing
boost::shared_ptr<boost::promise<long> > m_promise;
};
class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant>
{
public:
    template <class Scheduler>
    ServantProxy(Scheduler s):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s)
    {}
    // caller will get a future
    BOOST_ASYNC_FUTURE_MEMBER(calc_algo)
};

}

BOOST_AUTO_TEST_CASE( test_continuation_algo_except )
{
    {
        // a single-threaded world, where Servant will live.
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(
                                new boost::asynchronous::single_thread_scheduler<
                                     boost::asynchronous::threadsafe_list<> >);
        {
            ServantProxy proxy(scheduler);
            boost::shared_future<boost::shared_future<long> > fu = proxy.calc_algo();
            boost::shared_future<long> resfu = fu.get();
            long res = resfu.get();
            BOOST_CHECK_MESSAGE(-1 == res,"we didn't get the expected task result");
        }
    }
}


