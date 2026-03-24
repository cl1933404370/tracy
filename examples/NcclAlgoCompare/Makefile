# ═══════════════════════════════════════════════════════════════════════════
# Makefile — NCCL/HCCL AllReduce Algorithm Compare (ChromeExport tracing)
#
# 用法:
#   make                    # 编译
#   make run                # 编译+运行
#   make clean              # 清理
#
# 如果要放到 MPICH 源码树里编译，只需要:
#   1. 把 TRACY_ROOT 指向 tracy 的 public/ 目录
#   2. 把 TRACY_SYSTEM_SRC 加到你的 Makefile 的 SRCS 里
#   3. CXXFLAGS 加 -I$(TRACY_ROOT)
#   4. 在你想要 instrument 的 .cpp 里 #include "tracy/TracyHcomm.hpp"
#
# 示例: 在 MPICH 的 src/mpi/coll/allreduce/ 里:
#   CXXFLAGS += -I/path/to/tracy/public
#   SRCS     += /path/to/tracy/public/common/TracySystem.cpp
#   然后在 allreduce_ring.c 或 allreduce_tree.c 里直接打点
# ═══════════════════════════════════════════════════════════════════════════

# ── Tracy 路径 (相对于本 Makefile) ─────────────────────────────────────────
TRACY_ROOT       := ../../public
TRACY_SYSTEM_SRC := $(TRACY_ROOT)/common/TracySystem.cpp

# ── 编译器 & 选项 ─────────────────────────────────────────────────────────
CXX      := g++
CXXFLAGS := -std=c++14 -O2 -g -Wall
CXXFLAGS += -I$(TRACY_ROOT)
LDFLAGS  := -lpthread

# 如果用 MPICH 编译，替换为:
# CXX := mpicxx
# LDFLAGS += -lmpi

# ── 源文件 ─────────────────────────────────────────────────────────────────
TARGET := nccl_algo_compare
SRCS   := nccl_algo_compare.cpp $(TRACY_SYSTEM_SRC)
OBJS   := $(patsubst %.cpp,%.o,$(notdir $(SRCS)))

# ── 规则 ──────────────────────────────────────────────────────────────────
all: $(TARGET)

$(TARGET): nccl_algo_compare.o TracySystem.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

nccl_algo_compare.o: nccl_algo_compare.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

TracySystem.o: $(TRACY_SYSTEM_SRC)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)
	@echo ""
	@echo "=== trace.json ready ==="
	@head -3 trace.json
	@echo "..."
	@wc -l trace.json

clean:
	rm -f $(TARGET) *.o trace.json trace.tracy

.PHONY: all run clean
