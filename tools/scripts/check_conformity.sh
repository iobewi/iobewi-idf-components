#!/bin/bash
# check_conformity.sh
# Script de vérification de conformité CDC pour les composants
# Version 2.0 - Support structure components/ avec double nomenclature

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Déterminer le répertoire racine du projet
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${1:-$(cd "$SCRIPT_DIR/../.." && pwd)}"

if [ ! -d "$REPO_ROOT/components" ]; then
    echo -e "${RED}❌ Erreur: Dossier components/ non trouvé dans $REPO_ROOT${NC}"
    echo "Usage: $0 [chemin_racine_projet]"
    exit 1
fi

cd "$REPO_ROOT/components"

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}   Audit de Conformité CDC${NC}"
echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}   Répertoire: $REPO_ROOT/components${NC}"
echo -e "${BLUE}=========================================${NC}"
echo ""

TOTAL=0
CONFORM=0
PARTIAL=0
NON_CONFORM=0

# Fonction pour mapper nom de répertoire vers nom API
get_api_name() {
    local dir_name="$1"

    # iobewi_driver_* -> drv_*
    if [[ "$dir_name" =~ ^iobewi_driver_(.+)$ ]]; then
        echo "drv_${BASH_REMATCH[1]}"
        return 0
    fi

    # iobewi_libs_* -> lib_*
    if [[ "$dir_name" =~ ^iobewi_libs_(.+)$ ]]; then
        echo "lib_${BASH_REMATCH[1]}"
        return 0
    fi

    # iobewi_mw_* -> mw_*
    if [[ "$dir_name" =~ ^iobewi_mw_(.+)$ ]]; then
        echo "mw_${BASH_REMATCH[1]}"
        return 0
    fi

    # iobewi_apps_* -> app_*
    if [[ "$dir_name" =~ ^iobewi_apps_(.+)$ ]]; then
        echo "app_${BASH_REMATCH[1]}"
        return 0
    fi

    # Pas de mapping, retourner tel quel
    echo "$dir_name"
}

# Parcourir tous les composants iobewi_*
for comp_dir in iobewi_driver_* iobewi_libs_* iobewi_mw_* iobewi_apps_*; do
    if [ ! -d "$comp_dir" ]; then continue; fi

    # Obtenir le nom API (forme courte)
    comp_api=$(get_api_name "$comp_dir")

    TOTAL=$((TOTAL + 1))
    SCORE=0
    MAX_SCORE=7

    echo -e "${BLUE}=== $comp_dir ===${NC}"
    echo -e "${BLUE}    API name: $comp_api${NC}"

    # 1. Vérifier headers types et api
    types_h="${comp_dir}/include/${comp_api}/${comp_api}_types.h"
    api_h="${comp_dir}/include/${comp_api}/${comp_api}.h"

    if [ -f "$types_h" ]; then
        echo -e "  ${GREEN}✅${NC} ${comp_api}_types.h"
        SCORE=$((SCORE + 1))
    else
        echo -e "  ${RED}❌${NC} MISSING: ${comp_api}_types.h"
    fi

    if [ -f "$api_h" ]; then
        echo -e "  ${GREEN}✅${NC} ${comp_api}.h"
        SCORE=$((SCORE + 1))
    else
        echo -e "  ${RED}❌${NC} MISSING: ${comp_api}.h"
    fi

    # 2. Vérifier headers extra (non conformes)
    if [ -d "${comp_dir}/include/${comp_api}" ]; then
        extra_headers=$(find "${comp_dir}/include/${comp_api}" -maxdepth 1 -name "*.h" \
            ! -name "${comp_api}_types.h" ! -name "${comp_api}.h" 2>/dev/null | wc -l)

        if [ $extra_headers -eq 0 ]; then
            echo -e "  ${GREEN}✅${NC} Pas de headers extra"
            SCORE=$((SCORE + 1))
        else
            echo -e "  ${YELLOW}⚠️${NC} $extra_headers headers extra trouvés"
        fi
    else
        echo -e "  ${RED}❌${NC} MISSING: include/${comp_api}/"
    fi

    # 3. Vérifier nommage des fonctions publiques
    if [ -f "$api_h" ]; then
        funcs=$(grep -h "^esp_err_t\|^bool\|^void" "${comp_dir}/include/${comp_api}/"*.h 2>/dev/null | \
                grep -o '[a-z_0-9]*(' | sed 's/($//' || true)

        if [ -n "$funcs" ]; then
            incorrect=0
            while IFS= read -r func; do
                # Exceptions pour uros_core (préfixe accepté)
                if [[ ! "$func" =~ ^${comp_api}_ ]] && [[ ! "$func" =~ ^uros_core_ ]]; then
                    echo -e "  ${RED}❌${NC} Fonction incorrecte: $func (devrait être ${comp_api}_*)"
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
    if [ -f "${comp_dir}/CMakeLists.txt" ]; then
        echo -e "  ${GREEN}✅${NC} CMakeLists.txt présent"
        SCORE=$((SCORE + 1))
    else
        echo -e "  ${RED}❌${NC} MISSING: CMakeLists.txt"
    fi

    # 5. Vérifier README.md
    if [ -f "${comp_dir}/README.md" ]; then
        echo -e "  ${GREEN}✅${NC} README.md présent"
        SCORE=$((SCORE + 1))
    else
        echo -e "  ${RED}❌${NC} MISSING: README.md"
    fi

    # 6. Vérifier examples/basic_app (dans ../examples/)
    examples_dir="$REPO_ROOT/examples/${comp_dir}"
    if [ -d "${examples_dir}/basic_app" ]; then
        echo -e "  ${GREEN}✅${NC} examples/${comp_dir}/basic_app présent"
        SCORE=$((SCORE + 1))
    else
        echo -e "  ${RED}❌${NC} MISSING: examples/${comp_dir}/basic_app"
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

if [ $TOTAL -eq 0 ]; then
    echo -e "${RED}❌ Aucun composant trouvé dans $REPO_ROOT/components${NC}"
    exit 1
fi

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
