# #!/bin/sh

set -xe

clang++ -g -std=c++11 -O3 -Wall -framework CoreVideo -framework IOKit -framework Cocoa -framework GLUT -framework OpenGL ../raylibtest/lib/libraylib.a main.cpp -o BlurryFaces -I../raylibtest/lib -I/opt/homebrew/include -lavformat -lavcodec -lavutil -lswscale -L/opt/homebrew/lib