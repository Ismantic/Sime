PYTHON = python3
MIN_COUNT = 50
COUNT_MAX = 83886080
NPROC = 16
SIME = ../../build

CORPUS = sentences.en.cut.txt
OUT = output
VOCAB = $(shell wc -l < sime.en.token.dict.txt 2>/dev/null || echo 0)

all: $(OUT)/sime.en.dict $(OUT)/sime.en.cnt

# Step 1: count all tokens in corpus
chars.cnt: count.bin $(CORPUS)
	./count.bin $(CORPUS) chars.cnt.txt

count.bin: ../count.cc
	g++ -O3 -std=c++17 $< -o $@

merge.bin: ../merge.cc
	g++ -O3 -std=c++17 $< -o $@

# Step 2: generate token dictionary (no pinyin)
dict: chars.cnt.txt
	$(PYTHON) gen_token_dict.py --min-count $(MIN_COUNT) --cnt chars.cnt.txt --output sime.en.token.dict.txt

# Step 3: n-gram count (parallel)
count: sime.en.token.dict.txt $(CORPUS)
	mkdir -p $(OUT)
	../parallel_count.sh -n 3 -d sime.en.token.dict.txt -o $(OUT)/raw.3gram -c $(COUNT_MAX) -j $(NPROC) $(CORPUS)

# Step 4: construct language model
construct: $(OUT)/raw.3gram sime.en.token.dict.txt
	$(SIME)/sime-construct $(OUT)/raw.3gram \
		-n 3 \
		-o $(OUT)/raw.slm \
		-c 0,5,10 \
		-w $(VOCAB) \
		-r $$(($(VOCAB) + 3)),2000000,2000000

# Step 5: convert token dict to dict
# Requires en_dict.cut.txt (generated separately)
convert: sime.en.token.dict.txt en_dict.cut.txt
	$(SIME)/sime-converter sime.en.token.dict.txt $(OUT)/sime.en.dict en_dict.cut.txt

# Step 6: compact (quantize + compress)
compact: $(OUT)/raw.slm
	$(SIME)/sime-compact $(OUT)/raw.slm $(OUT)/sime.en.cnt

clean:
	rm -f count.bin merge.bin chars.cnt.txt sime.en.token.dict.txt
	rm -rf $(OUT)

.PHONY: all chars dict count construct convert compact clean
