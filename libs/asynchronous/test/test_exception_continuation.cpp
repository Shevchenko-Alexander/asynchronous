
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
typedef std::vector<int>::iterator Iterator;

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
                                                   boost::asynchronous::make_shared_scheduler_proxy<
                                                        boost::asynchronous::threadpool_scheduler<
                                                           boost::asynchronous::lockfree_queue<>>>(6))
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
        std::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();

        // start long tasks
        post_callback(
            [this]()
            {
                BOOST_THROW_EXCEPTION( my_exception());
                return boost::asynchronous::parallel_transform(this->m_data.begin(), this->m_data.end(), this->m_result.begin(), [](int i) { return ++i; }, 1500, "", 0);
            },
            [aPromise, this](boost::asynchronous::expected<Iterator> res) mutable
            {
                BOOST_CHECK_MESSAGE(res.has_exception(), "work should have thrown an exception.");
                bool failed=false;
                try
                {
                    res.get();
                }
                catch(my_exception&)
                {
                    failed=true;
                }
                BOOST_CHECK_MESSAGE(failed,"task should have thrown my_exception.");
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

        m_result.resize(m_data.size());
    }

    std::vector<int> m_data;
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
};

}

BOOST_AUTO_TEST_CASE( test_exception_before_continuation )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();

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
BOOST_AUTO_TEST_CASE( test_exception_before_continuation_post_future )
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<
                            boost::asynchronous::threadpool_scheduler<
                                boost::asynchronous::lockfree_queue<>>>(6);

        std::vector<int> data(10000, 1);
        std::vector<int> result(10000, 1);
        auto fu =
        boost::asynchronous::post_future(
                     scheduler,
                     [&data,&result]()
                     {
                         BOOST_THROW_EXCEPTION( my_exception());
                         return boost::asynchronous::parallel_transform(
                                     data.begin(), data.end(), result.begin(), [](int i) { return ++i; }, 1500, "", 0);
                     },"",0);
        bool failed=false;
        try
        {
            fu.get();
        }
        //TODO why does my_exception not work?
        catch(std::exception&)
        {
            failed =true;
        }
        BOOST_CHECK_MESSAGE(failed,"task should have thrown an exception.");
    }
}
