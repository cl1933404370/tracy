# Tracy NoSendPerf `.so` 集成最小模板（主体 C++14 + 单 TU C++17）

## 目的

这份文档记录一个**最小可抄**的集成方案：

- 目标产物是一个 **shared library (`.so`)**
- 主体业务代码 / Tracy 主体代码仍保持 **C++14**
- 只有 `TracyLitePerfetto.cpp` 这个翻译单元单独使用 **C++17**
- 最终一起链接进同一个 `.so`

这适合以下场景：

- 目标工程默认还是 C++14
- 需要 `TRACY_SAVE_NO_SEND=ON`
- 需要 `TRACYLITE_PERFETTO=ON`
- 不希望把整个工程或整个 Tracy 客户端目标全部提升到 C++17

---

## 已验证结论

已做过的验证结论如下：

1. `TracyLitePerfetto.cpp` 在 **C++14** 下会失败。
2. 失败的主要原因来自 `scripts/perfetto-sdk-offline/perfetto.h`，它使用了 C++17 特性，例如：
   - `if constexpr`
   - `std::optional`
   - `std::string_view`
   - `std::variant_size_v`
   - `std::is_same_v`
3. `TracyLitePerfetto.cpp` 在 **GCC 7.3.0 + C++17** 下可以通过语法级编译检查。
4. 其它 `NoSendPerf` 的 `.cpp` 文件在 **C++14** 下仍可通过编译，即不会因为定义了 `TRACYLITE_PERFETTO` 就被 `perfetto.h` 直接污染。
5. 因此，**“主体 C++14 + 单文件 C++17”** 是目前最现实、最小侵入的方案。

---

## 最小 CMake 片段

将下面片段抄到目标工程后，按需替换路径即可：

```cmake
cmake_minimum_required(VERSION 3.10)

project(MyPerfSo CXX)

find_package(Threads REQUIRED)

set(TRACY_ROOT "/path/to/tracy")
set(TRACY_PUBLIC_DIR "${TRACY_ROOT}/public")
set(TRACY_CLIENT_CPP "${TRACY_PUBLIC_DIR}/TracyClient.cpp")
set(TRACY_LITE_PERFETTO_CPP "${TRACY_PUBLIC_DIR}/client/TracyLitePerfetto.cpp")

# 如果直接使用离线 perfetto SDK：
set(TRACY_PERFETTO_OFFLINE_DIR "${TRACY_ROOT}/scripts/perfetto-sdk-offline")
set(TRACY_PERFETTO_OFFLINE_CPP "${TRACY_PERFETTO_OFFLINE_DIR}/perfetto.cc")

add_library(MyPerfSo SHARED
    src/my_perf_entry.cpp
    ${TRACY_CLIENT_CPP}
    ${TRACY_LITE_PERFETTO_CPP}
)

# 主体目标默认还是 C++14
 target_compile_features(MyPerfSo PRIVATE cxx_std_14)

 target_include_directories(MyPerfSo PRIVATE
    "${TRACY_PUBLIC_DIR}"
    "${TRACY_PERFETTO_OFFLINE_DIR}"
)

 target_compile_definitions(MyPerfSo PRIVATE
    TRACY_ENABLE=1
    TRACY_SAVE_NO_SEND=1
    TRACYLITE_PERFETTO=1
    TRACY_ON_DEMAND=1
    TRACY_NO_BROADCAST=1
    TRACY_ONLY_LOCALHOST=1
    TRACY_NO_SAMPLING=1
    TRACY_NO_CONTEXT_SWITCH=1
)

 target_link_libraries(MyPerfSo PRIVATE
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# 如果没有单独的 perfetto_sdk target，就把离线 perfetto.cc 编进来
 target_sources(MyPerfSo PRIVATE
    "${TRACY_PERFETTO_OFFLINE_CPP}"
)

# 只让 TracyLitePerfetto.cpp 这个翻译单元使用 C++17
if(MSVC)
    set_source_files_properties("${TRACY_LITE_PERFETTO_CPP}"
        PROPERTIES COMPILE_FLAGS "/std:c++17 /bigobj")
else()
    set_source_files_properties("${TRACY_LITE_PERFETTO_CPP}"
        PROPERTIES COMPILE_FLAGS "-std=c++17")
endif()
```

---

## 如果工程里已经有 `perfetto_sdk` target

如果目标工程已经单独构建了 `perfetto_sdk`，则可改成：

```cmake
 target_include_directories(MyPerfSo PRIVATE
    "${TRACY_PUBLIC_DIR}"
)

 target_link_libraries(MyPerfSo PRIVATE
    Threads::Threads
    ${CMAKE_DL_LIBS}
    perfetto_sdk
)

# 如有需要，再补 perfetto_sdk 的 include 路径
 target_include_directories(MyPerfSo PRIVATE
    "/path/to/perfetto/sdk/include"
)
```

这时通常不需要再把离线的 `perfetto.cc` 编进目标。

---

## 示例入口代码

### 方案 A：宏形式（更接近业务代码）

```cpp
#include <tracy/TracyHcomm.hpp>

extern "C" void MyHotFunction()
{
    ZoneScopedN("MyHotFunction");
    // 热点逻辑
}
```

### 方案 B：直接调用 Collector（更接近最小开销测试）

```cpp
#include "client/TracyLiteAll.hpp"

extern "C" void MyHotFunction()
{
    static constexpr tracylite::SourceLocationDataLite srcloc{
        "MyHotFunction", "MyHotFunction", "my_perf_entry.cpp", 1, 0
    };

    tracylite::Collector::ZoneBegin(&srcloc);
    // 热点逻辑
    tracylite::Collector::ZoneEnd();
}
```

---

## 这套方案为什么安全

这套方案可行的关键点是：

1. `TracyLitePerfetto.cpp` 的 C++17 依赖主要来自 `perfetto.h`。
2. `TracyLitePerfetto.hpp` 本身没有把 `perfetto.h` 暴露给其它翻译单元。
3. 因此其它 `.cpp` 仍可以继续按照 C++14 编译。
4. 最终把 C++14 / C++17 的对象文件一起链接进同一个 `.so`，在同一工具链下通常没有问题。

---

## 兼容性注意事项

### 1. 使用同一工具链

最好保证以下内容全部由**同一套编译器 / 标准库**完成：

- 业务 `.so`
- Tracy 源文件
- Perfetto 相关源文件

### 2. ABI 选项必须一致

若工程里显式设置过下列选项，需要保持一致：

- `_GLIBCXX_USE_CXX11_ABI`

### 3. 不建议把 `PerfettoNativeExporter` 当跨工具链公共 ABI 大面积暴露

例如：

- `std::vector<uint8_t>` 这类 STL 类型，最好只在同一工具链控制范围内使用。

作为**内部实现 / 内部 `.so` 导出链**问题不大；
作为**跨编译器公共接口**则应更谨慎。

---

## 什么时候不要走这条方案

如果目标是：

- 整个工程必须严格保持 C++14
- `TracyLitePerfetto.cpp` 也不能单独升到 C++17

那就不能用这份方案，必须继续考虑：

- 裁剪 `perfetto.h`
- 提取 `perfetto_protozero.h`
- 或完全避开 `TRACYLITE_PERFETTO`

---

## 推荐的接入顺序

建议按以下顺序验证：

1. 先验证 `.so` 能否成功编译/链接。
2. 再验证热点函数能否正常运行。
3. 最后再测性能数据。

这样更容易区分：

- CMake/编译问题
- 还是运行时/性能问题

---

## 对应仓内示例

仓内对应示例文件：

- `examples/NoSendPerf/CMakeLists.txt`

该文件体现的就是：

- 目标整体 `cxx_std_14`
- `TracyLitePerfetto.cpp` 单独使用 C++17
- 最终编进 `TracyNoSendPerfCase` 这个 `.so`

---

## 适合后续抽成 skills 的关键词

后续如果要做 skills / prompt，可用这些关键词：

- `Tracy NoSendPerf`
- `C++14 body + single TU C++17`
- `TracyLitePerfetto.cpp`
- `shared library integration`
- `TRACY_SAVE_NO_SEND`
- `TRACYLITE_PERFETTO`
- `perfetto.h requires C++17`
- `minimal CMake snippet`
