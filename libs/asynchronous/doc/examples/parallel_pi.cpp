#include <iostream>
#include <vector>
#include <limits>

#include <boost/asynchronous/callable_any.hpp>

#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/scheduler/multiqueue_threadpool_scheduler.hpp>
#include <boost/asynchronous/servant_proxy.h>
#include <boost/asynchronous/trackable_servant.hpp>
#include <boost/asynchronous/algorithm/parallel_reduce.hpp>
#include <boost/asynchronous/algorithm/parallel_for.hpp>

#define COUNT 100000000
#define THREAD_COUNT 6
#define STEP_SIZE COUNT/THREAD_COUNT/100

namespace {
std::vector<double> datavec;
}

void mkdata()
{
    datavec.reserve(COUNT);
    for (long i = 0; i < COUNT; ++i) {
        datavec.push_back((double) i);
	}
}
double serial_pi()
{
    double res = 0.0;
    for (long i = 0; i < COUNT; ++i) {
        res += ((double) ((((long) i) % 2 == 0) ? 1 : -1)) / ((double) (2 * i + 1));
    }
    return res * 4.0;
}
struct Servant : boost::asynchronous::trackable_servant<>
{
    typedef int simple_ctor;
    Servant(boost::asynchronous::any_weak_scheduler<> scheduler)
        : boost::asynchronous::trackable_servant<>(scheduler,
                                               boost::asynchronous::create_shared_scheduler_proxy(
                                                   new boost::asynchronous::multiqueue_threadpool_scheduler<
                                                           boost::asynchronous::lockfree_queue<> >(THREAD_COUNT)))
        , m_data(std::move(datavec))
    {}
    
    boost::shared_future<double> calc_pi()
    {
        std::cout << "start calculating PI" << std::endl;
        // we need a promise to inform caller when we're done
        boost::shared_ptr<boost::promise<double> > aPromise(new boost::promise<double>);
        boost::shared_future<double> fu = aPromise->get_future();
        boost::asynchronous::any_shared_scheduler_proxy<> tp = get_worker();
        // start long tasks
        post_callback(
           [tp,this](){
                    auto sum = [](double a, double b)
                    {
                        return a + b;
                    };
                    auto step = [](double const& n)
                    {
                        const_cast<double&>(n) = ((double) ((((long) n) % 2 == 0) ? 1 : -1)) / ((double) (2 * n + 1));
                    };
                    auto for_cont = boost::asynchronous::parallel_for(std::move(this->m_data), step, STEP_SIZE);
                    return boost::asynchronous::parallel_reduce(for_cont, sum, STEP_SIZE);
           },// work
           [aPromise,tp,this](boost::future<double> res){
                        double res_intermediate = res.get();
                        std::cout << "res_intermediate = " << res_intermediate << std::endl;
                        double piq = res_intermediate * 4.0;
                        aPromise->set_value(piq);
           }// callback functor.
        );
        return fu;
    }
private:
    std::vector<double> m_data;
};


class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant> {
public:
    template <class Scheduler>
    ServantProxy(Scheduler s):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s)
    {}
    // caller will get a future
    BOOST_ASYNC_FUTURE_MEMBER(calc_pi)
};

void parallel_pi()
{
    mkdata();
    std::cout.precision(std::numeric_limits< double >::digits10 +2);
    std::cout << "calculating pi, serial way" << std::endl;
    typename boost::chrono::high_resolution_clock::time_point start;
    typename boost::chrono::high_resolution_clock::time_point stop;
    start = boost::chrono::high_resolution_clock::now();
    double res = serial_pi();
    stop = boost::chrono::high_resolution_clock::now();
    std::cout << "PI = " << res << std::endl;
    std::cout << "serial_pi took in us:"
              <<  (boost::chrono::nanoseconds(stop - start).count() / 1000) <<"\n" <<std::endl;

    std::cout << "calculating pi, parallel way" << std::endl;
	auto scheduler = boost::asynchronous::create_shared_scheduler_proxy(
							new boost::asynchronous::single_thread_scheduler<
                                 boost::asynchronous::lockfree_queue<> >);
	{
		ServantProxy proxy(scheduler);
        start = boost::chrono::high_resolution_clock::now();
        boost::shared_future<boost::shared_future<double> > fu = proxy.calc_pi();
        boost::shared_future<double> resfu = fu.get();
        res = resfu.get();
        stop = boost::chrono::high_resolution_clock::now();
        std::cout << "PI = " << res << std::endl;
        std::cout << "parallel_pi took in us:"
                  <<  (boost::chrono::nanoseconds(stop - start).count() / 1000) <<"\n" <<std::endl;
	}
}
