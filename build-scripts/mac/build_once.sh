#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd "${REPO_ROOT}"

if [[ "$(uname -s)" != "Darwin" ]]; then
	echo "This script must be run on macOS."
	exit 1
fi

for tool in arch conan git make python3; do
	if ! command -v "${tool}" >/dev/null 2>&1; then
		echo "Missing required tool: ${tool}"
		exit 1
	fi
done

configure_contains_all() {
	python3 - "$@" <<'PY'
import pathlib
import sys

configure = pathlib.Path("configure").read_text()
sys.exit(0 if all(needle in configure for needle in sys.argv[1:]) else 1)
PY
}

apply_patch_once() {
	local patch_file="$1"
	shift

	if git apply --check "${patch_file}" >/dev/null 2>&1; then
		git apply "${patch_file}"
		return
	fi

	if git apply --reverse --check "${patch_file}" >/dev/null 2>&1; then
		echo "Patch already applied: ${patch_file}"
		return
	fi

	if [[ "$#" -gt 0 ]] && configure_contains_all "$@"; then
		echo "Patch already satisfied by current configure: ${patch_file}"
		return
	fi

	echo "Cannot apply patch cleanly: ${patch_file}"
	exit 1
}

apply_patch_once ./build-scripts/configure-zimg.patch \
	"check_lib libzimg zimg.h zimg_get_api_version -lzimg"
apply_patch_once ./build-scripts/configure-aom.patch \
	"check_lib libaom aom/aom_codec.h aom_codec_version" \
	"check_pkg_config libaom \"aom >= 2.0.0\""
apply_patch_once ./build-scripts/mac/configure-ossl.patch \
	"check_pkg_config openssl \"openssl >= 1.1.1\" openssl/ssl.h DTLS_get_data_mtu" \
	"check_lib openssl openssl/ssl.h DTLS_get_data_mtu -lssl -lcrypto"

if [[ "${BUILD_ONCE_SKIP_BUILD:-}" == "1" ]]; then
	echo "Skipping build because BUILD_ONCE_SKIP_BUILD=1."
	exit 0
fi

add_rpath_once() {
	local binary="$1"
	local rpath="$2"

	if otool -l "${binary}" | /usr/bin/grep -A2 LC_RPATH | /usr/bin/grep -q "path ${rpath} "; then
		return
	fi

	install_name_tool -add_rpath "${rpath}" "${binary}"
}

repair_output_rpaths() {
	local output_dir="$1"

	for binary in "${output_dir}"/bin/ff*; do
		if [[ -f "${binary}" ]]; then
			add_rpath_once "${binary}" "@executable_path/../lib"
		fi
	done

	for binary in "${output_dir}"/lib/ff*; do
		if [[ -f "${binary}" ]]; then
			add_rpath_once "${binary}" "@loader_path"
		fi
	done
}

BUILD_REASON=LocalBuild BUILD_MAC_EXTRA_CONFIGURE_FLAGS="--disable-bzlib --disable-lzma" arch -arm64 bash ./build-scripts/mac/build_mac.sh 1 ./builds-arm ./builds-x86 ./builds-univ '' ''

repair_output_rpaths ./builds-arm
repair_output_rpaths ./builds-x86
