#!/bin/bash
# check_conformity.sh
# Script de vérification de conformité CDC pour les composants

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

REPO_ROOT="${1:-.}"
cd "$REPO_ROOT"

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}   Audit de Conformité CDC${NC}"
echo -e "${BLUE}=========================================${NC}"
echo ""

TOTAL=0
CONFORM=0
PARTIAL=0
NON_CONFORM=0

for comp in drv_* lib_* mw_* app_*; do
    if [ ! -d "$comp" ]; then continue; fi

    TOTAL=$((TOTAL + 1))
    SCORE=0
    MAX_SCORE=7

    echo -e "${BLUE}=== $comp ===${NC}"

    # 1. Vérifier headers types et api
    types_h="${comp}/include/${comp}/${comp}_types.h"
    api_h="${comp}/include/${comp}/${comp}.h"

    if [ -f "$types_h" ]; then
        echo -e "  ${GREEN}✅${NC} ${comp}_types.h"
        SCORE=$((SCORE + 1))
    else
        echo -e "  ${RED}❌${NC} MISSING: ${comp}_types.h"
    fi

    if [ -f "$api_h" ]; then
        echo -e "  ${GREEN}✅${NC} ${comp}.h"
        SCORE=$((SCORE + 1))
    else
        echo -e "  ${RED}❌${NC} MISSING: ${comp}.h"
    fi

    # 2. Vérifier headers extra (non conformes)
    extra_headers=$(find "${comp}/include/${comp}" -maxdepth 1 -name "*.h" \
        ! -name "${comp}_types.h" ! -name "${comp}.h" 2>/dev/null | wc -l)

    if [ $extra_headers -eq 0 ]; then
        echo -e "  ${GREEN}✅${NC} Pas de headers extra"
        SCORE=$((SCORE + 1))
    else
        echo -e "  ${YELLOW}⚠️${NC} $extra_headers headers extra trouvés"
    fi

    # 3. Vérifier nommage des fonctions publiques
    if [ -f "$api_h" ]; then
        funcs=$(grep -h "^esp_err_t\|^bool\|^void" "${comp}/include/${comp}/"*.h 2>/dev/null | \
                grep -o '[a-z_]*(' | sed 's/($//' || true)

        if [ -n "$funcs" ]; then
            incorrect=0
            while IFS= read -r func; do
                # Exceptions pour uros_core (préfixe accepté)
                if [[ ! "$func" =~ ^${comp}_ ]] && [[ ! "$func" =~ ^uros_core_ ]]; then
                    echo -e "  ${RED}❌${NC} Fonction incorrecte: $func"
                    incorrect=1
                fi
            done <<< "$funcs"

            if [ $incorrect -eq 0 ]; then
                echo -e "  ${GREEN}✅${NC} Nommage fonctions correct"
                SCORE=$((SCORE + 1))
            fi
        else
            echo -e "  ${YELLOW}⚠️${NC} Aucune fonction publique trouvée"
        fi
    fi

    # 4. Vérifier CMakeLists.txt
    if [ -f "${comp}/CMakeLists.txt" ]; then
        echo -e "  ${GREEN}✅${NC} CMakeLists.txt présent"
        SCORE=$((SCORE + 1))
    else
        echo -e "  ${RED}❌${NC} MISSING: CMakeLists.txt"
    fi

    # 5. Vérifier README.md
    if [ -f "${comp}/README.md" ]; then
        echo -e "  ${GREEN}✅${NC} README.md présent"
        SCORE=$((SCORE + 1))
    else
        echo -e "  ${RED}❌${NC} MISSING: README.md"
    fi

    # 6. Vérifier examples/basic_app
    if [ -d "${comp}/examples/basic_app" ]; then
        echo -e "  ${GREEN}✅${NC} examples/basic_app présent"
        SCORE=$((SCORE + 1))
    else
        echo -e "  ${RED}❌${NC} MISSING: examples/basic_app"
    fi

    # Calcul du statut
    PERCENT=$((SCORE * 100 / MAX_SCORE))

    echo -e "  ${BLUE}Score: $SCORE/$MAX_SCORE ($PERCENT%)${NC}"

    if [ $PERCENT -ge 85 ]; then
        echo -e "  ${GREEN}✅ CONFORME${NC}"
        CONFORM=$((CONFORM + 1))
    elif [ $PERCENT -ge 60 ]; then
        echo -e "  ${YELLOW}⚠️ PARTIEL${NC}"
        PARTIAL=$((PARTIAL + 1))
    else
        echo -e "  ${RED}❌ NON-CONFORME${NC}"
        NON_CONFORM=$((NON_CONFORM + 1))
    fi

    echo ""
done

# Résumé
echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}   Résumé${NC}"
echo -e "${BLUE}=========================================${NC}"
echo -e "Total composants     : $TOTAL"
echo -e "${GREEN}✅ Conformes        : $CONFORM${NC}"
echo -e "${YELLOW}⚠️ Partiels         : $PARTIAL${NC}"
echo -e "${RED}❌ Non-conformes    : $NON_CONFORM${NC}"

CONFORM_PERCENT=$((CONFORM * 100 / TOTAL))
echo -e "${BLUE}Taux de conformité  : $CONFORM_PERCENT%${NC}"
echo ""

if [ $CONFORM_PERCENT -eq 100 ]; then
    echo -e "${GREEN}🎉 OBJECTIF ATTEINT : 100% de conformité !${NC}"
    exit 0
elif [ $CONFORM_PERCENT -ge 70 ]; then
    echo -e "${YELLOW}⚠️ Bon progrès, continuez !${NC}"
    exit 0
else
    echo -e "${RED}❌ Travail nécessaire pour atteindre l'objectif${NC}"
    exit 1
fi
