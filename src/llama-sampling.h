#pragma once

// TODO: rename llama-sampling.h/.cpp to llama-sampler.h/.cpp ?

#include "llama.h"

#include <vector>

struct llama_vocab;
struct llama_grammar;

// sampler chain

struct llama_sampler_chain {
    llama_sampler_chain_params params;

    std::vector<struct llama_sampler *> samplers;

    // timing

    mutable int64_t t_sample_us;

    mutable int32_t n_sample;
};

struct llama_sampler * llama_sampler_init_dry_testing(
                         int32_t   context_size,
                           float   dry_multiplier,
                           float   dry_base,
                         int32_t   dry_allowed_length,
                         int32_t   dry_penalty_last_n,
  const std::vector<std::vector<llama_token>>& seq_breakers);

// Add function declaration
LLAMA_API struct llama_sampler * llama_sampler_init_power_law(uint32_t seed, float target, float min_target,
                                                              float max_target, float width, float tail_heaviness,
                                                              float peak_logit_value, size_t queue_size, float min_p);
