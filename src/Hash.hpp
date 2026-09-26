#pragma once

#include <cstddef>
#include <functional>
#include <hyprutils/math/Vector2D.hpp>

// std::hash has no built-in specialization for Vector2D; add one so it folds
// into hashCombine like any other hashable value below.
template <>
struct std::hash<Hyprutils::Math::Vector2D> {
    size_t operator()(const Hyprutils::Math::Vector2D& value) const noexcept {
        size_t seed = std::hash<double>{}(value.x);
        seed ^= std::hash<double>{}(value.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

namespace Hash {

// Boost-style variadic hash fold: mixes each argument into seed in turn via
// std::hash<T>, in the order given. Used by CGlassDecoration's render-order
// fingerprint to fold window identity/geometry/alpha into a per-monitor
// running hash without allocating.
template <typename T, typename... Rest>
inline void hashCombine(size_t& seed, const T& value, const Rest&... rest) {
    seed ^= std::hash<T>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    if constexpr (sizeof...(rest) > 0)
        hashCombine(seed, rest...);
}

} // namespace Hash
