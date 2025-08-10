#include "llama-impl.h"

#include "llama-chat.h"
#include "llama-mmap.h"
#include "llama-vocab.h"
#include "llama-model-loader.h"
#include "llama-model-saver.h"
#include "llama-model.h"

#include "ggml.h"
#include "ggml-backend.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#if defined(_MSC_VER)
#pragma warning(disable: 4244 4267) // possible loss of data
#endif

//
// interface implementation
//

struct llama_sampler_chain_params llama_sampler_chain_default_params() {
    struct llama_sampler_chain_params result = {
        /*.no_perf                     =*/ true,
    };

    return result;
}

size_t llama_max_devices(void) {
    return 16;
}

bool llama_supports_mmap(void) {
    return llama_mmap::SUPPORTED;
}

bool llama_supports_mlock(void) {
    return llama_mlock::SUPPORTED;
}

bool llama_supports_gpu_offload(void) {
    return ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU) != nullptr ||
           llama_supports_rpc();
}

bool llama_supports_rpc(void) {
    return ggml_backend_reg_by_name("RPC") != nullptr;
}

void llama_backend_init(void) {
    ggml_time_init();

    // needed to initialize f16 tables
    {
        struct ggml_init_params params = { 0, NULL, false };
        struct ggml_context * ctx = ggml_init(params);
        ggml_free(ctx);
    }
}

void llama_numa_init(enum ggml_numa_strategy numa) {
    if (numa != GGML_NUMA_STRATEGY_DISABLED) {
        auto * dev = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
        GGML_ASSERT(dev && "CPU backend is not loaded");
        auto * reg = ggml_backend_dev_backend_reg(dev);
        auto * numa_init_fn = (decltype(ggml_numa_init) *) ggml_backend_reg_get_proc_address(reg, "ggml_backend_cpu_numa_init");
        numa_init_fn(numa);
    }
}

void llama_backend_free(void) {
    ggml_quantize_free();
}

int64_t llama_time_us(void) {
    return ggml_time_us();
}

// Returns 0 on success, -1 on error, and -2 on cancellation via llama_progress_callback
static int llama_model_load(const std::string & fname, std::vector<std::string> & splits, llama_model & model, llama_model_params & params) {
    // loading time will be recalculated after the first eval, so
    // we take page faults deferred by mmap() into consideration
    model.t_load_us = 0;
    time_meas tm(model.t_load_us);

    model.t_start_us = tm.t_start_us;

    try {
        llama_model_loader ml(fname, splits, params.use_mmap, params.check_tensors, params.kv_overrides, params.tensor_buft_overrides);

        ml.print_info();

        model.hparams.vocab_only = params.vocab_only;

        try {
            model.load_arch(ml);
        } catch(const std::exception & e) {
            throw std::runtime_error("error loading model architecture: " + std::string(e.what()));
        }
        try {
            model.load_hparams(ml);
        } catch(const std::exception & e) {
            throw std::runtime_error("error loading model hyperparameters: " + std::string(e.what()));
        }
        try {
            model.load_vocab(ml);
        } catch(const std::exception & e) {
            throw std::runtime_error("error loading model vocabulary: " + std::string(e.what()));
        }

        model.load_stats(ml);
        model.print_info();

        if (params.vocab_only) {
            LLAMA_LOG_INFO("%s: vocab only - skipping tensors\n", __func__);
            return 0;
        }

        if (!model.load_tensors(ml)) {
            return -2;
        }
    } catch (const std::exception & err) {
        LLAMA_LOG_ERROR("%s: error loading model: %s\n", __func__, err.what());
        return -1;
    }

    return 0;
}

static struct llama_model * llama_model_load_from_file_impl(
        const std::string & path_model,
        std::vector<std::string> & splits,
        struct llama_model_params params) {
    ggml_time_init();

    if (!params.vocab_only && ggml_backend_reg_count() == 0) {
        LLAMA_LOG_ERROR("%s: no backends are loaded. hint: use ggml_backend_load() or ggml_backend_load_all() to load a backend before calling this function\n", __func__);
        return nullptr;
    }

    unsigned cur_percentage = 0;
    if (params.progress_callback == NULL) {
        params.progress_callback_user_data = &cur_percentage;
        params.progress_callback = [](float progress, void * ctx) {
            unsigned * cur_percentage_p = (unsigned *) ctx;
            unsigned percentage = (unsigned) (100 * progress);
            while (percentage > *cur_percentage_p) {
                *cur_percentage_p = percentage;
                LLAMA_LOG_CONT(".");
                if (percentage >= 100) {
                    LLAMA_LOG_CONT("\n");
                }
            }
            return true;
        };
    }

    llama_model * model = new llama_model(params);

    // create list of devices to use with this model
    if (params.devices) {
        for (ggml_backend_dev_t * dev = params.devices; *dev; ++dev) {
            model->devices.push_back(*dev);
        }
    } else {
        std::vector<ggml_backend_dev_t> rpc_servers;
        // use all available devices
        for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
            ggml_backend_dev_t dev = ggml_backend_dev_get(i);
            switch (ggml_backend_dev_type(dev)) {
                case GGML_BACKEND_DEVICE_TYPE_CPU:
                case GGML_BACKEND_DEVICE_TYPE_ACCEL:
                    // skip CPU backends since they are handled separately
                    break;

                case GGML_BACKEND_DEVICE_TYPE_GPU:
                    ggml_backend_reg_t reg = ggml_backend_dev_backend_reg(dev);
                    if (ggml_backend_reg_name(reg) == std::string("RPC")) {
                        rpc_servers.push_back(dev);
                    } else {
                        model->devices.push_back(dev);
                    }
                    break;
            }
        }
        // add RPC servers at the front of the list
        if (!rpc_servers.empty()) {
            model->devices.insert(model->devices.begin(), rpc_servers.begin(), rpc_servers.end());
        }
    }

    // if using single GPU mode, remove all except the main GPU
    if (params.split_mode == LLAMA_SPLIT_MODE_NONE) {
        if (params.main_gpu < 0) {
            model->devices.clear();
        } else {
            if (params.main_gpu >= (int)model->devices.size()) {
                LLAMA_LOG_ERROR("%s: invalid value for main_gpu: %d (available devices: %zu)\n", __func__, params.main_gpu, model->devices.size());
                llama_model_free(model);
                return nullptr;
            }
            ggml_backend_dev_t main_gpu = model->devices[params.main_gpu];
            model->devices.clear();
            model->devices.push_back(main_gpu);
        }
    }

    for (auto * dev : model->devices) {
        size_t free, total; // NOLINT
        ggml_backend_dev_memory(dev, &free, &total);
        LLAMA_LOG_INFO("%s: using device %s (%s) - %zu MiB free\n", __func__, ggml_backend_dev_name(dev), ggml_backend_dev_description(dev), free/1024/1024);
    }

    const int status = llama_model_load(path_model, splits, *model, params);
    GGML_ASSERT(status <= 0);
    if (status < 0) {
        if (status == -1) {
            LLAMA_LOG_ERROR("%s: failed to load model\n", __func__);
        } else if (status == -2) {
            LLAMA_LOG_INFO("%s: cancelled model load\n", __func__);
        }

        llama_model_free(model);
        return nullptr;
    }

    return model;
}

// deprecated
struct llama_model * llama_load_model_from_file(
        const char * path_model,
        struct llama_model_params params) {
    return llama_model_load_from_file(path_model, params);
}

struct llama_model * llama_model_load_from_file(
        const char * path_model,
        struct llama_model_params params) {
    std::vector<std::string> splits = {};
    return llama_model_load_from_file_impl(path_model, splits, params);
}

struct llama_model * llama_model_load_from_splits(
        const char ** paths,
        size_t n_paths,
        struct llama_model_params params) {
    std::vector<std::string> splits;
    if (n_paths == 0) {
        LLAMA_LOG_ERROR("%s: list of splits is empty\n", __func__);
        return nullptr;
    }
    for (size_t i = 0; i < n_paths; ++i) {
        splits.push_back(paths[i]);
    }
    return llama_model_load_from_file_impl(splits.front(), splits, params);
}

void llama_model_save_to_file(const struct llama_model * model, const char * path_model) {
    llama_model_saver ms(*model);
    ms.add_kv_from_model();
    ms.add_tensors_from_model();
    ms.save(path_model);
}

//
// chat templates
//

int32_t llama_chat_apply_template(
                              const char * tmpl,
         const struct llama_chat_message * chat,
                                  size_t   n_msg,
                                    bool   add_ass,
                                    char * buf,
                                 int32_t   length) {
    const std::string curr_tmpl(tmpl == nullptr ? "chatml" : tmpl);

    // format the chat to string
    std::vector<const llama_chat_message *> chat_vec;
    chat_vec.resize(n_msg);
    for (size_t i = 0; i < n_msg; i++) {
        chat_vec[i] = &chat[i];
    }

    std::string formatted_chat;
    llm_chat_template detected_tmpl = llm_chat_detect_template(curr_tmpl);
    if (detected_tmpl == LLM_CHAT_TEMPLATE_UNKNOWN) {
        return -1;
    }
    int32_t res = llm_chat_apply_template(detected_tmpl, chat_vec, formatted_chat, add_ass);
    if (res < 0) {
        return res;
    }
    if (buf && length > 0) {
        strncpy(buf, formatted_chat.c_str(), length);
    }
    return res;
}

//
// model split
//

int llama_split_path(char * split_path, size_t maxlen, const char * path_prefix, int split_no, int split_count) {
    static const char * const SPLIT_PATH_FORMAT = "%s-%05d-of-%05d.gguf";
    if (snprintf(split_path, maxlen, SPLIT_PATH_FORMAT, path_prefix, split_no + 1, split_count)) {
        return strlen(split_path);
    }
    return 0;
}

int llama_split_prefix(char * split_prefix, size_t maxlen, const char * split_path, int split_no, int split_count) {
    std::string str_split_path(split_path);
    char postfix[32];
    snprintf(postfix, 32, "-%05d-of-%05d.gguf", split_no + 1, split_count);
    std::string str_postfix(postfix);

    // check if split_prefix ends with postfix
    int size_prefix = str_split_path.size() - str_postfix.size();
    if (size_prefix > 0 && str_split_path.find(str_postfix, size_prefix) != std::string::npos) {
        snprintf(split_prefix, std::min((size_t) size_prefix + 1, maxlen), "%s", split_path);
        return size_prefix;
    }

    return 0;
}

const char * llama_print_system_info(void) {
    static std::string s;
    s.clear(); // Clear the string, since it's static, otherwise it will accumulate data from previous calls.

    for (size_t i = 0; i < ggml_backend_reg_count(); i++) {
        auto * reg = ggml_backend_reg_get(i);
        auto * get_features_fn = (ggml_backend_get_features_t) ggml_backend_reg_get_proc_address(reg, "ggml_backend_get_features");
        if (get_features_fn) {
            ggml_backend_feature * features = get_features_fn(reg);
            s += ggml_backend_reg_name(reg);
            s += " : ";
            for (; features->name; features++) {
                s += features->name;
                s += " = ";
                s += features->value;
                s += " | ";
            }
        }
    }

    return s.c_str();
}

void * llama_create_tensor_buffer_type_overrides(const char * overrides_str) {
    fprintf(stderr, "DEBUG: Creating tensor buffer type overrides for: '%s'\n", overrides_str);

    // First, count how many overrides we have
    std::string value(overrides_str);
    size_t      override_count = 1;  // Start with 1 for the last part
    for (size_t i = 0; i < value.length(); i++) {
        if (value[i] == ',') {
            override_count++;
        }
    }

    fprintf(stderr, "DEBUG: Estimated %zu overrides\n", override_count);

    // Need one extra for the NULL terminator
    auto * overrides =
        (llama_model_tensor_buft_override *) malloc((override_count + 1) * sizeof(llama_model_tensor_buft_override));

    if (!overrides) {
        fprintf(stderr, "DEBUG: Failed to allocate memory for overrides\n");
        return nullptr;
    }

    fprintf(stderr, "DEBUG: Allocated override array at %p\n", (void *) overrides);

    // Initialize the structure to zeros
    memset(overrides, 0, (override_count + 1) * sizeof(llama_model_tensor_buft_override));

    // Match the original code's buffer type discovery
    std::map<std::string, ggml_backend_buffer_type_t> buft_list;
    fprintf(stderr, "DEBUG: Building buffer type list\n");

    if (buft_list.empty()) {
        fprintf(stderr, "DEBUG: Buffer type list is empty, populating\n");
        // enumerate all the devices and add their buffer types to the list
        for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
            fprintf(stderr, "DEBUG: Getting device %zu\n", i);
            auto * dev = ggml_backend_dev_get(i);
            if (!dev) {
                fprintf(stderr, "DEBUG: Device %zu is NULL\n", i);
                continue;
            }

            fprintf(stderr, "DEBUG: Getting buffer type for device %zu\n", i);
            auto * buft = ggml_backend_dev_buffer_type(dev);
            if (buft) {
                const char * name = ggml_backend_buft_name(buft);
                fprintf(stderr, "DEBUG: Adding buffer type '%s' to list\n", name);
                buft_list[name] = buft;
            } else {
                fprintf(stderr, "DEBUG: Buffer type for device %zu is NULL\n", i);
            }
        }
    }

    fprintf(stderr, "DEBUG: Available buffer types:\n");
    for (const auto & it : buft_list) {
        fprintf(stderr, "DEBUG:   %s at %p\n", it.first.c_str(), (void *) it.second);
    }

    // Use the same string-split implementation
    std::vector<std::string> parts;
    size_t                   begin_pos     = 0;
    size_t                   separator_pos = value.find(',');
    while (separator_pos != std::string::npos) {
        std::string part = value.substr(begin_pos, separator_pos - begin_pos);
        fprintf(stderr, "DEBUG: Found part: '%s'\n", part.c_str());
        parts.emplace_back(part);
        begin_pos     = separator_pos + 1;
        separator_pos = value.find(',', begin_pos);
    }
    std::string last_part = value.substr(begin_pos);
    fprintf(stderr, "DEBUG: Found last part: '%s'\n", last_part.c_str());
    parts.emplace_back(last_part);

    fprintf(stderr, "DEBUG: Split into %zu parts\n", parts.size());

    // Process exactly as the original code does
    size_t valid_count = 0;
    for (size_t i = 0; i < parts.size(); i++) {
        const auto & override = parts[i];
        fprintf(stderr, "DEBUG: Processing part %zu: '%s'\n", i, override.c_str());

        std::string::size_type pos = override.find('=');
        if (pos == std::string::npos) {
            fprintf(stderr, "DEBUG: No '=' found in part %zu, skipping\n", i);
            continue;
        }

        std::string tensor_name = override.substr(0, pos);
        std::string buffer_type = override.substr(pos + 1);
        fprintf(stderr, "DEBUG: Tensor name: '%s', Buffer type: '%s'\n", tensor_name.c_str(), buffer_type.c_str());

        if (buft_list.find(buffer_type) == buft_list.end()) {
            fprintf(stderr, "DEBUG: Buffer type '%s' not found in list\n", buffer_type.c_str());
            continue;
        }

        fprintf(stderr, "DEBUG: Found buffer type '%s' in list\n", buffer_type.c_str());

        // Follow the exact same structure as the original
        fprintf(stderr, "DEBUG: Creating entry at index %zu\n", valid_count);

        fprintf(stderr, "DEBUG: Duplicating tensor name: '%s'\n", tensor_name.c_str());
        overrides[valid_count].pattern = strdup(tensor_name.c_str());
        if (!overrides[valid_count].pattern) {
            fprintf(stderr, "DEBUG: strdup failed for pattern\n");
            continue;
        }
        fprintf(stderr,
                "DEBUG: Tensor name duplicated to %p: '%s'\n",
                (void *) overrides[valid_count].pattern,
                overrides[valid_count].pattern);

        fprintf(stderr, "DEBUG: Setting buffer type to %p\n", (void *) buft_list.at(buffer_type));
        overrides[valid_count].buft = buft_list.at(buffer_type);

        valid_count++;
    }

    // Make sure the array is NULL-terminated
    fprintf(stderr, "DEBUG: Setting NULL terminator at index %zu\n", valid_count);
    overrides[valid_count].pattern = nullptr;
    overrides[valid_count].buft    = nullptr;

    fprintf(stderr, "DEBUG: Final override array at %p has %zu entries\n", (void *) overrides, valid_count);
    for (size_t i = 0; i < valid_count; i++) {
        fprintf(
            stderr, "DEBUG: Entry %zu: pattern='%s', buft=%p\n", i, overrides[i].pattern, (void *) overrides[i].buft);
    }

    return overrides;
}

void llama_free_tensor_buffer_type_overrides(void * overrides_ptr) {
    fprintf(stderr, "DEBUG: Freeing tensor buffer type overrides at %p\n", overrides_ptr);

    if (overrides_ptr == nullptr) {
        fprintf(stderr, "DEBUG: Overrides pointer is NULL, nothing to free\n");
        return;
    }

    llama_model_tensor_buft_override * overrides = (llama_model_tensor_buft_override *) overrides_ptr;

    // Free each entry until we hit the NULL terminator
    for (size_t i = 0; overrides[i].pattern != nullptr; i++) {
        fprintf(stderr, "DEBUG: Freeing pattern at %p: '%s'\n", (void *) overrides[i].pattern, overrides[i].pattern);
        free((void *) overrides[i].pattern);
    }

    fprintf(stderr, "DEBUG: Freeing override array at %p\n", (void *) overrides);
    free(overrides);
    fprintf(stderr, "DEBUG: Override array freed\n");
}
