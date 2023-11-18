#include "ffmpeg_decoder.cpp"
#include "../externals/raylib/lib/raylib.h"
#include "../externals/raylib/lib/rlgl.h"

struct frames_memory
{   
    u8 *TempFramePtr;
    Image FrameImage;
    Texture DisplayTexture;
};