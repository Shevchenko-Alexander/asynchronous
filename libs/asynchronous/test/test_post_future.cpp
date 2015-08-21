
// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2013
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/queue/threadsafe_list.hpp>
#include <boost/asynchronous/scheduler/threadpool_scheduler.hpp>
#include <boost/asynchronous/post.hpp>
#include <boost/test/unit_test.hpp>

namespace
{
// main thread id
boost::thread::id main_thread_id;
bool called=false;
bool called2=false;
struct void_task
{
    void operator()()const
    {
        called=true;
    }
};
struct int_task
{
    int_task()=default;
    int_task(int_task&&)=default;
    int_task& operator=(int_task&&)=default;
    int_task(int_task const&)=delete;
    int_task& operator=(int_task const&)=delete;

    int operator()()const
    {
        return 42;
    }
};
struct blocking_void_task
{
    blocking_void_task(){}
    void operator()()const
    {
        boost::this_thread::sleep(boost::posix_time::milliseconds(500000));
        // should never come there
        called2=true;
    }
};
struct blocking_int_task
{
    int operator()()const
    {
        boost::this_thread::sleep(boost::posix_time::milliseconds(500000));
        // should never come there
        return 42;
    }
};
}
BOOST_AUTO_TEST_CASE( test_void_post_future )
{
    auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::threadpool_scheduler<
                                                                        boost::asynchronous::lockfree_queue<>>>(1);
    boost::shared_future<void> fuv = boost::asynchronous::post_future(scheduler, void_task());
    fuv.get();
    BOOST_CHECK_MESSAGE(called,"post_future<void> didn't call task.");
}

BOOST_AUTO_TEST_CASE( test_int_post_future )
{
    auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::threadpool_scheduler<
                                                                        boost::asynchronous::lockfree_queue<>>>(1);
    boost::shared_future<int> fui = boost::asynchronous::post_future(scheduler, int_task());
    int res = fui.get();
    BOOST_CHECK_MESSAGE(42 == res,"post_future<int> returned wrong value.");
}

BOOST_AUTO_TEST_CASE( test_interruptible_void_post_future )
{
    auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::threadpool_scheduler<
                                                                        boost::asynchronous::lockfree_queue<>>>(1);


    std::tuple<boost::shared_future<void>,boost::asynchronous::any_interruptible> res = boost::asynchronous::interruptible_post_future(scheduler, blocking_void_task());
    // we let the task start
    boost::this_thread::sleep(boost::posix_time::milliseconds(200));
    std::get<1>(res).interrupt();
    try
    {
        std::get<0>(res).get();
    }
    catch (boost::asynchronous::task_aborted_exception&)
    {
        //normal flow
    }
    catch (std::exception&)
    {
        // can happen if interrupt very fast, before we can start anything
    }
    BOOST_CHECK_MESSAGE(!called2,"interruptible_post_future<void> called task.");
}

BOOST_AUTO_TEST_CASE( test_interruptible_int_post_future )
{
    auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::threadpool_scheduler<
                                                                        boost::asynchronous::lockfree_queue<>>>(1);


    std::tuple<boost::shared_future<int>,boost::asynchronous::any_interruptible> res = boost::asynchronous::interruptible_post_future(scheduler, blocking_int_task());
    int task_res=0;
    // we let the task start
    boost::this_thread::sleep(boost::posix_time::milliseconds(200));
    std::get<1>(res).interrupt();
    try
    {
        task_res = std::get<0>(res).get();
    }
    catch (boost::asynchronous::task_aborted_exception&)
    {
        //normal flow
    }
    catch (std::exception&)
    {
        // can happen if interrupt very fast, before we can start anything
    }
    BOOST_CHECK_MESSAGE(task_res == 0,"interruptible_post_future<int> called task.");
}
