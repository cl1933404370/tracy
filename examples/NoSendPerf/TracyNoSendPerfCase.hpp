#ifndef TRACY_NO_SEND_PERF_CASE_HPP
#define TRACY_NO_SEND_PERF_CASE_HPP

#include <cstddef>

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

#endif
