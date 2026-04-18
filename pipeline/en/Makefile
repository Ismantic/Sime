PYTHON = python3
COUNT_MAX = 83886080
NPROC = 4
SIME = ../../build

RAW = CnnDailyMail.sentences.txt
CORPUS = en.sentences.cut.txt
MIN_COUNT = 16
EN_WORDS = en_dict.raw
EN_DICT = sime.en.dict.txt
OUT = output
VOCAB = $(shell wc -l < sime.en.token.dict.txt 2>/dev/null || echo 0)

all: $(OUT)/sime.en.dict $(OUT)/sime.en.raw.cnt

# Step 0: segment raw text (no dict = char-level for Chinese)
cut: $(RAW)
	$(PYTHON) ../cut.py -j $(NPROC) $(RAW) $(CORPUS)

# Step 1: count all tokens in corpus
chars.cnt: count.bin $(CORPUS)
	./count.bin $(CORPUS) chars.cnt.txt

count.bin: ../count.cc
	g++ -O3 -std=c++17 $< -o $@

# Step 2: generate token dict + english dict
dict: chars.cnt.txt $(EN_WORDS)
	$(PYTHON) gen_token_dict.py --min-count $(MIN_COUNT) --cnt chars.cnt.txt --en-words $(EN_WORDS) --token-output sime.en.token.dict.txt --en-output $(EN_DICT)

merge.bin: ../merge.cc
	g++ -O3 -std=c++17 $< -o $@

# Step 2: n-gram count (parallel) -- emits $(OUT)/raw.1gram .. raw.3gram
count: sime.en.token.dict.txt $(CORPUS)
	mkdir -p $(OUT)
	../parallel_count.sh -n 3 -d sime.en.token.dict.txt -o $(OUT)/raw -c $(COUNT_MAX) -j $(NPROC) $(CORPUS)

# Step 3: construct language model (reads $(OUT)/raw.1gram/.2gram/.3gram)
# -w is derived from raw.1gram (record size = 2*uint32 = 8 bytes) so the MKN
# gamma normalization matches the data regardless of dict/corpus alignment.
construct: $(OUT)/raw.3gram sime.en.token.dict.txt
	AV=$$(( $$(stat -c%s $(OUT)/raw.1gram) / 8 )); \
	echo "active vocab = $$AV"; \
	$(SIME)/sime-construct $(OUT)/raw \
		-n 3 \
		-o $(OUT)/sime.en.raw.cnt \
		-c 0,2,5 \
		-w $$AV \
		-r $$((AV + 3)),2000000,2000000

# Step 4: convert token dict to dict
convert: sime.en.token.dict.txt $(EN_DICT)
	$(SIME)/sime-converter sime.en.token.dict.txt $(OUT)/sime.en.dict --en $(EN_DICT)

clean:
	rm -f count.bin merge.bin chars.cnt.txt sime.en.token.dict.txt $(EN_DICT)
	rm -rf $(OUT)

.PHONY: all cut chars dict count construct convert clean
