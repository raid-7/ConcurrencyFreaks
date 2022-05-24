#pragma once

#include <tuple>
#include <stdexcept>

namespace mpg {

template<class ValueType, template<ValueType> class C>
struct ParameterSet {
    template<ValueType... values>
    struct Parameters {
    private:
        template<class R, class F, size_t index>
        static constexpr R apply(ValueType value, F&& f) {
            if constexpr(index == sizeof...(values)) {
                throw std::logic_error("Index out of bounds");
            } else {
                if (value == Values[index]) {
                    return std::forward<F>(f)(Instance<index>{});
                } else {
                    return apply<R, F, index + 1>(value, std::forward<F>(f));
                }
            }
        }

    public:
        template<size_t i>
        using Instance = std::tuple_element_t<i, std::tuple<C<values>...>>;

        static constexpr std::array Values = {values...};

        template<class R, class F>
        static constexpr R apply(ValueType value, F&& f) {
            return apply<R, F, 0>(value, std::forward<F>(f));
        }

        template<class R, class F>
        static constexpr R apply_index(size_t index, F&& f) {
            return apply<R, F>(Values[index], std::forward<F>(f));
        }
    };
};

namespace {
template<class ValueType>
struct IntegralConstant {
    template<ValueType value>
    using Constant = std::integral_constant<ValueType, value>;
};
}

template<class ValueType>
using ConstantSet = ParameterSet<ValueType, IntegralConstant<ValueType>::template Constant>;

}
