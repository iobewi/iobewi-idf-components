#!/usr/bin/env bash
set -euo pipefail

ANNEXES_DIR="docs/annexes"
FORBIDDEN_WORDS=(
  "DOIT"
  "NE DOIT PAS"
  "OBLIGATOIRE"
  "INTERDIT"
  "STRICTEMENT INTERDIT"
)

echo "========================================"
echo "📘 Annexes Normativity Lint"
echo "Directory : ${ANNEXES_DIR}"
echo "========================================"

FOUND=0

for word in "${FORBIDDEN_WORDS[@]}"; do
  echo "🔎 Checking: ${word}"
  if grep -RIn "${word}" "${ANNEXES_DIR}" >/dev/null; then
    grep -RIn "${word}" "${ANNEXES_DIR}"
    FOUND=1
  fi
done

if [ "${FOUND}" -eq 1 ]; then
  echo
  echo "❌ Normative wording detected in annexes."
  echo "👉 Annexes must remain INFORMATIVE."
  echo "👉 Replace with:"
  echo "   - « conforme au standard STD-XXX »"
  echo "   - « exemple conforme »"
  echo "   - « attendu selon le standard »"
  exit 1
fi

echo "✅ Annexes are lexically non-normative."
