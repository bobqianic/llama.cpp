#include "llama.h"
#include "common.h"
#include "unicode.h"
#include "console.h"

#include <cassert>
#include <codecvt>
#include <cstdio>
#include <cstring>
#include <locale>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <vocab-file>\n", argv[0]);
        return 1;
    }

    const std::string fname = argv[1];

    fprintf(stderr, "%s : reading vocab from: '%s'\n", __func__, fname.c_str());

    llama_model * model;
    llama_context * ctx;

    llama_backend_init();

    // load the vocab
    {
        auto mparams = llama_model_default_params();

        mparams.vocab_only = true;

        model = llama_load_model_from_file(fname.c_str(), mparams);

        if (model == NULL) {
            fprintf(stderr, "%s: error: failed to load vocab '%s'\n", __func__, fname.c_str());
            return 1;
        }

        auto cparams = llama_context_default_params();

        ctx = llama_new_context_with_model(model, cparams);

        if (ctx == NULL) {
            fprintf(stderr, "%s: error: failed to load vocab '%s'\n", __func__, fname.c_str());
            llama_free_model(model);
            return 1;
        }
    }

    GGML_ASSERT(llama_vocab_type(model) == LLAMA_VOCAB_TYPE_SPM);

#ifdef _WIN32
    // We need this for unicode console support
    console::init(false, false);
    atexit([]() { console::cleanup(); });
#endif

    const int n_vocab = llama_n_vocab(model);

    for (int i = 0; i < n_vocab; ++i) {
        std::string str = llama_detokenize_spm(ctx, std::vector<int>(1, i));
        std::vector<llama_token> tokens = llama_tokenize(ctx, str, false);
        std::string check = llama_detokenize_spm(ctx, tokens);
        if (check != str) {
            fprintf(stderr, "%s : error: token %d detokenizes to '%s'(%zu) but tokenization of this detokenizes to '%s'(%zu)\n",
                __func__, i, str.c_str(), str.length(), check.c_str(), check.length());
            return 2;
        }
    }

    // unicode
    {
        static UNICODE unicode_engine;
        const int nthread = std::thread::hardware_concurrency();

        std::vector<std::thread> threads(nthread);

        for (int i = 0; i < nthread; ++i) {
            threads[i] = std::thread([i, nthread, ctx]() {
                for (uint32_t cp = i; cp < 0x0010ffff; cp += nthread) {
                    if (cp >= 0xd800 && cp <= 0xdfff) {
                        continue;
                    }

                    std::string str = unicode_engine.to_string(cp);
                    std::vector<llama_token> tokens = llama_tokenize(ctx, str, false);
                    std::string check = llama_detokenize_spm(ctx, tokens);
                    if (cp != 9601 && str != check) {
                        fprintf(stderr, "error: codepoint %x detokenizes to '%s'(%zu) instead of '%s'(%zu)\n",
                                cp, check.c_str(), check.length(), str.c_str(), str.length());
                        std::exit(3);
                    }
                }
            });
        }

        for (auto & t : threads) {
            t.join();
        }
    }

    llama_free_model(model);
    llama_free(ctx);

    llama_backend_free();

    return 0;
}
