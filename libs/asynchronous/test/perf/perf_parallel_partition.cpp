#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>
//#include <stdlib.h>

#include <boost/lexical_cast.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/smart_ptr/shared_array.hpp>

#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/servant_proxy.hpp>
#include <boost/asynchronous/scheduler/multiqueue_threadpool_scheduler.hpp>
#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/queue/any_queue_container.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/trackable_servant.hpp>
#include <boost/asynchronous/algorithm/parallel_partition.hpp>

using namespace std;


#define LOOP 1

//#define NELEM 80000000
//#define SORTED_TYPE uint32_t

#define NELEM 10000000
#define SORTED_TYPE std::string

typename boost::chrono::high_resolution_clock::time_point servant_time;
double servant_intern=0.0;
long tpsize = 12;

namespace boost {
template<>
inline std::string lexical_cast(const uint32_t& arg)
{
    return std::to_string(arg);
}
}

template <class T, class U>
typename boost::disable_if<boost::is_same<T,U>,U >::type
test_cast(T const& t)
{
    return boost::lexical_cast<U>(t);
}
template <class T, class U>
typename boost::enable_if<boost::is_same<T,U>,U >::type
test_cast(T const& t)
{
    return t;
}

SORTED_TYPE compare_with = test_cast<uint32_t,SORTED_TYPE>(NELEM/2);

struct Servant : boost::asynchronous::trackable_servant<>
{
    //typedef int simple_ctor;
    //typedef int requires_weak_scheduler;
    typedef std::vector<SORTED_TYPE>::iterator Iterator;
    Servant(boost::asynchronous::any_weak_scheduler<> scheduler)
        : boost::asynchronous::trackable_servant<>(scheduler,
                                               boost::asynchronous::make_shared_scheduler_proxy<
                                                   boost::asynchronous::multiqueue_threadpool_scheduler<
                                                           boost::asynchronous::lockfree_queue<>,
                                                           boost::asynchronous::default_find_position< boost::asynchronous::sequential_push_policy>,
                                                       //boost::asynchronous::default_save_cpu_load<10,80000,1000>
                                                       boost::asynchronous::no_cpu_load_saving
                                                       >>(tpsize))
        , m_promise(new boost::promise<void>)
        , m_tpsize(tpsize)
    {
    }
    ~Servant(){}

    void on_callback_vec(std::vector<SORTED_TYPE>&& /*vec*/)
    {
        servant_intern += (boost::chrono::nanoseconds(boost::chrono::high_resolution_clock::now() - servant_time).count() / 1000000);
        /*std::cout << "partitioned? " << std::is_partitioned(vec.begin(),vec.end(),
                                                            [](SORTED_TYPE i)
                                                            {
                                                                // cheap version
                                                                return i < compare_with;
                                                                // expensive version
                                                                //return i < test_cast<uint32_t,SORTED_TYPE>(NELEM/2);
                                                            })
                                     << std::endl;*/
        m_promise->set_value();
    }
    boost::shared_future<void> do_partition_vec(std::vector<SORTED_TYPE> a)
    {
        boost::shared_future<void> fu = m_promise->get_future();
        size_t tpsize = m_tpsize;
        boost::shared_ptr<std::vector<SORTED_TYPE>> vec = boost::make_shared<std::vector<SORTED_TYPE>>(std::move(a));
        servant_time = boost::chrono::high_resolution_clock::now();
        post_callback(
               [vec,tpsize](){
                        return boost::asynchronous::parallel_partition(std::move(*vec),
                                                                       [](SORTED_TYPE i)
                                                                       {
                                                                          // cheap version
                                                                          return i < compare_with;
                                                                          // expensive version to show parallelism
                                                                          //return i < test_cast<uint32_t,SORTED_TYPE>(NELEM/2);
                                                                       },tpsize,"",0);
                      },// work
               // the lambda calls Servant, just to show that all is safe, Servant is alive if this is called
               [this](boost::asynchronous::expected<std::pair<std::vector<SORTED_TYPE>,Iterator>> res)
               {
                    auto res_pair = std::move(res.get());
                    std::vector<SORTED_TYPE> res_vec = std::move(res_pair.first);
                    this->on_callback_vec(std::move(res_vec));
               }// callback functor.
               ,"",0,0
        );
        return fu;
    }
private:
// for testing
boost::shared_ptr<boost::promise<void> > m_promise;
size_t m_tpsize;
};
class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant>
{
public:
    template <class Scheduler>
    ServantProxy(Scheduler s):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s)
    {}
    BOOST_ASYNC_FUTURE_MEMBER(do_partition_vec,0)
};

void ParallelAsyncPostCb(std::vector<SORTED_TYPE>& vec)
{
    auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<
                            boost::asynchronous::single_thread_scheduler<boost::asynchronous::lockfree_queue<>,
                                                                         boost::asynchronous::default_save_cpu_load<10,80000,1000>>>(tpsize);
    {
        ServantProxy proxy(scheduler);
        boost::shared_future<boost::shared_future<void> > fu = proxy.do_partition_vec(vec);
        boost::shared_future<void> resfu = fu.get();
        resfu.get();
    }
    {
        auto seq_start = boost::chrono::high_resolution_clock::now();
        std::partition(vec.begin(),vec.end(),[](SORTED_TYPE i)
        {
            // cheap version
            return i < compare_with;
            // expensive version
            //return i < test_cast<uint32_t,SORTED_TYPE>(NELEM/2);
        });
        auto seq_stop = boost::chrono::high_resolution_clock::now();
        double seq_time = (boost::chrono::nanoseconds(seq_stop - seq_start).count() / 1000000);
        printf ("\n%50s: time = %.1f msec\n","sequential", seq_time);
    }
}
void test_sorted_elements(void(*pf)(std::vector<SORTED_TYPE>& ))
{
    std::vector<SORTED_TYPE> a(NELEM);
    for ( uint32_t i = 0 ; i < NELEM ; ++i)
    {
        a[i] = test_cast<uint32_t,SORTED_TYPE>( i+NELEM) ;
    }
    (*pf)(a);
}
void test_random_elements_many_repeated(void(*pf)(std::vector<SORTED_TYPE>& ))
{
    std::vector<SORTED_TYPE> a(NELEM);
    for ( uint32_t i = 0 ; i < NELEM ; ++i)
    {
        a[i] = test_cast<uint32_t,SORTED_TYPE>(rand() % 10000) ;
    }
    (*pf)(a);
}
void test_random_elements_few_repeated(void(*pf)(std::vector<SORTED_TYPE>& ))
{
    std::vector<SORTED_TYPE> a(NELEM);
    for ( uint32_t i = 0 ; i < NELEM ; ++i)
    {
        a[i] = test_cast<uint32_t,SORTED_TYPE>(rand());
    }
    (*pf)(a);
}
void test_random_elements_quite_repeated(void(*pf)(std::vector<SORTED_TYPE>& ))
{
    std::vector<SORTED_TYPE> a(NELEM);
    for ( uint32_t i = 0 ; i < NELEM ; ++i)
    {
        a[i] = test_cast<uint32_t,SORTED_TYPE>(rand() % (NELEM/2)) ;
    }
    (*pf)(a);
}
void test_reversed_sorted_elements(void(*pf)(std::vector<SORTED_TYPE>& ))
{
    std::vector<SORTED_TYPE> a(NELEM);
    for ( uint32_t i = 0 ; i < NELEM ; ++i)
    {
        a[i] = test_cast<uint32_t,SORTED_TYPE>((NELEM<<1) -i) ;
    }
    (*pf)(a);
}
void test_equal_elements(void(*pf)(std::vector<SORTED_TYPE>& ))
{
    std::vector<SORTED_TYPE> a(NELEM);
    for ( uint32_t i = 0 ; i < NELEM ; ++i)
    {
        a[i] = test_cast<uint32_t,SORTED_TYPE>(NELEM) ;
    }
    (*pf)(a);
}
int main( int argc, const char *argv[] ) 
{           
    tpsize = (argc>1) ? strtol(argv[1],0,0) : boost::thread::hardware_concurrency();
    std::cout << "tpsize=" << tpsize << std::endl;
    
    servant_intern=0.0;
    for (int i=0;i<LOOP;++i)
    {
        test_random_elements_many_repeated(ParallelAsyncPostCb);
    }
    printf ("%50s: time = %.1f msec\n","test_random_elements_many_repeated", servant_intern);
    
    servant_intern=0.0;
    for (int i=0;i<LOOP;++i)
    {
        test_random_elements_few_repeated(ParallelAsyncPostCb);
    }
    printf ("%50s: time = %.1f msec\n","test_random_elements_few_repeated", servant_intern);
    
    servant_intern=0.0;
    for (int i=0;i<LOOP;++i)
    {     
        test_random_elements_quite_repeated(ParallelAsyncPostCb);
    }
    printf ("%50s: time = %.1f msec\n","test_random_elements_quite_repeated", servant_intern);
    
    servant_intern=0.0;
    for (int i=0;i<LOOP;++i)
    {     
        test_sorted_elements(ParallelAsyncPostCb);
    }
    printf ("%50s: time = %.1f msec\n","test_sorted_elements", servant_intern);
    
    servant_intern=0.0;
    for (int i=0;i<LOOP;++i)
    {     
        test_reversed_sorted_elements(ParallelAsyncPostCb);
    }
    printf ("%50s: time = %.1f msec\n","test_reversed_sorted_elements", servant_intern);
    
    servant_intern=0.0;
    for (int i=0;i<LOOP;++i)
    {     
        test_equal_elements(ParallelAsyncPostCb);
    }
    printf ("%50s: time = %.1f msec\n","test_equal_elements", servant_intern);
    
    std::cout << std::endl;
    
    return 0;
}
