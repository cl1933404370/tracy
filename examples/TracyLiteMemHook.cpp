// TracyLiteMemHook.cpp
//
// Opt-in global operator new/delete hook for TracyLite memory tracking.
//
// Usage:
//   1. Copy this file into your project
//   2. Compile it alongside your other source files
//   3. All heap allocations (new, make_shared, make_unique, etc.)
//      will automatically appear as "Heap Size" counter tracks in
//      Perfetto UI — no business code changes required.
//
// To disable: simply remove this file from your build.
//
// Note: This file uses a thread_local recursion guard to prevent
// infinite recursion when TracyLite's own internals allocate memory
// (e.g., during the first GetThreadState() call per thread).

#include <cstdlib>
#include <cstdio>
#include <new>

#include <tracy/TracyHcomm.hpp>

#if defined(TRACY_ENABLE) && defined(TRACY_SAVE_NO_SEND)

#ifdef _MSC_VER
static __declspec(thread) int s_inMemHook = 0;
#else
static thread_local int s_inMemHook = 0;
#endif

void* operator new( std::size_t size )
{
    void* ptr = std::malloc( size );
    if( !ptr ) throw std::bad_alloc();
    if( !s_inMemHook )
    {
        s_inMemHook = 1;
        TracyAlloc( ptr, size );
        s_inMemHook = 0;
    }
    return ptr;
}

void* operator new( std::size_t size, const std::nothrow_t& ) noexcept
{
    void* ptr = std::malloc( size );
    if( ptr && !s_inMemHook )
    {
        s_inMemHook = 1;
        TracyAlloc( ptr, size );
        s_inMemHook = 0;
    }
    return ptr;
}

void* operator new[]( std::size_t size )
{
    void* ptr = std::malloc( size );
    if( !ptr ) throw std::bad_alloc();
    if( !s_inMemHook )
    {
        s_inMemHook = 1;
        TracyAlloc( ptr, size );
        s_inMemHook = 0;
    }
    return ptr;
}

void* operator new[]( std::size_t size, const std::nothrow_t& ) noexcept
{
    void* ptr = std::malloc( size );
    if( ptr && !s_inMemHook )
    {
        s_inMemHook = 1;
        TracyAlloc( ptr, size );
        s_inMemHook = 0;
    }
    return ptr;
}

void operator delete( void* ptr ) noexcept
{
    if( ptr && !s_inMemHook )
    {
        s_inMemHook = 1;
        TracyFree( ptr );
        s_inMemHook = 0;
    }
    std::free( ptr );
}

void operator delete( void* ptr, std::size_t ) noexcept
{
    if( ptr && !s_inMemHook )
    {
        s_inMemHook = 1;
        TracyFree( ptr );
        s_inMemHook = 0;
    }
    std::free( ptr );
}

void operator delete[]( void* ptr ) noexcept
{
    if( ptr && !s_inMemHook )
    {
        s_inMemHook = 1;
        TracyFree( ptr );
        s_inMemHook = 0;
    }
    std::free( ptr );
}

void operator delete[]( void* ptr, std::size_t ) noexcept
{
    if( ptr && !s_inMemHook )
    {
        s_inMemHook = 1;
        TracyFree( ptr );
        s_inMemHook = 0;
    }
    std::free( ptr );
}

#endif // TRACY_ENABLE && TRACY_SAVE_NO_SEND
