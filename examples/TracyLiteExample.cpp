#include "public/client/TracyLite.hpp"
#include "public/client/TracyLiteExporter.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

using namespace tracylite;

// Test functions
void SimulateWork(int iterations = 100) {
    TRACYLITE_ZONE("SimulateWork");
    
    for (int i = 0; i < iterations; ++i) {
        {
            TRACYLITE_ZONE("Loop_Iteration");
            TRACYLITE_COUNTER("iteration", i);
            
            // Simulate some CPU work
            volatile int sum = 0;
            for (int j = 0; j < 1000; ++j) {
                sum += j;
            }
        }
    }
}

void ProducerThread(int id) {
    std::string threadName = "Producer_" + std::to_string(id);
    TRACYLITE_ZONE(threadName.c_str());
    
    for (int i = 0; i < 50; ++i) {
        {
            TRACYLITE_ZONE("ProducerWork");
            SimulateWork(10);
        }
        
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void ConsumerThread(int id) {
    std::string threadName = "Consumer_" + std::to_string(id);
    TRACYLITE_ZONE(threadName.c_str());
    
    for (int i = 0; i < 50; ++i) {
        {
            TRACYLITE_ZONE("ConsumerWork");
            SimulateWork(5);
        }
        
        std::this_thread::sleep_for(std::chrono::microseconds(150));
    }
}

int main() {
    std::cout << "=== TracyLite Offline Profiler ===" << std::endl;
    std::cout << "Initializing collector..." << std::endl;
    
    // Initialize with 64MB buffer
    Collector::Instance().Initialize(64 * 1024 * 1024);
    
    std::cout << "Starting profiling threads..." << std::endl;
    
    {
        TRACYLITE_ZONE("main");
        
        // Create worker threads
        std::vector<std::thread> threads;
        
        // Spawn producer threads
        for (int i = 0; i < 2; ++i) {
            threads.emplace_back(ProducerThread, i);
        }
        
        // Spawn consumer threads
        for (int i = 0; i < 2; ++i) {
            threads.emplace_back(ConsumerThread, i);
        }
        
        std::cout << "Running workload..." << std::endl;
        
        // Wait for all threads to complete
        for (auto& t : threads) {
            t.join();
        }
    }
    
    std::cout << "Profiling complete. Exporting data..." << std::endl;
    
    // Export to JSON (Perfetto format)
    if (PerfettoExporter::ExportToJSONFile(Collector::Instance(), "trace.json")) {
        std::cout << "✓ Exported to trace.json" << std::endl;
        std::cout << "Open with: https://ui.perfetto.dev/#!/viewer" << std::endl;
    } else {
        std::cout << "✗ Failed to export trace.json" << std::endl;
        return 1;
    }
    
    std::cout << "Done!" << std::endl;
    return 0;
}
