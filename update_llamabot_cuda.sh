cp /media/mrjackspade/4TB/llama.cpp/build/bin/libggml-cuda.so /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA
cp /media/mrjackspade/4TB/llama.cpp/build/bin/libggml.so /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA
cp /media/mrjackspade/4TB/llama.cpp/build/bin/libggml-cpu.so /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA
cp /media/mrjackspade/4TB/llama.cpp/build/bin/libllama.so /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA
cp /media/mrjackspade/4TB/llama.cpp/build/bin/libggml-base.so /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA

git commit -m "updating libraries"
git push
