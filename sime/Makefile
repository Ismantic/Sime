PYTHON = python3
MIN_COUNT = 16
COUNT_MAX = 83886080
SIME = ../build

CORPUS = sentences.cut.txt
OUT = output
VOCAB = $(shell wc -l < sime.dict.txt 2>/dev/null || echo 0)

all: $(OUT)/sime.trie $(OUT)/sime.cnt

# Step 1: count all tokens in corpus
chars: count.bin $(CORPUS)
	./count.bin $(CORPUS) chars.cnt.txt

count.bin: count.cc
	g++ -O3 -std=c++17 $< -o $@

# Step 2: generate pinyin dictionary
dict: chars.cnt.txt chinese_units.txt
	$(PYTHON) gen_dict.py --min-count $(MIN_COUNT) --cnt chars.cnt.txt --units chinese_units.txt --output sime.dict.txt

# Step 3: n-gram count
count: sime.dict.txt $(CORPUS)
	mkdir -p $(OUT)
	$(SIME)/sime-count -n 3 \
		-d sime.dict.txt \
		-s $(OUT)/swap.bin \
		-o $(OUT)/raw.3gram \
		-c $(COUNT_MAX) \
		$(CORPUS)

# Step 4: construct language model
construct: $(OUT)/raw.3gram sime.dict.txt
	$(SIME)/sime-construct $(OUT)/raw.3gram \
		-n 3 \
		-o $(OUT)/raw.slm \
		-c 0,5,10 \
		-w $(VOCAB) \
		-r $$(($(VOCAB) + 3)),2000000,2000000

# Step 5: convert pinyin dict to trie
convert: sime.dict.txt
	$(SIME)/sime-converter sime.dict.txt $(OUT)/sime.trie

# Step 6: compact (quantize + compress)
compact: $(OUT)/raw.slm
	$(SIME)/sime-compact $(OUT)/raw.slm $(OUT)/sime.cnt

clean:
	rm -f count.bin chars.cnt.txt sime.dict.txt
	rm -rf $(OUT)

.PHONY: all chars dict count construct convert compact clean
