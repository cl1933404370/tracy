#include "public/client/TracyLite.hpp"
#include "public/client/TracyLiteExporter.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cmath>

using namespace tracylite;

// Simulated workloads
class WorkloadSimulator {
public:
    static void CPUBound(int duration_ms) {
        TRACYLITE_ZONE("CPUBound");
        
        auto start = std::chrono::high_resolution_clock::now();
        volatile double result = 0;
        
        while (true) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed >= duration_ms) break;
            
            // CPU-intensive work
            for (int i = 0; i < 10000; ++i) {
                result += std::sin(i * 0.001);
            }
        }
    }

    static void IOSimulation(int iterations) {
        TRACYLITE_ZONE("IOSimulation");
        
        for (int i = 0; i < iterations; ++i) {
            {
                TRACYLITE_ZONE("IOOperation");
                TRACYLITE_INSTANT("io_start");
                
                // Simulate I/O wait
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                
                TRACYLITE_INSTANT("io_complete");
                TRACYLITE_COUNTER("io_bytes", 4096);
            }
        }
    }

    static void MemoryAllocation(int count) {
        TRACYLITE_ZONE("MemoryAllocation");
        
        std::vector<void*> ptrs;
        
        for (int i = 0; i < count; ++i) {
            {
                TRACYLITE_ZONE("AllocCycle");
                
                void* ptr = malloc(1024);
                TRACYLITE_MALLOC(ptr, 1024);
                ptrs.push_back(ptr);
                
                TRACYLITE_COUNTER("alloc_count", (int64_t)ptrs.size());
            }
        }
        
        // Cleanup
        for (void* ptr : ptrs) {
            TRACYLITE_FREE(ptr);
            free(ptr);
        }
    }

    static void LockContention(int iterations) {
        TRACYLITE_ZONE("LockContention");
        
        static std::mutex m;
        static int counter = 0;
        
        for (int i = 0; i < iterations; ++i) {
            {
                TRACYLITE_ZONE("CriticalSection");
                std::lock_guard<std::mutex> lock(m);
                counter++;
                TRACYLITE_COUNTER("shared_counter", counter);
            }
            
            // Simulate work outside lock
            {
                TRACYLITE_ZONE("UnlockedWork");
                volatile int sum = 0;
                for (int j = 0; j < 100; ++j) {
                    sum += j;
                }
            }
        }
    }
};

// Thread pool work distribution
void ThreadPoolWorker(int worker_id, int task_count) {
    std::string worker_name = "Worker_" + std::to_string(worker_id);
    TRACYLITE_ZONE(worker_name.c_str());
    
    for (int i = 0; i < task_count; ++i) {
        {
            TRACYLITE_ZONE("Task");
            TRACYLITE_COUNTER("task_id", worker_id * 1000 + i);
            
            // Randomize workload distribution
            int workload = (i % 3);
            switch (workload) {
                case 0:
                    WorkloadSimulator::CPUBound(1);
                    break;
                case 1:
                    WorkloadSimulator::MemoryAllocation(5);
                    break;
                case 2:
                    WorkloadSimulator::IOSimulation(3);
                    break;
            }
        }
    }
}

// Demonstrate hot path analysis
void HotPath(int iterations) {
    TRACYLITE_ZONE("HotPath");
    
    for (int iter = 0; iter < iterations; ++iter) {
        {
            TRACYLITE_ZONE("Loop");
            
            // Most time spent here (hot)
            {
                TRACYLITE_ZONE("HotFunction");
                CPUBound(5);
            }
            
            // Less time here (warm)
            {
                TRACYLITE_ZONE("WarmFunction");
                CPUBound(1);
            }
            
            // Very little time here (cold)
            {
                TRACYLITE_ZONE("ColdFunction");
                IOSimulation(1);
            }
        }
    }
}

// Simple CPU-bound work
void CPUBound(int iterations) {
    volatile double sum = 0;
    for (int i = 0; i < iterations * 1000000; ++i) {
        sum += std::sin(i * 0.001);
    }
}

int main(int argc, char* argv[]) {
    std::cout << "╔════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║     TracyLite - High Performance Offline Profiler     ║" << std::endl;
    std::cout << "║     Visualization: https://ui.perfetto.dev/           ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;
    
    // Parse command line
    int buffer_size_mb = 64;
    if (argc > 1) {
        buffer_size_mb = std::atoi(argv[1]);
    }
    
    std::cout << "Initializing collector with " << buffer_size_mb << "MB buffer..." << std::endl;
    Collector::Instance().Initialize(buffer_size_mb * 1024 * 1024);
    
    {
        TRACYLITE_ZONE("main");
        
        // Demonstrate various profiling scenarios
        std::cout << "\n=== Scenario 1: Thread Pool ===" << std::endl;
        {
            TRACYLITE_ZONE("ThreadPool");
            
            std::vector<std::thread> workers;
            const int num_workers = 4;
            const int tasks_per_worker = 10;
            
            for (int i = 0; i < num_workers; ++i) {
                workers.emplace_back(ThreadPoolWorker, i, tasks_per_worker);
            }
            
            for (auto& w : workers) {
                w.join();
            }
        }
        
        std::cout << "✓ Thread pool profiling complete" << std::endl;
        
        // Demonstrate hot path analysis
        std::cout << "\n=== Scenario 2: Hot Path Analysis ===" << std::endl;
        {
            HotPath(5);
        }
        std::cout << "✓ Hot path analysis complete" << std::endl;
        
        // Demonstrate memory tracking
        std::cout << "\n=== Scenario 3: Memory Tracking ===" << std::endl;
        {
            TRACYLITE_ZONE("MemoryTracking");
            WorkloadSimulator::MemoryAllocation(20);
        }
        std::cout << "✓ Memory tracking complete" << std::endl;
        
        // Demonstrate lock contention
        std::cout << "\n=== Scenario 4: Lock Contention ===" << std::endl;
        {
            TRACYLITE_ZONE("LockContentionTest");
            WorkloadSimulator::LockContention(30);
        }
        std::cout << "✓ Lock contention analysis complete" << std::endl;
        
        // Demonstrate I/O simulation
        std::cout << "\n=== Scenario 5: I/O Simulation ===" << std::endl;
        {
            WorkloadSimulator::IOSimulation(10);
        }
        std::cout << "✓ I/O simulation complete" << std::endl;
    }
    
    std::cout << "\n" << std::endl;
    std::cout << "Profiling complete. Exporting data..." << std::endl;
    
    // Export to JSON
    const char* output_file = "trace_detailed.json";
    
    if (PerfettoExporter::ExportToJSONFile(Collector::Instance(), output_file)) {
        std::cout << "✓ Successfully exported to: " << output_file << std::endl;
        std::cout << "\n╔════════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║ Next steps:                                            ║" << std::endl;
        std::cout << "║ 1. Open: https://ui.perfetto.dev/#!/viewer            ║" << std::endl;
        std::cout << "║ 2. Click: Open trace file                             ║" << std::endl;
        std::cout << "║ 3. Select: " << output_file << "                     ║" << std::endl;
        std::cout << "║ 4. Analyze: Use Timeline, Flamegraph, and Counters    ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════════════════╝" << std::endl;
    } else {
        std::cout << "✗ Failed to export trace file" << std::endl;
        return 1;
    }
    
    return 0;
}
