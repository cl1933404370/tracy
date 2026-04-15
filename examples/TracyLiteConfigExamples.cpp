/**
 * TracyLite Export Configuration Examples
 * 
 * This file demonstrates how to configure and use different export formats
 * (JSON Chrome Trace vs Perfetto native trace) in TracyLite.
 */

#include "public/client/TracyLiteAll.hpp"
#include "public/tracy/TracyHcomm.hpp"
#include <iostream>
#include <thread>
#include <chrono>

// ============================================================================
// Example 1: Default Configuration (auto-selects format based on build)
// ============================================================================
void Example1_DefaultConfig()
{
    std::cout << "\n=== Example 1: Default Configuration ===\n";
    
    // When TRACYLITE_PERFETTO is defined, defaults to Perfetto format
    // Otherwise defaults to JSON format
    tracylite::DumpConfig config;
    config.export_on_destroy = true;
    config.max_buffer_mb = 64;
    
    TRACYLITE_AUTO_DUMP(config);
    
    {
        ZoneScoped;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "Export will happen automatically on shutdown\n";
    std::cout << "Format: " 
#ifdef TRACYLITE_PERFETTO
              << "Perfetto (.perfetto-trace)\n";
#else
              << "JSON (.json)\n";
#endif
}

// ============================================================================
// Example 2: Explicit JSON Export
// ============================================================================
void Example2_ExplicitJSON()
{
    std::cout << "\n=== Example 2: Explicit JSON Export ===\n";
    
    tracylite::DumpConfig config;
    config.format = tracylite::ExportFormat::JSON;
    config.export_on_destroy = false;  // Manual export
    
    tracylite::DumpManager::Instance().Initialize(config);
    
    {
        ZoneScoped;
        TRACYLITE_COUNTER("example2_counter", 42);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    // Explicit JSON export with custom path
    if (TRACYLITE_EXPORT_JSON("./traces/my_trace.json"))
    {
        std::cout << "JSON trace exported to ./traces/my_trace.json\n";
    }
    
    // Or export with auto-generated filename
    if (TRACYLITE_EXPORT_JSON(nullptr))
    {
        std::cout << "JSON trace exported with default filename\n";
    }
}

// ============================================================================
// Example 3: Explicit Perfetto Export (when available)
// ============================================================================
void Example3_ExplicitPerfetto()
{
#ifdef TRACYLITE_PERFETTO
    std::cout << "\n=== Example 3: Explicit Perfetto Export ===\n";
    
    tracylite::DumpConfig config;
    config.format = tracylite::ExportFormat::Perfetto;
    config.export_on_destroy = false;
    
    tracylite::DumpManager::Instance().Initialize(config);
    
    {
        ZoneScoped;
        TRACYLITE_COUNTER("perfetto_counter", 100);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    // Explicit Perfetto export with custom path
    if (TRACYLITE_EXPORT_PERFETTO("./traces/my_trace.perfetto-trace"))
    {
        std::cout << "Perfetto trace exported to ./traces/my_trace.perfetto-trace\n";
    }
    
    // Or export with auto-generated filename
    if (TRACYLITE_EXPORT_PERFETTO(nullptr))
    {
        std::cout << "Perfetto trace exported with default filename\n";
    }
#else
    std::cout << "\n=== Example 3: Perfetto Not Available ===\n";
    std::cout << "Build with -DTRACYLITE_PERFETTO=ON to enable Perfetto export\n";
#endif
}

// ============================================================================
// Example 4: Runtime Format Selection
// ============================================================================
void Example4_RuntimeFormatSelection()
{
    std::cout << "\n=== Example 4: Runtime Format Selection ===\n";
    
    // Initialize with one format
    tracylite::DumpConfig config;
    config.format = tracylite::ExportFormat::JSON;
    config.export_on_destroy = false;
    
    tracylite::DumpManager::Instance().Initialize(config);
    
    {
        ZoneScopedN("Phase1_JSON");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    // Export as JSON
    TRACYLITE_EXPORT_JSON("./traces/phase1.json");
    std::cout << "Phase 1 exported as JSON\n";
    
#ifdef TRACYLITE_PERFETTO
    // Switch to Perfetto format for next export
    config.format = tracylite::ExportFormat::Perfetto;
    TRACYLITE_SET_DUMP_CONFIG(config);
    
    {
        ZoneScopedN("Phase2_Perfetto");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    // Export as Perfetto
    TRACYLITE_EXPORT_PERFETTO("./traces/phase2.perfetto-trace");
    std::cout << "Phase 2 exported as Perfetto\n";
#endif
}

// ============================================================================
// Example 5: Export with Callbacks
// ============================================================================
void Example5_ExportWithCallbacks()
{
    std::cout << "\n=== Example 5: Export with Callbacks ===\n";
    
    tracylite::DumpConfig config;
    config.export_on_destroy = false;
    
    tracylite::DumpManager::Instance().Initialize(config);
    
    // Set pre-export callback (runs before export)
    TRACYLITE_SET_PRE_EXPORT([]() {
        std::cout << "[PreExport] Preparing trace data...\n";
    });
    
    // Set post-export callback (runs after successful export)
    TRACYLITE_SET_POST_EXPORT([](const std::string& path) {
        std::cout << "[PostExport] Trace saved to: " << path << "\n";
        std::cout << "[PostExport] File size: " 
                  << std::ifstream(path, std::ios::binary | std::ios::ate).tellg() 
                  << " bytes\n";
    });
    
    {
        ZoneScoped;
        TRACYLITE_INSTANT("callback_example", "process");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    // Callbacks will be invoked during export
    TRACYLITE_EXPORT();
}

// ============================================================================
// Example 6: Getting Statistics
// ============================================================================
void Example6_Statistics()
{
    std::cout << "\n=== Example 6: Getting Statistics ===\n";
    
    tracylite::DumpManager::Instance().Initialize();
    
    for (int i = 0; i < 100; ++i)
    {
        ZoneScopedN("WorkItem");
        TRACYLITE_COUNTER("iteration", i);
        volatile int sum = 0;
        for (int j = 0; j < 1000; ++j) sum += j;
    }
    
    auto stats = TRACYLITE_GET_DUMP_STATS();
    std::cout << "Buffer size: " << stats.buffer_size << " events\n";
    std::cout << "Export count: " << stats.export_count << "\n";
    
    // Export and check stats again
    TRACYLITE_EXPORT();
    
    stats = TRACYLITE_GET_DUMP_STATS();
    std::cout << "After export - Export count: " << stats.export_count << "\n";
}

// ============================================================================
// Example 7: Comparing JSON vs Perfetto Export
// ============================================================================
void Example7_CompareFormats()
{
    std::cout << "\n=== Example 7: Comparing JSON vs Perfetto ===\n";
    
#ifdef TRACYLITE_PERFETTO
    // Collect some trace data
    tracylite::DumpManager::Instance().Initialize();
    
    for (int i = 0; i < 50; ++i)
    {
        ZoneScopedN("TestZone");
        TRACYLITE_COUNTER("test_counter", i * 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Export as both formats for comparison
    std::cout << "Exporting the same trace data in both formats...\n";
    
    auto start_json = std::chrono::high_resolution_clock::now();
    TRACYLITE_EXPORT_JSON("./traces/compare.json");
    auto end_json = std::chrono::high_resolution_clock::now();
    auto json_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_json - start_json);
    
    auto start_perfetto = std::chrono::high_resolution_clock::now();
    TRACYLITE_EXPORT_PERFETTO("./traces/compare.perfetto-trace");
    auto end_perfetto = std::chrono::high_resolution_clock::now();
    auto perfetto_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_perfetto - start_perfetto);
    
    std::cout << "JSON export time: " << json_duration.count() << " ms\n";
    std::cout << "Perfetto export time: " << perfetto_duration.count() << " ms\n";
    
    std::cout << "\nViewing instructions:\n";
    std::cout << "  JSON: Open compare.json in chrome://tracing\n";
    std::cout << "  Perfetto: Open compare.perfetto-trace in https://ui.perfetto.dev\n";
#else
    std::cout << "This example requires TRACYLITE_PERFETTO to be enabled\n";
    std::cout << "Build with: cmake -DTRACYLITE_PERFETTO=ON\n";
#endif
}

// ============================================================================
// Main: Run All Examples
// ============================================================================
int main()
{
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     TracyLite Export Configuration Examples              ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    
    std::cout << "\nBuild Configuration:\n";
#ifdef TRACYLITE_PERFETTO
    std::cout << "  TRACYLITE_PERFETTO: ENABLED\n";
    std::cout << "  Supported formats: JSON, Perfetto\n";
#else
    std::cout << "  TRACYLITE_PERFETTO: DISABLED\n";
    std::cout << "  Supported formats: JSON only\n";
#endif
    
    try
    {
        Example1_DefaultConfig();
        Example2_ExplicitJSON();
        Example3_ExplicitPerfetto();
        Example4_RuntimeFormatSelection();
        Example5_ExportWithCallbacks();
        Example6_Statistics();
        Example7_CompareFormats();
        
        std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
        std::cout << "║  All examples completed successfully!                     ║\n";
        std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
