# !/bin/sh

# set -xe

FLAGS="-g -std=c++11 -Wall -Wno-deprecated-declarations -Wno-unused-but-set-variable"

FRAMEWORKS="-framework CoreVideo -framework IOKit -framework Cocoa 
-framework GLUT -framework OpenGL"

LIBS="-lopencv_core -lopencv_imgproc -lavformat -lavcodec -lavutil -lswscale ../raylibtest/lib/libraylib.a"

LIBS_PATHS="-L/opt/homebrew/lib"

INCLUDE_PATHS="-I../raylibtest/lib -I/opt/homebrew/include -I/opt/homebrew/include/opencv4"

clang++ $FLAGS $FRAMEWORKS main.cpp -o BlurryFaces $INCLUDE_PATHS $LIBS $LIBS_PATHS