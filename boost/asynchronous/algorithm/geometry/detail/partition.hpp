// Boost.Geometry (aka GGL, Generic Geometry Library)

// Copyright (c) 2011-2014 Barend Gehrels, Amsterdam, the Netherlands.

// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_ASYNCHRONOUS_GEOMETRY_ALGORITHMS_DETAIL_PARTITION_HPP
#define BOOST_ASYNCHRONOUS_GEOMETRY_ALGORITHMS_DETAIL_PARTITION_HPP

#include <vector>
#include <boost/assert.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/geometry/algorithms/assign.hpp>
#include <boost/geometry/core/coordinate_type.hpp>
#include <boost/geometry/algorithms/detail/partition.hpp>

#include <boost/asynchronous/algorithm/detail/safe_advance.hpp>
#include <boost/asynchronous/callable_any.hpp>
#include <boost/asynchronous/detail/continuation_impl.hpp>
#include <boost/asynchronous/continuation_task.hpp>

namespace boost { namespace geometry
{

namespace detail { namespace partition
{

/*typedef std::vector<std::size_t> index_vector_type;

template <int Dimension, typename Box>
void divide_box(Box const& box, Box& lower_box, Box& upper_box)
{
    typedef typename coordinate_type<Box>::type ctype;

    // Divide input box into two parts, e.g. left/right
    ctype two = 2;
    ctype mid = (geometry::get<min_corner, Dimension>(box)
            + geometry::get<max_corner, Dimension>(box)) / two;

    lower_box = box;
    upper_box = box;
    geometry::set<max_corner, Dimension>(lower_box, mid);
    geometry::set<min_corner, Dimension>(upper_box, mid);
}

// Divide collection into three subsets: lower, upper and oversized
// (not-fitting)
// (lower == left or bottom, upper == right or top)
template <typename OverlapsPolicy, typename InputCollection, typename Box>
inline void divide_into_subsets(Box const& lower_box,
        Box const& upper_box,
        InputCollection const& collection,
        index_vector_type const& input,
        index_vector_type& lower,
        index_vector_type& upper,
        index_vector_type& exceeding)
{
    typedef boost::range_iterator
        <
            index_vector_type const
        >::type index_iterator_type;

    for(index_iterator_type it = boost::begin(input);
        it != boost::end(input);
        ++it)
    {
        bool const lower_overlapping = OverlapsPolicy::apply(lower_box,
                    collection[*it]);
        bool const upper_overlapping = OverlapsPolicy::apply(upper_box,
                    collection[*it]);

        if (lower_overlapping && upper_overlapping)
        {
            exceeding.push_back(*it);
        }
        else if (lower_overlapping)
        {
            lower.push_back(*it);
        }
        else if (upper_overlapping)
        {
            upper.push_back(*it);
        }
        else
        {
            // Is nowhere! Should not occur!
            BOOST_ASSERT(false);
        }
    }
}

// Match collection with itself
template <typename InputCollection, typename Policy>
inline void handle_one(InputCollection const& collection,
        index_vector_type const& input,
        Policy& policy)
{
    typedef boost::range_iterator<index_vector_type const>::type
                index_iterator_type;
    // Quadratic behaviour at lowest level (lowest quad, or all exceeding)
    for(index_iterator_type it1 = boost::begin(input);
        it1 != boost::end(input);
        ++it1)
    {
        index_iterator_type it2 = it1;
        for(++it2; it2 != boost::end(input); ++it2)
        {
            policy.apply(collection[*it1], collection[*it2]);
        }
    }
}

// Match collection 1 with collection 2
template
<
    typename InputCollection1,
    typename InputCollection2,
    typename Policy
>
inline void handle_two(
        InputCollection1 const& collection1, index_vector_type const& input1,
        InputCollection2 const& collection2, index_vector_type const& input2,
        Policy& policy)
{
    typedef boost::range_iterator
        <
            index_vector_type const
        >::type index_iterator_type;

    for(index_iterator_type it1 = boost::begin(input1);
        it1 != boost::end(input1);
        ++it1)
    {
        for(index_iterator_type it2 = boost::begin(input2);
            it2 != boost::end(input2);
            ++it2)
        {
            policy.apply(collection1[*it1], collection2[*it2]);
        }
    }
}
*/
template
<
    int Dimension,
    typename Box,
    typename OverlapsPolicy,
    typename VisitBoxPolicy
>
class parallel_partition_one_collection
{
    typedef std::vector<std::size_t> index_vector_type;
    typedef typename coordinate_type<Box>::type ctype;
    typedef parallel_partition_one_collection
            <
                1 - Dimension,
                Box,
                OverlapsPolicy,
                VisitBoxPolicy
            > sub_divide;

    template <typename InputCollection, typename Policy>
    static inline void next_level(Box const& box,
            InputCollection const& collection,
            index_vector_type const& input,
            int level, std::size_t min_elements,
            Policy& policy, VisitBoxPolicy& box_policy)
    {
        if (boost::size(input) > 0)
        {
            if (std::size_t(boost::size(input)) > min_elements && level < 100)
            {
                sub_divide::apply(box, collection, input, level + 1,
                            min_elements, policy, box_policy);
            }
            else
            {
                handle_one(collection, input, policy);
            }
        }
    }

public :
    template <typename InputCollection, typename Policy>
    static inline void apply(Box const& box,
            InputCollection const& collection,
            index_vector_type const& input,
            int level,
            std::size_t min_elements,
            Policy& policy, VisitBoxPolicy& box_policy)
    {
        box_policy.apply(box, level);

        Box lower_box, upper_box;
        divide_box<Dimension>(box, lower_box, upper_box);

        index_vector_type lower, upper, exceeding;
        divide_into_subsets<OverlapsPolicy>(lower_box, upper_box, collection,
                    input, lower, upper, exceeding);

        if (boost::size(exceeding) > 0)
        {
            // All what is not fitting a partition should be combined
            // with each other, and with all which is fitting.
            handle_one(collection, exceeding, policy);
            handle_two(collection, exceeding, collection, lower, policy);
            handle_two(collection, exceeding, collection, upper, policy);
        }

        // Recursively call operation both parts
        next_level(lower_box, collection, lower, level, min_elements,
                        policy, box_policy);
        next_level(upper_box, collection, upper, level, min_elements,
                        policy, box_policy);
    }
};
/*
template
<
    typename InputCollection1,
    typename InputCollection2,
    typename VisitBoxPolicy,
    typename IndexVectorType,
    typename Policy
>
void next_level_(Box const& box,
        InputCollection1 const& collection1,
        IndexVectorType const& input1,
        InputCollection2 const& collection2,
        IndexVectorType const& input2,
        int level, std::size_t min_elements,
        Policy& policy, VisitBoxPolicy& box_policy)
{
    if (boost::size(input1) > 0 && boost::size(input2) > 0)
    {
        if (std::size_t(boost::size(input1)) > min_elements
            && std::size_t(boost::size(input2)) > min_elements
            && level < 100)
        {
            sub_divide::apply(box, collection1, input1, collection2,
                            input2, level + 1, min_elements,
                            policy, box_policy);
        }
        else
        {
            box_policy.apply(box, level + 1);
            handle_two(collection1, input1, collection2, input2, policy);
        }
    }
}
*/
template
<
    int Dimension,
    typename Box,
    typename OverlapsPolicy1,
    typename OverlapsPolicy2,
    typename VisitBoxPolicy,
    typename Job
>
class parallel_partition_two_collections
{
    typedef parallel_partition_two_collections this_type;

    typedef std::vector<std::size_t> index_vector_type;
    typedef typename coordinate_type<Box>::type ctype;
    typedef parallel_partition_two_collections
            <
                1 - Dimension,
                Box,
                OverlapsPolicy1,
                OverlapsPolicy2,
                VisitBoxPolicy,
                Job
            > sub_divide;

    typedef boost::range_iterator
        <
            index_vector_type const
        >::type index_iterator_type;

    typedef std::tuple<index_vector_type,index_vector_type,index_vector_type> subsets_task_result_type;

    template<int, class,class,class,class,class> friend class parallel_partition_two_collections;

    template
    <
        typename InputCollection,
        typename OverlapsPolicy
    >
    struct divide_into_subsets_task : public boost::asynchronous::continuation_task<subsets_task_result_type>
    {        
        divide_into_subsets_task(Box const& lower_box,
                                 Box const& upper_box,
                                 boost::shared_ptr<InputCollection> collection,
                                 boost::shared_ptr<index_vector_type> input,
                                 index_iterator_type beg,index_iterator_type end,
                                 long cutoff,const std::string& task_name, std::size_t prio)
        : boost::asynchronous::continuation_task<subsets_task_result_type>(task_name)
        , lower_box_(lower_box)
        , upper_box_(upper_box)
        , collection_(collection)
        , input_(input)
        , beg_(beg)
        , end_(end)
        , cutoff_(cutoff)
        , prio_(prio)
        {}
        void operator()()const
        {
            //std::cout << "divide_into_subsets_task. Elements: " << input_->size() << ": " << std::boolalpha << (input_->size() < 400000) << std::endl;
            // the result of this task, will be either set directly if < cutoff, otherwise when taks is ready
            boost::asynchronous::continuation_result<subsets_task_result_type> task_res = this->this_task_result();
            // advance up to cutoff            
            index_iterator_type it = boost::asynchronous::detail::find_cutoff(beg_,cutoff_,end_);
            // if not at end, recurse, otherwise execute here
            if (it == end_)
            {
                index_vector_type lower;
                index_vector_type upper;
                index_vector_type exceeding;

                divide_into_subsets<OverlapsPolicy>(lower_box_, upper_box_, *collection_,*input_, lower, upper, exceeding);
                task_res.set_value(std::move(std::make_tuple(std::move(lower),std::move(upper),std::move(exceeding))));
            }
            else
            {
                boost::asynchronous::create_callback_continuation_job<Job>(
                            // called when subtasks are done, set our result
                            [task_res](std::tuple<boost::asynchronous::expected<subsets_task_result_type>,
                                                  boost::asynchronous::expected<subsets_task_result_type> > res)
                            {
                                try
                                {
                                    // get results
                                    subsets_task_result_type sub1 (std::move(std::get<0>(res).get()));
                                    index_vector_type lower1(std::move(std::get<0>(sub1)));
                                    index_vector_type upper1(std::move(std::get<1>(sub1)));
                                    index_vector_type exceeding1(std::move(std::get<2>(sub1)));

                                    subsets_task_result_type sub2 (std::move(std::get<1>(res).get()));
                                    index_vector_type lower2(std::move(std::get<0>(sub2)));
                                    index_vector_type upper2(std::move(std::get<1>(sub2)));
                                    index_vector_type exceeding2(std::move(std::get<2>(sub2)));

                                    // merge results
                                    //std::cout << "divide_into_subsets_task. Reserve for lower1: " << lower1.size()+lower2.size() << std::endl;
                                    //std::cout << "divide_into_subsets_task. Reserve for upper1: " << upper1.size()+upper2.size() << std::endl;
                                    //std::cout << "divide_into_subsets_task. Reserve for exceeding1: " << exceeding1.size()+exceeding2.size() << std::endl;

                                    lower1.reserve(lower1.size()+lower2.size());
                                    upper1.reserve(upper1.size()+upper2.size());
                                    exceeding1.reserve(exceeding1.size()+exceeding2.size());

                                    //std::cout << "divide_into_subsets_task. After reserve" << std::endl;

                                    std::move(lower2.begin(),lower2.end(),std::back_inserter(lower1));
                                    std::move(upper2.begin(),upper2.end(),std::back_inserter(upper1));
                                    std::move(exceeding2.begin(),exceeding2.end(),std::back_inserter(exceeding1));

                                    //std::cout << "divide_into_subsets_task. After move size lower1: " << lower1.size()<< std::endl;
                                    //std::cout << "divide_into_subsets_task. After move size upper1: " << upper1.size()<< std::endl;
                                    //std::cout << "divide_into_subsets_task. After move size exceeding1: " << exceeding1.size() << std::endl;


                                    task_res.set_value(std::make_tuple(std::move(lower1),std::move(upper1),std::move(exceeding1)));

                                    //std::cout << "divide_into_subsets_task. After set_value" << std::endl;
                                }
                                catch(std::exception& e)
                                {
                                    std::cout << "divide_into_subsets_task. Exception" << std::endl;
                                    task_res.set_exception(boost::copy_exception(e));
                                }
                            },
                            // recursive tasks
                            divide_into_subsets_task<InputCollection,OverlapsPolicy>(lower_box_,upper_box_,collection_,input_,beg_,it,
                                                                                     cutoff_,this->get_name(),prio_),
                            divide_into_subsets_task<InputCollection,OverlapsPolicy>(lower_box_,upper_box_,collection_,input_,it,end_,
                                                                                     cutoff_,this->get_name(),prio_)
                   );
            }

        }
        Box lower_box_;
        Box upper_box_;
        boost::shared_ptr<InputCollection> collection_;
        boost::shared_ptr<index_vector_type> input_;
        index_iterator_type beg_;
        index_iterator_type end_;
        long cutoff_;
        std::size_t prio_;
    };


    template
    <
        typename InputCollection1,
        typename InputCollection2,
        typename Policy
    >
    static inline void next_level(Box const& box,
            InputCollection1 const& collection1,
            index_vector_type const& input1,
            InputCollection2 const& collection2,
            index_vector_type const& input2,
            int level, std::size_t min_elements,
            Policy& policy, VisitBoxPolicy& box_policy)
    {
        if (boost::size(input1) > 0 && boost::size(input2) > 0)
        {
            if (std::size_t(boost::size(input1)) > min_elements
                && std::size_t(boost::size(input2)) > min_elements
                && level < 100)
            {
                sub_divide::apply(box, collection1, input1, collection2,
                                input2, level + 1, min_elements,
                                policy, box_policy);
            }
            else
            {
                box_policy.apply(box, level + 1);
                handle_two(collection1, input1, collection2, input2, policy);
            }
        }
    }
    template
    <
        typename InputCollection1,
        typename InputCollection2,
        typename Policy,
        typename Owner
    >
    struct dummy_partition_task : public boost::asynchronous::continuation_task<Policy>
    {
        dummy_partition_task(Box const& box,
                                boost::shared_ptr<InputCollection1> collection1,boost::shared_ptr<index_vector_type> input1,
                                boost::shared_ptr<InputCollection2> collection2,boost::shared_ptr<index_vector_type> input2,
                                int level,
                                std::size_t min_elements,
                                boost::shared_ptr<Policy> policy, VisitBoxPolicy& box_policy,
                                long cutoff,const std::string& task_name, std::size_t prio)
            : boost::asynchronous::continuation_task<Policy>(task_name)
            // TODO move
            , box_(box)
            , collection1_(collection1), collection2_(collection2)
            , input1_(input1), input2_(input2)
            , level_(level)
            , min_elements_(min_elements),policy_(policy),box_policy_(box_policy)
            , cutoff_(cutoff), prio_(prio)
        {}

        void operator()()
        {
            // the result of this task, will be either set when both subtasks are ready
            // subtasks will be ready when their subtasks are ready, etc.
            boost::asynchronous::continuation_result<Policy> task_res = this->this_task_result();
            box_policy_.apply(box_, level_);

            Box lower_box, upper_box;
            divide_box<Dimension>(box_, lower_box, upper_box);

            index_vector_type lower1, upper1, exceeding1;
            index_vector_type lower2, upper2, exceeding2;
            divide_into_subsets<OverlapsPolicy1>(lower_box, upper_box, *collection1_,
                        *input1_, lower1, upper1, exceeding1);
            divide_into_subsets<OverlapsPolicy2>(lower_box, upper_box, *collection2_,
                        *input2_, lower2, upper2, exceeding2);

            if (boost::size(exceeding1) > 0)
            {
                // All exceeding from 1 with 2:
                handle_two(*collection1_, exceeding1, *collection2_, exceeding2,
                            *policy_);

                // All exceeding from 1 with lower and upper of 2:
                handle_two(*collection1_, exceeding1, *collection2_, lower2, *policy_);
                handle_two(*collection1_, exceeding1, *collection2_, upper2, *policy_);
            }
            if (boost::size(exceeding2) > 0)
            {
                // All exceeding from 2 with lower and upper of 1:
                handle_two(*collection1_, lower1, *collection2_, exceeding2, *policy_);
                handle_two(*collection1_, upper1, *collection2_, exceeding2, *policy_);
            }

            Owner::next_level(lower_box, *collection1_, lower1, *collection2_, lower2, level_,
                            min_elements_, *policy_, box_policy_);
            Owner::next_level(upper_box, *collection1_, upper1, *collection2_, upper2, level_,
                            min_elements_, *policy_, box_policy_);

            task_res.set_value(std::move(*policy_));
        }

        Box box_;
        boost::shared_ptr<InputCollection1> collection1_;
        boost::shared_ptr<InputCollection2> collection2_;
        boost::shared_ptr<index_vector_type> input1_;
        boost::shared_ptr<index_vector_type> input2_;
        int level_;
        std::size_t min_elements_;
        boost::shared_ptr<Policy> policy_;
        VisitBoxPolicy box_policy_;
        long cutoff_;
        std::size_t prio_;
    };
    template
    <
        typename InputCollection1,
        typename InputCollection2,
        typename Policy,
        typename Owner
    >
    struct parallel_partition_task : public boost::asynchronous::continuation_task<Policy>
    {
        parallel_partition_task(Box const& box,
                                boost::shared_ptr<InputCollection1> collection1,boost::shared_ptr<index_vector_type> input1,
                                boost::shared_ptr<InputCollection2> collection2,boost::shared_ptr<index_vector_type> input2,
                                int level,
                                std::size_t min_elements,
                                boost::shared_ptr<Policy> policy, VisitBoxPolicy& box_policy,
                                long cutoff,const std::string& task_name, std::size_t prio)
            : boost::asynchronous::continuation_task<Policy>(task_name)
            // TODO move
            , box_(box)
            , collection1_(collection1), collection2_(collection2)
            , input1_(input1), input2_(input2)
            , level_(level)
            , min_elements_(min_elements),policy_(policy),box_policy_(box_policy)
            , cutoff_(cutoff), prio_(prio)
        {}

        void operator()()const
        {
            box_policy_.apply(box_, level_);

            Box lower_box, upper_box;
            divide_box<Dimension>(box_, lower_box, upper_box);
            // the result of this task, will be either set when both subtasks are ready
            // subtasks will be ready when their subtasks are ready, etc.
            boost::asynchronous::continuation_result<Policy> task_res = this->this_task_result();

            auto policy = policy_;
            auto collection1 = collection1_;
            auto collection2 = collection2_;
            auto level = level_;
            auto box_policy = box_policy_;
            auto min_elements = min_elements_;

            //std::cout << "parallel_partition_task. Elements 1: " << input1_->size() << std::endl;
            //std::cout << "parallel_partition_task. Elements 2: " << input2_->size() << std::endl;

            boost::asynchronous::create_callback_continuation_job<Job>(
                        // called when subtasks are done, set our result
                        [task_res,policy,collection1,collection2,level,box_policy,lower_box, upper_box,min_elements]
                        (std::tuple<boost::asynchronous::expected<subsets_task_result_type>,
                                    boost::asynchronous::expected<subsets_task_result_type> > res) mutable
                        {
                            try
                            {
                                // get results
                                subsets_task_result_type sub1 (std::move(std::get<0>(res).get()));
                                index_vector_type lower1(std::move(std::get<0>(sub1)));
                                index_vector_type upper1(std::move(std::get<1>(sub1)));
                                index_vector_type exceeding1(std::move(std::get<2>(sub1)));

                                subsets_task_result_type sub2 (std::move(std::get<1>(res).get()));
                                index_vector_type lower2(std::move(std::get<0>(sub2)));
                                index_vector_type upper2(std::move(std::get<1>(sub2)));
                                index_vector_type exceeding2(std::move(std::get<2>(sub2)));

                                //std::cout << "parallel_partition_task. size lower1: " << lower1.size()<< std::endl;
                                //std::cout << "parallel_partition_task. size upper1: " << upper1.size()<< std::endl;
                                //std::cout << "parallel_partition_task. size exceeding1: " << exceeding1.size() << std::endl;
                                //std::cout << "parallel_partition_task. size lower2: " << lower2.size()<< std::endl;
                                //std::cout << "parallel_partition_task. size upper2: " << upper2.size()<< std::endl;
                                //std::cout << "parallel_partition_task. size exceeding2: " << exceeding2.size() << std::endl;


                                if (boost::size(exceeding1) > 0)
                                {
                                    // All exceeding from 1 with 2:
                                    handle_two(*collection1, exceeding1, *collection2, exceeding2,
                                                *policy);

                                    // All exceeding from 1 with lower and upper of 2:
                                    handle_two(*collection1, exceeding1, *collection2, lower2, *policy);
                                    handle_two(*collection1, exceeding1, *collection2, upper2, *policy);
                                }
                                if (boost::size(exceeding2) > 0)
                                {
                                    // All exceeding from 2 with lower and upper of 1:
                                    handle_two(*collection1, lower1, *collection2, exceeding2, *policy);
                                    handle_two(*collection1, upper1, *collection2, exceeding2, *policy);
                                }
                                //std::cout << "parallel_partition_task. before next_level" << std::endl;
                                Owner::next_level(lower_box, *collection1, lower1, *collection2, lower2, level,
                                                min_elements, *policy, box_policy);
                                Owner::next_level(upper_box, *collection1, upper1, *collection2, upper2, level,
                                                min_elements, *policy, box_policy);

                                //std::cout << "parallel_partition_task. after next_level" << std::endl;
                                task_res.set_value(std::move(*policy));
                                //std::cout << "parallel_partition_task. after set_value" << std::endl;
                            }
                            catch(std::exception& e)
                            {
                                std::cout << "parallel_partition_task. Exception" << std::endl;
                                task_res.set_exception(boost::copy_exception(e));
                            }
                        },
                        // recursive tasks
                        divide_into_subsets_task<InputCollection1,OverlapsPolicy1>(lower_box,upper_box,collection1_,input1_,
                                                                                         boost::begin(*input1_),boost::end(*input1_),
                                                                                         cutoff_,this->get_name(),prio_),
                        divide_into_subsets_task<InputCollection2,OverlapsPolicy2>(lower_box,upper_box,collection2_,input2_,
                                                                                         boost::begin(*input2_),boost::end(*input2_),
                                                                                         cutoff_,this->get_name(),prio_)
               );
        }

        Box box_;
        boost::shared_ptr<InputCollection1> collection1_;
        boost::shared_ptr<InputCollection2> collection2_;
        boost::shared_ptr<index_vector_type> input1_;
        boost::shared_ptr<index_vector_type> input2_;
        int level_;
        std::size_t min_elements_;
        boost::shared_ptr<Policy> policy_;
        VisitBoxPolicy box_policy_;
        long cutoff_;
        std::size_t prio_;
    };
public :
    template
    <
        typename InputCollection1,
        typename InputCollection2,
        typename Policy
    >
    static inline
    boost::asynchronous::detail::callback_continuation<Policy,Job>
    apply2(Box const& box,
            InputCollection1 const& collection1, index_vector_type const& input1,
            InputCollection2 const& collection2, index_vector_type const& input2,
            int level,
            std::size_t min_elements,
            Policy& policy, VisitBoxPolicy& box_policy)
    {
        // TODO not hard-coded, +name +prio
        // TODO move?
        return boost::asynchronous::top_level_callback_continuation_job<Policy,Job>
                 (parallel_partition_task/*dummy_partition_task*/<InputCollection1,InputCollection2,Policy,this_type>(
                      box,
                      boost::make_shared<InputCollection1>(collection1),boost::make_shared<index_vector_type>(input1),
                      boost::make_shared<InputCollection2>(collection2),boost::make_shared<index_vector_type>(input2),
                      level,min_elements,
                      boost::make_shared<Policy>(policy),box_policy,
                      3000000,"geometry::parallel_partition",0));
    }

    template
    <
        typename InputCollection1,
        typename InputCollection2,
        typename Policy
    >
    static inline void apply(Box const& box,
            InputCollection1 const& collection1, index_vector_type const& input1,
            InputCollection2 const& collection2, index_vector_type const& input2,
            int level,
            std::size_t min_elements,
            Policy& policy, VisitBoxPolicy& box_policy)
    {
        box_policy.apply(box, level);

        Box lower_box, upper_box;
        divide_box<Dimension>(box, lower_box, upper_box);

        index_vector_type lower1, upper1, exceeding1;
        index_vector_type lower2, upper2, exceeding2;
        divide_into_subsets<OverlapsPolicy1>(lower_box, upper_box, collection1,
                    input1, lower1, upper1, exceeding1);
        divide_into_subsets<OverlapsPolicy2>(lower_box, upper_box, collection2,
                    input2, lower2, upper2, exceeding2);

        if (boost::size(exceeding1) > 0)
        {
            // All exceeding from 1 with 2:
            handle_two(collection1, exceeding1, collection2, exceeding2,
                        policy);

            // All exceeding from 1 with lower and upper of 2:
            handle_two(collection1, exceeding1, collection2, lower2, policy);
            handle_two(collection1, exceeding1, collection2, upper2, policy);
        }
        if (boost::size(exceeding2) > 0)
        {
            // All exceeding from 2 with lower and upper of 1:
            handle_two(collection1, lower1, collection2, exceeding2, policy);
            handle_two(collection1, upper1, collection2, exceeding2, policy);
        }

        next_level(lower_box, collection1, lower1, collection2, lower2, level,
                        min_elements, policy, box_policy);
        next_level(upper_box, collection1, upper1, collection2, upper2, level,
                        min_elements, policy, box_policy);
    }
};

}} // namespace detail::partition
/*
struct visit_no_policy
{
    template <typename Box>
    static inline void apply(Box const&, int )
    {}
};
*/
template
<
    typename Box,
    typename ExpandPolicy1,
    typename OverlapsPolicy1,
    typename ExpandPolicy2 = ExpandPolicy1,
    typename OverlapsPolicy2 = OverlapsPolicy1,
    typename VisitBoxPolicy = visit_no_policy,
    typename Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB
>
class parallel_partition
{
    typedef std::vector<std::size_t> index_vector_type;

    template <typename ExpandPolicy, typename InputCollection>
    static inline void expand_to_collection(InputCollection const& collection,
                Box& total, index_vector_type& index_vector)
    {
        std::size_t index = 0;
        for(typename boost::range_iterator<InputCollection const>::type it
            = boost::begin(collection);
            it != boost::end(collection);
            ++it, ++index)
        {
            ExpandPolicy::apply(total, *it);
            index_vector.push_back(index);
        }
    }

public :
    template <typename InputCollection, typename VisitPolicy>
    static inline void apply(InputCollection const& collection,
            VisitPolicy& visitor,
            std::size_t min_elements = 16,
            VisitBoxPolicy box_visitor = visit_no_policy()
            )
    {
        if (std::size_t(boost::size(collection)) > min_elements)
        {
            index_vector_type index_vector;
            Box total;
            assign_inverse(total);
            expand_to_collection<ExpandPolicy1>(collection, total, index_vector);

            detail::partition::parallel_partition_one_collection
                <
                    0, Box,
                    OverlapsPolicy1,
                    VisitBoxPolicy
                >::apply(total, collection, index_vector, 0, min_elements,
                                visitor, box_visitor);
        }
        else
        {
            typedef typename boost::range_iterator
                <
                    InputCollection const
                >::type iterator_type;
            for(iterator_type it1 = boost::begin(collection);
                it1 != boost::end(collection);
                ++it1)
            {
                iterator_type it2 = it1;
                for(++it2; it2 != boost::end(collection); ++it2)
                {
                    visitor.apply(*it1, *it2);
                }
            }
        }
    }

    template
    <
        typename InputCollection1,
        typename InputCollection2,
        typename VisitPolicy
    >
    static inline boost::asynchronous::detail::callback_continuation<VisitPolicy,Job>
    apply(InputCollection1 const& collection1,
                InputCollection2 const& collection2,
                VisitPolicy& visitor,
                std::size_t min_elements = 16,
                VisitBoxPolicy box_visitor = visit_no_policy()
                )
    {
        //TODO
      // // if (std::size_t(boost::size(collection1)) > min_elements
     //       && std::size_t(boost::size(collection2)) > min_elements)
       // {
            index_vector_type index_vector1, index_vector2;
            Box total;
            assign_inverse(total);
            expand_to_collection<ExpandPolicy1>(collection1, total, index_vector1);
            expand_to_collection<ExpandPolicy2>(collection2, total, index_vector2);

            return
            detail::partition::parallel_partition_two_collections
                <
                    0, Box, OverlapsPolicy1, OverlapsPolicy2, VisitBoxPolicy,Job
                >::apply2(total,
                    collection1, index_vector1,
                    collection2, index_vector2,
                    0, min_elements, visitor, box_visitor);
       // }
       /* else
        {
            typedef typename boost::range_iterator
                <
                    InputCollection1 const
                >::type iterator_type1;
            typedef typename boost::range_iterator
                <
                    InputCollection2 const
                >::type iterator_type2;
            for(iterator_type1 it1 = boost::begin(collection1);
                it1 != boost::end(collection1);
                ++it1)
            {
                for(iterator_type2 it2 = boost::begin(collection2);
                    it2 != boost::end(collection2);
                    ++it2)
                {
                    visitor.apply(*it1, *it2);
                }
            }
        }*/
    }
};


}} // namespace boost::geometry

#endif // BOOST_ASYNCHRONOUS_GEOMETRY_ALGORITHMS_DETAIL_PARTITION_HPP
