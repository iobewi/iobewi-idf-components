# 📁 `docs/annexes/README.md`

```markdown
# 📎 Annexes – Guides & Détails Techniques

Ce dossier contient les **annexes techniques** du framework **iobewi-idf-components**.

Les annexes complètent le document maître (`docs/standard.md`) en fournissant :
- des **détails opérationnels**
- des **guides d’application**
- des **exemples concrets**
- des **patterns approfondis**

---

## 🎯 Objectifs des annexes

Les annexes servent à :

- Expliquer **comment appliquer** les règles du standard
- Centraliser les détails trop volumineux pour le document maître
- Faciliter l’onboarding des développeurs
- Documenter des cas spécifiques (tests, micro-ROS, migration)

---

## 📘 Relation avec le document maître

⚠️ **Règle fondamentale** :

- Le document maître (`docs/standard.md`) est **normatif**
- Les annexes sont **informatives et explicatives**

👉 Les annexes :
- **NE DOIVENT PAS** introduire de nouvelles règles
- **NE DOIVENT PAS** contredire le standard
- **PEUVENT** détailler, illustrer ou exemplifier

En cas de conflit :
> **Le standard prévaut toujours.**

---

## 📂 Structure attendue

Exemple de structure :

````

docs/annexes/
├── testing.md          # Détails des tests unitaires et patterns de mock
├── microros.md         # micro-ROS, rcl/rclc, QoS, transport
├── examples.md         # Exemples complets et cas d’usage
├── migration.md        # Guides de refactorisation et harmonisation

```

---

## 🧪 Exemples d’annexes

- **testing.md**
  - Patterns de tests unitaires
  - Exemples de mocks
  - Organisation des suites de tests

- **microros.md**
  - Bonnes pratiques rcl/rclc
  - Erreurs fréquentes
  - Patterns d’executor

- **migration.md**
  - Avant / après
  - Stratégies de découpage `drv → lib → mw → app`
  - Checklists de refactorisation

---

## 📝 Règles d’écriture des annexes

- Être **factuel et pédagogique**
- Utiliser des exemples concrets
- Référencer explicitement les sections du standard
- Éviter les formulations ambiguës (“il faudrait”, “on peut”)
- Privilégier :
  - “Voici comment appliquer la règle X”
  - “Voici un exemple conforme”

---

## 🤖 Utilisation par des agents IA

Les annexes peuvent être utilisées par des agents IA pour :
- comprendre les patterns recommandés
- générer du code conforme
- proposer des refactorisations

⚠️ Les agents ne doivent jamais considérer une annexe comme une source normative.

---

## ✅ Règle d’or

> **Le standard décide.  
> Les annexes expliquent.**