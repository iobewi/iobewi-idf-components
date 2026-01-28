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

Ce document décrit **comment gérer une déviation au standard**,  
dans un cadre contrôlé, traçable et temporaire.

Il n’a **aucune valeur normative**.  
Toute règle opposable reste exclusivement définie dans `docs/standard.md`.

---

## 1. Principe fondamental

> **Le standard ne s’adapte jamais à une implémentation.**  
> Une implémentation peut, dans certains cas exceptionnels, s’en écarter temporairement.

Une exception est généralement :
- rare
- temporaire
- explicitement documentée
- traçable
- réversible

Une déviation non documentée ou non justifiée est considérée comme une **violation du standard**, et non comme une exception.

---

## 2. Définition d’une exception (au sens du standard)

Une **exception** correspond à une **déviation volontaire et consciente** à une règle `STD-*`, acceptée temporairement pour des raisons techniques justifiées.

Une exception :
- ne crée pas de précédent
- ne modifie pas le standard
- n’est jamais implicite
- cesse d’exister sans justification active

---

## 3. Situations qui ne constituent pas une exception

Les situations suivantes sont **considérées comme des violations**, et non comme des exceptions :

- “Le code existait avant le standard”
- “C’est trop long à refactorer maintenant”
- “L’IA a généré ce code”
- “Ça fonctionne comme ça depuis longtemps”
- “On corrigera plus tard sans suivi”
- “La CI laisse passer”

Ces cas n’impliquent aucune acceptation explicite d’une déviation par rapport au standard.

---

## 4. Cas où une exception peut être envisagée

Les exceptions acceptables sont **rares et encadrées**.

### 4.1 Code vendor / tiers non maîtrisé

Exemples :
- SDK fabricant
- HAL propriétaire
- code livré sous forme imposée

Bonnes pratiques généralement attendues :
- isolation stricte dans un sous-dossier dédié
- aucune exposition dans l’API publique
- aucun impact sur la taxonomie des composants

---

### 4.2 Dette technique transitoire planifiée

Exemples :
- composant en cours de refactorisation
- migration progressive (`drv_*` → `lib_*`, etc.)

Bonnes pratiques attendues :
- plan de sortie identifié
- échéance ou condition de levée définie
- périmètre limité et explicite

---

### 4.3 Contraintes techniques temporaires

Exemples :
- bug ESP-IDF connu
- limitation de toolchain
- contrainte matérielle transitoire

Bonnes pratiques attendues :
- référence externe documentée
- workaround clairement identifié
- suppression prévue

---

## 5. Cas généralement incompatibles avec une exception

Certains écarts touchent au **cœur du standard** et sont généralement considérés comme non acceptables.

Référence normative : `docs/standard.md`.

Exemples typiques :
- violation de la taxonomie (`drv_*` contenant de la logique métier)
- dépendance micro-ROS hors `mw_*`
- API publique non namespacée
- exposition de structures internes
- absence de tests unitaires
- suppression d’un `basic_app`
- exception sans horizon de sortie

Ces situations sont considérées par le standard comme des violations structurelles.

---

## 6. Gestion documentaire des exceptions (recommandée)

Pour assurer la traçabilité, il est recommandé de documenter chaque exception dans un fichier dédié, versionné dans le dépôt.

### Convention courante (recommandée)

```

docs/exceptions/<component_id>.md

````

Cette convention facilite l’audit et l’automatisation, mais le standard n’impose ni format ni emplacement unique.

---

### Contenu recommandé d’un document d’exception

```md
# Exception — <component_id>

## Règle(s) concernée(s)
- STD-XXX-YYY

## Description de la déviation
Description factuelle et précise.

## Justification technique
Pourquoi la règle ne peut pas être respectée actuellement.

## Périmètre impacté
- fichiers concernés
- API impactée (oui/non)
- utilisateurs impactés (oui/non)

## Durée de validité
- Date de début
- Date de fin ou condition de levée

## Plan de retour à la conformité
Étapes concrètes et ordonnées.

## Responsable
Nom / rôle

## Statut
- active
- levée
````

---

## 7. Relation avec la CI

Par défaut, les outils CI appliquent le standard **sans tenir compte des exceptions**.

Une exception :

* ne modifie pas automatiquement le comportement de la CI
* n’implique pas la désactivation implicite d’un contrôle

Si un ajustement CI est nécessaire :

* il est documenté
* il est traçable
* il est limité dans le temps

---

## 8. Exceptions et agents IA

Par conception :

* les agents IA n’initient pas d’exception
* les agents IA n’invoquent pas d’exception implicite
* les agents IA appliquent le standard par défaut

Toute exception applicable est idéalement **explicitement visible dans le dépôt**.

Références normatives associées :

* `STD-AI-001`
* `STD-AI-002`

---

## 9. Fin de vie d’une exception

Une exception est considérée comme levée lorsque :

* sa date de validité est atteinte
* la condition de levée est remplie
* le plan de retour est exécuté

Le document est conservé à des fins d’historique, avec un statut mis à jour.

---

## 10. Anti-patterns liés aux exceptions

Exemples d’usages incorrects :

* exception non documentée
* exception sans horizon de sortie
* exception globale au projet
* exception héritée sans réévaluation
* exception utilisée comme substitut à une correction

---

## 📌 Conclusion

Les exceptions sont :

* un **outil de transition**
* un **signal de dette technique**
* jamais un état normal

Le standard reste la référence immuable.

> 📘 `docs/standard.md` est la seule autorité normative
> ⚠️ `exceptions.md` décrit uniquement comment s’en écarter temporairement sans affaiblir l’architecture
