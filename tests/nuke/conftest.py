"""pytest configuration of the compatibility tests (see README.md)."""

import os
import sys

import pytest

sys.dont_write_bytecode = True  # no __pycache__ in the source tree

import harness  # noqa: E402 (after the line above)


def pytest_addoption(parser):
    """Adds --ofx-prefix, --nuke and --keep."""
    group = parser.getgroup("nuke", "compatibility tests (tests/nuke)")
    group.addoption("--ofx-prefix", default=os.environ.get("OFX_PREFIX"),
                    help="openfx-misc install prefix, with hostDemo in "
                    "PREFIX/bin (default: $OFX_PREFIX)")
    group.addoption("--nuke", default=os.environ.get("NUKE"),
                    help="the Nuke executable (default: $NUKE, or the "
                    "newest in /Applications)")
    group.addoption("--keep", default=None,
                    help="keep the work files in this directory")


def pytest_configure(config):
    """Registers the nuke marker."""
    config.addinivalue_line(
        "markers", "nuke: renders with Nuke and hostDemo (tests/nuke)")


class Renderer:
    """Runs the cases of one session (or of one pytest-xdist worker)."""

    def __init__(self, nuke, prefix, work):
        self.nuke = nuke
        self.prefix = prefix
        self.work = work

    def check(self, case):
        """Renders a case and asserts that both sides match.

        A plug-in that is not installed skips the test.
        """
        try:
            worst, where = harness.run_case(self.nuke, self.prefix, case,
                                            self.work)
        except harness.PluginMissing as ex:
            pytest.skip(str(ex))
        assert worst <= case["tolerance"], (
            "max diff %.3g > tolerance %.3g at %s"
            % (worst, case["tolerance"], where))


@pytest.fixture(scope="session")
def renderer(request, tmp_path_factory):
    """The session's Renderer; skips every test if it cannot run."""
    config = request.config
    prefix = config.getoption("--ofx-prefix")
    if not prefix:
        pytest.skip("no openfx-misc install: use --ofx-prefix or "
                    "$OFX_PREFIX")
    if not os.path.isdir(os.path.join(prefix, "OFX", "Plugins")):
        pytest.skip("%s/OFX/Plugins does not exist" % prefix)
    keep = config.getoption("--keep")
    worker = getattr(config, "workerinput", {}).get("workerid", "main")
    if keep:
        work = os.path.join(os.path.abspath(keep), worker)
        os.makedirs(work, exist_ok=True)
    else:
        work = str(tmp_path_factory.mktemp("nuke-" + worker))
    nuke = config.getoption("--nuke") or harness.find_nuke()
    try:
        harness.check_nuke(nuke, work)
    except harness.NukeUnavailable as ex:
        pytest.skip(str(ex))
    return Renderer(nuke, prefix, work)
