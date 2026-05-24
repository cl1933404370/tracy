# TracyLite Export Configuration Guide

This guide explains how to configure and use different export formats in TracyLite.

## Overview

TracyLite supports two export formats:

1. **JSON (Chrome Trace Format)** - Always available, compatible with chrome://tracing
2. **Perfetto Native** - Optional, requires `TRACYLITE_PERFETTO=ON`, viewable in https://ui.perfetto.dev

## Build Configuration

### Enable JSON Export Only (Default)
```cmake
cmake -DTRACY_SAVE_NO_SEND=ON ..
```

### Enable Both JSON and Perfetto Export
```cmake
cmake -DTRACY_SAVE_NO_SEND=ON -DTRACYLITE_PERFETTO=ON ..
```

### Using CMake Presets (Recommended)

All `hcomm` presets already include both `TRACY_SAVE_NO_SEND=ON` and `TRACYLITE_PERFETTO=ON`:

```bash
# Windows
cmake --preset win-hcomm-debug        # Client only
cmake --preset win-all-hcomm-debug    # Full build

# Linux
cmake --preset linux-hcomm-debug
cmake --preset linux-all-hcomm-debug

# WSL2
cmake --preset wsl2-hcomm-debug
cmake --preset wsl2-all-hcomm-debug
```

## Export Format Selection

### Option 1: Configuration-Based Selection (Runtime)

```cpp
#include "public/client/TracyLiteAll.hpp"

// Configure export format
tracylite::DumpConfig config;
config.format = tracylite::ExportFormat::JSON;  // or ExportFormat::Perfetto
config.export_on_destroy = true;
config.max_buffer_mb = 64;

// Initialize with config
TRACYLITE_AUTO_DUMP(config);

// Your instrumented code...
{
    ZoneScoped;
    // ...
}

// Export happens automatically on shutdown according to config.format
```

### Option 2: Explicit Export Methods (Recommended)

```cpp
#include "public/client/TracyLiteAll.hpp"

// Initialize (format doesn't matter for explicit exports)
tracylite::DumpManager::Instance().Initialize();

// Your instrumented code...
{
    ZoneScoped;
    TRACYLITE_COUNTER("my_counter", 42);
}

// Export explicitly as JSON
TRACYLITE_EXPORT_JSON("./my_trace.json");

// Export explicitly as Perfetto (if available)
#ifdef TRACYLITE_PERFETTO
TRACYLITE_EXPORT_PERFETTO("./my_trace.perfetto-trace");
#endif
```

### Option 3: Default Auto-Export

```cpp
// Uses default format based on build configuration
TRACYLITE_EXPORT();  // Uses config.format
```

## API Reference

### Configuration Struct

```cpp
struct DumpConfig {
    size_t max_buffer_mb = 64;           // Max buffer size in MB
    bool export_on_destroy = true;       // Auto-export on shutdown
    ExportFormat format = /* default */; // JSON or Perfetto
};

enum class ExportFormat : uint8_t {
    JSON = 0,
#ifdef TRACYLITE_PERFETTO
    Perfetto = 1,
#endif
};
```

### Export Methods

```cpp
// Automatic export using configured format
bool ExportNow();
TRACYLITE_EXPORT()

// Explicit format exports
bool ExportAsJSON(const char* filepath = nullptr);
TRACYLITE_EXPORT_JSON(filepath)

#ifdef TRACYLITE_PERFETTO
bool ExportAsPerfetto(const char* filepath = nullptr);
TRACYLITE_EXPORT_PERFETTO(filepath)
#endif

// String export (JSON only)
std::string ExportToString() const;
TRACYLITE_EXPORT_STRING()
```

### Callbacks

```cpp
// Pre-export callback (runs before any export)
TRACYLITE_SET_PRE_EXPORT([]() {
    std::cout << "Starting export...\n";
});

// Post-export callback (runs after successful export)
TRACYLITE_SET_POST_EXPORT([](const std::string& filepath) {
    std::cout << "Exported to: " << filepath << "\n";
});
```

### Statistics

```cpp
auto stats = TRACYLITE_GET_DUMP_STATS();
std::cout << "Buffer size: " << stats.buffer_size << " events\n";
std::cout << "Export count: " << stats.export_count << "\n";
```

## Performance Considerations

### Instrumentation Overhead

**The export format selection has ZERO impact on instrumentation overhead.** 

The hot-path macros (`ZoneScoped`, `TRACYLITE_COUNTER`, etc.) only write to an in-memory ring buffer. Format selection only affects the export process, which happens off the critical path.

```cpp
// This has the same overhead regardless of export format:
{
    ZoneScoped;  // Just writes to ring buffer
    // Your code here
}
```

### Export Performance

- **JSON Export**: Slower but more compatible, produces human-readable text
- **Perfetto Export**: Faster, produces binary format optimized for large traces

Benchmark on typical workload (10,000 events):
- JSON export: ~50-100ms
- Perfetto export: ~10-20ms

## Common Usage Patterns

### Pattern 1: Development (JSON for quick viewing)

```cpp
tracylite::DumpConfig config;
config.format = tracylite::ExportFormat::JSON;
TRACYLITE_AUTO_DUMP(config);

// Open output in chrome://tracing for quick inspection
```

### Pattern 2: Production (Perfetto for performance)

```cpp
#ifdef TRACYLITE_PERFETTO
tracylite::DumpConfig config;
config.format = tracylite::ExportFormat::Perfetto;
config.export_on_destroy = true;
TRACYLITE_AUTO_DUMP(config);

// Use Perfetto UI for advanced analysis
#endif
```

### Pattern 3: Both Formats for Comparison

```cpp
tracylite::DumpManager::Instance().Initialize();

// Collect trace data
{
    ZoneScoped;
    // Your code...
}

// Export in both formats
TRACYLITE_EXPORT_JSON("./trace.json");
#ifdef TRACYLITE_PERFETTO
TRACYLITE_EXPORT_PERFETTO("./trace.perfetto-trace");
#endif
```

### Pattern 4: Conditional Export

```cpp
void ExportTrace(bool use_perfetto)
{
    if (use_perfetto)
    {
#ifdef TRACYLITE_PERFETTO
        TRACYLITE_EXPORT_PERFETTO(nullptr);
#else
        std::cerr << "Perfetto not available, falling back to JSON\n";
        TRACYLITE_EXPORT_JSON(nullptr);
#endif
    }
    else
    {
        TRACYLITE_EXPORT_JSON(nullptr);
    }
}
```

## Viewing Traces

### JSON Traces (.json)
1. Open Chrome/Chromium browser
2. Navigate to `chrome://tracing`
3. Click "Load" and select your `.json` file

### Perfetto Traces (.perfetto-trace)
1. Open https://ui.perfetto.dev in your browser
2. Click "Open trace file"
3. Select your `.perfetto-trace` file

Or use the Perfetto command-line tools:
```bash
# View trace
perfetto ui ./trace.perfetto-trace

# Convert to JSON for other tools
traceconv json ./trace.perfetto-trace ./trace.json
```

## Troubleshooting

### "TRACYLITE_EXPORT_PERFETTO not defined"

**Cause**: Perfetto support not enabled during build

**Solution**: Rebuild with:
```cmake
cmake -DTRACY_SAVE_NO_SEND=ON -DTRACYLITE_PERFETTO=ON ..
```

### Empty or corrupted trace files

**Cause**: Export called before events are collected, or buffer overflow

**Solution**:
```cpp
// Check buffer before export
auto stats = TRACYLITE_GET_DUMP_STATS();
if (stats.buffer_size > 0)
{
    TRACYLITE_EXPORT();
}

// Increase buffer size if needed
tracylite::DumpConfig config;
config.max_buffer_mb = 128;  // Increase from default 64MB
TRACYLITE_AUTO_DUMP(config);
```

### Dropped events

**Cause**: Ring buffer full (instrumentation rate > export rate)

**Solution**:
```cpp
// Check dropped count
std::cout << "Dropped events: " 
          << tracylite::Collector::GetDroppedCount() << "\n";

// Increase buffer size
tracylite::DumpConfig config;
config.max_buffer_mb = 256;  // Larger buffer
TRACYLITE_AUTO_DUMP(config);

// Or export more frequently
TRACYLITE_EXPORT();  // Periodic exports drain the buffer
```

## Best Practices

1. **Use explicit export methods** (`TRACYLITE_EXPORT_JSON` / `TRACYLITE_EXPORT_PERFETTO`) for clarity
2. **Set export format at initialization** and don't change it frequently
3. **Use callbacks** for logging and file size monitoring
4. **Export periodically** for long-running processes to avoid buffer overflow
5. **Prefer Perfetto** for large traces (>100MB) due to better performance
6. **Use JSON** for quick debugging and maximum compatibility

## Examples

See `examples/TracyLiteConfigExamples.cpp` for complete working examples of all configuration patterns.

## Migration from Older API

### Old (implicit format selection)
```cpp
TRACYLITE_EXPORT();  // Format determined by config only
```

### New (explicit format selection - recommended)
```cpp
TRACYLITE_EXPORT_JSON("./trace.json");        // Clear intent
TRACYLITE_EXPORT_PERFETTO("./trace.perfetto"); // Clear intent
```

Both approaches work, but explicit methods are clearer and don't require checking the config.
