#include <cstdio>
#include <cstring>
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
int RunZoneBenchmark()
{
    long long elapsedNs = 0;
    double nsPerPair = 0.0;
    const int zoneRc = TracyNoSend_RunZoneBeginEndPerf(200000, &elapsedNs, &nsPerPair);
    std::printf("[TracyNoSendPerfRunner] zone rc=%d elapsed_ns=%lld ns_per_pair=%.2f\n", zoneRc, elapsedNs, nsPerPair);
    return zoneRc == 0 ? 0 : 1;
}

int RunExportBenchmark()
{
    const std::string outputPath = "tracylite_export_perf.perfetto-trace";
    long long exportElapsedUs = 0;
    long long serializeElapsedUs = 0;
    long long writeElapsedUs = 0;
    size_t fileSize = 0;
    double throughputMbPerSec = 0.0;
    const int fileRc = TracyNoSend_RunExportToFilePerf(4,
                                                       10000,
                                                       outputPath.c_str(),
                                                       &exportElapsedUs,
                                                       &serializeElapsedUs,
                                                       &writeElapsedUs,
                                                       &fileSize,
                                                       &throughputMbPerSec);
    std::printf("[TracyNoSendPerfRunner] file rc=%d total_elapsed_us=%lld serialize_us=%lld write_us=%lld file_size=%zu write_throughput_mb_s=%.2f\n",
                fileRc,
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

    if( argc > 1 )
    {
        if( std::strcmp( argv[1], "zone" ) == 0 ) return RunZoneBenchmark();
        if( std::strcmp( argv[1], "export" ) == 0 ) return RunExportBenchmark();

        std::fprintf( stderr, "Usage: %s [zone|export]\n", argv[0] );
        return 2;
    }

    const int zoneRc = RunZoneBenchmark();
    const int fileRc = RunExportBenchmark();
    return ( zoneRc == 0 && fileRc == 0 ) ? 0 : 1;
}
