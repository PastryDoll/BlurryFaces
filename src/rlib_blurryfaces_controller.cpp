inline
void Pause(bool *Stop)
{
    *Stop = !*Stop;
    printf("Pause: %i\n", *Stop);
}

inline
void Controller(video_decoder *VideoDecoder, void *Frame, face *Face, bool *Stop)
{
    // static int currNumberFaces = 1;
    // bool insideAnyFace = false;
    // assert(currNumberFaces < MAX_NUMBER_FACES);
    if (IsKeyPressed(KEY_SPACE))
    {
        *Stop = !*Stop;
        printf("Pause: %i\n", *Stop);
    }

    if (IsKeyPressed(KEY_RIGHT))
    {
        GetForcedFrame(VideoDecoder, Frame, Stop);
    }

    static int CounterArrowPressed = 0;

    if (IsKeyDown(KEY_RIGHT))
    {
        CounterArrowPressed++;
        // printf("Coutner %i\n",CounterArrowPressed);
        if (CounterArrowPressed > 40)
        {
            GetForcedFrame(VideoDecoder, Frame, Stop);
        }
    }

    if (IsKeyReleased(KEY_RIGHT))
    {
        CounterArrowPressed = 0;
    }

    // if (IsKeyPressed(KEY_LEFT))
    // {
    //     if (!videoState->stop) videoState->stop = !videoState->stop;
    //     videoState->Incremental = false;
    //     videoState->Decrement = true;
    // }


    int CurrentGesture = GetGestureDetected();
    float MouseX = GetMouseX();
    float MouseY = GetMouseY();

    // for (int i = 0; i < currNumberFaces; i++)
    // {
    //     Face *face = videoState->face + i;        
    //     if (CheckCollisionPointRec((Vector2){(float)GetMouseX(),(float)GetMouseY()},(Rectangle){face->Box.x,face->Box.y,face->Box.width,face->Box.height}) && (face->done))
    //     {
    //         insideAnyFace = true;
    //         break;
    //     }
    // }

    // //Deselect faces clicked outside
    // if ((currentGesture == GESTURE_TAP) && !insideAnyFace)
    // {
    //     for (int i = 0; i < currNumberFaces; i++)
    //     {
    //         Face *face = videoState->face + i;
    //         face->selected = false;
    //     }

    // }


    // for (int i = 0; i < currNumberFaces; i++)
    // {
    //     Face *face = videoState->face + i;

    //     if (IsKeyPressed(KEY_BACKSPACE))
    //     {
    //         if(face->selected)
    //         {
    //             face->Box = (Rectangle){0,0,0,0};
    //             face->done = 0;
    //             face->selected = 0;
    //             face->inside = 0;

    //         }
    //     }

    //     face->inside = (CheckCollisionPointRec((Vector2){(float)GetMouseX(),(float)GetMouseY()},(Rectangle){face->Box.x,face->Box.y,face->Box.width,face->Box.height}) && (face->done));

    //     //We are creating Faces.. not multi selecting them
    //     if (activeBlur || IsKeyDown(KEY_TAB))
    //     {
    //         // Start drawing box
    //         if ((currentGesture & (GESTURE_TAP|GESTURE_HOLD)) && !face->done && !insideAnyFace)
    //         {
    //             face->Box.x = GetMouseX();
    //             face->Box.y = GetMouseY();
    //             face->Box.width = 0;
    //             face->Box.height = 0;
    //         }
    //         //Drag for size
    //         if ((currentGesture == GESTURE_DRAG) && !face->done && !insideAnyFace)
    //         {   
    //             face->Box.width = GetMouseX() - face->Box.x;
    //             face->Box.height = GetMouseY() - face->Box.y;

    //         }
    //     }
    if ((CurrentGesture & (GESTURE_TAP|GESTURE_HOLD)) && !Face->IsDone)// && !insideAnyFace)
    {
        Face->Box.x = MouseX;
        Face->Box.y = MouseY;
        Face->Box.width = 0;
        Face->Box.height = 0;
    }
    if ((CurrentGesture == GESTURE_DRAG) && !Face->IsDone)// && !insideAnyFace)
    {   
            Face->Box.width = MouseX - Face->Box.x;
            Face->Box.height = MouseY - Face->Box.y;

    }
    if (Face->Box.width > 0 && CurrentGesture == GESTURE_NONE && !Face->IsDone) 
    {
        printf("done\n");
        Face->IsDone = true;
        // currNumberFaces++;
    } 
    //     //Ended Drawing box
    //     if (face->Box.width > 0 && currentGesture == GESTURE_NONE && !face->done) 
    //     {
    //         printf("done\n");
    //         face->done = true;
    //         currNumberFaces++;
    //     } 

    //     //Select a face
    //     if ((currentGesture & (GESTURE_TAP)) && (face->inside))
    //     {
    //         Vector2 pos = GetMousePosition();
    //         initialx = pos.x;
    //         initialy = pos.y;
    //         face->selected = true;
    //     }
        
    //     //Drag Face
    //     if ((currentGesture & (GESTURE_DRAG)) && (face->inside) && (face->selected))
    //     {
    //         Vector2 pos1 = GetMousePosition();
    //         // printf("Initialx : %f, dragx: %f, %lf\n", initialx, pos1.x, GetTime());
    //         face->Box.x += pos1.x - initialx;
    //         face->Box.y += pos1.y - initialy;
    //         initialx = pos1.x;
    //         initialy = pos1.y;
    //     }
    // }

}