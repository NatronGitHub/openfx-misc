"""pytest configuration of the compatibility tests (see README.md)."""

import os

import pytest

import harness


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
        self._knob_orders = {}  # Nuke node class -> knob names

    def knob_order(self, node_class, batch):
        """The knobs of a Nuke node in panel order.

        The first call for a class runs one Nuke process for the classes
        of its batch that are not known yet.

        Args:
            node_class: the Nuke node class.
            batch: the classes listed by the same Nuke process (at most
                about 8, with node_class).

        Returns:
            A list of knob names (see harness.KNOB_ORDER_SCRIPT).
        """
        if node_class not in self._knob_orders:
            todo = [c for c in batch if c not in self._knob_orders]
            self._knob_orders.update(
                harness.nuke_knob_orders(self.nuke, todo, self.work))
        return self._knob_orders[node_class]

    def param_order(self, plugin, context=None):
        """The visible params of a plug-in, in definition order.

        A plug-in that is not installed skips the test.

        Args:
            plugin: the OFX plug-in identifier.
            context: the OFX context, or None for the default.

        Returns:
            A list of param names (see harness.describe_params).
        """
        try:
            return harness.describe_params(self.prefix, plugin, self.work,
                                           context)
        except harness.PluginMissing as ex:
            pytest.skip(str(ex))


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
