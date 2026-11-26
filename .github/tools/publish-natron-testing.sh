#!/bin/bash
# Publish Natron's testing bundles (README.md's "Natron's testing bundles"; natron-testing.yml):
# the testing artifacts of a ci.yml run of master become the assets of the natron_testing release
# (a prerelease, created at the run's commit if it doesn't exist; its tag is never moved):
#   artifact openfx-misc-build-<PLATFORM>-testing  ->  asset openfx-misc-build-<PLATFORM>-testing.zip
# for PLATFORM ubuntu_24 and windows_ucrt64. Each zip holds Misc.ofx.bundle, CImg.ofx.bundle,
# Shadertoy.ofx.bundle and openfx-misc-testing/ (README.txt with the commit, and the licenses) at
# its root. A run that lacks one of the artifacts (its job failed or was cancelled before the
# upload) leaves that asset unchanged, so the two assets may come from different commits. The new
# zip is uploaded under a temporary name (new-<run>-<asset>), then the old asset is deleted and the
# new one renamed: the asset is missing for one API call (delete, then rename), not for the whole
# upload. Its label gives the commit and the run. Leftover new-*-<asset> files of an interrupted
# run are deleted by the next one.
#
# Usage: .github/tools/publish-natron-testing.sh [RUN_ID]
#   RUN_ID: a completed (not skipped) ci.yml run of master (a push or a manual run) in this
#   repository; by default, the latest one that has testing artifacts. A failing gh call fails.
# Needs gh, with GH_TOKEN (contents: write, actions: read) and GITHUB_REPOSITORY (owner/repo).
set -euo pipefail

tag=natron_testing
platforms='ubuntu_24 windows_ucrt64'
repo=${GITHUB_REPOSITORY:?GITHUB_REPOSITORY (owner/repo) is not set}
[ $# -le 1 ] || { echo "usage: $0 [RUN_ID]"; exit 2; }
run=${1:-}

# testing_artifacts RUN: the names of the unexpired testing artifacts of the run
testing_artifacts() {
    gh api --paginate "repos/$repo/actions/runs/$1/artifacts" \
        --jq '.artifacts[] | select(.expired | not) | .name | select(test("^openfx-misc-build-.*-testing$"))'
}

if [ -z "$run" ]; then
    # (assignments, not "for r in $(...)" or "[ -n "$(...)" ]", so that a failing gh fails)
    runs=$(gh run list -R "$repo" --workflow ci.yml --branch master --status completed --limit 30 \
               --json databaseId,event,conclusion \
               --jq '.[] | select(.event == "push" or .event == "workflow_dispatch") | select(.conclusion != "skipped") | .databaseId')
    for r in $runs; do
        a=$(testing_artifacts "$r")
        if [ -n "$a" ]; then
            run=$r
            break
        fi
    done
    [ -n "$run" ] || { echo "ERROR: no recent ci.yml run of master has testing artifacts"; exit 1; }
fi
case "$run" in ''|*[!0-9]*) echo "ERROR: bad run id '$run'"; exit 2 ;; esac

# Only the runs of ci.yml on master of this repository, for a push or a manual run (never a pull
# request, another branch, a tag or a fork)
info=$(gh api "repos/$repo/actions/runs/$run" \
    --jq '[.path, .head_branch, .event, .head_repository.full_name, .status, (.conclusion // "none"), .head_sha] | join(" ")')
read -r path branch event head_repo status conclusion sha <<< "$info"
echo "run $run: $path $branch $event $head_repo $status $conclusion $sha"
if [ "$path" != .github/workflows/ci.yml ] || [ "$branch" != master ] || [ "$head_repo" != "$repo" ] ||
   { [ "$event" != push ] && [ "$event" != workflow_dispatch ]; } || [ "$status" != completed ]; then
    echo "ERROR: run $run is not a completed ci.yml run of $repo's master (push or manual run)"
    exit 1
fi
if [ "$conclusion" = skipped ]; then
    echo "run $run was skipped: nothing to publish"
    exit 0
fi
artifacts=$(testing_artifacts "$run")
if [ -z "$artifacts" ]; then
    echo "run $run has no testing artifacts: nothing to publish"
    exit 0
fi

if ! gh release view "$tag" -R "$repo" > /dev/null 2>&1; then
    gh release create "$tag" -R "$repo" --prerelease --latest=false --target "$sha" \
        --title 'Binaries for Natron automated testing' \
        --notes "Self-contained openfx-misc bundles (Misc, CImg and Shadertoy), for the tests of Natron's automated build (NatronGitHub/Natron's CI). Not a release: they are replaced after each change of master. Each zip holds the bundles at its root, and openfx-misc-testing/README.txt gives the commit they were built from.

The binaries are made by this project's [CI](https://github.com/$repo/actions/workflows/ci.yml) and published by [natron-testing.yml](https://github.com/$repo/actions/workflows/natron-testing.yml). See \"Natron's testing bundles\" in README.md."
fi
release_id=$(gh api "repos/$repo/releases/tags/$tag" --jq .id)

# asset_ids NAME: the ids of the release's assets named NAME
asset_ids() {
    gh api --paginate "repos/$repo/releases/$release_id/assets" --jq ".[] | select(.name == \"$1\") | .id"
}

# leftover_ids NAME: the ids of the temporary uploads of NAME (new-<run>-NAME), of any run
leftover_ids() {
    gh api --paginate "repos/$repo/releases/$release_id/assets" \
        --jq ".[] | select(.name | startswith(\"new-\") and endswith(\"-$1\")) | .id"
}

work=$(mktemp -d "${TMPDIR:-/tmp}/natron-testing.XXXXXX")
trap 'rm -rf "$work"' EXIT
date=$(date -u +%Y-%m-%d)
published=0
for p in $platforms; do
    name=openfx-misc-build-$p-testing
    if ! printf '%s\n' "$artifacts" | grep -qx "$name"; then
        echo "::warning::run $run has no artifact $name: $name.zip is not updated"
        continue
    fi
    gh run download "$run" -R "$repo" -n "$name" -D "$work/$p"
    for b in Misc CImg Shadertoy; do
        [ -d "$work/$p/$b.ofx.bundle" ] || { echo "ERROR: $name has no $b.ofx.bundle"; exit 1; }
    done
    [ -f "$work/$p/openfx-misc-testing/README.txt" ] || { echo "ERROR: $name has no openfx-misc-testing/README.txt"; exit 1; }
    tmpname=new-$run-$name.zip
    (cd "$work/$p" && zip -qr -X "$work/$tmpname" .)
    # the leftovers of interrupted runs (of any run)
    ids=$(leftover_ids "$name.zip")
    for id in $ids; do gh api -X DELETE "repos/$repo/releases/assets/$id"; done
    gh release upload "$tag" -R "$repo" "$work/$tmpname"
    new=$(asset_ids "$tmpname")
    [ -n "$new" ] || { echo "ERROR: $tmpname was not uploaded"; exit 1; }
    ids=$(asset_ids "$name.zip")
    for id in $ids; do gh api -X DELETE "repos/$repo/releases/assets/$id"; done
    if ! gh api -X PATCH "repos/$repo/releases/assets/$new" -f name="$name.zip" \
            -f label="$name.zip ($date, commit ${sha:0:7}, CI run $run)" > /dev/null; then
        echo "::error::renaming $tmpname to $name.zip failed: $name.zip is MISSING from the $tag release (run this workflow again)"
        exit 1
    fi
    echo "published $name.zip (commit $sha, CI run $run)"
    published=$((published + 1))
done
[ "$published" -gt 0 ] || { echo "ERROR: nothing published"; exit 1; }
