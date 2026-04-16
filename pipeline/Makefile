PYTHON = python3
MIN_COUNT = 16
COUNT_MAX = 83886080
NPROC = 12
SIME = ../build

UNITS = units.20260408.txt
CORPUS = sentences.cut.txt
DICT = dict.txt
PIECES = piece.txt
EN_DICT = en_dict.cut.txt
OUT = output
VOCAB = $(shell wc -l < sime.token.dict.txt 2>/dev/null || echo 0)

all: $(OUT)/sime.dict $(OUT)/sime.cnt

# Step 1: count all tokens in corpus
chars.cnt: count.bin $(CORPUS)
	./count.bin $(CORPUS) chars.cnt.txt

count.bin: count.cc
	g++ -O3 -std=c++17 $< -o $@

merge.bin: merge.cc
	g++ -O3 -std=c++17 $< -o $@

# Step 2: generate token dict + pinyin dictionary
dict: chars.cnt.txt $(UNITS) $(DICT) $(PIECES)
	$(PYTHON) gen_dict.py --min-count $(MIN_COUNT) --cnt chars.cnt.txt --units $(UNITS) --output sime.dict.txt --token-output sime.token.dict.txt --dict $(DICT) --pieces $(PIECES)

# Step 3: n-gram count (parallel)
count: sime.token.dict.txt $(CORPUS)
	mkdir -p $(OUT)
	./parallel_count.sh -n 3 -d sime.token.dict.txt -o $(OUT)/raw.3gram -c $(COUNT_MAX) -j $(NPROC) $(CORPUS)

# Step 4: construct language model
construct: $(OUT)/raw.3gram sime.token.dict.txt
	$(SIME)/sime-construct $(OUT)/raw.3gram \
		-n 3 \
		-o $(OUT)/raw.slm \
		-c 0,2,5 \
		-w $(VOCAB) \
		-r $$(($(VOCAB) + 3)),3000000,3000000

# Step 5: convert pinyin dict to trie
convert: sime.token.dict.txt sime.dict.txt $(EN_DICT)
	$(SIME)/sime-converter sime.token.dict.txt $(OUT)/sime.dict sime.dict.txt $(EN_DICT)

# Step 6: compact (quantize + compress)
compact: $(OUT)/raw.slm
	$(SIME)/sime-compact $(OUT)/raw.slm $(OUT)/sime.cnt

clean:
	rm -f count.bin merge.bin chars.cnt.txt sime.dict.txt sime.token.dict.txt
	rm -rf $(OUT)

.PHONY: all chars dict count construct convert compact clean
