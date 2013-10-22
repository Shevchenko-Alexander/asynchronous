// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2013
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#ifndef BOOST_ASYNCHRON_TRACKABLE_SERVANT_HPP
#define BOOST_ASYNCHRON_TRACKABLE_SERVANT_HPP

#include <cstddef>

#include <boost/shared_ptr.hpp>
#include <boost/asynchronous/callable_any.hpp>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/checks.hpp>
#include <boost/asynchronous/scheduler/tss_scheduler.hpp>
#include <boost/system/error_code.hpp>

namespace boost { namespace asynchronous
{
//TODO in detail
struct track{};

// simple class for post and callback management
// hides threadpool and weak scheduler, adds automatic trackability for callbacks and tasks
// inherit from it to get functionality
template <class JOB = boost::asynchronous::any_callable,class WJOB = boost::asynchronous::any_callable>
class trackable_servant
{
public:
    typedef int requires_weak_scheduler;
    trackable_servant(boost::asynchronous::any_weak_scheduler<JOB> const& s,
                      boost::asynchronous::any_shared_scheduler_proxy<WJOB> w=boost::asynchronous::any_shared_scheduler_proxy<WJOB>())
        : m_tracking(boost::make_shared<boost::asynchronous::track>())
        , m_scheduler(s)
        , m_worker(w)
    {}
    trackable_servant(boost::asynchronous::any_shared_scheduler_proxy<WJOB> w=boost::asynchronous::any_shared_scheduler_proxy<WJOB>())
        : m_tracking(boost::make_shared<boost::asynchronous::track>())
        , m_scheduler(boost::asynchronous::get_thread_scheduler<JOB>())
        , m_worker(w)
    {}
    // copy-ctor and operator= are needed for correct tracking
    trackable_servant(trackable_servant const& rhs)
        : m_tracking(boost::make_shared<boost::asynchronous::track>())
        , m_scheduler(rhs.m_scheduler)
        , m_worker(rhs.m_worker)
    {
    }
    ~trackable_servant()
    {
    }

    trackable_servant& operator= (trackable_servant const& rhs)
    {
        m_tracking = boost::make_shared<boost::asynchronous::track>();
        m_scheduler(rhs.m_scheduler);
        m_worker(rhs.m_worker);
    }
    //TODO move?

    // make a callback, which posts if not the correct thread, and call directly otherwise
    // in any case, check if this object is still alive
    template<typename... Args>
    std::function<void(Args... )> make_safe_callback(std::function<void(Args... )> func)
    {
        boost::weak_ptr<track> tracking (m_tracking);
        boost::asynchronous::any_weak_scheduler<JOB> wscheduler = get_scheduler();
        //TODO functor with move
        std::function<void(Args...)> res = [func,tracking,wscheduler](Args... as)mutable
        {
            boost::asynchronous::any_shared_scheduler<JOB> sched = wscheduler.lock();
            if (sched.is_valid())
            {
                std::vector<boost::thread::id> ids = sched.thread_ids();
                if ((std::find(ids.begin(),ids.end(),boost::this_thread::get_id()) != ids.end()))
                {
                    // our thread, call if servant alive
                   std::bind( boost::asynchronous::check_alive([func](Args... args){func(args...);},tracking),as...)();
                }
                else
                {
                    // not in our thread, post
                    sched.post(std::bind( boost::asynchronous::check_alive([func](Args... args){func(args...);},tracking),as...));
                }
            }
        };
        return res;
    }
       
    // helper to make it easier using a timer service
    template <class Timer, class F>
    void async_wait(Timer& t, F&& func)
    {
        std::function<void(const ::boost::system::error_code&)> f = std::forward<F>(func);
        call_callback(t.get_proxy(),
                      t.unsafe_async_wait(make_safe_callback(f)),
                      // ignore async_wait callback functor., real callback is above
                      [](boost::future<void> ){}
                      );
    }
                
#ifndef BOOST_NO_RVALUE_REFERENCES
    template <class F1, class F2>
    void post_callback(F1&& func,F2&& cb_func, std::string const& task_name="", std::size_t post_prio=0, std::size_t cb_prio=0)
    {
        // we want to log if possible
        boost::asynchronous::post_callback(m_worker,
                                        boost::asynchronous::check_alive_before_exec(std::forward<F1>(func),m_tracking),
                                        //std::forward<F1>(func),
                                        m_scheduler,
                                        boost::asynchronous::check_alive(std::forward<F2>(cb_func),m_tracking),
                                        task_name,
                                        post_prio,
                                        cb_prio);
    }
    template <class F1, class F2>
    boost::asynchronous::any_interruptible interruptible_post_callback(F1&& func,F2&& cb_func, std::string const& task_name="",
                                                                    std::size_t post_prio=0, std::size_t cb_prio=0)
    {
        return boost::asynchronous::interruptible_post_callback(
                                        m_worker,
                                        boost::asynchronous::check_alive_before_exec(std::forward<F1>(func),m_tracking),
                                        m_scheduler,
                                        boost::asynchronous::check_alive(std::forward<F2>(cb_func),m_tracking),
                                        task_name,
                                        post_prio,
                                        cb_prio);
    }
    template <class CallerSched,class F1, class F2>
    void call_callback(CallerSched s, F1&& func,F2&& cb_func, std::string const& task_name="", std::size_t post_prio=0, std::size_t cb_prio=0)
    {
        // we want to log if possible
        boost::asynchronous::post_callback(s,
                                        boost::asynchronous::check_alive_before_exec(std::forward<F1>(func),m_tracking),
                                        m_scheduler,
                                        boost::asynchronous::check_alive(std::forward<F2>(cb_func),m_tracking),
                                        task_name,
                                        post_prio,
                                        cb_prio);
    }
    template <class CallerSched,class F1, class F2>
    boost::asynchronous::any_interruptible interruptible_call_callback(CallerSched s, F1&& func,F2&& cb_func, std::string const& task_name="",
                                                                    std::size_t post_prio=0, std::size_t cb_prio=0)
    {
        return boost::asynchronous::interruptible_post_callback(
                                        s,
                                        boost::asynchronous::check_alive_before_exec(std::forward<F1>(func),m_tracking),
                                        m_scheduler,
                                        boost::asynchronous::check_alive(std::forward<F2>(cb_func),m_tracking),
                                        task_name,
                                        post_prio,
                                        cb_prio);
    }
#else
    template <class F1, class F2>
    void post_callback(F1 const& func,F2 const& cb_func, std::string const& task_name="",
                       std::size_t post_prio=0, std::size_t cb_prio=0)
    {
        boost::asynchronous::post_callback(
                                        m_worker,
                                        boost::asynchronous::check_alive_before_exec(func,m_tracking),
                                        m_scheduler,
                                        boost::asynchronous::check_alive(cb_func,m_tracking),
                                        task_name,
                                        post_prio,
                                        cb_prio);
    }
    template <class F1, class F2>
    boost::asynchronous::any_interruptible interruptible_post_callback(F1 const& func,F2 const& cb_func, std::string const& task_name="",
                                                                    std::size_t post_prio=0, std::size_t cb_prio=0)
    {
        return boost::asynchronous::interruptible_post_callback(
                                        m_worker,
                                        boost::asynchronous::check_alive_before_exec(func,m_tracking),
                                        m_scheduler,
                                        boost::asynchronous::check_alive(cb_func,m_tracking),
                                        task_name,
                                        post_prio,
                                        cb_prio);
    }
#endif
protected:
    boost::asynchronous::any_shared_scheduler_proxy<WJOB> const& get_worker()const
    {
        return m_worker;
    }
    void set_worker(boost::asynchronous::any_shared_scheduler_proxy<WJOB> w)
    {
        m_worker=w;
    }
    boost::asynchronous::any_weak_scheduler<JOB> const& get_scheduler()const
    {
        return m_scheduler;
    }
    // tracking object for callbacks / tasks
    boost::shared_ptr<track> m_tracking;
private:
    // scheduler where we are living
    boost::asynchronous::any_weak_scheduler<JOB> m_scheduler;
    // our worker pool
    boost::asynchronous::any_shared_scheduler_proxy<WJOB> m_worker;

};

}}

#endif // BOOST_ASYNCHRON_TRACKABLE_SERVANT_HPP
