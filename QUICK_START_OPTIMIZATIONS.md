# Sime IME 优化快速入门

## 🚀 一键最优构建

```bash
# 执行完整 PGO 优化流程（推荐）
./scripts/pgo_all.sh

# 优化后的二进制文件位于：
# build-pgo-use/ime_interpreter
```

---

## 📊 优化选项对比

| 构建方式 | 性能 | 构建时间 | 使用场景 |
|---------|------|---------|---------|
| 标准 Release | ⭐⭐⭐ | 快 (1x) | 开发测试 |
| + SIMD | ⭐⭐⭐⭐ | 快 (1x) | 日常使用 |
| + PGO | ⭐⭐⭐⭐⭐ | 慢 (2x) | 生产部署 |

---

## ⚙️ 手动构建选项

### 选项 1：标准 Release 构建
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 选项 2：启用 SIMD
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DSIME_ENABLE_SIMD=ON \
      ..
make -j$(nproc)
```

### 选项 3：完整 PGO 优化（最佳性能）
```bash
# 使用自动化脚本（推荐）
./scripts/pgo_all.sh

# 或手动执行
./scripts/pgo_generate.sh  # 生成 profile 数据
./scripts/pgo_build.sh     # 使用 profile 优化构建
```

---

## 🎯 性能优化总览

已实现的优化技术：

1. ✅ **FastNetStates** (+20%)
   - 平面哈希表替代嵌套 map
   - 分阶段剪枝

2. ✅ **回退缓存** (+5-15%)
   - 缓存 Scorer::Back() 结果
   - 避免重复计算

3. ✅ **批处理** (+10-20%)
   - 列级批处理状态转移
   - 提高缓存局部性

4. ✅ **SIMD 向量化** (+10-25%)
   - AVX2 优化的查找函数
   - 小范围并行搜索

5. ✅ **PGO** (+10-30%)
   - 基于运行时数据优化
   - 改进分支预测和内联

**组合效果**: 60-130% 性能提升

---

## 📁 构建产物位置

```
Sime/
├── build/                    # 标准构建
│   └── ime_interpreter
├── build-pgo-gen/           # PGO 插桩构建
│   ├── ime_interpreter       (instrumented)
│   └── pgo-profiles/         (profile data)
└── build-pgo-use/           # PGO 优化构建 ⭐
    └── ime_interpreter       (最优性能)
```

---

## 🧪 性能测试

```bash
# 进入构建目录
cd build-pgo-use  # 或 build

# 运行基准测试
../benchmark_optimizations.sh
```

---

## 🧹 清理

```bash
# 清理 PGO 构建目录
./scripts/pgo_clean.sh

# 清理标准构建
rm -rf build
```

---

## 🔍 验证优化

### 检查 SIMD 支持
```bash
# 检查 CPU 是否支持 AVX2
lscpu | grep avx2

# 检查二进制是否包含 AVX2 指令
objdump -d build/ime_interpreter | grep -i 'vpcmp\|vpadd' | head -5
```

### 检查 PGO 效果
```bash
# 对比二进制大小
ls -lh build/ime_interpreter build-pgo-use/ime_interpreter

# 对比性能（运行相同输入）
time echo "nihao" | build/ime_interpreter --pydict ... --lm ... --nbest 5
time echo "nihao" | build-pgo-use/ime_interpreter --pydict ... --lm ... --nbest 5
```

---

## 📚 详细文档

- **设计文档**: `SIMD_PGO_DESIGN.md`
- **实现文档**: `SIMD_PGO_IMPLEMENTATION.md`
- **批处理优化**: `BATCH_BACK_OPTIMIZATION.md`
- **优化总结**: `OPTIMIZATION_SUMMARY.md`

---

## ❓ 常见问题

### Q: SIMD 优化在我的 CPU 上不工作？
A: 检查 CPU 是否支持 AVX2 (`lscpu | grep avx2`)。如不支持，代码会自动回退到标量版本。

### Q: PGO 构建时间太长？
A: PGO 需要编译两次（插桩 + 优化），适合生产部署。日常开发可只用 SIMD 构建。

### Q: 如何更新 PGO 训练数据？
A: 编辑 `pgo_training_data.txt`，添加更多代表性输入，然后重新运行 `./scripts/pgo_all.sh`

### Q: 性能没有明显提升？
A:
1. 确保使用 Release 构建 (`-DCMAKE_BUILD_TYPE=Release`)
2. 检查是否在快速 SSD 上运行
3. 验证所有优化已启用 (`cmake -L | grep SIME_ENABLE`)

---

## 🎓 最佳实践

1. **开发阶段**: 使用标准 Release 或 SIMD 构建
2. **测试阶段**: 使用 SIMD 构建验证功能
3. **生产部署**: 使用 PGO 完整优化构建

**推荐流程**:
```bash
# 开发
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DSIME_ENABLE_SIMD=ON ..
make -j$(nproc)

# 发布前
cd ..
./scripts/pgo_all.sh  # 生成最优二进制

# 部署
cp build-pgo-use/ime_interpreter /opt/sime/
```

---

**快速链接**:
- [完整设计](SIMD_PGO_DESIGN.md)
- [实现细节](SIMD_PGO_IMPLEMENTATION.md)
- [问题反馈](https://github.com/your-repo/issues)
