// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2013
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#ifndef BOOST_ASYNCHRONOUS_PARALLEL_SORT_HPP
#define BOOST_ASYNCHRONOUS_PARALLEL_SORT_HPP

#include <vector>
#include <iterator> // for std::iterator_traits
#include <boost/smart_ptr/shared_array.hpp>

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
#include <boost/asynchronous/algorithm/parallel_merge.hpp>
#include <boost/asynchronous/algorithm/parallel_is_sorted.hpp>
#include <boost/asynchronous/algorithm/parallel_reverse.hpp>
#include <boost/asynchronous/algorithm/detail/parallel_sort_helper.hpp>

#include <boost/range/begin.hpp>
#include <boost/range/end.hpp>
#include <boost/range/iterator_range.hpp>

namespace boost { namespace asynchronous
{
// fast version for iterators (double memory costs) => will return nothing
namespace detail
{
template <class Iterator,class Func, class Job, class Sort>
struct parallel_sort_fast_helper: public boost::asynchronous::continuation_task<void>
{
    typedef typename std::iterator_traits<Iterator>::value_type value_type;

    parallel_sort_fast_helper(Iterator beg, Iterator end,unsigned int depth,boost::shared_array<value_type> merge_memory,
                              value_type* beg2, value_type* end2,
                              Func func,long cutoff,const std::string& task_name, std::size_t prio)
        : boost::asynchronous::continuation_task<void>(task_name),
          beg_(beg),end_(end),depth_(depth),func_(std::move(func)),cutoff_(cutoff),prio_(prio),merge_memory_(merge_memory)
        , merge_beg_( beg2), merge_end_(end2)
    {
    }
    static void helper(Iterator beg, Iterator end,unsigned int depth,boost::shared_array<value_type> merge_memory,
                       value_type* beg2, value_type* end2,
                       Func func,long cutoff,const std::string& task_name, std::size_t prio,
                       boost::asynchronous::continuation_result<void> task_res)
    {
        // advance up to cutoff
        Iterator it = boost::asynchronous::detail::find_cutoff(beg,cutoff,end);
        // if not at end, recurse, otherwise execute here
        if ((it == end)&&(depth %2 == 0))
        {
            Sort()(beg,it,func);
            task_res.set_value();
        }
        else
        {
            auto it2 = beg2+std::distance(beg,it);
            auto merge_task_name = task_name + "_merge";
            boost::asynchronous::create_callback_continuation_job<Job>(
                        // called when subtasks are done, set our result
                        [task_res,func,beg,end,it,beg2,end2,it2,depth,cutoff,merge_task_name,prio,merge_memory]
                        (std::tuple<boost::asynchronous::expected<void>,boost::asynchronous::expected<void> > res)
                        {
                            try
                            {
                                // get to check that no exception
                                std::get<0>(res).get();
                                std::get<1>(res).get();
                                // merge both sorted sub-ranges
                                auto on_done_fct = [task_res,depth,merge_memory](std::tuple<boost::asynchronous::expected<void> >&& merge_res)
                                {
                                    try
                                    {
                                        // get to check that no exception
                                        std::get<0>(merge_res).get();
                                        task_res.set_value();
                                    }
                                    catch(std::exception& e)
                                    {
                                        task_res.set_exception(boost::copy_exception(e));
                                    }
                                };
                                if (depth%2 == 0)
                                {
                                    // merge into first range
                                    auto c = boost::asynchronous::parallel_merge<value_type*,value_type*,Iterator,Func,Job>
                                            (beg2,it2,it2,end2,beg,func,cutoff,merge_task_name,prio);
                                    c.on_done(std::move(on_done_fct));
                                }
                                else
                                {
                                    // merge into second range
                                    auto c = boost::asynchronous::parallel_merge<Iterator,Iterator,value_type*,Func,Job>
                                            (beg,it,it,end,beg2,func,cutoff,merge_task_name,prio);
                                    c.on_done(std::move(on_done_fct));
                                }
                            }
                            catch(std::exception& e)
                            {
                                task_res.set_exception(boost::copy_exception(e));
                            }
                        },
                        // recursive tasks
                        parallel_sort_fast_helper<Iterator,Func,Job,Sort>
                            (beg,it,depth+1,merge_memory,beg2,it2,func,cutoff,task_name,prio),
                        parallel_sort_fast_helper<Iterator,Func,Job,Sort>
                            (it,end,depth+1,merge_memory,it2,end2,func,cutoff,task_name,prio)
               );
        }
    }

    void operator()()const
    {
        boost::asynchronous::continuation_result<void> task_res = this_task_result();
        if (depth_ == 0)
        {
            // optimization for cases where we are already sorted
            auto beg = beg_;
            auto end = end_;
            auto depth = depth_;
            auto merge_memory = merge_memory_;
            auto merge_beg = merge_beg_;
            auto merge_end = merge_end_;
            auto func = func_;
            auto cutoff = cutoff_;
            auto task_name = this->get_name();
            auto cont = boost::asynchronous::parallel_is_sorted<Iterator,Func,Job>(beg_,end_,func_,cutoff_,task_name+"_is_sorted",prio_);
            auto prio = prio_;
            cont.on_done([beg,end,depth,merge_memory,merge_beg,merge_end,func,cutoff,task_name,prio,task_res]
                         (std::tuple<boost::asynchronous::expected<bool> >&& res)
            {
                try
                {
                    bool sorted = std::get<0>(res).get();
                    if (sorted)
                    {
                        task_res.set_value();
                        return;
                    }
                    auto cont2 = boost::asynchronous::parallel_is_reverse_sorted<Iterator,Func,Job>
                            (beg,end,func,cutoff,task_name+"_is_reverse_sorted",prio);
                    cont2.on_done([beg,end,depth,merge_memory,merge_beg,merge_end,func,cutoff,task_name,prio,task_res]
                                  (std::tuple<boost::asynchronous::expected<bool> >&& res)
                    {
                        try
                        {
                            bool sorted = std::get<0>(res).get();
                            if (sorted)
                            {
                                // reverse sorted
                                auto cont3 = boost::asynchronous::parallel_reverse<Iterator,Job>(beg,end,cutoff,task_name+"_reverse",prio);
                                cont3.on_done([task_res](std::tuple<boost::asynchronous::expected<void> >&& res)
                                {
                                    try
                                    {
                                        std::get<0>(res).get();
                                        task_res.set_value();
                                    }
                                    catch(std::exception& e)
                                    {
                                        task_res.set_exception(boost::copy_exception(e));
                                    }
                                });
                                return;
                            }
                            helper(beg,end,depth,merge_memory,merge_beg,merge_end,func,cutoff,task_name,prio,task_res);
                        }
                        catch(std::exception& e)
                        {
                            task_res.set_exception(boost::copy_exception(e));
                        }
                    });
                }
                catch(std::exception& e)
                {
                    task_res.set_exception(boost::copy_exception(e));
                }
            });
            return;
        }
        helper(beg_,end_,depth_,merge_memory_,merge_beg_,merge_end_,func_,cutoff_,this->get_name(),prio_,std::move(task_res));
    }
    Iterator beg_;
    Iterator end_;
    unsigned int depth_;
    Func func_;
    long cutoff_;
    std::size_t prio_;
    boost::shared_array<value_type> merge_memory_;
    value_type* merge_beg_;
    value_type* merge_end_;
};
}
// fast version for iterators => will return nothing
template <class Iterator,class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<void,Job>
parallel_sort(Iterator beg, Iterator end,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
              const std::string& task_name, std::size_t prio)
#else
              const std::string& task_name="", std::size_t prio=0)
#endif
{
    // create extra memory for merge
    auto size = std::distance(beg,end);
    boost::shared_array<typename std::iterator_traits<Iterator>::value_type> merge_memory (
                new typename std::iterator_traits<Iterator>::value_type[size]);
    return boost::asynchronous::top_level_callback_continuation_job<void,Job>
            (boost::asynchronous::detail::parallel_sort_fast_helper<Iterator,Func,Job,boost::asynchronous::std_sort>
              (beg,end,0,merge_memory,merge_memory.get(),merge_memory.get()+size,std::move(func),cutoff,task_name,prio));
}

template <class Iterator, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<void,Job>
parallel_stable_sort(Iterator beg, Iterator end,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
                     const std::string& task_name, std::size_t prio)
#else
                     const std::string& task_name="", std::size_t prio=0)
#endif
{
    // create extra memory for merge
    auto size = std::distance(beg,end);
    boost::shared_array<typename std::iterator_traits<Iterator>::value_type> merge_memory (
                new typename std::iterator_traits<Iterator>::value_type[size]);
    return boost::asynchronous::top_level_callback_continuation_job<void,Job>
            (boost::asynchronous::detail::parallel_sort_fast_helper<Iterator,Func,Job,boost::asynchronous::std_stable_sort>
               (beg,end,0,merge_memory,merge_memory.get(),merge_memory.get()+size,std::move(func),cutoff,task_name,prio));
}

#ifdef BOOST_ASYNCHRONOUS_USE_BOOST_SPREADSORT
template <class Iterator, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<void,Job>
parallel_spreadsort(Iterator beg, Iterator end,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
              const std::string& task_name, std::size_t prio)
#else
              const std::string& task_name="", std::size_t prio=0)
#endif
{
    // create extra memory for merge
    auto size = std::distance(beg,end);
    boost::shared_array<typename std::iterator_traits<Iterator>::value_type> merge_memory (
                new typename std::iterator_traits<Iterator>::value_type[size]);
    return boost::asynchronous::top_level_callback_continuation_job<void,Job>
            (boost::asynchronous::detail::parallel_sort_fast_helper<Iterator,Func,Job,boost::asynchronous::boost_spreadsort>
               (beg,end,0,merge_memory,merge_memory.get(),merge_memory.get()+size,std::move(func),cutoff,task_name,prio));
}
#endif



// fast version for ranges held only by reference => will return nothing (void)
template <class Range,class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
typename boost::disable_if<has_is_continuation_task<Range>,boost::asynchronous::detail::callback_continuation<void,Job> >::type
parallel_sort(Range& range, Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
              const std::string& task_name, std::size_t prio)
#else
              const std::string& task_name="", std::size_t prio=0)
#endif
{
    return boost::asynchronous::parallel_sort<decltype(boost::begin(range)),Func,Job>
            (boost::begin(range),boost::end(range),std::move(func),cutoff,task_name,prio);
}
template <class Range, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
typename boost::disable_if<has_is_continuation_task<Range>,boost::asynchronous::detail::callback_continuation<void,Job> >::type
parallel_stable_sort(Range& range,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
                     const std::string& task_name, std::size_t prio)
#else
                     const std::string& task_name="", std::size_t prio=0)
#endif
{
   return boost::asynchronous::parallel_stable_sort<decltype(boost::begin(range)),Func,Job>
           (boost::begin(range),boost::end(range),std::move(func),cutoff,task_name,prio);
}

#ifdef BOOST_ASYNCHRONOUS_USE_BOOST_SPREADSORT
template <class Range, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
typename boost::disable_if<has_is_continuation_task<Range>,boost::asynchronous::detail::callback_continuation<void,Job> >::type
parallel_spreadsort(Range& range,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
              const std::string& task_name, std::size_t prio)
#else
              const std::string& task_name="", std::size_t prio=0)
#endif
{
   return boost::asynchronous::parallel_spreadsort<decltype(boost::begin(range)),Func,Job>(boost::begin(range),boost::end(range),std::move(func),cutoff,task_name,prio);
}
#endif

// version for moved ranges => will return the range as continuation
template <class Range, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
typename boost::disable_if<boost::asynchronous::detail::is_serializable<Func>,boost::asynchronous::detail::callback_continuation<Range,Job> >::type
parallel_sort_move(Range&& range,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
                   const std::string& task_name, std::size_t prio)
#else
                   const std::string& task_name="", std::size_t prio=0)
#endif
{
    auto r = boost::make_shared<Range>(std::forward<Range>(range));
    auto beg = boost::begin(*r);
    auto end = boost::end(*r);
    return boost::asynchronous::top_level_callback_continuation_job<Range,Job>
            (boost::asynchronous::detail::parallel_sort_range_move_helper<boost::asynchronous::detail::callback_continuation<void,Job>,Range>
             (boost::asynchronous::parallel_sort<decltype(boost::begin(*r)),Func,Job>
                                (beg,end,std::move(func),cutoff,task_name,prio),r,task_name));
}
template <class Range, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
typename boost::disable_if<boost::asynchronous::detail::is_serializable<Func>,boost::asynchronous::detail::callback_continuation<Range,Job> >::type
parallel_stable_sort_move(Range&& range,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
                          const std::string& task_name, std::size_t prio)
#else
                          const std::string& task_name="", std::size_t prio=0)
#endif
{
    auto r = boost::make_shared<Range>(std::forward<Range>(range));
    auto beg = boost::begin(*r);
    auto end = boost::end(*r);
    return boost::asynchronous::top_level_callback_continuation_job<Range,Job>
            (boost::asynchronous::detail::parallel_sort_range_move_helper<boost::asynchronous::detail::callback_continuation<void,Job>,Range>
             (boost::asynchronous::parallel_stable_sort<decltype(boost::begin(*r)),Func,Job>
              (beg,end,std::move(func),cutoff,task_name,prio),r,task_name));
}
#ifdef BOOST_ASYNCHRONOUS_USE_BOOST_SPREADSORT
template <class Range, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
typename boost::disable_if<boost::asynchronous::detail::is_serializable<Func>,boost::asynchronous::detail::callback_continuation<Range,Job> >::type
parallel_spreadsort_move(Range&& range,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
                   const std::string& task_name, std::size_t prio)
#else
                   const std::string& task_name="", std::size_t prio=0)
#endif
{
    auto r = boost::make_shared<Range>(std::forward<Range>(range));
    auto beg = boost::begin(*r);
    auto end = boost::end(*r);
    return boost::asynchronous::top_level_callback_continuation_job<Range,Job>
            (boost::asynchronous::detail::parallel_sort_range_move_helper<boost::asynchronous::detail::callback_continuation<void,Job>,Range>
             (boost::asynchronous::parallel_spreadsort<decltype(boost::begin(*r)),Func,Job>(beg,end,std::move(func),cutoff,task_name,prio),r,task_name));
}
#endif

template <class Range, class Func, class Job, class Sort>
struct parallel_sort_range_move_helper_serializable
        : public boost::asynchronous::continuation_task<Range>
        , public boost::asynchronous::serializable_task
{
    //default ctor only when deserialized immediately after
    parallel_sort_range_move_helper_serializable():boost::asynchronous::serializable_task("parallel_sort_range_move_helper")
    {
    }
    template <class Iterator>
    parallel_sort_range_move_helper_serializable(boost::shared_ptr<Range> range,Iterator beg, Iterator end,Func func,unsigned int depth, long cutoff,
                        const std::string& task_name, std::size_t prio)
        : boost::asynchronous::continuation_task<Range>(task_name)
        , boost::asynchronous::serializable_task(func.get_task_name())
        , range_(range),func_(std::move(func))
        , cutoff_(cutoff),task_name_(task_name),prio_(prio),depth_(depth)
        , begin_(beg)
        , end_(end)
    {
    }
    static void helper(boost::shared_ptr<Range> full_range,decltype(boost::begin(*full_range)) beg, decltype(boost::begin(*full_range)) end,unsigned int depth,
                       Func func,long cutoff,const std::string& task_name, std::size_t prio,
                       boost::asynchronous::continuation_result<Range> task_res)
    {
        // advance up to cutoff
        auto it = boost::asynchronous::detail::find_cutoff(beg,cutoff,end);
        // if not at end, recurse, otherwise execute here
        if (it == end)
        {
            Sort()(beg,it,func);
            Range res (std::distance(beg,end));
            std::move(beg,it,boost::begin(res));
            task_res.set_value(std::move(res));
        }
        else
        {
            boost::asynchronous::create_callback_continuation_job<Job>(
                        // called when subtasks are done, set our result
                        [full_range,task_res,it,cutoff,func,task_name,prio](std::tuple<boost::asynchronous::expected<Range>,boost::asynchronous::expected<Range> > res)
                        {                            
                            try
                            {
                                boost::shared_ptr<Range> r1 =  boost::make_shared<Range>(std::move(std::get<0>(res).get()));
                                boost::shared_ptr<Range> r2 =  boost::make_shared<Range>(std::move(std::get<1>(res).get()));
                                boost::shared_ptr<Range> range = boost::make_shared<Range>(r1->size()+r2->size());
                                
                                // merge both sorted sub-ranges
                                auto on_done_fct = [full_range,task_res,r1,r2,range](std::tuple<boost::asynchronous::expected<void> >&& merge_res)
                                {
                                    try
                                    {
                                        // get to check that no exception
                                        std::get<0>(merge_res).get();
                                        task_res.set_value(std::move(*range));
                                    }
                                    catch(std::exception& e)
                                    {
                                        task_res.set_exception(boost::copy_exception(e));
                                    }
                                };                                
                                auto c = boost::asynchronous::parallel_merge<decltype(boost::begin(*r1)),decltype(boost::begin(*r1)),
                                                                             decltype(boost::begin(*range)),Func,Job>
                                        (boost::begin(*r1),boost::end(*r1),boost::begin(*r2),boost::end(*r2), boost::begin(*range),func,
                                         cutoff,task_name+"_merge",prio);
                                c.on_done(std::move(on_done_fct));
                            }
                            catch(std::exception& e)
                            {
                                task_res.set_exception(boost::copy_exception(e));
                            }
                        },
                        // recursive tasks
                        parallel_sort_range_move_helper_serializable<Range,Func,Job,Sort>(
                                    full_range,beg,it,
                                    func,depth+1,cutoff,task_name,prio),
                        parallel_sort_range_move_helper_serializable<Range,Func,Job,Sort>(
                                    full_range,it,end,
                                    func,depth+ 1,cutoff,task_name,prio)
            );
        }
    }

    void operator()()const
    {
        auto task_res = this->this_task_result();
        if (depth_ == 0)
        {
            // optimization for cases where we are already sorted
            auto range = range_;
            auto beg = begin_;
            auto end = end_;
            auto depth = depth_;
            auto func = func_;
            auto cutoff = cutoff_;
            auto task_name = this->get_name();
            auto cont = boost::asynchronous::parallel_is_sorted<decltype(boost::begin(*range_)),Func,Job>
                    (begin_,end_,func_,cutoff_,task_name+"_is_sorted",prio_);
            auto prio = prio_;
            cont.on_done([range,beg,end,depth,func,cutoff,task_name,prio,task_res]
                         (std::tuple<boost::asynchronous::expected<bool> >&& res)
            {
                try
                {
                    bool sorted = std::get<0>(res).get();
                    if (sorted)
                    {
                        task_res.set_value(std::move(*range));
                        return;
                    }
                    auto cont2 = boost::asynchronous::parallel_is_reverse_sorted<decltype(boost::begin(*range_)),Func,Job>
                            (beg,end,func,cutoff,task_name+"_is_reverse_sorted",prio);
                    cont2.on_done([range,beg,end,depth,func,cutoff,task_name,prio,task_res]
                                  (std::tuple<boost::asynchronous::expected<bool> >&& res)
                    {
                        try
                        {
                            bool sorted = std::get<0>(res).get();
                            if (sorted)
                            {
                                // reverse sorted
                                auto cont3 = boost::asynchronous::parallel_reverse<decltype(boost::begin(*range_)),Job>
                                        (beg,end,cutoff,task_name+"_reverse",prio);
                                cont3.on_done([range,task_res](std::tuple<boost::asynchronous::expected<void> >&& res)
                                {
                                    try
                                    {
                                        std::get<0>(res).get();
                                        task_res.set_value(std::move(*range));
                                    }
                                    catch(std::exception& e)
                                    {
                                        task_res.set_exception(boost::copy_exception(e));
                                    }
                                });
                                return;
                            }
                            helper(range,beg,end,depth,std::move(func),cutoff,task_name,prio,task_res);
                        }
                        catch(std::exception& e)
                        {
                            task_res.set_exception(boost::copy_exception(e));
                        }
                    });
                }
                catch(std::exception& e)
                {
                    task_res.set_exception(boost::copy_exception(e));
                }
            });
            return;
        }
        helper(range_,begin_,end_,depth_,std::move(func_),cutoff_,task_name_,prio_,task_res);
    }    

    template <class Archive>
    void save(Archive & ar, const unsigned int /*version*/)const
    {
        // only part range
        // TODO avoid copying
        auto r = std::move(boost::copy_range< Range>(boost::make_iterator_range(begin_,end_)));
        ar & r;
        ar & func_;
        ar & cutoff_;
        ar & task_name_;
        ar & prio_;
        ar & depth_;
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
        ar & depth_;
        begin_ = boost::begin(*range_);
        end_ = boost::end(*range_);
    }
    BOOST_SERIALIZATION_SPLIT_MEMBER()

    boost::shared_ptr<Range> range_;
    Func func_;
    long cutoff_;
    std::string task_name_;
    std::size_t prio_;
    unsigned int depth_;
    decltype(boost::begin(*range_)) begin_;
    decltype(boost::end(*range_)) end_;
};

template <class Range, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
typename boost::enable_if<boost::asynchronous::detail::is_serializable<Func>,boost::asynchronous::detail::callback_continuation<Range,Job> >::type
parallel_sort_move(Range&& range,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
                   const std::string& task_name, std::size_t prio)
#else
                   const std::string& task_name="", std::size_t prio=0)
#endif
{
    auto r = boost::make_shared<Range>(std::forward<Range>(range));
    auto beg = boost::begin(*r);
    auto end = boost::end(*r);
    return boost::asynchronous::top_level_callback_continuation_job<Range,Job>
            (boost::asynchronous::parallel_sort_range_move_helper_serializable<Range,Func,Job,boost::asynchronous::std_sort>
                (r,beg,end,std::move(func),0,cutoff,task_name,prio));
}
template <class Range, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
typename boost::enable_if<boost::asynchronous::detail::is_serializable<Func>,boost::asynchronous::detail::callback_continuation<Range,Job> >::type
parallel_stable_sort_move(Range&& range,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
                          const std::string& task_name, std::size_t prio)
#else
                          const std::string& task_name="", std::size_t prio=0)
#endif
{
    auto r = boost::make_shared<Range>(std::forward<Range>(range));
    auto beg = boost::begin(*r);
    auto end = boost::end(*r);
    return boost::asynchronous::top_level_callback_continuation_job<Range,Job>
            (boost::asynchronous::parallel_sort_range_move_helper_serializable<Range,Func,Job,boost::asynchronous::std_stable_sort>
                (r,beg,end,std::move(func),0,cutoff,task_name,prio));
}
#ifdef BOOST_ASYNCHRONOUS_USE_BOOST_SPREADSORT
template <class Range, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
typename boost::enable_if<boost::asynchronous::detail::is_serializable<Func>,boost::asynchronous::detail::callback_continuation<Range,Job> >::type
parallel_spreadsort_move(Range&& range,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
                   const std::string& task_name, std::size_t prio)
#else
                   const std::string& task_name="", std::size_t prio=0)
#endif
{
    auto r = boost::make_shared<Range>(std::forward<Range>(range));
    auto beg = boost::begin(*r);
    auto end = boost::end(*r);
    return boost::asynchronous::top_level_callback_continuation_job<Range,Job>
            (boost::asynchronous::parallel_sort_range_move_helper_serializable<Range,Func,Job,boost::asynchronous::boost_spreadsort>
                (r,beg,end,std::move(func),0,cutoff,task_name,prio));
}
#endif

// version for ranges given as continuation => will return the range as continuation
namespace detail
{
// adapter to non-callback continuations
template <class Continuation, class Func, class Job,class Enable=void>
struct parallel_sort_continuation_range_helper: public boost::asynchronous::continuation_task<typename Continuation::return_type>
{
    parallel_sort_continuation_range_helper(Continuation const& c,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        :boost::asynchronous::continuation_task<typename Continuation::return_type>(task_name)
        ,cont_(c),func_(std::move(func)),cutoff_(cutoff),prio_(prio)
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<typename Continuation::return_type> task_res = this->this_task_result();
        auto func(std::move(func_));
        auto cutoff = cutoff_;
        auto task_name = this->get_name();
        auto prio = prio_;
        cont_.on_done([task_res,func,cutoff,task_name,prio](std::tuple<boost::future<typename Continuation::return_type> >&& continuation_res)
        {
            try
            {
                auto new_continuation = boost::asynchronous::parallel_sort_move<typename Continuation::return_type, Func, Job>(std::move(std::get<0>(continuation_res).get()),func,cutoff,task_name,prio);
                new_continuation.on_done([task_res](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& new_continuation_res)
                {
                    task_res.set_value(std::move(std::get<0>(new_continuation_res).get()));
                });
            }
            catch(std::exception& e)
            {
                task_res.set_exception(boost::copy_exception(e));
            }
        }
        );
        boost::asynchronous::any_continuation ac(std::move(cont_));
        boost::asynchronous::get_continuations().emplace_front(std::move(ac));
    }
    Continuation cont_;
    Func func_;
    long cutoff_;
    std::size_t prio_;
};
// Continuation is a callback continuation
template <class Continuation, class Func, class Job>
struct parallel_sort_continuation_range_helper<Continuation,Func,Job,typename ::boost::enable_if< has_is_callback_continuation_task<Continuation> >::type>:
        public boost::asynchronous::continuation_task<typename Continuation::return_type>
{
    parallel_sort_continuation_range_helper(Continuation const& c,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        :boost::asynchronous::continuation_task<typename Continuation::return_type>(task_name)
        ,cont_(c),func_(std::move(func)),cutoff_(cutoff),prio_(prio)
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<typename Continuation::return_type> task_res = this->this_task_result();
        auto func(std::move(func_));
        auto cutoff = cutoff_;
        auto task_name = this->get_name();
        auto prio = prio_;
        cont_.on_done([task_res,func,cutoff,task_name,prio](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& continuation_res)
        {
            try
            {
                auto new_continuation = boost::asynchronous::parallel_sort_move<typename Continuation::return_type, Func, Job>(std::move(std::get<0>(continuation_res).get()),func,cutoff,task_name,prio);
                new_continuation.on_done([task_res](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& new_continuation_res)
                {
                    task_res.set_value(std::move(std::get<0>(new_continuation_res).get()));
                });
            }
            catch(std::exception& e)
            {
                task_res.set_exception(boost::copy_exception(e));
            }
        }
        );
    }
    Continuation cont_;
    Func func_;
    long cutoff_;
    std::size_t prio_;
};

// adapter to non-callback continuations
template <class Continuation, class Func, class Job,class Enable=void>
struct parallel_stable_sort_continuation_range_helper: public boost::asynchronous::continuation_task<typename Continuation::return_type>
{
    parallel_stable_sort_continuation_range_helper(Continuation const& c,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        :boost::asynchronous::continuation_task<typename Continuation::return_type>(task_name)
        ,cont_(c),func_(std::move(func)),cutoff_(cutoff),prio_(prio)
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<typename Continuation::return_type> task_res = this->this_task_result();
        auto func(std::move(func_));
        auto cutoff = cutoff_;
        auto task_name = this->get_name();
        auto prio = prio_;
        cont_.on_done([task_res,func,cutoff,task_name,prio](std::tuple<boost::future<typename Continuation::return_type> >&& continuation_res)
        {
            try
            {
                auto new_continuation = boost::asynchronous::parallel_stable_sort_move<typename Continuation::return_type,Func,Job>
                        (std::move(std::get<0>(continuation_res).get()),func,cutoff,task_name,prio);
                new_continuation.on_done([task_res](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& new_continuation_res)
                {
                    task_res.set_value(std::move(std::get<0>(new_continuation_res).get()));
                });
            }
            catch(std::exception& e)
            {
                task_res.set_exception(boost::copy_exception(e));
            }
        }
        );
        boost::asynchronous::any_continuation ac(std::move(cont_));
        boost::asynchronous::get_continuations().emplace_front(std::move(ac));
    }
    Continuation cont_;
    Func func_;
    long cutoff_;
    std::size_t prio_;
};
// Continuation is a callback continuation
template <class Continuation, class Func, class Job>
struct parallel_stable_sort_continuation_range_helper<Continuation,Func,Job,typename ::boost::enable_if< has_is_callback_continuation_task<Continuation> >::type>
        : public boost::asynchronous::continuation_task<typename Continuation::return_type>
{
    parallel_stable_sort_continuation_range_helper(Continuation const& c,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        :boost::asynchronous::continuation_task<typename Continuation::return_type>(task_name)
        ,cont_(c),func_(std::move(func)),cutoff_(cutoff),prio_(prio)
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<typename Continuation::return_type> task_res = this->this_task_result();
        auto func(std::move(func_));
        auto cutoff = cutoff_;
        auto task_name = this->get_name();
        auto prio = prio_;
        cont_.on_done([task_res,func,cutoff,task_name,prio](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& continuation_res)
        {
            try
            {
                auto new_continuation = boost::asynchronous::parallel_stable_sort_move<typename Continuation::return_type,Func,Job>
                        (std::move(std::get<0>(continuation_res).get()),func,cutoff,task_name,prio);
                new_continuation.on_done([task_res](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& new_continuation_res)
                {
                    task_res.set_value(std::move(std::get<0>(new_continuation_res).get()));
                });
            }
            catch(std::exception& e)
            {
                task_res.set_exception(boost::copy_exception(e));
            }
        }
        );
    }
    Continuation cont_;
    Func func_;
    long cutoff_;
    std::size_t prio_;
};
#ifdef BOOST_ASYNCHRONOUS_USE_BOOST_SPREADSORT
// adapter to non-callback continuations
template <class Continuation, class Func, class Job,class Enable=void>
struct parallel_spreadsort_continuation_range_helper: public boost::asynchronous::continuation_task<typename Continuation::return_type>
{
    parallel_spreadsort_continuation_range_helper(Continuation const& c,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        :boost::asynchronous::continuation_task<typename Continuation::return_type>(task_name)
        ,cont_(c),func_(std::move(func)),cutoff_(cutoff),prio_(prio)
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<typename Continuation::return_type> task_res = this->this_task_result();
        auto func(std::move(func_));
        auto cutoff = cutoff_;
        auto task_name = this->get_name();
        auto prio = prio_;
        cont_.on_done([task_res,func,cutoff,task_name,prio](std::tuple<boost::future<typename Continuation::return_type> >&& continuation_res)
        {
            try
            {
                auto new_continuation = boost::asynchronous::parallel_spreadsort_move<typename Continuation::return_type,Func,Job>
                        (std::move(std::get<0>(continuation_res).get()),func,cutoff,task_name,prio);
                new_continuation.on_done([task_res](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& new_continuation_res)
                {
                    task_res.set_value(std::move(std::get<0>(new_continuation_res).get()));
                });
            }
            catch(std::exception& e)
            {
                task_res.set_exception(boost::copy_exception(e));
            }
        }
        );
        boost::asynchronous::any_continuation ac(std::move(cont_));
        boost::asynchronous::get_continuations().emplace_front(std::move(ac));
    }
    Continuation cont_;
    Func func_;
    long cutoff_;
    std::size_t prio_;
};
// Continuation is a callback continuation
template <class Continuation, class Func, class Job>
struct parallel_spreadsort_continuation_range_helper<Continuation,Func,Job,typename ::boost::enable_if< has_is_callback_continuation_task<Continuation> >::type>
        : public boost::asynchronous::continuation_task<typename Continuation::return_type>
{
    parallel_spreadsort_continuation_range_helper(Continuation const& c,Func func,long cutoff,
                        const std::string& task_name, std::size_t prio)
        :boost::asynchronous::continuation_task<typename Continuation::return_type>(task_name)
        ,cont_(c),func_(std::move(func)),cutoff_(cutoff),prio_(prio)
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<typename Continuation::return_type> task_res = this->this_task_result();
        auto func(std::move(func_));
        auto cutoff = cutoff_;
        auto task_name = this->get_name();
        auto prio = prio_;
        cont_.on_done([task_res,func,cutoff,task_name,prio](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& continuation_res)
        {
            try
            {
                auto new_continuation = boost::asynchronous::parallel_spreadsort_move<typename Continuation::return_type,Func,Job>
                        (std::move(std::get<0>(continuation_res).get()),func,cutoff,task_name,prio);
                new_continuation.on_done([task_res](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& new_continuation_res)
                {
                    task_res.set_value(std::move(std::get<0>(new_continuation_res).get()));
                });
            }
            catch(std::exception& e)
            {
                task_res.set_exception(boost::copy_exception(e));
            }
        }
        );
    }
    Continuation cont_;
    Func func_;
    long cutoff_;
    std::size_t prio_;
};
#endif
}
template <class Range, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
typename boost::enable_if<has_is_continuation_task<Range>,boost::asynchronous::detail::callback_continuation<typename Range::return_type,Job> >::type
parallel_sort(Range range,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
              const std::string& task_name, std::size_t prio)
#else
              const std::string& task_name="", std::size_t prio=0)
#endif
{
    return boost::asynchronous::top_level_callback_continuation_job<typename Range::return_type,Job>
            (boost::asynchronous::detail::parallel_sort_continuation_range_helper<Range,Func,Job>(range,std::move(func),cutoff,task_name,prio));
}
template <class Range, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
typename boost::enable_if<has_is_continuation_task<Range>,boost::asynchronous::detail::callback_continuation<typename Range::return_type,Job> >::type
parallel_stable_sort(Range range,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
                     const std::string& task_name, std::size_t prio)
#else
                     const std::string& task_name="", std::size_t prio=0)
#endif
{
    return boost::asynchronous::top_level_callback_continuation_job<typename Range::return_type,Job>
            (boost::asynchronous::detail::parallel_stable_sort_continuation_range_helper<Range,Func,Job>(range,std::move(func),cutoff,task_name,prio));
}
#ifdef BOOST_ASYNCHRONOUS_USE_BOOST_SPREADSORT
template <class Range, class Func, class Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
typename boost::enable_if<has_is_continuation_task<Range>,boost::asynchronous::detail::callback_continuation<typename Range::return_type,Job> >::type
parallel_spreadsort(Range range,Func func,long cutoff,
#ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
                     const std::string& task_name, std::size_t prio)
#else
                     const std::string& task_name="", std::size_t prio=0)
#endif
{
    return boost::asynchronous::top_level_callback_continuation_job<typename Range::return_type,Job>
            (boost::asynchronous::detail::parallel_spreadsort_continuation_range_helper<Range,Func,Job>(range,std::move(func),cutoff,task_name,prio));
}
#endif

}}
#endif // BOOST_ASYNCHRONOUS_PARALLEL_SORT_HPP
