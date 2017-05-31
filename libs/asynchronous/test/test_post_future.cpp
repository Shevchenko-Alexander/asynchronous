
// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2013
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/scheduler/threadpool_scheduler.hpp>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/diagnostics/any_loggable.hpp>

#include <boost/test/unit_test.hpp>

namespace
{
typedef boost::asynchronous::any_loggable servant_job;
typedef std::map<std::string,std::list<boost::asynchronous::diagnostic_item> > diag_type;

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
struct my_exception : boost::asynchronous::asynchronous_exception
{
    virtual const char* what() const throw()
    {
        return "my_exception";
    }
};
struct throwing_int_task
{
    throwing_int_task()=default;
    throwing_int_task(throwing_int_task&&)=default;
    throwing_int_task& operator=(throwing_int_task&&)=default;
    throwing_int_task(throwing_int_task const&)=delete;
    throwing_int_task& operator=(throwing_int_task const&)=delete;

    int operator()()const
    {
        ASYNCHRONOUS_THROW(my_exception());
        return 0;
    }
};
struct throwing_void_task
{
    throwing_void_task()=default;
    throwing_void_task(throwing_void_task&&)=default;
    throwing_void_task& operator=(throwing_void_task&&)=default;
    throwing_void_task(throwing_void_task const&)=delete;
    throwing_void_task& operator=(throwing_void_task const&)=delete;

    void operator()()const
    {
        ASYNCHRONOUS_THROW(my_exception());
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
    std::future<void> fuv = boost::asynchronous::post_future(scheduler, void_task());
    fuv.get();
    BOOST_CHECK_MESSAGE(called,"post_future<void> didn't call task.");
}

BOOST_AUTO_TEST_CASE( test_int_post_future )
{
    auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::threadpool_scheduler<
                                                                        boost::asynchronous::lockfree_queue<>>>(1);
    std::future<int> fui = boost::asynchronous::post_future(scheduler, int_task());
    int res = fui.get();
    BOOST_CHECK_MESSAGE(42 == res,"post_future<int> returned wrong value.");
}

//TODO repair
BOOST_AUTO_TEST_CASE( test_interruptible_void_post_future )
{
    auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::threadpool_scheduler<
                                                                        boost::asynchronous::lockfree_queue<>>>(1);


    std::tuple<std::future<void>,boost::asynchronous::any_interruptible> res = boost::asynchronous::interruptible_post_future(scheduler, blocking_void_task());
    // we let the task start
    boost::this_thread::sleep(boost::posix_time::milliseconds(2000));
    std::get<1>(res).interrupt();
    bool is_interrupted=false;
    try
    {
        std::get<0>(res).get();
    }
    catch (boost::asynchronous::task_aborted_exception& e)
    {
        //normal flow
        is_interrupted = true;
    }
    catch (...)
    {
        // can happen if interrupt very fast, before we can start anything
    }
    BOOST_CHECK_MESSAGE(is_interrupted,"interruptible_post_future<void> not interrupted.");
    BOOST_CHECK_MESSAGE(!called2,"interruptible_post_future<void> called task.");
}

BOOST_AUTO_TEST_CASE( test_interruptible_int_post_future )
{
    auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::threadpool_scheduler<
                                                                        boost::asynchronous::lockfree_queue<>>>(1);


    std::tuple<std::future<int>,boost::asynchronous::any_interruptible> res = boost::asynchronous::interruptible_post_future(scheduler, blocking_int_task());
    int task_res=0;
    // we let the task start
    boost::this_thread::sleep(boost::posix_time::milliseconds(2000));
    std::get<1>(res).interrupt();
    bool is_interrupted=false;
    try
    {
        task_res = std::get<0>(res).get();
    }
    catch (boost::asynchronous::task_aborted_exception&)
    {
        //normal flow
        is_interrupted = true;
    }
    catch (...)
    {
        // can happen if interrupt very fast, before we can start anything
    }
    BOOST_CHECK_MESSAGE(is_interrupted,"interruptible_post_future<void> not interrupted.");
    BOOST_CHECK_MESSAGE(task_res == 0,"interruptible_post_future<int> called task.");
}

BOOST_AUTO_TEST_CASE( test_throw_int_post_future )
{
    auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::threadpool_scheduler<
                                                                        boost::asynchronous::lockfree_queue<servant_job>>>(1);
    std::future<int> fui = boost::asynchronous::post_future(scheduler, throwing_int_task(),"throwing_int_task",0);
    bool future_with_exception = false;
    try
    {
        fui.get();
    }
    catch(my_exception& e)
    {
        future_with_exception = true;
        // check for correct exception data
        BOOST_CHECK_MESSAGE(std::string(e.what_) == "my_exception","test_throw_int_post_future has wrong data");
        BOOST_CHECK_MESSAGE(!std::string(e.file_).empty(),"test_throw_int_post_future has wrong data");
        BOOST_CHECK_MESSAGE(e.line_ != -1,"test_throw_int_post_future has wrong data");
    }
    catch(...)
    {
    }

    BOOST_CHECK_MESSAGE(future_with_exception,"post_future<throwing_int_task> did not throw.");
    boost::this_thread::sleep(boost::posix_time::milliseconds(1000));

    // check if we found a throwing task
    diag_type diag = scheduler.get_diagnostics().totals();

    for (auto mit = diag.begin(); mit != diag.end() ; ++mit)
    {
        if ((*mit).first != "throwing_int_task")
        {
            continue;
        }
        for (auto jit = (*mit).second.begin(); jit != (*mit).second.end();++jit)
        {
            BOOST_CHECK_MESSAGE((*jit).is_failed(),"task should have been marked as failed.");
        }
    }
}
BOOST_AUTO_TEST_CASE( test_throw_void_post_future )
{
    auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::threadpool_scheduler<
                                                                        boost::asynchronous::lockfree_queue<servant_job>>>(1);
    auto fu = boost::asynchronous::post_future(scheduler, throwing_void_task(),"throwing_void_task",0);
    bool future_with_exception = false;
    try
    {
        fu.get();
    }
    catch(my_exception& e)
    {
        future_with_exception = true;
        // check for correct exception data
        BOOST_CHECK_MESSAGE(std::string(e.what_) == "my_exception","test_throw_int_post_future has wrong data");
        BOOST_CHECK_MESSAGE(!std::string(e.file_).empty(),"test_throw_int_post_future has wrong data");
        BOOST_CHECK_MESSAGE(e.line_ != -1,"test_throw_int_post_future has wrong data");
    }
    catch(...)
    {
    }

    BOOST_CHECK_MESSAGE(future_with_exception,"post_future<throwing_void_task> did not throw.");
    boost::this_thread::sleep(boost::posix_time::milliseconds(1000));

    // check if we found a throwing task
    diag_type diag = scheduler.get_diagnostics().totals();

    for (auto mit = diag.begin(); mit != diag.end() ; ++mit)
    {
        if ((*mit).first != "throwing_void_task")
        {
            continue;
        }
        for (auto jit = (*mit).second.begin(); jit != (*mit).second.end();++jit)
        {
            BOOST_CHECK_MESSAGE((*jit).is_failed(),"task should have been marked as failed.");
        }
    }
}
