/*
 * glcube — a hardware-accelerated OpenGL toy for Arcadia.
 *
 * Arcadia composites 8-bit DIB surfaces with GDI, so you can't get accelerated
 * GL *into* the DC it hands you. Instead this toy creates its own child window
 * over the Arcadia canvas, gives that child an OpenGL pixel format, and renders
 * to it directly with SwapBuffers — fully GPU-accelerated and independent of the
 * host's palette/compositing. It resizes with the Arcadia window.
 *
 * It uses OpenGL 1.1 fixed-function (immediate mode), so there is no shader or
 * extension-loading boilerplate — just wgl + glBegin/glEnd. Link opengl32.
 *
 * Note: it leaves the SDK's paint() callback unset, so the SDK's GDI
 * double-buffer stays out of the way; rendering is driven entirely from tick().
 */
#include <arcadia/toy.h>
#include <GL/gl.h>

static const char GL_CLASS[] = "ArcadiaGLChildWindow";

static HWND   s_glwnd;         /* our child window over the canvas */
static HDC    s_gldc;          /* its device context               */
static HGLRC  s_glrc;          /* the OpenGL rendering context      */
static int    s_w, s_h;        /* current child size                */
static float  s_angle;         /* cube rotation                     */

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

static int on_open(ArContext *ctx, HWND canvas, void *offer)
{
    HINSTANCE hi = self_instance();
    WNDCLASSA wc;
    RECT rc;
    PIXELFORMATDESCRIPTOR pfd;
    int pf;
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
    s_w = rc.right; s_h = rc.bottom;

    s_glwnd = CreateWindowExA(0, GL_CLASS, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                              0, 0, s_w, s_h, canvas, NULL, hi, NULL);
    if (!s_glwnd) return 1;
    s_gldc = GetDC(s_glwnd);

    ZeroMemory(&pfd, sizeof pfd);
    pfd.nSize      = sizeof pfd;
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pf = ChoosePixelFormat(s_gldc, &pfd);
    if (!pf || !SetPixelFormat(s_gldc, pf, &pfd)) return 1;

    s_glrc = wglCreateContext(s_gldc);
    if (!s_glrc) return 1;
    wglMakeCurrent(s_gldc, s_glrc);

    glEnable(GL_DEPTH_TEST);
    set_viewport(s_w, s_h);

    ar_print("glcube: OpenGL child window is up.");
    return 0;
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

static void on_tick(ArContext *ctx, unsigned now_ms)
{
    HWND canvas = ar_window();
    RECT rc;
    (void)ctx; (void)now_ms;

    if (!s_glrc || !canvas) return;

    /* Track the Arcadia canvas size. */
    if (GetClientRect(canvas, &rc) && (rc.right != s_w || rc.bottom != s_h)) {
        s_w = rc.right; s_h = rc.bottom;
        MoveWindow(s_glwnd, 0, 0, s_w, s_h, FALSE);
        set_viewport(s_w, s_h);
    }

    s_angle += 1.5f;

    glClearColor(0.06f, 0.09f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -5.0f);
    glRotatef(s_angle,        1.0f, 0.0f, 0.0f);
    glRotatef(s_angle * 0.7f, 0.0f, 1.0f, 0.3f);
    draw_cube();

    SwapBuffers(s_gldc);
}

static void on_close(ArContext *ctx)
{
    (void)ctx;
    wglMakeCurrent(NULL, NULL);
    if (s_glrc)  { wglDeleteContext(s_glrc); s_glrc = 0; }
    if (s_gldc)  { ReleaseDC(s_glwnd, s_gldc); s_gldc = 0; }
    if (s_glwnd) { DestroyWindow(s_glwnd); s_glwnd = 0; }
}

void ArcadiaToyRegister(ArToy *toy)
{
    toy->name  = "glcube";
    toy->open  = on_open;
    toy->tick  = on_tick;
    toy->close = on_close;
    /* No paint callback: rendering is done in tick() via SwapBuffers, so the
     * SDK's GDI double-buffer never runs and can't fight our GL window. */
}
