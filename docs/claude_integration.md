# 🤖 Intégration avec Claude Code

Ce document explique comment intégrer les bonnes pratiques CDC avec Claude Code pour garantir le respect automatique des standards.

---

## ❓ Question : Forcer la lecture d'un fichier au démarrage de Claude ?

**Réponse** : Claude Code CLI ne supporte pas nativement le chargement automatique d'un fichier de référence au démarrage d'une session. Cependant, il existe plusieurs **solutions alternatives** pour garantir le respect des bonnes pratiques.

---

## ✅ Solutions Recommandées

### Solution 1 : Instructions Système via `.claudeignore` ou Configuration

**Statut** : ❌ Non supporté directement par Claude Code

Claude Code ne permet pas actuellement de spécifier des fichiers de référence obligatoires dans une configuration système.

---

### Solution 2 : 🎯 **Utiliser un Prompt de Projet** (RECOMMANDÉ)

**Statut** : ✅ Fonctionne

Créer un fichier `.claude/instructions.md` à la racine du projet qui sera automatiquement lu par Claude lors de l'initialisation.

#### Implémentation

```bash
# Créer le répertoire .claude
mkdir -p .claude

# Créer le fichier d'instructions
cat > .claude/instructions.md << 'EOF'
# Instructions Projet - iobewi-idf-components

## Règles Obligatoires

Avant toute modification de composant :

1. **Lire** : `docs/component_best_practices.md` (bonnes pratiques CDC)
2. **Vérifier** : Checklist de conformité (section 8)
3. **Valider** : Exécuter `scripts/check_conformity.sh`

## Références Critiques

- **CDC** : `docs/cdc.md`
- **Best Practices** : `docs/component_best_practices.md`
- **Audit** : `docs/conformity_audit_2026-01-26.md`

## Composants Références (100% Conformes)

Utilisez ces composants comme templates :

- `drv_a02yyuw` : Driver conforme
- `lib_a02_provider` : Library conforme
- `app_scan_ultra` : Application conforme

## Checklist Express

- [ ] Headers : `<component>_types.h` + `<component>.h`
- [ ] Nommage : Préfixe `<component>_*`
- [ ] Handle opaque : `typedef struct <component>_s <component>_t;`
- [ ] CMakeLists : REQUIRES vs PRIV_REQUIRES
- [ ] Example : `examples/basic_app/` fonctionnel

## Workflow Recommandé

1. Lire `component_best_practices.md`
2. Comprendre la taxonomie (drv/lib/mw/app)
3. Coder selon le pattern handle opaque
4. Tester avec `check_conformity.sh`
5. Valider build : `idf.py build`

## ⚠️ Interdictions

- ❌ Créer un header sans le suffixe `<component>_`
- ❌ Mélanger types et API dans un seul header
- ❌ Violer la taxonomie (drv avec logique métier, lib avec micro-ROS, etc.)
- ❌ Utiliser des noms génériques (scan_engine, tof_provider, etc.)

EOF
```

**Avantages** :
- ✅ Chargé automatiquement par Claude au démarrage
- ✅ Version contrôlée avec Git
- ✅ Facile à maintenir

**Limitations** :
- ⚠️ Nécessite que l'utilisateur ait configuré Claude Code pour lire `.claude/instructions.md`

---

### Solution 3 : 📋 **Checklist Interactive via Script**

**Statut** : ✅ Fonctionne

Créer un script interactif qui rappelle les bonnes pratiques avant chaque modification.

#### Implémentation

```bash
#!/bin/bash
# scripts/pre_commit_check.sh

echo "========================================="
echo "   Checklist Conformité CDC"
echo "========================================="
echo ""
echo "Avant de commiter, vérifiez :"
echo ""
echo "1. Avez-vous lu component_best_practices.md ? (Y/n)"
read -r response
if [[ "$response" =~ ^[Nn]$ ]]; then
    echo "⚠️ Veuillez lire docs/component_best_practices.md d'abord"
    exit 1
fi

echo "2. Exécuter check_conformity.sh ? (Y/n)"
read -r response
if [[ ! "$response" =~ ^[Nn]$ ]]; then
    ./scripts/check_conformity.sh
fi

echo ""
echo "3. Headers conformes (<component>_types.h + <component>.h) ? (Y/n)"
read -r response
if [[ "$response" =~ ^[Nn]$ ]]; then
    echo "❌ Veuillez corriger la structure des headers"
    exit 1
fi

echo "4. Nommage conforme (préfixe <component>_*) ? (Y/n)"
read -r response
if [[ "$response" =~ ^[Nn]$ ]]; then
    echo "❌ Veuillez corriger le nommage"
    exit 1
fi

echo ""
echo "✅ Checklist validée ! Vous pouvez commiter."
```

**Utilisation** :

```bash
chmod +x scripts/pre_commit_check.sh

# Avant chaque commit
./scripts/pre_commit_check.sh
git commit -m "..."
```

**Avantages** :
- ✅ Force la vérification explicite
- ✅ Éducatif pour les nouveaux contributeurs

---

### Solution 4 : 🪝 **Git Pre-Commit Hook** (AUTOMATIQUE)

**Statut** : ✅ Fonctionne

Intégrer la vérification de conformité dans un hook Git qui s'exécute automatiquement avant chaque commit.

#### Implémentation

```bash
# Installer le hook
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash
# Pre-commit hook : Vérification conformité CDC

echo "🔍 Vérification conformité CDC..."

# Exécuter le script de vérification
if ! ./scripts/check_conformity.sh; then
    echo ""
    echo "❌ COMMIT BLOQUÉ : Conformité < 70%"
    echo ""
    echo "Actions requises :"
    echo "  1. Lire docs/component_best_practices.md"
    echo "  2. Corriger les problèmes identifiés"
    echo "  3. Réessayer le commit"
    echo ""
    echo "Pour bypasser (NON RECOMMANDÉ) : git commit --no-verify"
    exit 1
fi

echo "✅ Conformité validée"
exit 0
EOF

chmod +x .git/hooks/pre-commit
```

**Avantages** :
- ✅ Automatique et transparent
- ✅ Force le respect des standards
- ✅ Éducatif (messages d'erreur explicites)

**Limitations** :
- ⚠️ Peut être bypassé avec `--no-verify`
- ⚠️ Nécessite installation manuelle (pas versionné avec Git)

---

### Solution 5 : 📚 **Documentation Contextualisée**

**Statut** : ✅ Fonctionne

Ajouter des liens vers la documentation directement dans les fichiers critiques.

#### Implémentation

**Dans chaque composant `README.md`** :

```markdown
# <component_name>

> ⚠️ **Avant modification** : Lire [component_best_practices.md](../docs/component_best_practices.md)

[reste du README]
```

**Dans chaque header `<component>.h`** :

```c
/**
 * @file <component>.h
 * @brief <Description>
 *
 * @warning Toute modification doit respecter component_best_practices.md
 * @see docs/component_best_practices.md
 */
```

**Avantages** :
- ✅ Visible par tous les contributeurs
- ✅ Intégré au code source

---

## 🎯 Solution Recommandée : Combinaison

Pour une efficacité maximale, **combiner plusieurs solutions** :

```
┌─────────────────────────────────────────────┐
│  1. .claude/instructions.md                 │
│     └─> Chargé par Claude automatiquement   │
├─────────────────────────────────────────────┤
│  2. scripts/check_conformity.sh             │
│     └─> Vérification manuelle               │
├─────────────────────────────────────────────┤
│  3. .git/hooks/pre-commit                   │
│     └─> Vérification automatique            │
├─────────────────────────────────────────────┤
│  4. Documentation dans README.md            │
│     └─> Rappel visible                      │
└─────────────────────────────────────────────┘
```

### Mise en Place Complète

```bash
# 1. Créer .claude/instructions.md
mkdir -p .claude
cat docs/component_best_practices.md > .claude/instructions.md

# 2. Scripts déjà créés
chmod +x scripts/check_conformity.sh

# 3. Installer pre-commit hook
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash
./scripts/check_conformity.sh || {
    echo "❌ Conformité insuffisante. Lire docs/component_best_practices.md"
    exit 1
}
EOF
chmod +x .git/hooks/pre-commit

# 4. Ajouter avertissement dans README principal
echo "" >> README.md
echo "> ⚠️ **Contributeurs** : Lire [component_best_practices.md](docs/component_best_practices.md) avant toute modification" >> README.md
```

---

## 📖 Workflow Claude Code Recommandé

### Pour Claude (assistant IA)

Lorsque vous travaillez sur ce projet :

1. **Au démarrage de session** :
   ```
   Lire automatiquement :
   - docs/component_best_practices.md
   - docs/conformity_audit_2026-01-26.md
   ```

2. **Avant toute modification** :
   ```
   - Identifier la catégorie du composant (drv/lib/mw/app)
   - Vérifier la checklist de conformité (section 8)
   - Utiliser les composants conformes comme référence
   ```

3. **Pendant le développement** :
   ```
   - Respecter le pattern handle opaque
   - Utiliser le préfixe <component>_ partout
   - Séparer types et API dans 2 headers
   ```

4. **Après modifications** :
   ```
   - Exécuter scripts/check_conformity.sh
   - Valider le build : idf.py build
   - Mettre à jour la documentation
   ```

### Pour les Contributeurs Humains

1. **Première contribution** :
   ```bash
   # Lire la documentation
   cat docs/component_best_practices.md | less

   # Installer les hooks
   ./scripts/setup_hooks.sh  # À créer
   ```

2. **Workflow quotidien** :
   ```bash
   # Avant de coder
   cat docs/component_best_practices.md | grep "Checklist"

   # Pendant le dev : suivre les patterns

   # Avant commit
   ./scripts/check_conformity.sh
   git commit -m "..."
   ```

---

## 🔧 Scripts Utilitaires

### Script 1 : Setup Environnement

```bash
#!/bin/bash
# scripts/setup_dev_env.sh

echo "Configuration environnement de développement..."

# 1. Créer .claude/instructions.md
mkdir -p .claude
cp docs/component_best_practices.md .claude/instructions.md

# 2. Installer pre-commit hook
if [ -d .git ]; then
    cp scripts/pre-commit.sample .git/hooks/pre-commit
    chmod +x .git/hooks/pre-commit
    echo "✅ Pre-commit hook installé"
fi

# 3. Créer alias utiles
cat >> ~/.bash_aliases << 'EOF'
alias cdc-check='./scripts/check_conformity.sh'
alias cdc-doc='less docs/component_best_practices.md'
EOF

echo "✅ Environnement configuré"
echo "Rechargez votre shell : source ~/.bashrc"
```

### Script 2 : Vérification Rapide

```bash
#!/bin/bash
# scripts/quick_check.sh

COMPONENT="${1}"

if [ -z "$COMPONENT" ]; then
    echo "Usage: $0 <component_name>"
    exit 1
fi

echo "Vérification rapide de $COMPONENT..."

# Headers
[ -f "$COMPONENT/include/$COMPONENT/${COMPONENT}_types.h" ] && echo "✅ types.h" || echo "❌ types.h"
[ -f "$COMPONENT/include/$COMPONENT/${COMPONENT}.h" ] && echo "✅ .h" || echo "❌ .h"

# Nommage
grep -h "^esp_err_t" "$COMPONENT/include/$COMPONENT/"*.h 2>/dev/null | \
    grep -v "${COMPONENT}_" && echo "❌ Nommage incorrect" || echo "✅ Nommage OK"

# Build
cd "$COMPONENT/examples/basic_app"
idf.py build > /dev/null 2>&1 && echo "✅ Build OK" || echo "❌ Build FAILED"
```

---

## 📝 Résumé

| Solution | Auto | Efficacité | Complexité | Recommandé |
|----------|------|------------|------------|------------|
| `.claude/instructions.md` | ✅ | 🟡 Moyen | 🟢 Faible | ✅ |
| `check_conformity.sh` | ❌ | 🟢 Élevé | 🟢 Faible | ✅ |
| Pre-commit hook | ✅ | 🟢 Élevé | 🟡 Moyen | ✅ |
| Documentation inline | ❌ | 🟡 Moyen | 🟢 Faible | ✅ |

**Meilleure approche** : **Combinaison des 4 solutions** pour couverture maximale.

---

## 🎓 Formation des Contributeurs

### Onboarding Checklist

Pour tout nouveau contributeur :

- [ ] Lire `docs/cdc.md`
- [ ] Lire `docs/component_best_practices.md`
- [ ] Examiner un composant conforme (drv_a02yyuw)
- [ ] Exécuter `scripts/check_conformity.sh`
- [ ] Installer pre-commit hook
- [ ] Faire un commit test
- [ ] Valider conformité à 100%

---

## 📞 Support

Pour questions sur l'intégration Claude :

- 📚 Consulter ce document
- 🔍 Lire `component_best_practices.md`
- 💬 Ouvrir une discussion

---

**Créé le** : 2026-01-26
**Version** : 1.0
