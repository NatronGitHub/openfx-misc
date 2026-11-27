"""pytest configuration of the DaVinci Resolve tests (see README.md)."""

import os

import pytest

import openfx_misc_tests
import resolve_harness


def pytest_addoption(parser):
    """Adds --results."""
    group = parser.getgroup("resolve", "DaVinci Resolve tests (tests/resolve)")
    group.addoption("--results", default=None,
                    help="a results directory written by openfx_misc_tests.py "
                    "in Resolve, or a root of them (default: the newest under "
                    "$OPENFX_MISC_RESOLVE_RESULTS or %s)"
                    % openfx_misc_tests.DEFAULT_ROOT)


def pytest_configure(config):
    """Registers the resolve marker."""
    config.addinivalue_line(
        "markers", "resolve: checks results rendered in DaVinci Resolve")


@pytest.fixture(scope="session")
def results_dir(request):
    """The results directory to check; skips if there is none."""
    path = request.config.getoption("--results")
    if path and os.path.isfile(os.path.join(path, "results.json")):
        return os.path.abspath(path)
    root = path or openfx_misc_tests.results_root()
    run = resolve_harness.latest_results(root)
    if run is None:
        pytest.skip("no Resolve results under %s: run openfx_misc_tests in "
                    "Resolve first (see tests/resolve/README.md)" % root)
    return run


@pytest.fixture(scope="session")
def results(results_dir):
    """The parsed results.json."""
    return resolve_harness.load_results(results_dir)
