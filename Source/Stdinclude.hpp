/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2019-09-21
    License: MIT
*/

#pragma once

// Ignore warnings from external code.
#pragma warning(push, 0)

// Standard-library includes.
#include <unordered_map>
#include <functional>
#include <cassert>
#include <cstdint>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <array>
#include <any>

// Platform-library includes.
#define WIN32_LEAN_AND_MEAN
#include <ObjIdl.h>
#include <gdiplus.h>
#include <windows.h>
#include <windowsx.h>
#undef min
#undef max

// External-library includes.
#include <pugixml.hpp>

// Restore warnings.
#pragma warning(pop)

// Common-library includes.
#include <Utilities/FNV1Hash.hpp>

// Extensions to the language.
using namespace std::string_literals;

// Vertex and sub-pixel coordinate-system, could probably go down to half-precision floats.
using point4_t = struct { union {  struct { int16_t x0, y0, x1, y1; }; int16_t Raw[4]; }; };
using vec4_t = struct { union {  struct { float x0, y0, x1, y1; }; float Raw[4]; }; };
using point3_t = struct { union { struct { int16_t x, y, z; }; int16_t Raw[3]; }; };
using point2_t = struct { union { struct { int16_t x, y; }; int16_t Raw[2]; }; };
using vec3_t = struct { union { struct { float x, y, z; }; float Raw[3]; }; };
using vec2_t = struct { union { struct { float x, y; }; float Raw[2]; }; };

// Elements provide the core of the UI.
using Elementstate_t = union
{
    struct
    {
        uint8_t isHoveredover : 1;
        uint8_t isLeftclicked : 1;
        uint8_t isRightclicked : 1;
        uint8_t isMiddleclicked : 1;
    };
    uint8_t Raw;
};
struct Element_t
{
    // Region of the screen this element occupies.
    vec4_t Area{};

    // Packed element info.
    struct
    {
        // ID of this elements children.
        uint8_t Child_1;
        uint8_t Child_2;
        uint8_t Child_3;
        uint8_t Child_4;

        // Display information.
        Elementstate_t State;
        uint8_t StyleID;

        // Callbacks, see Callback_t.
        uint8_t onState;   // IN = Elementstate_t, RET = Consume event
        uint8_t onFrame;   // IN = float Delta-time
    };
};

constexpr size_t a = sizeof(Element_t);

// Classes are a set of attributes.
using Class_t = std::unordered_map<uint32_t, std::any>;
using Callback_t = std::function<bool(struct Element_t &This, const void *Argument)>;

// Simple class for tracking the used size.
template<typename T, uint32_t Maxsize>
struct Array
{
    std::array<T, Maxsize> Data{}; uint32_t Size{};
    T &operator[](uint32_t i) { return Data[i]; }
    std::pair<uint32_t, T *> add(T v = {})
    {
        assert(Size != Maxsize);
        auto k = &Data[Size]; *k = v;
        return { Size++, k };
    }
};

// Parse the markup into arrays.
bool Parseblueprint(vec4_t Boundingbox,
                    std::string_view Filepath,
                    Array<Element_t, UINT8_MAX> *Nodes,
                    Array<Class_t, UINT8_MAX> *Properties,
                    Array<Callback_t, UINT8_MAX> *Callbacks);
