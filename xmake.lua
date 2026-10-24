set_project("mcTitlePatch")
set_version("1.0.0")

set_languages("cxx23")

set_plat("android")
set_arch("arm64-v8a")

add_rules("mode.release")

add_cxflags(
    "-O2",
    "-fvisibility=hidden",
    "-ffunction-sections",
    "-fdata-sections",
    "-flto",
    "-w",
    "-fdeclspec",
    { force = true }
)

add_ldflags(
    "-Wl,--gc-sections",
    "-Wl,--strip-all",
    "-s"
)

add_repositories(
    "xmake-repo https://github.com/xmake-io/xmake-repo.git"
)

add_requires("libhat v0.5.0")

target("mcTitlePatch")
set_kind("shared")

add_packages("libhat")

add_files(
    "src/main.cpp"
)
add_linkdirs("lib/ARM64", "lib/ARM")

add_links(
    "GlossHook",
    "log",
    "android"
)
