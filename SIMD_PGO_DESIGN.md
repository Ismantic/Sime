# SIMD 向量化与 PGO 优化设计

## 目标

1. **SIMD 向量化**: 使用 AVX2/AVX-512 指令加速热点代码路径
2. **PGO (Profile-Guided Optimization)**: 基于运行时性能数据优化编译

## 一、SIMD 向量化优化

### 1.1 热点分析

通过分析代码，识别出以下 SIMD 优化机会：

#### 候选 1: 二分查找 (GetNode/GetLeave)
```cpp
// 当前实现：串行二分查找
std::lower_bound(first, last, w, comparator);
```

**SIMD 优化方案**:
- 对于小范围搜索 (< 32 个元素)，使用 SIMD 线性扫描
- 使用 `_mm256_cmpeq_epi32` 并行比较 8 个元素 (AVX2)
- 适用于叶子节点的 token 查找

#### 候选 2: 批处理状态转移
```cpp
// 当前：逐个处理转移
for (const auto& trans : transitions) {
    ScoreMove(...);
    BackCached(...);
}
```

**SIMD 优化方案**:
- 批量加载 scorer states
- 向量化概率查表操作
- 批量计算 scores

#### 候选 3: 概率表查找
```cpp
// pr_table_[index] 批量查找
double pr = pr_table_[next_state.pr];
```

**SIMD 优化方案**:
- 使用 gather 指令批量加载概率值
- `_mm256_i32gather_ps` (AVX2)

### 1.2 实现策略

**编译时检测**:
```cpp
#if defined(__AVX2__)
  #define SIME_HAS_AVX2 1
#endif

#if defined(__AVX512F__)
  #define SIME_HAS_AVX512 1
#endif
```

**运行时检测**:
```cpp
bool has_avx2 = __builtin_cpu_supports("avx2");
bool has_avx512f = __builtin_cpu_supports("avx512f");
```

**选择性优化**:
- 为小数据集保留标量版本
- SIMD 版本用于大批量处理
- 编译选项控制启用/禁用

### 1.3 具体实现

#### A. SIMD 线性搜索 (小范围)

```cpp
// 对于 <= 32 个元素的范围，使用 SIMD 线性扫描
std::size_t SIMDLinearSearch(const NodeEntry* entries,
                              std::size_t count,
                              TokenID target) {
#ifdef __AVX2__
    if (count <= 32 && count >= 8) {
        __m256i target_vec = _mm256_set1_epi32(target);
        for (size_t i = 0; i < count; i += 8) {
            __m256i data = _mm256_loadu_si256(
                (__m256i*)&entries[i].id);
            __m256i cmp = _mm256_cmpeq_epi32(data, target_vec);
            int mask = _mm256_movemask_epi8(cmp);
            if (mask != 0) {
                return i + __builtin_ctz(mask) / 4;
            }
        }
    }
#endif
    // Fallback to binary search
    return BinarySearch(entries, count, target);
}
```

#### B. 批量概率查找

```cpp
// 使用 gather 指令批量加载概率
void GatherProbabilities(const float* pr_table,
                         const uint32_t* indices,
                         float* output,
                         size_t count) {
#ifdef __AVX2__
    for (size_t i = 0; i < count; i += 8) {
        __m256i idx = _mm256_loadu_si256((__m256i*)&indices[i]);
        __m256 probs = _mm256_i32gather_ps(pr_table, idx, 4);
        _mm256_storeu_ps(&output[i], probs);
    }
#endif
}
```

### 1.4 编译选项

```cmake
# 在 CMakeLists.txt 中添加
option(SIME_ENABLE_SIMD "Enable SIMD optimizations" ON)

if(SIME_ENABLE_SIMD)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        # AVX2 support (broader compatibility)
        target_compile_options(sime_core PRIVATE -mavx2 -mfma)
        # Optionally enable AVX-512 if detected
        # target_compile_options(sime_core PRIVATE -mavx512f)
    endif()
endif()
```

---

## 二、PGO (Profile-Guided Optimization)

### 2.1 工作原理

PGO 分三个阶段：

1. **Instrumentation Build**: 编译时插入性能分析代码
2. **Profile Run**: 运行程序收集性能数据
3. **Optimized Build**: 使用性能数据重新编译优化

### 2.2 收益

- **更好的内联决策**: 基于实际调用频率
- **更准确的分支预测**: 优化热路径
- **更优的代码布局**: 减少指令缓存未命中
- **预期性能提升**: 10-30%

### 2.3 实现方案

#### A. CMake 配置

```cmake
# PGO support
option(SIME_ENABLE_PGO_GENERATE "Generate PGO profile data" OFF)
option(SIME_ENABLE_PGO_USE "Use PGO profile data" OFF)

if(SIME_ENABLE_PGO_GENERATE)
    message(STATUS "PGO: Generating profile data")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-generate")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-generate")
elseif(SIME_ENABLE_PGO_USE)
    message(STATUS "PGO: Using profile data")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-use -fprofile-correction")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-use")
endif()
```

#### B. 训练数据生成脚本

```bash
#!/bin/bash
# pgo_train.sh - Generate PGO training data

# Step 1: Build with instrumentation
mkdir -p build-pgo-gen
cd build-pgo-gen
cmake -DCMAKE_BUILD_TYPE=Release \
      -DSIME_ENABLE_PGO_GENERATE=ON ..
make -j$(nproc)

# Step 2: Run representative workload
./ime_interpreter --pydict ../pydict_sc.ime.bin \
                  --lm ../lm_sc.t3g \
                  --nbest 5 < ../pgo_training_data.txt

# Step 3: Rebuild with profile data
cd ..
mkdir -p build-pgo-use
cd build-pgo-use
cmake -DCMAKE_BUILD_TYPE=Release \
      -DSIME_ENABLE_PGO_USE=ON ..
# Copy profile data
cp ../build-pgo-gen/*.gcda .
make -j$(nproc)
```

#### C. 训练数据集

创建代表性输入数据：
```text
# pgo_training_data.txt
nihao
beijing
zhongguo
shijie
renmin
gongheguo
zhonghuarenmingongheguo
renminribao
gongyewulianguanli
:q
```

### 2.4 使用流程

```bash
# 1. 生成配置文件
./scripts/pgo_generate.sh

# 2. 使用配置文件优化构建
./scripts/pgo_build.sh

# 3. 清理配置文件
./scripts/pgo_clean.sh
```

---

## 三、组合优化

### 3.1 优化堆叠

```
基准性能 (100%)
  ↓ + FastNetStates (20%)
  ↓ + 回退缓存 (5-15%)
  ↓ + 批处理 (10-20%)
  ↓ + SIMD (10-25%)
  ↓ + PGO (10-30%)
═══════════════════════════════
预期总提升: 60-120%
```

### 3.2 编译配置

**最优性能配置**:
```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DSIME_USE_FAST_STATES=ON \
      -DSIME_USE_BATCH_PROCESSING=ON \
      -DSIME_ENABLE_SIMD=ON \
      -DSIME_ENABLE_PGO_USE=ON \
      ..
```

---

## 四、实现计划

### Phase 1: SIMD 基础设施 ✓
- [x] 添加 SIMD 检测代码
- [x] 创建 SIMD 工具函数
- [x] 添加编译选项

### Phase 2: SIMD 优化热点 ✓
- [x] 实现 SIMD 二分查找
- [x] 实现批量概率查找
- [ ] 向量化状态转移（可选）

### Phase 3: PGO 支持 ✓
- [x] 添加 CMake PGO 选项
- [x] 创建训练脚本
- [x] 创建训练数据集

### Phase 4: 测试验证 ✓
- [x] 功能测试
- [x] 性能基准测试
- [x] 不同 CPU 架构验证

---

## 五、风险与限制

### 5.1 SIMD

**优势**:
- 显著性能提升
- 利用现代 CPU 特性
- 编译时可选

**限制**:
- 需要 AVX2 支持 (2013+ CPU)
- 代码复杂度增加
- 移植性考虑（ARM 需要 NEON）

### 5.2 PGO

**优势**:
- 自动优化
- 针对实际工作负载
- GCC/Clang 原生支持

**限制**:
- 需要代表性训练数据
- 构建流程更复杂
- Profile 数据可能过时

---

## 六、性能预期

| 优化技术 | 预期提升 | 实现难度 | 风险 |
|---------|---------|---------|------|
| FastNetStates | +20% | 中 | 低 |
| 回退缓存 | +5-15% | 低 | 低 |
| 批处理 | +10-20% | 中 | 低 |
| SIMD | +10-25% | 高 | 中 |
| PGO | +10-30% | 低 | 低 |
| **总计** | **+60-120%** | - | - |

---

## 七、参考资料

- Intel Intrinsics Guide: https://www.intel.com/content/www/us/en/docs/intrinsics-guide/
- GCC PGO Documentation: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
- AVX2 Programming Reference
