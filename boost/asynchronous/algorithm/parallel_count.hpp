// Boost.Asynchronous library
//  Copyright (C) Christophe Henry, Tobias Holl 2014
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#ifndef BOOST_ASYNCHRON_PARALLEL_COUNT_HPP
#define BOOST_ASYNCHRON_PARALLEL_COUNT_HPP

#include <algorithm>
#include <utility>
#include <type_traits>
#include <iterator>

#include <boost/thread/future.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/serialization/vector.hpp>

#include <boost/asynchronous/detail/any_interruptible.hpp>
#include <boost/asynchronous/callable_any.hpp>
#include <boost/asynchronous/detail/continuation_impl.hpp>
#include <boost/asynchronous/continuation_task.hpp>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/scheduler/serializable_task.hpp>
#include <boost/asynchronous/algorithm/detail/safe_advance.hpp>
#include <boost/asynchronous/detail/metafunctions.hpp>

#include <boost/range/begin.hpp>
#include <boost/range/end.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/range/algorithm.hpp>

namespace boost { namespace asynchronous
{
    
namespace detail {

/* Did not work.
template <class Range, class Func>
typename boost::range_difference<Range>::type count(Range const& r, Func fn) {
    return boost::size(r | boost::adaptors::filtered(fn));
}
*/

template <class Range, class Func>
long count(Range const& r, Func fn) {
    long c = 0;
    for (auto it = boost::begin(r); it != boost::end(r); ++it) {
        if (fn(*it))
            ++c;
    }
    return c;
}
    
}

// version for moved ranges
namespace detail
{
template <class Range, class Func, class Job,class Enable=void>
struct parallel_count_range_move_helper: public boost::asynchronous::continuation_task<long>
{
    parallel_count_range_move_helper(Range&& range,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        :range_(boost::make_shared<Range>(std::forward<Range>(range))),func_(std::move(func)),cutoff_(cutoff),task_name_(std::move(task_name)),prio_(prio)
    {}
    void operator()()const
    {
        std::vector<boost::future<long> > fus;
        boost::asynchronous::any_weak_scheduler<Job> weak_scheduler = boost::asynchronous::get_thread_scheduler<Job>();
        boost::asynchronous::any_shared_scheduler<Job> locked_scheduler = weak_scheduler.lock();
        //TODO return what?
    //    if (!locked_scheduler.is_valid())
    //        // give up
    //        return;
        boost::shared_ptr<Range> range = std::move(range_);
        for (auto it= boost::begin(*range); it != boost::end(*range) ; )
        {
            auto itp = it;
            boost::asynchronous::detail::safe_advance(it,cutoff_,boost::end(*range));
            auto func = func_;
            boost::future<long> fu = boost::asynchronous::post_future(locked_scheduler,
                                                                      [it,itp,func]()
                                                                      {
                                                                        return boost::asynchronous::detail::count(boost::make_iterator_range(itp, it), func);
                                                                      },
                                                                      task_name_,prio_);
            fus.emplace_back(std::move(fu));
        }
        boost::asynchronous::continuation_result<long> task_res = this->this_task_result();
        auto func = func_;
        boost::asynchronous::create_continuation_job<Job>(
                    // called when subtasks are done, set our result
                    [task_res, func,range](std::vector<boost::future<long>> res)
                    {
                        try
                        {
                            long rt = 0;
                            for (typename std::vector<boost::future<long>>::iterator itr = res.begin();itr != res.end();++itr)
                            {
                                // get values, check that no exception exists
                                rt += (*itr).get();
                            }
                            task_res.set_value(rt);
                        }
                        catch(std::exception& e)
                        {
                            task_res.set_exception(boost::copy_exception(e));
                        }
                    },
                    // future results of recursive tasks
                    std::move(fus));
    }
    boost::shared_ptr<Range> range_;
    Func func_;
    long cutoff_;
    std::string task_name_;
    std::size_t prio_;
};
}

template <class Func, class Range>
struct serializable_count : public boost::asynchronous::serializable_task
{
    serializable_count(): boost::asynchronous::serializable_task(""){}
    serializable_count(Func&& f, Range&& r)
        : boost::asynchronous::serializable_task(f.get_task_name())
        , func_(std::forward<Func>(f))
        , range_(std::forward<Range>(r))
    {}
    template <class Archive>
    void serialize(Archive & ar, const unsigned int /*version*/)
    {
        ar & func_;
        ar & range_;
    }
    long operator()()
    {
        return boost::asynchronous::detail::count<decltype(range_), Func>(range_, func_);
    }

    Func func_;
    Range range_;
};

namespace detail
{
template <class Range, class Func, class Job>
struct parallel_count_range_move_helper<Range,Func,Job,typename ::boost::enable_if<boost::asynchronous::detail::is_serializable<Func> >::type>
        : public boost::asynchronous::continuation_task<long>
        , public boost::asynchronous::serializable_task
{
    parallel_count_range_move_helper(Range&& range,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        : boost::asynchronous::continuation_task<long>()
        , boost::asynchronous::serializable_task(func.get_task_name())
        , range_(boost::make_shared<Range>(std::forward<Range>(range))),func_(std::move(func)),cutoff_(cutoff),task_name_(std::move(task_name)),prio_(prio)
    {}
    void operator()()const
    {
        typedef std::vector<typename std::iterator_traits<decltype(boost::begin(*range_))>::value_type> sub_range;
        std::vector<boost::future<long> > fus;
        boost::asynchronous::any_weak_scheduler<Job> weak_scheduler = boost::asynchronous::get_thread_scheduler<Job>();
        boost::asynchronous::any_shared_scheduler<Job> locked_scheduler = weak_scheduler.lock();
        //TODO return what?
    //    if (!locked_scheduler.is_valid())
    //        // give up
    //        return;
        boost::shared_ptr<Range> range = boost::make_shared<Range>(std::move(*range_));
        for (auto it= boost::begin(*range); it != boost::end(*range) ; )
        {
            auto itp = it;
            boost::asynchronous::detail::safe_advance(it,cutoff_,boost::end(*range));
            auto part_vec = boost::copy_range<sub_range>(boost::make_iterator_range(itp,it));
            auto func = func_;
            boost::future<long> fu = boost::asynchronous::post_future(locked_scheduler,
                                                                            boost::asynchronous::serializable_count<Func,sub_range>
                                                                                (std::move(func),std::move(part_vec)),
                                                                            task_name_,prio_);
            fus.emplace_back(std::move(fu));
        }
        boost::asynchronous::continuation_result<long> task_res = this->this_task_result();
        auto func = func_;
        boost::asynchronous::create_continuation_job<Job>(
                    // called when subtasks are done, set our result
                    [task_res, func,range](std::vector<boost::future<long>> res)
                    {
                        try
                        {
                            long rt = 0;
                            for (typename std::vector<boost::future<long>>::iterator itr = res.begin();itr != res.end();++itr)
                            {
                                // get values, check that no exception exists
                                rt = (*itr).get();
                            }
                            task_res.set_value(rt);
                        }
                        catch(std::exception& e)
                        {
                            task_res.set_exception(boost::copy_exception(e));
                        }
                    },
                    // future results of recursive tasks
                    std::move(fus));
    }
    template <class Archive>
    void save(Archive & ar, const unsigned int /*version*/)const
    {
        ar & (*range_);
        ar & func_;
        ar & cutoff_;
        ar & task_name_;
        ar & prio_;
    }
    template <class Archive>
    void load(Archive & ar, const unsigned int /*version*/)
    {
        range_ = boost::make_shared<Range>();
        ar & (*range_);
        ar & func_;
        ar & cutoff_;
        ar & task_name_;
        ar & prio_;
    }
    BOOST_SERIALIZATION_SPLIT_MEMBER()
    boost::shared_ptr<Range> range_;
    Func func_;
    long cutoff_;
    std::string task_name_;
    std::size_t prio_;
};
}

template <class Range, class Func, class Job=boost::asynchronous::any_callable>
typename boost::disable_if<has_is_continuation_task<Range>,boost::asynchronous::detail::continuation<long,Job> >::type
parallel_count(Range&& range,Func func,long cutoff,
             const std::string& task_name="", std::size_t prio=0)
{
    return boost::asynchronous::top_level_continuation_log<long,Job>
            (boost::asynchronous::detail::parallel_count_range_move_helper<Range,Func,Job>(std::forward<Range>(range),func,cutoff,task_name,prio));
}

// version for ranges held only by reference
namespace detail
{
template <class Range, class Func, class Job>
struct parallel_count_range_helper: public boost::asynchronous::continuation_task<long>
{
    parallel_count_range_helper(Range const& range,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        :range_(range),func_(std::move(func)),cutoff_(cutoff),task_name_(std::move(task_name)),prio_(prio)
    {}
    void operator()()const
    {
        std::vector<boost::future<long> > fus;
        boost::asynchronous::any_weak_scheduler<Job> weak_scheduler = boost::asynchronous::get_thread_scheduler<Job>();
        boost::asynchronous::any_shared_scheduler<Job> locked_scheduler = weak_scheduler.lock();
        //TODO return what?
    //    if (!locked_scheduler.is_valid())
    //        // give up
    //        return;
        for (auto it= boost::begin(range_); it != boost::end(range_) ; )
        {
            auto itp = it;
            boost::asynchronous::detail::safe_advance(it,cutoff_,boost::end(range_));
            auto func = func_;
            boost::future<long> fu = boost::asynchronous::post_future(locked_scheduler,
                                                                      [it,itp,func]()
                                                                      {
                                                                        return boost::asynchronous::detail::count(boost::make_iterator_range(itp,it),func);
                                                                      },
                                                                      task_name_,prio_);
            fus.emplace_back(std::move(fu));
        }
        boost::asynchronous::continuation_result<long> task_res = this->this_task_result();
        auto func = func_;
        boost::asynchronous::create_continuation_job<Job>(
                    // called when subtasks are done, set our result
                    [task_res, func](std::vector<boost::future<long>> res)
                    {
                        try
                        {
                            long rt = 0;
                            for (typename std::vector<boost::future<long>>::iterator itr = res.begin();itr != res.end();++itr)
                            {
                                // get values, check that no exception exists
                                rt += (*itr).get();
                            }
                            task_res.set_value(rt);
                        }
                        catch(std::exception& e)
                        {
                            task_res.set_exception(boost::copy_exception(e));
                        }
                    },
                    // future results of recursive tasks
                    std::move(fus));
    }
    Range const& range_;
    Func func_;
    long cutoff_;
    std::string task_name_;
    std::size_t prio_;
};
}
template <class Range, class Func, class Job=boost::asynchronous::any_callable>
typename boost::disable_if<has_is_continuation_task<Range>,boost::asynchronous::detail::continuation<long,Job> >::type
parallel_count(Range const& range,Func func,long cutoff,
             const std::string& task_name="", std::size_t prio=0)
{
    return boost::asynchronous::top_level_continuation_log<long,Job>
            (boost::asynchronous::detail::parallel_count_range_helper<Range,Func,Job>(range,func,cutoff,task_name,prio));
}

// version for ranges given as continuation
namespace detail
{
template <class Continuation, class Func, class Job>
struct parallel_count_continuation_range_helper: public boost::asynchronous::continuation_task<long>
{
    parallel_count_continuation_range_helper(Continuation const& c,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        :cont_(c),func_(std::move(func)),cutoff_(cutoff),task_name_(std::move(task_name)),prio_(prio)
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<long> task_res = this->this_task_result();
        auto func(std::move(func_));
        auto cutoff = cutoff_;
        auto task_name = task_name_;
        auto prio = prio_;
        cont_.on_done([task_res,func,cutoff,task_name,prio](std::tuple<boost::future<typename Continuation::return_type> >&& continuation_res)
        {
            try
            {
                auto new_continuation = boost::asynchronous::parallel_count<typename Continuation::return_type, Func, Job>(std::move(std::get<0>(continuation_res).get()),func,cutoff,task_name,prio);
                new_continuation.on_done([task_res](std::tuple<boost::future<long> >&& new_continuation_res)
                {
                    task_res.set_value(std::move(std::get<0>(new_continuation_res).get()));
                });
                boost::asynchronous::any_continuation nac(new_continuation);
                boost::asynchronous::get_continuations().push_front(nac);
            }
            catch(std::exception& e)
            {
                task_res.set_exception(boost::copy_exception(e));
            }
        }
        );
        boost::asynchronous::any_continuation ac(cont_);
        boost::asynchronous::get_continuations().push_front(ac);
    }
    Continuation cont_;
    Func func_;
    long cutoff_;
    std::string task_name_;
    std::size_t prio_;
};
}

template <class Range, class Func, class Job=boost::asynchronous::any_callable>
typename boost::enable_if<has_is_continuation_task<Range>, boost::asynchronous::detail::continuation<long, Job>>::type
parallel_count(Range range,Func func,long cutoff,
             const std::string& task_name="", std::size_t prio=0)
{
    return boost::asynchronous::top_level_continuation_log<long,Job>
            (boost::asynchronous::detail::parallel_count_continuation_range_helper<Range,Func,Job>(range,func,cutoff,task_name,prio));
}

// version for iterators
namespace detail
{
template <class Iterator, class Func, class Job>
struct parallel_count_helper: public boost::asynchronous::continuation_task<long>
{
    parallel_count_helper(Iterator beg, Iterator end,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        : beg_(beg),end_(end),func_(std::move(func)),cutoff_(cutoff),task_name_(std::move(task_name)),prio_(prio)
    {}
    void operator()()const
    {
        std::vector<boost::future<long> > fus;
        boost::asynchronous::any_weak_scheduler<Job> weak_scheduler = boost::asynchronous::get_thread_scheduler<Job>();
        boost::asynchronous::any_shared_scheduler<Job> locked_scheduler = weak_scheduler.lock();
        //TODO return what?
    //    if (!locked_scheduler.is_valid())
    //        // give up
    //        return;
        for (Iterator it=beg_; it != end_ ; )
        {
            Iterator itp = it;
            boost::asynchronous::detail::safe_advance(it,cutoff_,end_);
            auto func = func_;
            boost::future<long> fu = boost::asynchronous::post_future(locked_scheduler,
                                                                      [it,itp,func]()
                                                                      {
                                                                        return boost::asynchronous::detail::count(boost::make_iterator_range(itp, it),func);
                                                                      },
                                                                      task_name_,prio_);
            fus.emplace_back(std::move(fu));
        }
        boost::asynchronous::continuation_result<long> task_res = this->this_task_result();
        auto func = func_;
        boost::asynchronous::create_continuation_job<Job>(
                    // called when subtasks are done, set our result
                    [task_res, func](std::vector<boost::future<long>> res)
                    {
                        try
                        {
                            long rt = 0;
                            for (typename std::vector<boost::future<long>>::iterator itr = res.begin();itr != res.end();++itr)
                            {
                                // get values, check that no exception exists
                                rt += (*itr).get();
                            }
                            task_res.set_value(std::move(rt));
                        }
                        catch(std::exception& e)
                        {
                            task_res.set_exception(boost::copy_exception(e));
                        }
                    },
                    // future results of recursive tasks
                    std::move(fus));
    }
    Iterator beg_;
    Iterator end_;
    Func func_;
    long cutoff_;
    std::string task_name_;
    std::size_t prio_;
};
}
template <class Iterator, class Func,
          class Job=boost::asynchronous::any_callable>
boost::asynchronous::detail::continuation<long,Job>
parallel_count(Iterator beg, Iterator end,Func func,long cutoff,
             const std::string& task_name="", std::size_t prio=0)
{
    return boost::asynchronous::top_level_continuation_log<long,Job>
            (boost::asynchronous::detail::parallel_count_helper<Iterator,Func,Job>(beg,end,func,cutoff,task_name,prio));
}

}}
#endif // BOOST_ASYNCHRON_PARALLEL_COUNT_HPP
