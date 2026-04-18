PYTHON = python3
MIN_COUNT = 16
COUNT_MAX = 83886080
NPROC = 2
SIME = ../build

RAW = News.sentences.txt
UNITS = units.20260408.txt
CORPUS = sentences.cut.txt
CUT_DICT = dict.txt
DICT = dict.txt
EN_WORDS = en_dict.raw
EN_DICT = sime.en.dict.txt
OUT = output
VOCAB = $(shell wc -l < sime.token.dict.txt 2>/dev/null || echo 0)

all: $(OUT)/sime.dict $(OUT)/sime.raw.cnt

# Step 0: segment raw text
cut: $(RAW) $(CUT_DICT)
	$(PYTHON) cut.py --dict $(CUT_DICT) -j $(NPROC) $(RAW) $(CORPUS)

# Step 1: count all tokens in corpus
chars.cnt: count.bin $(CORPUS)
	./count.bin $(CORPUS) chars.cnt.txt

count.bin: count.cc
	g++ -O3 -std=c++17 $< -o $@

merge.bin: merge.cc
	g++ -O3 -std=c++17 $< -o $@

# Step 2: generate token dict + pinyin/english dictionaries
dict: chars.cnt.txt $(UNITS) $(DICT) $(EN_WORDS)
	$(PYTHON) gen_dict.py --min-count $(MIN_COUNT) --cnt chars.cnt.txt --units $(UNITS) --dict $(DICT) --en-words $(EN_WORDS) --token-output sime.token.dict.txt --cn-output sime.dict.txt --en-output $(EN_DICT)

# Step 3: n-gram count (parallel)
count: sime.token.dict.txt $(CORPUS)
	mkdir -p $(OUT)
	./parallel_count.sh -n 3 -d sime.token.dict.txt -o $(OUT)/raw.3gram -c $(COUNT_MAX) -j $(NPROC) $(CORPUS)

# Step 4: construct language model
construct: $(OUT)/raw.3gram sime.token.dict.txt
	$(SIME)/sime-construct $(OUT)/raw.3gram \
		-n 3 \
		-o $(OUT)/sime.raw.cnt \
		-c 0,2,5 \
		-w $(VOCAB) \
		-r $$(($(VOCAB) + 3)),3000000,3000000

# Step 5: convert pinyin dict to trie
convert: sime.token.dict.txt sime.dict.txt $(EN_DICT)
	$(SIME)/sime-converter sime.token.dict.txt $(OUT)/sime.dict --cn sime.dict.txt --en $(EN_DICT)

clean:
	rm -f count.bin merge.bin chars.cnt.txt sime.dict.txt sime.en.dict.txt sime.token.dict.txt
	rm -rf $(OUT)

.PHONY: all cut chars dict count construct convert clean
