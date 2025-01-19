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

// String-like thing with fixed or dynamic size
template <size_t Size = size_t(-1)>
struct concat_res {
	explicit concat_res(size_t) noexcept
	{
	}

	char data[Size];
	size_t size;
	char* ptr() noexcept { return data; }
	const char* c_str() const noexcept { return data; }
	std::string_view get() const noexcept { return {data, size}; }
};

template <>
struct concat_res<size_t(-1)> {
	concat_res() noexcept = default;

	// Should be copyable so it's not a unique_ptr
	explicit concat_res(size_t size)
		: data({make_smart_ptr<char[], false>(size + 1)})
	{
	}

	const_string data;
	size_t size = 0;
	char* ptr() noexcept { return data.data(); }
	const char* c_str() const noexcept { return data.c_str(); }
	std::string_view get() const noexcept { return {data.c_str(), size}; }
};

template <typename T>
constexpr bool concat_fixed()
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

template <typename T>
constexpr size_t concat_sz()
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
constexpr size_t concat_sz(const T& arg)
{
	if constexpr (std::is_integral_v<T>)
		return concat_sz<T>();
	else
		return strnlen(std::data(arg), std::size(arg));
}

inline size_t concat_sz(const char* arg)
{
	return strlen(arg);
}

template <typename T>
constexpr void concat_append(char*& ptr, const T& arg, [[maybe_unused]] size_t len)
{
	if constexpr (std::is_integral_v<T>) {
		ptr = std::to_chars(ptr, ptr + concat_sz<T>(), arg).ptr;
	} else {
		memcpy(ptr, std::data(arg), len);
		ptr += len;
	}
}

inline void concat_append(char*& ptr, const char* arg, size_t len)
{
	memcpy(ptr, arg, len);
	ptr += len;
}

template <bool Alloc = false, typename... Args, bool Dyn = (Alloc || ... || !concat_fixed<Args>())>
static constexpr auto concat(const Args&... args) {
	// Compute static size
	constexpr size_t Size = Dyn ? size_t(-1) : (concat_sz<Args>() + ... + size_t(1));
	// Compute dynamic size
	const size_t sizes[]{concat_sz(args)...};
	size_t size = 0;
	for (size_t i = 0; i < sizeof...(Args); i++)
		size += sizes[i];
	if constexpr (Dyn)
		if (!size)
			return const_string{};
	concat_res<Size> result(size);
	char* ptr = result.ptr();
	size = 0;
	(concat_append(ptr, args, sizes[size++]), ...);
	result.size = ptr - result.ptr();
	*ptr = 0;
	if constexpr (Dyn && (std::is_integral_v<Args> || ...))
		result.data.ptr.shrink(result.size + 1);
	if constexpr (Dyn)
		return std::move(result.data);
	else
		return result;
}
