#include "LlamaOperatorTest.h"
#include "common/llama_operator.h"
#include <gtest/gtest.h>

namespace voice_agent {

TEST_F(LlamaOperatorTest, TestEmbeddingCalculation) {
    LlamaOperator llama;

    bool loaded = llama.loadEmbedModel("/usr/local/ai.models/llamaModel/mxbaiV1.gguf", LLAMA_POOLING_TYPE_CLS);
    
    EXPECT_TRUE(loaded);

    if (loaded) {
        auto emb = llama.calculateEmbeddings("hello");
        EXPECT_FALSE(emb.empty());
    }
}

} // namespace voice_agent
