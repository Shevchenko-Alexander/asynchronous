#include <iostream>

#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/scheduler/threadpool_scheduler.hpp>
#include <boost/asynchronous/queue/threadsafe_list.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/servant_proxy.hpp>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/trackable_servant.hpp>

namespace
{
struct Servant
{
    // optional, ctor is simple enough not to be posted
    typedef int simple_ctor;
    Servant(boost::asynchronous::any_weak_scheduler<>,int data): m_data(data){}
    int foo()const
    {
        std::cout << "Servant::foo" << std::endl;
        std::cout << "Servant::foo with m_data:" << m_data << std::endl;
        return 5;
    }
    void foobar(int i, char c)const
    {
        std::cout << "Servant::foobar" << std::endl;
        std::cout << "Servant::foobar with int:" << i << " and char:" << c <<std::endl;
    }
    int m_data;
};

class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant>
{
public:
    template <class Scheduler>
    ServantProxy(Scheduler s, int data):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s, data)
    {}
    BOOST_ASYNC_UNSAFE_MEMBER(foo)
    BOOST_ASYNC_UNSAFE_MEMBER(foobar)
};

struct Servant2 : boost::asynchronous::trackable_servant<>
{
    // optional, ctor is simple enough not to be posted
    typedef int simple_ctor;
    
    Servant2(boost::asynchronous::any_weak_scheduler<> scheduler,ServantProxy worker)
        :boost::asynchronous::trackable_servant<>(scheduler)
        ,m_worker(worker)
    {}
    boost::shared_future<void> doIt()
    {
        // we return a future so that the caller knows when we're done.
        boost::shared_ptr<boost::promise<void> > p (new boost::promise<void>);
        boost::shared_future<void> fu = p->get_future();
        call_callback(m_worker.get_proxy(),
                      m_worker.foo(),
                      // callback functor.
                      [](boost::asynchronous::expected<int> res)
                      {std::cout << "callback of foo in doIt with result: " << res.get() << std::endl;}
        );
        call_callback(m_worker.get_proxy(),
                      m_worker.foobar(1,'a'),
                      // callback functor.
                      [p](boost::asynchronous::expected<void> )
                      {std::cout << "callback of foobar in doIt" << std::endl;p->set_value();}
        );
        return fu;
    }
private:
    ServantProxy m_worker;
};

class ServantProxy2 : public boost::asynchronous::servant_proxy<ServantProxy2,Servant2>
{
public:
    template <class Scheduler>
    ServantProxy2(Scheduler s, ServantProxy worker):
        boost::asynchronous::servant_proxy<ServantProxy2,Servant2>(s, worker)
    {}

    BOOST_ASYNC_FUTURE_MEMBER(doIt)
};

}

void example_two_simple_servants()
{
    {
        std::cout << "start example_two_simple_servants: create schedulers" << std::endl;
        auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(
                    new boost::asynchronous::single_thread_scheduler<
                          boost::asynchronous::threadsafe_list<> >);
        
        auto scheduler2 = boost::asynchronous::create_shared_scheduler_proxy(
                    new boost::asynchronous::single_thread_scheduler<
                          boost::asynchronous::threadsafe_list<> >);

        {
            ServantProxy proxy(scheduler,42);
            ServantProxy2 proxy2(scheduler2,proxy);
            boost::shared_future<boost::shared_future<void> > fu = proxy2.doIt();
            boost::shared_future<void> fu2 = fu.get();
            fu2.get();
        }
    }
}
