#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <n.n.n>" >&2
  exit 1
fi

VERSION="$1"
if [[ ! "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Error: version must match n.n.n (for example 1.2.3)." >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

# 1) Build first so we don't create a version commit/tag if build fails.
pebble build

# 2) Bump version and let npm create both:
#    - commit message: CHORE: Increment version number to n.n.n
#    - git tag: vn.n.n
npm version "${VERSION}" -m "CHORE: Increment version number to ${VERSION}"
