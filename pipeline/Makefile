PYTHON = python3
MIN_COUNT = 16
COUNT_MAX = 83886080
NPROC = 12
SIME = ../build

RAW = sentences.txt
UNITS = units.20260408.txt
CORPUS = sentences.cut.txt
CUT_DICT = dict.txt
DICT = dict.txt
EN_WORDS = en_dict.raw
EN_DICT = sime.en.dict.txt
PUNCT = punct.txt
OUT = output
VOCAB = $(shell wc -l < sime.token.dict.txt 2>/dev/null || echo 0)

all: $(OUT)/sime.raw.cnt $(OUT)/sime.dict

# Step 0: segment raw text
cut: $(CORPUS)
$(CORPUS): $(RAW) $(CUT_DICT) cut.py
	$(PYTHON) cut.py --dict $(CUT_DICT) -j $(NPROC) $(RAW) $(CORPUS)

# Step 1: count all tokens in corpus
chars.cnt: chars.cnt.txt
chars.cnt.txt: count.bin $(CORPUS)
	./count.bin $(CORPUS) chars.cnt.txt

count.bin: count.cc
	g++ -O3 -std=c++17 $< -o $@

merge.bin: merge.cc
	g++ -O3 -std=c++17 $< -o $@

# Step 2: generate token dict + pinyin/english dictionaries
dict: sime.token.dict.txt
sime.token.dict.txt sime.dict.txt $(EN_DICT) &: chars.cnt.txt $(UNITS) $(DICT) $(EN_WORDS) $(PUNCT) japan_chars.txt gen_dict.py
	$(PYTHON) gen_dict.py --min-count $(MIN_COUNT) --cnt chars.cnt.txt --units $(UNITS) --dict $(DICT) --en-words $(EN_WORDS) --punct $(PUNCT) --jp-blacklist japan_chars.txt --token-output sime.token.dict.txt --cn-output sime.dict.txt --en-output $(EN_DICT)

# Step 3: n-gram count (parallel) -- emits $(OUT)/raw.1gram .. raw.3gram
count: $(OUT)/raw.3gram
$(OUT)/raw.1gram $(OUT)/raw.2gram $(OUT)/raw.3gram &: sime.token.dict.txt $(CORPUS) merge.bin parallel_count.sh
	mkdir -p $(OUT)
	./parallel_count.sh -n 3 -d sime.token.dict.txt -o $(OUT)/raw -c $(COUNT_MAX) -j $(NPROC) $(CORPUS)

# Step 4: construct language model (reads $(OUT)/raw.1gram/.2gram/.3gram)
# -w is the *active* vocab: number of distinct unigrams actually in raw.1gram
# (each record is 2*uint32 = 8 bytes), which keeps the MKN gamma normalization
# aligned with the data even if sime.token.dict.txt drifts.
# -r sets the bigram and trigram reserves for entropy pruning; unigrams are
# never pruned so no unigram slot is exposed.
construct: $(OUT)/sime.raw.cnt
$(OUT)/sime.raw.cnt: $(OUT)/raw.3gram sime.token.dict.txt
	AV=$$(( $$(stat -c%s $(OUT)/raw.1gram) / 8 )); \
	echo "active vocab = $$AV"; \
	$(SIME)/sime-construct $(OUT)/raw \
		-n 3 \
		-o $(OUT)/sime.raw.cnt \
		-c 0,1,2 \
		-w $$AV \
		-r 2500000,1000000

# Step 5: convert pinyin dict to trie
convert: $(OUT)/sime.dict
$(OUT)/sime.dict: sime.token.dict.txt sime.dict.txt $(EN_DICT)
	mkdir -p $(OUT)
	$(SIME)/sime-converter sime.token.dict.txt $(OUT)/sime.dict --cn sime.dict.txt --en $(EN_DICT)

clean:
	rm -f count.bin merge.bin $(CORPUS) chars.cnt.txt sime.dict.txt sime.en.dict.txt sime.token.dict.txt
	rm -rf $(OUT)

.PHONY: all cut chars.cnt dict count construct convert clean
