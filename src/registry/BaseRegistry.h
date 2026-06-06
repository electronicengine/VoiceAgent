#pragma once

#include "registry/SqliteDatabase.h"
#include "common/llama_operator.h"
#include <vector>
#include <string>
#include <memory>
#include <cstring>

namespace voice_agent {

class BaseRegistry {
public:
    BaseRegistry(SqliteDatabase& db, LlamaOperator& llama) 
        : db_(db), llama_(llama) {}
    virtual ~BaseRegistry() = default;

    virtual bool Initialize() = 0;

protected:
    // Helper to serialize vector of floats to binary for SQLite BLOB
    std::vector<char> SerializeEmbedding(const std::vector<float>& embedding) {
        std::vector<char> blob(embedding.size() * sizeof(float));
        std::memcpy(blob.data(), embedding.data(), blob.size());
        return blob;
    }

    // Helper to deserialize binary from SQLite BLOB to vector of floats
    std::vector<float> DeserializeEmbedding(const void* blob, int size) {
        int count = size / sizeof(float);
        std::vector<float> embedding(count);
        std::memcpy(embedding.data(), blob, size);
        return embedding;
    }

    SqliteDatabase& db_;
    LlamaOperator& llama_;
};

} // namespace voice_agent
