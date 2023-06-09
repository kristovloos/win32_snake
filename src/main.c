#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#pragma comment(lib, "kernel32")
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")

#define Min(a, b) ((a) < (b) ? (a) : (b))
#define Max(a, b) ((a) > (b) ? (a) : (b))
#define Clamp(a, v, b) (Min(Max(a, v), b))
#define ArrayCount(a) (sizeof(a) / sizeof(a[0]))

//------------------------------------------------------------------------------
// Utility
//------------------------------------------------------------------------------
inline unsigned int
Pow10U(unsigned int exp)
{
    unsigned int result = 1;
    
    while(exp--)
    {
        result *= 10;
    }
    
    return result;
}

//------------------------------------------------------------------------------
// Drawing
//------------------------------------------------------------------------------
void
ClearScreenBuffer(void *buffer, int bufferWidth, int bufferHeight, unsigned int color)
{
    unsigned int *pixel = (unsigned int *)buffer;
    unsigned int *end   = pixel + bufferWidth * bufferHeight;
    
    while(pixel != end)
    {
        *pixel++ = color;
    }
}

void
FillRectangle(void *buffer, int bufferWidth, int bufferHeight, 
              int x, int y, int w, int h,
              unsigned int color)
{
    unsigned int minX = Clamp(0, x, bufferWidth);
    unsigned int minY = Clamp(0, y, bufferHeight);
    unsigned int maxX = Clamp(0, (x + w), bufferWidth);
    unsigned int maxY = Clamp(0, (y + h), bufferHeight);
    
    unsigned int *row = (unsigned int *)buffer + (minX + minY * bufferWidth);
    
    for(unsigned int by = minY;
        by < maxY;
        y++)
    {
        unsigned int *pixel = row;
        
        for(unsigned int bx = minX;
            bx < maxX;
            x++)
        {
            *pixel++ = color;
        }
        
        row += bufferWidth;
    }
}

#define TestBit(V, B) (((V) & (1 << (B))) != 0)

void DrawSingleNumber(void *buffer, int bufferWidth, int bufferHeight, 
                      unsigned int number, 
                      int xOffset, int yOffset, 
                      int width, int height,
                      unsigned int color)
{
#define DIGIT_PIXELS_X 3
#define DIGIT_PIXELS_Y 5
    
    int digitPixelWidth = width / DIGIT_PIXELS_X;
    int digitPixelHeight = height / DIGIT_PIXELS_Y;
    
    static unsigned short numbers[10] = 
    {
        0x7B6F, 0x4924, 0x73E7, 0x79E7, 0x49ED, 0x79CF, 0x7BC9, 0x4927, 0x7BEF, 0x49EF
    };
    
    for(int dy = 0;
        dy < DIGIT_PIXELS_Y;
        dy++)
    {
        for(int dx = 0;
            dx < DIGIT_PIXELS_X;
            dx++)
        {
            if(TestBit(numbers[number], (dx + dy * 3)))
            {
                int x = xOffset + dx * digitPixelWidth;
                int y = yOffset - dy * digitPixelHeight - digitPixelHeight;
                
                FillRectangle(buffer, bufferWidth, bufferHeight,
                              x, y,
                              digitPixelWidth, digitPixelHeight, color);
            }
        }
    }
}


//------------------------------------------------------------------------------
// Win32
//------------------------------------------------------------------------------
typedef BOOLEAN (* rtl_gen_random_proc) (PVOID RandomBuffer, ULONG RandomBufferLength);

typedef struct
{
    LONG  width;
    LONG  height;
    void *data;
    
    BITMAPINFO bmi;
    
} win32_screenbuffer;

static win32_screenbuffer screenbuffer;

#pragma function(memset)
void *memset(void *dest, int c, size_t count)
{
    __stosb(dest, c, count);
    return dest;
}

void
ResizeScreenBuffer(win32_screenbuffer *buffer, LONG width, LONG height)
{
    if(buffer->data)
    {
        VirtualFree(buffer->data, 0, MEM_RELEASE);
    }
    
    unsigned int bufferBytes = width * height * sizeof(unsigned int);
    
    buffer->data = VirtualAlloc(0, bufferBytes, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    
    if(buffer->data)
    {
        BITMAPINFO bmi = {
            .bmiHeader.biSize = sizeof(BITMAPINFOHEADER),
            .bmiHeader.biWidth = width,
            .bmiHeader.biHeight = height,
            .bmiHeader.biPlanes = 1,
            .bmiHeader.biBitCount = 32,
            .bmiHeader.biCompression = BI_RGB,
        };
        
        buffer->width = width;
        buffer->height = height;
        buffer->bmi = bmi;
    }
}

void
DisplayScreenBuffer(HDC dc, win32_screenbuffer *screenbuffer)
{
    StretchDIBits(dc, 
                  0, 0,
                  screenbuffer->width, screenbuffer->height,
                  0, 0,
                  screenbuffer->width, screenbuffer->height,
                  screenbuffer->data,
                  &screenbuffer->bmi,
                  DIB_RGB_COLORS,
                  SRCCOPY);
}

static WINDOWPLACEMENT windowPlacement = { sizeof(windowPlacement) };

void
ToggleFullscreen(HWND window)
{
    DWORD dwStyle = GetWindowLong(window, GWL_STYLE);
    if (dwStyle & WS_OVERLAPPEDWINDOW) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(window, &windowPlacement) &&
            GetMonitorInfo(MonitorFromWindow(window,
                                             MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(window, GWL_STYLE,
                          dwStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(window, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    } else {
        SetWindowLong(window, GWL_STYLE,
                      dwStyle | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(window, &windowPlacement);
        SetWindowPos(window, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}



//------------------------------------------------------------------------------
// Snake
//------------------------------------------------------------------------------

// Config
//------------------------------------------------------------------------------
#define SCORE_MAX_DIGITS 6
#define MAP_WIDTH 15
#define MAP_HEIGHT 15
#define MAP_SIZE (MAP_WIDTH * MAP_HEIGHT)

// State
//------------------------------------------------------------------------------
typedef enum
{
    MAP_TILE_EMPTY,
    MAP_TILE_SNAKE,
    MAP_TILE_FRUIT, 
} map_tile;

typedef struct
{
    int width;
    int height;
    map_tile tiles[MAP_SIZE];
    
} snake_map;

typedef struct
{
    int snakeX, snakeY;
    int snakeDirX, snakeDirY;
    int snakeRequestedDirX, snakeRequestedDirY;
    
    int snakeHeadIndex, snakeTailIndex;
    int snakeSegments[MAP_SIZE];
    
    unsigned int score;
    
    int fruitPlaced;
    int shouldGameOver;
    int gameOver;
    int screenWrap;
    int lsdMode;
    
    int currentFrame;
    int framesPerTick;
    
    snake_map map;
    
} snake_state;

inline int
MapIndex(snake_map *map, int x, int y)
{
    return x + y * map->width;
}

inline int
GetTileAt(snake_map *map, int x, int y)
{
    int result = -1;
    
    if(x >= 0 && x < map->width && y >= 0 && y < map->height)
    {
        result = map->tiles[MapIndex(map, x, y)];
    }
    
    return result;
}

void
ResetGameState(snake_state *state)
{
    state->map.width = MAP_WIDTH;
    state->map.height = MAP_HEIGHT;
    
    state->snakeX = state->map.width / 2;
    state->snakeY = state->map.height / 2;
    
    state->snakeDirX = 1;
    state->snakeDirY = 0;
    
    state->snakeRequestedDirX = 0;
    state->snakeRequestedDirY = 0;
    
    state->snakeHeadIndex = state->snakeTailIndex = 0;
    
    state->score = 0;
    state->fruitPlaced = 0;
    state->shouldGameOver = 0;
    state->gameOver = 0;
    state->currentFrame = 0;
    state->framesPerTick = 5;
    
    memset(state->map.tiles, 0, state->map.width * state->map.height * sizeof(map_tile));
    state->snakeSegments[state->snakeHeadIndex] = MapIndex(&state->map, state->snakeX, state->snakeY);
    state->map.tiles[state->snakeSegments[state->snakeHeadIndex]] = MAP_TILE_SNAKE;
}

void
UpdateGameplay(snake_state *state)
{
    if(state->snakeRequestedDirX || state->snakeRequestedDirY)
    {
        state->snakeDirX = state->snakeRequestedDirX;
        state->snakeDirY = state->snakeRequestedDirY;
        
        state->snakeRequestedDirX = state->snakeRequestedDirY = 0;
    }
    
    int snakeNewX = state->snakeX + state->snakeDirX;
    int snakeNewY = state->snakeY + state->snakeDirY;
    
    if(state->screenWrap)
    {
        if(snakeNewX < 0)
            snakeNewX = state->map.width + snakeNewX;
        else if(snakeNewX >= state->map.width)
            snakeNewX -= state->map.width;
        
        if(snakeNewY < 0)
            snakeNewY = state->map.height + snakeNewY;
        else if(snakeNewY >= state->map.height)
            snakeNewY -= state->map.height;
    }
    
    int newTile = GetTileAt(&state->map, snakeNewX, snakeNewY);
    
    if(newTile == -1 || newTile == MAP_TILE_SNAKE)
    {
        if(!state->shouldGameOver)
        {
            state->shouldGameOver = 1;
            return;
        }
        else
        {
            state->gameOver = 1;
        }
    }
    else
    {
        if(newTile == MAP_TILE_FRUIT)
        {
            state->score += 10;
            state->fruitPlaced = 0;
        }
        else
        {
            state->map.tiles[state->snakeSegments[state->snakeTailIndex]] = 0;
            state->snakeTailIndex = (state->snakeTailIndex + 1) % ArrayCount(state->snakeSegments);;
        }
        
        state->snakeHeadIndex = (state->snakeHeadIndex + 1) % ArrayCount(state->snakeSegments);
        
        state->snakeX = snakeNewX;
        state->snakeY = snakeNewY;
        
        state->snakeSegments[state->snakeHeadIndex] = MapIndex(&state->map, state->snakeX, state->snakeY);
        
        state->map.tiles[state->snakeSegments[state->snakeHeadIndex]] = MAP_TILE_SNAKE;
    }
}

//------------------------------------------------------------------------------
// Application
//------------------------------------------------------------------------------
LRESULT CALLBACK
WindowProc(HWND window,
           UINT message,
           WPARAM wParam,
           LPARAM lParam)
{
    LRESULT result = 0;
    
    switch(message)
    {
        case WM_SIZE:
        {
            RECT clientRect;
            GetClientRect(window, &clientRect);
            ResizeScreenBuffer(&screenbuffer, clientRect.right, clientRect.bottom);
            ClearScreenBuffer(screenbuffer.data, screenbuffer.width, screenbuffer.height, 0xFF111111);
        } break;
        
        case WM_SETCURSOR:
        {
            if(LOWORD(lParam) == HTCLIENT)
            {
                SetCursor(NULL);
                result = 1;
            }
            
        } break;
        
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(window, &ps);
            DisplayScreenBuffer(dc, &screenbuffer);
            EndPaint(window, &ps);
        } break;
        
        case WM_DESTROY:
        {
            PostQuitMessage(0);
        } break;
        
        default:
        {
            result = DefWindowProc(window, message, wParam, lParam);
        }
    }
    
    
    return result;
}

void
WinMainCRTStartup(void)
{
    
    //------------------------------------------------------------------------------
    // Create Window
    //------------------------------------------------------------------------------
    HINSTANCE instance = GetModuleHandle(0);
    
    WNDCLASS wndClass = {
        .style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
        .lpfnWndProc = WindowProc,
        .hInstance = instance,
        .lpszClassName = "SnakeWindowClass",
    };
    
    RegisterClass(&wndClass);
    
    HWND window = CreateWindow(wndClass.lpszClassName,
                               "Win32 Snake",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, 
                               0, 0, instance, 0);
    
    
    HDC dc = GetDC(window);
    
    //------------------------------------------------------------------------------
    // Load Entropy Function
    //------------------------------------------------------------------------------
    HMODULE advapiDLL = LoadLibrary("Advapi32.dll");
    rtl_gen_random_proc RtlGenRandom = (rtl_gen_random_proc)GetProcAddress(advapiDLL, "SystemFunction036");
    
    //------------------------------------------------------------------------------
    // Init Game State
    //------------------------------------------------------------------------------
    snake_state state = {0};
    ResetGameState(&state);
    
    //------------------------------------------------------------------------------
    // Main Loop
    //------------------------------------------------------------------------------
    int running = 1;
    int timestep = 16;
    
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    
    while(running)
    {
        LARGE_INTEGER begin;
        QueryPerformanceCounter(&begin);
        
        //------------------------------------------------------------------------------
        // Process Input
        //------------------------------------------------------------------------------
        MSG msg;
        
        while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            switch(msg.message)
            {
                case WM_QUIT:
                {
                    running = 0;
                } break;
                
                case WM_KEYDOWN:
                {
                    switch(msg.wParam)
                    {
                        case VK_UP: {
                            state.snakeRequestedDirY = state.snakeDirY ? state.snakeDirY : 1;
                            state.snakeRequestedDirX = 0;
                        } break;
                        
                        case VK_DOWN: {
                            state.snakeRequestedDirY = state.snakeDirY ? state.snakeDirY : -1;
                            state.snakeRequestedDirX = 0;
                        } break;
                        
                        case VK_LEFT: {
                            state.snakeRequestedDirX = state.snakeDirX ? state.snakeDirX : -1;
                            state.snakeRequestedDirY = 0;
                        } break;
                        
                        case VK_RIGHT: {
                            state.snakeRequestedDirX = state.snakeDirX ? state.snakeDirX : 1;
                            state.snakeRequestedDirY = 0;
                        } break;
                        
                        case VK_RETURN:
                        {
                            if(state.gameOver)
                            {
                                ResetGameState(&state);
                            }
                        } break;
                        
                        case VK_F1:
                        {
                            if(state.framesPerTick > 1)
                                state.framesPerTick--;
                            
                            state.currentFrame = 0;
                            
                        } break;
                        
                        case VK_F2:
                        {
                            state.framesPerTick++;
                            state.currentFrame = 0;
                        } break;
                        
                        case VK_F3:
                        {
                            state.screenWrap = !state.screenWrap;
                        } break;
                        
                        case VK_F4:
                        {
                            ToggleFullscreen(window);
                        } break;
                        
                        case VK_F5:
                        {
                            state.lsdMode = !state.lsdMode;
                        } break;
                        
                        case VK_ESCAPE:
                        {
                            running = 0;
                        } break;
                    }
                } break;
                
            }
            
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        //------------------------------------------------------------------------------
        // Update Game
        //------------------------------------------------------------------------------
        while(!state.fruitPlaced)
        {
            unsigned int fruitIndex;
            RtlGenRandom(&fruitIndex, sizeof(unsigned int));
            fruitIndex = fruitIndex % (state.map.width * state.map.height);
            
            if(state.map.tiles[fruitIndex] == 0)
            {
                state.map.tiles[fruitIndex] = MAP_TILE_FRUIT;
                state.fruitPlaced = 1;
            }
        }
        
        if((state.currentFrame++ == state.framesPerTick) && !state.gameOver)
        {
            UpdateGameplay(&state);
            state.currentFrame = 0;
        }
        
        //------------------------------------------------------------------------------
        // Draw Game
        //------------------------------------------------------------------------------
        unsigned int tileSize = screenbuffer.height / state.map.height;
        
        if(state.map.width > state.map.height)
        {
            tileSize = screenbuffer.width / state.map.width;
        }
        
        unsigned int gameWidth = tileSize * state.map.width;
        unsigned int gameHeight = tileSize * state.map.height;
        
        unsigned int gameOffsetX = (screenbuffer.width - gameWidth) / 2;
        unsigned int gameOffsetY = (screenbuffer.height - gameHeight) / 2;
        
        // Background
        unsigned int mapColor = 0xFF222222;
        
        if(state.lsdMode)
        {
            RtlGenRandom(&mapColor, sizeof(unsigned int));
        }
        
        FillRectangle(screenbuffer.data, screenbuffer.width, screenbuffer.height, gameOffsetX, gameOffsetY, gameWidth, gameHeight, mapColor);
        
        // Objects
        for(unsigned int tileIndex = 0;
            tileIndex < ArrayCount(state.map.tiles);
            tileIndex++)
        {
            unsigned int tile = state.map.tiles[tileIndex];
            
            if(tile)
            {
                unsigned int color = (tile == MAP_TILE_SNAKE) ? 0xFF555555 : 0xFFFF3300;
                
                if(state.lsdMode && tile == MAP_TILE_SNAKE)
                {
                    RtlGenRandom(&color, sizeof(unsigned int));
                }
                
                unsigned int x = (tileIndex % state.map.width) * tileSize + gameOffsetX;
                unsigned int y = (tileIndex / state.map.width) * tileSize + gameOffsetY;
                
                FillRectangle(screenbuffer.data, screenbuffer.width, screenbuffer.height, x, y, tileSize, tileSize, color);
            }
        }
        
        // Score
        int digitWidth = (screenbuffer.width / 400) * DIGIT_PIXELS_X;
        int digitHeight = (screenbuffer.width / 400) * DIGIT_PIXELS_Y;
        int digitPadding = digitWidth / 4;
        int digitXOffset = screenbuffer.width - gameOffsetX - ((SCORE_MAX_DIGITS) * (digitWidth + digitPadding)) + digitPadding;
        int digitYOffset = screenbuffer.height - gameOffsetY;
        int scoreMargin = digitHeight;
        
        
        unsigned int power = Pow10U(SCORE_MAX_DIGITS - 1);
        unsigned int score = state.score;
        
        for(int place = 0;
            place < SCORE_MAX_DIGITS;
            place++)
        {
            unsigned int digit = score / power;
            
            DrawSingleNumber(screenbuffer.data, screenbuffer.width, screenbuffer.height, digit, digitXOffset - scoreMargin, digitYOffset - scoreMargin, digitWidth, digitHeight, 0xFFDDDDDD);
            
            score %= power;
            power /= 10;
            
            digitXOffset += digitWidth + digitPadding;
        }
        
        DisplayScreenBuffer(dc, &screenbuffer);
        
        //------------------------------------------------------------------------------
        // Limit Framerate
        //------------------------------------------------------------------------------
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        
        unsigned int msElapsed = ((end.QuadPart - begin.QuadPart) / freq.QuadPart) * 1000;
        
        if(msElapsed < timestep)
        {
            Sleep(timestep - msElapsed);
        }
    }
    
    ExitProcess(0);
}