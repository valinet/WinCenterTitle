#include <valinet/ini/ini.h>
#include <valinet/pdb/pdb.h>
#include <stdio.h>
#include <stdbool.h>
#include <Windows.h>
#include <funchook.h>
#pragma comment(lib, "Psapi.lib") //required by funchook
#include <Uxtheme.h>
#pragma comment(lib, "Uxtheme.lib")

#define DEBUG TRUE
// https://stackoverflow.com/questions/1644868/define-macro-for-debug-printing-in-c
#if DEBUG
#define TRACE(fmt, ...) \
        do { if (DEBUG) fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, \
                                __LINE__, __func__, __VA_ARGS__); } while (0)
#else
#define TRACE(x)
#endif

#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

#define MARGIN_OVERHANG_WIDTH_FIX                   2
#define AERO_COLOR_OFFSET                           0x19
#define NUMBER_OF_REQUESTED_SYMBOLS_WINDOWS10       15
#define NUMBER_OF_REQUESTED_SYMBOLS_WINDOWS8        3
#define NUMBER_OF_REQUESTED_SYMBOLS_WINDOWS7        2
#define DOWNLOAD_SYMBOLS_ERROR                      0x100
#define GET_SYMBOLS_ERROR                           0x101
#define GET_OS_VERSION_ERROR                        0x102
#define OUT_OF_MEMORY_ERROR                         0x103
#define SETTINGS_RELATIVE_PATH                      "\\symbols\\settings.ini"
#define SETTINGS_ADDRESSES_SECTION_NAME             "Addresses"
#define MODULE_NAME_UDWM                            "uDWM"
#define DLL_NAME                                    MODULE_NAME_UDWM ".dll"
#define SYMBOL_NAME_MAX_LEN                         200
#define ADDR_G_PDMINSTANCE                          "g_pdmInstance"
#define ADDR_CDESKTOPMANAGER_LOADTHEME              "CDesktopManager::LoadTheme"
#define ADDR_CMATRIXTRANSFORMPROXY_UPDATE           "CMatrixTransformProxy::Update"
#define ADDR_CTEXT_VALIDATERESOURCES                "CText::ValidateResources"
#define ADDR_CTEXT_UPDATEALIGNMENTTRANFORM          "CText::UpdateAlignmentTransform"
#define ADDR_CTEXT_GETLEFTOFFSET                    "CText::GetLeftOffset"
#define ADDR_CVISUAL_HIDE                           "CVisual::Hide"
#define ADDR_CTOPLEVELWINDOW_UPDATEWINDOWVISUALS    "CTopLevelWindow::UpdateWindowVisuals"
#define ADDR_CSolidColorLegacyMilBrushProxyUpdate   "CSolidColorLegacyMilBrushProxy::Update"
#define ADDR_CTextSetBackgroundColor                "CText::SetBackgroundColor"
#define ADDR_CVisualSetOpacity                      "CVisual::SetOpacity"
#define ADDR_CAccentUpdateAccentPolicy              "CAccent::UpdateAccentPolicy"
#define ADDR_CTopLevelWindowOnAccentPolicyUpdated   "CTopLevelWindow::OnAccentPolicyUpdated"
#define ADDR_CAccent__UpdateSolidFill               "CAccent::_UpdateSolidFill"
#define ADDR_CAccent_GetShadowMargins               "CAccent::GetShadowMargins"
#define ADDR_ResourceHelperCreateRectangleGeometry  "CRectangleGeometryProxy::SetRectangle"
#define SETTINGS_OS_SECTION_NAME                    "OS"
#define OS_VERSION_STRING                           "Build"

#define TEXT_COLOR                                  RGB(0, 0, 0)
#undef  TEXT_COLOR

#define APPLICATION_ICON_SHOWN                      1
#define APPLICATION_ICON_HIDDEN                     0
#define APPLICATION_ICON_VISIBILITY                 APPLICATION_ICON_HIDDEN

#define ENTIRE_TITLEBAR                             10000

#define TITLEBARS_WHITE_DEFAULT                     1
#define TITLEBARS_COLORED                           0
#define TITLEBARS_DESIRED_COLOR                     TITLEBARS_WHITE_DEFAULT

typedef LONG NTSTATUS, * PNTSTATUS;
#define STATUS_SUCCESS (0x00000000)

typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

BOOL GetOSVersion(PRTL_OSVERSIONINFOW lpRovi)
{
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod != NULL)
    {
        RtlGetVersionPtr fxPtr = (RtlGetVersionPtr)GetProcAddress(
            hMod,
            "RtlGetVersion"
        );
        if (fxPtr != NULL)
        {
            lpRovi->dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);
            if (STATUS_SUCCESS == fxPtr(lpRovi))
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

funchook_t* funchook = NULL;
HMODULE hModule = NULL;
BYTE* titlebar_color = NULL;
BYTE old_titlebar_color = 0;

typedef struct _WINCOMPATTRDATA
{
    DWORD dwAttribute;
    LPCVOID pvAttribute;
    DWORD cbAttribute;
} WINCOMPATTRDATA;

static HRESULT(*SetWindowCompositionAttribute)(
    HWND,
    WINCOMPATTRDATA*
    );

typedef struct _milmatrix
{
    double s11;
    double s12;
    double s21;
    double s22;
    double dx;
    double dy;
} milmatrix;

typedef struct _D3DCOLORVALUE {
    float r;
    float g;
    float b;
    float a;
} D3DCOLORVALUE;

void* g_CTextInstance;

static int64_t(*CTextGetLeftOffsetFunc)(
    void* _this
    );

int64_t CTextGetLeftOffsetHook(
    void* _this
)
{
    // On Windows 8, there is this function which offers an x offset for the
    // transform matrix of the title label; by setting it to 0, the
    // behavour is like in Windows 7 (i.e. left align)
    return 0;
}

static int64_t(*CVisualSetOpacity)(
    void* _this,
    double opacity
    );

static int64_t(*CTextSetBackgroundColorFunc)(
    void* _this,
    int a1
    );

static int64_t(*CSolidColorLegacyMilBrushProxyUpdateFunc)(
    void* _this,
    double a1,
    D3DCOLORVALUE* color
    );

int64_t CSolidColorLegacyMilBrushProxyUpdateHook(
    void* _this,
    double a1,
    D3DCOLORVALUE* color
)
{
    float r = color->r;
    float g = color->g;
    float b = color->b;
    float a = color->a;
    //printf("%p %p %f %f %f %f\n", _ReturnAddress(), _this, r, g, b, a);
    if (r == 1.0 && g == 1.0 && b == 1.0 && a == 1.0)
    {
        color->r = 0.0; //0.0;
        color->g = 0.0; // 0.63;
        color->b = 0.0; // 0.9;
        color->a = 0.0; // 0.6;
    }
    return CSolidColorLegacyMilBrushProxyUpdateFunc(
        _this,
        a1,
        color
    );
}

DWORD async_print(DWORD param)
{
    printf("%u\n", param);
}

static int64_t(*ResourceHelperCreateRectangleGeometryFunc)(
    void* _t,
    float x,
    float y,
    float n
    );

int64_t ResourceHelperCreateRectangleGeometryHook(
    void* _t,
    float x,
    float y,
    float n
)
{
    //printf("%f %f %f\n", x, y, n);
    return ResourceHelperCreateRectangleGeometryFunc(
        _t,
        x,
        y,
        n
    );
}

static int64_t(*CAccent_GetShadowMarginsFunc)(
    void* _this,
    void* a2
    );

int64_t CAccent_GetShadowMarginsHook(
    void* _this,
    void* a2
)
{
    MARGINS* m = CAccent_GetShadowMarginsFunc(
        _this,
        a2
    );
    printf("%d %d %d %d\n", m->cxLeftWidth, m->cxRightWidth, m->cyTopHeight, m->cyBottomHeight);
    m->cxLeftWidth = 0;
    m->cxRightWidth = 0;
    m->cyTopHeight = 0;
    m->cyBottomHeight = 0;
    return m;
}


typedef enum _ACCENT {							// Values passed to SetWindowCompositionAttribute determining the appearance of a window
    ACCENT_ENABLE_GRADIENT = 1,					// Use a solid color specified by nColor. This mode ignores the alpha value and is fully opaque.
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,		// Use a tinted transparent overlay. nColor is the tint color.
    ACCENT_ENABLE_BLURBEHIND = 3,				// Use a tinted blurry overlay. nColor is the tint color.
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,					// Use an aspect similar to Fluent design. nColor is tint color. This mode bugs if the alpha value is 0.

    ACCENT_NORMAL = 150							// (Fake value) Emulate regular taskbar appearance
} ACCENT;

typedef struct _ACCENTPOLICY				// Determines how a window's transparent region will be painted
{
    ACCENT   nAccentState;			// Appearance
    int32_t  nFlags;				// Nobody knows how this value works
    uint32_t nColor;				// A color in the hex format AABBGGRR
    int32_t  nAnimationId;			// Nobody knows how this value works
} ACCENTPOLICY;

static int64_t(*CAccentUpdateAccentPolicyFunc)(
    void* _this,
    RECT* rect,
    ACCENTPOLICY* policy,
    void* proxy
    );

int64_t CAccentUpdateAccentPolicyHook(
    void* _this,
    RECT* rect,
    ACCENTPOLICY* policy,
    void* proxy
)
{
    /*if (rect)
    {
        printf("%d %d %d %d\n", rect->left, rect->right, rect->top, rect->bottom);
    }*/

    policy->nAccentState = ACCENT_ENABLE_BLURBEHIND;
    policy->nFlags = 0x2; //0xE2; // CAccent::UpdateLayout 0xE0
    policy->nColor = 0x80efdead;

    RECT rc = *rect;
    //rect->right -= 11;
    //rect->bottom -= 11;

    int64_t rv = CAccentUpdateAccentPolicyFunc(
        _this,
        rect,
        policy,
        proxy
    );
    *rect = rc;
    return rv;
}

static int64_t(*CAccent__UpdateSolidFillFunc)(
    void* _this,
    void* renderData,
    int a3,
    void* a4,
    int a5
    );

int64_t CAccent__UpdateSolidFillHook(
    void* _this,
    void* renderData,
    int a3,
    void* a4,
    int a5
)
{
    int64_t rv;
    //printf("%x\n", a3);
    rv = CAccent__UpdateSolidFillFunc(
        _this,
        renderData,
        a3,
        a4,
        a5
    );

    return rv;
}

static int64_t(*CVisualHideFunc)(
    void* _this
    );

static int64_t(*CTopLevelWindowOnAccentPolicyUpdatedFunc)(
    void* _this
    );

static int64_t(*CTopLevelWindowUpdateButtonVisualsFunc)(
    void* _this
    );

int64_t CTopLevelWindowUpdateButtonVisualsHook(
    void* _this
)
{
    int64_t rv;

    // this is how you locate the hWnd of the window in a CTopLevelWindow
    // instance; to find this out, search for uses of
    // SetWindowCompositionAttribute in the symbols
    //
    // uintptr_t v55 = *((void**)_this + 91);
    // HWND hWnd = *(void**)(v55 + 40);

    // type of blur is in accent + 70 (CAccent::UpdateAccentPolicy)

    uintptr_t v55 = *((void**)_this + 91);
    RECT* rc = v55 + 48;
    DWORD* acs = v55 + 152; // this is type of blur, found out in CTopLevelWindow::ValidateVisual - UpdateAccentPolicy
    if (*acs == 0)
    {
        *acs = 3;
        CTopLevelWindowOnAccentPolicyUpdatedFunc(_this);
        //uintptr_t accent = *((void**)_this + 34);
        //BYTE* gradient = ((BYTE*)accent + 284); // CAccent::_UpdateAccentBlurBehind
        //uint32_t* color = (uint32_t*)(((BYTE*)accent + 376)); // GetSolidFillOpacity
        //*gradient = 2;
        //*color = 0xBEEFDEAD;
        //*((DWORD*)accent + 72) = 0xbeefdead;
        uintptr_t accent = *((void**)_this + 34);
        RECT* rect = ((char*)accent + 616);
        rect->left += 10;
    }
    if (*acs == 3)
    {
        uintptr_t accent = *((void**)_this + 34);
        //BYTE* gradient = ((BYTE*)accent + 284);
        //uint32_t* color = (uint32_t*)(((BYTE*)accent + 376)); // = 94 = 72 ; GetSolidFillOpacity
        /* _mm_storeu_si128((__m128i *)((char *)v4 + 280), v8);
      v11 = (*((_BYTE *)v4 + 284) & 1) == 0;
      v12 = *((_DWORD *)v4 + 72);
      *((_DWORD *)v4 + 94) = v12;
      */
        //printf("%d %d %d %d - %d %d -- %d %x\n", rc->left, rc->right, rc->top, rc->bottom, *acs, accent, *gradient, *color);
        // *gradient = 2;
        // *color = 0xBEEFDEAD;
        //printf("%p\n", ((DWORD*)accent + 72));
        //*((DWORD*)accent + 72) = 0xbeefdead;
        RECT* rect = ((char*)accent + 616);
        //printf("%p %d %d %d %d\n", rect, rect->left, rect->right, rect->top, rect->bottom);
        //rect->left += 10;
    }



    rv = CTopLevelWindowUpdateButtonVisualsFunc(_this);

    /*
    void** v8 = (void**)((char*)_this + 272);
    void* v14 = *v8;
    if (v14)
    {
        DWORD* v13 = ((DWORD*)v14 + 70);
        *v13 = 1;
    }
    */


    // this is how you can hide the icon; to determine its location,
    // check out CTopLevelWindow::UpdateWindowVisuals, before updating
    // the CText, it also updates a CVisual which is the icon
#if APPLICATION_ICON_VISIBILITY == APPLICATION_ICON_HIDDEN
    if (*((void**)_this + 66))
    {
        CVisualHideFunc(
            *((void**)_this + 66)
        );
    }
#endif
    
    return rv;
}
BOOL bED = TRUE;

// Paint the title on the custom frame.
void PaintCustomCaption(RECT rcClient, HDC hdc)
{
    HTHEME hTheme = OpenThemeData(NULL, L"CompositedWindow::Window");
    if (hTheme)
    {
        HDC hdcPaint = CreateCompatibleDC(hdc);
        if (hdcPaint)
        {
            int cx = rcClient.right - rcClient.left;
            int cy = rcClient.bottom - rcClient.top;

            // Define the BITMAPINFO structure used to draw text.
            // Note that biHeight is negative. This is done because
            // DrawThemeTextEx() needs the bitmap to be in top-to-bottom
            // order.
            BITMAPINFO dib = { 0 };
            dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            dib.bmiHeader.biWidth = cx;
            dib.bmiHeader.biHeight = -cy;
            dib.bmiHeader.biPlanes = 1;
            dib.bmiHeader.biBitCount = 32;
            dib.bmiHeader.biCompression = BI_RGB;

            void* ppvBits;
            HBITMAP hbm = CreateDIBSection(hdc, &dib, DIB_RGB_COLORS, &ppvBits, NULL, 0);
            if (hbm)
            {
                HBITMAP hbmOld = (HBITMAP)SelectObject(hdcPaint, hbm);

                // Setup the theme drawing options.
                DTTOPTS DttOpts = { sizeof(DTTOPTS) };
                DttOpts.dwFlags = DTT_COMPOSITED | DTT_GLOWSIZE;
                DttOpts.iGlowSize = 15;

                // Select a font.
                LOGFONT lgFont;
                HFONT hFontOld = NULL;
                if (GetThemeSysFont(hTheme, 0, &lgFont) == S_OK)
                {
                    HFONT hFont = CreateFontIndirect(&lgFont);
                    hFontOld = (HFONT)SelectObject(hdcPaint, hFont);
                }

                // Draw the title.
                RECT rcPaint = rcClient;
                rcPaint.top = 0;
                rcPaint.right = rcClient.right - rcClient.left;
                rcPaint.left = 0;
                rcPaint.bottom = rcClient.bottom - rcClient.top;
                SetTextColor(hdc, RGB(255, 0, 0));
                DrawThemeTextEx(hTheme,
                    hdcPaint,
                    0, 0,
                    L"Welcome",
                    -1,
                    DT_LEFT | DT_WORD_ELLIPSIS,
                    &rcPaint,
                    &DttOpts);
                
                // Blit text to the frame.
                BitBlt(hdc, 20, 0, cx, cy, hdcPaint, 0, 0, SRCCOPY);

                

                SelectObject(hdcPaint, hbmOld);
                if (hFontOld)
                {
                    SelectObject(hdcPaint, hFontOld);
                }
                DeleteObject(hbm);
            }
            DeleteDC(hdcPaint);
        }
        CloseThemeData(hTheme);
    }
}

static int64_t(*CTextUpdateAlignmentTransformFunc)(
    void* _this
    );

static int64_t(*CTextValidateResourcesFuncW10)(
    void* _this
    );
HDC ghdc = 0;
int64_t CTextValidateResourcesHookW10(
    void* _this
)
{
    int64_t rv;
    DWORD* val;

    //CTextSetBackgroundColorFunc(_this, 0x000000FF);

    // memorize address of this instance of CText, to be used in a subsequent
    // call to CMatrixTransformProxy::Update on chain
    // CText::ValidateResources - CText::UpdateAlignmentTransform
    g_CTextInstance = _this;
    // call original function
    rv = CTextValidateResourcesFuncW10(_this);
    // we need this hack so that we trick DWM into triggering an update of the
    // title label at each resize, as now the title is centered so its
    // position should change when the size of the window changes;
    // how this works is that it seems DWM decides whether to update the
    // label based on a check of whether the label size exceeded the window
    // size; so, by setting the label size to a very large value, DWM is
    // tricked into thinking the overflow is always happening
    val = ((DWORD*)g_CTextInstance + 100);
    if (*val < ENTIRE_TITLEBAR)
    {
        *val += ENTIRE_TITLEBAR;
    }

    return rv;
}

static int64_t(*CMatrixTransformProxyUpdateFunc)(
    void* _this,
    milmatrix* mat
    );

int64_t CMatrixTransformProxyUpdateHook(
    void* _this,
    milmatrix* mat
)
{
    // this approach is based on how DWM centered the title bar text in
    // Windows 8: instead of drawing the label from side to side with
    // DT_CENTER, as we also did previously, DWM used to move the position
    // where the label is rendered; although this was not necessary, it
    // enabled the possiblity of Aero to work correctly (by not blurring the
    // entire title bar, just the portion containing text), if Aero would
    // have made a comeback

    if (g_CTextInstance)
    {
        // this is the width of the title bar label containing the text
        double v1 = *((DWORD*)g_CTextInstance + 100);
        // this is the size available for the label (the size between the
        // window icon, or the left window margin, and the caption buttons, or the
        // right window margin)
        double v3 = *((DWORD*)g_CTextInstance + 30);
        // this is the height of the titlebar text (UNUSED)
        double v4 = *((DWORD*)g_CTextInstance + 31);
        // the following have been found out by analyzing the function
        // UpdateNCAreaPositionsAndSizes
        // this is the width of the window icon
        double v6 = *((DWORD*)g_CTextInstance + 32);
        // this is the width of the window caption buttons (for example,
        // Close, Minimize, Maximize etc)
        double v5 = *((DWORD*)g_CTextInstance + 33);
        // this also contains the width and height:
        // tagSIZE st = *((tagSIZE*)g_CTextInstance + 15);
        // where tagSIZE is a struct of 2 LONGs (width, and height)
        //printf("%p %p %f %d\n", g_CTextInstance, (DWORD*)g_CTextInstance + 30, v3, (*((DWORD*)g_CTextInstance + 148) & 0x400000));


        // check if caller is CText::UpdateAlignmentTransfor
        // do not hook CTextUpdateAlignmentTransform for this to work
        // also, could be better improved by parsing the PE header
        // or looking for the ret instruction of this code block;
        // for now, for regular dwm compiles, it seems to work just fine like 
        // this
        if (
            (-1) * (uintptr_t)_ReturnAddress() >
            (-1) * (uintptr_t)CTextUpdateAlignmentTransformFunc - 120
            )
        {
            // take into account previous hack; we need the real dimension
            if (v1 > ENTIRE_TITLEBAR)
            {
                v1 = v1 - ENTIRE_TITLEBAR;
            }
            // we determine the "origin" of the label
            double dx = (v3 + v5 - v6 - v1) / 2;
            // this happens when the centered label overlaps the caption 
            // buttons in that case, we center classically, between icon
            // and caption buttons, same as Windows 8 did in this case
            if (dx + v1 > v3)
            {
                dx = (v3 - v1) / 2;
            }
            // if there is not enough space to display the title, we left
            // align,  and text ellipsis will be shown anyway
            if (dx < 0)
            {
                mat->dx = 0;
            }
            else
            {
                mat->dx = (int)dx;
            }
        }
    }

    // call original function
    return CMatrixTransformProxyUpdateFunc(_this, mat);
}

static int64_t(*CDesktopManagerLoadThemeFunc)(
    void* _this
    );

int64_t CDesktopManagerLoadThemeHook(
    void* _this
)
{
    int64_t ret = CDesktopManagerLoadThemeFunc(_this);
    *titlebar_color = TITLEBARS_DESIRED_COLOR;
    return ret;
}

static int(*FillRectFunc)(
    HDC        hDC,
    const RECT* lprc,
    HBRUSH     hbr
    );

int FillRectHook(
    HDC        hDC,
    const RECT* lprc,
    HBRUSH     hbr
)
{
    //printf("da\n");
    //return 1;
    HBRUSH br = CreateSolidBrush(RGB(188, 188, 188));
    int rv = FillRectFunc(
        hDC,
        lprc,
        hbr
    );
    DeleteObject(br);
    return rv;
}

static int(*DrawTextWFunc)(
    HDC     hdc,
    LPCWSTR lpchText,
    int     cchText,
    LPRECT  lprc,
    UINT    format
    );

int DrawTextWHook(
    HDC     hdc,
    LPCWSTR lpchText,
    int     cchText,
    LPRECT  lprc,
    UINT    format
)
{
#ifdef TEXT_COLOR
    SetTextColor(hdc, TEXT_COLOR);
#endif
    ghdc = hdc;
    if (!bED)
    {
        //wprintf(L"%d %s\n", format, lpchText);
        /*DTTOPTS ops;
        ops.dwSize = sizeof(DTTOPTS);
        ops.dwFlags = 0;*/
        int rv = DrawTextWFunc(
            hdc,
            lpchText,
            cchText,
            lprc,
            format
        );
        if (format & DT_CALCRECT)
        {
            //HTHEME hTheme = OpenThemeData(NULL, L"DWMWindow");

            /*DrawThemeTextEx(
                hTheme,
                hdc,
                0,
                0,
                lpchText,
                cchText,
                format,
                lprc,
                &ops
            );
            CloseThemeData(hTheme);
            */

            /*lprc->top = 0;
            lprc->left = 0;
            lprc->bottom = 0;
            lprc->right = 0;*/
            return rv;
        }
        bED = TRUE;
        //printf("%d %d %d %d\n", lprc->left, lprc->right, lprc->top, lprc->bottom);
        SetTextColor(hdc, RGB(255, 255, 255));
        PaintCustomCaption(*lprc, hdc);
        bED = FALSE;
        return rv;
    }
    else
    {
        SetBkMode(hdc, TRANSPARENT);
        int rv = DrawTextWFunc(
            hdc,
            lpchText,
            cchText,
            lprc,
            format
        );
        //SetBkColor(hdc, OPAQUE);
        return rv;
    }
    /*
    int rv = 0;

    if (!(format & DT_CALCRECT))
    {
        // fixes title label sometimes overlapping right margin of window
        lprc->right -= MARGIN_OVERHANG_WIDTH_FIX;
#ifdef SET_COLOR
        SetTextColor(hdc, TEXT_COLOR);
#endif
    }
    rv = DrawTextWFunc(
        hdc,
        lpchText,
        cchText,
        lprc,
        (format & (~DT_LEFT)) | DT_CENTER
    );
    if (format & DT_CALCRECT)
    {
        lprc->right = ENTIRE_TITLEBAR;
    }
    return rv;
    */
}


__declspec(dllexport) DWORD WINAPI main(
    _In_ LPVOID lpParameter
)
{
#if DEBUG
    FILE* conout;
    AllocConsole();
    freopen_s(
        &conout, 
        "CONOUT$", 
        "w", 
        stdout
    );
#endif

    int rv = 0;
   
    if (!funchook)
    {
        const HINSTANCE hMod = LoadLibrary(TEXT("user32.dll"));
        if (hMod)
        {
            SetWindowCompositionAttribute = (int64_t(*)(void*))(
                GetProcAddress(hMod, "SetWindowCompositionAttribute")
                );
            FreeLibrary(hMod);
        }

        funchook = funchook_create();
        if (!funchook)
        {
            TRACE(
                "%s", 
                "[ERROR!] funchook: Out of memory.\n"
            );
            FreeLibraryAndExitThread(
                hModule,
                OUT_OF_MEMORY_ERROR
            );
            return OUT_OF_MEMORY_ERROR;
        }

        FillRectFunc = FillRect;
        rv = funchook_prepare(
            funchook,
            (void**)&FillRectFunc,
            FillRectHook
        );
        if (rv != 0)
        {
            FreeLibraryAndExitThread(
                hModule,
                rv
            );
            return rv;
        }

        DrawTextWFunc = DrawTextW;
        rv = funchook_prepare(
            funchook, 
            (void**)&DrawTextWFunc, 
            DrawTextWHook
        );
        if (rv != 0) 
        {
            FreeLibraryAndExitThread(
                hModule, 
                rv
            );
            return rv;
        }

        // determine aero.msstyles code path flag location
        if (titlebar_color == NULL)
        {
            DWORD dwNumberOfSymbols = 0;
            char* symbolNamesWindows10[] = {
                ADDR_G_PDMINSTANCE,
                ADDR_CDESKTOPMANAGER_LOADTHEME,
                ADDR_CMATRIXTRANSFORMPROXY_UPDATE,
                ADDR_CTEXT_VALIDATERESOURCES,
                ADDR_CTEXT_UPDATEALIGNMENTTRANFORM,
                ADDR_CVISUAL_HIDE,
                ADDR_CTOPLEVELWINDOW_UPDATEWINDOWVISUALS,
                ADDR_CSolidColorLegacyMilBrushProxyUpdate,
                ADDR_CTextSetBackgroundColor,
                ADDR_CVisualSetOpacity,
                ADDR_CAccentUpdateAccentPolicy,
                ADDR_CTopLevelWindowOnAccentPolicyUpdated,
                ADDR_CAccent__UpdateSolidFill,
                ADDR_CAccent_GetShadowMargins,
                ADDR_ResourceHelperCreateRectangleGeometry
            };
            char* symbolNamesWindows8[] = {
                ADDR_G_PDMINSTANCE,
                ADDR_CDESKTOPMANAGER_LOADTHEME,
                ADDR_CTEXT_GETLEFTOFFSET
            };
            char* symbolNamesWindows7[] = {
                ADDR_G_PDMINSTANCE,
                ADDR_CDESKTOPMANAGER_LOADTHEME
            };
            char* symbolNames = NULL;
            // https://stackoverflow.com/questions/36543301/detecting-windows-10-version/36543774#36543774
            RTL_OSVERSIONINFOW rovi;
            if (!GetOSVersion(&rovi))
            {
                TRACE("%s", "[ERROR!] GetOSVersion\n");
                FreeLibraryAndExitThread(
                    hModule,
                    GET_OS_VERSION_ERROR
                );
                return GET_OS_VERSION_ERROR;
            }
            // enable left aligning for Windows 8, and Windows 8.1
            if (rovi.dwMajorVersion >= 10)
            {
                dwNumberOfSymbols = NUMBER_OF_REQUESTED_SYMBOLS_WINDOWS10;
                symbolNames = symbolNamesWindows10;
            }
            else if ((rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 2) ||
                     (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 3))
            {
                dwNumberOfSymbols = NUMBER_OF_REQUESTED_SYMBOLS_WINDOWS8;
                symbolNames = symbolNamesWindows8;
            }
            else
            {
                dwNumberOfSymbols = NUMBER_OF_REQUESTED_SYMBOLS_WINDOWS7;
                symbolNames = symbolNamesWindows7;
            }
            // https://stackoverflow.com/questions/47926094/detecting-windows-10-os-build-minor-version
            DWORD32 ubr = 0, ubr_size = sizeof(DWORD32);
            HKEY hKey;
            LONG lRes = RegOpenKeyExW(
                HKEY_LOCAL_MACHINE,
                wcschr(
                    wcschr(
                        wcschr(
                            UNIFIEDBUILDREVISION_KEY,
                            '\\'
                        ) + 1,
                        '\\'
                    ) + 1,
                    '\\'
                ) + 1,
                0,
                KEY_READ,
                &hKey
            );
            if (lRes == ERROR_SUCCESS)
            {
                RegQueryValueExW(
                    hKey,
                    UNIFIEDBUILDREVISION_VALUE,
                    0,
                    NULL,
                    &ubr,
                    &ubr_size
                );
            }

            SIZE_T dwRet;

            DWORD addresses[MAX(
                NUMBER_OF_REQUESTED_SYMBOLS_WINDOWS10,
                MAX(NUMBER_OF_REQUESTED_SYMBOLS_WINDOWS8,
                    NUMBER_OF_REQUESTED_SYMBOLS_WINDOWS7)
            )];
            ZeroMemory(
                addresses, 
                MAX(
                    NUMBER_OF_REQUESTED_SYMBOLS_WINDOWS10,
                    MAX(NUMBER_OF_REQUESTED_SYMBOLS_WINDOWS8,
                        NUMBER_OF_REQUESTED_SYMBOLS_WINDOWS7))
                * sizeof(DWORD)
            );
            char szSettingsPath[_MAX_PATH];
            ZeroMemory(
                szSettingsPath, 
                (_MAX_PATH) * sizeof(char)
            );
            TCHAR wszSettingsPath[_MAX_PATH];
            ZeroMemory(
                wszSettingsPath, 
                (_MAX_PATH) * sizeof(TCHAR)
            );
            GetModuleFileNameA(
                hModule,
                szSettingsPath,
                _MAX_PATH
            );
            PathRemoveFileSpecA(szSettingsPath);
            strcat_s(
                szSettingsPath,
                _MAX_PATH,
                SETTINGS_RELATIVE_PATH
            );
            mbstowcs_s(
                &dwRet,
                wszSettingsPath,
                _MAX_PATH,
                szSettingsPath,
                _MAX_PATH
            );
            addresses[0] = VnGetUInt(
                TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                TEXT(ADDR_G_PDMINSTANCE),
                0,
                wszSettingsPath
            );
            addresses[1] = VnGetUInt(
                TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                TEXT(ADDR_CDESKTOPMANAGER_LOADTHEME),
                0,
                wszSettingsPath
            );
            if (symbolNames == symbolNamesWindows10)
            {
                addresses[2] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CMATRIXTRANSFORMPROXY_UPDATE),
                    0,
                    wszSettingsPath
                );
                addresses[3] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CTEXT_VALIDATERESOURCES),
                    0,
                    wszSettingsPath
                );
                addresses[4] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CTEXT_UPDATEALIGNMENTTRANFORM),
                    0,
                    wszSettingsPath
                );
                addresses[5] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CVISUAL_HIDE),
                    0,
                    wszSettingsPath
                );
                addresses[6] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CTOPLEVELWINDOW_UPDATEWINDOWVISUALS),
                    0,
                    wszSettingsPath
                );
                addresses[7] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CSolidColorLegacyMilBrushProxyUpdate),
                    0,
                    wszSettingsPath
                );
                addresses[8] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CTextSetBackgroundColor),
                    0,
                    wszSettingsPath
                );
                addresses[9] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CVisualSetOpacity),
                    0,
                    wszSettingsPath
                );
                addresses[10] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CAccentUpdateAccentPolicy),
                    0,
                    wszSettingsPath
                );
                addresses[11] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CTopLevelWindowOnAccentPolicyUpdated),
                    0,
                    wszSettingsPath
                );
                addresses[12] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CAccent__UpdateSolidFill),
                    0,
                    wszSettingsPath
                );
                addresses[13] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CAccent_GetShadowMargins),
                    0,
                    wszSettingsPath
                );  
                addresses[14] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_ResourceHelperCreateRectangleGeometry),
                    0,
                    wszSettingsPath
                );
            }
            else if (symbolNames == symbolNamesWindows8)
            {
                addresses[2] = VnGetUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CTEXT_GETLEFTOFFSET),
                    0,
                    wszSettingsPath
                );
            }
            else if (symbolNames == symbolNamesWindows7)
            {

            }
            TCHAR szReportedVersion[_MAX_PATH];
            ZeroMemory(
                szReportedVersion,
                (_MAX_PATH) * sizeof(TCHAR)
            );
            TCHAR szStoredVersion[_MAX_PATH];
            ZeroMemory(
                szStoredVersion,
                (_MAX_PATH) * sizeof(TCHAR)
            );
            wsprintf(
                szReportedVersion,
                L"%d.%d.%d.%d",
                rovi.dwMajorVersion,
                rovi.dwMinorVersion,
                rovi.dwBuildNumber,
                ubr
            );
            VnGetString(
                TEXT(SETTINGS_OS_SECTION_NAME),
                TEXT(OS_VERSION_STRING),
                szStoredVersion,
                _MAX_PATH,
                _MAX_PATH,
                NULL,
                wszSettingsPath
            );

            if (addresses[0] == 0 || 
                addresses[1] == 0 || 
                addresses[2] == 0 || 
                addresses[3] == 0 ||
                addresses[4] == 0 || 
                addresses[5] == 0 ||
                wcscmp(szReportedVersion, szStoredVersion)
            )
            {
                if (VnDownloadSymbols(
                    hModule,
                    DLL_NAME,
                    szSettingsPath,
                    _MAX_PATH
                ))
                {
                    TRACE(
                        "%s", 
                        "[ERROR!] VnDownloadSymbols\n"
                    );
                    FreeLibraryAndExitThread(
                        hModule, 
                        DOWNLOAD_SYMBOLS_ERROR
                    );
                    return DOWNLOAD_SYMBOLS_ERROR;
                }
                INT x;
                if (x = VnGetSymbols(
                    szSettingsPath,
                    addresses,
                    symbolNames,
                    dwNumberOfSymbols
                ))
                {
                    TRACE(
                        "%s", 
                        "[ERROR!] VnGetSymbols\n"
                    );
                    FreeLibraryAndExitThread(
                        hModule, 
                        -x
                    );
                    return GET_SYMBOLS_ERROR;
                }
                VnWriteUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_G_PDMINSTANCE),
                    addresses[0],
                    wszSettingsPath
                );
                VnWriteUInt(
                    TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                    TEXT(ADDR_CDESKTOPMANAGER_LOADTHEME),
                    addresses[1],
                    wszSettingsPath
                );
                if (symbolNames == symbolNamesWindows10)
                {
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CMATRIXTRANSFORMPROXY_UPDATE),
                        addresses[2],
                        wszSettingsPath
                    );
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CTEXT_VALIDATERESOURCES),
                        addresses[3],
                        wszSettingsPath
                    );
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CTEXT_UPDATEALIGNMENTTRANFORM),
                        addresses[4],
                        wszSettingsPath
                    );
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CVISUAL_HIDE),
                        addresses[5],
                        wszSettingsPath
                    );
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CTOPLEVELWINDOW_UPDATEWINDOWVISUALS),
                        addresses[6],
                        wszSettingsPath
                    );
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CSolidColorLegacyMilBrushProxyUpdate),
                        addresses[7],
                        wszSettingsPath
                    );
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CTextSetBackgroundColor),
                        addresses[8],
                        wszSettingsPath
                    );
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CVisualSetOpacity),
                        addresses[9],
                        wszSettingsPath
                    );
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CAccentUpdateAccentPolicy),
                        addresses[10],
                        wszSettingsPath
                    );
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CTopLevelWindowOnAccentPolicyUpdated),
                        addresses[11],
                        wszSettingsPath
                    );
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CAccent__UpdateSolidFill),
                        addresses[12],
                        wszSettingsPath
                    );
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CAccent_GetShadowMargins),
                        addresses[13],
                        wszSettingsPath
                    );
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_ResourceHelperCreateRectangleGeometry),
                        addresses[14],
                        wszSettingsPath
                    );
                }
                else if (symbolNames == symbolNamesWindows8)
                {
                    VnWriteUInt(
                        TEXT(SETTINGS_ADDRESSES_SECTION_NAME),
                        TEXT(ADDR_CTEXT_GETLEFTOFFSET),
                        addresses[2],
                        wszSettingsPath
                    );
                }
                else if (symbolNames == symbolNamesWindows7)
                {

                }
                VnWriteString(
                    TEXT(SETTINGS_OS_SECTION_NAME),
                    TEXT(OS_VERSION_STRING),
                    szReportedVersion,
                    wszSettingsPath
                );
            }

            HANDLE hudwm = GetModuleHandle(TEXT(MODULE_NAME_UDWM));
            uintptr_t* g_pdmInstance = (uintptr_t*)(
                (uintptr_t)hudwm + 
                (uintptr_t)addresses[0]
                );
            titlebar_color = (BYTE*)(
                (uintptr_t)(*g_pdmInstance) + 
                (uintptr_t)AERO_COLOR_OFFSET
                );
            old_titlebar_color = *titlebar_color;
            *titlebar_color = TITLEBARS_DESIRED_COLOR;

            CDesktopManagerLoadThemeFunc = (int64_t(*)(void*))(
                (uintptr_t)hudwm + 
                (uintptr_t)addresses[1]
                );
            rv = funchook_prepare(
                funchook, 
                (void**)&CDesktopManagerLoadThemeFunc, 
                CDesktopManagerLoadThemeHook
            );
            if (rv != 0)
            {
                TRACE(
                    "%s", 
                    "[ERROR!] funchook: CDesktopManager::LoadTheme\n"
                );
                FreeLibraryAndExitThread(
                    hModule, 
                    rv
                );
                return rv;
            }

            if (symbolNames == symbolNamesWindows10)
            {
                CMatrixTransformProxyUpdateFunc = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[2]
                    );
                rv = funchook_prepare(
                    funchook,
                    (void**)&CMatrixTransformProxyUpdateFunc,
                    CMatrixTransformProxyUpdateHook
                );
                if (rv != 0)
                {
                    TRACE(
                        "%s",
                        "[ERROR!] funchook: CMatrixTransformProxy::Update\n"
                    );
                    FreeLibraryAndExitThread(
                        hModule,
                        rv
                    );
                    return rv;
                }

                CTextValidateResourcesFuncW10 = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[3]
                    );
                rv = funchook_prepare(
                    funchook,
                    (void**)&CTextValidateResourcesFuncW10,
                    CTextValidateResourcesHookW10
                );
                if (rv != 0)
                {
                    TRACE(
                        "%s",
                        "[ERROR!] funchook: CText::ValidateResources\n"
                    );
                    FreeLibraryAndExitThread(
                        hModule,
                        rv
                    );
                    return rv;
                }

                CTextUpdateAlignmentTransformFunc = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[4]
                    );

                CVisualHideFunc = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[5]
                    );

                CTopLevelWindowUpdateButtonVisualsFunc = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[6]
                    );
                rv = funchook_prepare(
                    funchook,
                    (void**)&CTopLevelWindowUpdateButtonVisualsFunc,
                    CTopLevelWindowUpdateButtonVisualsHook
                );
                if (rv != 0)
                {
                    TRACE(
                        "%s",
                        "[ERROR!] funchook: CTopLevelWindow::UpdateButtonVisualsFunc\n"
                    );
                    FreeLibraryAndExitThread(
                        hModule,
                        rv
                    );
                    return rv;
                }

                CSolidColorLegacyMilBrushProxyUpdateFunc = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[7]
                    );
                rv = funchook_prepare(
                    funchook,
                    (void**)&CSolidColorLegacyMilBrushProxyUpdateFunc,
                    CSolidColorLegacyMilBrushProxyUpdateHook
                );
                if (rv != 0)
                {
                    TRACE(
                        "%s",
                        "[ERROR!] funchook: CSolidColorLegacyMilBrushProxy::Update\n"
                    );
                    FreeLibraryAndExitThread(
                        hModule,
                        rv
                    );
                    return rv;
                }

                CTextSetBackgroundColorFunc = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[8]
                    );

                CVisualSetOpacity = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[9]
                    );

                CAccentUpdateAccentPolicyFunc = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[10]
                    );
                rv = funchook_prepare(
                    funchook,
                    (void**)&CAccentUpdateAccentPolicyFunc,
                    CAccentUpdateAccentPolicyHook
                );
                if (rv != 0)
                {
                    TRACE(
                        "%s",
                        "[ERROR!] funchook: CAccent::UpdateAccentPolicyFunc\n"
                    );
                    FreeLibraryAndExitThread(
                        hModule,
                        rv
                    );
                    return rv;
                }

                CTopLevelWindowOnAccentPolicyUpdatedFunc = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[11]
                    );

                CAccent__UpdateSolidFillFunc = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[12]
                    );
                rv = funchook_prepare(
                    funchook,
                    (void**)&CAccent__UpdateSolidFillFunc,
                    CAccent__UpdateSolidFillHook
                );
                if (rv != 0)
                {
                    TRACE(
                        "%s",
                        "[ERROR!] funchook: CAccent::_UpdateSolidFillFunc\n"
                    );
                    FreeLibraryAndExitThread(
                        hModule,
                        rv
                    );
                    return rv;
                }

                CAccent_GetShadowMarginsFunc = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[13]
                    );
                rv = funchook_prepare(
                    funchook,
                    (void**)&CAccent_GetShadowMarginsFunc,
                    CAccent_GetShadowMarginsHook
                );
                if (rv != 0)
                {
                    TRACE(
                        "%s",
                        "[ERROR!] funchook: CAccent::_UpdateSolidFillFunc\n"
                    );
                    FreeLibraryAndExitThread(
                        hModule,
                        rv
                    );
                    return rv;
                }

                ResourceHelperCreateRectangleGeometryFunc = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[14]
                    );
                rv = funchook_prepare(
                    funchook,
                    (void**)&ResourceHelperCreateRectangleGeometryFunc,
                    ResourceHelperCreateRectangleGeometryHook
                );
                if (rv != 0)
                {
                    TRACE(
                        "%s",
                        "[ERROR!] funchook: CAccent::_UpdateSolidFillFunc\n"
                    );
                    FreeLibraryAndExitThread(
                        hModule,
                        rv
                    );
                    return rv;
                }                
            }
            else if (symbolNames == symbolNamesWindows8)
            {
                CTextGetLeftOffsetFunc = (int64_t(*)(void*))(
                    (uintptr_t)hudwm +
                    (uintptr_t)addresses[2]
                    );
                rv = funchook_prepare(
                    funchook,
                    (void**)&CTextGetLeftOffsetFunc,
                    CTextGetLeftOffsetHook
                );
                if (rv != 0)
                {
                    TRACE(
                        "%s",
                        "[ERROR!] funchook: CText::GetLeftOffset\n"
                    );
                    FreeLibraryAndExitThread(
                        hModule,
                        rv
                    );
                    return rv;
                }
            }
            else if (symbolNames == symbolNamesWindows7)
            {

            }
        }

        rv = funchook_install(
            funchook, 
            0
        );
        if (rv != 0)
        {
            TRACE(
                "%s",
                "[ERROR!] funchook: install\n"
            );
            FreeLibraryAndExitThread(
                hModule, 
                rv
            );
            return rv;
        }
    }
    else
    {
        rv = funchook_uninstall(
            funchook, 
            0
        );
        if (rv != 0)
        {
            TRACE(
                "%s",
                "[ERROR!] funchook: uninstall\n"
            );
            FreeLibraryAndExitThread(
                hModule, 
                rv
            );
            return rv;
        }

        rv = funchook_destroy(funchook);
        if (rv != 0)
        {
            TRACE(
                "%s",
                "[ERROR!] funchook: destroy\n"
            );
            FreeLibraryAndExitThread(
                hModule, 
                rv
            );
            return rv;
        }

        if (titlebar_color != NULL)
        {
            *titlebar_color = old_titlebar_color;
        }

        FreeLibraryAndExitThread(
            hModule, 
            0
        );
    }

    return 0;
}

BOOL WINAPI DllMain(
    _In_ HINSTANCE hinstDLL,
    _In_ DWORD     fdwReason,
    _In_ LPVOID    lpvReserved
)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        hModule = hinstDLL;
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}