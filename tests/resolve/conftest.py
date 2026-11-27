"""pytest configuration of the DaVinci Resolve tests (see README.md)."""

import os

import pytest

import resolve_harness


def pytest_addoption(parser):
    """Adds --ofx-prefix, --resolve, --launch-resolve, --resolve-gui, --keep."""
    group = parser.getgroup("resolve", "DaVinci Resolve tests (tests/resolve)")
    group.addoption("--ofx-prefix", default=os.environ.get("OFX_PREFIX"),
                    help="openfx-misc install prefix, with PREFIX/OFX/Plugins "
                    "(default: $OFX_PREFIX)")
    group.addoption("--resolve", default=os.environ.get("RESOLVE"),
                    help="the DaVinci Resolve app or executable (default: "
                    "$RESOLVE, or %s)" % resolve_harness.DEFAULT_APP)
    group.addoption("--launch-resolve", action="store_true",
                    help="allow the tests to start Resolve, a GUI "
                    "application (without it, every test is skipped)")
    group.addoption("--resolve-gui", action="store_true",
                    help="start Resolve with its user interface (default: "
                    "-nogui)")
    group.addoption("--keep", default=None,
                    help="keep the log and images in this directory")


def pytest_configure(config):
    """Registers the resolve marker."""
    config.addinivalue_line(
        "markers", "resolve: renders with DaVinci Resolve (tests/resolve)")


@pytest.fixture(scope="session")
def resolve_session(request, tmp_path_factory):
    """A running Resolve with the install's plug-ins; skips if it can't run."""
    config = request.config
    prefix = config.getoption("--ofx-prefix")
    if not prefix:
        pytest.skip("no openfx-misc install: use --ofx-prefix or $OFX_PREFIX")
    prefix = os.path.abspath(prefix)
    if not os.path.isdir(os.path.join(prefix, "OFX", "Plugins")):
        pytest.skip("%s/OFX/Plugins does not exist" % prefix)
    try:
        executable = resolve_harness.find_executable(
            config.getoption("--resolve"))
        resolve_harness.load_script_module(executable)
    except resolve_harness.ResolveUnavailable as ex:
        pytest.skip(str(ex))
    if not config.getoption("--launch-resolve"):
        pytest.skip("starting Resolve (a GUI application) needs "
                    "--launch-resolve")
    keep = config.getoption("--keep")
    if keep:
        work = os.path.abspath(keep)
        os.makedirs(work, exist_ok=True)
    else:
        work = str(tmp_path_factory.mktemp("resolve"))
    session = resolve_harness.Session(
        executable, prefix, work, nogui=not config.getoption("--resolve-gui"))
    try:
        session.start()
    except resolve_harness.ResolveUnavailable as ex:
        pytest.skip(str(ex))
    yield session
    session.stop()
