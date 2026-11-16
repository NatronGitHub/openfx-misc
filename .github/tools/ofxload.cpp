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
 * contains no plugin, returns an incomplete OfxPlugin, or if the same
 * identifier+version is registered twice, in the same binary or in two of the
 * binaries given, since a host that loads them all could not tell them apart.
 *
 * This is a registration-only test: it does not call setHost or mainEntry, so
 * kOfxActionLoad and kOfxActionDescribe are never sent. It does not call
 * OfxSetHost either, which the OFX C++ Support library does not export.
 */

#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "ofxCore.h"
#include "ofxImageEffect.h"

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
static std::string loadError()
{
    DWORD code = GetLastError();
    char* msg = NULL;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, code, 0, (LPSTR)&msg, 0, NULL);
    std::ostringstream ss;
    ss << "error " << code;
    if (msg) {
        ss << ": " << msg; // ends with "\r\n"
        LocalFree(msg);
    }
    return ss.str();
}
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
#ifdef _WIN32
    // Report a missing DLL as a load error, instead of opening a dialog box
    // that would block the CI job.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
#endif
    int errors = 0;
    int plugins = 0;
    std::map<std::string, std::string> seen; // "identifier version" -> binary that registered it
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
        for (int i = 0; i < n; ++i) {
            OfxPlugin* p = getPlugin(i);
            if (!p) {
                std::printf("  ERROR: OfxGetPlugin(%d) returned NULL\n", i);
                ++errors;
                continue;
            }
            if (!p->pluginIdentifier) {
                std::printf("  ERROR: plugin %d has a NULL pluginIdentifier\n", i);
                ++errors;
                continue;
            }
            std::ostringstream key;
            key << p->pluginIdentifier << ' ' << p->pluginVersionMajor << '.' << p->pluginVersionMinor;
            std::printf("  %s\n", key.str().c_str());
            ++plugins;
            // All our plugins are image effects, and a host calls setHost and
            // mainEntry on every plugin it loads.
            if (!p->pluginApi || std::strcmp(p->pluginApi, kOfxImageEffectPluginApi) != 0) {
                std::printf("    ERROR: pluginApi is %s, not %s\n", p->pluginApi ? p->pluginApi : "NULL", kOfxImageEffectPluginApi);
                ++errors;
            }
            if (!p->setHost || !p->mainEntry) {
                std::printf("    ERROR: setHost or mainEntry is NULL\n");
                ++errors;
            }
            std::pair<std::map<std::string, std::string>::iterator, bool> ins = seen.insert(std::make_pair(key.str(), std::string(argv[a])));
            if (!ins.second) {
                std::printf("    ERROR: duplicate, also registered by %s\n", ins.first->second.c_str());
                ++errors;
            }
        }
        // The library is not unloaded: OFX hosts usually keep plugins loaded.
    }
    std::printf("%d binaries, %d plugins, %d errors\n", argc - 1, plugins, errors);
    return errors ? 1 : 0;
}
