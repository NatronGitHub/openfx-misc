"""pytest configuration of the DaVinci Resolve tests (see README.md)."""

import os

import pytest

import resolve_harness


def pytest_addoption(parser):
    """Adds --log and --results."""
    group = parser.getgroup("resolve", "DaVinci Resolve tests (tests/resolve)")
    group.addoption("--log", default=None,
                    help="Resolve's stdout with the script's results (default: "
                    "$OPENFX_MISC_RESOLVE_RESULTS/%s or "
                    "~/openfx-misc-resolve-results/%s)"
                    % (resolve_harness.STDOUT_LOG, resolve_harness.STDOUT_LOG))
    group.addoption("--results", default=None,
                    help="check this results directory (with results.json) "
                    "instead of the newest block of --log")


def pytest_configure(config):
    """Registers the resolve marker."""
    config.addinivalue_line(
        "markers", "resolve: checks results rendered in DaVinci Resolve")


def _log(config):
    return config.getoption("--log") or os.path.join(
        resolve_harness.config_dir(), resolve_harness.STDOUT_LOG)


def pytest_generate_tests(metafunc):
    """Parametrizes `case` with the case names of the results to check."""
    if "case" not in metafunc.fixturenames:
        return
    config = metafunc.config
    run = config.getoption("--results")
    if run and os.path.isfile(os.path.join(run, "results.json")):
        results = resolve_harness.load_results(run)
    else:
        results = resolve_harness.newest_results(_log(config))
    names = [c.get("name") for c in
             (results or {}).get("cases") or [] if isinstance(c, dict)]
    metafunc.parametrize("case", names or [None], ids=names or ["no-results"])


@pytest.fixture(scope="session")
def results_dir(request):
    """The results directory to check; skips if there is none."""
    run = request.config.getoption("--results")
    if run:
        if not os.path.isfile(os.path.join(run, "results.json")):
            pytest.skip("%s has no results.json" % run)
        return os.path.abspath(run)
    log = _log(request.config)
    run = resolve_harness.prepare_run(log)
    if run is None:
        pytest.skip("no openfx_misc_tests results in %s: run the script in "
                    "Resolve first (see tests/resolve/README.md)" % log)
    return run


@pytest.fixture(scope="session")
def results(results_dir):
    """The parsed results.json."""
    return resolve_harness.load_results(results_dir)
