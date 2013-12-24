# From within a Linux environment

./waf configure build
gcc -I./build/include/ -I./include examples/simple.c -l csp -L ./build -lpthread -lrt

