set_project("Chess")
set_version("0.1.0")
set_languages("c++20")
set_warnings("all")

add_rules("mode.debug", "mode.release")
set_defaultmode("release")

-- Use MSVC on Windows even when xmake is started from an MSYS/Git Bash shell.
if is_host("windows") then
    set_defaultplat("windows")
end

add_requires("sfml 3.0.1", "imgui-sfml v3.0", "nanosvg", "sqlite3")
add_requires("doctest")

if is_plat("macosx") then
    -- Build FreeType into the program instead of linking a Homebrew copy other Macs do not have.
    -- (Release builds for other Macs also lower the minimum macOS version; tools/package_release.py
    -- configures that with `xmake f --target_minver=11.0`.)
    add_requireconfs("**.freetype", {system = false})
end

if is_plat("windows") then
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "UNICODE", "_UNICODE")
    add_cxflags("/utf-8")
end

-- Chess rules. No rendering or UI dependencies.
target("chesscore")
    set_kind("static")
    add_files("src/chess/*.cpp")
    add_includedirs("src", {public = true})
    add_includedirs("third_party", {public = true})

-- Operating system services: child processes, user folders, file dialogs, shared libraries.
-- The only place with OS-specific code; one implementation folder is built per platform.
target("platform")
    set_kind("static")
    add_includedirs("src", {public = true})
    if is_plat("windows") then
        add_files("src/platform/windows/*.cpp")
        add_syslinks("comctl32", "comdlg32", {public = true})
    else
        add_files("src/platform/posix/*.cpp")
        if is_plat("macosx") then
            add_files("src/platform/macos/*.mm")
            add_frameworks("AppKit", "OpenGL", {public = true})
        end
    end

-- UCI engine communication. No rendering or UI dependencies.
target("engine")
    set_kind("static")
    add_files("src/engine/*.cpp")
    add_includedirs("src", {public = true})
    add_deps("platform")

-- Game database (SQLite). No rendering or UI dependencies.
target("storage")
    set_kind("static")
    add_files("src/storage/*.cpp")
    add_deps("chesscore")
    add_packages("sqlite3", {public = true})

target("chess")
    set_kind("binary")
    set_default(true)
    add_files("src/main.cpp", "src/app/*.cpp", "src/ui/*.cpp")
    add_deps("chesscore", "engine", "storage", "platform")
    add_packages("imgui-sfml", "sfml", "imgui", "nanosvg")
    if is_plat("windows") then
        add_ldflags("/SUBSYSTEM:WINDOWS", {force = true})
    elseif is_plat("macosx") then
        -- Static SFML keeps Objective-C category methods (e.g. -[SFOpenGLView enableKeyRepeat])
        -- in object files the linker would otherwise drop; -ObjC forces them in.
        add_ldflags("-ObjC", {force = true})
    end

target("tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/*.cpp")
    add_deps("chesscore", "engine", "storage", "platform")
    add_packages("doctest")
    add_tests("default")

-- Console check: starts a UCI engine and asks for a move.
target("ucicheck")
    set_kind("binary")
    set_default(false)
    add_files("tools/ucicheck.cpp")
    add_deps("engine")
