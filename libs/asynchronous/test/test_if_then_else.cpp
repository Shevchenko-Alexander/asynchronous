

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
#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/scheduler/threadpool_scheduler.hpp>

#include <boost/asynchronous/servant_proxy.hpp>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/trackable_servant.hpp>
#include <boost/asynchronous/algorithm/parallel_for.hpp>
#include <boost/asynchronous/algorithm/if_then_else.hpp>

#include "test_common.hpp"

#include <boost/test/unit_test.hpp>
using namespace boost::asynchronous::test;

namespace
{
// main thread id
boost::thread::id main_thread_id;
bool servant_dtor=false;

struct Servant : boost::asynchronous::trackable_servant<>
{
    typedef int simple_ctor;
    Servant(boost::asynchronous::any_weak_scheduler<> scheduler)
        : boost::asynchronous::trackable_servant<>(scheduler,
                                                   boost::asynchronous::make_shared_scheduler_proxy<
                                                   boost::asynchronous::threadpool_scheduler<
                                                      boost::asynchronous::lockfree_queue<>>>(6))
        , m_data(10000,1)
    {
    }
    ~Servant()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant dtor not posted.");
        servant_dtor = true;
    }

    boost::shared_future<void> async_if_true()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant start_async_work_range3 not posted.");
        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp =get_worker();
        // start long tasks
        post_callback(
           [tp,this](){
                    BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant work not posted.");
                    std::vector<boost::thread::id> ids = tp.thread_ids();
                    BOOST_CHECK_MESSAGE(contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task executed in the wrong thread");
                    return boost::asynchronous::parallel_for(
                                    boost::asynchronous::if_then_else(
                                        [](std::vector<int> const&){return true;},
                                        [](std::vector<int> res)
                                        {
                                            std::vector<unsigned int> new_result(res.begin(),res.end());
                                            return boost::asynchronous::parallel_for(
                                                                std::move(new_result),
                                                                [](unsigned int const& i)
                                                                {
                                                                   const_cast<unsigned int&>(i) += 3;
                                                                },1500);
                                        },
                                        [](std::vector<int> res)
                                        {
                                            std::vector<unsigned int> new_result(res.begin(),res.end());
                                            return boost::asynchronous::parallel_for(
                                                                std::move(new_result),
                                                                [](unsigned int const& i)
                                                                {
                                                                   const_cast<unsigned int&>(i) += 4;
                                                                },1500);
                                        }
                                    )
                                    (
                                        boost::asynchronous::parallel_for(
                                                                         std::move(this->m_data),
                                                                         [](int const& i)
                                                                         {
                                                                            const_cast<int&>(i) += 2;
                                                                         },1500)
                                    ),
                        [](unsigned int const& i)
                        {
                           const_cast<unsigned int&>(i) += 1;
                        },1500
                    );
                    },// work
           [aPromise,tp,this](boost::asynchronous::expected<std::vector<unsigned int>> res){
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                        std::vector<boost::thread::id> ids = tp.thread_ids();
                        BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        auto modified_vec = res.get();
                        auto it = modified_vec.begin();
                        BOOST_CHECK_MESSAGE(*it == 7,"data[0] is wrong: "+ std::to_string(*it));
                        std::advance(it,100);
                        BOOST_CHECK_MESSAGE(*it == 7,"data[100] is wrong: "+ std::to_string(*it));
                        std::advance(it,900);
                        BOOST_CHECK_MESSAGE(*it == 7,"data[1000] is wrong: "+ std::to_string(*it));
                        std::advance(it,8999);
                        BOOST_CHECK_MESSAGE(*it == 7,"data[9999] is wrong: "+ std::to_string(*it));
                        auto r = std::accumulate(modified_vec.begin(),modified_vec.end(),0,[](int a, int b){return a+b;});
                        BOOST_CHECK_MESSAGE((r == 70000),
                                            ("result of parallel_for after if/else was " + std::to_string(r) +
                                             ", should have been 70000"));

                        // reset
                        m_data = std::vector<int>(10000,1);
                        aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }

    boost::shared_future<void> async_if_false()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant start_async_work_range3 not posted.");
        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp =get_worker();
        // start long tasks
        post_callback(
           [tp,this](){
                    BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant work not posted.");
                    std::vector<boost::thread::id> ids = tp.thread_ids();
                    BOOST_CHECK_MESSAGE(contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task executed in the wrong thread");
                    return boost::asynchronous::parallel_for(
                                    boost::asynchronous::if_then_else(
                                        [](std::vector<int> const&){return false;},
                                        [](std::vector<int> res)
                                        {
                                            std::vector<unsigned int> new_result(res.begin(),res.end());
                                            return boost::asynchronous::parallel_for(
                                                                std::move(new_result),
                                                                [](unsigned int const& i)
                                                                {
                                                                   const_cast<unsigned int&>(i) += 3;
                                                                },1500);
                                        },
                                        [](std::vector<int> res)
                                        {
                                            std::vector<unsigned int> new_result(res.begin(),res.end());
                                            return boost::asynchronous::parallel_for(
                                                                std::move(new_result),
                                                                [](unsigned int const& i)
                                                                {
                                                                   const_cast<unsigned int&>(i) += 4;
                                                                },1500);
                                        }
                                    )
                                    (
                                        boost::asynchronous::parallel_for(
                                                                         std::move(this->m_data),
                                                                         [](int const& i)
                                                                         {
                                                                            const_cast<int&>(i) += 2;
                                                                         },1500)
                                    ),
                        [](unsigned int const& i)
                        {
                           const_cast<unsigned int&>(i) += 1;
                        },1500
                    );
                    },// work
           [aPromise,tp,this](boost::asynchronous::expected<std::vector<unsigned int>> res){
                BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                std::vector<boost::thread::id> ids = tp.thread_ids();
                BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                auto modified_vec = res.get();
                auto it = modified_vec.begin();
                BOOST_CHECK_MESSAGE(*it == 8,"data[0] is wrong: "+ std::to_string(*it));
                std::advance(it,100);
                BOOST_CHECK_MESSAGE(*it == 8,"data[100] is wrong: "+ std::to_string(*it));
                std::advance(it,900);
                BOOST_CHECK_MESSAGE(*it == 8,"data[1000] is wrong: "+ std::to_string(*it));
                std::advance(it,8999);
                BOOST_CHECK_MESSAGE(*it == 8,"data[9999] is wrong: "+ std::to_string(*it));
                auto r = std::accumulate(modified_vec.begin(),modified_vec.end(),0,[](int a, int b){return a+b;});
                BOOST_CHECK_MESSAGE((r == 80000),
                                    ("result of parallel_for after if/else was " + std::to_string(r) +
                                     ", should have been 80000"));

                // reset
                m_data = std::vector<int>(10000,1);
                aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }

    boost::shared_future<void> async_elseif_true_if_true()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant start_async_work_range3 not posted.");
        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp =get_worker();
        // start long tasks
        post_callback(
           [tp,this](){
                    BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant work not posted.");
                    std::vector<boost::thread::id> ids = tp.thread_ids();
                    BOOST_CHECK_MESSAGE(contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task executed in the wrong thread");
                    return boost::asynchronous::parallel_for(
                                    boost::asynchronous::if_then_else(
                                        [](std::vector<int> const&){return true;},
                                        [](std::vector<int> res)
                                        {
                                            std::vector<unsigned int> new_result(res.begin(),res.end());
                                            return
                                              boost::asynchronous::if_then_else(
                                                [](std::vector<unsigned int> const&){return true;},
                                                [](std::vector<unsigned int> res)
                                                {
                                                    return boost::asynchronous::parallel_for(
                                                                        std::move(res),
                                                                        [](unsigned int const& i)
                                                                        {
                                                                           const_cast<unsigned int&>(i) += 5;
                                                                        },1500);
                                                },
                                                [](std::vector<unsigned int> res)
                                                {
                                                    return boost::asynchronous::parallel_for(
                                                                        std::move(res),
                                                                        [](unsigned int const& i)
                                                                        {
                                                                           const_cast<unsigned int&>(i) += 4;
                                                                        },1500);
                                                }
                                            )
                                            (
                                                boost::asynchronous::parallel_for(
                                                                                 std::move(new_result),
                                                                                 [](unsigned int const& i)
                                                                                 {
                                                                                    const_cast<unsigned int&>(i) += 3;
                                                                                 },1500)
                                            );
                                        },
                                        [](std::vector<int> res)
                                        {
                                            std::vector<unsigned int> new_result(res.begin(),res.end());
                                            return boost::asynchronous::parallel_for(
                                                                std::move(new_result),
                                                                [](unsigned int const& i)
                                                                {
                                                                   const_cast<unsigned int&>(i) += 4;
                                                                },1500);
                                        }
                                    )
                                    (
                                        boost::asynchronous::parallel_for(
                                                                         std::move(this->m_data),
                                                                         [](int const& i)
                                                                         {
                                                                            const_cast<int&>(i) += 2;
                                                                         },1500)
                                    ),
                        [](unsigned int const& i)
                        {
                           const_cast<unsigned int&>(i) += 1;
                        },1500
                    );
                    },// work
           [aPromise,tp,this](boost::asynchronous::expected<std::vector<unsigned int>> res){
                BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                std::vector<boost::thread::id> ids = tp.thread_ids();
                BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                auto modified_vec = res.get();
                auto it = modified_vec.begin();
                BOOST_CHECK_MESSAGE(*it == 12,"data[0] is wrong: "+ std::to_string(*it));
                std::advance(it,100);
                BOOST_CHECK_MESSAGE(*it == 12,"data[100] is wrong: "+ std::to_string(*it));
                std::advance(it,900);
                BOOST_CHECK_MESSAGE(*it == 12,"data[1000] is wrong: "+ std::to_string(*it));
                std::advance(it,8999);
                BOOST_CHECK_MESSAGE(*it == 12,"data[9999] is wrong: "+ std::to_string(*it));
                auto r = std::accumulate(modified_vec.begin(),modified_vec.end(),0,[](int a, int b){return a+b;});
                BOOST_CHECK_MESSAGE((r == 120000),
                                    ("result of parallel_for after if/else was " + std::to_string(r) +
                                     ", should have been 120000"));

                // reset
                m_data = std::vector<int>(10000,1);
                aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }
private:
    std::vector<int> m_data;
};

class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant>
{
public:
    template <class Scheduler>
    ServantProxy(Scheduler s):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s)
    {}
    BOOST_ASYNC_FUTURE_MEMBER(async_if_true)
    BOOST_ASYNC_FUTURE_MEMBER(async_if_false)
    BOOST_ASYNC_FUTURE_MEMBER(async_elseif_true_if_true)
};

}


BOOST_AUTO_TEST_CASE( test_if_then_else_if_true )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.async_if_true();
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

BOOST_AUTO_TEST_CASE( test_if_then_else_if_false )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.async_if_false();
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

BOOST_AUTO_TEST_CASE( test_if_then_esle_elseif_true_if_true )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.async_elseif_true_if_true();
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
