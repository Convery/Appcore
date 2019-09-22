/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2019-09-18
    License: MIT
*/

#include <Stdinclude.hpp>

namespace Global
{
    bool shouldReload{ false };
    bool isDirty{ true };
    uint32_t Errorno;

    // TODO(tcn): Move this somewhere.
    std::unordered_map<uint32_t, Callback_t> Callbacks;
}

namespace Attributes
{
    struct Background { uint32_t Colour, Border; std::string Image; };
}

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
    auto Windowhandle = CreateWindowExA(WS_EX_LAYERED | WS_EX_APPWINDOW | WS_EX_TOPMOST, "Desktop_cpp", NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL, Windowclass.hInstance, NULL);
    if(Windowhandle == 0) return nullptr;

    // Use a pixel-value of {0xFF, 0xFF, 0xFF} to mean transparent, because we should not use pure white anyway.
    if(FALSE == SetLayeredWindowAttributes(Windowhandle, 0xFFFFFF, 0, LWA_COLORKEY)) return nullptr;

    // Resize the window to the requested size.
    SetWindowPos(Windowhandle, NULL,
                 std::lround(Desktopcenterx - Windowsize.x * 0.5),
                 std::lround(Desktopcentery - Windowsize.y * 0.5),
                 Windowsize.x, Windowsize.y,
                 SWP_NOSENDCHANGING);
    ShowWindow(Windowhandle, SW_SHOWNORMAL);
    return Windowhandle;
}

// Get input and other such interrupts.
inline bool Hittest(point2_t Point, vec4_t Area)
{
    return Point.x >= Area.x0 && Point.x <= Area.x1 && Point.y >= Area.y0 && Point.y <= Area.y1;
}
void Processmessages(const void *Windowhandle, Array<Element_t, UINT8_MAX> &Nodetree, Array<Callback_t, UINT8_MAX> &Callbacks)
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
            Array<uint8_t, UINT8_MAX> Hit, Miss;

            // Recursive check of the nodes following the root.
            std::function<void(uint8_t, bool)> Lambda = [&](uint8_t Piviot, bool Missed) -> void
            {
                const auto &This = Nodetree[Piviot];
                if(!Missed) Missed = !Hittest(Mouse, This.Area);

                if(This.Child_1) Lambda(This.Child_1, Missed);
                if(This.Child_2) Lambda(This.Child_2, Missed);
                if(This.Child_3) Lambda(This.Child_3, Missed);
                if(This.Child_4) Lambda(This.Child_4, Missed);

                if(Missed) Miss.add(Piviot);
                else Hit.add(Piviot);
            };
            Lambda(0, false);

            // Clear the state of missed elements.
            for(size_t i = 0; i < Miss.Size; ++i)
            {
                auto &Node = Nodetree[Miss[i]];

                if(Node.State.isHoveredover)
                {
                    auto Copy = Node.State;
                    Copy.isHoveredover = false;
                    Copy.isLeftclicked &= Event.message == WM_LBUTTONUP;
                    Copy.isRightclicked &= Event.message == WM_RBUTTONUP;
                    Copy.isMiddleclicked &= Event.message == WM_MBUTTONUP;
                    if(Node.onState) Callbacks[Node.onState](Node, &Copy);

                    Node.State = Copy;
                }
            }

            // Notify the set elements.
            for(size_t i = 0; i < Hit.Size; ++i)
            {
                auto &Node = Nodetree[Hit[i]];
                auto Copy = Node.State;

                Copy.isHoveredover = true;
                Copy.isLeftclicked |= Event.message == WM_LBUTTONDOWN;
                Copy.isRightclicked |= Event.message == WM_RBUTTONDOWN;
                Copy.isMiddleclicked |= Event.message == WM_MBUTTONDOWN;
                if(Node.onState) Callbacks[Node.onState](Node, &Copy);

                Node.State = Copy;
            }

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

// Parse the markup into arrays.
bool Parseblueprint(vec4_t Boundingbox, std::string_view Filepath, Array<Element_t, UINT8_MAX> *Nodes,
                    Array<Class_t, UINT8_MAX> *Properties, Array<Callback_t, UINT8_MAX> *Callbacks)
{
    // Parse the XML document.
    pugi::xml_document Document;
    if(!Document.load_file(Filepath.data())) return false;

    // Ensure that the arrays are 'empty'.
    Nodes->Size = Properties->Size = 0;
    Callbacks->Size = 1;

    // Temporary storage for hashes.
    std::unordered_map<uint32_t, uint8_t> Classindex{};

    // Initialize the class system.
    for(const auto &Class : Document.children("Class"))
    {
        auto [Index, pClass] = Properties->add();
        Classindex[Hash::FNV1a_32(Class.attribute("Name").as_string())] = Index;

        const auto Size = Class.child("Size");
        pClass->insert_or_assign(Hash::FNV1a_32("Size"), vec2_t{
            Size.attribute("Width").as_float() / 100,
            Size.attribute("Height").as_float() / 100 });

        const auto Offset = Class.child("Offset");
        pClass->insert_or_assign(Hash::FNV1a_32("Offset"), vec2_t{
            Offset.attribute("Left").as_float() / 100,
            Offset.attribute("Top").as_float() / 100 });

        const auto Background = Class.child("Background");
        pClass->insert_or_assign(Hash::FNV1a_32("Background"), Attributes::Background{
                                     _byteswap_ulong(Background.attribute("Colour").as_uint()),
                                     _byteswap_ulong(Background.attribute("Border").as_uint()),
                                     Background.attribute("Image").as_string() });
    }

    // Build the node-tree recursively.
    std::function<uint8_t(const pugi::xml_node &)> Buildnode = [&](const pugi::xml_node &Node) -> uint8_t
    {
        auto [Index, Entry] = Nodes->add();
        Entry->StyleID = Classindex[Hash::FNV1a_32(Node.attribute("Class").as_string())];

        const auto Register = [&](auto Iterator) -> uint8_t
        {
            if(Iterator == Global::Callbacks.end()) return 0;

            // Skip the dummy function.
            for(uint32_t i = 1; i < Callbacks->Size; ++i)
            {
                // HACK(tcn): Apparently std::function::target() does not like lambdas..
                if(0 == std::memcmp(&(*Callbacks)[i], &Iterator->second, sizeof(Callback_t)))
                {
                    return i & 0xFF;
                }
            }

            auto [i, _] = Callbacks->add(Iterator->second);
            return i & 0xFF;
        };
        Entry->onFrame = Register(Global::Callbacks.find(Hash::FNV1a_32(Node.child_value("onFrame"))));
        Entry->onState = Register(Global::Callbacks.find(Hash::FNV1a_32(Node.child_value("onState"))));

        for(const auto &Child : Node.children("Node"))
        {
            if(!Entry->Child_1) { Entry->Child_1 = Buildnode(Child); continue; }
            if(!Entry->Child_2) { Entry->Child_2 = Buildnode(Child); continue; }
            if(!Entry->Child_3) { Entry->Child_3 = Buildnode(Child); continue; }
            if(!Entry->Child_4) { Entry->Child_4 = Buildnode(Child); continue; }

            assert(false);
        }

        return Index;
    };
    for(const auto &Node : Document.children("Node"))
    {
        Buildnode(Node);
    }

    // Calculate the dimensions of the items.
    std::function<void(uint8_t, vec4_t)> Initialize = [&](uint8_t Piviot, vec4_t Box)
    {
        auto This = &(*Nodes)[Piviot];
        const auto Width = Box.x1 - Box.x0;
        const auto Height = Box.y1 - Box.y0;
        const auto Attributes = (*Properties)[This->StyleID];
        const auto Size = Attributes.find(Hash::FNV1a_32("Size"));
        const auto Offset = Attributes.find(Hash::FNV1a_32("Offset"));

        This->Area = {};
        if(Size != Attributes.end())
        {
            This->Area.x1 = Width * std::any_cast<vec2_t>(Size->second).x;
            This->Area.y1 = Height * std::any_cast<vec2_t>(Size->second).y;
        }
        if(Offset != Attributes.end())
        {
            This->Area.x0 += Box.x0 + Width * std::any_cast<vec2_t>(Offset->second).x;
            This->Area.x1 += Box.x0 + Width * std::any_cast<vec2_t>(Offset->second).x;
            This->Area.y0 += Box.y0 + Height * std::any_cast<vec2_t>(Offset->second).y;
            This->Area.y1 += Box.y0 + Height * std::any_cast<vec2_t>(Offset->second).y;
        }

        if(This->Child_1) Initialize(This->Child_1, This->Area);
        if(This->Child_2) Initialize(This->Child_2, This->Area);
        if(This->Child_3) Initialize(This->Child_3, This->Area);
        if(This->Child_4) Initialize(This->Child_4, This->Area);
    };
    Initialize(0, Boundingbox);

    return true;
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

    // TODO(tcn): Move this somewhere..
    Global::Callbacks[Hash::FNV1a_32("Toolbar::onState")] = [&](Element_t &This, const void *Param) -> bool
    {
        auto Newstate = static_cast<const Elementstate_t *>(Param);

        if(Newstate->isLeftclicked)
        {
            POINT Point;
            ReleaseCapture();
            GetCursorPos(&Point);
            SendMessageA((HWND)Windowhandle, WM_NCLBUTTONDOWN, HTCAPTION, MAKEWPARAM(Point.x, Point.y));
        }

        return Newstate->isLeftclicked;
    };

    // Parse our markup.
    Array<Class_t, UINT8_MAX> Classes;
    Array<Element_t, UINT8_MAX> Nodetree;
    Array<Callback_t, UINT8_MAX> Callbacks;
    Parseblueprint({ 0.0f, 0.0f, float(Windowsize.x), float(Windowsize.y) },
                   "../Assets/Mainwindow.xml", &Nodetree, &Classes, &Callbacks);

    // Developer only.
    #if !defined(NDEBUG)
    std::thread([]()
    {
        FILETIME Blueprint{}, Style{};

        while(true)
        {
            if(auto Filehandle = CreateFileA("../Assets/Mainwindow.xml", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL))
            {
                FILETIME Localresult{};
                GetFileTime(Filehandle, NULL, NULL, &Localresult);

                if(*(uint64_t *)&Localresult != *(uint64_t *)&Blueprint)
                {
                    Blueprint = Localresult;
                    Global::shouldReload = true;
                }

                CloseHandle(Filehandle);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }).detach();
    #endif

    // Main loop.
    while(true)
    {
        // Track the frame-time, should be less than 33ms.
        static auto Lastframe{ std::chrono::high_resolution_clock::now() };
        const auto Thisframe{ std::chrono::high_resolution_clock::now() };

        // Process window-messages.
        Processmessages(Windowhandle, Nodetree, Callbacks);

        // And update the state as needed.
        const auto Deltatime = std::chrono::duration<float>(Thisframe - Lastframe).count();
        for(size_t i = 0; i < Nodetree.Size; ++i)
        {
            auto &Node = Nodetree[i];
            if(Node.onFrame) Callbacks[Node.onFrame](Node, (void *)&Deltatime);
        }

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

            // Render each of the nodes to the window.
            for(size_t i = 0; i < Nodetree.Size; ++i)
            {
                // Background attributes.
                const auto Background = std::any_cast<Attributes::Background>
                    (Classes[Nodetree[i].StyleID][Hash::FNV1a_32("Background")]);

                // Not allowed to be initialized without a colour.. Silly..
                static auto Brush{ new Gdiplus::SolidBrush(Gdiplus::Color()) };
                static auto Pen{ new Gdiplus::Pen(Gdiplus::Color()) };

                // Render order: Solid, Overlay, Outline.
                if(Background.Colour)
                {
                    Brush->SetColor(Background.Colour);
                    Drawingcontext->FillRectangle(Brush,
                                                  Nodetree[i].Area.x0,
                                                  Nodetree[i].Area.y0,
                                                  Nodetree[i].Area.x1 - Nodetree[i].Area.x0,
                                                  Nodetree[i].Area.y1 - Nodetree[i].Area.y0);
                }
                if(!Background.Image.empty())
                {
                    // TODO(tcn): Need a buffer system..
                }
                if(Background.Border)
                {
                    Pen->SetColor(Background.Border);

                    // HACK(tcn): Drawingcontext->DrawLines will allocate a separate buffer, so we draw each line directly.
                    Drawingcontext->DrawLine(Pen, Nodetree[i].Area.x0, Nodetree[i].Area.y0, Nodetree[i].Area.x1 - 1, Nodetree[i].Area.y0);
                    Drawingcontext->DrawLine(Pen, Nodetree[i].Area.x1 - 1, Nodetree[i].Area.y0, Nodetree[i].Area.x1 - 1, Nodetree[i].Area.y1 - 1);
                    Drawingcontext->DrawLine(Pen, Nodetree[i].Area.x1 - 1, Nodetree[i].Area.y1 - 1, Nodetree[i].Area.x0, Nodetree[i].Area.y1 - 1);
                    Drawingcontext->DrawLine(Pen, Nodetree[i].Area.x0, Nodetree[i].Area.y1 - 1, Nodetree[i].Area.x0, Nodetree[i].Area.y0);
                }
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
            Parseblueprint({ 0.0f, 0.0f, float(Windowsize.x), float(Windowsize.y) },
                           "../Assets/Mainwindow.xml", &Nodetree, &Classes, &Callbacks);
            Global::shouldReload = false;
            Global::isDirty = true;
        }

        // Sleep until the next frame.
        std::this_thread::sleep_until(Lastframe + std::chrono::milliseconds(1000 / 60));
        Lastframe = Thisframe;
    }

    // Check errors.

    return 0;
}
