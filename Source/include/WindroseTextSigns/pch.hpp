#pragma once

// Keep this file stable. Frequent edits to PCH trigger broad recompiles.
// First-pass conservative PCH: common STL + UE4SS/Unreal core headers.
// Avoid Windows/socket headers here to prevent include-order macro issues.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <ios>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <UE4SSProgram.hpp>

#include <Unreal/AActor.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FClassProperty.hpp>
#include <Unreal/Property/FNameProperty.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FStrProperty.hpp>
#include <Unreal/Property/FTextProperty.hpp>
#include <Unreal/Transform.hpp>
#include <Unreal/UActorComponent.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UStruct.hpp>
#include <Unreal/UnrealCoreStructs.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/World.hpp>
