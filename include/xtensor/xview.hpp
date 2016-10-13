/***************************************************************************
* Copyright (c) 2016, Johan Mabille and Sylvain Corlay                     *
*                                                                          *
* Distributed under the terms of the BSD 3-Clause License.                 *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
****************************************************************************/

#ifndef XVIEW_HPP
#define XVIEW_HPP

#include <utility>
#include <type_traits>
#include <tuple>
#include <algorithm>

#include "xexpression.hpp"
#include "xutils.hpp"
#include "xslice.hpp"
#include "xindex.hpp"
#include "xiterator.hpp"

namespace xt
{

    /*********************
     * xview declaration *
     *********************/

    template <class E, class... S>
    class xview_stepper;

    template <class E, class... S>
    class xview : public xexpression<xview<E, S...>>
    {

    public:

        using self_type = xview<E, S...>;
        using expression_type = E;

        using value_type = typename E::value_type;
        using reference = typename E::reference;
        using const_reference = typename E::const_reference;
        using pointer = typename E::pointer;
        using const_pointer = typename E::const_pointer;
        using size_type = typename E::size_type;
        using difference_type = typename E::difference_type;

        using shape_type = xshape<size_type>;
        using strides_type = xstrides<size_type>;
        using slice_type = std::tuple<S...>;

        using stepper = xview_stepper<E, S...>;
        using const_stepper = xview_stepper<const E, S...>;

        using iterator = xiterator<stepper>;
        using const_iterator = xiterator<const_stepper>;
        
        using closure_type = const self_type&;

        template <class... SL>
        xview(E& e, SL&&... slices) noexcept;

        size_type dimension() const noexcept;

        shape_type shape() const noexcept;
        slice_type slices() const noexcept;

        template <class... Args>
        reference operator()(Args... args);

        template <class... Args>
        const_reference operator()(Args... args) const;

        bool broadcast_shape(shape_type& shape) const;
        bool is_trivial_broadcast(const strides_type& strides) const;

        iterator begin();
        iterator end();

        const_iterator begin() const;
        const_iterator end() const;
        const_iterator cbegin() const;
        const_iterator cend() const;

        iterator xbegin(const shape_type& shape);
        iterator xend(const shape_type& shape);

        const_iterator xbegin(const shape_type& shape) const;
        const_iterator xend(const shape_type& shape) const;
        const_iterator cxbegin(const shape_type& shape) const;
        const_iterator cxend(const shape_type& shape) const;

        stepper stepper_begin(const shape_type& shape);
        stepper stepper_end(const shape_type& shape);

        const_stepper stepper_begin(const shape_type& shape) const;
        const_stepper stepper_end(const shape_type& shape) const;

    private:

        E& m_e;
        slice_type m_slices;
        shape_type m_shape;

        template <size_type... I, class... Args>
        reference access_impl(std::index_sequence<I...>, Args... args);

        template <size_type... I, class... Args>
        const_reference access_impl(std::index_sequence<I...>, Args... args) const;

        template <size_type I, class... Args>
        std::enable_if_t<(I<sizeof...(S)), size_type> index(Args... args) const;

        template <size_type I, class... Args>
        std::enable_if_t<(I>=sizeof...(S)), size_type> index(Args... args) const;

        template<size_type I, class T, class... Args>
        size_type sliced_access(const xslice<T>& slice, Args... args) const;

        template<size_type I, class T, class... Args>
        disable_xslice<T, size_type> sliced_access(const T& squeeze, Args...) const;

    };

    template <class E, class... S>
    xview<E, std::remove_reference_t<S>...> make_xview(E& e, S&&... slices);

    /*****************************
     * xview_stepper declaration *
     *****************************/

    namespace detail
    {
        template <class V>
        struct get_stepper_impl
        {
            using expression_type = typename V::expression_type;
            using type = typename expression_type::stepper;
        };

        template <class V>
        struct get_stepper_impl<const V>
        {
            using expression_type = typename V::expression_type;
            using type = typename expression_type::const_stepper;
        };
    }

    template <class V>
    using get_stepper = typename detail::get_stepper_impl<V>::type;

    template <class E, class... S>
    class xview_stepper
    {

    public:

        using view_type = std::conditional_t<std::is_const<E>::value,
                                             xview<E, S...>,
                                             const xview<E, S...>>;
        using substepper_type = get_stepper<view_type>;

        using value_type = typename substepper_type::value_type;
        using reference = typename substepper_type::reference;
        using pointer = typename substepper_type::pointer;
        using difference_type = typename substepper_type::difference_type;
        using size_type = typename view_type::size_type;

        xview_stepper(view_type* view, substepper_type it, size_type offset);

        reference operator*() const;

        void step(size_type dim, size_type n = 1);
        void step_back(size_type dim, size_type n = 1);
        void reset(size_type dim);

        void to_end();

        bool equal(const xview_stepper& rhs) const;

    private:

        view_type* p_view;
        substepper_type m_it;
        size_type m_offset;
    };

    template <class E, class... S>
    bool operator==(const xview_stepper<E, S...>& lhs,
                    const xview_stepper<E, S...>& rhs);

    template <class E, class... S>
    bool operator!=(const xview_stepper<E, S...>& lhs,
                    const xview_stepper<E, S...>& rhs);

    /********************************
     * helper functions declaration *
     ********************************/

    // number of integral types in the specified sequence of types
    template <class... S>
    constexpr std::size_t integral_count();

    // number of integral types in the specified sequence of types before specified index.
    template <class... S>
    constexpr std::size_t integral_count_before(std::size_t i);

    // index in the specified sequence of types of the ith non-integral type.
    template <class... S>
    constexpr std::size_t integral_skip(std::size_t i);

    /************************
     * xview implementation *
     ************************/

    template <class E, class... S>
    template <class... SL>
    inline xview<E, S...>::xview(E& e, SL&&... slices) noexcept
        : m_e(e), m_slices(std::forward<SL>(slices)...)
    {
        auto func = [](const auto& s) { return get_size(s); };
        m_shape.resize(dimension());
        for (size_type i = 0; i != dimension(); ++i)
        {
            size_type index = integral_skip<S...>(i);
            if (index < sizeof...(S))
            {
                m_shape[i] = apply<std::size_t>(index, func, m_slices);
            }
            else
            {
                m_shape[i] = m_e.shape()[index];
            }
        }
    }

    template <class E, class... S>
    inline auto xview<E, S...>::dimension() const noexcept -> size_type
    {
        return m_e.dimension() - integral_count<S...>();
    }

    template <class E, class... S>
    inline auto xview<E, S...>::shape() const noexcept -> shape_type
    {
        return m_shape;
    }

    template <class E, class... S>
    inline auto xview<E, S...>::slices() const noexcept -> slice_type
    {
        return m_slices;
    }

    template <class E, class... S>
    template <class... Args>
    inline auto xview<E, S...>::operator()(Args... args) -> reference
    {
        return access_impl(std::make_index_sequence<sizeof...(Args) + integral_count<S...>()>(), args...);
    }

    template <class E, class... S>
    template <class... Args>
    inline auto xview<E, S...>::operator()(Args... args) const -> const_reference
    {
        return access_impl(std::make_index_sequence<sizeof...(Args) + integral_count<S...>()>(), args...);
    }

    template <class E, class... S>
    inline bool xview<E, S...>::broadcast_shape(shape_type& shape) const
    {
        return xt::broadcast_shape(m_shape, shape);
    }

    template <class E, class... S>
    inline bool xview<E, S...>::is_trivial_broadcast(const strides_type& strides) const
    {
        return false;
    }

    template <class E, class... S>
    template <typename E::size_type... I, class... Args>
    inline auto xview<E, S...>::access_impl(std::index_sequence<I...>, Args... args) -> reference
    {
        return m_e(index<I>(args...)...);
    }

    template <class E, class... S>
    template <typename E::size_type... I, class... Args>
    inline auto xview<E, S...>::access_impl(std::index_sequence<I...>, Args... args) const -> const_reference
    {
        return m_e(index<I>(args...)...);
    }

    template <class E, class... S>
    template <typename E::size_type I, class... Args>
    inline auto xview<E, S...>::index(Args... args) const -> std::enable_if_t<(I<sizeof...(S)), size_type>
    {
        return sliced_access<I - integral_count_before<S...>(I)>(std::get<I>(m_slices), args...);
    }

    template <class E, class... S>
    template <typename E::size_type I, class... Args>
    inline auto xview<E, S...>::index(Args... args) const -> std::enable_if_t<(I>=sizeof...(S)), size_type>
    {
        return argument<I - integral_count_before<S...>(I)>(args...);
    }

    template <class E, class... S>
    template<typename E::size_type I, class T, class... Args>
    inline auto xview<E, S...>::sliced_access(const xslice<T>& slice, Args... args) const -> size_type
    {
        return slice.derived_cast()(argument<I>(args...));
    }

    template <class E, class... S>
    template<typename E::size_type I, class T, class... Args>
    inline auto xview<E, S...>::sliced_access(const T& squeeze, Args...) const -> disable_xslice<T, size_type>
    {
        return squeeze;
    }

    template <class E, class... S>
    inline xview<E, std::remove_reference_t<S>...> make_xview(E& e, S&&... slices)
    {
        return xview<E, std::remove_reference_t<S>...>(e, std::forward<S>(slices)...);
    }

    /****************
     * iterator api *
     ****************/

    template <class E, class... S>
    inline auto xview<E, S...>::begin() -> iterator
    {
        return xbegin(shape());
    }

    template <class E, class... S>
    inline auto xview<E, S...>::end() -> iterator
    {
        return xend(shape());
    }

    template <class E, class... S>
    inline auto xview<E, S...>::begin() const -> const_iterator
    {
        return xbegin(shape());
    }

    template <class E, class... S>
    inline auto xview<E, S...>::end() const -> const_iterator
    {
        return xend(shape());
    }

    template <class E, class... S>
    inline auto xview<E, S...>::cbegin() const -> const_iterator
    {
        return begin();
    }

    template <class E, class... S>
    inline auto xview<E, S...>::cend() const -> const_iterator
    {
        return end();
    }

    template <class E, class... S>
    inline auto xview<E, S...>::xbegin(const shape_type& shape) -> iterator
    {
        return iterator(stepper_begin(shape), shape);
    }

    template <class E, class... S>
    inline auto xview<E, S...>::xend(const shape_type& shape) -> iterator
    {
        return iterator(stepper_end(shape), shape);
    }

    template <class E, class... S>
    inline auto xview<E, S...>::xbegin(const shape_type& shape) const -> const_iterator
    {
        return const_iterator(stepper_begin(shape), shape);
    }

    template <class E, class... S>
    inline auto xview<E, S...>::xend(const shape_type& shape) const -> const_iterator
    {
        return const_iterator(stepper_end(shape), shape);
    }

    template <class E, class... S>
    inline auto xview<E, S...>::cxbegin(const shape_type& shape) const -> const_iterator
    {
        return xbegin(shape);
    }

    template <class E, class... S>
    inline auto xview<E, S...>::cxend(const shape_type& shape) const -> const_iterator
    {
        return xend(shape);
    }

    /***************
     * stepper api *
     ***************/

    template <class E, class... S>
    inline auto xview<E, S...>::stepper_begin(const shape_type& shape) -> stepper
    {
        size_type offset = shape.size() - dimension();
        return stepper(this, m_e.stepper_begin(m_e.shape()), offset);
    }

    template <class E, class... S>
    inline auto xview<E, S...>::stepper_end(const shape_type& shape) -> stepper
    {
        size_type offset = shape.size() - dimension();
        return stepper(this, m_e.stepper_end(m_e.shape()), offset);
    }

    template <class E, class... S>
    inline auto xview<E, S...>::stepper_begin(const shape_type& shape) const -> const_stepper
    {
        size_type offset = shape.size() - dimension();
        return const_stepper(this, m_e.stepper_begin(m_e.shape()), offset);
    }

    template <class E, class... S>
    inline auto xview<E, S...>::stepper_end(const shape_type& shape) const -> const_stepper
    {
        size_type offset = shape.size() - dimension();
        return const_stepper(this, m_e.stepper_end(m_e.shape()), offset);
    }

    /********************************
     * xview_stepper implementation *
     ********************************/

    template <class E, class... S>
    inline xview_stepper<E, S...>::xview_stepper(view_type* view, substepper_type it, size_type offset)
        : p_view(view), m_it(it), m_offset(offset)
    {
        // TODO
    }

    template <class E, class... S>
    inline auto xview_stepper<E, S...>::operator*() const -> reference
    {
        return *m_it;
    }

    template <class E, class... S>
    inline void xview_stepper<E, S...>::step(size_type dim, size_type n)
    {
        if(dim >= m_offset)
        {
            auto func = [](const auto& s) { return s.step_size(0); };
            size_type index = integral_skip<S...>(dim);
            size_type step_size = apply(index, func, p_view->slices());
            m_it.step(index, step_size * n);
        }
    }

    template <class E, class... S>
    inline void xview_stepper<E, S...>::step_back(size_type dim, size_type n)
    {
        if(dim >= m_offset)
        {
            auto func = [](const auto& s) { return s.step_size(0); };
            size_type index = integral_skip<S...>(dim);
            size_type step_size = apply(index, func, p_view->slices());
            m_it.step_back(index, step_size * n);
        }
    }

    template <class E, class... S>
    inline void xview_stepper<E, S...>::reset(size_type dim)
    {
        if(dim >= m_offset)
        {
            auto size_func = [](const auto& s) { return get_size(s); };
            auto step_func = [](const auto& s) { return s.step_size(0); };
            size_type index = integral_skip<S...>(dim);
            size_type size = apply(index, size_func, p_view->slices());
            size_type step_size = apply(index, step_func, p_view->slices());
            m_it.step_back(index, step_size * size);
        }
    }

    template <class E, class... S>
    inline void xview_stepper<E, S...>::to_end()
    {
        m_it.to_end();
    }

    template <class E, class... S>
    inline bool xview_stepper<E, S...>::equal(const xview_stepper& rhs) const
    {
        return p_view == rhs.p_view && m_it == rhs.m_it && m_offset == rhs.m_offset;
    }

    template <class E, class... S>
    inline bool operator==(const xview_stepper<E, S...>& lhs,
                           const xview_stepper<E, S...>& rhs)
    {
        return lhs.equal(rhs);
    }

    template <class E, class... S>
    inline bool operator!=(const xview_stepper<E, S...>& lhs,
                           const xview_stepper<E, S...>& rhs)
    {
        return !(lhs.equal(rhs));
    }

    /************************
     * count integral types *
     ************************/

    namespace detail
    {

        template <class T, class... S>
        struct integral_count_impl
        {
            static constexpr std::size_t count(std::size_t i) noexcept
            {
                return i ? (integral_count_impl<S...>::count(i - 1) + (std::is_integral<std::remove_reference_t<T>>::value ? 1 : 0)) : 0;
            }
        };

        template <>
        struct integral_count_impl<void>
        {
            static constexpr std::size_t count(std::size_t i) noexcept
            {
                return i;
            }
        };
    }

    template <class... S>
    constexpr std::size_t integral_count()
    {
        return detail::integral_count_impl<S..., void>::count(sizeof...(S));
    }

    template <class... S>
    constexpr std::size_t integral_count_before(std::size_t i)
    {
        return detail::integral_count_impl<S..., void>::count(i);
    }

    /**********************************
     * index of ith non-integral type *
     **********************************/

    namespace detail
    {

        template <class T, class... S>
        struct integral_skip_impl
        {
            static constexpr std::size_t count(std::size_t i) noexcept
            {
                return i == 0 ? count_impl() : count_impl(i);
            }

        private:

            static constexpr std::size_t count_impl(std::size_t i) noexcept
            {
                return 1 + (
                    std::is_integral<std::remove_reference_t<T>>::value ?
                        integral_skip_impl<S...>::count(i) :
                        integral_skip_impl<S...>::count(i - 1)
                );
            }

            static constexpr std::size_t count_impl() noexcept
            {
                return std::is_integral<std::remove_reference_t<T>>::value ? 1 + integral_skip_impl<S...>::count(0) : 0;
            }
        };

        template <>
        struct integral_skip_impl<void>
        {
            static constexpr std::size_t count(std::size_t i) noexcept
            {
                return i;
            }
        };
    }

    template <class... S>
    constexpr std::size_t integral_skip(std::size_t i)
    {
        return detail::integral_skip_impl<S..., void>::count(i);
    }
}

#endif

