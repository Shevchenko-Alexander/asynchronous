// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2015
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#ifndef BOOST_ASYNCHRONOUS_CONTAINER_ALGORITHMS_HPP
#define BOOST_ASYNCHRONOUS_CONTAINER_ALGORITHMS_HPP

#include <boost/mpl/has_xxx.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asynchronous/continuation_task.hpp>
#include <boost/asynchronous/algorithm/parallel_placement.hpp>

BOOST_MPL_HAS_XXX_TRAIT_DEF(asynchronous_container)


namespace boost { namespace asynchronous
{
namespace detail
{
// push_back
template <class Container, class T>
struct push_back_task: public boost::asynchronous::continuation_task<Container>
{
    push_back_task(Container c, T val)
        : boost::asynchronous::continuation_task<Container>("push_back_task")
        , m_container(std::move(c)),m_value(std::move(val))
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<Container> task_res = this->this_task_result();
        try
        {
            // if we can insert without reallocating, we are done fast.
            if (m_container.size() + 1 <= m_container.capacity() )
            {
                m_container.push_back(m_value);
                task_res.set_value(std::move(m_container));
                return;
            }
            // reallocate
            boost::shared_ptr<Container> c = boost::make_shared<Container>(std::move(m_container));
            auto v = m_value;
            auto capacity = c->calc_new_capacity(c->size());
            auto cont = c->async_reallocate(capacity,c->size());
            cont.on_done([task_res,c,v,capacity]
                           (std::tuple<boost::asynchronous::expected<typename Container::internal_data_type> >&& res)mutable
            {
                try
                {
                    // reallocation has already been done, ok to call push_back directly
                    c->set_internal_data(std::move(std::get<0>(res).get()),capacity);
                    c->push_back(v);
                    task_res.set_value(std::move(*c));
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
    }
    Container m_container;
    T m_value;
};
}
template <class Container, class T, typename Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<Container,Job>
async_push_back(Container c, T&& data,typename boost::disable_if<boost::asynchronous::detail::has_is_continuation_task<Container>>::type* = 0)
{
    return boost::asynchronous::top_level_callback_continuation_job<Container,Job>
        (boost::asynchronous::detail::push_back_task<Container,T>(std::move(c), std::move(data)));

}
// continuation
namespace detail
{
template <class Continuation, class T, class Job>
struct push_back_task_continuation: public boost::asynchronous::continuation_task<typename Continuation::return_type>
{
    push_back_task_continuation(Continuation c, T val)
        : boost::asynchronous::continuation_task<typename Continuation::return_type>("push_back_task")
        , m_continuation(std::move(c)),m_value(std::move(val))
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<typename Continuation::return_type> task_res = this->this_task_result();
        try
        {
            auto val = m_value;
            m_continuation.on_done([task_res,val]
                                   (std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& continuation_res)mutable
            {
                try
                {
                    auto c = std::move(std::get<0>(continuation_res).get());
                    auto push_back_cont = boost::asynchronous::async_push_back<typename Continuation::return_type,T,Job>
                                            (std::move(c),std::move(val));
                    push_back_cont.on_done([task_res](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& res) mutable
                    {
                        try
                        {
                            task_res.set_value(std::move(std::get<0>(res).get()));
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
        }
        catch(std::exception& e)
        {
            task_res.set_exception(boost::copy_exception(e));
        }
    }
    Continuation m_continuation;
    T m_value;
};
}
template <class Container, class T, typename Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<typename Container::return_type,Job>
async_push_back(Container c, T&& data,typename boost::enable_if<boost::asynchronous::detail::has_is_continuation_task<Container>>::type* = 0)
{
    return boost::asynchronous::top_level_callback_continuation_job<typename Container::return_type,Job>
        (boost::asynchronous::detail::push_back_task_continuation<Container,T,Job>(std::move(c), std::move(data)));

}

// resize
namespace detail
{
template <class Container, class Job>
struct resize_task: public boost::asynchronous::continuation_task<Container>
{
    typedef typename Container::value_type value_type;
    resize_task(Container c, std::size_t val)
        : boost::asynchronous::continuation_task<Container>("resize_task")
        , m_container(std::move(c)),m_value(std::move(val))
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<Container> task_res = this->this_task_result();
        try
        {
            // if nothing to so, end
            if (m_container.size() == m_value )
            {
                task_res.set_value(std::move(m_container));
                return;
            }
            boost::shared_ptr<Container> c = boost::make_shared<Container>(std::move(m_container));
            if (m_value > c->size())
            {
                // we need to allocate new elements
                if (m_value > c->capacity())
                {
                    // reallocate memory
                    auto v = m_value;
                    auto capacity = c->calc_new_capacity(m_value);
                    auto cont = c->async_reallocate(capacity,m_value);
                    cont.on_done([task_res,c,v,capacity]
                                   (std::tuple<boost::asynchronous::expected<typename Container::internal_data_type> >&& res)mutable
                    {
                        try
                        {
                            auto new_data = std::move(std::get<0>(res).get());
                            c->set_internal_data(new_data,capacity);
                            task_res.set_value(std::move(*c));
                        }
                        catch(std::exception& e)
                        {
                            task_res.set_exception(boost::copy_exception(e));
                        }
                    });
                }
            }
            else
            {
                // remove elements
                auto data = c->get_internal_data();
                data->size_ = m_value;
                auto cont = boost::asynchronous::parallel_placement_delete<value_type,Job>
                        (data->data_,m_value,c->size(),c->get_cutoff(),c->get_name()+"_vector_resize_placement_delete",c->get_prio());
                cont.on_done([task_res,c,data](std::tuple<boost::asynchronous::expected<void>>&& res)mutable
                {
                    try
                    {
                        // check for exception
                        std::get<0>(res).get();
                        c->set_internal_data(data,c->capacity());
                        task_res.set_value(std::move(*c));
                    }
                    catch(std::exception& e)
                    {
                        task_res.set_exception(boost::copy_exception(e));
                    }
                });
            }
        }
        catch(std::exception& e)
        {
            task_res.set_exception(boost::copy_exception(e));
        }
    }
    Container m_container;
    std::size_t m_value;
};
}
template <class Container, typename Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<Container,Job>
async_resize(Container c, std::size_t s,typename boost::disable_if<boost::asynchronous::detail::has_is_continuation_task<Container>>::type* = 0)
{
    return boost::asynchronous::top_level_callback_continuation_job<Container,Job>
        (boost::asynchronous::detail::resize_task<Container,Job>(std::move(c), s));

}
// continuation
namespace detail
{
template <class Continuation, class Job>
struct resize_task_continuation: public boost::asynchronous::continuation_task<typename Continuation::return_type>
{
    resize_task_continuation(Continuation c, std::size_t val)
        : boost::asynchronous::continuation_task<typename Continuation::return_type>("resize_task")
        , m_continuation(std::move(c)),m_value(val)
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<typename Continuation::return_type> task_res = this->this_task_result();
        try
        {
            auto val = m_value;
            m_continuation.on_done([task_res,val]
                                   (std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& continuation_res)mutable
            {
                try
                {
                    auto c = std::move(std::get<0>(continuation_res).get());
                    auto resize_cont = boost::asynchronous::async_resize<typename Continuation::return_type,Job>
                                            (std::move(c),val);
                    resize_cont.on_done([task_res](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& res) mutable
                    {
                        try
                        {
                            task_res.set_value(std::move(std::get<0>(res).get()));
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
        }
        catch(std::exception& e)
        {
            task_res.set_exception(boost::copy_exception(e));
        }
    }
    Continuation m_continuation;
    std::size_t m_value;
};
}
template <class Container, typename Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<typename Container::return_type,Job>
async_resize(Container c, std::size_t s,typename boost::enable_if<boost::asynchronous::detail::has_is_continuation_task<Container>>::type* = 0)
{
    return boost::asynchronous::top_level_callback_continuation_job<typename Container::return_type,Job>
        (boost::asynchronous::detail::resize_task_continuation<Container,Job>(std::move(c), s));

}


// reserve
namespace detail
{
template <class Container>
struct reserve_task: public boost::asynchronous::continuation_task<Container>
{
    reserve_task(Container c, std::size_t val)
        : boost::asynchronous::continuation_task<Container>("reserve_task")
        , m_container(std::move(c)),m_value(std::move(val))
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<Container> task_res = this->this_task_result();
        try
        {
            if (m_value <= m_container.capacity())
            {
                task_res.set_value(std::move(m_container));
                return;
            }
            // more memory, same size
            boost::shared_ptr<Container> c = boost::make_shared<Container>(std::move(m_container));
            auto v = m_value;
            auto cont = c->async_reallocate(m_value,c->size());
            cont.on_done([task_res,c,v]
                           (std::tuple<boost::asynchronous::expected<typename Container::internal_data_type> >&& res)mutable
            {
                try
                {
                    auto new_data = std::get<0>(res).get();
                    c->set_internal_data(new_data,v);
                    task_res.set_value(std::move(*c));
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
    }
    Container m_container;
    std::size_t m_value;
};
}
template <class Container, typename Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<Container,Job>
async_reserve(Container c, std::size_t s,typename boost::disable_if<boost::asynchronous::detail::has_is_continuation_task<Container>>::type* = 0)
{
    return boost::asynchronous::top_level_callback_continuation_job<Container,Job>
        (boost::asynchronous::detail::reserve_task<Container>(std::move(c), s));

}

// continuation
namespace detail
{
template <class Continuation>
struct reserve_task_continuation: public boost::asynchronous::continuation_task<typename Continuation::return_type>
{
    reserve_task_continuation(Continuation c, std::size_t val)
        : boost::asynchronous::continuation_task<typename Continuation::return_type>("reserve_task")
        , m_continuation(std::move(c)),m_value(val)
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<typename Continuation::return_type> task_res = this->this_task_result();
        try
        {
            auto val = m_value;
            m_continuation.on_done([task_res,val]
                                   (std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& continuation_res)mutable
            {
                try
                {
                    auto c = std::move(std::get<0>(continuation_res).get());
                    auto resize_cont = boost::asynchronous::async_reserve<typename Continuation::return_type>
                                            (std::move(c),val);
                    resize_cont.on_done([task_res](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& res) mutable
                    {
                        try
                        {
                            task_res.set_value(std::move(std::get<0>(res).get()));
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
        }
        catch(std::exception& e)
        {
            task_res.set_exception(boost::copy_exception(e));
        }
    }
    Continuation m_continuation;
    std::size_t m_value;
};
}
template <class Container, typename Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<typename Container::return_type,Job>
async_reserve(Container c, std::size_t s,typename boost::enable_if<boost::asynchronous::detail::has_is_continuation_task<Container>>::type* = 0)
{
    return boost::asynchronous::top_level_callback_continuation_job<typename Container::return_type,Job>
        (boost::asynchronous::detail::reserve_task_continuation<Container>(std::move(c), s));

}


// shrink_to_fit
namespace detail
{
template <class Container>
struct shrink_to_fit_task: public boost::asynchronous::continuation_task<Container>
{
    shrink_to_fit_task(Container c)
        : boost::asynchronous::continuation_task<Container>("shrink_to_fit_task")
        , m_container(std::move(c))
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<Container> task_res = this->this_task_result();
        try
        {
            if (m_container.size() == m_container.capacity())
            {
                // nothing to do
                task_res.set_value(std::move(m_container));
                return;
            }
            // reduce memory to size
            boost::shared_ptr<Container> c = boost::make_shared<Container>(std::move(m_container));
            auto cont = c->async_reallocate(c->size(),c->size());
            cont.on_done([task_res,c]
                           (std::tuple<boost::asynchronous::expected<typename Container::internal_data_type> >&& res)mutable
            {
                try
                {
                    auto new_data = std::get<0>(res).get();
                    c->set_internal_data(new_data,c->size());
                    task_res.set_value(std::move(*c));
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
    }
    Container m_container;
};
}
template <class Container, typename Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<Container,Job>
async_shrink_to_fit(Container c,typename boost::disable_if<boost::asynchronous::detail::has_is_continuation_task<Container>>::type* = 0)
{
    return boost::asynchronous::top_level_callback_continuation_job<Container,Job>
        (boost::asynchronous::detail::shrink_to_fit_task<Container>(std::move(c)));

}

// continuation
namespace detail
{
template <class Continuation>
struct shrink_to_fit_task_continuation: public boost::asynchronous::continuation_task<typename Continuation::return_type>
{
    shrink_to_fit_task_continuation(Continuation c)
        : boost::asynchronous::continuation_task<typename Continuation::return_type>("shrink_to_fit_task")
        , m_continuation(std::move(c))
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<typename Continuation::return_type> task_res = this->this_task_result();
        try
        {
            m_continuation.on_done([task_res]
                                   (std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& continuation_res)mutable
            {
                try
                {
                    auto c = std::move(std::get<0>(continuation_res).get());
                    auto resize_cont = boost::asynchronous::async_shrink_to_fit<typename Continuation::return_type>
                                            (std::move(c));
                    resize_cont.on_done([task_res](std::tuple<boost::asynchronous::expected<typename Continuation::return_type> >&& res) mutable
                    {
                        try
                        {
                            task_res.set_value(std::move(std::get<0>(res).get()));
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
        }
        catch(std::exception& e)
        {
            task_res.set_exception(boost::copy_exception(e));
        }
    }
    Continuation m_continuation;
};
}
template <class Container, typename Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<typename Container::return_type,Job>
async_shrink_to_fit(Container c, typename boost::enable_if<boost::asynchronous::detail::has_is_continuation_task<Container>>::type* = 0)
{
    return boost::asynchronous::top_level_callback_continuation_job<typename Container::return_type,Job>
        (boost::asynchronous::detail::shrink_to_fit_task_continuation<Container>(std::move(c)));

}
namespace detail
{
template<typename Range, typename Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
struct make_asynchronous_range_task: public boost::asynchronous::continuation_task<Range>
{
    make_asynchronous_range_task(std::size_t n,long cutoff,const std::string& task_name, std::size_t prio)
    : boost::asynchronous::continuation_task<Range>(task_name)
    , m_size(n),m_cutoff(cutoff),m_task_name(task_name),m_prio(prio)
    {}
    void operator()();
    std::size_t m_size;
    long m_cutoff;
    std::string m_task_name;
    std::size_t m_prio;
};
//version asynchronous contaimners
template<typename Range, typename Job>
void make_asynchronous_range_task<Range,Job>::operator()()
{
    boost::asynchronous::continuation_result<Range> task_res = this->this_task_result();
    try
    {
        auto v = boost::make_shared<Range>(m_cutoff,m_size,m_task_name,m_prio);
        auto alloc = v->get_allocator();
        auto n = m_size;
        boost::shared_ptr<typename Range::value_type> raw (alloc.allocate(n),
                                                           [alloc,n](typename Range::value_type* p)mutable{alloc.deallocate(p,n);});

        auto cutoff = m_cutoff;
        auto task_name = m_task_name;
        auto prio = m_prio;

        auto cont = boost::asynchronous::parallel_placement<typename Range::value_type,Job>
                (0,n,(char*)raw.get(),typename Range::value_type(),cutoff,task_name+"_vector_placement",prio);
        cont.on_done(
        [task_res, raw,v,n,cutoff,task_name,prio]
        (std::tuple<boost::asynchronous::expected<boost::asynchronous::detail::parallel_placement_helper_result> >&& continuation_res) mutable
        {
            try
            {
                auto res = std::get<0>(continuation_res).get();
                if (res.first != boost::asynchronous::detail::parallel_placement_helper_enum::success)
                {
                    task_res.set_exception(res.second);
                }
                else
                {
                    v->set_internal_data(boost::make_shared<boost::asynchronous::placement_deleter<typename Range::value_type,
                                                                                          Job,
                                                                                          boost::shared_ptr<typename Range::value_type>>>
                            (n,raw,cutoff,task_name,prio),n);
                    task_res.set_value(std::move(*v));
                }
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
}

//version for plain old containers
template<typename Range>
struct make_standard_range_task: public boost::asynchronous::continuation_task<Range>
{
    make_standard_range_task(std::size_t n)
    : m_size(n)
    {}
    void operator()()
    {
        boost::asynchronous::continuation_result<Range> task_res = this->this_task_result();
        task_res.set_value(Range(m_size));
    }

    std::size_t m_size;
};

}

//version asynchronous contaimners
template <typename Range, typename Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<Range,Job>
make_asynchronous_range(std::size_t n,long cutoff,
 #ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
            const std::string& task_name, std::size_t prio, typename boost::enable_if<has_asynchronous_container<Range>>::type* = 0)
 #else
            const std::string& task_name="", std::size_t prio=0, typename boost::enable_if<has_asynchronous_container<Range>>::type* = 0)
 #endif
{
    return boost::asynchronous::top_level_callback_continuation_job<Range,Job>
            (boost::asynchronous::detail::make_asynchronous_range_task<Range,Job>
             (n,cutoff,task_name,prio));
}

//version for plain old contaimners
template <typename Range, typename Job=BOOST_ASYNCHRONOUS_DEFAULT_JOB>
boost::asynchronous::detail::callback_continuation<Range,Job>
make_asynchronous_range(std::size_t n,long ,
 #ifdef BOOST_ASYNCHRONOUS_REQUIRE_ALL_ARGUMENTS
            const std::string& , std::size_t , typename boost::disable_if<has_asynchronous_container<Range>>::type* = 0)
 #else
            const std::string& ="", std::size_t =0, typename boost::disable_if<has_asynchronous_container<Range>>::type* = 0)
 #endif
{
    return boost::asynchronous::top_level_callback_continuation_job<Range,Job>
            (boost::asynchronous::detail::make_standard_range_task<Range>(n));
}

}}

#endif // BOOST_ASYNCHRONOUS_CONTAINER_ALGORITHMS_HPP

