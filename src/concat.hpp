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
#include "utils.hpp"

using size_t = decltype(sizeof(char));

// Return owning string_view-alike thing
template <size_t Size = size_t(-1)>
struct concat_res {
	explicit concat_res(size_t) noexcept
	{
	}

	char data[Size];
	size_t size;
	const char* c_str() const { return data; }
	std::string_view get() const { return {data, size}; }
};

template <>
struct concat_res<size_t(-1)> {
	// Should be copyable so it's not a unique_ptr
	explicit concat_res(size_t size)
		: data(make_smart_ptr<char>(size + 1))
	{
	}

	smart_ptr<char> data;
	size_t size;
	const char* c_str() const { return data.get(); }
	std::string_view get() const { return {data.get(), size}; }
};

template <typename T>
constexpr bool concat_fixed()
{
	if constexpr (std::is_trivially_copyable_v<T>)
		if constexpr (std::is_integral_v<T> || std::is_array_v<T>)
			return true;
		else
			return std::size(std::declval<T>());
	else
		return false;
}

template <typename T>
constexpr size_t concat_sz()
{
	if constexpr (std::is_integral_v<T>)
		return sizeof(T) == 1 ? 4 : sizeof(T) * 3;
	else if constexpr (std::is_array_v<T>)
		return sizeof(T);
	else if constexpr (std::is_trivially_copyable_v<T>)
		return std::size(std::declval<T>());
	else
		return 0;
}

template <typename T>
constexpr size_t concat_sz(const T& arg)
{
	if constexpr (std::is_integral_v<T>)
		return concat_sz<T>();
	else
		return std::size(arg);
}

template <typename T>
constexpr void concat_append(char*& ptr, const T& arg)
{
	if constexpr (std::is_integral_v<T>) {
		ptr = std::to_chars(ptr, ptr + concat_sz<T>(), arg).ptr;
	} else {
		size_t len = strnlen(std::data(arg), std::size(arg));
		memcpy(ptr, std::data(arg), len);
		ptr += len;
	}
}

template <bool Alloc = false, typename... Args, bool Dyn = (Alloc || ... || !concat_fixed<Args>())>
static constexpr auto concat(const Args&... args) {
	// Compute static size
	constexpr size_t Size = Dyn ? size_t(-1) : (concat_sz<Args>() + ... + size_t(1));
	// Compute dynamic size
	concat_res<Size> result((concat_sz(args) + ... + size_t(0)));
	char* ptr = &*result.data;
	(concat_append(ptr, args), ...);
	result.size = ptr - &*result.data;
	*ptr = 0;
	return result;
}
