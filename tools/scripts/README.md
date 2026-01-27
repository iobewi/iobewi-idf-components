# 🛠️ Scripts Utilitaires - iobewi-idf-components

Scripts pour vérifier et maintenir la conformité CDC du framework.

---

## 📋 Scripts Disponibles

### `check_conformity.sh` ✅

**Description** : Vérifie la conformité de tous les composants selon les bonnes pratiques CDC.

**Utilisation** :

```bash
./scripts/check_conformity.sh
```

**Sortie** :

```
=========================================
   Audit de Conformité CDC
=========================================

=== drv_a02yyuw ===
  ✅ drv_a02yyuw_types.h
  ✅ drv_a02yyuw.h
  ✅ Pas de headers extra
  ✅ Nommage fonctions correct
  ✅ CMakeLists.txt présent
  ✅ README.md présent
  ✅ examples/basic_app présent
  Score: 7/7 (100%)
  ✅ CONFORME

...

=========================================
   Résumé
=========================================
Total composants     : 10
✅ Conformes        : 6
⚠️ Partiels         : 1
❌ Non-conformes    : 3
Taux de conformité  : 60%
```

**Codes de retour** :

- `0` : Conformité ≥ 70%
- `1` : Conformité < 70%

**Critères vérifiés** :

1. Headers types et API présents
2. Pas de headers extra (hors vendor)
3. Nommage des fonctions avec préfixe correct
4. CMakeLists.txt présent
5. README.md présent
6. examples/basic_app présent

---

## 🎯 Utilisation

### Vérification Manuelle

```bash
# Vérifier tous les composants
./scripts/check_conformity.sh

# Vérifier depuis un autre répertoire
./scripts/check_conformity.sh /path/to/iobewi-idf-components
```

### Intégration CI/CD

```yaml
# .github/workflows/conformity.yml
name: Conformity Check

on: [push, pull_request]

jobs:
  check:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Check Conformity
        run: ./scripts/check_conformity.sh
```

### Git Pre-Commit Hook

```bash
# Installer le hook
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash
./scripts/check_conformity.sh || {
    echo "❌ Conformité insuffisante"
    exit 1
}
EOF

chmod +x .git/hooks/pre-commit
```

---

## 📊 Interprétation des Résultats

### Score par Composant

| Score | Statut | Signification |
|-------|--------|---------------|
| 7/7 (100%) | ✅ CONFORME | Parfait, aucune action |
| 6/7 (85%+) | ✅ CONFORME | Bon, vérifier détails |
| 5/7 (71%+) | ⚠️ PARTIEL | Amélioration nécessaire |
| 0-4/7 (<71%) | ❌ NON-CONFORME | Refactorisation requise |

### Taux Global

| Taux | Statut | Action |
|------|--------|--------|
| 100% | 🎉 Excellent | Maintenir |
| 70-99% | ⚠️ Bon | Continuer |
| <70% | ❌ Insuffisant | Priorité haute |

---

## 🔧 Scripts à Créer (TODO)

### `build_all.sh`

Compiler tous les exemples :

```bash
#!/bin/bash
TARGET="${1:-esp32s3}"

for comp in drv_* lib_* mw_* app_*; do
    if [ -d "$comp/examples/basic_app" ]; then
        echo "Building $comp..."
        cd "$comp/examples/basic_app"
        idf.py set-target $TARGET
        idf.py build || echo "FAILED: $comp"
        cd ../../..
    fi
done
```

### `quick_check.sh`

Vérification rapide d'un composant :

```bash
#!/bin/bash
COMP="${1}"

[ -z "$COMP" ] && { echo "Usage: $0 <component>"; exit 1; }

echo "Checking $COMP..."
[ -f "$COMP/include/$COMP/${COMP}_types.h" ] && echo "✅ types.h" || echo "❌ types.h"
[ -f "$COMP/include/$COMP/${COMP}.h" ] && echo "✅ .h" || echo "❌ .h"
```

### `setup_dev_env.sh`

Configuration environnement développement :

```bash
#!/bin/bash
mkdir -p .claude
cp docs/component_best_practices.md .claude/instructions.md

if [ -d .git ]; then
    cp scripts/pre-commit.sample .git/hooks/pre-commit
    chmod +x .git/hooks/pre-commit
fi

echo "✅ Environnement configuré"
```

---

## 📝 Maintenance

### Ajouter un Nouveau Critère

Pour ajouter un critère de vérification dans `check_conformity.sh` :

1. Incrémenter `MAX_SCORE`
2. Ajouter la vérification
3. Incrémenter `SCORE` si validation OK
4. Afficher le résultat

**Exemple** :

```bash
# Nouveau critère : vérifier idf_component.yml
if [ -f "${comp}/idf_component.yml" ]; then
    echo -e "  ${GREEN}✅${NC} idf_component.yml présent"
    SCORE=$((SCORE + 1))
else
    echo -e "  ${RED}❌${NC} MISSING: idf_component.yml"
fi

MAX_SCORE=8  # Incrémenter
```

### Mettre à Jour les Seuils

Modifier les seuils de conformité dans le script :

```bash
# Actuellement : 85% = conforme, 60% = partiel
if [ $PERCENT -ge 85 ]; then
    echo -e "  ${GREEN}✅ CONFORME${NC}"
elif [ $PERCENT -ge 60 ]; then
    echo -e "  ${YELLOW}⚠️ PARTIEL${NC}"
else
    echo -e "  ${RED}❌ NON-CONFORME${NC}"
fi
```

---

## 🐛 Dépannage

### Script ne trouve pas les composants

**Problème** : `Total composants : 0`

**Solution** :

```bash
# Vérifier le chemin
pwd
ls -d drv_* lib_* mw_* app_*

# Exécuter depuis la racine
cd /workspaces/iobewi-idf-components
./scripts/check_conformity.sh
```

### Erreur de permissions

**Problème** : `Permission denied`

**Solution** :

```bash
chmod +x scripts/check_conformity.sh
```

### Faux positifs dans le nommage

**Problème** : Script détecte incorrectement les préfixes

**Solution** : Vérifier la regex dans le script (ligne ~55) :

```bash
if [[ ! "$func" =~ ^${comp}_ ]] && [[ ! "$func" =~ ^uros_core_ ]]; then
    # Ajouter exceptions si nécessaire
fi
```

---

## 📚 Références

- [component_best_practices.md](../docs/component_best_practices.md) - Bonnes pratiques complètes
- [conformity_audit_2026-01-26.md](../docs/conformity_audit_2026-01-26.md) - Audit détaillé
- [conformity_campaign_plan.md](../docs/conformity_campaign_plan.md) - Plan de remise aux normes

---

**Version** : 1.0
**Dernière mise à jour** : 2026-01-26
