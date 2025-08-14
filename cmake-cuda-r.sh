cmake -S . -B build/ \
    -D CMAKE_BUILD_TYPE=Release \
    -DGGML_CUDA=ON \
    -DGGML_AVX=ON \
    -DGGML_AVX2=ON \
    -DGGML_AVX512=ON \
    -DGGML_AVX512_VBMI=ON \
    -DGGML_AVX512_VNNI=ON \
    -DGGML_FMA=ON \
    -DGGML_F16C=ON \
    -DLLAMA_CURL=OFF

cmake --build build/ -- -j 8
sudo find / | grep libggml.so
sudo find / | grep libllama.so
