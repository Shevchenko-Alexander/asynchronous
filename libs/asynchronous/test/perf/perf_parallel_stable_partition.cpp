#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>
//#include <stdlib.h>

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
#include <boost/asynchronous/algorithm/parallel_stable_partition.hpp>

using namespace std;


#define LOOP 1

#define NELEM 100000000
#define SORTED_TYPE float

//#define NELEM 10000000
//#define SORTED_TYPE std::string

typename boost::chrono::high_resolution_clock::time_point servant_time;
double servant_intern=0.0;
long tpsize = 12;
long tasks = 500;


SORTED_TYPE compare_with = NELEM/2;

void ParallelAsyncPostCb(std::vector<SORTED_TYPE>& a)
{
    auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<
            boost::asynchronous::multiqueue_threadpool_scheduler<
                    boost::asynchronous::lockfree_queue<>,
                    boost::asynchronous::default_find_position< boost::asynchronous::sequential_push_policy>,
                boost::asynchronous::default_save_cpu_load<10,80000,1000>
                //boost::asynchronous::no_cpu_load_saving
                >>(tpsize);
    // set processor affinity to improve cache usage. We start at core 0, until tpsize-1
    scheduler.processor_bind(0);

    boost::shared_ptr<std::vector<SORTED_TYPE>> vec = boost::make_shared<std::vector<SORTED_TYPE>>(a);
    boost::shared_ptr<std::vector<SORTED_TYPE>> vec_res = boost::make_shared<std::vector<SORTED_TYPE>>(vec->size());
    long tasksize = NELEM / tasks;
    servant_time = boost::chrono::high_resolution_clock::now();
    auto fu = boost::asynchronous::post_future(
                scheduler,
                [vec,vec_res,tasksize](){
                         return boost::asynchronous::parallel_stable_partition(
                                                                        (*vec).begin(),(*vec).end(),
                                                                        (*vec_res).begin(),
                                                                        [](SORTED_TYPE const& i)
                                                                        {
                                                                           // cheap version
                                                                           //return i < compare_with;
                                                                           // expensive version
                                                                           return i/(i*i)/i/i < compare_with/(compare_with * compare_with)/compare_with/compare_with;
                                                                        },tasksize,"",0);
                       }
                );
    fu.wait();
    servant_intern += (boost::chrono::nanoseconds(boost::chrono::high_resolution_clock::now() - servant_time).count() / 1000000);

    {
        auto seq_start = boost::chrono::high_resolution_clock::now();
        std::partition(a.begin(),a.end(),[](SORTED_TYPE const& i)
        {
            // cheap version
            //return i < compare_with;
            // expensive version
            return i/(i*i)/i/i < compare_with/(compare_with * compare_with)/compare_with/compare_with;
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
        a[i] = i+NELEM ;
    }
    (*pf)(a);
}
void test_random_elements_many_repeated(void(*pf)(std::vector<SORTED_TYPE>& ))
{
    std::vector<SORTED_TYPE> a(NELEM);
    for ( uint32_t i = 0 ; i < NELEM ; ++i)
    {
        a[i] = rand() % 10000 ;
    }
    (*pf)(a);
}
void test_random_elements_few_repeated(void(*pf)(std::vector<SORTED_TYPE>& ))
{
    std::vector<SORTED_TYPE> a(NELEM);
    for ( uint32_t i = 0 ; i < NELEM ; ++i)
    {
        a[i] = rand();
    }
    (*pf)(a);
}
void test_random_elements_quite_repeated(void(*pf)(std::vector<SORTED_TYPE>& ))
{
    std::vector<SORTED_TYPE> a(NELEM);
    for ( uint32_t i = 0 ; i < NELEM ; ++i)
    {
        a[i] = rand() % (NELEM/2) ;
    }
    (*pf)(a);
}
void test_reversed_sorted_elements(void(*pf)(std::vector<SORTED_TYPE>& ))
{
    std::vector<SORTED_TYPE> a(NELEM);
    for ( uint32_t i = 0 ; i < NELEM ; ++i)
    {
        a[i] = (NELEM<<1) -i ;
    }
    (*pf)(a);
}
void test_equal_elements(void(*pf)(std::vector<SORTED_TYPE>& ))
{
    std::vector<SORTED_TYPE> a(NELEM);
    for ( uint32_t i = 0 ; i < NELEM ; ++i)
    {
        a[i] = NELEM ;
    }
    (*pf)(a);
}
int main( int argc, const char *argv[] )
{
    tpsize = (argc>1) ? strtol(argv[1],0,0) : boost::thread::hardware_concurrency();
    tasks = (argc>2) ? strtol(argv[2],0,0) : 500;
    std::cout << "tasks=" << tasks << std::endl;
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
