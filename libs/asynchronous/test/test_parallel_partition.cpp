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
#include <boost/asynchronous/algorithm/parallel_partition.hpp>

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
void generate(std::vector<int>& data, unsigned elements, unsigned dist)
{
    data = std::vector<int>(elements,1);
    std::random_device rd;
    std::mt19937 mt(rd());
    //std::mt19937 mt(static_cast<unsigned int>(std::time(nullptr)));
    std::uniform_int_distribution<> dis(0, dist);
    std::generate(data.begin(), data.end(), std::bind(dis, std::ref(mt)));
}

struct Servant : boost::asynchronous::trackable_servant<>
{
    typedef int simple_ctor;
    Servant(boost::asynchronous::any_weak_scheduler<> scheduler)
        : boost::asynchronous::trackable_servant<>(scheduler,
                                               boost::asynchronous::create_shared_scheduler_proxy(
                                                   new boost::asynchronous::threadpool_scheduler<
                                                           boost::asynchronous::lockfree_queue<> >(boost::thread::hardware_concurrency())))
    {
    }
    ~Servant()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant dtor not posted.");
        servant_dtor = true;
    }

    boost::shared_future<void> test_parallel_partition()
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant async work not posted.");
        generate(m_data1,100,70);
        m_data2 = std::vector<int>(100,-1);
        auto data_copy = m_data1;
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
                    return boost::asynchronous::parallel_partition(m_data1.begin(),m_data1.end(),m_data2.begin(),
                                                                   [](int i){return i < 30;},10);
                    },// work
           [aPromise,tp,data_copy,this](boost::asynchronous::expected<std::vector<int>::iterator> res) mutable{
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant callback in main thread.");
                        std::vector<boost::thread::id> ids = tp.thread_ids();
                        BOOST_CHECK_MESSAGE(!contains_id(ids.begin(),ids.end(),boost::this_thread::get_id()),"task callback executed in the wrong thread(pool)");
                        BOOST_CHECK_MESSAGE(!res.has_exception(),"servant work threw an exception.");
                        auto it = std::partition(data_copy.begin(),data_copy.end(),[](int i){return i < 30;});
                        std::size_t dist2 = it - data_copy.begin();                                                                       
                        auto it2 = res.get();

                        //try comparison
                        std::sort(data_copy.begin(), it, std::less<int>());
                        std::sort(m_data2.begin(), it2, std::less<int>());
                        std::sort(it, data_copy.end(), std::less<int>());
                        std::sort(it2, m_data2.end(), std::less<int>());

                        BOOST_CHECK_MESSAGE(m_data2 == data_copy,"parallel_partition did not partition correctly.");

                        std::size_t dist1 = it2 - m_data2.begin();
                        std::cout << "found: " << dist1 << " true elements" << std::endl;
                        BOOST_CHECK_MESSAGE(dist1 == dist2,"parallel_partition gave the wrong iterator.");
                        std::vector<int> true_part_1(m_data2.begin(),it2);
                        std::vector<int> true_part_2(data_copy.begin(),it);
                        BOOST_CHECK_MESSAGE(true_part_1 == true_part_2,"parallel_partition partitioned first part wrong.");
                        std::cout << "seq true" << std::endl;
                        for (auto it3 = data_copy.begin() ; it3 != it ; ++it3)
                        {
                            std::cout << *it3 <<" ";
                        }
                        std::cout << std::endl;std::cout << std::endl;
                        bool ok = true;
                        std::cout << "parallel true" << std::endl;
                        for (auto it3 = m_data2.begin() ; it3 != it2 ; ++it3)
                        {
                            std::cout << *it3 <<" ";
                            if (*it3 >= 30 || *it3 == -1)
                                ok = false;
                        }
                        std::cout << std::endl;std::cout << std::endl;
                        BOOST_CHECK_MESSAGE(ok,"parallel_partition has false elements at wrong place.");
                        std::cout << "seq false" << std::endl;
                        for (auto it3 = it ; it3 != data_copy.end() ; ++it3)
                        {
                            std::cout << *it3 <<" ";
                        }
                        std::cout << std::endl;std::cout << std::endl;
                        ok = true;
                        std::cout << "parallel false" << std::endl;
                        for (auto it3 = it2 ; it3 != m_data2.end() ; ++it3)
                        {
                          std::cout << *it3 <<" " ;
                           if (*it3 < 30)
                               ok = false;
                        }
                        std::cout << std::endl;
                        BOOST_CHECK_MESSAGE(ok,"parallel_partition has true elements at wrong place.");
                        aPromise->set_value();
           }// callback functor.
        );
        return fu;
    }

private:
    std::vector<int> m_data1;
    std::vector<int> m_data2;
};
class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant>
{
public:
    template <class Scheduler>
    ServantProxy(Scheduler s):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s)
    {}
    BOOST_ASYNC_FUTURE_MEMBER(test_parallel_partition)
};
}

BOOST_AUTO_TEST_CASE( test_parallel_partition )
{
    servant_dtor=false;
    {
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(new boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<> >);

        main_thread_id = boost::this_thread::get_id();
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fuv = proxy.test_parallel_partition();
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



