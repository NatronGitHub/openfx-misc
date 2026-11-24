"""One compatibility test per case of cases.py.

A case that matches is a plain test. A known difference is an expected
failure (strict, so that a test that starts to match fails and the case
gets updated); its reason is the issue number and title.
"""

import pytest

import cases
import harness


def _param(case):
    """The pytest parameter of a case, with its marks."""
    marks = [pytest.mark.nuke]
    if case["known"]:
        marks.append(pytest.mark.xfail(
            strict=True, raises=AssertionError,
            reason="#%d: %s" % (case["known"], cases.ISSUES[case["known"]])))
    return pytest.param(case, id=harness.case_id(case), marks=marks)


@pytest.mark.parametrize("case", [_param(c) for c in cases.CASES])
def test_case(case, renderer):
    """Renders the case with Nuke and hostDemo; the pixels must match."""
    renderer.check(case)
