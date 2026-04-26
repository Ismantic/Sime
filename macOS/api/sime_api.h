#pragma once
// Pure C API for sime_core — imported by the Swift bridging header.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SimeHandle SimeHandle;

typedef struct {
  char *text;        // UTF-8 display text (malloc'd)
  char *units;       // segmented pinyin e.g. "ni'hao" (malloc'd)
  uint32_t *tokens;  // token IDs for LM context (malloc'd)
  int token_count;
  float score;
  int consumed;  // bytes of pinyin input consumed
} SimeResult;

typedef struct {
  SimeResult *items;  // malloc'd array
  int count;
} SimeResults;

// Lifecycle
SimeHandle *sime_create(const char *dict_path, const char *cnt_path);
void sime_destroy(SimeHandle *h);
bool sime_ready(const SimeHandle *h);
int sime_context_size(const SimeHandle *h);

// Decoding
// decode_sentence: full Viterbi sentence decode; extra = number of N-best
// alternatives beyond the top result.
SimeResults sime_decode_sentence(const SimeHandle *h, const char *input,
                                 int extra);
// decode_str: single-word / multi-word candidates (all starting at input[0])
SimeResults sime_decode_str(const SimeHandle *h, const char *input, int num);

// Prediction: given LM context token IDs, return likely next words.
SimeResults sime_next_tokens(const SimeHandle *h, const uint32_t *tokens,
                             int count, int num);

// Free results returned by any of the above.
void sime_free_results(SimeResults *r);

#ifdef __cplusplus
}
#endif
