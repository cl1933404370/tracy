#ifndef __TRACY_CHROME_EXPORT_HPP__
#define __TRACY_CHROME_EXPORT_HPP__
#include "../client/TracyChromeExport.hpp"
#include "Tracy.hpp"
#ifndef TRACY_SAVE_NO_SEND
#  define ChromeSetOutputCallback( cb )
#  define ChromeFlushToCallback()
inline void ChromeTraceDump() {}
#else
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

// 存在dlopen场景，析构函数无法被调用，导致日志无法正常输出，因此提供一个外部接口，强制调用日志dump函数
inline void ChromeTraceDump()
{
    tracy_dump::ChromeInit::DumpLog();
}
#  endif
#endif // !TRACY_SAVE_NO_SEND
#endif // __TRACY_CHROME_EXPORT_HPP__
