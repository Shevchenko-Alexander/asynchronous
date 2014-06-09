

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
#include <boost/lexical_cast.hpp>

#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/scheduler/threadpool_scheduler.hpp>

#include <boost/asynchronous/servant_proxy.hpp>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/trackable_servant.hpp>
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
        , m_data(10000,1)
    {
    }
    ~Servant()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant dtor not posted.");
        servant_dtor = true;
    }

    boost::shared_future<void> start_async_work()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant start_async_work not posted.");
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
                    return boost::asynchronous::parallel_for(this->m_data.begin(),this->m_data.end(),
                                                      [](int const& i)
                                                      {
                                                        const_cast<int&>(i) += 2;
                                                      },1500);
                    },// work
           [aPromise,tp,this](boost::future<void> res){
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                        std::vector<boost::thread::id> ids = tp.thread_ids();
                        BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        auto it = this->m_data.begin();
                        BOOST_CHECK_MESSAGE(*it == 3,"m_data[0] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        std::advance(it,100);
                        BOOST_CHECK_MESSAGE(*it == 3,"m_data[100] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        std::advance(it,900);
                        BOOST_CHECK_MESSAGE(*it == 3,"m_data[1000] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        std::advance(it,8999);
                        BOOST_CHECK_MESSAGE(*it == 3,"m_data[9999] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        auto r = std::accumulate(this->m_data.begin(),this->m_data.end(),0,[](int a, int b){return a+b;});
                        BOOST_CHECK_MESSAGE((r == 30000),
                                            ("result of parallel_for was " + boost::lexical_cast<std::string, int>(r) + ", should have been 30000"));
                        r = std::accumulate(this->m_data.begin(),this->m_data.end(),0,[](int a, int b){return a+b;});
                        BOOST_CHECK_MESSAGE((r == 30000),
                                            ("result of parallel_for was " + boost::lexical_cast<std::string, int>(r) + ", should have been 30000"));

                        // reset
                        m_data = std::vector<int>(10000,1);
                        aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }
    boost::shared_future<void> start_async_work_range()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant start_async_work_range not posted.");
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
                    return boost::asynchronous::parallel_for(const_cast<std::vector<int>const&>(this->m_data),
                                                             [](int const& i)
                                                             {
                                                               const_cast<int&>(i) += 2;
                                                             },1500);
                    },// work
           [aPromise,tp,this](boost::future<void> res){
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                        std::vector<boost::thread::id> ids = tp.thread_ids();
                        BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        auto it = this->m_data.begin();
                        BOOST_CHECK_MESSAGE(*it == 3,"m_data[0] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        std::advance(it,100);
                        BOOST_CHECK_MESSAGE(*it == 3,"m_data[100] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        std::advance(it,900);
                        BOOST_CHECK_MESSAGE(*it == 3,"m_data[1000] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        std::advance(it,8999);
                        BOOST_CHECK_MESSAGE(*it == 3,"m_data[9999] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        auto r = std::accumulate(this->m_data.begin(),this->m_data.end(),0,[](int a, int b){return a+b;});
                        BOOST_CHECK_MESSAGE((r == 30000),
                                            ("result of parallel_for was " + boost::lexical_cast<std::string>(r) + ", should have been 30000"));
                        r = std::accumulate(this->m_data.begin(),this->m_data.end(),0,[](int a, int b){return a+b;});

                        BOOST_CHECK_MESSAGE((r == 30000),
                                            ("result of parallel_for was " + boost::lexical_cast<std::string>(r) + ", should have been 30000"));

                        // reset
                        m_data = std::vector<int>(10000,1);
                        aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }
    boost::shared_future<void> start_async_work_range2()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant start_async_work_range2 not posted.");
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
                    return boost::asynchronous::parallel_for(std::move(this->m_data),
                                                      [](int const& i)
                                                      {
                                                        const_cast<int&>(i) += 2;
                                                      },1500);
                    },// work
           [aPromise,tp,this](boost::future<std::vector<int>> res){
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                        std::vector<boost::thread::id> ids = tp.thread_ids();
                        BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        auto modified_vec = res.get();
                        auto it = modified_vec.begin();
                        BOOST_CHECK_MESSAGE(*it == 3,"data[0] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        std::advance(it,100);
                        BOOST_CHECK_MESSAGE(*it == 3,"data[100] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        std::advance(it,900);
                        BOOST_CHECK_MESSAGE(*it == 3,"data[1000] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        std::advance(it,8999);
                        BOOST_CHECK_MESSAGE(*it == 3,"data[9999] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        auto r = std::accumulate(modified_vec.begin(),modified_vec.end(),0,[](int a, int b){return a+b;});
                        BOOST_CHECK_MESSAGE((r == 30000),
                                            ("result of parallel_for was " + boost::lexical_cast<std::string, int>(r) + ", should have been 30000"));
                        // reset
                        m_data = std::vector<int>(10000,1);
                        aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }
    boost::shared_future<void> start_async_work_range3()
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
                    return boost::asynchronous::parallel_for(boost::asynchronous::parallel_for(boost::asynchronous::parallel_for(
                                                                 std::move(this->m_data),
                                                                 [](int const& i)
                                                                 {
                                                                    const_cast<int&>(i) += 2;
                                                                 },1500),
                                                              [](int const& i)
                                                              {
                                                                const_cast<int&>(i) += 2;
                                                              },1500),
                                                             [](int const& i)
                                                             {
                                                               const_cast<int&>(i) += 2;
                                                             },1500);
                    },// work
           [aPromise,tp,this](boost::future<std::vector<int>> res){
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                        std::vector<boost::thread::id> ids = tp.thread_ids();
                        BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        auto modified_vec = res.get();
                        auto it = modified_vec.begin();
                        BOOST_CHECK_MESSAGE(*it == 7,"data[0] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        std::advance(it,100);
                        BOOST_CHECK_MESSAGE(*it == 7,"data[100] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        std::advance(it,900);
                        BOOST_CHECK_MESSAGE(*it == 7,"data[1000] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        std::advance(it,8999);
                        BOOST_CHECK_MESSAGE(*it == 7,"data[9999] is wrong: "+ boost::lexical_cast<std::string>(*it));
                        auto r = std::accumulate(modified_vec.begin(),modified_vec.end(),0,[](int a, int b){return a+b;});
                        BOOST_CHECK_MESSAGE((r == 70000),
                                            ("result of parallel_for was " + boost::lexical_cast<std::string, int>(r) + ", should have been 70000"));

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
    BOOST_ASYNC_FUTURE_MEMBER(start_async_work)
    BOOST_ASYNC_FUTURE_MEMBER(start_async_work_range)
    BOOST_ASYNC_FUTURE_MEMBER(start_async_work_range2)
    BOOST_ASYNC_FUTURE_MEMBER(start_async_work_range3)
};

}

BOOST_AUTO_TEST_CASE( test_parallel_for )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<> >);

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_async_work();
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

//BOOST_AUTO_TEST_CASE( test_parallel_for_range )
//{
//    servant_dtor=false;
//    {
//        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
//                                                                            boost::asynchronous::lockfree_queue<> >);

//        main_thread_id = boost::this_thread::get_id();
//        ServantProxy proxy(scheduler);
//        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_async_work_range();
//        try
//        {
//            boost::shared_future<void> resfuv = fuv.get();
//            resfuv.get();
//        }
//        catch(...)
//        {
//            BOOST_FAIL( "unexpected exception" );
//        }
//    }
//    BOOST_CHECK_MESSAGE(servant_dtor,"servant dtor not called.");
//}

//BOOST_AUTO_TEST_CASE( test_parallel_for_range2 )
//{
//    servant_dtor=false;
//    {
//        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
//                                                                            boost::asynchronous::lockfree_queue<> >);

//        main_thread_id = boost::this_thread::get_id();
//        ServantProxy proxy(scheduler);
//        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_async_work_range2();
//        try
//        {
//            boost::shared_future<void> resfuv = fuv.get();
//            resfuv.get();
//        }
//        catch(...)
//        {
//            BOOST_FAIL( "unexpected exception" );
//        }
//    }
//    BOOST_CHECK_MESSAGE(servant_dtor,"servant dtor not called.");
//}

//BOOST_AUTO_TEST_CASE( test_parallel_for_range3 )
//{
//    servant_dtor=false;
//    {
//        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
//                                                                            boost::asynchronous::lockfree_queue<> >);

//        main_thread_id = boost::this_thread::get_id();
//        ServantProxy proxy(scheduler);
//        boost::shared_future<boost::shared_future<void> > fuv = proxy.start_async_work_range3();
//        try
//        {
//            boost::shared_future<void> resfuv = fuv.get();
//            resfuv.get();
//        }
//        catch(...)
//        {
//            BOOST_FAIL( "unexpected exception" );
//        }
//    }
//    BOOST_CHECK_MESSAGE(servant_dtor,"servant dtor not called.");
//}

