/*
 * Smart pointer for single-thread simple use cases.
 * Aux allocator interface.
 * License: MIT (see also: LICENSE).
 * © 2025 Nekotekina.
 */
#pragma once

#include <type_traits>
#include <cassert>
#include <memory>
#include <string_view>

using size_t = decltype(sizeof(char));

// Refcounted not-so-smart pointer, either single object of non-trivial type, or an array of trivial type
template <typename PT>
class smart_ptr final {
	// Can be made thread-safe by using atomic operations but it will be slower
	unsigned& refs() const noexcept
	{
		return *(reinterpret_cast<unsigned*>(this->ptr) - 1);
	}

	unsigned& _size() const noexcept
	{
		return *(reinterpret_cast<unsigned*>(this->ptr) - 2);
	}

	template <typename TT, bool, typename... Args>
	friend smart_ptr<TT> make_smart_ptr(size_t, const Args&...);

public:
	static constexpr bool is_sized = std::is_unbounded_array_v<PT>;
	using T = std::conditional_t<is_sized, std::remove_extent_t<PT>, PT>;
	static constexpr size_t cb_block = is_sized ? sizeof(int[2]) : sizeof(int);
	static constexpr size_t cb_align = alignof(T) > alignof(int) ? alignof(T) : alignof(int);
	static constexpr size_t cb_size = cb_align > cb_block ? cb_align : cb_block;

	smart_ptr() noexcept = default;

	smart_ptr(const smart_ptr& r) noexcept
		: ptr(r.ptr)
	{
		if (this->ptr)
			refs()++;
	}

	smart_ptr(smart_ptr&& r) noexcept
		: ptr(r.ptr)
	{
		r.ptr = nullptr;
	}

	smart_ptr& operator=(const smart_ptr& r) noexcept
	{
		return *this = smart_ptr(r);
	}

	smart_ptr& operator=(smart_ptr&& r) noexcept
	{
		std::swap(this->ptr, r.ptr);
		return *this;
	}

	~smart_ptr()
	{
		if (this->ptr && !--refs()) {
			// This is not reverse order of construction, who cares.
			if constexpr (is_sized) {
				for (size_t i = 0; i < this->size(); i++)
					this->ptr[i].~T();
			} else {
				this->ptr->~T();
			}
			::operator delete[](reinterpret_cast<std::byte*>(this->ptr) - cb_size, std::align_val_t{cb_align});
		}
	}

	T& operator*() const noexcept
	{
		return *this->ptr;
	}

	T* operator->() const noexcept
	{
		return this->ptr;
	}

	T& operator[](size_t n) const noexcept
	{
		return this->ptr[n];
	}

	explicit operator bool() const noexcept
	{
		return this->ptr != nullptr;
	}

	T* get() const noexcept
	{
		return this->ptr;
	}

	size_t size() const noexcept requires(is_sized)
	{
		if (!this->ptr)
			return 0;
		return this->_size();
	}

	T* begin() const noexcept requires(is_sized)
	{
		return this->ptr;
	}

	T* end() const noexcept requires(is_sized)
	{
		return this->ptr ? this->ptr + this->size() : nullptr;
	}

	void shrink(size_t size) noexcept requires(is_sized && std::is_trivially_destructible_v<T>);

	bool operator==(const smart_ptr&) const noexcept = default;

private:
	T* ptr = nullptr;
};

template <typename TT, bool Init = true, typename... Args>
smart_ptr<TT> make_smart_ptr(size_t count = 1, const Args&... args)
{
	using T = smart_ptr<TT>::T;
	static_assert(std::is_nothrow_default_constructible_v<T>);
	if constexpr (!std::is_trivially_destructible_v<T> && !smart_ptr<TT>::is_sized)
		assert(count == 1);

	constexpr size_t cb_size = smart_ptr<TT>::cb_size;
	std::byte* bytes = static_cast<std::byte*>(::operator new[](cb_size + count * sizeof(T), std::align_val_t{smart_ptr<TT>::cb_align}));
	memset(bytes, 0, cb_size); // Clear control block
	T* arr = reinterpret_cast<T*>(bytes + cb_size);

	// For non-trivial objects Init arg is irrelevant
	if constexpr (sizeof...(Args) > 0) {
		for (size_t i = 0; i < count; i++) {
			new(arr + i) T(args...);
		}
	} else if constexpr (Init)
		std::uninitialized_value_construct_n(arr, count);
	else
		std::uninitialized_default_construct_n(arr, count);

	smart_ptr<TT> r;
	r.ptr = std::launder(arr);
	r.refs()++;
	if constexpr (smart_ptr<TT>::is_sized)
		r._size() = count;
	return r;
}

// Make smart_ptr "null-terminated" and initialize from src
template <typename S>
auto make_smart_buf(const S& src, size_t add = 1)
{
	using T = std::decay_t<decltype(*std::begin(src))>;
	smart_ptr<T> r;
	size_t sz = std::size(src) + add;
	if (!sz)
		return r;
	r = make_smart_ptr<T, false>(sz);
	size_t i = 0;
	for (auto it = std::begin(src), end = std::end(src); it != end; it++)
		r[i++] = *it;
	std::uninitialized_value_construct_n(r.get() + (sz - add), add);
	return r;
}

// Make smart_ptr with available size() and initialize from src
template <typename S>
auto make_smart_array(const S& src, size_t add = 0)
{
	using T = std::decay_t<decltype(*std::begin(src))>;
	smart_ptr<T[]> r;
	size_t sz = std::size(src) + add;
	if (!sz)
		return r;
	r = make_smart_ptr<T[], false>(sz);
	size_t i = 0;
	for (auto it = std::begin(src), end = std::end(src); it != end; it++)
		r[i++] = *it;
	std::uninitialized_value_construct_n(r.get() + (sz - add), add);
	return r;
}

template <typename S>
auto make_buf(const S& src, size_t add = 1)
{
	using T = std::decay_t<decltype(*std::begin(src))>;
	static_assert(std::is_nothrow_default_constructible_v<T>);
	std::unique_ptr<T[]> r;
	size_t sz = std::size(src) + add;
	if (!sz)
		return r;
	r.reset(static_cast<T*>(operator new[](sz * sizeof(T), std::align_val_t{alignof(T)})));
	auto next = std::uninitialized_default_construct_n(r.get(), (sz - add));
	size_t i = 0;
	for (auto it = std::begin(src), end = std::end(src); it != end; it++)
		r[i++] = *it;
	std::uninitialized_value_construct_n(next, add);
	return r;
}

struct const_string {
	smart_ptr<char[]> ptr;

	operator std::string_view() const noexcept
	{
		if (!ptr)
			return {};
		return {ptr.get(), ptr.size() - 1};
	}

	const char* c_str() const noexcept
	{
		if (!ptr)
			return "";
		return ptr.get();
	}

	char* data() const noexcept
	{
		return ptr.get();
	}

	size_t size() const noexcept
	{
		if (!ptr)
			return 0;
		return ptr.size() - 1;
	}

	bool empty() const noexcept
	{
		return !this->size();
	}

	// Checks allocation, not size (allocation may be of empty NTS)
	explicit operator bool() const noexcept
	{
		return !!ptr;
	}

	template <typename T>
	bool operator ==(const T& rhs) const noexcept
	{
		return operator std::string_view() == std::string_view(rhs);
	}

	template <typename T>
	bool operator <(const T& rhs) const noexcept
	{
		return operator std::string_view() < std::string_view(rhs);
	}
};

inline const_string make_string(std::string_view str)
{
	if (str.empty())
		return {};
	return {make_smart_array(str, +1)};
}

class aux_alloc {
	// Global variable
	inline static bool use_aux_allocator = false;

	// Get old value
	bool old = use_aux_allocator;

	friend void* operator new(size_t, std::align_val_t);

public:
	// Try to shrink latest allocation to new_size.
	// If shrinking is not possible, does nothing.
	static void shrink(void* ptr, size_t old_size, size_t new_size) noexcept;

	void* get_head() const noexcept;
	size_t get_size() const noexcept;
	size_t get_count() const noexcept;

	aux_alloc() noexcept
	{
		use_aux_allocator = true;
	}
	aux_alloc(const aux_alloc&) = delete;
	aux_alloc& operator=(const aux_alloc&) = delete;
	~aux_alloc()
	{
		use_aux_allocator = old;
	}
};

template <typename PT>
inline void smart_ptr<PT>::shrink(size_t size) noexcept requires(is_sized && std::is_trivially_destructible_v<smart_ptr<PT>::T>)
{
	if (!this->ptr)
		return;
	if (!size) {
		*this = smart_ptr();
		return;
	}
	unsigned& sz = this->_size();
	if (size < sz) {
		// Reduce aux memory usage if possible (does nothing otherwise)
		aux_alloc::shrink(reinterpret_cast<std::byte*>(this->ptr) - cb_size, sz * sizeof(T) + cb_size, size * sizeof(T) + cb_size);
		sz = size;
	}
}
