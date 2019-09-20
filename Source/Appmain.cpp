/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2019-09-18
    License: MIT
*/

#define WIN32_LEAN_AND_MEAN
#pragma comment(lib, "gdiplus")

// Platform specific.
#include <ObjIdl.h>
#include <gdiplus.h>
#include <windows.h>
#include <windowsx.h>
#undef min
#undef max

// Third party.
#include <pugixml.hpp>

// Portable.
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <chrono>
#include <string>
#include <memory>
#include <thread>
#include <array>

// First party.
#include <Utilities/FNV1Hash.hpp>

namespace Global
{
    bool shouldReload{ false };
    bool isDirty{ true };
    uint32_t Errorno;
}
// Vertex and sub-pixel coordinate-system, could probably go down to half-precision floats.
using vec4_t = struct { union { struct { float x, y, z, w; }; struct { float x0, y0, x1, y1; }; float Raw[4]; }; };
using vec3_t = struct { union { struct { float x, y, z; }; float Raw[3]; }; };
using vec2_t = struct { union { struct { float x, y; }; float Raw[2]; }; };

// -||-
using point4_t = struct { union { struct { int16_t x, y, z, w; }; struct { int16_t x0, y0, x1, y1; }; int16_t Raw[4]; }; };
using point3_t = struct { union { struct { int16_t x, y, z; }; int16_t Raw[3]; }; };
using point2_t = struct { union { struct { int16_t x, y; }; int16_t Raw[2]; }; };

// In-flight representation, will be optimized out (probably), should still be aligned to 16 bytes.
using rgba_t = struct { union { struct { float R, G, B, A; }; float Raw[4]; }; };
using rgb_t = struct { union { struct { float R, G, B; }; float Raw[3]; }; };

// Create a centred window chroma-keyed on 0xFFFFFF.
void *Createwindow(point2_t Windowsize, RECT Desktop)
{
    // Register the window.
    WNDCLASSEXA Windowclass{};
    Windowclass.lpfnWndProc = DefWindowProc;
    Windowclass.cbSize = sizeof(WNDCLASSEXA);
    Windowclass.lpszClassName = "Desktop_cpp";
    Windowclass.hInstance = GetModuleHandleA(NULL);
    Windowclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    Windowclass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    if(NULL == RegisterClassExA(&Windowclass)) return nullptr;

    // Pre-calculate the window-offsets.
    const auto Desktopwidth = Desktop.right - Desktop.left;
    const auto Desktopheight = Desktop.bottom - Desktop.top;
    const auto Desktopcenterx = Desktop.left + Desktopwidth * 0.5f;
    const auto Desktopcentery = Desktop.top + Desktopheight * 0.5f;

    // HACK(tcn): We create the window with a size of 0 to prevent rendering of the first frame (black).
    auto Windowhandle = CreateWindowExA(WS_EX_LAYERED | WS_EX_APPWINDOW, "Desktop_cpp", NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL, Windowclass.hInstance, NULL);
    if(Windowhandle == 0) return nullptr;

    // Use a pixel-value of {0xFF, 0xFF, 0xFF} to mean transparent, because we should not use pure white anyway.
    if(FALSE == SetLayeredWindowAttributes(Windowhandle, 0xFFFFFF, 0, LWA_COLORKEY)) return nullptr;

    // Resize the window to the requested size.
    SetWindowPos(Windowhandle, NULL,
                 static_cast<int>(std::round(Desktopcenterx - Windowsize.x * 0.5)),
                 static_cast<int>(std::round(Desktopcentery - Windowsize.y * 0.5)),
                 Windowsize.x, Windowsize.y,
                 SWP_NOSENDCHANGING);
    ShowWindow(Windowhandle, SW_SHOWNORMAL);
    return Windowhandle;
}

union ChildID_t { uint32_t Raw; uint8_t ID[4]; };
namespace Attributes
{
    struct Offsets_t { float Top, Left; };
    struct Size_t { float Width, Height; };
    struct Background_t { uint32_t Colour; std::string Image; };
}
union Attribute_t
{
    Attributes::Background_t *Background;
    Attributes::Offsets_t *Offsets;
    Attributes::Size_t *Size;
    void *Raw;
};
struct Element_t
{
    vec4_t Area{};          // Recalculated on style-change.
    uint32_t ClassID{};     // Assigned by blueprint.
    ChildID_t Children{};   // -||-
};

using Class_t = std::unordered_map<uint32_t, Attribute_t>;
std::unordered_map<uint32_t, Class_t> Classes;

template<typename T, size_t Maxsize>
struct Array
{
    std::array<T, Maxsize> Data{}; size_t Size{};
    T &operator[](size_t i) { return Data[i]; }
    std::pair<size_t, T *> add(T v = {})
    {
        if(Size == Maxsize) return {};
        auto k = &Data[Size]; *k = v;
        return { Size++, k };
    }
};

// Initialize an array of elements.
Array<Element_t, UINT8_MAX> Createnodes(std::string_view Filepath)
{
    Array<Element_t, UINT8_MAX> Result{};
    pugi::xml_document Document;

    // Parse the XML document.
    if(!Document.load_file(Filepath.data()))
    {
        return Result;
    }

    // Build the tree recursively.
    std::function<uint8_t(const pugi::xml_node)> Lambda = [&](const pugi::xml_node Node) -> uint8_t
    {
        auto This = Result.add();
        This.second->ClassID = Hash::FNV1a_32(Node.attribute("Class").as_string());

        for(const auto Item : Node.children())
        {
            if(This.second->Children.ID[0] == 0) { This.second->Children.ID[0] = Lambda(Item); continue; }
            if(This.second->Children.ID[1] == 0) { This.second->Children.ID[1] = Lambda(Item); continue; }
            if(This.second->Children.ID[2] == 0) { This.second->Children.ID[2] = Lambda(Item); continue; }
            if(This.second->Children.ID[3] == 0) { This.second->Children.ID[3] = Lambda(Item); continue; }
        }

        return This.first;
    };
    Lambda(Document.first_child());

    return Result;
}

// Initialize / update the classes.
void Processclasses(std::string_view Filepath, Array<Element_t, UINT8_MAX> &Nodetree, vec4_t Boundingbox)
{
    pugi::xml_document Document;

    // Parse the XML document.
    if(!Document.load_file(Filepath.data()))
    {
        return;
    }

    // Classes only have a single level.
    for(const auto &Class : Document.children())
    {
        auto pClass = &Classes[Hash::FNV1a_32(Class.name())];
        const auto Background = Class.child("Background");
        const auto Offsets = Class.child("Offsets");
        const auto Size = Class.child("Size");

        if(!Background.empty())
        {
            auto This = &(*pClass)[Hash::FNV1a_32("Background")];
            if(!This->Raw) This->Raw = new Attributes::Background_t();

            auto Colour = Background.attribute("Colour");
            auto Image = Background.attribute("Image");

            if(!Colour.empty()) This->Background->Colour = _byteswap_ulong(Colour.as_uint());
            if(!Image.empty()) This->Background->Image = Image.as_string();
        }

        if(!Offsets.empty())
        {
            auto This = &(*pClass)[Hash::FNV1a_32("Offsets")];
            if(!This->Raw) This->Raw = new Attributes::Offsets_t();

            auto Left = Offsets.attribute("Left");
            auto Top = Offsets.attribute("Top");

            if(!Left.empty()) This->Offsets->Left = Left.as_float() / 100;
            if(!Top.empty()) This->Offsets->Top = Top.as_float() / 100;
        }

        if(!Size.empty())
        {
            auto This = &(*pClass)[Hash::FNV1a_32("Size")];
            if(!This->Raw) This->Raw = new Attributes::Size_t();

            auto Height = Size.attribute("Height");
            auto Width = Size.attribute("Width");

            if(!Height.empty()) This->Size->Height = Height.as_float() / 100;
            if(!Width.empty()) This->Size->Width = Width.as_float() / 100;
        }
    }

    // Update all items.
    std::function<void(uint8_t, vec4_t)> Lambda = [&](uint8_t Piviot, vec4_t Box)
    {
        auto This = &Nodetree[Piviot];

        if(This->ClassID)
        {
            This->Area = {};
            const auto Width = Box.x1 - Box.x0;
            const auto Height = Box.y1 - Box.y0;
            const auto Attributes = Classes[This->ClassID];
            const auto Size = Attributes.find(Hash::FNV1a_32("Size"));
            const auto Offsets = Attributes.find(Hash::FNV1a_32("Offsets"));

            if(Size != Attributes.end())
            {
                This->Area.x1 = Width * Size->second.Size->Width;
                This->Area.y1 = Height * Size->second.Size->Height;
            }

            if(Offsets != Attributes.end())
            {
                This->Area.x0 += Box.x0 + Width * Offsets->second.Offsets->Left;
                This->Area.x1 += Box.x0 + Width * Offsets->second.Offsets->Left;
                This->Area.y0 += Box.y0 + Height * Offsets->second.Offsets->Top;
                This->Area.y1 += Box.y0 + Height * Offsets->second.Offsets->Top;
            }
        }

        if(This->Children.Raw)
        {
            if(This->Children.ID[0]) Lambda(This->Children.ID[0], This->Area);
            if(This->Children.ID[1]) Lambda(This->Children.ID[1], This->Area);
            if(This->Children.ID[2]) Lambda(This->Children.ID[2], This->Area);
            if(This->Children.ID[3]) Lambda(This->Children.ID[3], This->Area);
        }
    };
    Lambda(0, Boundingbox);
}

// Get input and other such interrupts.
inline bool Hittest(point2_t Point, vec4_t Area)
{
    return Point.x >= Area.x0 && Point.x <= Area.x1 && Point.y >= Area.y0 && Point.y <= Area.y1;
}
void Processmessages(const void *Windowhandle, Array<Element_t, UINT8_MAX> &Nodetree)
{
    MSG Event{};

    // Non-blocking polling for messages.
    while(PeekMessageA(&Event, (HWND)Windowhandle, NULL, NULL, PM_REMOVE) > 0)
    {
        // Process mouse events first as they are the most common.
        if(Event.message >= WM_MOUSEFIRST && Event.message <= WM_MOUSELAST)
        {
            // Coordinates relative to the window.
            const point2_t Mouse{ GET_X_LPARAM(Event.lParam), GET_Y_LPARAM(Event.lParam) };
            Array<uint8_t, UINT8_MAX> Hits;

            // Recursive check of the nodes following the root.
            std::function<void(uint8_t)> Lambda = [&](uint8_t Piviot) -> void
            {
                const auto &This = Nodetree[Piviot];
                if(!Hittest(Mouse, This.Area)) return;

                Hits.add(Piviot);
                if(This.Children.Raw)
                {
                    if(This.Children.ID[0]) Lambda(This.Children.ID[0]);
                    if(This.Children.ID[1]) Lambda(This.Children.ID[1]);
                    if(This.Children.ID[2]) Lambda(This.Children.ID[2]);
                    if(This.Children.ID[3]) Lambda(This.Children.ID[3]);
                }
            };
            Lambda(0);

            // TODO(tcn): Check if a node wants this.

            continue;
        }

        // If Windows wants us to redraw, we oblige.
        if(Event.message == WM_PAINT) Global::isDirty = true;

        // If we should quit, break the loop without processing the rest of the queue.
        if(Event.message == WM_SYSCOMMAND && Event.wParam == SC_CLOSE)
        {
            Global::Errorno = 1;
            return;
        }

        // If we couldn't handle the event, let Windows do it.
        DispatchMessageA(&Event);
    }
}

// Entrypoint.
int __cdecl main(int, char **)
{
    RECT Desktoparea{};
    point2_t Windowsize{ 1280, 720 };

    // As we are single-threaded (in release), boost our priority.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    // Create the window based on our environment.
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &Desktoparea, 0);
    const auto Windowhandle = Createwindow(Windowsize, Desktoparea);

    // Initialize GDI with Windows's default parameters.
    Gdiplus::GdiplusStartupInput Input{}; ULONG_PTR T;
    Gdiplus::GdiplusStartup(&T, &Input, nullptr);

    // Parse the markup.
    auto Nodetree = Createnodes("../Assets/Mainwindow.xml");
    Processclasses("../Assets/Style.xml", Nodetree, { 0.0f, 0.0f,
                   static_cast<float>(Windowsize.x),
                   static_cast<float>(Windowsize.y)
                   });

    std::thread([]()
    {
        FILETIME Lastresult{};

        while(true)
        {
            if(auto Filehandle = CreateFileA("../Assets/Style.xml", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL))
            {
                FILETIME Localresult{};
                GetFileTime(Filehandle, NULL, NULL, &Localresult);

                if(*(uint64_t *)&Localresult != *(uint64_t *)&Lastresult)
                {
                    Lastresult = Localresult;
                    Global::shouldReload = true;
                }

                CloseHandle(Filehandle);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

    }).detach();

    // Main loop.
    while(true)
    {
        // Track the frame-time, should be less than 33ms.
        static auto Lastframe{ std::chrono::high_resolution_clock::now() };
        const auto Thisframe{ std::chrono::high_resolution_clock::now() };

        // Process window-messages.
        Processmessages(Windowhandle, Nodetree);

        // Render the previous frame.
        if(Global::isDirty)
        {
            // Notify Windows, the window needs repainting.
            InvalidateRect((HWND)Windowhandle, NULL, FALSE);

            // Grab a handle to the window.
            PAINTSTRUCT Updateinformation{};
            auto Devicecontext = BeginPaint((HWND)Windowhandle, &Updateinformation);
            auto Memorycontext = CreateCompatibleDC(Devicecontext);

            // Create a surface that we can draw to.
            auto Surface = CreateCompatibleBitmap(Devicecontext, Windowsize.x, Windowsize.y);
            auto Backup = (HBITMAP)SelectObject(Memorycontext, Surface);

            // Create a graphics object and clear the surface to white (chroma-key for transparent).
            auto Drawingcontext = std::make_unique<Gdiplus::Graphics>(Memorycontext);
            Drawingcontext->Clear(Gdiplus::Color::White);

            // DO render here
            for(size_t i = 0; i < Nodetree.Size; ++i)
            {
                // Not allowed to be initialized without a colour.. Silly..
                static auto Brush{ new Gdiplus::SolidBrush(Gdiplus::Color()) };
                const auto &Node = Nodetree[i];
                Brush->SetColor(Classes[Node.ClassID][Hash::FNV1a_32("Background")].Background->Colour);

                Drawingcontext->FillRectangle(Brush, Node.Area.x0, Node.Area.y0, Node.Area.x1 - Node.Area.x0, Node.Area.y1 - Node.Area.y0);
            }

            // Present to the window.
            BitBlt(Devicecontext, 0, 0, Windowsize.x, Windowsize.y, Memorycontext, 0, 0, SRCCOPY);

            // Restore the surface and do cleanup.
            SelectObject(Memorycontext, Backup);
            DeleteDC(Memorycontext);
            DeleteObject(Surface);

            // Notify Windows, we are done painting.
            EndPaint((HWND)Windowhandle, &Updateinformation);

            // This frame is cleeeean.
            Global::isDirty = false;
        }

        // Process any errors later.
        if(Global::Errorno) break;

        // Developer, reloading.
        if(Global::shouldReload)
        {
            Processclasses("../Assets/Style.xml", Nodetree, { 0.0f, 0.0f,
                   static_cast<float>(Windowsize.x),
                   static_cast<float>(Windowsize.y)
                   });

            Global::isDirty = true;
        }

        // Sleep until the next frame.
        std::this_thread::sleep_until(Lastframe + std::chrono::milliseconds(1000 / 60));
        Lastframe = Thisframe;
    }

    // Check errors.

    return 0;
}

