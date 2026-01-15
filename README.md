是语输入法

1) Data Prepare

sudo pacman -S sunpinyin-data
cp /usr/share/sunpinyin/pydict_sc.bin .
cp /usr/share/sunpinyin/lm_sc.t3g .

g++ trie_conv.cc -o trie_conv
./trie_conv --input pydict_sc.bin --output pydict_sc.ime.bin

2) Build and Test
mkdir build
cd build
cmake ..
make
 ./ime_interpreter --pydict ../pydict_sc.ime.bin --lm ../lm_sc.t3g


TODO
调整词表，调整语料，
