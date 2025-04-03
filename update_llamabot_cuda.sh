#cp /media/mrjackspade/4TB/llama.cpp/build/ggml/src/libggml.so /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA/libggml.so
#rm /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA/libggml.so.zip
#rm /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA/libggml.so.z01
#cp /media/mrjackspade/4TB/llama.cpp/build/src/libllama.so /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA/libllama.so
#rm /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA/libllama.so.zip
#cd /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA
#zip -9 -s 95m libggml.so.zip libggml.so
#zip -9 libllama.so.zip libllama.so
#git add libggml.so.zip
#git add libggml.so.z01
#git add libllama.so.zip
cp /media/mrjackspade/4TB/llama.cpp/build/ggml/src/ggml-cuda/libggml-cuda.so /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA
cp /media/mrjackspade/4TB/llama.cpp/build/ggml/src/libggml.so /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA
cp /media/mrjackspade/4TB/llama.cpp/build/ggml/src/libggml-cpu.so /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA
cp /media/mrjackspade/4TB/llama.cpp/build/src/libllama.so /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA
cp /media/mrjackspade/4TB/llama.cpp/build/ggml/src/libggml-base.so /media/mrjackspade/4TB/LlamaBot/LlamaNative/Binaries/CUDA

git commit -m "updating libraries"
git push
