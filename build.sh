# #!/bin/sh

set -xe

clang++ -std=c++11 -O3 -framework CoreVideo -framework IOKit -framework Cocoa -framework GLUT -framework OpenGL ../raylibtest/lib/libraylib.a main.cpp -o BlurryFaces -I../raylibtest/lib -I/opt/homebrew/include -lavformat -lavcodec -lavutil -lswscale -L/opt/homebrew/lib