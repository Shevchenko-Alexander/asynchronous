// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2013
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#ifndef BOOST_ASYNCHRON_SCHEDULER_LOCKABLE_WEAK_SCHEDULER_HPP
#define BOOST_ASYNCHRON_SCHEDULER_LOCKABLE_WEAK_SCHEDULER_HPP


#include <memory>
#include <boost/asynchronous/any_scheduler.hpp>

namespace boost { namespace asynchronous { namespace detail
{
// weak scheduler for use in the servant context
// implements any_weak_scheduler_concept

template <class S>
struct lockable_weak_scheduler
{
    lockable_weak_scheduler(std::shared_ptr<S> scheduler): m_scheduler(scheduler){}
    lockable_weak_scheduler(std::weak_ptr<S> scheduler): m_scheduler(scheduler){}
    any_shared_scheduler<typename S::job_type> lock()const
    {
        std::shared_ptr<S> wscheduler = m_scheduler.lock();
        any_shared_scheduler_ptr<typename S::job_type> pscheduler(std::move(wscheduler));
        return any_shared_scheduler<typename S::job_type>(std::move(pscheduler));
    }
private:
    std::weak_ptr<S> m_scheduler;
};

}}}

#endif // BOOST_ASYNCHRON_SCHEDULER_LOCKABLE_WEAK_SCHEDULER_HPP
