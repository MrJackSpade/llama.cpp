cp build/ggml/src/libggml.so ../LlamaBot/LlamaNative/Binaries/CPU/libggml.so
rm ../LlamaBot/LlamaNative/Binaries/CPU/libggml.so.zip
rm ../LlamaBot/LlamaNative/Binaries/CPU/libggml.so.z01
cp build/src/libllama.so ../LlamaBot/LlamaNative/Binaries/CPU/libllama.so
rm ../LlamaBot/LlamaNative/Binaries/CPU/libllama.so.zip
cd ../LlamaBot/LlamaNative/Binaries/CPU
zip -9 -s 95m libggml.so.zip libggml.so
zip -9 libllama.so.zip libllama.so
git add libggml.so.zip
git add libggml.so.z01
git add libllama.so.zip
git commit -m "updating libraries"
git push
