#include "rlib_renderer.h"
#include <stdlib.h>

frames_memory *InitializeFramesMemory()
{
    frames_memory *Memory = (frames_memory *)malloc(sizeof(frames_memory));
    
    Texture texture = {0};
    texture.width   = TARGET_WIDTH;
    texture.height  = TARGET_HEIGHT;
    texture.format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
    texture.mipmaps = 1;
    texture.id = rlLoadTexture(NULL, texture.width, texture.height, texture.format, texture.mipmaps);

    Image image = {0};
    image.width = TARGET_WIDTH;
    image.height = TARGET_HEIGHT;
    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
    image.mipmaps = 1;
    image.data = (u8*)malloc(TARGET_WIDTH*TARGET_HEIGHT*3);

    Memory->DisplayTexture = texture;
    Memory->FrameImage = image;

    return Memory;

}