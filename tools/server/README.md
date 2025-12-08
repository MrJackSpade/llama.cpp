# Power Law Sampler: Technical Documentation

## Overview

The Power Law Sampler is an advanced token sampling strategy that reshapes probability distributions to favor tokens whose original probabilities fall near a specified target value. Unlike traditional samplers that focus on the highest-probability tokens, this sampler enables controlled exploration of the probability space based on the model's confidence structure.

## Table of Contents

1. [Core Mechanism](#core-mechanism)
2. [Algorithm Walkthrough](#algorithm-walkthrough)
3. [Parameters](#parameters)
4. [Mathematical Foundation](#mathematical-foundation)
5. [Benefits and Use Cases](#benefits-and-use-cases)
6. [Comparison with Other Samplers](#comparison-with-other-samplers)
7. [Practical Examples](#practical-examples)
8. [Limitations](#limitations)
9. [Implementation Notes](#implementation-notes)

---

## Core Mechanism

The Power Law Sampler operates on a fundamentally different principle than traditional samplers:

**Traditional Approach**: "Select from the most probable tokens"  
**Power Law Approach**: "Select tokens whose probability falls near a target value"

This shift allows for probability-aware exploration, where you can intentionally sample from specific regions of the probability distribution rather than always favoring the top candidates.

### Key Innovation

The sampler treats **probability space as navigable terrain**, enabling fine-grained control over the exploration/exploitation tradeoff based on the model's actual confidence structure.

---

## Algorithm Walkthrough

### Step-by-Step Process

```
1. Compute Original Probabilities
   └─> Apply softmax to get initial token probabilities

2. Calculate Dynamic Target
   ├─> If no history: use base target value
   └─> If history exists: compute adaptive target
       └─> target = (base_target × queue_size - sum_of_recent) / 1
       └─> Clamp between min_target and max_target

3. Reshape Distribution (Power Law Transform)
   For each token:
   ├─> Calculate distance from target: |p - target|
   ├─> Normalize distance: distance / distribution_width
   └─> Apply power law: new_logit = peak / (1 + normalized_distance^tail_heaviness)
   
   Special case (width ≈ 0):
   ├─> Find token closest to target
   ├─> Set closest token to peak_logit_value
   └─> Set all others to very low value (-100.0)

4. Apply Softmax Again
   └─> Convert new logits back to probabilities

5. Sample Token
   └─> Use standard distribution sampling from reshaped probabilities

6. Update History
   └─> Record the ORIGINAL probability of selected token
   └─> Maintain rolling window of size queue_size
```

### Visual Representation

```
Original Distribution:
Token A: 0.60 ████████████
Token B: 0.25 █████
Token C: 0.10 ██
Token D: 0.05 █

With target=0.10, width=0.05:
Token A: 0.05 █          (far from target, penalized)
Token B: 0.15 ███        (moderately far)
Token C: 0.70 ██████████ (at target, boosted)
Token D: 0.10 ██         (near target)
```

---

## Parameters

### Primary Parameters

#### `target` (float)
- **Range**: 0.0 to 1.0
- **Default**: Typically 0.1-0.3
- **Description**: The probability value to center the distribution around
- **Effect**: 
  - High (0.7-0.9): Conservative, near-deterministic sampling
  - Mid (0.1-0.3): Balanced creativity and coherence
  - Low (0.01-0.05): Aggressive exploration of unlikely tokens

#### `distribution_width` (float)
- **Range**: 0.0 to 1.0
- **Default**: Typically 0.05-0.2
- **Description**: Tolerance for deviation from target
- **Effect**:
  - Near 0: Only tokens very close to target are considered
  - Larger values: More tokens across probability range are viable
  - Acts as a "window" around the target probability

#### `tail_heaviness` (float)
- **Range**: Typically 1.0 to 10.0
- **Default**: 2.0-4.0
- **Description**: Controls steepness of probability falloff
- **Effect**:
  - Low (1-2): Gentle falloff, considers wider range
  - High (5-10): Steep falloff, strongly focuses on target
  - Exponent in the power law formula

#### `peak_logit_value` (float)
- **Range**: Typically 5.0 to 20.0
- **Default**: 10.0-15.0
- **Description**: Maximum logit value assigned to on-target tokens
- **Effect**: Higher values make the target region more dominant after softmax

### Adaptive Parameters

#### `queue_size` (int)
- **Range**: 1 to 100+
- **Default**: 10-50
- **Description**: Size of rolling history window
- **Effect**: Larger values = smoother adaptive behavior

#### `min_target` / `max_target` (float)
- **Range**: 0.0 to 1.0
- **Description**: Bounds for adaptive target calculation
- **Effect**: Prevents adaptive mechanism from going to extremes

---

## Mathematical Foundation

### Power Law Transform

For each token with original probability `p`:

```
distance = |p - target|
normalized_distance = distance / distribution_width
new_logit = peak_logit_value / (1 + normalized_distance^tail_heaviness)
```

### Properties

1. **Maximum at Target**: Tokens with `p = target` receive `peak_logit_value`
2. **Symmetric**: Equal penalization above and below target
3. **Smooth Falloff**: Continuous function prevents discontinuities
4. **Controllable Decay**: `tail_heaviness` parameter controls rate

### Adaptive Target Calculation

```
sum_excluding_oldest = Σ(history[i]) for i in [0, queue_size-2]
next_value = (base_target × queue_size) - sum_excluding_oldest
computed_target = clamp(next_value, min_target, max_target)
```

This creates a **moving average** effect where:
- If recent samples had high original probabilities → target decreases
- If recent samples had low original probabilities → target increases

---

## Benefits and Use Cases

### 1. Mid-Range Token Promotion

**Problem**: Traditional samplers overwhelmingly favor top-3 tokens  
**Solution**: Explicitly target the "interesting middle" (5-20% probability range)

**Use Case**: Creative writing where you want novel but plausible word choices

```
Prompt: "The warrior entered the dark"
Traditional Top-K: "castle" (60%), "room" (20%), "forest" (10%)
Power Law (target=0.08): "cavern" (8%), "tunnel" (7%), "passage" (9%)
```

### 2. Controlled Randomness

**Problem**: Temperature is a blunt instrument (affects all tokens equally)  
**Solution**: Target specific probability regions based on desired creativity level

**Use Case**: Adjustable creativity without sacrificing coherence

```
Conservative: target=0.6, width=0.1  → Near-deterministic
Balanced:     target=0.2, width=0.15 → Creative but safe
Aggressive:   target=0.03, width=0.05 → Unusual choices
```

### 3. Adaptive Exploration

**Problem**: Static sampling leads to repetitive patterns  
**Solution**: History-based target adjustment prevents getting stuck

**Use Case**: Long-form generation that maintains variety

```
Sequence: "The cat sat on the mat. The cat..."
Without adaptation: Continues with high-prob "sat/lay/slept"
With adaptation: Target drops → explores alternatives
```

### 4. Quality-Aware Exploration

**Problem**: Pure random sampling includes nonsense low-probability tokens  
**Solution**: Respects model's confidence structure while exploring

**Use Case**: Balancing creativity with grammatical correctness

```
Model assigns:
"quickly" → 0.40 (safe, coherent)
"swiftly" → 0.08 (interesting, coherent)
"xyzabc"  → 0.0001 (random noise)

Target=0.08 favors "swiftly" while still excluding "xyzabc"
```

---

## Comparison with Other Samplers

### Feature Matrix

| Sampler | Selection Basis | Exploration Type | Coherence | Adaptivity |
|---------|----------------|------------------|-----------|------------|
| **Greedy** | Highest probability | None | Maximum | None |
| **Top-K** | Top K tokens | Fixed set size | High | None |
| **Top-P (Nucleus)** | Cumulative probability | Dynamic set | High | None |
| **Temperature** | Scaled logits | Uniform scaling | Variable | None |
| **Min-P** | Relative threshold | Probability floor | Medium-High | None |
| **Mirostat** | Target perplexity | Perplexity-based | Medium | Yes (perplexity) |
| **Power Law** | **Target probability** | **Probability-aware** | **Configurable** | **Yes (history)** |

### Detailed Comparisons

#### vs. Top-K
```
Top-K (K=5):
✓ Simple, fast
✓ Predictable set size
✗ Arbitrary cutoff
✗ Ignores probability structure

Power Law:
✓ Respects probability magnitudes
✓ Smooth, continuous selection
✗ More complex
✗ Slower computation
```

#### vs. Top-P (Nucleus)
```
Top-P (P=0.9):
✓ Dynamic set size
✓ Cumulative threshold
✗ Still biased to high-prob tokens
✗ No control over probability range

Power Law:
✓ Can target any probability range
✓ Explicit control over exploration
✗ Less intuitive parameters
✗ Requires tuning
```

#### vs. Temperature
```
Temperature (T=1.5):
✓ Universal scaling
✓ Single parameter
✗ Affects all tokens equally
✗ Can boost nonsense tokens

Power Law:
✓ Selective boosting by probability
✓ Quality-aware exploration
✗ Multiple parameters
✗ More complex behavior
```

#### vs. Mirostat
```
Mirostat:
✓ Adaptive (perplexity-based)
✓ Theoretically grounded
✗ Targets perplexity, not probability
✗ Indirect control

Power Law:
✓ Direct probability targeting
✓ Intuitive probability-space control
✗ Less theoretical foundation
✗ Heuristic adaptation
```

---

## Practical Examples

### Example 1: Creative Writing

**Goal**: Generate vivid descriptions without nonsense

```c
target = 0.12              // Target moderately-likely words
distribution_width = 0.08  // Accept 4-20% probability range
tail_heaviness = 3.0       // Moderately steep falloff
queue_size = 20            // Smooth adaptation over phrases
```

**Result**: Favors descriptive alternatives over common words, but maintains grammatical sense.

### Example 2: Code Generation

**Goal**: Suggest less obvious but valid completions

```c
target = 0.25              // Target upper-middle probability
distribution_width = 0.15  // Wide acceptance range
tail_heaviness = 2.0       // Gentle falloff
queue_size = 10            // Quick adaptation
```

**Result**: Explores alternative implementations while staying syntactically valid.

### Example 3: Dialogue Variation

**Goal**: Prevent repetitive response patterns

```c
target = 0.15              // Mid-range responses
distribution_width = 0.10  // Moderate range
tail_heaviness = 4.0       // Focus on target
queue_size = 30            // Long memory for diversity
min_target = 0.05          // Prevent going too random
max_target = 0.40          // Prevent going too conservative
```

**Result**: Maintains conversational flow while varying expression style.

### Example 4: Deterministic with Slight Variation

**Goal**: Mostly follow top choices, occasional surprises

```c
target = 0.60              // High probability target
distribution_width = 0.20  // Wide tolerance
tail_heaviness = 2.0       // Gentle falloff
queue_size = 5             // Minimal adaptation
```

**Result**: Behaves mostly greedy with occasional detours.

---

## Limitations

### 1. Computational Cost

**Issue**: Requires two softmax passes plus distance calculations  
**Impact**: ~2-3x slower than simple top-k sampling  
**Mitigation**: Cache calculations, optimize distance computation

### 2. Parameter Complexity

**Issue**: Multiple interdependent parameters  
**Impact**: Difficult to tune intuitively  
**Mitigation**: Use preset profiles (creative/balanced/conservative)

### 3. Risk of Incoherence

**Issue**: Low targets can select grammatically unlikely tokens  
**Impact**: Broken sentences, nonsense output  
**Mitigation**: Use min_target bounds, combine with min-p filtering

### 4. History Dependency

**Issue**: Behavior changes unpredictably over sequence length  
**Impact**: Inconsistent generation quality  
**Mitigation**: Use moderate queue sizes, monitor adaptive target

### 5. Distribution Assumptions

**Issue**: Assumes meaningful probability magnitudes  
**Impact**: May not work well with poorly calibrated models  
**Mitigation**: Test with specific model, adjust parameters accordingly

### 6. Edge Cases

**Issue**: Very flat or very peaked distributions  
**Impact**: May select inappropriately  
**Mitigation**: Add probability shape detection, fallback to traditional sampling

---

## Implementation Notes

### Critical Details

#### Original Probability Preservation

```c
// MUST store original probabilities BEFORE reshaping
std::vector<float> original_probs;
for (size_t i = 0; i < cur_p->size; ++i) {
    original_probs.push_back(cur_p->data[i].p);
}
// ... reshape distribution ...
// Later: update history with ORIGINAL probability
float original_p = original_probs[idx];
ctx->history.push_back(original_p);
```

**Why**: History tracks model's actual confidence, not reshaped values

#### Sorting Constraint

```c
// MUST use unsorted softmax
llama_sampler_softmax_impl(cur_p, false);  // false = no sorting
```

**Why**: Sorting would invalidate index correspondence between arrays

#### Zero-Width Handling

```c
if (ctx->distribution_width <= FLT_EPSILON) {
    // Degenerate to greedy selection of closest token
    if ((int) i == closest_token_idx) {
        cur_p->data[i].logit = ctx->peak_logit_value;
    } else {
        cur_p->data[i].logit = -100.0f;
    }
}
```

**Why**: Prevents division by zero, provides deterministic fallback

#### Adaptive Target Clamping

```c
computed_target = std::max(ctx->min_target, 
                          std::min(next_value, ctx->max_target));
```

**Why**: Prevents runaway adaptation from driving target to extremes

### Performance Optimizations

1. **Early Exit**: If distribution_width is zero, skip distance calculations
2. **SIMD**: Vectorize distance and power calculations
3. **Caching**: Store peak_logit_value / (1 + ...) computations
4. **Pruning**: Optionally skip tokens with distance > 3× distribution_width

### Integration Considerations

```c
// Typical sampler chain
samplers = {
    min_p_sampler,      // Remove very unlikely tokens first
    power_law_sampler,  // Reshape remaining distribution
    // No additional temperature/top-k needed
};
```

**Note**: Power Law typically replaces temperature/top-k, not supplements them

---

## Advanced Topics

### Theoretical Interpretation

The power law transform can be viewed as:

1. **Soft Attention**: Attending to probability regions rather than individual tokens
2. **Kernel Smoothing**: Gaussian-like kernel centered on target (with power law tails)
3. **Energy-Based**: Minimizing "distance energy" from target probability

### Relationship to Entropy

```
High target → Low entropy (concentrated selection)
Low target  → High entropy (dispersed selection)
Adaptive    → Target entropy maintenance
```

### Extensions

Possible enhancements:

- **Multi-Modal Targets**: Target multiple probability regions simultaneously
- **Position-Dependent**: Vary target based on sequence position
- **Context-Aware**: Adjust parameters based on prompt/context analysis
- **Hybrid**: Combine with other samplers (e.g., power law + min-p + DRY)

---

## Conclusion

The Power Law Sampler represents a paradigm shift in token sampling: from **selecting the best** to **selecting from a targeted quality range**. This enables:

- Controlled exploration of the probability space
- Quality-aware randomness
- Adaptive diversity maintenance
- Fine-grained creativity control

While more complex than traditional samplers, it offers unique capabilities for applications requiring the sweet spot between deterministic coherence and creative exploration.

### When to Use

**Use Power Law when**:
- You want creativity without nonsense
- You need adaptive diversity over long sequences
- You want to explore "interesting but plausible" alternatives
- Traditional temperature/top-k feel too blunt

**Use Traditional Samplers when**:
- You need maximum speed
- Simple parameters are sufficient
- You want predictable, well-understood behavior
- You're targeting top candidates (greedy/top-k sufficient)

---

## References

- Implementation: `llama_sampler_power_law_apply()`
- Related: Mirostat (adaptive perplexity), Nucleus Sampling (top-p)
- Theoretical: Power law distributions, kernel density estimation

## Version History

- v1.0: Initial implementation with adaptive targeting
- Current: Stable, production-ready