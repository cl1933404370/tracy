// g++ fibers.cpp ../public/TracyClient.cpp -I../public/tracy -DTRACY_ENABLE -DTRACY_FIBERS -lpthread -ldl

#include <thread>
#include <chrono>

#include "Tracy.hpp"
#include "TracyC.h"

auto fiber = "job1";
TracyCZoneCtx zone = {};

int main()
{
    // Thread 1: enter fiber and begin a C zone, store its context in 'zone'
    std::thread t1( [] {
        TracyFiberEnter( fiber );
        // Declare a C zone ctx named 'ctx' and begin the zone with callstack depth 1
        // ReSharper disable once CppLocalVariableMayBeConst
        TracyCZone( ctx, 1 );
        // Save the ctx so another thread can end the zone (demonstration only).
        zone = ctx;
        std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
        TracyFiberLeave;
    } );
    t1.join();

    // Thread 2: enter same fiber and end the previously begun zone
    std::thread t2( [] {
        TracyFiberEnter( fiber );
        std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
        TracyCZoneEnd( zone );
        TracyFiberLeave;
    } );
    t2.join();

    return 0;
}
