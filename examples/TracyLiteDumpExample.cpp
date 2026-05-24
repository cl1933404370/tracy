#include "public/client/TracyLite.hpp"
#include "public/client/TracyLiteDump.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

using namespace tracylite;

/**
 * 演示如何使用 TracyLite + DumpManager
 * 
 * 这个示例展示：
 * 1. 自动初始化 (TRACYLITE_AUTO_DUMP)
 * 2. 后台 Dump 线程
 * 3. 主动导出
 * 4. 自定义回调
 */

void SimulateWork(int iterations = 100) {
    TRACYLITE_ZONE("SimulateWork");
    
    for (int i = 0; i < iterations; ++i) {
        {
            TRACYLITE_ZONE("WorkItem");
            TRACYLITE_COUNTER("progress", i);
            
            // 模拟 CPU 工作
            volatile int sum = 0;
            for (int j = 0; j < 10000; ++j) {
                sum += j;
            }
        }
    }
}

void ProducerThread(int id, int iterations) {
    std::string name = "Producer_" + std::to_string(id);
    TRACYLITE_ZONE(name.c_str());
    
    for (int i = 0; i < iterations; ++i) {
        {
            TRACYLITE_ZONE("ProduceItem");
            TRACYLITE_COUNTER("producer_progress", i);
            SimulateWork(10);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void PrintStats() {
    auto stats = TRACYLITE_GET_DUMP_STATS();
    std::cout << "\n=== Dump Statistics ===" << std::endl;
    std::cout << "Export count:  " << stats.export_count << std::endl;
    std::cout << "Buffer size:   " << stats.buffer_size / (1024 * 1024) << " MB" << std::endl;
}

int main() {
    std::cout << "╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  TracyLite + DumpManager (Auto-Dump + Auto-Export)       ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;
    
    {
        // 方案 1: 自动初始化 + 自动后台 Dump
        // ========================================
        
        std::cout << "Initializing with auto-dump enabled..." << std::endl;
        
        // 配置自动 Dump
        DumpConfig config;
        config.auto_dump = true;
        config.dump_interval_ms = 2000;  // 每 2 秒 dump 一次
        config.export_path = "trace_autodump.json";
        config.export_on_destroy = true;
        config.max_buffer_mb = 64;
        
        // 自动初始化 (类似 ChromeAutoInit)
        TRACYLITE_AUTO_DUMP(config);
        
        // 设置导出前回调
        TRACYLITE_SET_PRE_EXPORT([]() {
            std::cout << "[PreExport] Preparing data..." << std::endl;
        });
        
        // 设置导出后回调
        TRACYLITE_SET_POST_EXPORT([](const std::string& path) {
            std::cout << "[PostExport] Saved to: " << path << std::endl;
        });
        
        {
            TRACYLITE_ZONE("main_workload");
            
            std::cout << "Starting workload threads..." << std::endl;
            
            // 创建工作线程
            std::vector<std::thread> threads;
            for (int i = 0; i < 3; ++i) {
                threads.emplace_back(ProducerThread, i, 20);
            }
            
            std::cout << "Running for ~10 seconds (后台每 2 秒自动 dump)..." << std::endl;
            
            // 等待所有线程完成
            for (auto& t : threads) {
                t.join();
            }
        }
        
        PrintStats();
        
        // 方案 2: 主动导出
        // ========================================
        
        std::cout << "\n--- Manual Export Example ---" << std::endl;
        
        bool success = TRACYLITE_EXPORT("trace_manual.json");
        if (success) {
            std::cout << "✓ Manual export successful" << std::endl;
        } else {
            std::cout << "✗ Manual export failed" << std::endl;
        }
        
        PrintStats();
        
        // 方案 3: 导出到字符串 (用于网络传输或处理)
        // ========================================
        
        std::cout << "\n--- Export to String Example ---" << std::endl;
        
        std::string jsonStr = TRACYLITE_EXPORT_STRING();
        std::cout << "✓ Exported to string, size: " << jsonStr.size() << " bytes" << std::endl;
        
        // 可以发送到网络、保存到数据库等
        // NetworkSend(jsonStr);
        // database.Save("trace", jsonStr);
        
        std::cout << "\nWaiting for auto-dump to finish..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    
    PrintStats();
    
    std::cout << "\n╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  程序结束，自动导出触发                                    ║" << std::endl;
    std::cout << "║  生成文件: trace_autodump.json, trace_manual.json          ║" << std::endl;
    std::cout << "║  打开: https://ui.perfetto.dev/#!/viewer                  ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
    
    return 0;
}
