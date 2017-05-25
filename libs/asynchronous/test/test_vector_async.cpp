// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2015
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#ifndef BOOST_THREAD_PROVIDES_FUTURE
#define BOOST_THREAD_PROVIDES_FUTURE
#endif
#include <algorithm>
#include <vector>

#include <boost/thread.hpp>

#include <boost/asynchronous/container/vector.hpp>
#include <boost/asynchronous/container/algorithms.hpp>
#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/scheduler/multiqueue_threadpool_scheduler.hpp>
#include <boost/asynchronous/diagnostics/any_loggable.hpp>
#include "test_common.hpp"

#include <boost/test/unit_test.hpp>


using namespace boost::asynchronous::test;

namespace
{
std::atomic<int> ctor_count(0);
std::atomic<int> dtor_count(0);
struct some_type
{
    some_type(int d=0)
        :data(d)
    {
        ++ctor_count;
    }
    some_type(some_type const& rhs)
        :data(rhs.data)
    {
        ++ctor_count;
    }
    some_type(some_type&& rhs)
        :data(rhs.data)
    {
        ++ctor_count;
    }
    some_type& operator=(some_type const&)=default;
    some_type& operator=(some_type&&)=default;

    ~some_type()
    {
        ++dtor_count;
    }

    int data;
};
}

// asynchronous interface tests
BOOST_AUTO_TEST_CASE( test_make_asynchronous_range)
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::multiqueue_threadpool_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>(8);

        std::future<boost::asynchronous::vector<some_type>> fu = boost::asynchronous::post_future(scheduler,
        []()mutable
        {
            return boost::asynchronous::make_asynchronous_range<boost::asynchronous::vector<some_type>>
                            (10000,100);
        },
        "test_vector_async_ctor_push_back",0);
        boost::asynchronous::vector<some_type> v (fu.get());
        BOOST_CHECK_MESSAGE(v.size() == 10000,"vector size should be 10000.");
        BOOST_CHECK_MESSAGE(v[0].data == 0,"vector[0] should have value 0.");
        // vectors created this way have no scheduler as worker (would be unsafe to have a scheduler in its own thread)
        // so we need to add one (note: this vector is then no more allowed to be posted into this scheduler)
        v.set_scheduler(scheduler);
        v.push_back(some_type(42));
        BOOST_CHECK_MESSAGE(v.size() == 10001,"vector size should be 10001.");
        BOOST_CHECK_MESSAGE(v[0].data == 0,"vector[0] should have value 0.");
        BOOST_CHECK_MESSAGE(v[10000].data == 42,"vector[10000] should have value 42.");
    }
    // vector is destroyed, check we got one dtor for each ctor
    BOOST_CHECK_MESSAGE(ctor_count.load()==dtor_count.load(),"wrong number of ctors/dtors called.");
}
// asynchronous interface tests
BOOST_AUTO_TEST_CASE( test_vector_async_ctor_push_back )
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::multiqueue_threadpool_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>(8);

        // make shared_ptr to avoid requiring C++14 move-capture lambda
        std::shared_ptr<boost::asynchronous::vector<some_type>> pv =
                std::make_shared<boost::asynchronous::vector<some_type>>(scheduler, 100 /* cutoff */, 10000 /* number of elements */);
        // we have to release scheduler as a scheduler cannot live into its own thread
        // (inside the pool, it doesn't need any anyway)
        pv->release_scheduler();

        std::future<boost::asynchronous::vector<some_type>> fu = boost::asynchronous::post_future(scheduler,
        [pv]()mutable
        {
            return boost::asynchronous::async_push_back<boost::asynchronous::vector<some_type>,some_type>(std::move(*pv), some_type(42));
        },
        "test_vector_async_ctor_push_back",0);
        boost::asynchronous::vector<some_type> v (fu.get());
        // reset scheduler to avoid leak
        v.set_scheduler(scheduler);
        BOOST_CHECK_MESSAGE(v.size() == 10001,"vector size should be 10001.");
        BOOST_CHECK_MESSAGE(v[0].data == 0,"vector[0] should have value 0.");
        BOOST_CHECK_MESSAGE(v[10000].data == 42,"vector[10000] should have value 42.");
    }
    // vector is destroyed, check we got one dtor for each ctor
    BOOST_CHECK_MESSAGE(ctor_count.load()==dtor_count.load(),"wrong number of ctors/dtors called.");
}

BOOST_AUTO_TEST_CASE( test_vector_async_ctor_push_back_2)
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::multiqueue_threadpool_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>(8);

        std::future<boost::asynchronous::vector<some_type>> fu = boost::asynchronous::post_future(scheduler,
        []()mutable
        {
            return boost::asynchronous::async_push_back(
                        boost::asynchronous::async_push_back(
                            boost::asynchronous::make_asynchronous_range<boost::asynchronous::vector<some_type>>
                                (10000,100),
                            some_type(42)),
                        some_type(41));
        },
        "test_vector_async_ctor_push_back",0);
        boost::asynchronous::vector<some_type> v (fu.get());
        // reset scheduler to avoid leak
        v.set_scheduler(scheduler);
        BOOST_CHECK_MESSAGE(v.size() == 10002,"vector size should be 10001.");
        BOOST_CHECK_MESSAGE(v[0].data == 0,"vector[0] should have value 0.");
        BOOST_CHECK_MESSAGE(v[10000].data == 42,"vector[10000] should have value 42.");
        BOOST_CHECK_MESSAGE(v[10001].data == 41,"vector[10001] should have value 41.");
    }
    // vector is destroyed, check we got one dtor for each ctor
    BOOST_CHECK_MESSAGE(ctor_count.load()==dtor_count.load(),"wrong number of ctors/dtors called.");
}

BOOST_AUTO_TEST_CASE( test_vector_async_resize)
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::multiqueue_threadpool_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>(8);

        // make shared_ptr to avoid requiring C++14 move-capture lambda
        std::shared_ptr<boost::asynchronous::vector<some_type>> pv =
                std::make_shared<boost::asynchronous::vector<some_type>>(scheduler, 100 /* cutoff */, 10000 /* number of elements */);
        // we have to release scheduler as a scheduler cannot live into its own thread
        // (inside the pool, it doesn't need any anyway)
        pv->release_scheduler();

        std::future<boost::asynchronous::vector<some_type>> fu = boost::asynchronous::post_future(scheduler,
        [pv]()mutable
        {
            return boost::asynchronous::async_resize(
                        std::move(*pv),30000);
        },
        "test_vector_async_ctor_push_back",0);
        boost::asynchronous::vector<some_type> v (fu.get());
        // reset scheduler to avoid leak
        v.set_scheduler(scheduler);
        BOOST_CHECK_MESSAGE(v.size() == 30000,"vector size should be 30000.");
        BOOST_CHECK_MESSAGE(v[0].data == 0,"vector[0] should have value 0.");
        BOOST_CHECK_MESSAGE(v[20000].data == 0,"vector[20000] should have value 0.");
    }
    // vector is destroyed, check we got one dtor for each ctor
    BOOST_CHECK_MESSAGE(ctor_count.load()==dtor_count.load(),"wrong number of ctors/dtors called.");
}

BOOST_AUTO_TEST_CASE( test_vector_async_resize_2)
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::multiqueue_threadpool_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>(8);

        std::future<boost::asynchronous::vector<some_type>> fu = boost::asynchronous::post_future(scheduler,
        []()mutable
        {
            return boost::asynchronous::async_resize(
                            boost::asynchronous::make_asynchronous_range<boost::asynchronous::vector<some_type>>
                                (10000,100),
                            30000);
        },
        "test_vector_async_ctor_push_back",0);
        boost::asynchronous::vector<some_type> v (fu.get());
        // reset scheduler to avoid leak
        v.set_scheduler(scheduler);
        BOOST_CHECK_MESSAGE(v.size() == 30000,"vector size should be 30000.");
        BOOST_CHECK_MESSAGE(v[0].data == 0,"vector[0] should have value 0.");
        BOOST_CHECK_MESSAGE(v[10000].data == 0,"vector[10000] should have value 0.");
    }
    // vector is destroyed, check we got one dtor for each ctor
    BOOST_CHECK_MESSAGE(ctor_count.load()==dtor_count.load(),"wrong number of ctors/dtors called.");
}

BOOST_AUTO_TEST_CASE( test_vector_async_resize_to_smaller)
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::multiqueue_threadpool_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>(8);

        // make shared_ptr to avoid requiring C++14 move-capture lambda
        std::shared_ptr<boost::asynchronous::vector<some_type>> pv =
                std::make_shared<boost::asynchronous::vector<some_type>>(scheduler, 100 /* cutoff */, 10000 /* number of elements */);
        // we have to release scheduler as a scheduler cannot live into its own thread
        // (inside the pool, it doesn't need any anyway)
        pv->release_scheduler();

        std::future<boost::asynchronous::vector<some_type>> fu = boost::asynchronous::post_future(scheduler,
        [pv]()mutable
        {
            return boost::asynchronous::async_resize(
                        std::move(*pv),5000);
        },
        "test_vector_async_ctor_push_back",0);
        boost::asynchronous::vector<some_type> v (fu.get());
        // reset scheduler to avoid leak
        v.set_scheduler(scheduler);
        BOOST_CHECK_MESSAGE(v.size() == 5000,"vector size should be 5000.");
        BOOST_CHECK_MESSAGE(v[0].data == 0,"vector[0] should have value 0.");
        BOOST_CHECK_MESSAGE(v[4000].data == 0,"vector[4000] should have value 0.");
    }
    // vector is destroyed, check we got one dtor for each ctor
    BOOST_CHECK_MESSAGE(ctor_count.load()==dtor_count.load(),"wrong number of ctors/dtors called.");
}

BOOST_AUTO_TEST_CASE( test_vector_async_reserve_to_smaller)
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::multiqueue_threadpool_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>(8);

        std::future<boost::asynchronous::vector<some_type>> fu = boost::asynchronous::post_future(scheduler,
        []()mutable
        {
            return boost::asynchronous::async_reserve(
                            boost::asynchronous::make_asynchronous_range<boost::asynchronous::vector<some_type>>
                                (10000,100),
                            5000);
        },
        "test_vector_async_ctor_push_back",0);
        boost::asynchronous::vector<some_type> v (fu.get());
        // reset scheduler to avoid leak
        v.set_scheduler(scheduler);
        BOOST_CHECK_MESSAGE(v.size() == 10000,"vector size should be 10000.");
        BOOST_CHECK_MESSAGE(v.capacity() == 10000,"vector capacity should be 10000.");
        BOOST_CHECK_MESSAGE(v[0].data == 0,"vector[0] should have value 0.");
    }
    // vector is destroyed, check we got one dtor for each ctor
    BOOST_CHECK_MESSAGE(ctor_count.load()==dtor_count.load(),"wrong number of ctors/dtors called.");
}

BOOST_AUTO_TEST_CASE( test_vector_async_reserve_to_bigger)
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::multiqueue_threadpool_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>(8);

        std::future<boost::asynchronous::vector<some_type>> fu = boost::asynchronous::post_future(scheduler,
        []()mutable
        {
            return boost::asynchronous::async_reserve(
                            boost::asynchronous::make_asynchronous_range<boost::asynchronous::vector<some_type>>
                                (10000,100),
                            20000);
        },
        "test_vector_async_ctor_push_back",0);
        boost::asynchronous::vector<some_type> v (fu.get());
        // reset scheduler to avoid leak
        v.set_scheduler(scheduler);
        BOOST_CHECK_MESSAGE(v.size() == 10000,"vector size should be 10000.");
        BOOST_CHECK_MESSAGE(v.capacity() == 20000,"vector capacity should be 20000.");
        BOOST_CHECK_MESSAGE(v[0].data == 0,"vector[0] should have value 0.");
    }
    // vector is destroyed, check we got one dtor for each ctor
    BOOST_CHECK_MESSAGE(ctor_count.load()==dtor_count.load(),"wrong number of ctors/dtors called.");
}

BOOST_AUTO_TEST_CASE( test_vector_async_shrink_to_fit)
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::multiqueue_threadpool_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>(8);

        std::future<boost::asynchronous::vector<some_type>> fu = boost::asynchronous::post_future(scheduler,
        []()mutable
        {
            return boost::asynchronous::async_shrink_to_fit(
                        boost::asynchronous::async_resize(
                                boost::asynchronous::make_asynchronous_range<boost::asynchronous::vector<some_type>>
                                    (10000,100),
                                5000));
        },
        "test_vector_async_shrink_to_fit",0);
        boost::asynchronous::vector<some_type> v (fu.get());
        // reset scheduler to avoid leak
        v.set_scheduler(scheduler);
        BOOST_CHECK_MESSAGE(v.size() == 5000,"vector size should be 5000.");
        BOOST_CHECK_MESSAGE(v.capacity() == 5000,"vector capacity should be 5000.");
    }
    // vector is destroyed, check we got one dtor for each ctor
    BOOST_CHECK_MESSAGE(ctor_count.load()==dtor_count.load(),"wrong number of ctors/dtors called.");
}

BOOST_AUTO_TEST_CASE( test_vector_async_merge)
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::multiqueue_threadpool_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>(8);

        struct merge_task : public boost::asynchronous::continuation_task<boost::asynchronous::vector<some_type>>
        {
            void operator()()
            {
                boost::asynchronous::continuation_result<boost::asynchronous::vector<some_type>> task_res = this_task_result();

                // we are in the threadpool where we cannot block so we need to use the asynchronous version of vector
                auto cont = boost::asynchronous::make_asynchronous_range<boost::asynchronous::vector<some_type>>(30000,100);
                cont.on_done([task_res](std::tuple<boost::asynchronous::expected<boost::asynchronous::vector<some_type>>>&& cont_res) mutable
                {
                    try
                    {
                        // we must forget no move because we want to avoid a copy (the vector has no scheduler yet, so not possible anyway)
                        std::shared_ptr<boost::asynchronous::vector<some_type>> res =
                                std::make_shared<boost::asynchronous::vector<some_type>>(std::move(std::get<0>(cont_res).get()));
                        // we want to merge a few vectors
                        auto v1 = std::make_shared<std::vector<some_type>>(10000, some_type(1));
                        auto v2 = std::make_shared<std::vector<some_type>>(10000, some_type(2));
                        auto v3 = std::make_shared<std::vector<some_type>>(10000, some_type(3));
                        // create a vector of continuations and wait for all of them
                        std::vector<boost::asynchronous::detail::callback_continuation<void>> subs;
                        subs.push_back(boost::asynchronous::parallel_move(v1->begin(),v1->end(),res->begin(),100));
                        subs.push_back(boost::asynchronous::parallel_move(v2->begin(),v2->end(),res->begin()+10000,100));
                        subs.push_back(boost::asynchronous::parallel_move(v3->begin(),v3->end(),res->begin()+20000,100));
                        boost::asynchronous::create_callback_continuation(
                                        // do not forget the mutable or you get a copy
                                        [task_res,res,v1,v2,v3](std::vector<boost::asynchronous::expected<void>>&&)mutable
                                        {
                                            try
                                            {
                                                // we must forget no move because we want to avoid a copy
                                                // (the vector has no scheduler yet, so not possible anyway)
                                                task_res.set_value(std::move(*res));
                                            }
                                            catch(std::exception& e)
                                            {
                                                task_res.set_exception(std::make_exception_ptr(e));
                                            }
                                        },
                                        std::move(subs));
                    }
                    catch(std::exception& e)
                    {
                        task_res.set_exception(std::make_exception_ptr(e));
                    }
                });
            }
        };

        std::future<boost::asynchronous::vector<some_type>> fu = boost::asynchronous::post_future(scheduler,
        []()mutable
        {
            return boost::asynchronous::top_level_callback_continuation<boost::asynchronous::vector<some_type>>(merge_task());
        },
        "test_vector_async_merge",0);
        boost::asynchronous::vector<some_type> v (fu.get());
        // reset scheduler to avoid leak
        v.set_scheduler(scheduler);
        BOOST_CHECK_MESSAGE(v.size() == 30000,"vector size should be 30000.");
        BOOST_CHECK_MESSAGE(v[100].data == 1,"vector[100] should have value 1.");
        BOOST_CHECK_MESSAGE(v[10100].data == 2,"vector[10100] should have value 2.");
        BOOST_CHECK_MESSAGE(v[20100].data == 3,"vector[20100] should have value 3.");
    }
    // vector is destroyed, check we got one dtor for each ctor
    BOOST_CHECK_MESSAGE(ctor_count.load()==dtor_count.load(),"wrong number of ctors/dtors called.");
}

BOOST_AUTO_TEST_CASE( test_vector_async_merge_task )
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::multiqueue_threadpool_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>(8);

        // make shared_ptr to avoid requiring C++14 move-capture lambda
        std::shared_ptr<boost::asynchronous::vector<some_type>> pv1 =
                std::make_shared<boost::asynchronous::vector<some_type>>(scheduler, 100 /* cutoff */, 10000 /* number of elements */);
        // we have to release scheduler as a scheduler cannot live into its own thread
        // (inside the pool, it doesn't need any anyway)
        pv1->release_scheduler();
        std::shared_ptr<boost::asynchronous::vector<some_type>> pv2 =
                std::make_shared<boost::asynchronous::vector<some_type>>(scheduler, 100 /* cutoff */, 20000 /* number of elements */);
        // we have to release scheduler as a scheduler cannot live into its own thread
        // (inside the pool, it doesn't need any anyway)
        pv2->release_scheduler();

        std::future<boost::asynchronous::vector<some_type>> fu = boost::asynchronous::post_future(scheduler,
        [pv1,pv2]()mutable
        {
            return boost::asynchronous::async_merge_containers<boost::asynchronous::vector<some_type>>(std::move(*pv1),std::move(*pv2));
        },
        "test_vector_async_merge",0);
        boost::asynchronous::vector<some_type> v (fu.get());
        // reset scheduler to avoid leak
        v.set_scheduler(scheduler);
        BOOST_CHECK_MESSAGE(v.size() == 30000,"vector size should be 30000.");
    }
    // vector is destroyed, check we got one dtor for each ctor
    BOOST_CHECK_MESSAGE(ctor_count.load()==dtor_count.load(),"wrong number of ctors/dtors called.");
}

BOOST_AUTO_TEST_CASE( test_vector_async_merge_task_vec )
{
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::multiqueue_threadpool_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>(8);

        // make shared_ptr to avoid requiring C++14 move-capture lambda
        boost::asynchronous::vector<some_type> pv1 (scheduler, 100 /* cutoff */, 10000 /* number of elements */);
        // we have to release scheduler as a scheduler cannot live into its own thread
        // (inside the pool, it doesn't need any anyway)
        pv1.release_scheduler();
        boost::asynchronous::vector<some_type> pv2 (scheduler, 100 /* cutoff */, 20000 /* number of elements */);
        // we have to release scheduler as a scheduler cannot live into its own thread
        // (inside the pool, it doesn't need any anyway)
        pv2.release_scheduler();
        std::shared_ptr<std::vector<boost::asynchronous::vector<some_type>>> vecs =
                std::make_shared<std::vector<boost::asynchronous::vector<some_type>>>();
        vecs->push_back(std::move(pv1));
        vecs->push_back(std::move(pv2));


        std::future<boost::asynchronous::vector<some_type>> fu = boost::asynchronous::post_future(scheduler,
        [vecs]()mutable
        {
            return boost::asynchronous::async_merge_containers<std::vector<boost::asynchronous::vector<some_type>>>(std::move(*vecs));
        },
        "test_vector_async_merge",0);
        boost::asynchronous::vector<some_type> v (fu.get());
        // reset scheduler to avoid leak
        v.set_scheduler(scheduler);
        BOOST_CHECK_MESSAGE(v.size() == 30000,"vector size should be 30000.");
    }
    // vector is destroyed, check we got one dtor for each ctor
    BOOST_CHECK_MESSAGE(ctor_count.load()==dtor_count.load(),"wrong number of ctors/dtors called.");
}
