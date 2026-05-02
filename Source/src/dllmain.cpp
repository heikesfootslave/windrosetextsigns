#include <WindroseTextSigns/SignTextMod.hpp>

#define WINDROSE_TEXT_SIGNS_API __declspec(dllexport)

extern "C"
{
    WINDROSE_TEXT_SIGNS_API RC::CppUserModBase* start_mod()
    {
        return new WindroseTextSigns::SignTextMod();
    }

    WINDROSE_TEXT_SIGNS_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
