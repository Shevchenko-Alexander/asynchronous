// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2014
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <set>
#include <vector>

#include <boost/lexical_cast.hpp>

#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/scheduler/threadpool_scheduler.hpp>

#include <boost/asynchronous/servant_proxy.hpp>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/trackable_servant.hpp>
#include <boost/asynchronous/algorithm/parallel_transform.hpp>

#include "test_common.hpp"

#include <boost/test/unit_test.hpp>

using namespace boost::asynchronous::test;

namespace
{

// main thread id
boost::thread::id main_thread_id;
bool servant_dtor=false;

struct my_exception : virtual boost::exception, virtual std::exception
{
    virtual const char* what() const throw()
    {
        return "my_exception";
    }
};

struct Servant : boost::asynchronous::trackable_servant<>
{
    typedef int simple_ctor;
    Servant(boost::asynchronous::any_weak_scheduler<> scheduler)
        : boost::asynchronous::trackable_servant<>(scheduler,
                                                   boost::asynchronous::create_shared_scheduler_proxy(
                                                       new boost::asynchronous::threadpool_scheduler<
                                                           boost::asynchronous::lockfree_queue<> >(6)))
    {
        generate();
    }

    ~Servant()
    {
        BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant dtor not posted.");
        servant_dtor = true;
    }

    boost::shared_future<void> start_parallel_transform()
    {
        BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant async work not posted.");

        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp = get_worker();
        auto data_copy = m_data;
        auto result_copy = m_result;

        // start long tasks
        post_callback(
            [tp, this]()
            {
                BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant work not posted.");
                std::vector<boost::thread::id> ids = tp.thread_ids();
                BOOST_CHECK_MESSAGE(contains_id(ids.begin(), ids.end(), boost::this_thread::get_id()), "task executed in the wrong thread");
                return boost::asynchronous::parallel_transform(this->m_data.begin(), this->m_data.end(), this->m_result.begin(), [](int i) { return ++i; }, 1500, "", 0);
            },
            [aPromise, tp, data_copy, result_copy, this](boost::asynchronous::expected<void> res) mutable
            {
                BOOST_CHECK_MESSAGE(!res.has_exception(), "servant work threw an exception.");
                BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant callback in main thread.");
                std::vector<boost::thread::id> ids = tp.thread_ids();
                BOOST_CHECK_MESSAGE(!contains_id(ids. begin(), ids.end(), boost::this_thread::get_id()), "task callback executed in the wrong thread(pool)");
                BOOST_CHECK_MESSAGE(!res.has_exception(), "servant work threw an exception.");
                std::transform(data_copy.begin(), data_copy.end(), result_copy.begin(), [](int i) { return ++i; });
                BOOST_CHECK_MESSAGE(result_copy == this->m_result, "parallel_transform gave a wrong value.");
                // reset
                generate();
                aPromise->set_value();
            }
        );

        return fu;
    }

    boost::shared_future<void> start_parallel_transform2()
    {
        BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant async work not posted.");

        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp = get_worker();
        auto data_copy = m_data;
        auto data2_copy = m_data;
        auto result_copy = m_result;

        // start long tasks
        post_callback(
            [tp, this]()
            {
                BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant work not posted.");
                std::vector<boost::thread::id> ids = tp.thread_ids();
                BOOST_CHECK_MESSAGE(contains_id(ids.begin(), ids.end(), boost::this_thread::get_id()), "task executed in the wrong thread");
                return boost::asynchronous::parallel_transform(this->m_data.begin(), this->m_data.end(), this->m_data2.begin(), this->m_result.begin(), [](int i, int j) { return i + j; }, 1500, "", 0);
            },
            [aPromise, tp, data_copy, data2_copy, result_copy, this](boost::asynchronous::expected<void> res) mutable
            {
                BOOST_CHECK_MESSAGE(!res.has_exception(), "servant work threw an exception.");
                BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant callback in main thread.");
                std::vector<boost::thread::id> ids = tp.thread_ids();
                BOOST_CHECK_MESSAGE(!contains_id(ids. begin(), ids.end(), boost::this_thread::get_id()), "task callback executed in the wrong thread(pool)");
                BOOST_CHECK_MESSAGE(!res.has_exception(), "servant work threw an exception.");
                std::transform(data_copy.begin(), data_copy.end(), data2_copy.begin(), result_copy.begin(), [](int i, int j) { return i + j; });
                BOOST_CHECK_MESSAGE(result_copy == this->m_result, "parallel_transform gave a wrong value.");
                // reset
                generate();
                aPromise->set_value();
            }
        );

        return fu;
    }

    boost::shared_future<void> start_parallel_transform_range()
    {
        BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant async work not posted.");

        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp = get_worker();
        auto data_copy = m_data;
        auto result_copy = m_result;

        // start long tasks
        post_callback(
            [tp, this]()
            {
                BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant work not posted.");
                std::vector<boost::thread::id> ids = tp.thread_ids();
                BOOST_CHECK_MESSAGE(contains_id(ids.begin(), ids.end(), boost::this_thread::get_id()), "task executed in the wrong thread");
                return boost::asynchronous::parallel_transform(this->m_data, this->m_result.begin(), [](int i) { return ++i; }, 1500, "", 0);
            },
            [aPromise, tp, data_copy, result_copy, this](boost::asynchronous::expected<void> res) mutable
            {
                BOOST_CHECK_MESSAGE(!res.has_exception(), "servant work threw an exception.");
                BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant callback in main thread.");
                std::vector<boost::thread::id> ids = tp.thread_ids();
                BOOST_CHECK_MESSAGE(!contains_id(ids. begin(), ids.end(), boost::this_thread::get_id()), "task callback executed in the wrong thread(pool)");
                BOOST_CHECK_MESSAGE(!res.has_exception(), "servant work threw an exception.");
                std::transform(data_copy.begin(), data_copy.end(), result_copy.begin(), [](int i) { return ++i; });
                BOOST_CHECK_MESSAGE(result_copy == this->m_result, "parallel_transform gave a wrong value.");
                // reset
                generate();
                aPromise->set_value();
            }
        );

        return fu;
    }

    boost::shared_future<void> start_parallel_transform2_range()
    {
        BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant async work not posted.");

        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp = get_worker();
        auto data_copy = m_data;
        auto data2_copy = m_data;
        auto result_copy = m_result;

        // start long tasks
        post_callback(
            [tp, this]()
            {
                BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant work not posted.");
                std::vector<boost::thread::id> ids = tp.thread_ids();
                BOOST_CHECK_MESSAGE(contains_id(ids.begin(), ids.end(), boost::this_thread::get_id()), "task executed in the wrong thread");
                return boost::asynchronous::parallel_transform(this->m_data, this->m_data2, this->m_result.begin(), [](int i, int j) { return i + j; }, 1500, "", 0);
            },
            [aPromise, tp, data_copy, data2_copy, result_copy, this](boost::asynchronous::expected<void> res) mutable
            {
                BOOST_CHECK_MESSAGE(!res.has_exception(), "servant work threw an exception.");
                BOOST_CHECK_MESSAGE(main_thread_id != boost::this_thread::get_id(), "servant callback in main thread.");
                std::vector<boost::thread::id> ids = tp.thread_ids();
                BOOST_CHECK_MESSAGE(!contains_id(ids. begin(), ids.end(), boost::this_thread::get_id()), "task callback executed in the wrong thread(pool)");
                BOOST_CHECK_MESSAGE(!res.has_exception(), "servant work threw an exception.");
                std::transform(data_copy.begin(), data_copy.end(), data2_copy.begin(), result_copy.begin(), [](int i, int j) { return i + j; });
                BOOST_CHECK_MESSAGE(result_copy == this->m_result, "parallel_transform gave a wrong value.");
                // reset
                generate();
                aPromise->set_value();
            }
        );

        return fu;
    }

private:
    // helper, generate vectors
    void generate()
    {
        m_data = std::vector<int>(10000, 1);
        std::random_device rd;
        std::mt19937 mt(rd());
        std::uniform_int_distribution<> dis(0, 1000);
        std::generate(m_data.begin(), m_data.end(), std::bind(dis, std::ref(mt)));

        m_data2 = m_data;

        m_result.resize(m_data.size());
    }

    std::vector<int> m_data;
    std::vector<int> m_data2;
    std::vector<int> m_result;
};

class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy, Servant>
{
public:
    template<class Scheduler>
    ServantProxy(Scheduler scheduler)
        : boost::asynchronous::servant_proxy<ServantProxy, Servant>(scheduler)
    {}

    BOOST_ASYNC_FUTURE_MEMBER(start_parallel_transform)
    BOOST_ASYNC_FUTURE_MEMBER(start_parallel_transform2)
    BOOST_ASYNC_FUTURE_MEMBER(start_parallel_transform_range)
    BOOST_ASYNC_FUTURE_MEMBER(start_parallel_transform2_range)
};

}

BOOST_AUTO_TEST_CASE( test_parallel_transform )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<> >);

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_parallel_transform();
        try
        {
            boost::shared_future<void> resfuv = fuv.get();
            resfuv.get();
        }
        catch(...)
        {
            BOOST_FAIL( "unexpected exception" );
        }
    }
    BOOST_CHECK_MESSAGE(servant_dtor,"servant dtor not called.");
}

BOOST_AUTO_TEST_CASE( test_parallel_transform2 )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<> >);

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_parallel_transform2();
        try
        {
            boost::shared_future<void> resfuv = fuv.get();
            resfuv.get();
        }
        catch(...)
        {
            BOOST_FAIL( "unexpected exception" );
        }
    }
    BOOST_CHECK_MESSAGE(servant_dtor,"servant dtor not called.");
}

BOOST_AUTO_TEST_CASE( test_parallel_transform_range )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<> >);

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_parallel_transform_range();
        try
        {
            boost::shared_future<void> resfuv = fuv.get();
            resfuv.get();
        }
        catch(...)
        {
            BOOST_FAIL( "unexpected exception" );
        }
    }
    BOOST_CHECK_MESSAGE(servant_dtor,"servant dtor not called.");
}

BOOST_AUTO_TEST_CASE( test_parallel_transform2_range )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<> >);

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_parallel_transform2_range();
        try
        {
            boost::shared_future<void> resfuv = fuv.get();
            resfuv.get();
        }
        catch(...)
        {
            BOOST_FAIL( "unexpected exception" );
        }
    }
    BOOST_CHECK_MESSAGE(servant_dtor,"servant dtor not called.");
}
