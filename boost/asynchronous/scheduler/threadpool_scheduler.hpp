// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2013
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#ifndef BOOST_ASYNC_SCHEDULER_THREADPOOL_SCHEDULER_HPP
#define BOOST_ASYNC_SCHEDULER_THREADPOOL_SCHEDULER_HPP

#include <utility>
#include <vector>
#include <cstddef>

#ifndef BOOST_THREAD_PROVIDES_FUTURE
#define BOOST_THREAD_PROVIDES_FUTURE
#endif

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <boost/bind.hpp>
#include <boost/thread/future.hpp>
#include <boost/thread/mutex.hpp>

#include <boost/asynchronous/scheduler/detail/scheduler_helpers.hpp>
#include <boost/asynchronous/scheduler/detail/exceptions.hpp>
#include <boost/asynchronous/detail/any_interruptible.hpp>
#include <boost/asynchronous/diagnostics/default_loggable_job.hpp>
#include <boost/asynchronous/job_traits.hpp>
#include <boost/asynchronous/scheduler/detail/job_diagnostic_closer.hpp>
#include <boost/asynchronous/scheduler/detail/single_queue_scheduler_policy.hpp>
#include <boost/asynchronous/detail/any_joinable.hpp>
#include <boost/asynchronous/queue/threadsafe_list.hpp>
#include <boost/asynchronous/scheduler/tss_scheduler.hpp>
#include <boost/asynchronous/scheduler/detail/lockable_weak_scheduler.hpp>
#include <boost/asynchronous/scheduler/detail/any_continuation.hpp>

namespace boost { namespace asynchronous
{

template<class Q>
class threadpool_scheduler: public boost::asynchronous::detail::single_queue_scheduler_policy<Q>
{
public:
    typedef boost::asynchronous::threadpool_scheduler<Q> this_type;
    typedef Q queue_type;
    typedef typename Q::job_type job_type;
    typedef typename boost::asynchronous::job_traits<typename Q::job_type>::diagnostic_table_type diag_type;
    
#ifndef BOOST_NO_CXX11_VARIADIC_TEMPLATES
    template<typename... Args>
    threadpool_scheduler(size_t number_of_workers, Args... args)
        : boost::asynchronous::detail::single_queue_scheduler_policy<Q>(boost::make_shared<queue_type>(args...))
        , m_private_queue (boost::make_shared<boost::asynchronous::threadsafe_list<job_type> >())
        , m_number_of_workers(number_of_workers)
    {
    }
#endif
    threadpool_scheduler(size_t number_of_workers)
        : boost::asynchronous::detail::single_queue_scheduler_policy<Q>()
        , m_private_queue (boost::make_shared<boost::asynchronous::threadsafe_list<job_type> >())
        , m_number_of_workers(number_of_workers)
    {
    }
    void constructor_done(boost::weak_ptr<this_type> weak_self)
    {
        m_diagnostics = boost::make_shared<diag_type>();
        m_thread_ids.reserve(m_number_of_workers);
        m_group.reset(new boost::thread_group);
        for (size_t i = 0; i< m_number_of_workers;++i)
        {
            boost::promise<boost::thread*> new_thread_promise;
            boost::shared_future<boost::thread*> fu = new_thread_promise.get_future();
            boost::thread* new_thread =
                    m_group->create_thread(boost::bind(&threadpool_scheduler::run,this->m_queue,m_private_queue,m_diagnostics,fu,weak_self));
            new_thread_promise.set_value(new_thread);
            m_thread_ids.push_back(new_thread->get_id());
        }
    }

    ~threadpool_scheduler()
    {
        for (size_t i = 0; i< m_thread_ids.size();++i)
        {
            boost::asynchronous::detail::default_termination_task<typename Q::diagnostic_type,boost::thread_group> ttask(m_group);
            // this task has to be executed lat => lowest prio
#ifndef BOOST_NO_RVALUE_REFERENCES
            typename queue_type::job_type job(ttask);
            m_private_queue->push(std::move(job),std::numeric_limits<std::size_t>::max());
#else
            m_private_queue->push(typename queue_type::job_type(ttask),std::numeric_limits<std::size_t>::max());
#endif
        }
    }

    //TODO move?
    boost::asynchronous::any_joinable get_worker()const
    {
        return boost::asynchronous::any_joinable (boost::asynchronous::detail::worker_wrap<boost::thread_group>(m_group));
    }

    std::vector<boost::thread::id> thread_ids()const
    {
        return m_thread_ids;
    }
    std::map<std::string,
             std::list<typename boost::asynchronous::job_traits<typename queue_type::job_type>::diagnostic_item_type > >
    get_diagnostics(std::size_t =0)const
    {
        return m_diagnostics->get_map();
    }
    void set_steal_from_queues(std::vector<boost::asynchronous::any_queue_ptr<job_type> > const&)
    {
        // this scheduler does not steal
    }

    static void run(boost::shared_ptr<queue_type> queue,boost::shared_ptr<boost::asynchronous::threadsafe_list<job_type> > private_queue,
                    boost::shared_ptr<diag_type> diagnostics,
                    boost::shared_future<boost::thread*> self,
                    boost::weak_ptr<this_type> this_)
    {
        boost::thread* t = self.get();
        boost::asynchronous::detail::single_queue_scheduler_policy<Q>::m_self_thread.reset(new thread_ptr_wrapper(t));
        // thread scheduler => tss
        boost::asynchronous::any_weak_scheduler<job_type> self_as_weak = boost::asynchronous::detail::lockable_weak_scheduler<this_type>(this_);
        boost::asynchronous::get_thread_scheduler<job_type>(self_as_weak,true);

        boost::asynchronous::get_continuations(std::deque<boost::asynchronous::any_continuation>(),true);

        while(true)
        {
            try
            {
                {
                    // get a job
                    //TODO rval ref?
                    typename Q::job_type job;
                    // try from queue
                    bool popped = queue->try_pop(job);
                    if (!popped)
                    {
                        // try from private queue
                        popped = private_queue->try_pop(job);
                    }
                    // did we manage to pop or steal?
                    if (popped)
                    {
                        // automatic closing of log
                        boost::asynchronous::detail::job_diagnostic_closer<typename Q::job_type,diag_type> closer
                                (&job,diagnostics.get());
                        // log time
                        boost::asynchronous::job_traits<typename Q::job_type>::set_started_time(job);
                        // execute job
                        job();
                    }
                    else
                    {
                        // look for waiting tasks
                        std::deque<boost::asynchronous::any_continuation>& waiting = boost::asynchronous::get_continuations();
                        if (!waiting.empty())
                        {
                            for (std::deque<boost::asynchronous::any_continuation>::iterator it = waiting.begin(); it != waiting.end();)
                            {
                                if ((*it).is_ready())
                                {
                                    (*it)();
                                    it = waiting.erase(it);
                                }
                                else
                                {
                                    ++it;
                                }
                            }
                        }
                        // nothing for us to do, give up our time slice
                        boost::this_thread::yield();
                    }
                } // job destroyed (for destruction useful)
                // check if we got an interruption job
                boost::this_thread::interruption_point();
            }
            catch(boost::asynchronous::detail::shutdown_exception&)
            {
                // we are done
                return;
            }
            catch(boost::thread_interrupted&)
            {
                // task interrupted, no problem, just continue
            }
            catch(std::exception&)
            {
                // TODO, user-defined error
            }
        }
    }
private:
    boost::shared_ptr<boost::thread_group> m_group;
    std::vector<boost::thread::id> m_thread_ids;
    boost::shared_ptr<diag_type> m_diagnostics;
    boost::shared_ptr<boost::asynchronous::threadsafe_list<job_type> > m_private_queue;
    size_t m_number_of_workers;
};

}} // boost::asynchronous::scheduler


#endif // BOOST_ASYNC_SCHEDULER_THREADPOOL_SCHEDULER_HPP
