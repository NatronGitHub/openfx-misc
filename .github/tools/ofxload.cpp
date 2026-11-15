/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
 *
 * openfx-misc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * ofxload: minimal OpenFX plugin load test, used by the CI.
 *
 * Usage: ofxload <binary.ofx>...
 *
 * Each argument is the binary inside a bundle, e.g.
 * Misc.ofx.bundle/Contents/MacOS/Misc.ofx (Linux-x86-64 or Win64 on other systems).
 * Like a host, it loads each binary and calls the two entry points every OFX
 * plugin binary must export (see openfx/include/ofxCore.h), then prints the
 * identifier and version of each plugin. It fails if a binary cannot be loaded,
 * contains no plugin, or registers the same identifier+version twice, since a
 * host could then not tell these plugins apart.
 */

#include <cstdio>
#include <set>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "ofxCore.h"

typedef int (*GetNumberOfPluginsFunc)(void);
typedef OfxPlugin* (*GetPluginFunc)(int nth);

// Load a shared library and look up a symbol, using the native API of each OS.
#ifdef _WIN32
static void* loadLibrary(std::string path)
{
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '/') {
            path[i] = '\\'; // LoadLibrary wants backslashes
        }
    }
    return (void*)LoadLibraryA(path.c_str());
}
static void* findSymbol(void* lib, const char* name) { return (void*)GetProcAddress((HMODULE)lib, name); }
static std::string loadError() { std::ostringstream ss; ss << "error " << GetLastError(); return ss.str(); }
#else
static void* loadLibrary(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
static void* findSymbol(void* lib, const char* name) { return dlsym(lib, name); }
static std::string loadError() { const char* e = dlerror(); return e ? e : "unknown error"; }
#endif

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <binary.ofx>...\n", argv[0]);
        return 2;
    }
    int errors = 0;
    int plugins = 0;
    for (int a = 1; a < argc; ++a) {
        std::printf("%s\n", argv[a]);
        void* lib = loadLibrary(argv[a]);
        if (!lib) {
            std::printf("  ERROR: cannot load: %s\n", loadError().c_str());
            ++errors;
            continue;
        }
        GetNumberOfPluginsFunc getNumberOfPlugins = (GetNumberOfPluginsFunc)findSymbol(lib, "OfxGetNumberOfPlugins");
        GetPluginFunc getPlugin = (GetPluginFunc)findSymbol(lib, "OfxGetPlugin");
        if (!getNumberOfPlugins || !getPlugin) {
            std::printf("  ERROR: OfxGetNumberOfPlugins or OfxGetPlugin not exported\n");
            ++errors;
            continue;
        }
        // The spec allows 0 plugins for hosts that call OfxSetHost first, but
        // our bundles do not need it, so 0 means something is broken.
        int n = getNumberOfPlugins();
        if (n <= 0) {
            std::printf("  ERROR: no plugin\n");
            ++errors;
        }
        std::set<std::string> seen; // "identifier version" of each plugin in this binary
        for (int i = 0; i < n; ++i) {
            OfxPlugin* p = getPlugin(i);
            if (!p || !p->pluginIdentifier) {
                std::printf("  ERROR: plugin %d is NULL\n", i);
                ++errors;
                continue;
            }
            std::ostringstream key;
            key << p->pluginIdentifier << ' ' << p->pluginVersionMajor << '.' << p->pluginVersionMinor;
            bool duplicate = !seen.insert(key.str()).second;
            std::printf("  %s%s\n", key.str().c_str(), duplicate ? "  ERROR: duplicate" : "");
            errors += duplicate;
            ++plugins;
        }
        // The library is not unloaded: OFX hosts usually keep plugins loaded.
    }
    std::printf("%d binaries, %d plugins, %d errors\n", argc - 1, plugins, errors);
    return errors ? 1 : 0;
}
