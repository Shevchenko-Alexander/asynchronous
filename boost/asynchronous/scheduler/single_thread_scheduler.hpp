// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2013
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#ifndef BOOST_ASYNC_SCHEDULER_SINGLE_THREAD_SCHEDULER_HPP
#define BOOST_ASYNC_SCHEDULER_SINGLE_THREAD_SCHEDULER_HPP

#include <utility>
#include <cstddef>

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/tss.hpp>
#include <boost/bind.hpp>

#include <boost/asynchronous/scheduler/detail/scheduler_helpers.hpp>
#include <boost/asynchronous/detail/any_joinable.hpp>
#include <boost/asynchronous/detail/any_interruptible.hpp>
#include <boost/asynchronous/diagnostics/default_loggable_job.hpp>
#include <boost/asynchronous/job_traits.hpp>
#include <boost/asynchronous/scheduler/detail/job_diagnostic_closer.hpp>
#include <boost/asynchronous/queue/any_queue.hpp>
#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/scheduler/detail/single_queue_scheduler_policy.hpp>
#include <boost/asynchronous/detail/any_joinable.hpp>
#include <boost/asynchronous/scheduler/tss_scheduler.hpp>
#include <boost/asynchronous/scheduler/detail/lockable_weak_scheduler.hpp>
#include <boost/asynchronous/scheduler/cpu_load_policies.hpp>

namespace boost { namespace asynchronous
{

template<class Q, class CPULoad =
#ifdef BOOST_ASYNCHRONOUS_NO_SAVING_CPU_LOAD
        boost::asynchronous::no_cpu_load_saving
#else
        boost::asynchronous::default_save_cpu_load<>
#endif
                  >
class single_thread_scheduler: public boost::asynchronous::detail::single_queue_scheduler_policy<Q>
{
public:
    typedef Q queue_type;
    typedef typename Q::job_type job_type;
    typedef typename boost::asynchronous::job_traits<typename Q::job_type>::diagnostic_table_type diag_type;
    typedef single_thread_scheduler<Q,CPULoad> this_type;

#ifndef BOOST_NO_RVALUE_REFERENCES
    single_thread_scheduler(boost::shared_ptr<queue_type>&& queue)
        : boost::asynchronous::detail::single_queue_scheduler_policy<Q>(std::forward<boost::shared_ptr<queue_type> >(queue))
        , m_private_queue(boost::make_shared<boost::asynchronous::lockfree_queue<boost::asynchronous::any_callable>>())
    {
    }
#endif
#ifndef BOOST_NO_CXX11_VARIADIC_TEMPLATES
    template<typename... Args>
    single_thread_scheduler(Args... args)
        : boost::asynchronous::detail::single_queue_scheduler_policy<Q>(boost::make_shared<queue_type>(std::move(args)...))
        , m_private_queue(boost::make_shared<boost::asynchronous::lockfree_queue<boost::asynchronous::any_callable>>())
    {
    }
#endif

    single_thread_scheduler()
        : boost::asynchronous::detail::single_queue_scheduler_policy<Q>()
        , m_private_queue(boost::make_shared<boost::asynchronous::lockfree_queue<boost::asynchronous::any_callable>>())
    {
    }
    void constructor_done(boost::weak_ptr<this_type> weak_self)
    {
        m_diagnostics = boost::make_shared<diag_type>(1);
        boost::promise<boost::thread*> new_thread_promise;
        boost::shared_future<boost::thread*> fu = new_thread_promise.get_future();
        boost::thread* new_thread =
                new boost::thread(boost::bind(&single_thread_scheduler::run,this->m_queue,m_diagnostics,m_private_queue,fu,weak_self));
        new_thread_promise.set_value(new_thread);
        m_thread.reset(new_thread);
    }

    ~single_thread_scheduler()
    {
        boost::asynchronous::detail::default_termination_task<typename Q::diagnostic_type,boost::thread> ttask;
        // this task has to be executed lat => lowest prio
#ifndef BOOST_NO_RVALUE_REFERENCES
        boost::asynchronous::any_callable job(std::move(ttask));
        m_private_queue->push(std::move(job),std::numeric_limits<std::size_t>::max());
#else
        m_private_queue->push(boost::asynchronous::any_callable(ttask),std::numeric_limits<std::size_t>::max());
#endif
    }
    //TODO move?
    boost::asynchronous::any_joinable get_worker()const
    {
        return boost::asynchronous::any_joinable (boost::asynchronous::detail::worker_wrap<boost::thread>(m_thread));
    }

    boost::thread::id id()const
    {
        return m_thread->get_id();
    }

    std::vector<boost::thread::id> thread_ids()const
    {
        std::vector<boost::thread::id> ids;
        ids.push_back(m_thread->get_id());
        return ids;
    }

    boost::asynchronous::scheduler_diagnostics<job_type>
    get_diagnostics(std::size_t =0)const
    {
        return boost::asynchronous::scheduler_diagnostics<job_type>(m_diagnostics->get_map(),m_diagnostics->get_current());
    }
    void clear_diagnostics()
    {
        m_diagnostics->clear();
    }
    std::vector<boost::asynchronous::any_queue_ptr<job_type> > get_queues()
    {
        // this scheduler doesn't give any queues for stealing
        return std::vector<boost::asynchronous::any_queue_ptr<job_type> >();
    }
    void set_steal_from_queues(std::vector<boost::asynchronous::any_queue_ptr<job_type> > const&)
    {
        // this scheduler does not steal
    }
    void set_name(std::string const& name)
    {
        boost::asynchronous::detail::set_name_task<typename Q::diagnostic_type> ntask(name);
#ifndef BOOST_NO_RVALUE_REFERENCES
        boost::asynchronous::any_callable job(std::move(ntask));
        m_private_queue->push(std::move(job),std::numeric_limits<std::size_t>::max());
#else
        m_private_queue->push(boost::asynchronous::any_callable(ntask),std::numeric_limits<std::size_t>::max());
#endif
    }
    // try to execute a job, return true
    static bool execute_one_job(boost::shared_ptr<queue_type> queue,CPULoad& cpu_load,boost::shared_ptr<diag_type> diagnostics,
                                std::list<boost::asynchronous::any_continuation>& waiting)
    {
        bool popped = false;
        // get a job
        typename Q::job_type job;
        try
        {
            {

                // try from queue
                popped = queue->try_pop(job);
                if (popped)
                {
                    cpu_load.popped_job();
                    // automatic closing of log
                    boost::asynchronous::detail::job_diagnostic_closer<typename Q::job_type,diag_type> closer
                            (&job,diagnostics.get());
                    // log time
                    boost::asynchronous::job_traits<typename Q::job_type>::set_started_time(job);
                    // log current
                    boost::asynchronous::job_traits<typename Q::job_type>::add_current_diagnostic(0,job,diagnostics.get());
                    // execute job
                    job();
                    boost::asynchronous::job_traits<typename Q::job_type>::reset_current_diagnostic(0,diagnostics.get());
                }
                else
                {
                    // look for waiting tasks
                    if (!waiting.empty())
                    {
                        for (std::list<boost::asynchronous::any_continuation>::iterator it = waiting.begin(); it != waiting.end();)
                        {
                            if ((*it).is_ready())
                            {
                                boost::asynchronous::any_continuation c = std::move(*it);
                                it = waiting.erase(it);
                                c();
                            }
                            else
                            {
                                ++it;
                            }
                        }
                    }
                }

            }
        }
        catch(boost::thread_interrupted&)
        {
            // task interrupted, no problem, just continue
        }
        catch(std::exception&)
        {
            if (popped)
            {
                boost::asynchronous::job_traits<typename Q::job_type>::set_failed(job);
                boost::asynchronous::job_traits<typename Q::job_type>::reset_current_diagnostic(0,diagnostics.get());
            }
        }
        return popped;
    }

    static void run(boost::shared_ptr<queue_type> queue,boost::shared_ptr<diag_type> diagnostics,
                    boost::shared_ptr<boost::asynchronous::lockfree_queue<boost::asynchronous::any_callable> > private_queue,
                    boost::shared_future<boost::thread*> self,
                    boost::weak_ptr<this_type> this_)
    {
        boost::thread* t = self.get();
        boost::asynchronous::detail::single_queue_scheduler_policy<Q>::m_self_thread.reset(new thread_ptr_wrapper(t));
        // thread scheduler => tss
        boost::asynchronous::any_weak_scheduler<job_type> self_as_weak = boost::asynchronous::detail::lockable_weak_scheduler<this_type>(this_);
        boost::asynchronous::get_thread_scheduler<job_type>(self_as_weak,true);

        std::list<boost::asynchronous::any_continuation>& waiting =
                boost::asynchronous::get_continuations(std::list<boost::asynchronous::any_continuation>(),true);

        CPULoad cpu_load;
        while(true)
        {
            // get a job
            typename Q::job_type job;
            try
            {
                {
                    bool popped = execute_one_job(queue,cpu_load,diagnostics,waiting);
                    if (!popped)
                    {
                        cpu_load.loop_done_no_job();
                        // nothing for us to do, give up our time slice
                        boost::this_thread::yield();
                    }
                    // check for shutdown
                    boost::asynchronous::any_callable djob;
                    popped = private_queue->try_pop(djob);
                    if (popped)
                    {
                        djob();
                    }
                } // job destroyed (for destruction useful)
                // check if we got an interruption job
                boost::this_thread::interruption_point();
            }
            catch(boost::asynchronous::detail::shutdown_exception&)
            {
                // we are done, execute jobs posted short before to the end, then shutdown
                while(execute_one_job(queue,cpu_load,diagnostics,waiting));
                delete boost::asynchronous::detail::single_queue_scheduler_policy<Q>::m_self_thread.release();
                return;
            }
            catch(boost::thread_interrupted&)
            {
                // task interrupted, no problem, just continue
            }
            catch(std::exception&)
            {
                boost::asynchronous::job_traits<typename Q::job_type>::set_failed(job);
            }
        }
    }
private:
    boost::shared_ptr<boost::thread> m_thread;
    boost::shared_ptr<diag_type> m_diagnostics;
    boost::shared_ptr<boost::asynchronous::lockfree_queue<boost::asynchronous::any_callable>> m_private_queue;
};

}} // boost::async::scheduler

#endif /* BOOST_ASYNC_SCHEDULER_SINGLE_THREAD_SCHEDULER_HPP */
