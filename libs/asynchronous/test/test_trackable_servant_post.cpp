
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

#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/queue/threadsafe_list.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/scheduler/threadpool_scheduler.hpp>

#include <boost/asynchronous/servant_proxy.h>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/trackable_servant.hpp>

#include <boost/test/unit_test.hpp>

namespace
{
// main thread id
boost::thread::id main_thread_id;
bool task_called=false;
bool dtor_called=false;

struct Servant : boost::asynchronous::trackable_servant<>
{
    typedef int simple_ctor;
    Servant(boost::asynchronous::any_weak_scheduler<> scheduler)
        : boost::asynchronous::trackable_servant<>(scheduler,
                                               boost::asynchronous::create_shared_scheduler_proxy(
                                                   new boost::asynchronous::threadpool_scheduler<
                                                           boost::asynchronous::threadsafe_list<> >(1)))
        , m_dtor_done(new boost::promise<void>)
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
    void start_endless_async_work(boost::shared_ptr<boost::promise<void> > startp,boost::shared_future<void> end)
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant start_endless_async_work not posted.");
        // start long tasks
        post_future(
           [startp,end]()mutable{ task_called=true;startp->set_value();end.get();}// work
        );
    }

// for start_endless_async_work2
boost::shared_ptr<boost::promise<void> > m_dtor_done;
};

class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant>
{
public:
    template <class Scheduler>
    ServantProxy(Scheduler s):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s)
    {}
    BOOST_ASYNC_FUTURE_MEMBER(start_endless_async_work)
};

}

BOOST_AUTO_TEST_CASE( test_trackable_servant_post )
{
    main_thread_id = boost::this_thread::get_id();
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::threadsafe_list<> >);

        boost::promise<void> p;
        boost::shared_future<void> end=p.get_future();
        boost::shared_ptr<boost::promise<void> > startp(new boost::promise<void>);
        boost::shared_future<void> start=startp->get_future();
        {
            ServantProxy proxy(scheduler);
            boost::shared_future<void> fuv = proxy.start_endless_async_work(startp,end);
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


