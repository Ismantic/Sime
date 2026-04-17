PYTHON = python3
COUNT_MAX = 83886080
NPROC = 16
SIME = ../../build

CORPUS = en_sentences.en.cut.txt
PIECES = piece.txt
EN_DICT = en_dict_cut.en.txt
OUT = output
VOCAB = $(shell wc -l < sime.en.token.dict.txt 2>/dev/null || echo 0)

all: $(OUT)/sime.en.dict $(OUT)/sime.en.raw.cnt

# Step 1: extract token dict from pieces file
dict: $(PIECES)
	$(PYTHON) extract_tokens.py $(PIECES) sime.en.token.dict.txt

merge.bin: ../merge.cc
	g++ -O3 -std=c++17 $< -o $@

# Step 2: n-gram count (parallel)
count: sime.en.token.dict.txt $(CORPUS)
	mkdir -p $(OUT)
	../parallel_count.sh -n 3 -d sime.en.token.dict.txt -o $(OUT)/raw.3gram -c $(COUNT_MAX) -j $(NPROC) $(CORPUS)

# Step 3: construct language model
construct: $(OUT)/raw.3gram sime.en.token.dict.txt
	$(SIME)/sime-construct $(OUT)/raw.3gram \
		-n 3 \
		-o $(OUT)/sime.en.raw.cnt \
		-c 0,2,5 \
		-w $(VOCAB) \
		-r $$(($(VOCAB) + 3)),500000,500000

# Step 4: convert token dict to dict
convert: sime.en.token.dict.txt $(EN_DICT)
	$(SIME)/sime-converter sime.en.token.dict.txt $(OUT)/sime.en.dict --en $(EN_DICT)

clean:
	rm -f merge.bin sime.en.token.dict.txt
	rm -rf $(OUT)

.PHONY: all dict count construct convert clean
