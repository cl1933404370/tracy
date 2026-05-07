#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <thread>

#if defined(_WIN32)
#define TRACYNOSEND_API extern "C" __declspec(dllimport)
#else
#define TRACYNOSEND_API extern "C"
#endif

TRACYNOSEND_API int TracyNoSend_RunZoneBeginEndPerf(int iterations, long long* elapsedNs, double* nsPerPair);
TRACYNOSEND_API int TracyNoSend_RunExportToFilePerf(int threadCount,
                                                    int iterationsPerThread,
                                                    const char* outputPath,
                                                    long long* exportElapsedUs,
                                                    long long* serializeElapsedUs,
                                                    long long* writeElapsedUs,
                                                    size_t* fileSize,
                                                    double* throughputMbPerSec);

namespace
{
bool TryParsePositiveInt( const char* text, int& value )
{
    if( text == nullptr || *text == '\0' ) return false;

    char* end = nullptr;
    const long parsed = std::strtol( text, &end, 10 );
    if( end == text || *end != '\0' ) return false;
    if( parsed <= 0 || parsed > 2147483647L ) return false;

    value = static_cast<int>( parsed );
    return true;
}

int RunZoneBenchmark( int iterations )
{
    long long elapsedNs = 0;
    double nsPerPair = 0.0;
    const int zoneRc = TracyNoSend_RunZoneBeginEndPerf( iterations, &elapsedNs, &nsPerPair );
    std::printf("[TracyNoSendPerfRunner] zone rc=%d iterations=%d elapsed_ns=%lld ns_per_pair=%.2f\n",
                zoneRc,
                iterations,
                elapsedNs,
                nsPerPair);
    return zoneRc == 0 ? 0 : 1;
}

int RunExportBenchmark( int threadCount, int iterationsPerThread )
{
    const std::string outputPath = "tracylite_export_perf.perfetto-trace";
    long long exportElapsedUs = 0;
    long long serializeElapsedUs = 0;
    long long writeElapsedUs = 0;
    size_t fileSize = 0;
    double throughputMbPerSec = 0.0;
    const int fileRc = TracyNoSend_RunExportToFilePerf( threadCount,
                                                       iterationsPerThread,
                                                       outputPath.c_str(),
                                                       &exportElapsedUs,
                                                       &serializeElapsedUs,
                                                       &writeElapsedUs,
                                                       &fileSize,
                                                       &throughputMbPerSec);
    std::printf("[TracyNoSendPerfRunner] file rc=%d thread_count=%d iterations_per_thread=%d total_elapsed_us=%lld serialize_us=%lld write_us=%lld file_size=%zu write_throughput_mb_s=%.2f\n",
                fileRc,
                threadCount,
                iterationsPerThread,
                exportElapsedUs,
                serializeElapsedUs,
                writeElapsedUs,
                fileSize,
                throughputMbPerSec);
    return fileRc == 0 ? 0 : 1;
}
}

int main( int argc, char** argv )
{
    const auto cpuCount = std::thread::hardware_concurrency();
    std::printf("[TracyNoSendPerfRunner] cpu_count=%u\n", cpuCount);

    constexpr int defaultZoneIterations = 200000;
    constexpr int defaultExportThreadCount = 4;
    constexpr int defaultExportIterationsPerThread = 10000;

    if( argc > 1 )
    {
        if( std::strcmp( argv[1], "zone" ) == 0 )
        {
            int iterations = defaultZoneIterations;
            if( argc > 2 && !TryParsePositiveInt( argv[2], iterations ) )
            {
                std::fprintf( stderr, "Invalid zone iterations: %s\n", argv[2] );
                return 2;
            }
            return RunZoneBenchmark( iterations );
        }

        if( std::strcmp( argv[1], "export" ) == 0 )
        {
            int threadCount = defaultExportThreadCount;
            int iterationsPerThread = defaultExportIterationsPerThread;

            if( argc > 2 && !TryParsePositiveInt( argv[2], threadCount ) )
            {
                std::fprintf( stderr, "Invalid export threadCount: %s\n", argv[2] );
                return 2;
            }

            if( argc > 3 && !TryParsePositiveInt( argv[3], iterationsPerThread ) )
            {
                std::fprintf( stderr, "Invalid export iterationsPerThread: %s\n", argv[3] );
                return 2;
            }

            return RunExportBenchmark( threadCount, iterationsPerThread );
        }

        std::fprintf( stderr, "Usage: %s [zone [iterations]|export [threadCount [iterationsPerThread]]]\n", argv[0] );
        return 2;
    }

    const int zoneRc = RunZoneBenchmark( defaultZoneIterations );
    const int fileRc = RunExportBenchmark( defaultExportThreadCount, defaultExportIterationsPerThread );
    return ( zoneRc == 0 && fileRc == 0 ) ? 0 : 1;
}
