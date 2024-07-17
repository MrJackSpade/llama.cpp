cmake -S . -B build/ -D CMAKE_BUILD_TYPE=x64-linux-cuda-static
cmake --build build/ --config x64-linux-cuda-static
sudo find / | grep libggml.so
sudo find / | grep libllama.so
