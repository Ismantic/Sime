# SIMD 向量化与 PGO 优化实现总结

## 概览

成功实现了两大性能优化技术：
1. **SIMD 向量化** (AVX2)：使用 CPU 向量指令加速热点代码
2. **PGO (Profile-Guided Optimization)**：基于运行时性能数据的编译优化

---

## 一、SIMD 向量化实现

### 1.1 优化目标

热点函数：
- `Scorer::GetNode()` - 语言模型节点查找
- `Scorer::GetLeave()` - 叶子节点查找

这两个函数在解码过程中被频繁调用，优化收益显著。

### 1.2 优化策略

**传统方法**：二分查找 O(log n)
```cpp
std::lower_bound(first, last, target, comparator);
```

**SIMD 优化**：对于小范围 (8-32 个元素) 使用向量化线性扫描
```cpp
// 使用 AVX2 并行比较 8 个元素
__m256i target_vec = _mm256_set1_epi32(target);
__m256i data = _mm256_load_si256(...);
__m256i cmp = _mm256_cmpeq_epi32(data, target_vec);
int mask = _mm256_movemask_epi8(cmp);
```

### 1.3 为什么小范围线性扫描更快？

1. **避免分支预测失败**
   - 二分查找：每次迭代都有条件跳转
   - SIMD 线性扫描：无分支，流水线友好

2. **SIMD 并行性**
   - 一次比较 8 个元素 (AVX2) 或 16 个 (AVX-512)
   - 对于 32 个元素：SIMD 只需 4 次比较 vs 二分查找 5 次

3. **缓存局部性**
   - 顺序访问内存，预取友好
   - 二分查找跳跃访问，缓存不友好

### 1.4 实现细节

**文件修改**：`src/score.cc`

```cpp
#ifdef SIME_HAS_AVX2
    // SIMD path for small ranges
    if (count >= 8 && count <= 32) {
        const __m256i target_vec = _mm256_set1_epi32(static_cast<int>(w));
        const std::size_t simd_end = begin + (count & ~7u);

        for (std::size_t i = begin; i < simd_end; i += 8) {
            // Gather 8 token IDs
            alignas(32) std::uint32_t ids[8];
            for (int j = 0; j < 8; ++j) {
                ids[j] = nodes[i + j].id;
            }

            // SIMD compare
            __m256i data = _mm256_load_si256((__m256i*)ids);
            __m256i cmp = _mm256_cmpeq_epi32(data, target_vec);
            int mask = _mm256_movemask_epi8(cmp);

            if (mask != 0) {
                // Found match
                int byte_pos = __builtin_ctz(mask);
                return i + byte_pos / 4;
            }
        }

        // Handle remaining elements
        for (std::size_t i = simd_end; i < end; ++i) {
            if (nodes[i].id == w) return i;
            if (nodes[i].id > w) return end;
        }
    }
#endif
    // Fallback to binary search for larger ranges
```

### 1.5 编译支持

**CMakeLists.txt 修改**：

```cmake
option(SIME_ENABLE_SIMD "Enable SIMD (AVX2) optimizations" ON)

if(SIME_ENABLE_SIMD)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        message(STATUS "SIMD: Enabling AVX2 optimizations for x86_64")
        target_compile_options(sime_core PRIVATE -mavx2 -mfma)
        target_compile_definitions(sime_core PRIVATE SIME_HAS_AVX2=1)
    endif()
endif()
```

### 1.6 性能预期

- **最佳情况** (小范围查找)：30-50% 提升
- **平均情况**：10-25% 提升
- **最坏情况** (大范围)：无退化 (使用二分查找)

---

## 二、PGO 实现

### 2.1 PGO 工作流程

```
┌─────────────────────────────────────────────────────────────┐
│ Step 1: Instrumentation Build                              │
│ • 编译时插入性能分析代码 (-fprofile-generate)              │
│ • 生成可执行文件                                            │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ Step 2: Profile Run                                         │
│ • 运行代表性工作负载                                        │
│ • 收集运行时性能数据 (.gcda 文件)                           │
│   - 函数调用频率                                            │
│   - 分支跳转统计                                            │
│   - 热点代码路径                                            │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ Step 3: Optimized Build                                     │
│ • 使用性能数据重新编译 (-fprofile-use)                      │
│ • 编译器优化决策：                                          │
│   - 内联热点函数                                            │
│   - 优化分支预测                                            │
│   - 重排代码布局                                            │
│   - 移除冷路径                                              │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 CMake 配置

**文件**：`CMakeLists.txt`

```cmake
option(SIME_ENABLE_PGO_GENERATE "Generate PGO profile data" OFF)
option(SIME_ENABLE_PGO_USE "Use PGO profile data for optimization" OFF)

if(SIME_ENABLE_PGO_GENERATE)
    message(STATUS "PGO: Instrumentation build - will generate profile data")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-generate")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-generate")
    set(PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo-profiles")
    file(MAKE_DIRECTORY ${PGO_PROFILE_DIR})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-dir=${PGO_PROFILE_DIR}")

elseif(SIME_ENABLE_PGO_USE)
    message(STATUS "PGO: Optimized build - using profile data")
    set(PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo-profiles")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-use -fprofile-correction")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-use")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-dir=${PGO_PROFILE_DIR}")
endif()
```

### 2.3 训练数据集

**文件**：`pgo_training_data.txt`

代表性拼音输入，覆盖不同长度和频率：
```
nihao                          # 高频短词
beijing                        # 高频地名
zhongguo                       # 中频词
zhonghuarenmingongheguo        # 低频长词
renminribao                    # 专有名词
gongyewulianguanli             # 技术术语
```

### 2.4 自动化脚本

#### A. `scripts/pgo_generate.sh`
生成性能分析数据：
```bash
./scripts/pgo_generate.sh
```

功能：
1. 使用 `-fprofile-generate` 构建
2. 运行训练工作负载
3. 收集 .gcda 性能数据
4. 验证数据完整性

#### B. `scripts/pgo_build.sh`
使用性能数据构建优化版本：
```bash
./scripts/pgo_build.sh
```

功能：
1. 复制性能数据到新构建目录
2. 使用 `-fprofile-use` 重新编译
3. 生成 PGO 优化的二进制文件

#### C. `scripts/pgo_all.sh`
一键执行完整流程：
```bash
./scripts/pgo_all.sh
```

功能：
1. 调用 `pgo_generate.sh`
2. 调用 `pgo_build.sh`
3. 可选运行性能基准测试

#### D. `scripts/pgo_clean.sh`
清理 PGO 构建产物：
```bash
./scripts/pgo_clean.sh
```

### 2.5 使用示例

**完整 PGO 流程**：
```bash
# 一键执行（推荐）
./scripts/pgo_all.sh

# 或者手动分步执行
./scripts/pgo_generate.sh   # Step 1: 生成 profile
./scripts/pgo_build.sh       # Step 2: 使用 profile 构建
./scripts/pgo_clean.sh       # 清理（可选）
```

**构建输出**：
```
build-pgo-gen/      # 插桩构建目录
  ├── ime_interpreter    (instrumented binary)
  └── pgo-profiles/      (profile data)
      ├── sime_core.gcda
      └── ...

build-pgo-use/      # 优化构建目录
  └── ime_interpreter    (PGO-optimized binary)
```

### 2.6 性能提升机制

PGO 优化编译器决策的关键领域：

1. **函数内联**
   - 热路径函数激进内联
   - 冷路径保持独立

2. **分支预测优化**
   - 根据实际分支统计重排代码
   - 热路径代码紧密排列
   - 冷路径移到末尾

3. **代码布局**
   - 减少指令缓存未命中
   - 优化函数顺序

4. **循环优化**
   - 展开热循环
   - 向量化机会识别

### 2.7 性能预期

- **典型提升**：10-30%
- **最佳场景**：复杂控制流，多分支代码
- **依赖因素**：训练数据代表性

---

## 三、组合优化效果

### 3.1 优化堆叠

```
基准性能 (100%)
  ↓ + FastNetStates (+20%)
  ↓ + 回退缓存 (+5-15%)
  ↓ + 批处理 (+10-20%)
  ↓ + SIMD (+10-25%)
  ↓ + PGO (+10-30%)
═══════════════════════════════
预期总提升: 60-130%
```

### 3.2 最优构建配置

```bash
# 方法 1：使用 PGO 脚本（推荐）
./scripts/pgo_all.sh

# 方法 2：手动 CMake 配置
cmake -DCMAKE_BUILD_TYPE=Release \
      -DSIME_ENABLE_SIMD=ON \
      -DSIME_ENABLE_PGO_USE=ON \
      ..
```

### 3.3 优化验证

**检查 SIMD 支持**：
```bash
# 检查 AVX2 指令是否编译进二进制
objdump -d ime_interpreter | grep -i 'vpaddd\|vpcmp'

# 检查 CPU 支持
lscpu | grep avx2
```

**检查 PGO 效果**：
```bash
# 对比构建大小
ls -lh build/ime_interpreter build-pgo-use/ime_interpreter

# 运行性能基准测试
cd build-pgo-use
../benchmark_optimizations.sh
```

---

## 四、文件结构

```
Sime/
├── CMakeLists.txt              (SIMD + PGO 编译选项)
├── src/
│   └── score.cc                (SIMD 优化的查找函数)
├── scripts/
│   ├── pgo_generate.sh         (PGO: 生成 profile)
│   ├── pgo_build.sh            (PGO: 优化构建)
│   ├── pgo_all.sh              (PGO: 一键执行)
│   └── pgo_clean.sh            (PGO: 清理)
├── pgo_training_data.txt       (PGO 训练数据)
├── SIMD_PGO_DESIGN.md          (设计文档)
└── SIMD_PGO_IMPLEMENTATION.md  (本文档)
```

---

## 五、技术细节

### 5.1 SIMD 对齐要求

```cpp
// AVX2 requires 32-byte alignment
alignas(32) std::uint32_t ids[8];
```

### 5.2 掩码位操作

```cpp
// movemask_epi8 returns 32-bit mask for 8x32-bit comparison
// Each 32-bit element contributes 4 bits (0xF for match, 0x0 for no match)
int mask = _mm256_movemask_epi8(cmp);
if (mask != 0) {
    int byte_pos = __builtin_ctz(mask);  // Count trailing zeros
    int element_index = byte_pos / 4;    // Convert byte to element index
}
```

### 5.3 PGO 配置文件格式

```
# .gcda files (binary format)
- Function call counts
- Branch taken/not-taken statistics
- Block execution counts
- Arc traversal counts
```

---

## 六、性能基准测试

### 6.1 测试方法

```bash
# 基准版本（无 SIMD/PGO）
cmake -DCMAKE_BUILD_TYPE=Release -DSIME_ENABLE_SIMD=OFF ..

# SIMD 版本
cmake -DCMAKE_BUILD_TYPE=Release -DSIME_ENABLE_SIMD=ON ..

# PGO 版本
./scripts/pgo_all.sh

# 基准测试
./benchmark_optimizations.sh
```

### 6.2 预期结果

| 版本 | 相对性能 | 二进制大小 |
|------|---------|-----------|
| 基准 | 100% | ~140KB |
| +SIMD | 110-125% | ~140KB |
| +PGO | 110-130% | ~150KB |
| +SIMD+PGO | 120-160% | ~150KB |

---

## 七、限制与注意事项

### 7.1 SIMD 限制

- **CPU 要求**：AVX2 (Intel Haswell 2013+, AMD Excavator 2015+)
- **移植性**：仅支持 x86_64 (ARM 需要 NEON 实现)
- **编译器**：GCC 4.9+, Clang 3.4+

**检查 CPU 支持**：
```bash
cat /proc/cpuinfo | grep -o 'avx2'
```

### 7.2 PGO 限制

- **训练数据质量**：必须代表实际工作负载
- **数据过时**：代码变更后需重新训练
- **构建复杂性**：需要两次编译

---

## 八、故障排除

### 8.1 SIMD 编译错误

```
错误: #error "AVX2 not supported"
解决: 添加 -mavx2 编译标志或禁用 SIMD
```

### 8.2 PGO 配置文件不匹配

```
警告: profile data may be out of date
解决: 重新运行 pgo_generate.sh
```

### 8.3 性能下降

```
问题: PGO 版本反而更慢
原因: 训练数据不代表实际工作负载
解决: 更新 pgo_training_data.txt 使用实际输入
```

---

## 九、未来优化方向

1. **AVX-512 支持**
   - 一次比较 16 个元素
   - 预期额外提升 10-20%

2. **自适应 SIMD 阈值**
   - 运行时检测最优切换点
   - 不同 CPU 架构自适应

3. **持续 PGO 训练**
   - 收集生产环境数据
   - 定期更新优化配置

4. **GPU 加速**
   - 语言模型评分移至 GPU
   - 适用于大规模批处理

---

## 十、总结

### 成功实现：

✅ **SIMD 向量化**
- AVX2 优化的查找函数
- 自动回退到标量版本
- 预期提升 10-25%

✅ **PGO 支持**
- 完整的自动化工作流
- 一键式 PGO 构建脚本
- 预期提升 10-30%

✅ **组合优化**
- SIMD + PGO + 批处理 + 缓存
- 预期总提升 60-130%

### 关键洞察：

1. **SIMD 不是万能的**：小范围线性扫描 > 二分查找
2. **PGO 需要好数据**：训练集质量决定优化效果
3. **自动化很重要**：脚本降低 PGO 使用门槛
4. **性能测量必不可少**：基准测试验证优化效果

---

**实施者**: Claude Code
**完成时间**: 2026-01-21
**项目**: Sime IME (是语输入法)
