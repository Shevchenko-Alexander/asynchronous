// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2013
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#ifndef BOOST_ASYNC_SCHEDULER_SCHEDULER_HELPERS_HPP
#define BOOST_ASYNC_SCHEDULER_SCHEDULER_HELPERS_HPP

#include<string>
#include <boost/shared_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <boost/asynchronous/scheduler/detail/exceptions.hpp>

#ifdef BOOST_ASYNCHRONOUS_PRCTL_SUPPORT
#include <sys/prctl.h>
#endif

namespace boost { namespace asynchronous { namespace detail
{
// terminates processing of a given scheduler
template <class Diag, class ThreadType>
struct default_termination_task: public Diag
{
    default_termination_task( boost::shared_ptr<ThreadType> w): m_workers(w){}
    void operator()()const
    {
        throw boost::asynchronous::detail::shutdown_exception();
    }
    boost::shared_ptr<ThreadType> m_workers;
};

template<class ThreadType>
struct thread_join_helper
{
    template <class T>
    static void join(T* t)
    {
        t->join();
    }
};
template<>
struct thread_join_helper<boost::thread_group>
{
    template <class T>
    static void join(T* t)
    {
        t->join_all();        
    }
};

template <class ThreadType>
struct worker_wrap
{
    worker_wrap(boost::shared_ptr<ThreadType> g):m_group(g){}
    void join()
    {
        thread_join_helper<ThreadType>::join(m_group.get());
    }
    boost::shared_ptr<ThreadType> m_group;
};
template <class Diag>
struct set_name_task: public Diag
{
    set_name_task( std::string const& n): m_name(n){}
    void operator()()const
    {
#ifdef BOOST_ASYNCHRONOUS_PRCTL_SUPPORT
        prctl(PR_SET_NAME, m_name.c_str(), 0, 0, 0);
#endif
    }
    std::string m_name;
};

}}}
#endif // BOOST_ASYNC_SCHEDULER_SCHEDULER_HELPERS_HPP
