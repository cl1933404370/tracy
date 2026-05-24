#include "client/TracyLiteAll.hpp"
#include "tracy/TracyHcomm.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main()
{
    std::cout << "PerfettoTest start\n";
    tracylite::DumpConfig config;
    config.exportOnDestroy_ = false;
    tracylite::DumpManager::Instance().Initialize(config);

    {
        ZoneScopedN("PerfettoTestZone");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto stats = tracylite::DumpManager::Instance().GetStats();
    std::cout << "Buffer size before export: " << stats.bufferSize_ << " events\n";

#ifdef TRACYLITE_PERFETTO
    const char* perfpath = "./perfetto_test.perfetto-trace";
    std::cout << "Attempting Perfetto export to: " << perfpath << "\n";
    bool ok = tracylite::DumpManager::Instance().ExportAsPerfetto(perfpath);
    std::cout << "Perfetto export result: " << (ok ? "OK" : "FAILED") << "\n";
#else
    std::cout << "Perfetto not enabled in this build.\n";
#endif

    const char* jsonpath = "./perfetto_test.json";
    std::cout << "Attempting JSON export to: " << jsonpath << "\n";
    bool ok2 = tracylite::DumpManager::Instance().ExportNow();
    std::cout << "JSON export result: " << (ok2 ? "OK" : "FAILED") << "\n";

    return 0;
}
