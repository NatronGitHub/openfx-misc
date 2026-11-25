# shellcheck shell=bash
# Helpers shared by the scripts of .github/tools (sourced: . "$(dirname "$0")/lib.sh").

# os_arch: the Contents/<arch> folder of the OFX bundle binaries of this OS (the OFX spec's
# names, as openfx's cmake/OpenFX.cmake): MacOS, Linux-x86-64, Linux-x86, Linux-<uname -m>, or
# the glob Win* on Windows (Win64, Win-arm64ec, Win-arm64...). Fails on another OS.
os_arch() {
    case "$(uname -s)" in
        Darwin) echo MacOS ;;
        Linux)
            case "$(uname -m)" in
                x86_64|amd64) echo Linux-x86-64 ;;
                i[3-6]86) echo Linux-x86 ;;
                *) echo "Linux-$(uname -m)" ;;
            esac ;;
        # MSYS2: MINGW64_NT-*, UCRT64_NT-*, CLANG64_NT-*, MSYS_NT-*... (also Git for Windows' bash)
        *_NT-*) echo 'Win*' ;;
        *) return 1 ;;
    esac
}

# run_with_timeout SECONDS COMMAND [ARG...]: exec COMMAND, killed by SIGALRM (exit status 142)
# after SECONDS (perl's alarm survives exec). It replaces the calling shell: call it in a
# subshell, ( run_with_timeout ... ). Linux and macOS (needs perl).
run_with_timeout() {
    exec perl -e 'alarm shift; exec @ARGV or die "$!\n"' "$@"
}

# sha256_of FILE: the SHA256 of FILE (sha256sum, or macOS's shasum)
sha256_of() {
    if command -v sha256sum > /dev/null; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}
