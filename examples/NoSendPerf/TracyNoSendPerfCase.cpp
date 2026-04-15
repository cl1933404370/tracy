#include "../../public/client/TracyLiteAll.hpp"
#if defined(TRACYLITE_PERFETTO)
#include "../../public/client/TracyLitePerfetto.hpp"
#endif

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct PerfResult
    {
        long long elapsedNs = 0;
        double nsPerPair = 0.0;
        long long exportElapsedUs = 0;
        long long serializeElapsedUs = 0;
        long long writeElapsedUs = 0;
        size_t fileSize = 0;
        double throughputMbPerSec = 0.0;
    };

    void EmitWorkload(const int threadIndex, const int iterations)
    {
        static constexpr tracylite::SourceLocationDataLite srcloc{ "PerfExportZone", "EmitWorkload", "TracyNoSendPerfCase.cpp", 1, 0 };
        for (int i = 0; i < iterations; ++i)
        {
            tracylite::Collector::ZoneBegin(&srcloc);
            tracylite::Collector::Instant((i & 1) == 0 ? "InstantA" : "InstantB", "thread");
            tracylite::Collector::Counter((i & 1) == 0 ? "CounterA" : "CounterB", static_cast<int64_t>(i + threadIndex));
            tracylite::Collector::Counter((i & 1) == 0 ? "CounterFloatA" : "CounterFloatB", static_cast<double>(i) * 0.5);
            const auto ptrValue = (static_cast<uint64_t>(static_cast<uint32_t>(threadIndex)) << 32) | static_cast<uint32_t>(i + 1);
            const auto ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(ptrValue));
            tracylite::Collector::MemAlloc(ptr, static_cast<size_t>((i % 64) + 1));
            tracylite::Collector::MemFree(ptr);
            tracylite::Collector::ZoneEnd();
        }
    }

    void DrainCollector()
    {
#if defined(TRACYLITE_PERFETTO)
        (void)tracylite::PerfettoNativeExporter::ExportToBuffer(tracylite::Collector::Instance());
#endif
    }

    size_t GetFileSize(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            return 0;
        }

        return static_cast<size_t>(file.tellg());
    }

    bool WriteTraceToFile(const char* outputPath, const std::vector<uint8_t>& trace)
    {
        std::ofstream file(outputPath, std::ios::binary);
        if (!file)
        {
            return false;
        }

        constexpr size_t writeBufSize = static_cast<size_t>(256) * 1024;
        char writeBuf[writeBufSize];
        file.rdbuf()->pubsetbuf(writeBuf, writeBufSize);
        file.write(reinterpret_cast<const char*>(trace.data()), static_cast<std::streamsize>(trace.size()));
        file.flush();
        return file.good();
    }

    PerfResult RunZoneBeginEnd(const int iterations)
    {
        PerfResult result{};
        tracylite::Collector::Initialize(32 * 1024 * 1024);
        DrainCollector();

        static constexpr tracylite::SourceLocationDataLite srcloc{ "ZoneBeginEndPerf", "RunZoneBeginEnd", "TracyNoSendPerfCase.cpp", 1, 0 };

        // Warmup: trigger ThreadState allocation (1 GB buffer + memset)
        // BEFORE the timing loop so it doesn't inflate per-pair cost.
        tracylite::Collector::ZoneBegin(&srcloc);
        tracylite::Collector::ZoneEnd();
        DrainCollector();

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            tracylite::Collector::ZoneBegin(&srcloc);
            tracylite::Collector::ZoneEnd();
        }
        const auto end = std::chrono::steady_clock::now();

        result.elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        result.nsPerPair = iterations > 0 ? static_cast<double>(result.elapsedNs) / static_cast<double>(iterations) : 0.0;

#if defined(TRACYLITE_PERFETTO)
        auto trace = tracylite::PerfettoNativeExporter::ExportToBuffer(tracylite::Collector::Instance());
        if (trace.empty())
        {
            result.elapsedNs = -1;
        }
#endif

        return result;
    }

    PerfResult RunExportToFile(const int threadCount, const int iterationsPerThread, const char* outputPath)
    {
        PerfResult result{};
        tracylite::Collector::Initialize(32 * 1024 * 1024);
        DrainCollector();

        std::vector<std::thread> workers;
        workers.reserve(threadCount);
        for (int t = 0; t < threadCount; ++t)
        {
            workers.emplace_back([t, iterationsPerThread]() { EmitWorkload(t, iterationsPerThread); });
        }

        for (auto& worker : workers)
        {
            worker.join();
        }

        std::remove(outputPath);
    #if defined(TRACYLITE_PERFETTO)
        const auto serializeStart = std::chrono::steady_clock::now();
        auto trace = tracylite::PerfettoNativeExporter::ExportToBuffer(tracylite::Collector::Instance());
        const auto serializeEnd = std::chrono::steady_clock::now();

        result.serializeElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(serializeEnd - serializeStart).count();
        result.fileSize = trace.size();

        const auto writeStart = std::chrono::steady_clock::now();
        const auto ok = !trace.empty() && WriteTraceToFile(outputPath, trace);
        const auto writeEnd = std::chrono::steady_clock::now();

        result.writeElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(writeEnd - writeStart).count();
        result.exportElapsedUs = result.serializeElapsedUs + result.writeElapsedUs;
        result.fileSize = GetFileSize(outputPath);

        const auto seconds = static_cast<double>(result.writeElapsedUs) / 1'000'000.0;
        result.throughputMbPerSec = seconds > 0.0 ? (static_cast<double>(result.fileSize) / (1024.0 * 1024.0)) / seconds : 0.0;

        if (!ok || result.fileSize == 0)
        {
            result.exportElapsedUs = -1;
            result.serializeElapsedUs = -1;
            result.writeElapsedUs = -1;
        }
#else
        (void)outputPath;
        result.exportElapsedUs = -1;
        result.serializeElapsedUs = -1;
        result.writeElapsedUs = -1;
#endif

        return result;
    }
}

#if defined(_WIN32)
#define TRACYNOSEND_API extern "C" __declspec(dllexport)
#else
#define TRACYNOSEND_API extern "C"
#endif

TRACYNOSEND_API int TracyNoSend_RunZoneBeginEndPerf(const int iterations, long long* elapsedNs, double* nsPerPair)
{
    const auto result = RunZoneBeginEnd(iterations);
    if (elapsedNs) *elapsedNs = result.elapsedNs;
    if (nsPerPair) *nsPerPair = result.nsPerPair;
    return result.elapsedNs >= 0 ? 0 : -1;
}

TRACYNOSEND_API int TracyNoSend_RunExportToFilePerf(const int threadCount,
                                                    const int iterationsPerThread,
                                                    const char* outputPath,
                                                    long long* exportElapsedUs,
                                                    long long* serializeElapsedUs,
                                                    long long* writeElapsedUs,
                                                    size_t* fileSize,
                                                    double* throughputMbPerSec)
{
    const auto result = RunExportToFile(threadCount, iterationsPerThread, outputPath);
    if (exportElapsedUs) *exportElapsedUs = result.exportElapsedUs;
    if (serializeElapsedUs) *serializeElapsedUs = result.serializeElapsedUs;
    if (writeElapsedUs) *writeElapsedUs = result.writeElapsedUs;
    if (fileSize) *fileSize = result.fileSize;
    if (throughputMbPerSec) *throughputMbPerSec = result.throughputMbPerSec;
    return result.exportElapsedUs >= 0 ? 0 : -1;
}
