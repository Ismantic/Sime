#include "segment.h"

#include "sentence.h"
#include "ustr.h"

namespace sime {
void Segmenter::SegmentStream(std::istream& in,
                              std::ostream& out, 
                              SegmentOptions options) const {
    SentenceReader reader(in);
    std::u32string sentence; 
    std::size_t token_count = 0;

    if (!options.text_output) {
        out.write(reinterpret_cast<const char*>(&options.sentence_token), 
                  sizeof(TokenID));
        ++token_count;
    }

    while (reader.NextSentence(sentence)) {
        EmitSentence(sentence, out, options, token_count);
        if (!options.text_output) {
            out.write(reinterpret_cast<const char*>(&options.sentence_token),
                      sizeof(TokenID));
            ++token_count;
        }
    }
}

void Segmenter::EmitSentence(const std::u32string& sentence, 
                             std::ostream& out, 
                             SegmentOptions options, 
                             std::size_t& token_count) const {
    TokenID previous_id = options.sentence_token;
    for (std::size_t idx = 0; idx < sentence.size();) {
        auto match = dict_.DoMatch(sentence, idx);
        if (match.length == 0) {
            match.length = 1;
            match.token_id = NotToken;
        }

        std::u32string_view slice(sentence.data()+idx, match.length);
        std::string text = ustr::FromU32(slice);
        bool real_gap = (match.token_id != NotToken || previous_id != NotToken);
        if (options.text_output) {
            if (real_gap && token_count > 0) {
                out << ' ';
            }
            out << text;
        } else if (real_gap) {
            out.write(reinterpret_cast<const char*>(&match.token_id),
                      sizeof(TokenID));
        }

        if (real_gap) {
            ++token_count;
        }

        previous_id = match.token_id;
        idx += match.length;
    }
}

} // namespace sime

