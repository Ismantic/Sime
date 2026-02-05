#!/bin/bash
#
# 获取 Sunpinyin 数据文件脚本
#

set -e

SIME_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_DIR="${SIME_DIR}/data"

echo "======================================"
echo "获取 Sunpinyin 数据文件"
echo "======================================"
echo ""

mkdir -p "${DATA_DIR}"
cd "${DATA_DIR}"

# 方法1: 尝试从 GitHub 下载
echo "方法1: 从 GitHub 下载 sunpinyin 源码..."
if curl -L --connect-timeout 30 -o sunpinyin-master.tar.gz "https://github.com/sunpinyin/sunpinyin/archive/refs/heads/master.tar.gz" 2>/dev/null; then
    echo "  下载成功"
    tar -xzf sunpinyin-master.tar.gz
    
    # 查找数据文件
    if [ -f "sunpinyin-master/data/pydict_sc.bin" ]; then
        cp sunpinyin-master/data/pydict_sc.bin .
        echo "  找到 pydict_sc.bin"
    fi
    
    if [ -f "sunpinyin-master/data/lm_sc.t3g" ]; then
        cp sunpinyin-master/data/lm_sc.t3g .
        echo "  找到 lm_sc.t3g"
    fi
    
    rm -rf sunpinyin-master sunpinyin-master.tar.gz
else
    echo "  下载失败，尝试其他方法..."
fi

# 方法2: 从 Linux 系统复制（如果有 SSH 访问）
if [ ! -f "pydict_sc.bin" ] && [ -n "${LINUX_HOST}" ]; then
    echo ""
    echo "方法2: 从 Linux 服务器 (${LINUX_HOST}) 复制..."
    if scp "${LINUX_HOST}:/usr/share/sunpinyin/pydict_sc.bin" . 2>/dev/null; then
        echo "  成功复制 pydict_sc.bin"
    fi
    if scp "${LINUX_HOST}:/usr/share/sunpinyin/lm_sc.t3g" . 2>/dev/null; then
        echo "  成功复制 lm_sc.t3g"
    fi
fi

# 检查是否获取到文件
echo ""
echo "======================================"
if [ -f "pydict_sc.bin" ]; then
    echo "✓ 找到 pydict_sc.bin"
    ls -lh pydict_sc.bin
else
    echo "✗ 未找到 pydict_sc.bin"
fi

if [ -f "lm_sc.t3g" ]; then
    echo "✓ 找到 lm_sc.t3g"
    ls -lh lm_sc.t3g
else
    echo "✗ 未找到 lm_sc.t3g"
fi
echo "======================================"
echo ""

# 转换字典格式
if [ -f "pydict_sc.bin" ] && [ ! -f "pydict_sc.ime.bin" ]; then
    echo "转换字典格式..."
    if [ -f "${SIME_DIR}/trie_conv" ]; then
        "${SIME_DIR}/trie_conv" --input pydict_sc.bin --output pydict_sc.ime.bin
        echo "✓ 转换完成: pydict_sc.ime.bin"
    else
        echo "✗ 未找到 trie_conv 工具，请先编译:"
        echo "  cd ${SIME_DIR} && g++ trie_conv.cc -o trie_conv"
    fi
fi

echo ""
echo "数据文件位置: ${DATA_DIR}"
echo ""

if [ ! -f "pydict_sc.ime.bin" ] || [ ! -f "lm_sc.t3g" ]; then
    echo "缺少数据文件，请尝试以下方法:"
    echo ""
    echo "1. 从 Linux 系统复制:"
    echo "   scp user@linux-host:/usr/share/sunpinyin/pydict_sc.bin ${DATA_DIR}/"
    echo "   scp user@linux-host:/usr/share/sunpinyin/lm_sc.t3g ${DATA_DIR}/"
    echo ""
    echo "2. 手动下载:"
    echo "   访问 https://github.com/sunpinyin/sunpinyin/tree/master/data"
    echo "   下载 pydict_sc.bin 和 lm_sc.t3g 到 ${DATA_DIR}/"
    echo ""
    echo "3. 使用 Docker 获取:"
    echo "   docker run --rm -v ${DATA_DIR}:/output ubuntu bash -c \\"
    echo "     'apt-get update && apt-get install -y sunpinyin-data && \\"
    echo "      cp /usr/share/sunpinyin/* /output/'"
    echo ""
    exit 1
fi

echo "✓ 所有数据文件已就绪!"
echo ""
echo "安装到输入法:"
echo "  cp ${DATA_DIR}/pydict_sc.ime.bin ${DATA_DIR}/lm_sc.t3g \\"
echo "    ~/Library/Input\\ Methods/SimeIME.app/Contents/Resources/"
