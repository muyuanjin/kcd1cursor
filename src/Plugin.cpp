#include <windows.h>
#include <MinHook.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
constexpr DWORD kImageTimestamp = 0x69ccd815;
constexpr DWORD kImageSize = 0x039eb000;
constexpr std::size_t kCompleteInitRva = 0x0186ddc4;
constexpr std::size_t kSetCursorRva = 0x004fff48;
constexpr std::size_t kSetStringRva = 0x00500068;
constexpr std::size_t kScriptSystemVtableRva = 0x027d2488;
constexpr std::size_t kMouseVtableRva = 0x0267fed0;
constexpr std::size_t kConsoleRva = 0x029d1768;
constexpr std::size_t kRendererRva = 0x029d17c0;
constexpr std::size_t kMouseRva = 0x029d17c8;
constexpr std::size_t kLuaToStringRva = 0x002b7ee4;
constexpr std::size_t kAssignStringRva = 0x0028f7cc;
constexpr std::size_t kReleaseAnyRva = 0x003b9e68;
constexpr std::size_t kCreateSystemCursorRva = 0x00e24f50;

using CompleteInit = bool(*)(void*);
using SetCursor = bool(*)(void*, const char*, float, float);
using SetString = void(*)(void*, const char*);
std::byte* g_game;
std::filesystem::path g_log;
CompleteInit g_completeInit;
SetCursor g_setCursor;
SetString g_setString;
void* g_cursorCvar;
std::recursive_mutex g_cursorMutex;
std::unordered_set<std::string> g_declared;
std::unordered_map<std::string, void*> g_textures;
std::string g_lastPath;
std::string g_lockedPath;
thread_local bool g_syncingCvar;

void log_message(const char* message, const char* detail = "")
{
    FILE* file = nullptr;
    if (!_wfopen_s(&file, g_log.c_str(), L"a"))
    {
        std::fprintf(file, "[kcd1cursor] %s%s%s\n", message, *detail ? ": " : "", detail);
        std::fclose(file);
    }
}

template<class T>
T& field(void* object, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
}

template<class Fn>
Fn method(void* object, std::size_t offset)
{
    return field<Fn>(field<void*>(object, 0), offset);
}

template<class Fn>
Fn game_function(std::size_t rva)
{
    return reinterpret_cast<Fn>(g_game + rva);
}

std::string normal_path(std::string_view path)
{
    std::string result;
    result.reserve(path.size());
    for (char ch : path)
    {
        if (ch == '\\')
            ch = '/';
        if (ch == '/' && (result.empty() || result.back() == '/'))
            continue;
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch + ('a' - 'A'));
        result.push_back(ch);
    }
    return result;
}

bool is_fallback(std::string_view path)
{
    if (path.starts_with("%engine%/"))
        path.remove_prefix(9);
    return path == "engineassets/textures/cursor_green.tif" ||
        path == "engineassets/textures/cursor_green.dds" ||
        path == "textures/cursor_green.dds";
}

bool is_mod_path(const std::string& path)
{
    return !path.empty() && (path.starts_with("data/textures/cursor") ||
        path.starts_with("textures/cursor") || g_declared.contains(path)) &&
        !is_fallback(path);
}

const std::string& preferred_path()
{
    return g_lockedPath.empty() ? g_lastPath : g_lockedPath;
}

bool is_hardware_cursor(void* mouse)
{
    return field<bool>(mouse, 0x78);
}

bool apply_texture(void* mouse, const char* path, const std::string& key, float x, float y)
{
    if (is_hardware_cursor(mouse) || field<int>(mouse, 0x88) != 0)
        return false;

    auto* renderer = field<void*>(g_game, kRendererRva);
    if (!renderer)
        return false;

    const auto [entry, inserted] = g_textures.try_emplace(key, nullptr);
    if (inserted)
    {
        auto* texture = method<void*(*)(void*, const char*, unsigned)>(renderer, 0x470)(
            renderer, path, 0x54000);
        if (!texture)
        {
            g_textures.erase(entry);
            return false;
        }
        method<void(*)(void*, bool)>(texture, 0xa8)(texture, true);
        entry->second = texture;
        log_message("Cached cursor texture", path);
    }

    auto* texture = entry->second;
    game_function<void(*)(void*, const char*)>(kAssignStringRva)(
        static_cast<std::byte*>(mouse) + 0x70, path);
    field<float>(mouse, 0x68) = x;
    field<float>(mouse, 0x6c) = y;
    auto& activeTexture = field<void*>(mouse, 0x28);
    if (activeTexture != texture)
    {
        method<int(*)(void*)>(texture, 0x08)(texture);
        auto* previous = activeTexture;
        activeTexture = texture;
        if (previous)
            method<int(*)(void*)>(previous, 0x10)(previous);
    }
    return true;
}

bool apply_system_cursor(void* mouse, const char* path, float x, float y)
{
    auto& activeCursor = field<HCURSOR>(mouse, 0x80);
    if (activeCursor && field<int>(mouse, 0x88) == 0 &&
        std::strcmp(field<const char*>(mouse, 0x70), path) == 0 &&
        field<float>(mouse, 0x68) == x && field<float>(mouse, 0x6c) == y)
    {
        ::SetCursor(activeCursor);
        return true;
    }

    // Load first so a missing frame leaves the current cursor active.
    const auto create = game_function<HCURSOR(*)(void*, const char*, int, float, float)>(kCreateSystemCursorRva);
    const auto cursor = create(field<void*>(g_game, kRendererRva), path, 0, x, y);
    if (!cursor)
        return false;

    const auto previous = std::exchange(activeCursor, cursor);
    ::SetCursor(cursor);
    if (previous && field<int>(mouse, 0x88) == 0)
        DestroyCursor(previous);

    game_function<void(*)(void*, const char*)>(kAssignStringRva)(static_cast<std::byte*>(mouse) + 0x70, path);
    field<float>(mouse, 0x68) = x;
    field<float>(mouse, 0x6c) = y;
    field<int>(mouse, 0x88) = 0;
    return true;
}

void sync_cvar(const char* path)
{
    if (!g_cursorCvar)
        return;
    // The engine's change callback calls SetCursor synchronously.
    g_syncingCvar = true;
    g_setString(g_cursorCvar, path);
    g_syncingCvar = false;
}

bool apply_cursor(void* mouse, const char* path, const std::string& key, float x, float y)
{
    const bool applied = is_hardware_cursor(mouse)
        ? apply_system_cursor(mouse, path, x, y)
        : apply_texture(mouse, path, key, x, y);
    if (!applied)
    {
        log_message("Could not load cursor texture", path);
        return false;
    }

    game_function<void(*)(void*, const char*)>(kAssignStringRva)(static_cast<std::byte*>(mouse) + 0x60, path);
    sync_cvar(path);
    g_lastPath = path;
    return true;
}

bool on_set_cursor(void* mouse, const char* path, float x, float y)
{
    if (g_syncingCvar)
        return true;
    if (!path || !*path)
        return g_setCursor(mouse, path, x, y);

    const char* const originalPath = path;
    try
    {
        const std::lock_guard lock(g_cursorMutex);
        auto key = normal_path(path);
        std::string replacement;
        if (is_fallback(key) && !preferred_path().empty())
        {
            replacement = preferred_path();
            path = replacement.c_str();
            key = normal_path(path);
        }
        if (is_mod_path(key))
            return apply_cursor(mouse, path, key, x, y);
    }
    catch (const std::exception& error)
    {
        log_message("SetCursor failed", error.what());
    }
    return g_setCursor(mouse, originalPath, x, y);
}

void on_set_string(void* cvar, const char* value)
{
    if (cvar != g_cursorCvar || !value)
    {
        g_setString(cvar, value);
        return;
    }
    try
    {
        const std::lock_guard lock(g_cursorMutex);
        if (is_fallback(normal_path(value)) && !preferred_path().empty())
        {
            const auto path = preferred_path();
            g_setString(cvar, path.c_str());
            log_message("Substituted engine fallback", path.c_str());
            return;
        }
    }
    catch (const std::exception& error)
    {
        log_message("Cursor CVar update failed", error.what());
    }
    g_setString(cvar, value);
}

enum class AnyType : int { Boolean = 2, Table = 6 };

struct ScriptAny
{
    AnyType type;
    union
    {
        bool boolean;
        void* table;
        std::byte storage[16];
    };

    explicit ScriptAny(AnyType kind) : type(kind), storage{} {}
};

static_assert(sizeof(ScriptAny) == 24);

using Callback = int(*)(void*, void*, int);

struct FunctionDesc
{
    const char* name;
    const char* params;
    const char* global;
    std::byte functor[32]{};
    signed char paramOffset = 0;
    Callback callback;
    void* data = nullptr;
    int size = 0;
};

static_assert(offsetof(FunctionDesc, callback) == 0x40);

enum class Operation { Declare, Lock, Unlock };

template<Operation operation>
int cursor_api(void* handler, void*, int)
{
    ScriptAny result(AnyType::Boolean);
    try
    {
        const std::lock_guard lock(g_cursorMutex);
        if constexpr (operation == Operation::Unlock)
        {
            result.boolean = !g_lockedPath.empty();
            g_lockedPath.clear();
        }
        else
        {
            auto* script = field<void*>(handler, 0x10);
            auto* lua = field<void*>(script, 0x10);
            const int index = field<int>(handler, 0x20) + 1;
            std::size_t length = 0;
            const auto* path = game_function<const char*(*)(void*, int, std::size_t*)>(kLuaToStringRva)(
                lua, index, &length);
            if (path && length && !std::memchr(path, 0, length))
            {
                auto key = normal_path(path);
                if (!is_fallback(key))
                {
                    if constexpr (operation == Operation::Declare)
                    {
                        g_declared.insert(std::move(key));
                        result.boolean = true;
                    }
                    else
                    {
                        auto* mouse = field<void*>(g_game, kMouseRva);
                        if (mouse && field<void*>(mouse, 0) == g_game + kMouseVtableRva &&
                            apply_cursor(mouse, path, key, field<float>(mouse, 0x68), field<float>(mouse, 0x6c)))
                        {
                            g_declared.insert(std::move(key));
                            g_lockedPath = path;
                            result.boolean = true;
                            log_message("Locked cursor texture", path);
                        }
                    }
                }
            }
        }
    }
    catch (const std::exception& error)
    {
        log_message("Cursor Lua call failed", error.what());
    }
    return method<int(*)(void*, const ScriptAny&)>(handler, 0x50)(handler, result);
}

bool register_lua(void* script)
{
    auto* table = method<void*(*)(void*, bool)>(script, 0x68)(script, false);
    if (!table)
        return false;
    method<void(*)(void*)>(table, 0x18)(table);
    ScriptAny any(AnyType::Table);
    any.table = table;

    static constexpr FunctionDesc functions[] = {
        {"Declare", "path", "KCD1Cursor", {}, 0, cursor_api<Operation::Declare>},
        {"Lock", "path", "KCD1Cursor", {}, 0, cursor_api<Operation::Lock>},
        {"Unlock", "", "KCD1Cursor", {}, 0, cursor_api<Operation::Unlock>},
    };
    const bool registered = std::ranges::all_of(functions, [table](const FunctionDesc& function) {
        return method<bool(*)(void*, const FunctionDesc&)>(table, 0xb0)(table, function);
    });
    if (registered)
        method<void(*)(void*, const char*, const ScriptAny&)>(script, 0xf0)(script, "KCD1Cursor", any);

    game_function<void(*)(ScriptAny*)>(kReleaseAnyRva)(&any);
    return registered;
}

bool on_complete_init(void* framework)
{
    if (auto* console = field<void*>(g_game, kConsoleRva))
        g_cursorCvar = method<void*(*)(void*, const char*)>(console, 0xb8)(console, "r_MouseCursorTexture");

    auto* script = field<void*>(framework, 0x50);
    if (g_cursorCvar && script && field<void*>(script, 0) == g_game + kScriptSystemVtableRva &&
        register_lua(script))
        log_message("KCD1Cursor registered");
    else
        log_message("Cursor API registration failed");

    return g_completeInit(framework);
}

template<class Fn>
MH_STATUS create_hook(std::size_t rva, Fn hook, Fn& original)
{
    void* trampoline = nullptr;
    const auto status = MH_CreateHook(g_game + rva, reinterpret_cast<void*>(hook), &trampoline);
    original = reinterpret_cast<Fn>(trampoline);
    return status;
}

void install_hooks(HMODULE game)
{
    g_game = reinterpret_cast<std::byte*>(game);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_game + dos->e_lfanew);
    if (pe->FileHeader.TimeDateStamp != kImageTimestamp || pe->OptionalHeader.SizeOfImage != kImageSize)
    {
        log_message("Unsupported WHGame.dll");
        return;
    }

    if (const auto status = MH_Initialize(); status != MH_OK)
    {
        log_message("Could not initialize hooks", MH_StatusToString(status));
        return;
    }
    auto status = create_hook(kSetCursorRva, on_set_cursor, g_setCursor);
    if (status == MH_OK)
        status = create_hook(kSetStringRva, on_set_string, g_setString);
    if (status == MH_OK)
        status = create_hook(kCompleteInitRva, on_complete_init, g_completeInit);
    if (status == MH_OK)
        status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK)
    {
        MH_Uninitialize();
        log_message("Could not install hooks", MH_StatusToString(status));
        return;
    }
    log_message("Cursor hooks installed");
}

DWORD WINAPI wait_for_game(void*)
{
    for (int attempt = 0; attempt < 600; ++attempt)
    {
        if (const auto game = GetModuleHandleW(L"WHGame.dll"))
        {
            install_hooks(game);
            return 0;
        }
        Sleep(10);
    }
    log_message("WHGame.dll did not load");
    return 0;
}
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        wchar_t filename[MAX_PATH]{};
        const auto length = GetModuleFileNameW(nullptr, filename, MAX_PATH);
        if (length && length < MAX_PATH)
        {
            const std::filesystem::path exe(filename);
            if (_wcsicmp(exe.filename().c_str(), L"KingdomCome.exe") == 0)
            {
                g_log = exe.parent_path().parent_path().parent_path() / L"kcd1cursor.log";
                if (const auto thread = CreateThread(nullptr, 0, wait_for_game, nullptr, 0, nullptr))
                    CloseHandle(thread);
            }
        }
    }
    return TRUE;
}
