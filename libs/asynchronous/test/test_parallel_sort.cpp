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
#include <functional>
#include <random>
#include <boost/lexical_cast.hpp>

#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/scheduler/threadpool_scheduler.hpp>

#include <boost/asynchronous/servant_proxy.hpp>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/trackable_servant.hpp>
#include <boost/asynchronous/algorithm/parallel_sort.hpp>
#include <boost/asynchronous/algorithm/parallel_for.hpp>

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
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant dtor not posted.");
        servant_dtor = true;
    }

    boost::shared_future<void> start_parallel_sort()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant async work not posted.");
        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp =get_worker();
        auto data_copy = m_data;
        // start long tasks
        post_callback(
           [tp,this](){
                    BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant work not posted.");
                    std::vector<boost::thread::id> ids = tp.thread_ids();
                    BOOST_CHECK_MESSAGE(contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task executed in the wrong thread");
                    return boost::asynchronous::parallel_sort(this->m_data.begin(),this->m_data.end(),std::less<int>(),1500);
                    },// work
           [aPromise,tp,data_copy,this](boost::asynchronous::expected<void> res) mutable{
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                        std::vector<boost::thread::id> ids = tp.thread_ids();
                        BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        std::sort(data_copy.begin(),data_copy.end(),std::less<int>());
                        BOOST_CHECK_MESSAGE(data_copy == this->m_data,"parallel_sort gave a wrong value.");
                        // reset
                        generate();
                        aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }
    boost::shared_future<void> start_parallel_stable_sort()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant async work not posted.");
        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp =get_worker();
        auto data_copy = m_data;
        // start long tasks
        post_callback(
           [tp,this](){
                    BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant work not posted.");
                    std::vector<boost::thread::id> ids = tp.thread_ids();
                    BOOST_CHECK_MESSAGE(contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task executed in the wrong thread");
                    return boost::asynchronous::parallel_stable_sort(this->m_data.begin(),this->m_data.end(),std::less<int>(),1500);
                    },// work
           [aPromise,tp,data_copy,this](boost::asynchronous::expected<void> res) mutable{
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                        std::vector<boost::thread::id> ids = tp.thread_ids();
                        BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        std::stable_sort(data_copy.begin(),data_copy.end(),std::less<int>());
                        BOOST_CHECK_MESSAGE(data_copy == this->m_data,"parallel_stable_sort gave a wrong value.");
                        // reset
                        generate();
                        aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }
    boost::shared_future<void> start_parallel_sort_range()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant async work not posted.");
        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp =get_worker();
        auto data_copy = m_data;
        // start long tasks
        post_callback(
           [tp,this](){
                    BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant work not posted.");
                    std::vector<boost::thread::id> ids = tp.thread_ids();
                    BOOST_CHECK_MESSAGE(contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task executed in the wrong thread");
                    return boost::asynchronous::parallel_sort(this->m_data,std::less<int>(),1500);
                    },// work
           [aPromise,tp,data_copy,this](boost::asynchronous::expected<void> res) mutable{
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                        std::vector<boost::thread::id> ids = tp.thread_ids();
                        BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        std::sort(data_copy.begin(),data_copy.end(),std::less<int>());
                        BOOST_CHECK_MESSAGE(data_copy == this->m_data,"parallel_sort gave a wrong value.");
                        // reset
                        generate();
                        aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }
    boost::shared_future<void> start_parallel_stable_sort_range()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant async work not posted.");
        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp =get_worker();
        auto data_copy = m_data;
        // start long tasks
        post_callback(
           [tp,this](){
                    BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant work not posted.");
                    std::vector<boost::thread::id> ids = tp.thread_ids();
                    BOOST_CHECK_MESSAGE(contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task executed in the wrong thread");
                    return boost::asynchronous::parallel_stable_sort(this->m_data,std::less<int>(),1500);
                    },// work
           [aPromise,tp,data_copy,this](boost::asynchronous::expected<void> res) mutable{
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                        std::vector<boost::thread::id> ids = tp.thread_ids();
                        BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        std::stable_sort(data_copy.begin(),data_copy.end(),std::less<int>());
                        BOOST_CHECK_MESSAGE(data_copy == this->m_data,"parallel_stable_sort gave a wrong value.");
                        // reset
                        generate();
                        aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }
    boost::shared_future<void> start_parallel_sort_moved_range()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant async work not posted.");
        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp =get_worker();
        auto data_copy = m_data;
        // start long tasks
        post_callback(
           [tp,this](){
                    BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant work not posted.");
                    std::vector<boost::thread::id> ids = tp.thread_ids();
                    BOOST_CHECK_MESSAGE(contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task executed in the wrong thread");
                    return boost::asynchronous::parallel_sort_move(std::move(this->m_data),std::less<int>(),1500);
                    },// work
           [aPromise,tp,data_copy,this](boost::asynchronous::expected<std::vector<int>> res) mutable{
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                        std::vector<boost::thread::id> ids = tp.thread_ids();
                        BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        std::sort(data_copy.begin(),data_copy.end(),std::less<int>());
                        auto modified_vec = res.get();
                        BOOST_CHECK_MESSAGE(data_copy == modified_vec,"parallel_sort gave a wrong value.");
                        // reset
                        generate();
                        aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }
    boost::shared_future<void> start_parallel_sort_continuation()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant async work not posted.");
        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<void> > aPromise(new boost::promise<void>);
        boost::shared_future<void> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp =get_worker();
        auto data_copy = m_data;
        // start long tasks
        post_callback(
           [tp,this](){
                    BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant work not posted.");
                    std::vector<boost::thread::id> ids = tp.thread_ids();
                    BOOST_CHECK_MESSAGE(contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task executed in the wrong thread");
                    return boost::asynchronous::parallel_sort(
                                boost::asynchronous::parallel_for(std::move(this->m_data),
                                                                  [](int const& i)
                                                                  {
                                                                    const_cast<int&>(i) += 2;
                                                                  },1500),
                                std::less<int>(),1500);
                    },// work
           [aPromise,tp,data_copy,this](boost::asynchronous::expected<std::vector<int>> res) mutable{
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                        std::vector<boost::thread::id> ids = tp.thread_ids();
                        BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        std::for_each(data_copy.begin(),data_copy.end(),[](int const& i){const_cast<int&>(i) += 2;});
                        std::sort(data_copy.begin(),data_copy.end(),std::less<int>());
                        auto modified_vec = res.get();
                        BOOST_CHECK_MESSAGE(data_copy == modified_vec,"parallel_sort gave a wrong value.");
                        // reset
                        generate();
                        aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }
private:
    // helper, generate vector
    void generate()
    {
        m_data = std::vector<int>(10000,1);
        std::random_device rd;
        std::mt19937 mt(rd());
        std::uniform_int_distribution<> dis(0, 1000);
        std::generate(m_data.begin(), m_data.end(), std::bind(dis, std::ref(mt)));
    }

    std::vector<int> m_data;
};

class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant>
{
public:
    template <class Scheduler>
    ServantProxy(Scheduler s):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s)
    {}
    BOOST_ASYNC_FUTURE_MEMBER(start_parallel_sort)
    BOOST_ASYNC_FUTURE_MEMBER(start_parallel_stable_sort)
    BOOST_ASYNC_FUTURE_MEMBER(start_parallel_sort_range)
    BOOST_ASYNC_FUTURE_MEMBER(start_parallel_stable_sort_range)
    BOOST_ASYNC_FUTURE_MEMBER(start_parallel_sort_moved_range)
    BOOST_ASYNC_FUTURE_MEMBER(start_parallel_sort_continuation)
};

}

BOOST_AUTO_TEST_CASE( test_parallel_sort )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<> >);

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_parallel_sort();
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
BOOST_AUTO_TEST_CASE( test_parallel_stable_sort )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<> >);

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_parallel_stable_sort();
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
BOOST_AUTO_TEST_CASE( test_parallel_sort_range )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<> >);

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_parallel_sort_range();
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
BOOST_AUTO_TEST_CASE( test_parallel_stable_sort_range )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<> >);

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_parallel_stable_sort_range();
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
BOOST_AUTO_TEST_CASE( test_parallel_stable_sort_moved_range )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<> >);

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_parallel_sort_moved_range();
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
BOOST_AUTO_TEST_CASE( test_parallel_stable_sort_continuation )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<> >);

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_parallel_sort_continuation();
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
