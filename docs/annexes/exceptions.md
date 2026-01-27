> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.

# ⚠️ Exceptions — Cadre & gestion des déviations

## Annexe informative au standard `docs/standard.md`

> **STATUT : INFORMATIF**
>
> Ce document décrit **comment gérer une déviation au standard**,  
> **sans jamais affaiblir le standard lui-même**.
>
> ⚠️ Aucune règle normative n’est définie ici.  
> Toute règle opposable reste exclusivement dans `docs/standard.md`.

---

## 1. Principe fondamental

> **Le standard ne s’adapte jamais à une implémentation.  
> Une implémentation peut, exceptionnellement, dévier du standard.**

Une exception est :

- rare
- temporaire
- explicitement documentée
- traçable
- réversible

Toute autre situation est une **violation du standard**, pas une exception.

---

## 2. Ce qu’est une exception (définition stricte)

Une **exception** est une **déviation volontaire et consciente** à une règle
`STD-*`, acceptée **temporairement** pour des raisons techniques justifiées.

Une exception :

- **NE crée pas de précédent**
- **NE modifie pas le standard**
- **NE devient jamais implicite**
- **NE survit pas sans justification active**

---

## 3. Ce qui N’EST PAS une exception

Les cas suivants **ne sont jamais recevables** :

❌ “Le code existait avant le standard”  
❌ “C’est trop long à refactorer maintenant”  
❌ “L’IA a généré ce code”  
❌ “Ça fonctionne comme ça depuis longtemps”  
❌ “On corrigera plus tard sans ticket”  
❌ “La CI laisse passer”

👉 Tous ces cas sont des **violations**, pas des exceptions.

---

## 4. Cas typiques où une exception PEUT être envisagée

Les exceptions acceptables sont **rares et encadrées**.

### 4.1 Code vendor / tiers non maîtrisé

Exemples :

- SDK fabricant
- HAL propriétaire
- code livré sous forme imposée

Conditions minimales :

- code strictement isolé (sous-dossier dédié)
- aucune fuite dans l’API publique
- aucun impact sur la taxonomie

---

### 4.2 Dette technique transitoire planifiée

Exemples :

- composant en cours de refactor lourd
- migration progressive (extraction `lib_*`, etc.)

Conditions minimales :

- plan de sortie clair
- échéance définie
- périmètre limité

---

### 4.3 Contraintes techniques temporaires

Exemples :

- bug ESP-IDF connu
- limitation toolchain
- contrainte matérielle transitoire

Conditions minimales :

- référence externe documentée
- workaround clairement identifié
- suppression prévue

---

## 5. Cas où une exception n’est généralement pas acceptable (selon le standard)

Référence normative : `docs/standard.md` (mécanisme de dérogation et limites).

❌ Violation de la taxonomie (`drv` avec logique métier)  
❌ Dépendance micro-ROS hors `mw_*`  
❌ API publique non namespacée  
❌ Exposition de structures internes  
❌ Absence de tests unitaires  
❌ Suppression d’un `basic_app`  
❌ Exception “permanente”

Ces points touchent au **cœur du standard** et ne sont **jamais négociables**.

---

## 6. Processus formel d’exception

Processus recommandé pour une exception **traçable** (conforme à l’esprit du standard) :

### 6.1 Document d’exception obligatoire

Chaque exception est décrite dans un fichier dédié :

```

docs/exceptions/<component_id>.md

````

Aucune exception globale, aucune exception orale.

---

### 6.2 Contenu obligatoire du document d’exception

```md
# Exception — <component_id>

## Règle(s) concernée(s)
- STD-XXX-YYY

## Description de la déviation
Description factuelle et précise.

## Justification technique
Pourquoi cette règle ne peut pas être respectée maintenant.

## Périmètre impacté
- fichiers concernés
- API impactée (oui/non)
- utilisateurs impactés (oui/non)

## Durée de validité
- Date de début
- Date de fin OU condition de levée

## Plan de retour à la conformité
Étapes concrètes, ordonnées.

## Responsable
Nom / rôle

## Statut
- [ ] active
- [ ] levée
````

---

### 6.3 Validation

Une exception :

Points attendus pour une exception correctement gérée :
- validation au niveau architecture
- visibilité pour la CI (si applicable)
- revue périodique (date d’expiration ou checkpoint)

Sans validation explicite → **exception invalide**.

---

## 7. Relation avec la CI

Par défaut :

> **La CI ignore les exceptions.**

Une exception :

* **n’autorise pas** la CI à ignorer une règle
* **n’implique pas** de désactiver un check

Si la CI doit être adaptée temporairement :

* cela doit être documenté
* cela doit être tracé
* cela doit être strictement limité

---

## 8. Exceptions et agents IA

Les agents IA :

* **NE PEUVENT PAS créer d’exception**
* **NE PEUVENT PAS invoquer une exception implicite**
* **DOIVENT respecter le standard par défaut**

Toute exception doit être **explicitement visible dans le dépôt**.

Références :

* `STD-AI-001`
* `STD-AI-002`

---

## 9. Fin de vie d’une exception

Une exception prend fin quand :

* la date est atteinte
* la condition de levée est remplie
* le plan de retour est exécuté

À la fin :

* le document est conservé (historique)
* le statut passe à `levée`
* toute tolérance associée est supprimée

---

## 10. Anti-patterns liés aux exceptions

❌ Exception non documentée
❌ Exception sans date de fin
❌ Exception globale (“pour tout le projet”)
❌ Exception héritée sans revalidation
❌ Exception utilisée comme excuse d’inaction

---

## 📌 Conclusion

Les exceptions sont :

* un **outil de transition**
* un **signal de dette technique**
* jamais une normalité

Le standard reste immuable.

> 📘 `docs/standard.md` est la loi
> ⚠️ `exceptions.md` décrit uniquement comment **s’en écarter temporairement sans trahir l’architecture**

---

**Version** : 1.0
**Dernière mise à jour** : 2026-01-27