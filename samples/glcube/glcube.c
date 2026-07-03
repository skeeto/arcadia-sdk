/*
 * glcube — a hardware-accelerated OpenGL toy for Arcadia.
 *
 * Arcadia composites 8-bit DIB surfaces with GDI, so you can't get accelerated
 * GL *into* the DC it hands you. Instead this toy creates its own child window
 * over the Arcadia canvas, gives that child an OpenGL pixel format, and renders
 * to it directly with SwapBuffers — fully GPU-accelerated and independent of the
 * host's palette/compositing. It resizes with the Arcadia window and rotates
 * under the mouse.
 *
 * It uses OpenGL 1.1 fixed-function (immediate mode), so there is no shader or
 * extension-loading boilerplate — just wgl + glBegin/glEnd. Link opengl32.
 *
 * WHY A RENDER THREAD.  On modern Windows (10/11), simply having a live GL
 * context in the process makes the host's main-thread tick hitch for
 * ~150-200 ms every couple of seconds — the vendor GL driver / DWM engaging
 * (see docs/ABI.md, "GL context stalls the host main thread"). If we rendered
 * from tick() we would inherit that hitch. Rendering on our own thread decouples
 * the animation from the host's tick, so the cube stays smooth through the
 * host's hitches. We also raise the timer resolution (timeBeginPeriod) so a
 * Sleep(16) loop actually lands near 60 fps instead of the default ~64 Hz.
 * (This does not cure the host-side hitch — nothing a toy does can — it only
 * keeps *our* rendering smooth.)
 *
 * INPUT.  Arcadia doesn't route input to toys; toys poll global state. ar_mouse()
 * is GetCursorPos + ScreenToClient(canvas) + GetAsyncKeyState for the buttons,
 * and ar_key_down() is GetAsyncKeyState — none of which go through window
 * messages. So our topmost child GL window does NOT intercept input: dragging
 * with the left button rotates the cube even though the GL window is the window
 * physically under the cursor. Polling is also thread-agnostic, so we sample it
 * straight from the render thread.
 */
#include <arcadia/toy.h>
#include <GL/gl.h>
#include <mmsystem.h>            /* timeBeginPeriod/timeEndPeriod (link winmm) */

static const char GL_CLASS[] = "ArcadiaGLChildWindow";

static HWND           s_glwnd;   /* our child window over the canvas */
static HANDLE         s_thread;  /* the render thread                */
static volatile LONG  s_run;     /* render loop keep-going flag      */

/* This DLL's own module handle, for RegisterClass/CreateWindow. */
static HINSTANCE self_instance(void)
{
    HINSTANCE hi = 0;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(const void *)&self_instance, &hi);
    return hi;
}

static LRESULT CALLBACK gl_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_ERASEBKGND) return 1;   /* GL owns the pixels; skip GDI erase */
    return DefWindowProcA(h, m, w, l);
}

/* Set viewport + a simple perspective projection for a w x h surface. */
static void set_viewport(int w, int h)
{
    double top, aspect;
    if (h < 1) h = 1;
    aspect = (double)w / (double)h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    top = 0.5;                                   /* ~53 deg vertical FOV at near=1 */
    glFrustum(-top * aspect, top * aspect, -top, top, 1.0, 100.0);
    glMatrixMode(GL_MODELVIEW);
}

static void draw_cube(void)
{
    static const GLfloat v[8][3] = {
        {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
        {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1},
    };
    static const GLubyte face[6][4] = {
        {0,1,2,3}, {4,5,6,7}, {0,1,5,4}, {2,3,7,6}, {1,2,6,5}, {0,3,7,4},
    };
    static const GLfloat color[6][3] = {
        {1.0f,0.3f,0.3f}, {0.3f,1.0f,0.4f}, {0.3f,0.5f,1.0f},
        {1.0f,0.9f,0.3f}, {1.0f,0.4f,1.0f}, {0.4f,1.0f,1.0f},
    };
    int i, j;
    glBegin(GL_QUADS);
    for (i = 0; i < 6; i++) {
        glColor3fv(color[i]);
        for (j = 0; j < 4; j++) glVertex3fv(v[face[i][j]]);
    }
    glEnd();
}

/* Owns the GL context for its whole lifetime (a context is current per-thread),
 * and drives animation independently of the host tick. `param` is the canvas. */
static DWORD WINAPI render_thread(LPVOID param)
{
    HWND canvas = (HWND)param;
    HDC  dc = GetDC(s_glwnd);
    PIXELFORMATDESCRIPTOR pfd;
    int  pf;
    HGLRC rc;
    int   w = 0, h = 0;
    float yaw = 0.0f, pitch = 20.0f;   /* current orientation (degrees) */
    int   dragging = 0, lastx = 0, lasty = 0;

    ZeroMemory(&pfd, sizeof pfd);
    pfd.nSize      = sizeof pfd;
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd)) { ReleaseDC(s_glwnd, dc); return 1; }
    rc = wglCreateContext(dc);
    if (!rc) { ReleaseDC(s_glwnd, dc); return 1; }
    wglMakeCurrent(dc, rc);
    glEnable(GL_DEPTH_TEST);

    timeBeginPeriod(1);          /* so Sleep(16) actually approximates 60 fps */

    while (s_run) {
        RECT r;
        int mx, my, buttons;

        /* Follow the Arcadia canvas size. SWP_ASYNCWINDOWPOS so we never block
         * on the host's (possibly hitching) main thread. */
        if (canvas && GetClientRect(canvas, &r) && (r.right != w || r.bottom != h)) {
            w = r.right; h = r.bottom;
            SetWindowPos(s_glwnd, NULL, 0, 0, w, h,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
            set_viewport(w, h);
        }

        /* Drag with the left button to rotate; idle-spin otherwise. This reads
         * the mouse straight through the GL overlay via polling (see header). */
        buttons = ar_mouse(&mx, &my);
        if (buttons & AR_MOUSE_L) {
            if (dragging) { yaw += (mx - lastx) * 0.4f; pitch += (my - lasty) * 0.4f; }
            dragging = 1; lastx = mx; lasty = my;
        } else {
            dragging = 0;
            yaw += 0.8f;                       /* gentle auto-spin when idle */
        }

        glClearColor(0.06f, 0.09f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -5.0f);
        glRotatef(pitch, 1.0f, 0.0f, 0.0f);
        glRotatef(yaw,   0.0f, 1.0f, 0.0f);
        draw_cube();
        SwapBuffers(dc);

        Sleep(16);
    }

    timeEndPeriod(1);
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(s_glwnd, dc);
    return 0;
}

static int on_open(ArContext *ctx, HWND canvas, void *offer)
{
    HINSTANCE hi = self_instance();
    WNDCLASSA wc;
    RECT rc;
    (void)ctx; (void)offer;

    /* Register our child window class once (ignore "already registered"). */
    ZeroMemory(&wc, sizeof wc);
    wc.style         = CS_OWNDC;                 /* stable DC — nice for GL */
    wc.lpfnWndProc   = gl_wndproc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = GL_CLASS;
    RegisterClassA(&wc);

    if (!canvas || !GetClientRect(canvas, &rc)) return 1;   /* nonzero = decline */

    /* Create the child window on this (host) thread so the host's message pump
     * services it; the render thread only draws into it. */
    s_glwnd = CreateWindowExA(0, GL_CLASS, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                              0, 0, rc.right, rc.bottom, canvas, NULL, hi, NULL);
    if (!s_glwnd) return 1;

    s_run = 1;
    s_thread = CreateThread(NULL, 0, render_thread, canvas, 0, NULL);
    if (!s_thread) { s_run = 0; DestroyWindow(s_glwnd); s_glwnd = 0; return 1; }

    ar_print("glcube: OpenGL render thread is up. Drag to rotate.");
    return 0;
}

static void on_close(ArContext *ctx)
{
    (void)ctx;
    s_run = 0;
    if (s_thread) { WaitForSingleObject(s_thread, 2000); CloseHandle(s_thread); s_thread = 0; }
    if (s_glwnd)  { DestroyWindow(s_glwnd); s_glwnd = 0; }
}

void ArcadiaToyRegister(ArToy *toy)
{
    toy->name  = "glcube";
    toy->open  = on_open;
    toy->close = on_close;
    /* No tick and no paint: rendering runs on our own thread (see header), so
     * the host tick's periodic hitch can't stutter the animation, and the SDK's
     * GDI double-buffer never runs to fight our GL window. */
}
