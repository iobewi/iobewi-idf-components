# 🛠️ Scripts Utilitaires — iobewi-idf-components

Ce dossier contient les **scripts officiels** permettant de **vérifier, auditer et maintenir la conformité** des composants du framework **iobewi-idf-components**.

> ⚠️ Les règles opposables sont définies dans `docs/standard.md`
> Les scripts **appliquent et vérifient** ces règles — ils ne les définissent pas.

---

## 📋 Scripts disponibles

### `check_conformity.sh` ✅ (script principal)

**Rôle**

Audit automatisé de conformité des composants selon :

* `STD-TAX-*` (taxonomie)
* `STD-STR-*` (structure)
* `STD-API-*` (API publique)
* `STD-CLS-001` (clôture de cohérence)

**Ce que le script vérifie actuellement**

1. Nom du composant (`iobewi_<kind>_<name>`)
2. Mapping correct vers l’API (`drv_*`, `lib_*`, `mw_*`, `app_*`)
3. Présence des **2 headers publics** :

   * `<component_api>_types.h`
   * `<component_api>.h`
4. Absence de headers publics non autorisés
5. Nommage correct des fonctions publiques (`<component_api>_*`)
6. Présence de :

   * `CMakeLists.txt`
   * `README.md`
7. Présence de l’exemple :

   * `examples/<component_id>/basic_app/`

---

## ▶️ Utilisation

### ⚠️ IMPORTANT — Bash uniquement

Ce script **DOIT être exécuté avec `bash`**.
Lancer le script avec `sh` est **interdit** et provoquera une erreur.

✅ Commandes valides :

```bash
bash tools/scripts/check_conformity.sh
./tools/scripts/check_conformity.sh
```

❌ Commande invalide :

```bash
sh tools/scripts/check_conformity.sh
```

---

### Vérification manuelle

```bash
# Depuis la racine du repo
bash tools/scripts/check_conformity.sh

# Ou en précisant explicitement le chemin racine
bash tools/scripts/check_conformity.sh /path/to/iobewi-idf-components
```

---

### Exemple de sortie

```
=========================================
   Audit de Conformité CDC
=========================================

=== iobewi_driver_a02yyuw ===
  ✅ drv_a02yyuw_types.h
  ✅ drv_a02yyuw.h
  ✅ Pas de headers extra
  ✅ Nommage fonctions correct
  ✅ CMakeLists.txt présent
  ✅ README.md présent
  ✅ examples/iobewi_driver_a02yyuw/basic_app présent
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

---

## 📤 Codes de retour

| Code | Signification            |
| ---: | ------------------------ |
|  `0` | Conformité globale ≥ 70% |
|  `1` | Conformité globale < 70% |

👉 En CI, un code `1` **bloque le pipeline**.

---

## 🤖 Intégration CI/CD (exemple GitHub Actions)

```yaml
name: Conformity Check

on: [push, pull_request]

jobs:
  conformity:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Run conformity audit
        run: |
          chmod +x tools/scripts/check_conformity.sh
          ./tools/scripts/check_conformity.sh
```

---

## 🧩 Hook Git pre-commit (optionnel)

```bash
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash
set -e

bash tools/scripts/check_conformity.sh || {
    echo "❌ Conformité insuffisante — commit refusé"
    exit 1
}
EOF

chmod +x .git/hooks/pre-commit
```

---

## 📊 Interprétation des scores

### Score par composant

|      Score | Statut         | Interprétation           |
| ---------: | -------------- | ------------------------ |
| 7/7 (100%) | ✅ CONFORME     | Conforme strict          |
|      ≥ 85% | ✅ CONFORME     | Conforme avec remarques  |
|     60–84% | ⚠️ PARTIEL     | Corrections recommandées |
|      < 60% | ❌ NON CONFORME | Refactor requis          |

### Taux global

| Taux   | Action attendue          |
| ------ | ------------------------ |
| 100%   | 🎉 Objectif atteint      |
| 70–99% | ⚠️ Amélioration continue |
| < 70%  | ❌ Blocage CI             |

---

## 🔧 Maintenance du script

### Ajouter un nouveau critère

1. Ajouter la vérification dans la boucle
2. Incrémenter `MAX_SCORE`
3. Incrémenter `SCORE` si le critère est valide

Exemple :

```bash
if [ -f "${comp_dir}/idf_component.yml" ]; then
    echo -e "  ${GREEN}✅${NC} idf_component.yml présent"
    SCORE=$((SCORE + 1))
else
    echo -e "  ${RED}❌${NC} MISSING: idf_component.yml"
fi
```

---

## 🐛 Dépannage

### ❌ `Illegal option -o pipefail`

Cause :

* Script exécuté avec `sh`

Solution :

```bash
bash tools/scripts/check_conformity.sh
```

---

### ❌ Aucun composant détecté

Vérifier :

```bash
ls components/iobewi_*
```

Et lancer depuis la racine du repo.

---

## 📚 Références

* `docs/standard.md` — **source de vérité normative**
* `docs/annexes/audit_playbook.md`
* `docs/annexes/component_playbook.md`
* `docs/annexes/examples.md`
* `docs/annexes/testing.md`

---

**Version** : 1.1
**Dernière mise à jour** : 2026-01-27