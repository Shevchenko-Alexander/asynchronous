
// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2013
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#include <vector>
#include <set>
#include <future>

#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/scheduler/threadpool_scheduler.hpp>

#include <boost/asynchronous/servant_proxy.hpp>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/trackable_servant.hpp>

#include <boost/test/unit_test.hpp>
 
namespace
{
// main thread id
boost::thread::id main_thread_id;
bool task_called=false;
bool dtor_called=false;
struct void_task
{
    void_task()=default;
    void_task(void_task&&)=default;
    void_task& operator=(void_task&&)=default;
    void_task(void_task const&)=delete;
    void_task& operator=(void_task const&)=delete;

    void operator()()const
    {
        BOOST_FAIL( "unexpected call of task" );
    }
};

struct Servant : boost::asynchronous::trackable_servant<>
{
    typedef int simple_ctor;
    // optional, dtor is simple enough not to be waited for (no complicated dependency to other servants' schedulers)
    typedef int simple_dtor;

    Servant(boost::asynchronous::any_weak_scheduler<> scheduler)
        : boost::asynchronous::trackable_servant<>(scheduler,
                                               boost::asynchronous::make_shared_scheduler_proxy<
                                                   boost::asynchronous::threadpool_scheduler<
                                                           boost::asynchronous::lockfree_queue<>>>(1))
        , m_dtor_done(new std::promise<void>)
    {
    }
    ~Servant()
    {
        dtor_called=true;
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant dtor not posted.");
        this->m_tracking.reset();
        if (!!m_dtor_done)
            m_dtor_done->set_value();
    }
    void start_endless_async_work(std::shared_ptr<std::promise<void> > startp,std::shared_future<void> end)
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant start_endless_async_work not posted.");
        // start long tasks
        post_callback(
           [startp,end]()mutable{ task_called=true;startp->set_value();end.get();},// work
           [](boost::asynchronous::expected<void> ){ BOOST_FAIL( "unexpected call of callback" );}// should not be called
        );
    }
    void start_endless_async_work2()
    {
        // post blocking funtion, until dtor done
        std::shared_future<void> fu=m_dtor_done->get_future();
        auto blocking = [fu]() mutable {fu.get();};
        get_worker().post(blocking);

        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant start_endless_async_work2 not posted.");
        // start long task, which will never be called
        post_callback(
                void_task(),// work
                [](boost::asynchronous::expected<void> ){ BOOST_FAIL( "unexpected call of callback" );}// should not be called
        );
    }

// for start_endless_async_work2
std::shared_ptr<std::promise<void> > m_dtor_done;
};

class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant>
{
public:
    template <class Scheduler>
    ServantProxy(Scheduler s):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s)
    {}
    BOOST_ASYNC_FUTURE_MEMBER(start_endless_async_work)
    BOOST_ASYNC_FUTURE_MEMBER(start_endless_async_work2)
};

}

BOOST_AUTO_TEST_CASE( test_trackable_alive_callback_check )
{     
    main_thread_id = boost::this_thread::get_id();
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();
        
        std::promise<void> p;
        std::shared_future<void> end=p.get_future();
        std::shared_ptr<std::promise<void> > startp(new std::promise<void>);
        std::shared_future<void> start=startp->get_future();
        {
            ServantProxy proxy(scheduler);
            auto fuv = proxy.start_endless_async_work(startp,end);
            // wait for task to start
            start.get();
        }
        // servant is gone, try to provoke wrong callback
        p.set_value();
        BOOST_CHECK_MESSAGE(task_called,"servant task not called.");
    }
    // at this point, the dtor has been called
    BOOST_CHECK_MESSAGE(dtor_called,"servant dtor not called.");
}

BOOST_AUTO_TEST_CASE( test_trackable_alive_task_check )
{
    dtor_called =false;
    main_thread_id = boost::this_thread::get_id();
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();
        
       {
           ServantProxy proxy(scheduler);
           auto fuv = proxy.start_endless_async_work2();
           // wait for task to start
           fuv.get();
       }
    }
    // at this point, the dtor has been called
    BOOST_CHECK_MESSAGE(dtor_called,"servant dtor not called.");
}
