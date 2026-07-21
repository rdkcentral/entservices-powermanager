#!/bin/sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${SCRIPT_DIR}"

OUT_DIR="${1:-/tmp/powermanager-coverage}"
GCDA_DIR="/tmp/gcov"
GCNO_DIRS="/usr/lib/gcov/entservices-powermanager"
WORK_DIR="${OUT_DIR}/gcov-work"

mkdir -p "${OUT_DIR}"

if [ ! -d "${GCDA_DIR}" ] || ! find "${GCDA_DIR}" -type f -name '*.gcda' | grep -q .; then
    echo "No gcda data found under ${GCDA_DIR}. Run tests first and restart wpeframework to flush coverage." >&2
    exit 1
fi

has_component_gcda() {
    find "${GCDA_DIR}" -type f -name '*.gcda' | grep -Eq '/entservices-powermanager/'
}

count_gcda() {
    tag="$1"
    find "${GCDA_DIR}" -type f -name '*.gcda' 2>/dev/null | grep -E "/${tag}/" | wc -l
}

if ! has_component_gcda; then
    echo "No PowerManager gcda files found under ${GCDA_DIR}." >&2
    exit 1
fi

PLUGIN_GCDA_COUNT="$(count_gcda entservices-powermanager)"
echo "Detected gcda counts: plugin=${PLUGIN_GCDA_COUNT}"

# Sync .gcno files alongside .gcda files so lcov can locate notes files
echo "Syncing .gcno files into gcda tree ..."
for d in ${GCNO_DIRS}; do
    if [ -d "${d}" ]; then
        ( cd "${d}" && find . -type f -name '*.gcno' | while IFS= read -r f; do
            dest="${GCDA_DIR}/${f#./}"
            mkdir -p "$(dirname "${dest}")"
            cp -f "${f}" "${dest}"
        done )
    fi
done

# Build a component-local gcov workspace to avoid processing stale/foreign gcda
# from other plugins (which causes notes/stamp mismatch warnings).
rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}"

find "${GCDA_DIR}" -type f \( -name '*.gcda' -o -name '*.gcno' \) | while IFS= read -r f; do
    case "${f}" in
        *"/entservices-powermanager/"*)
            rel="${f#${GCDA_DIR}/}"
            mkdir -p "${WORK_DIR}/$(dirname "${rel}")"
            cp -f "${f}" "${WORK_DIR}/${rel}"
            ;;
    esac
done

if ! find "${WORK_DIR}" -type f -name '*.gcda' | grep -q .; then
    echo "No PowerManager gcda files found under ${GCDA_DIR}. Run tests first." >&2
    exit 1
fi

BASE_INFO="${OUT_DIR}/base.info"
RUN_INFO="${OUT_DIR}/run.info"
TOTAL_INFO="${OUT_DIR}/coverage.info"
XML_FILE="${OUT_DIR}/coverage.xml"
PLUGIN_INFO="${OUT_DIR}/plugin.info"

lcov --gcov-tool /usr/bin/gcov --capture --initial --directory "${WORK_DIR}" --output-file "${BASE_INFO}" --ignore-errors source || true
lcov --gcov-tool /usr/bin/gcov --capture --directory "${WORK_DIR}" --output-file "${RUN_INFO}" --ignore-errors source

if ! grep -q '^SF:' "${RUN_INFO}"; then
    echo "No valid runtime coverage records found in ${RUN_INFO}." >&2
    echo "Hint: clear /tmp/gcov, rerun tests, then rerun this script." >&2
    exit 1
fi
lcov -a "${BASE_INFO}" -a "${RUN_INFO}" -o "${OUT_DIR}/combined.info" --ignore-errors source

# Filter to plugin source files
STRICT_TOTAL_INFO="${OUT_DIR}/coverage.strict.info"
FALLBACK_TOTAL_INFO="${OUT_DIR}/coverage.fallback.info"
MERGED_TOTAL_INFO="${OUT_DIR}/coverage.merged.info"

lcov --extract "${OUT_DIR}/combined.info" \
    '*/entservices-powermanager/*/git/plugin/*' \
    --output-file "${STRICT_TOTAL_INFO}" --ignore-errors source || true

lcov --extract "${OUT_DIR}/combined.info" \
    '*/plugin/*' \
    --output-file "${FALLBACK_TOTAL_INFO}" --ignore-errors source || true

if grep -q '^SF:' "${STRICT_TOTAL_INFO}"; then
    if grep -q '^SF:' "${FALLBACK_TOTAL_INFO}"; then
        lcov -a "${STRICT_TOTAL_INFO}" -a "${FALLBACK_TOTAL_INFO}" -o "${MERGED_TOTAL_INFO}" --ignore-errors source || true
        cp -f "${MERGED_TOTAL_INFO}" "${TOTAL_INFO}"
    else
        cp -f "${STRICT_TOTAL_INFO}" "${TOTAL_INFO}"
    fi
else
    cp -f "${FALLBACK_TOTAL_INFO}" "${TOTAL_INFO}"
fi

if ! grep -q '^SF:' "${TOTAL_INFO}"; then
    echo "No plugin coverage records found in ${OUT_DIR}/combined.info." >&2
    exit 1
fi

# Remap SF paths to local sources when debug sources are available in target.
HTML_INFO="${TOTAL_INFO}"
SOURCE_ROOT="$(find /usr/src/debug -type d -path '*/entservices-powermanager/*/git' 2>/dev/null | head -n 1)"
SF_PREFIX="$(grep '^SF:' "${TOTAL_INFO}" | head -n 1 | sed -e 's#^SF:##' -e 's#/plugin/.*##')"
if [ -n "${SOURCE_ROOT}" ] && [ -n "${SF_PREFIX}" ]; then
    HTML_INFO="${OUT_DIR}/coverage.html.info"
    sed "s|${SF_PREFIX}|${SOURCE_ROOT}|g" "${TOTAL_INFO}" | sed 's#/git/git/#/git/#g' > "${HTML_INFO}"
fi

# Normalize SF paths so genhtml shows a clean component name: Plugin.
# This only affects the display info used by genhtml; coverage.info is not changed.
DISPLAY_INFO="${OUT_DIR}/coverage.display.info"
sed \
    -e 's|SF:.*/plugin/|SF:Plugin/|g' \
    "${HTML_INFO}" > "${DISPLAY_INFO}"

genhtml "${DISPLAY_INFO}" --output-directory "${OUT_DIR}/html" --ignore-errors source

# Generate Cobertura XML for CI/reporting tools when gcovr is available.
if command -v gcovr >/dev/null 2>&1; then
    GCOVR_ROOT="${SOURCE_ROOT:-/}"
    if ! gcovr \
        --gcov-executable /usr/bin/gcov \
        --object-directory "${WORK_DIR}" \
        --root "${GCOVR_ROOT}" \
        --filter '.*/entservices-powermanager/.*/git/plugin/.*' \
        --xml-pretty \
        --output "${XML_FILE}"; then
        echo "Warning: gcovr failed to generate XML report." >&2
    fi
else
    echo "Warning: gcovr not found. Install python3-gcovr to generate XML report." >&2
fi

echo ""
echo "Coverage info (plugin): ${TOTAL_INFO}"
echo ""
echo "HTML report: ${OUT_DIR}/html/index.html"
echo ""
if [ -f "${XML_FILE}" ]; then
    echo "XML report: ${XML_FILE}"
    echo ""
fi

# Print per-component summary for quick tracking.
lcov --extract "${TOTAL_INFO}" '*/entservices-powermanager/*/git/plugin/*' \
    --output-file "${PLUGIN_INFO}" --ignore-errors source || true
if ! grep -q '^SF:' "${PLUGIN_INFO}"; then
    lcov --extract "${TOTAL_INFO}" '*/plugin/*' \
        --output-file "${PLUGIN_INFO}" --ignore-errors source || true
fi

echo "Component summary:"
if grep -q '^SF:' "${PLUGIN_INFO}"; then
    echo "- Plugin"
    lcov --summary "${PLUGIN_INFO}"
else
    echo "- Plugin: no data"
fi

echo "Combined summary:"
lcov --summary "${TOTAL_INFO}"
