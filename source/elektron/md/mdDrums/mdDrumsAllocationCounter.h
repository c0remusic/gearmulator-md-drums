#pragma once

// For the tests that require code to allocate nothing, as what runs on the audio thread must not: the allocations made
// on the calling thread, counted while asked. It replaces the global operator new and delete, so include it in exactly
// one translation unit of a test executable, never in a library.

#include <cstddef>
#include <cstdlib>
#include <new>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace
{
	thread_local bool g_countAllocations = false;
	thread_local size_t g_allocations = 0;

	// The allocations _function makes on this thread, and only those: count around the calls in question alone
	template<class TFunction> size_t countAllocations(TFunction&& _function)
	{
		g_allocations = 0;
		g_countAllocations = true;
		_function();
		g_countAllocations = false;
		return g_allocations;
	}
}

void* operator new(const std::size_t _size)
{
	if(g_countAllocations)
		++g_allocations;
	if(void* const result = std::malloc(_size ? _size : 1))
		return result;
	throw std::bad_alloc();
}

void* operator new[](const std::size_t _size) { return ::operator new(_size); }

void* operator new(const std::size_t _size, const std::align_val_t _alignment)
{
	if(g_countAllocations)
		++g_allocations;
#if defined(_MSC_VER)
	if(void* const result = _aligned_malloc(_size ? _size : 1, static_cast<std::size_t>(_alignment)))
		return result;
#else
	void* result = nullptr;
	if(posix_memalign(&result, static_cast<std::size_t>(_alignment), _size ? _size : 1) == 0)
		return result;
#endif
	throw std::bad_alloc();
}

void* operator new[](const std::size_t _size, const std::align_val_t _alignment)
{
	return ::operator new(_size, _alignment);
}

void operator delete(void* const _pointer) noexcept { std::free(_pointer); }
void operator delete[](void* const _pointer) noexcept { std::free(_pointer); }
void operator delete(void* const _pointer, std::size_t) noexcept { std::free(_pointer); }
void operator delete[](void* const _pointer, std::size_t) noexcept { std::free(_pointer); }
#if defined(_MSC_VER)
void operator delete(void* const _pointer, std::align_val_t) noexcept { _aligned_free(_pointer); }
#else
void operator delete(void* const _pointer, std::align_val_t) noexcept { std::free(_pointer); }
#endif

void operator delete[](void* const _pointer, const std::align_val_t _alignment) noexcept
{
	::operator delete(_pointer, _alignment);
}

void operator delete(void* const _pointer, std::size_t, const std::align_val_t _alignment) noexcept
{
	::operator delete(_pointer, _alignment);
}

void operator delete[](void* const _pointer, std::size_t, const std::align_val_t _alignment) noexcept
{
	::operator delete(_pointer, _alignment);
}
