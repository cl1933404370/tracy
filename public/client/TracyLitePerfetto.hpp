#ifndef TRACYLITE_PERFETTO_HPP
#define TRACYLITE_PERFETTO_HPP

#ifdef TRACYLITE_PERFETTO

#include <cstdint>
#include <vector>

namespace tracylite {

class Collector;

class PerfettoNativeExporter {
public:
    static bool ExportToFile( const Collector& collector, const char* filename );
    static std::vector<uint8_t> ExportToBuffer( const Collector& collector );
};

} // namespace tracylite

#endif // TRACYLITE_PERFETTO
#endif // TRACYLITE_PERFETTO_HPP
