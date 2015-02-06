// Boost.Geometry (aka GGL, Generic Geometry Library)

// Copyright (c) 2007-2012 Barend Gehrels, Amsterdam, the Netherlands.
// Copyright (c) 2013 Adam Wulkiewicz, Lodz, Poland

// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_ASYNCHRONOUS_GEOMETRY_ALGORITHMS_DETAIL_OVERLAY_OVERLAY_HPP
#define BOOST_ASYNCHRONOUS_GEOMETRY_ALGORITHMS_DETAIL_OVERLAY_OVERLAY_HPP


#include <deque>
#include <map>

#include <boost/range.hpp>
#include <boost/mpl/assert.hpp>


#include <boost/geometry/algorithms/detail/overlay/enrich_intersection_points.hpp>
#include <boost/geometry/algorithms/detail/overlay/enrichment_info.hpp>
#include <boost/geometry/algorithms/detail/overlay/get_turns.hpp>
#include <boost/geometry/algorithms/detail/overlay/overlay_type.hpp>
#include <boost/geometry/algorithms/detail/overlay/traverse.hpp>
#include <boost/geometry/algorithms/detail/overlay/traversal_info.hpp>
#include <boost/geometry/algorithms/detail/overlay/turn_info.hpp>

#include <boost/geometry/algorithms/detail/recalculate.hpp>

#include <boost/geometry/algorithms/num_points.hpp>
#include <boost/geometry/algorithms/reverse.hpp>

#include <boost/geometry/algorithms/detail/overlay/add_rings.hpp>
#include <boost/geometry/algorithms/detail/overlay/assign_parents.hpp>
#include <boost/geometry/algorithms/detail/overlay/ring_properties.hpp>
#include <boost/asynchronous/algorithm/geometry/detail/select_rings.hpp>
#include <boost/geometry/algorithms/detail/overlay/do_reverse.hpp>

#include <boost/geometry/policies/robustness/segment_ratio_type.hpp>


#ifdef BOOST_GEOMETRY_DEBUG_ASSEMBLE
#  include <boost/geometry/io/dsv/write.hpp>
#endif

#ifdef BOOST_GEOMETRY_TIME_OVERLAY
# include <boost/timer.hpp>
#endif


namespace boost { namespace geometry
{


#ifndef DOXYGEN_NO_DETAIL
namespace detail { namespace overlay
{

// Skip for assemble process
/*template <typename TurnInfo>
inline bool skip(TurnInfo const& turn_info)
{
    return (turn_info.discarded || turn_info.both(operation_union))
        && ! turn_info.any_blocked()
        && ! turn_info.both(operation_intersection)
        ;
}


template <typename TurnPoints, typename Map>
inline void map_turns(Map& map, TurnPoints const& turn_points)
{
    typedef typename boost::range_value<TurnPoints>::type turn_point_type;
    typedef typename turn_point_type::container_type container_type;

    for (typename boost::range_iterator<TurnPoints const>::type
            it = boost::begin(turn_points);
         it != boost::end(turn_points);
         ++it)
    {
        if (! skip(*it))
        {
            for (typename boost::range_iterator<container_type const>::type
                    op_it = boost::begin(it->operations);
                op_it != boost::end(it->operations);
                ++op_it)
            {
                ring_identifier ring_id
                    (
                        op_it->seg_id.source_index,
                        op_it->seg_id.multi_index,
                        op_it->seg_id.ring_index
                    );
                map[ring_id]++;
            }
        }
    }
}*/


/*template
<
    typename GeometryOut, overlay_type Direction, bool ReverseOut,
    typename Geometry1, typename Geometry2,
    typename OutputIterator
>
inline OutputIterator return_if_one_input_is_empty(Geometry1 const& geometry1,
            Geometry2 const& geometry2,
            OutputIterator out)
{
    typedef std::deque
        <
            typename geometry::ring_type<GeometryOut>::type
        > ring_container_type;

    typedef ring_properties<typename geometry::point_type<Geometry1>::type> properties;

// Silence warning C4127: conditional expression is constant
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4127)
#endif

    // Union: return either of them
    // Intersection: return nothing
    // Difference: return first of them
    if (Direction == overlay_intersection
        || (Direction == overlay_difference
            && geometry::num_points(geometry1) == 0))
    {
        return out;
    }

#if defined(_MSC_VER)
#pragma warning(pop)
#endif


    std::map<ring_identifier, int> empty;
    std::map<ring_identifier, properties> all_of_one_of_them;

    select_rings<Direction>(geometry1, geometry2, empty, all_of_one_of_them, false);
    ring_container_type rings;
    assign_parents(geometry1, geometry2, rings, all_of_one_of_them);
    return add_rings<GeometryOut>(all_of_one_of_them, geometry1, geometry2, rings, out);
}
*/

template
<
    overlay_type OverlayType,
    typename Geometry1, typename Geometry2,
    typename IntersectionMap, typename SelectionMap
>
struct update_selection_map_fct
{
    update_selection_map_fct(){}
    update_selection_map_fct(Geometry1& geometry1,
                             Geometry2& geometry2,
                             IntersectionMap& intersection_map,
                             boost::shared_ptr<SelectionMap> map_with_all)
        : geometry1_(boost::make_shared<Geometry1>(std::move(geometry1)))
        , geometry2_(boost::make_shared<Geometry2>(std::move(geometry2)))
        , intersection_map_(boost::make_shared<IntersectionMap>(std::move(intersection_map)))
        , map_with_all_(map_with_all)
    {}

    template <class T>
    void operator()(/*SelectionMap*/T const& i)
    {
        bool found = (*intersection_map_).find(i.first) != (*intersection_map_).end();
        if (! found)
        {
            ring_identifier const id = i.first;
            typename SelectionMap::mapped_type properties = i.second; // Copy by value

            // Calculate the "within code" (previously this was done earlier but is
            // much efficienter here - it can be even more efficient doing it all at once,
            // using partition, TODO)
            // So though this is less elegant than before, it avoids many unused point-in-poly calculations
            switch(id.source_index)
            {
                case 0 :
                    properties.within_code
                        = geometry::within(properties.point, *geometry2_) ? 1 : -1;
                    break;
                case 1 :
                    properties.within_code
                        = geometry::within(properties.point, *geometry1_) ? 1 : -1;
                    break;
            }

            if (decide<OverlayType>::include(id, properties))
            {
                properties.reversed = decide<OverlayType>::reversed(id, properties);
                selection_map_[id] = properties;
            }
        }
    }
    void merge(update_selection_map_fct const& rhs)
    {
        // TODO move possible?
        selection_map_.insert(rhs.selection_map_.begin(),rhs.selection_map_.end());
    }
    boost::shared_ptr<Geometry1> geometry1_;
    boost::shared_ptr<Geometry2> geometry2_;
    boost::shared_ptr<IntersectionMap> intersection_map_;
    boost::shared_ptr<SelectionMap> map_with_all_;
    SelectionMap selection_map_;
};

template
<
    typename Job,
    typename Geometry1, typename Geometry2,
    bool Reverse1, bool Reverse2, bool ReverseOut,
    typename GeometryOut,
    overlay_type Direction
>
struct parallel_overlay
{
    template <typename TaskRes,typename RobustPolicy, typename Strategy>
    static inline void apply(
                TaskRes task_res,
                Geometry1& geometry1, Geometry2& geometry2,
                RobustPolicy& robust_policy,
                Strategy const& ,
                long cutoff)
    {
        boost::shared_ptr<typename TaskRes::return_type> output_collection(boost::make_shared<typename TaskRes::return_type>());
        auto out = std::back_inserter(*output_collection);

        if ( geometry::num_points(geometry1) == 0
          && geometry::num_points(geometry2) == 0 )
        {
            task_res.set_value(std::move(*output_collection));
            return;
        }

        if ( geometry::num_points(geometry1) == 0
          || geometry::num_points(geometry2) == 0 )
        {
            task_res.set_value(std::move(*output_collection));
            return_if_one_input_is_empty
                <
                    GeometryOut, Direction, ReverseOut
                >(geometry1, geometry2, out);
            return;
        }

        typedef typename geometry::point_type<GeometryOut>::type point_type;
        typedef detail::overlay::traversal_turn_info
        <
            point_type,
            typename geometry::segment_ratio_type<point_type, RobustPolicy>::type
        > turn_info;
        typedef std::deque<turn_info> container_type;

        typedef std::deque
            <
                typename geometry::ring_type<GeometryOut>::type
            > ring_container_type;

        container_type turn_points;

#ifdef BOOST_GEOMETRY_TIME_OVERLAY
        boost::timer timer;
#endif
#ifdef BOOST_GEOMETRY_DEBUG_ASSEMBLE
std::cout << "get turns" << std::endl;
#endif
        detail::get_turns::no_interrupt_policy policy;
        geometry::get_turns
            <
                Reverse1, Reverse2,
                detail::overlay::assign_null_policy
            >(geometry1, geometry2, robust_policy, turn_points, policy);

#ifdef BOOST_GEOMETRY_TIME_OVERLAY
        std::cout << "get_turns: " << timer.elapsed() << std::endl;
#endif

#ifdef BOOST_GEOMETRY_DEBUG_ASSEMBLE
std::cout << "enrich" << std::endl;
#endif
        typename Strategy::side_strategy_type side_strategy;
        geometry::enrich_intersection_points<Reverse1, Reverse2>(turn_points,
                Direction == overlay_union
                    ? geometry::detail::overlay::operation_union
                    : geometry::detail::overlay::operation_intersection,
                    geometry1, geometry2,
                    robust_policy,
                    side_strategy);

#ifdef BOOST_GEOMETRY_TIME_OVERLAY
        std::cout << "enrich_intersection_points: " << timer.elapsed() << std::endl;
#endif

#ifdef BOOST_GEOMETRY_DEBUG_ASSEMBLE
std::cout << "traverse" << std::endl;
#endif
        // Traverse through intersection/turn points and create rings of them.
        // Note that these rings are always in clockwise order, even in CCW polygons,
        // and are marked as "to be reversed" below
        boost::shared_ptr<ring_container_type> rings(boost::make_shared<ring_container_type>());
        traverse<Reverse1, Reverse2, Geometry1, Geometry2>::apply
                (
                    geometry1, geometry2,
                    Direction == overlay_union
                        ? geometry::detail::overlay::operation_union
                        : geometry::detail::overlay::operation_intersection,
                    robust_policy,
                    turn_points, *rings
                );

#ifdef BOOST_GEOMETRY_TIME_OVERLAY
        std::cout << "traverse: " << timer.elapsed() << std::endl;
#endif

        std::map<ring_identifier, int> map;
        map_turns(map, turn_points);

#ifdef BOOST_GEOMETRY_TIME_OVERLAY
        std::cout << "map_turns: " << timer.elapsed() << std::endl;
#endif

        typedef ring_properties<typename geometry::point_type<GeometryOut>::type> properties;

        //std::map<ring_identifier, properties> selected;
        typedef update_selection_map_fct<Direction,Geometry1,Geometry2,
                                         std::map<ring_identifier, int>,
                                         std::map<ring_identifier, properties>> select_ring_fct;

        auto cont = pselect_rings<Direction,std::map<ring_identifier, properties>,select_ring_fct,Job>(
                    geometry1, geometry2, map, ! turn_points.empty(),cutoff);

        cont.on_done(
        [task_res,rings,output_collection](std::tuple<boost::asynchronous::expected<select_ring_fct> >&& res)
        {
            try
            {
                select_ring_fct all_fct (std::move(std::get<0>(res).get()));
                // Add rings created during traversal
                {
                    ring_identifier id(2, 0, -1);
                    for (typename boost::range_iterator<ring_container_type>::type
                            it = boost::begin(*rings);
                         it != boost::end(*rings);
                         ++it)
                    {
                        (all_fct.selection_map_)[id] = properties(*it, true);
                        (all_fct.selection_map_)[id].reversed = ReverseOut;
                        id.multi_index++;
                    }
                }

                assign_parents(*all_fct.geometry1_, *all_fct.geometry2_, *rings, all_fct.selection_map_);

                add_rings<GeometryOut>(all_fct.selection_map_, *all_fct.geometry1_, *all_fct.geometry2_,
                                       *rings, std::back_inserter(*output_collection));
                task_res.set_value(std::move(*output_collection));
            }
            catch(std::exception& e)
            {
                task_res.set_exception(boost::copy_exception(e));
            }
        });


    }
};


}} // namespace detail::overlay
#endif // DOXYGEN_NO_DETAIL


}} // namespace boost::geometry


#endif // BOOST_ASYNCHRONOUS_GEOMETRY_ALGORITHMS_DETAIL_OVERLAY_OVERLAY_HPP
