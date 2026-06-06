#pragma once

#include "common/llama_operator.h"
#include <vector>
#include <cmath>
#include <string>
#include <filesystem>

namespace voice_agent {

class MockLlamaOperator : public LlamaOperator {
public:
    std::vector<float> calculateEmbeddings(const std::string& text) override {
        std::vector<float> emb(10, 0.0f);
        if (!text.empty()) {
            emb[0] = static_cast<float>(text.length()) / 100.0f;
        }
        return emb;
    }

    float getSimilarity(const std::vector<float>& Emb1, const std::vector<float>& Emb2) override {
        if (Emb1.empty() || Emb2.empty() || Emb1.size() != Emb2.size()) return 0.0f;
        if (std::abs(Emb1[0] - Emb2[0]) < 0.0001f) return 1.0f;
        return 0.5f;
    }
};

inline void CleanupTestDb(const std::string& path) {
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}

} // namespace voice_agent
