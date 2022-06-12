#pragma once

#include <tuple>
#include <stdexcept>

namespace mpg {

template<class C>
struct RebindTemplate;

template<template<class> class T, class P>
struct RebindTemplate<T<P>> {
    template<class P2>
    using To = T<P2>;
};

template<class... T>
struct TypeSet {
    using Tuple = std::tuple<T...>;

    template<size_t i>
    using Get = std::tuple_element_t<i, Tuple>;

    template<class F>
    static constexpr void foreach(F func) {
        (func.template operator()<T>(), ...);
    }
};

template<class ValueType>
struct ParameterSet {
    template<ValueType... values>
    struct Of {
    public:
        static constexpr std::array Values = { values... };

        using ValueConstants = TypeSet<std::integral_constant<ValueType, values>...>;

        template<class F>
        static constexpr void foreach(F f) {
            ValueConstants::foreach([&f]<class C>() {
                f.template operator()<C::value>();
            });
        }

        template<class F>
        static constexpr void apply(ValueType value, F&& f) {
            foreach([value, f = std::forward<F>(f)]<ValueType v>() mutable {
                if (v == value) {
                    std::forward<F>(f).template operator()<v>();
                }
            });
        }
    };
};

}
