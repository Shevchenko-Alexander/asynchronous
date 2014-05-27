// Boost.Asynchronous library
//  Copyright (C) Christophe Henry, Tobias Holl 2014
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#ifndef BOOST_ASYNCHRON_PARALLEL_FIND_ALL_HPP
#define BOOST_ASYNCHRON_PARALLEL_FIND_ALL_HPP

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
#include <boost/range/algorithm_ext/erase.hpp>
#include <boost/range/algorithm_ext/push_back.hpp>

namespace boost { namespace asynchronous
{
    
namespace detail {
    
template <class Range, class Func, class ReturnRange>
void find_all(Range rng, Func fn, ReturnRange& ret) {
    boost::copy(rng | boost::adaptors::filtered(fn), std::back_inserter(ret));
}

template <class Func>
struct not_
{
    not_(Func&& f):func_(std::forward<Func>(f)){}
    template <typename... Arg>
    bool operator()(Arg... arg)const
    {
        return !func_(arg...);
    }
    Func func_;
};

template <class Range, class Func>
void find_all(Range& rng, Func fn) {
    boost::remove_erase_if(rng , boost::asynchronous::detail::not_<Func>(std::move(fn)));
}
    
}

// version for moved ranges
namespace detail
{
template <class Range, class Func, class ReturnRange, class Job,class Enable=void>
struct parallel_find_all_range_move_helper: public boost::asynchronous::continuation_task<ReturnRange>
{
    parallel_find_all_range_move_helper(Range&& range,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        :range_(boost::make_shared<Range>(std::forward<Range>(range))),func_(std::move(func)),cutoff_(cutoff),task_name_(std::move(task_name)),prio_(prio)
    {}
    void operator()()const
    {
        std::vector<boost::future<ReturnRange> > fus;
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
            boost::future<ReturnRange> fu = boost::asynchronous::post_future(locked_scheduler,
                                                                      [it,itp,func]()
                                                                      {
                                                                        ReturnRange ret(itp,it);
                                                                        boost::asynchronous::detail::find_all(ret,func);
                                                                        return ret;
                                                                      },
                                                                      task_name_,prio_);
            fus.emplace_back(std::move(fu));
        }
        boost::asynchronous::continuation_result<ReturnRange> task_res = this->this_task_result();
        auto func = func_;
        boost::asynchronous::create_continuation_job<Job>(
                    // called when subtasks are done, set our result
                    [task_res, func,range](std::vector<boost::future<ReturnRange>> res)
                    {
                        try
                        {
                            ReturnRange rt;
                            bool set = false;
                            for (typename std::vector<boost::future<ReturnRange>>::iterator itr = res.begin();itr != res.end();++itr)
                            {
                                // get values, check that no exception exists
                                if (set)
                                    boost::range::push_back(rt, (*itr).get());
                                else {
                                    rt = (*itr).get();
                                    set = true;
                                }   
                            }
                            task_res.emplace_value(std::move(rt));
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

template <class Func, class Range, class ReturnRange>
struct serializable_find_all : public boost::asynchronous::serializable_task
{
    serializable_find_all(): boost::asynchronous::serializable_task(""){}
    serializable_find_all(Func&& f, Range&& r)
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
    ReturnRange operator()()
    {
        ReturnRange ret;
        boost::asynchronous::detail::find_all(range_, func_, ret);
        return ret;
    }

    Func func_;
    Range range_;
};

namespace detail
{
template <class Range, class Func, class ReturnRange, class Job>
struct parallel_find_all_range_move_helper<Range,Func,ReturnRange,Job,typename ::boost::enable_if<boost::asynchronous::detail::is_serializable<Func> >::type>
        : public boost::asynchronous::continuation_task<ReturnRange>
        , public boost::asynchronous::serializable_task
{
    parallel_find_all_range_move_helper(Range&& range,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        : boost::asynchronous::continuation_task<ReturnRange>()
        , boost::asynchronous::serializable_task(func.get_task_name())
        , range_(boost::make_shared<Range>(std::forward<Range>(range))),func_(std::move(func)),cutoff_(cutoff),task_name_(std::move(task_name)),prio_(prio)
    {}
    void operator()()const
    {
        typedef std::vector<typename std::iterator_traits<decltype(boost::begin(*range_))>::value_type> sub_range;
        std::vector<boost::future<ReturnRange> > fus;
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
            boost::future<ReturnRange> fu = boost::asynchronous::post_future(locked_scheduler,
                                                                            boost::asynchronous::serializable_find_all<Func,sub_range,ReturnRange>
                                                                                (std::move(func),std::move(part_vec)),
                                                                            task_name_,prio_);
            fus.emplace_back(std::move(fu));
        }
        boost::asynchronous::continuation_result<ReturnRange> task_res = this->this_task_result();
        auto func = func_;
        boost::asynchronous::create_continuation_job<Job>(
                    // called when subtasks are done, set our result
                    [task_res, func,range](std::vector<boost::future<ReturnRange>> res)
                    {
                        try
                        {
                            ReturnRange rt;
                            bool set = false;
                            for (typename std::vector<boost::future<ReturnRange>>::iterator itr = res.begin();itr != res.end();++itr)
                            {
                                // get values, check that no exception exists
                                if (set)
                                    boost::range::push_back(rt, (*itr).get());
                                else {
                                    rt = (*itr).get();
                                    set = true;
                                }   
                            }
                            task_res.emplace_value(std::move(rt));
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

template <class Range, class Func, class ReturnRange=Range, class Job=boost::asynchronous::any_callable>
typename boost::disable_if<has_is_continuation_task<Range>,boost::asynchronous::detail::continuation<ReturnRange,Job> >::type
parallel_find_all(Range&& range,Func func,long cutoff,
             const std::string& task_name="", std::size_t prio=0)
{
    return boost::asynchronous::top_level_continuation_log<ReturnRange,Job>
            (boost::asynchronous::detail::parallel_find_all_range_move_helper<Range,Func,ReturnRange,Job>(std::forward<Range>(range),func,cutoff,task_name,prio));
}

// version for ranges held only by reference
namespace detail
{
template <class Range, class Func, class ReturnRange, class Job>
struct parallel_find_all_range_helper: public boost::asynchronous::continuation_task<ReturnRange>
{
    parallel_find_all_range_helper(Range const& range,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        :range_(range),func_(std::move(func)),cutoff_(cutoff),task_name_(std::move(task_name)),prio_(prio)
    {}
    void operator()()const
    {
        std::vector<boost::future<ReturnRange> > fus;
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
            boost::future<ReturnRange> fu = boost::asynchronous::post_future(locked_scheduler,
                                                                      [it,itp,func]()
                                                                      {
                                                                        ReturnRange ret;
                                                                        boost::asynchronous::detail::find_all(boost::make_iterator_range(itp, it), func, ret);
                                                                        return ret;
                                                                      },
                                                                      task_name_,prio_);
            fus.emplace_back(std::move(fu));
        }
        boost::asynchronous::continuation_result<ReturnRange> task_res = this->this_task_result();
        auto func = func_;
        boost::asynchronous::create_continuation_job<Job>(
                    // called when subtasks are done, set our result
                    [task_res, func](std::vector<boost::future<ReturnRange>> res)
                    {
                        try
                        {
                            ReturnRange rt;
                            bool set = false;
                            for (typename std::vector<boost::future<ReturnRange>>::iterator itr = res.begin();itr != res.end();++itr)
                            {
                                // get values, check that no exception exists
                                if (set)
                                    boost::range::push_back(rt, (*itr).get());
                                else {
                                    rt = (*itr).get();
                                    set = true;
                                }   
                            }
                            task_res.emplace_value(std::move(rt));
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
template <class Range, class Func, class ReturnRange=Range, class Job=boost::asynchronous::any_callable>
typename boost::disable_if<has_is_continuation_task<Range>,boost::asynchronous::detail::continuation<ReturnRange,Job> >::type
parallel_find_all(Range const& range,Func func,long cutoff,
             const std::string& task_name="", std::size_t prio=0)
{
    return boost::asynchronous::top_level_continuation_log<ReturnRange,Job>
            (boost::asynchronous::detail::parallel_find_all_range_helper<Range,Func,ReturnRange,Job>(range,func,cutoff,task_name,prio));
}

// version for ranges given as continuation
namespace detail
{
template <class Continuation, class Func, class ReturnRange, class Job>
struct parallel_find_all_continuation_range_helper: public boost::asynchronous::continuation_task<ReturnRange>
{
    parallel_find_all_continuation_range_helper(Continuation const& c,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        :cont_(c),func_(std::move(func)),cutoff_(cutoff),task_name_(std::move(task_name)),prio_(prio)
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<ReturnRange> task_res = this->this_task_result();
        auto func(std::move(func_));
        auto cutoff = cutoff_;
        auto task_name = task_name_;
        auto prio = prio_;
        cont_.on_done([task_res,func,cutoff,task_name,prio](std::tuple<boost::future<typename Continuation::return_type> >&& continuation_res)
        {
            try
            {
                auto new_continuation = boost::asynchronous::parallel_find_all<typename Continuation::return_type, Func, ReturnRange, Job>(std::move(std::get<0>(continuation_res).get()),func,cutoff,task_name,prio);
                new_continuation.on_done([task_res](std::tuple<boost::future<ReturnRange> >&& new_continuation_res)
                {
                    task_res.emplace_value(std::move(std::get<0>(new_continuation_res).get()));
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

template <class Range, class Func, class ReturnRange=typename Range::return_type, class Job=typename boost::asynchronous::any_callable>
typename boost::enable_if<has_is_continuation_task<Range>, boost::asynchronous::detail::continuation<ReturnRange, Job>>::type
parallel_find_all(Range range,Func func,long cutoff,
             const std::string& task_name="", std::size_t prio=0)
{
    return boost::asynchronous::top_level_continuation_log<ReturnRange,Job>
            (boost::asynchronous::detail::parallel_find_all_continuation_range_helper<Range,Func,ReturnRange,Job>(range,func,cutoff,task_name,prio));
}

// version for iterators
namespace detail
{
template <class Iterator, class Func, class ReturnRange, class Job>
struct parallel_find_all_helper: public boost::asynchronous::continuation_task<ReturnRange>
{
    parallel_find_all_helper(Iterator beg, Iterator end,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        : beg_(beg),end_(end),func_(std::move(func)),cutoff_(cutoff),task_name_(std::move(task_name)),prio_(prio)
    {}
    void operator()()const
    {
        std::vector<boost::future<ReturnRange> > fus;
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
            boost::future<ReturnRange> fu = boost::asynchronous::post_future(locked_scheduler,
                                                                      [it,itp,func]()
                                                                      {
                                                                        ReturnRange ret(itp,it);
                                                                        boost::asynchronous::detail::find_all(ret,func);
                                                                        return ret;
                                                                      },
                                                                      task_name_,prio_);
            fus.emplace_back(std::move(fu));
        }
        boost::asynchronous::continuation_result<ReturnRange> task_res = this->this_task_result();
        auto func = func_;
        boost::asynchronous::create_continuation_job<Job>(
                    // called when subtasks are done, set our result
                    [task_res, func](std::vector<boost::future<ReturnRange>> res)
                    {
                        try
                        {
                            ReturnRange rt;
                            bool set = false;
                            for (typename std::vector<boost::future<ReturnRange>>::iterator itr = res.begin();itr != res.end();++itr)
                            {
                                // get values, check that no exception exists
                                if (set)
                                    boost::range::push_back(rt, (*itr).get());
                                else {
                                    rt = (*itr).get();
                                    set = true;
                                }   
                            }
                            task_res.emplace_value(std::move(rt));
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
          class ReturnRange=std::vector<typename std::iterator_traits<Iterator>::value_type>,
          class Job=boost::asynchronous::any_callable>
boost::asynchronous::detail::continuation<ReturnRange,Job>
parallel_find_all(Iterator beg, Iterator end,Func func,long cutoff,
             const std::string& task_name="", std::size_t prio=0)
{
    return boost::asynchronous::top_level_continuation_log<ReturnRange,Job>
            (boost::asynchronous::detail::parallel_find_all_helper<Iterator,Func,ReturnRange,Job>(beg,end,func,cutoff,task_name,prio));
}

}}
#endif // BOOST_ASYNCHRON_PARALLEL_FIND_ALL_HPP