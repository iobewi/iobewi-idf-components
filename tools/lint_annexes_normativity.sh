 #!/usr/bin/env bash
 set -euo pipefail
 
 ANNEXES_DIR="docs/annexes"
FORBIDDEN_PATTERNS=(
  "\\<doit\\>"
  "\\<doivent\\>"
  "\\<ne[[:space:]]+doit[[:space:]]+pas\\>"
  "\\<ne[[:space:]]+doivent[[:space:]]+pas\\>"
  "\\<obligatoire(s)?\\>"
  "\\<interdit(e|es|s)?\\>"
  "\\<strictement[[:space:]]+interdit(e|es|s)?\\>"
 )
 
 echo "========================================"
 echo "📘 Annexes Normativity Lint"
 echo "Directory : ${ANNEXES_DIR}"
 echo "========================================"
 
 FOUND=0
 
for pattern in "${FORBIDDEN_PATTERNS[@]}"; do
  echo "🔎 Checking: ${pattern}"
  if grep -RInEi "${pattern}" "${ANNEXES_DIR}" >/dev/null; then
    grep -RInEi "${pattern}" "${ANNEXES_DIR}"
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