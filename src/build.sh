# !/bin/sh

# set -xe

FLAGS="-g -std=c++11 -Wall -Wno-deprecated-declarations -Wno-unused-but-set-variable"

FRAMEWORKS="-framework CoreVideo -framework IOKit -framework Cocoa 
-framework GLUT -framework OpenGL"

LIBS="-lopencv_core -lopencv_imgproc -lavformat -lavcodec -lavutil -lswscale ../externals/raylib/lib/libraylib.a"

LIBS_PATHS="-L/opt/homebrew/lib"

INCLUDE_PATHS="-I../externals/raylib/lib -I/opt/homebrew/include -I/opt/homebrew/include/opencv4"

clang++ $FLAGS $FRAMEWORKS rlib_blurryface_main.cpp -o BlurryFaces $INCLUDE_PATHS $LIBS $LIBS_PATHS