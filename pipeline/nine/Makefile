PYTHON = python3
MAX_LINES = 8000000
SIME = ../../build
OUT = output

VOCAB = $(shell wc -l < pinyin.dict.txt 2>/dev/null || echo 0)

# Step 1: convert sentences to pinyin
units: ../sentences.txt
	$(PYTHON) gen_pinyin_corpus.py ../sentences.txt sentences.units.txt $(MAX_LINES)

# Step 2: export pinyin syllable dict from dict.inc
dict:
	$(PYTHON) gen_pinyin_dict.py ../../src/dict.inc pinyin.dict.txt

# Step 3: generate sime-count training data
data: sentences.units.txt pinyin.dict.txt
	$(PYTHON) gen_count_data.py sentences.units.txt data.cut.txt pinyin.dict.txt

# Step 4: bigram count
count: data.cut.txt pinyin.dict.txt
	mkdir -p $(OUT)
	$(SIME)/sime-count -n 2 \
		-d pinyin.dict.txt \
		-s $(OUT)/swap.bin \
		-o $(OUT)/raw.2gram \
		-c 8388608 \
		data.cut.txt

# Step 5: construct language model
construct: $(OUT)/raw.2gram pinyin.dict.txt
	$(SIME)/sime-construct $(OUT)/raw.2gram \
		-n 2 \
		-o $(OUT)/raw.slm \
		-c 0,0 \
		-w $(VOCAB) \
		-r $$(($(VOCAB) + 3)),200000

# Step 6: compact
compact: $(OUT)/raw.slm
	$(SIME)/sime-compact $(OUT)/raw.slm $(OUT)/sime.nine

clean:
	rm -f sentences.units.txt data.cut.txt pinyin.dict.txt
	rm -rf $(OUT)

.PHONY: units dict data count construct compact clean
