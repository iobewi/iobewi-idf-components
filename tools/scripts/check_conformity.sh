#!/bin/sh
# check_conformity.sh
# Script de vérification de conformité Standard iobewi-idf-components
# Version 3.1 - POSIX sh compatible (dash), aligné docs/standard.md
#
# Usage:
#   sh tools/scripts/check_conformity.sh [repo_root]
#
# Exit codes:
#   0 = OK (globalement acceptable)
#   1 = Non conforme (trop de violations)

set -eu

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# --- Paths ---
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=${1:-$(cd "$SCRIPT_DIR/../.." && pwd)}

if [ ! -d "$REPO_ROOT/components" ]; then
    printf "%b\n" "${RED}❌ Erreur: Dossier components/ non trouvé dans $REPO_ROOT${NC}"
    printf "Usage: %s [chemin_racine_projet]\n" "$0"
    exit 1
fi

if [ ! -d "$REPO_ROOT/examples" ]; then
    printf "%b\n" "${RED}❌ Erreur: Dossier examples/ non trouvé dans $REPO_ROOT${NC}"
    exit 1
fi

cd "$REPO_ROOT/components"

printf "%b\n" "${BLUE}=========================================${NC}"
printf "%b\n" "${BLUE}   Audit de Conformité — docs/standard.md${NC}"
printf "%b\n" "${BLUE}=========================================${NC}"
printf "%b\n" "${BLUE}   Répertoire: $REPO_ROOT${NC}"
printf "%b\n\n" "${BLUE}=========================================${NC}"

TOTAL=0
CONFORM=0
PARTIAL=0
NON_CONFORM=0

# ---------- Helpers ----------

kind_from_component_id() {
    cid="$1"
    case "$cid" in
        iobewi_driver_*) echo "driver" ;;
        iobewi_libs_*)   echo "library" ;;
        iobewi_mw_*)     echo "middleware" ;;
        iobewi_apps_*)   echo "application" ;;
        *)               echo "unknown" ;;
    esac
}

# STD-TAX-001/002 mapping folder -> API alias
get_api_name() {
    dir_name="$1"
    case "$dir_name" in
        iobewi_driver_*) echo "drv_${dir_name#iobewi_driver_}" ;;
        iobewi_libs_*)   echo "lib_${dir_name#iobewi_libs_}" ;;
        iobewi_mw_*)     echo "mw_${dir_name#iobewi_mw_}" ;;
        iobewi_apps_*)   echo "app_${dir_name#iobewi_apps_}" ;;
        *)               echo "$dir_name" ;;
    esac
}

# Extract iobewi_* deps from CMakeLists.txt (best-effort)
extract_iobewi_deps_from_cmake() {
    cmake_file="$1"
    [ -f "$cmake_file" ] || return 0

    # Take REQUIRES / PRIV_REQUIRES lines, strip comments, tokenize, keep iobewi_*
    # Note: best-effort; aims to be stable and deterministic.
    grep -E "REQUIRES|PRIV_REQUIRES" "$cmake_file" 2>/dev/null \
        | sed 's/#.*$//' \
        | tr '\t' ' ' \
        | tr -s ' ' \
        | tr '()' '  ' \
        | tr '\r' '\n' \
        | awk '{for(i=1;i<=NF;i++) print $i}' \
        | grep -E '^iobewi_(driver|libs|mw|apps)_' 2>/dev/null \
        | sort -u
}

deps_allowed() {
    comp_kind="$1" # driver|library|middleware|application
    dep_kind="$2"  # driver|library|middleware|application|unknown

    case "$comp_kind" in
        driver)
            # driver -> ESP-IDF only => no iobewi_* deps allowed
            return 1
            ;;
        library)
            [ "$dep_kind" = "driver" ] && return 0 || return 1
            ;;
        middleware)
            [ "$dep_kind" = "driver" ] || [ "$dep_kind" = "library" ]
            ;;
        application)
            [ "$dep_kind" = "driver" ] || [ "$dep_kind" = "library" ] || [ "$dep_kind" = "middleware" ]
            ;;
        *)
            return 1
            ;;
    esac
}

has_uros_in_public_headers() {
    include_dir="$1"
    [ -d "$include_dir" ] || return 1

    # Look for rcl/rclc includes in public headers (best-effort)
    grep -RIn --include="*.h" -E '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]rcl(c)?/|^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]rclc' \
        "$include_dir" >/dev/null 2>&1
}

list_public_esp_err_symbols() {
    include_dir="$1"
    [ -d "$include_dir" ] || return 0

    # Extract "esp_err_t foo(" -> "foo"
    grep -RIn --include="*.h" -E '^[[:space:]]*esp_err_t[[:space:]]+[A-Za-z0-9_]+[[:space:]]*\(' "$include_dir" 2>/dev/null \
        | sed -E 's/.*esp_err_t[[:space:]]+([A-Za-z0-9_]+)[[:space:]]*\(.*/\1/' \
        | sort -u
}

check_tests_structure() {
    test_dir="$1"
    [ -d "$test_dir" ] || return 1
    [ -f "$test_dir/CMakeLists.txt" ] || return 2

    n=$(find "$test_dir" -maxdepth 1 -type f -name "test_*.c" 2>/dev/null | wc -l | tr -d ' ')
    [ "$n" -ge 1 ] || return 3
    return 0
}

# ---------- Audit loop ----------

for comp_dir in iobewi_driver_* iobewi_libs_* iobewi_mw_* iobewi_apps_*; do
    [ -d "$comp_dir" ] || continue

    comp_api=$(get_api_name "$comp_dir")
    comp_kind=$(kind_from_component_id "$comp_dir")

    TOTAL=$((TOTAL + 1))
    SCORE=0
    MAX_SCORE=12

    printf "%b\n" "${BLUE}=== $comp_dir ===${NC}"
    printf "%b\n" "${BLUE}    kind: ${comp_kind} | api: ${comp_api}${NC}"

    # 1) Root required files
    if [ -f "${comp_dir}/CMakeLists.txt" ]; then
        printf "%b\n" "  ${GREEN}✅${NC} CMakeLists.txt"
        SCORE=$((SCORE + 1))
    else
        printf "%b\n" "  ${RED}❌${NC} MISSING: CMakeLists.txt"
    fi

    if [ -f "${comp_dir}/idf_component.yml" ]; then
        printf "%b\n" "  ${GREEN}✅${NC} idf_component.yml"
        SCORE=$((SCORE + 1))
    else
        printf "%b\n" "  ${RED}❌${NC} MISSING: idf_component.yml"
    fi

    if [ -f "${comp_dir}/README.md" ]; then
        printf "%b\n" "  ${GREEN}✅${NC} README.md"
        SCORE=$((SCORE + 1))
    else
        printf "%b\n" "  ${RED}❌${NC} MISSING: README.md"
    fi

    # 2) Namespace include + 2 public headers
    types_h="${comp_dir}/include/${comp_api}/${comp_api}_types.h"
    api_h="${comp_dir}/include/${comp_api}/${comp_api}.h"

    if [ -d "${comp_dir}/include/${comp_api}" ]; then
        printf "%b\n" "  ${GREEN}✅${NC} include/${comp_api}/"
        SCORE=$((SCORE + 1))
    else
        printf "%b\n" "  ${RED}❌${NC} MISSING: include/${comp_api}/"
    fi

    if [ -f "$types_h" ]; then
        printf "%b\n" "  ${GREEN}✅${NC} ${comp_api}_types.h"
        SCORE=$((SCORE + 1))
    else
        printf "%b\n" "  ${RED}❌${NC} MISSING: ${comp_api}_types.h"
    fi

    if [ -f "$api_h" ]; then
        printf "%b\n" "  ${GREEN}✅${NC} ${comp_api}.h"
        SCORE=$((SCORE + 1))
    else
        printf "%b\n" "  ${RED}❌${NC} MISSING: ${comp_api}.h"
    fi

    # 3) Exactly 2 public headers (no extras)
    if [ -d "${comp_dir}/include/${comp_api}" ]; then
        extra_headers=$(find "${comp_dir}/include/${comp_api}" -maxdepth 1 -type f -name "*.h" \
            ! -name "${comp_api}_types.h" ! -name "${comp_api}.h" 2>/dev/null | wc -l | tr -d ' ')
        if [ "$extra_headers" -eq 0 ]; then
            printf "%b\n" "  ${GREEN}✅${NC} Aucun header public extra"
            SCORE=$((SCORE + 1))
        else
            printf "%b\n" "  ${RED}❌${NC} ${extra_headers} headers publics extra (INTERDIT)"
        fi
    fi

    # 4) Canonical source file recommended
    src_c="${comp_dir}/src/${comp_api}.c"
    if [ -f "$src_c" ]; then
        printf "%b\n" "  ${GREEN}✅${NC} src/${comp_api}.c"
        SCORE=$((SCORE + 1))
    else
        printf "%b\n" "  ${YELLOW}⚠️${NC} MISSING: src/${comp_api}.c (recommandé, legacy toléré)"
    fi

    # 5) Public esp_err_t symbols namespaced
    if [ -d "${comp_dir}/include/${comp_api}" ]; then
        incorrect=0
        list_public_esp_err_symbols "${comp_dir}/include/${comp_api}" | while IFS= read -r sym; do
            [ -n "$sym" ] || continue
            case "$sym" in
                ${comp_api}_*) : ;;
                *)
                    printf "%b\n" "  ${RED}❌${NC} Symbole public non namespacé: ${sym} (attendu: ${comp_api}_*)"
                    incorrect=1
                    ;;
            esac
        done

        # NOTE: because the while runs in a subshell in POSIX sh, we can't rely on incorrect=1 set inside.
        # So re-check deterministically:
        bad_count=$(list_public_esp_err_symbols "${comp_dir}/include/${comp_api}" | grep -v "^${comp_api}_" 2>/dev/null | wc -l | tr -d ' ')
        if [ "$bad_count" -eq 0 ]; then
            printf "%b\n" "  ${GREEN}✅${NC} Nommage des symboles esp_err_t OK"
            SCORE=$((SCORE + 1))
        fi
    fi

    # 6) micro-ROS forbidden outside mw_*
    if has_uros_in_public_headers "${comp_dir}/include/${comp_api}"; then
        if [ "$comp_kind" = "middleware" ]; then
            printf "%b\n" "  ${GREEN}✅${NC} micro-ROS détecté (autorisé car mw_*)"
            SCORE=$((SCORE + 1))
        else
            printf "%b\n" "  ${RED}❌${NC} micro-ROS détecté dans headers publics (INTERDIT hors mw_*)"
        fi
    else
        printf "%b\n" "  ${GREEN}✅${NC} Pas d'include micro-ROS dans l'API publique"
        SCORE=$((SCORE + 1))
    fi

    # 7) Dependency graph (iobewi_*) STD-TAX-003
    cmake_file="${comp_dir}/CMakeLists.txt"
    bad_deps=0
    deps_list=$(extract_iobewi_deps_from_cmake "$cmake_file" || true)

    if [ "$comp_kind" = "driver" ]; then
        if [ -n "$deps_list" ]; then
            printf "%b\n" "  ${RED}❌${NC} Dépendances iobewi_* dans driver (INTERDIT):"
            printf "%s\n" "$deps_list" | sed 's/^/      - /'
            bad_deps=1
        fi
    else
        if [ -n "$deps_list" ]; then
            printf "%s\n" "$deps_list" | while IFS= read -r dep; do
                [ -n "$dep" ] || continue
                dep_kind=$(kind_from_component_id "$dep")
                if ! deps_allowed "$comp_kind" "$dep_kind"; then
                    printf "%b\n" "  ${RED}❌${NC} Dépendance interdite: $dep (kind=$dep_kind) depuis $comp_dir (kind=$comp_kind)"
                    bad_deps=1
                fi
            done
            # subshell note: re-evaluate deterministically:
            # if any dep is forbidden, count it
            forbidden_count=0
            for dep in $deps_list; do
                dep_kind=$(kind_from_component_id "$dep")
                if ! deps_allowed "$comp_kind" "$dep_kind"; then
                    forbidden_count=$((forbidden_count + 1))
                fi
            done
            if [ "$forbidden_count" -gt 0 ]; then
                bad_deps=1
            fi
        fi
    fi

    if [ "$bad_deps" -eq 0 ]; then
        printf "%b\n" "  ${GREEN}✅${NC} Dépendances iobewi_* conformes (STD-TAX-003)"
        SCORE=$((SCORE + 1))
    fi

    # 8) basic_app required
    examples_dir="$REPO_ROOT/examples/${comp_dir}/basic_app"
    if [ -d "$examples_dir" ]; then
        if [ -f "$examples_dir/CMakeLists.txt" ] && [ -d "$examples_dir/main" ] && [ -f "$examples_dir/main/main.c" ]; then
            printf "%b\n" "  ${GREEN}✅${NC} examples/${comp_dir}/basic_app (structure OK)"
            SCORE=$((SCORE + 1))
        else
            printf "%b\n" "  ${YELLOW}⚠️${NC} examples/${comp_dir}/basic_app présent mais incomplet (CMakeLists/main/main.c)"
        fi
    else
        printf "%b\n" "  ${RED}❌${NC} MISSING: examples/${comp_dir}/basic_app"
    fi

    # 9) Unit tests required
    test_dir="${comp_dir}/test"
    if check_tests_structure "$test_dir"; then
        printf "%b\n" "  ${GREEN}✅${NC} tests unitaires: test/ + CMakeLists + test_*.c"
        SCORE=$((SCORE + 1))
    else
        st=$?
        case "$st" in
            1) printf "%b\n" "  ${RED}❌${NC} MISSING: ${comp_dir}/test/ (tests unitaires obligatoires)" ;;
            2) printf "%b\n" "  ${RED}❌${NC} MISSING: ${comp_dir}/test/CMakeLists.txt" ;;
            3) printf "%b\n" "  ${RED}❌${NC} MISSING: ${comp_dir}/test/test_*.c (au moins 1 fichier)" ;;
            *) printf "%b\n" "  ${RED}❌${NC} tests unitaires: structure invalide" ;;
        esac
    fi

    PERCENT=$((SCORE * 100 / MAX_SCORE))
    printf "%b\n" "  ${BLUE}Score: $SCORE/$MAX_SCORE ($PERCENT%)${NC}"

    if [ "$PERCENT" -ge 85 ]; then
        printf "%b\n" "  ${GREEN}✅ CONFORME${NC}"
        CONFORM=$((CONFORM + 1))
    elif [ "$PERCENT" -ge 60 ]; then
        printf "%b\n" "  ${YELLOW}⚠️ PARTIEL${NC}"
        PARTIAL=$((PARTIAL + 1))
    else
        printf "%b\n" "  ${RED}❌ NON-CONFORME${NC}"
        NON_CONFORM=$((NON_CONFORM + 1))
    fi

    printf "\n"
done

# ---------- Résumé ----------
printf "%b\n" "${BLUE}=========================================${NC}"
printf "%b\n" "${BLUE}   Résumé${NC}"
printf "%b\n" "${BLUE}=========================================${NC}"
printf "Total composants     : %s\n" "$TOTAL"
printf "%b\n" "${GREEN}✅ Conformes        : $CONFORM${NC}"
printf "%b\n" "${YELLOW}⚠️ Partiels         : $PARTIAL${NC}"
printf "%b\n" "${RED}❌ Non-conformes    : $NON_CONFORM${NC}"

if [ "$TOTAL" -eq 0 ]; then
    printf "%b\n" "${RED}❌ Aucun composant trouvé dans $REPO_ROOT/components${NC}"
    exit 1
fi

CONFORM_PERCENT=$((CONFORM * 100 / TOTAL))
printf "%b\n\n" "${BLUE}Taux de conformité  : $CONFORM_PERCENT%${NC}"

if [ "$CONFORM_PERCENT" -eq 100 ]; then
    printf "%b\n" "${GREEN}🎉 OBJECTIF ATTEINT : 100%% de conformité !${NC}"
    exit 0
elif [ "$CONFORM_PERCENT" -ge 70 ]; then
    printf "%b\n" "${YELLOW}⚠️ Bon progrès, continuez !${NC}"
    exit 0
else
    printf "%b\n" "${RED}❌ Travail nécessaire pour atteindre l'objectif${NC}"
    exit 1
fi
