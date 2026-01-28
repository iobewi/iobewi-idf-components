# 📊 Reports – Audits & Analyses

Ce dossier contient les **rapports d’analyse, d’audit et de conformité** relatifs au framework **iobewi-idf-components**.

Les documents présents ici sont des **instantanés à un instant T** :
- audits de composants
- rapports de refactorisation
- analyses de dette technique
- comptes rendus d’évaluation qualité
- résultats d’outils automatisés

---

## 🎯 Objectifs

Les rapports servent à :

- Évaluer la **conformité** du code existant au standard du framework
- Identifier les **écarts** (structure, API, tests, CDC)
- Prioriser les **actions correctives**
- Tracer les **décisions techniques** dans le temps
- Fournir une base factuelle pour les audits internes ou industriels

---

## 📘 Positionnement par rapport à la documentation

⚠️ **Important** :

- Les documents de ce dossier **NE SONT PAS normatifs**
- Ils **n’introduisent aucune règle**
- Ils **n’ont aucune autorité** sur le code ou l’architecture

👉 La **seule source de vérité normative** est :
- `docs/standard.md`

Les rapports :
- analysent l’état du code **par rapport au standard**
- ne remplacent jamais le standard
- peuvent devenir obsolètes avec le temps

---

## 📂 Contenu typique

Exemples de rapports attendus :

- `audit_drv_vl53l0x.md`
- `audit_app_scan_tof.md`
- `component_conformity_report_2026Q1.md`
- `refactor_plan_lib_vl53l0x_provider.md`
- `ci_quality_report.md`

Chaque rapport doit préciser :
- la **date**
- la **version du standard** utilisée comme référence
- le **périmètre analysé**

---

## 📝 Format recommandé d’un rapport

```markdown
# Rapport d’audit – <composant ou périmètre>

## Contexte
## Références (standard, CDC, annexes)
## État actuel
## Non-conformités identifiées
## Impacts
## Recommandations
## Plan d’action (si applicable)
````

---

## 🔄 Cycle de vie des rapports

* Un rapport peut être :

  * clôturé (corrigé)
  * archivé
  * remplacé par un rapport plus récent
* Les rapports **ne doivent pas être modifiés rétroactivement**
* Toute correction se fait via :

  * un nouveau rapport
  * ou une mise à jour du standard

---

## 🤖 Utilisation par des agents IA

Les rapports peuvent être utilisés par des agents IA pour :

* analyser l’historique des décisions
* comprendre les écarts passés
* proposer des plans de refactorisation

⚠️ Les agents ne doivent **jamais** interpréter un rapport comme une règle normative.

---

## ✅ Règle d’or

> **Le standard définit les règles.
> Les rapports constatent l’état.**