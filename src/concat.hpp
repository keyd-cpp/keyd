/*
 * String concatenation utility with optional allocation.
 * License: MIT (see also: LICENSE).
 * © 2025 Nekotekina.
 */
#pragma once

#include <string.h>
#include <type_traits>
#include <memory>
#include <charconv>
#include <array>
#include <string_view>
#include "utils.hpp"

using size_t = decltype(sizeof(char));

// String-like thing with fixed size
template <size_t Size>
struct concat_res {
	char _data[Size];
	size_t _sz;

	const char* c_str() const noexcept { return _data; }
	std::string_view get() const noexcept { return {_data, _sz}; }
	operator std::string_view() const noexcept { return {_data, _sz}; }

	const char* data() const noexcept { return _data; }
	size_t size() const noexcept { return _sz; }
};

template <typename T>
constexpr bool concat_fixed() noexcept
{
	if constexpr (std::is_trivially_copyable_v<T>)
		if constexpr (std::is_integral_v<T> || std::is_array_v<T>)
			return true;
		else if constexpr (std::is_pointer_v<T>)
			return false;
		else
			return T().size();
	else
		return false;
}

// Possibly static size of an argument of type T
template <typename T>
constexpr size_t concat_sz() noexcept
{
	if constexpr (std::is_integral_v<T>)
		return sizeof(T) == 1 ? 4 : sizeof(T) * 3;
	else if constexpr (std::is_array_v<T>)
		return sizeof(T);
	else if constexpr (std::is_pointer_v<T>)
		return 0;
	else if constexpr (std::is_trivially_copyable_v<T>)
		return T().size();
	else
		return 0;
}

template <typename T>
[[gnu::always_inline]] constexpr auto concat_arg(const T& arg) noexcept
{
	// Transform arg into a copyable value or string_view
	if constexpr (std::is_integral_v<T>) {
		constexpr size_t max_sz = concat_sz<T>();
		concat_res<max_sz> res;
		res._sz = std::to_chars(res._data, res._data + max_sz, arg).ptr - res._data;
		return res;
	} else {
		const auto ptr = std::data(arg);
		return std::string_view(ptr, strnlen(ptr, std::size(arg)));
	}
}

template <size_t N>
[[gnu::always_inline]] inline std::string_view concat_arg(const concat_res<N>& s) noexcept
{
	return s.get();
}

[[gnu::always_inline]] inline std::string_view concat_arg(const char* arg) noexcept
{
	return arg;
}

[[gnu::always_inline]] inline size_t concat_impl(char* ptr, const auto&... views) noexcept
{
	// Copy args and terminate with 0
	((memcpy(ptr, views.data(), views.size()), ptr += views.size()), ...);
	*ptr = '\0';
	// Return final size
	return (views.size() + ...);
}

[[gnu::always_inline]] inline const_string concat_impl_dyn(const auto&... views)
{
	// Compute dynamic size
	auto ptr = make_smart_ptr<char[], false>((views.size() + ... + 1));
	concat_impl(ptr.get(), views...);
	return {ptr};
}

template <bool Alloc = false, typename... Args, bool Dyn = (Alloc || ... || !concat_fixed<Args>())>
static constexpr auto concat(const Args&... args)
{
	if constexpr (Dyn) {
		return concat_impl_dyn(concat_arg(args)...);
	} else {
		// Compute static size
		concat_res<(concat_sz<Args>() + ... + size_t(1))> result;
		result._sz = concat_impl(result._data, concat_arg(args)...);
		return result;
	}
}
