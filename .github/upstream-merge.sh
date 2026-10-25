#! /bin/bash
# Opens a pull request that merges the newest Aseprite release tag
# into main, unless main has it already or a pull request for it
# exists. With --dry-run it prints the pull request instead.
#
# The pull request's head is the tag's commit, so merging it with a
# merge commit is the upstream merge. It lists every upstream workflow
# that would arrive enabled: the fork keeps upstream's workflows
# disabled, and a workflow file it doesn't know yet starts enabled.

set -euo pipefail

dry_run=
[ "${1:-}" = "--dry-run" ] && dry_run=1

upstream=https://github.com/aseprite/aseprite.git
repo=${GH_REPO:-nordtal/aseprite}

# Release tags only, no betas
tag=$(git ls-remote --tags --refs "$upstream" 'v*' |
          sed 's|.*refs/tags/||' |
          grep -E '^v[0-9]+(\.[0-9]+)+$' |
          sort -V | tail -1)
git fetch --quiet --no-tags "$upstream" "refs/tags/$tag:refs/tags/$tag"

if git merge-base --is-ancestor "$tag" HEAD ; then
    echo "main has $tag already"
    exit 0
fi

branch="upstream/$tag"
if git ls-remote --exit-code --heads origin "$branch" >/dev/null ; then
    echo "$branch exists already"
    exit 0
fi

disabled=$(gh api --paginate "repos/$repo/actions/workflows" \
               --jq '.workflows[] | select(.state != "active") | .path')
enabled=$(git ls-tree --name-only "$tag" .github/workflows/ |
              grep -E '\.ya?ml$' | grep -vxF -f <(echo "$disabled") || true)

title="build: aseprite $tag is merged"
body="Merges Aseprite $tag into main. Merge it with a merge commit, not a squash, so the next upstream merge starts from it."
if [ -n "$enabled" ] ; then
    body+="

These upstream workflows would arrive enabled, and those with a pull_request trigger already run on this pull request. Disable each one in the Actions tab right after the merge:
$(echo "$enabled" | sed 's/^/- /')"
fi

if [ -n "$dry_run" ] ; then
    printf 'branch: %s\ntitle: %s\n\n%s\n' "$branch" "$title" "$body"
    exit 0
fi

git push origin "refs/tags/$tag^{commit}:refs/heads/$branch"
gh pr create --repo "$repo" --base main --head "$branch" --title "$title" --body "$body"
