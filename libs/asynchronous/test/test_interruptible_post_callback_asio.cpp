// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2013
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#include <future>

#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/extensions/asio/asio_scheduler.hpp>
#include <boost/asynchronous/servant_proxy.hpp>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/checks.hpp>
#include <boost/asynchronous/trackable_servant.hpp>
#include <boost/test/unit_test.hpp>

namespace
{
// main thread id
boost::thread::id main_thread_id;

struct Servant : boost::asynchronous::trackable_servant<>
{
    typedef int simple_ctor;
    Servant(boost::asynchronous::any_weak_scheduler<> scheduler)
        : boost::asynchronous::trackable_servant<>(scheduler,
                                               boost::asynchronous::make_shared_scheduler_proxy<
                                                   boost::asynchronous::asio_scheduler<>>(1))
        , m_ready(new std::promise<void>)
    {
    }
    ~Servant(){BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant dtor not posted.");}

    std::future<boost::asynchronous::any_interruptible> start_async_work(std::shared_ptr<std::promise<void> > p)
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant start_async_work not posted.");
        std::promise<boost::asynchronous::any_interruptible> apromise;
        std::future<boost::asynchronous::any_interruptible> fu = apromise.get_future();
        // start long tasks
        boost::asynchronous::any_interruptible interruptible =
        interruptible_post_callback(
               [p](){p->set_value();boost::this_thread::sleep(boost::posix_time::milliseconds(1000000));},
               [](boost::asynchronous::expected<void> ){BOOST_ERROR( "unexpected call of callback" );}// should not be called
        );
        apromise.set_value(interruptible);
        return fu;
    }
    std::pair<
       std::shared_ptr<std::promise<void> >,
       std::future<boost::asynchronous::any_interruptible>
    >
    start_async_work2()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant start_async_work2 not posted.");
        
        // post blocking funtion, until dtor done
        std::shared_future<void> fu=m_ready->get_future();
        auto blocking = [fu]() mutable {fu.get();};
        get_worker().post(blocking);
        
        std::promise<boost::asynchronous::any_interruptible> apromise;
        std::future<boost::asynchronous::any_interruptible> fu2 = apromise.get_future();
        // start long tasks
        boost::asynchronous::any_interruptible interruptible =
        interruptible_post_callback(
           [](){BOOST_ERROR( "unexpected call of task" );},
           [](boost::asynchronous::expected<void> ){BOOST_ERROR( "unexpected call of callback" );}// should not be called
        );
        apromise.set_value(interruptible);
        return std::make_pair(m_ready,std::move(fu2));
    }

private:
    std::shared_ptr<std::promise<void> > m_ready;
};
class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant>
{
public:
    template <class Scheduler>
    ServantProxy(Scheduler s):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s)
    {}
    BOOST_ASYNC_FUTURE_MEMBER(start_async_work)
    BOOST_ASYNC_FUTURE_MEMBER(start_async_work2)
};
}

BOOST_AUTO_TEST_CASE( test_interrupt_running_task_asio )
{     
    main_thread_id = boost::this_thread::get_id();
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();
        std::shared_ptr<std::promise<void> > p(new std::promise<void>);
        std::future<void> end=p->get_future();
        {
            ServantProxy proxy(scheduler);
            std::future<std::future<boost::asynchronous::any_interruptible> > fu = proxy.start_async_work(p);
            std::future<boost::asynchronous::any_interruptible> resfu = fu.get();
            boost::asynchronous::any_interruptible res = resfu.get();
            end.get();
            res.interrupt();
        }
    }
}

BOOST_AUTO_TEST_CASE( test_interrupt_not_running_task_asio )
{     
    main_thread_id = boost::this_thread::get_id();
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();
        {
            typedef std::pair<
                    std::shared_ptr<std::promise<void> >,
                    std::future<boost::asynchronous::any_interruptible>
                 > res_type;
            
            ServantProxy proxy(scheduler);
            std::future<res_type> fu = proxy.start_async_work2();
            res_type res = fu.get();
            boost::asynchronous::any_interruptible i = res.second.get();
            // provoke interrupt before job starts
            i.interrupt();
            // now let the job try to execute
            res.first->set_value();
        }
    }
}

