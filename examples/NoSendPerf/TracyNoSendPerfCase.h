#pragma once

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
                                                    size_t* fileSize,
                                                    double* throughputMbPerSec);
