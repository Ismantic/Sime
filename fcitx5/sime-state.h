#ifndef FCITX5_SIME_STATE_H
#define FCITX5_SIME_STATE_H

#include <fcitx/inputcontextproperty.h>
#include <string>
#include <vector>

namespace fcitx {

class SimeState : public InputContextProperty {
public:
    std::string preedit;
    std::vector<std::string> candidates; // UTF-8 text, global index

    void reset() {
        preedit.clear();
        candidates.clear();
    }

    bool empty() const { return preedit.empty(); }
};

} // namespace fcitx

#endif // FCITX5_SIME_STATE_H
