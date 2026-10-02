#!/usr/bin/env bash
# Builds a distributable Linux package of the game: the manager UI, the Unreal game
# target, cooked content and Chromium, archived under build/package/Linux and packed
# into build/package/ElyverseFootball-Linux.tar.gz.
#
# Usage: UE_ROOT=/path/to/UE_5.8 apps/unreal-game/package.sh [Shipping|Development]
set -euo pipefail

: "${UE_ROOT:?Set UE_ROOT to the Unreal Engine 5.8 installation}"
configuration="${1:-Shipping}"
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$project_dir/../.." && pwd)"
archive_dir="$repo_dir/build/package"

(cd "$repo_dir/apps/manager-ui" && pnpm install --frozen-lockfile && pnpm run build)

rm -rf "$archive_dir"
"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$project_dir/ElyverseFootball.uproject" \
  -platform=Linux -clientconfig="$configuration" \
  -build -cook -stage -pak -archive -archivedirectory="$archive_dir" \
  -nodebuginfo -noP4 -utf8output

# Epic ships Chromium's libcef.so with its debug info, which is 1.9 of its 2.3 GB.
objcopy="$(find "$UE_ROOT/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64" -name llvm-objcopy -path '*x86_64*' | head -n 1)"
find "$archive_dir/Linux" -name libcef.so -exec "$objcopy" --strip-debug {} \;

tar -czf "$archive_dir/ElyverseFootball-Linux.tar.gz" -C "$archive_dir/Linux" .
echo "Package: $archive_dir/Linux/ElyverseFootball.sh"
echo "Archive: $archive_dir/ElyverseFootball-Linux.tar.gz"
