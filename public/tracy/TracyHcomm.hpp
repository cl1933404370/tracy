#ifndef __TRACY_CHROME_EXPORT_HPP__
#define __TRACY_CHROME_EXPORT_HPP__

#include "Tracy.hpp"

#ifndef TRACY_SAVE_NO_SEND

#  define ChromeSetOutputCallback( cb )
#  define ChromeFlushToCallback()
inline void ChromeTraceDump() {}

#else

#  include <thread>
#  include <cstdio>
#  include <exception>
#  ifdef _WIN32
#    include <windows.h>
#  else
#    include <unistd.h>
#  endif

#  ifndef TRACY_ENABLE

#    define ChromeZoneScoped
#    define ChromeZoneNamed( name )
#    define ChromeFrameMark
#    define ChromeFrameMarkNamed( name )
#    define ChromePlot( name, val )
#    define ChromeSetThreadName( name )
#    define ChromeSetOutputCallback( cb )
#    define ChromeFlushToCallback()

#  else

#    include "../client/TracyLiteAll.hpp"

#    define ChromeSetOutputCallback( cb )                                 \
        do                                                                \
        {                                                                 \
            tracy_dump::ChromeTracer::Instance().SetOutputCallback( cb ); \
        } while( 0 )

#    define ChromeFlushToCallback()                                 \
        do                                                          \
        {                                                           \
            tracy_dump::ChromeTracer::Instance().FlushToCallback(); \
        } while( 0 )

#    define ZoneNamedLite( varname, active )                                                                                                                   \
        static constexpr tracylite::SourceLocationDataLite TracyConcat(__tracy_lite_source_location,TracyLine) { nullptr, TracyFunction, TraceFile, (uint32_t)TracyLine, 0 }; \
        tracylite::ScopedZoneLiteIf varname( &TracyConcat(__tracy_lite_source_location,TracyLine), active )

#    define ZoneNamedLiteN( varname, name, active )                                                                                                            \
        static constexpr tracylite::SourceLocationDataLite TracyConcat(__tracy_lite_source_location,TracyLine) { name, TracyFunction, TraceFile, (uint32_t)TracyLine, 0 };    \
        tracylite::ScopedZoneLiteIf varname( &TracyConcat(__tracy_lite_source_location,TracyLine), active )

#    define ZoneNamedLiteC( varname, color, active )                                                                                                           \
        static constexpr tracylite::SourceLocationDataLite TracyConcat(__tracy_lite_source_location,TracyLine) { nullptr, TracyFunction, TraceFile, (uint32_t)TracyLine, color }; \
        tracylite::ScopedZoneLiteIf varname( &TracyConcat(__tracy_lite_source_location,TracyLine), active )

#    define ZoneNamedLiteNC( varname, name, color, active )                                                                                                    \
        static constexpr tracylite::SourceLocationDataLite TracyConcat(__tracy_lite_source_location,TracyLine) { name, TracyFunction, TraceFile, (uint32_t)TracyLine, color };    \
        tracylite::ScopedZoneLiteIf varname( &TracyConcat(__tracy_lite_source_location,TracyLine), active )

#    ifdef ZoneNamed
#      undef ZoneNamed
#    endif
#    ifdef ZoneNamedN
#      undef ZoneNamedN
#    endif
#    ifdef ZoneNamedC
#      undef ZoneNamedC
#    endif
#    ifdef ZoneNamedNC
#      undef ZoneNamedNC
#    endif
#    ifdef ZoneScoped
#      undef ZoneScoped
#    endif
#    ifdef ZoneScopedN
#      undef ZoneScopedN
#    endif
#    ifdef ZoneScopedC
#      undef ZoneScopedC
#    endif
#    ifdef ZoneScopedNC
#      undef ZoneScopedNC
#    endif

#    ifdef ZoneTransient
#      undef ZoneTransient
#    endif
#    ifdef ZoneTransientN
#      undef ZoneTransientN
#    endif
#    ifdef ZoneTransientNC
#      undef ZoneTransientNC
#    endif
#    ifdef ZoneTransientS
#      undef ZoneTransientS
#    endif
#    ifdef ZoneTransientNS
#      undef ZoneTransientNS
#    endif

#    define ZoneTransient( varname, active ) ZoneNamedLite( varname, active )
#    define ZoneTransientN( varname, name, active ) ZoneNamedLiteN( varname, name, active )
#    define ZoneTransientNC( varname, name, color, active ) ZoneNamedLiteNC( varname, name, color, active )
#    define ZoneTransientS( varname, depth, active ) ZoneNamedLite( varname, active )
#    define ZoneTransientNS( varname, name, depth, active ) ZoneNamedLiteN( varname, name, active )

#    ifdef TracyPlot
#      undef TracyPlot
#    endif
#    ifdef FrameMark
#      undef FrameMark
#    endif
#    ifdef FrameMarkNamed
#      undef FrameMarkNamed
#    endif
#    ifdef TracyAlloc
#      undef TracyAlloc
#    endif
#    ifdef TracyFree
#      undef TracyFree
#    endif
#    ifdef TracyAllocN
#      undef TracyAllocN
#    endif
#    ifdef TracyFreeN
#      undef TracyFreeN
#    endif

#    define TracyPlot( name, val ) tracylite::Collector::Instance().Counter( name, static_cast<double>( val ) )
#    define FrameMark tracylite::Collector::Instance().Instant( "FrameMark" )
#    define FrameMarkNamed( name ) tracylite::Collector::Instance().Instant( name )
#    define TracyAlloc( ptr, size ) tracylite::Collector::MemAlloc( ptr, size )
#    define TracyFree( ptr ) tracylite::Collector::MemFree( ptr )
#    define TracyAllocN( ptr, size, name ) tracylite::Collector::MemAlloc( ptr, size )
#    define TracyFreeN( ptr, name ) tracylite::Collector::MemFree( ptr )

#    define ZoneNamed( varname, active ) ZoneNamedLite( varname, active )
#    define ZoneNamedN( varname, name, active ) ZoneNamedLiteN( varname, name, active )
#    define ZoneNamedC( varname, color, active ) ZoneNamedLiteC( varname, color, active )
#    define ZoneNamedNC( varname, name, color, active ) ZoneNamedLiteNC( varname, name, color, active )

#    ifdef SuppressVarShadowWarning
#      define ZoneScoped SuppressVarShadowWarning( ZoneNamed( ___tracy_scoped_zone, true ) )
#      define ZoneScopedN( name ) SuppressVarShadowWarning( ZoneNamedN( ___tracy_scoped_zone, name, true ) )
#      define ZoneScopedC( color ) SuppressVarShadowWarning( ZoneNamedC( ___tracy_scoped_zone, color, true ) )
#      define ZoneScopedNC( name, color ) SuppressVarShadowWarning( ZoneNamedNC( ___tracy_scoped_zone, name, color, true ) )
#    else
#      define ZoneScoped ZoneNamed( ___tracy_scoped_zone, true )
#      define ZoneScopedN( name ) ZoneNamedN( ___tracy_scoped_zone, name, true )
#      define ZoneScopedC( color ) ZoneNamedC( ___tracy_scoped_zone, color, true )
#      define ZoneScopedNC( name, color ) ZoneNamedNC( ___tracy_scoped_zone, name, color, true )
#    endif

inline void ChromeTraceDump()
{
    try
    {
        tracylite::DumpManager::Instance().ExportNow();
    }
    catch( const std::exception& e )
    {
        std::printf( "ChromeTraceDump exception: %s\n", e.what() );
    }
    catch( ... )
    {
        std::printf( "ChromeTraceDump unknown exception\n" );
    }
}

#  endif
#endif // !TRACY_SAVE_NO_SEND

#endif // __TRACY_CHROME_EXPORT_HPP__
