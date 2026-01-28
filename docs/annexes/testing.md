> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.

# 🧪 Tests unitaires — Guide d’implémentation

## Annexe au standard `iobewi-idf-components`

> **Statut : INFORMATIF**
> Ce document **n’introduit aucune règle normative**.
> Il explique **comment satisfaire** les exigences définies dans `docs/standard.md`, notamment :
>
> * `STD-TST-*` (tests unitaires)
> * `STD-API-*` (contrats d’API)
> * `STD-MEM-*` (robustesse mémoire)
> * `STD-TAX-*` (séparation des responsabilités)

---

## 1. Rôle des tests unitaires (rappel)

Les tests unitaires servent à :

* valider les **contrats d’API**
* garantir la **robustesse mémoire**
* détecter toute **régression fonctionnelle**
* permettre un **refus automatique en CI**

👉 Les règles opposables sont définies exclusivement dans `docs/standard.md`.
👉 Cette annexe décrit **comment les mettre en œuvre concrètement**.

---

## 2. Ce qu’est (et n’est pas) un test unitaire

### ✅ Test unitaire (dans ce framework)

Un test unitaire est :

* **isolé**
* **déterministe**
* **sans matériel réel**
* **sans timing réel**
* avec dépendances **mockées ou simulées**

### ❌ Ce qui n’est PAS un test unitaire

* accès I2C / SPI / UART réel
* dépendance à un capteur branché
* `vTaskDelay()` non mocké
* test nécessitant une carte cible
* dépendance à l’ordre global d’exécution

👉 Ces cas relèvent de l’intégration ou du HIL (hors scope).

---

## 3. Où placer les tests

### Structure recommandée

```
components/<component_id>/
├── test/
│   ├── CMakeLists.txt
│   ├── test_<component_api>_api.c
│   ├── test_<component_api>_memory.c
│   └── mocks/
│       ├── mock_gpio.c
│       ├── mock_i2c.c
│       ├── mock_uart.c
│       └── ...
```

Principes :

* `test_*.c` : scénarios ciblés
* `mocks/` : dépendances simulées
* éviter de placer des tests dans `src/`

---

## 4. Typologie de tests à écrire

### 4.1 Tests de contrats API (prioritaires)

Pour **chaque fonction publique** :

* arguments `NULL`
* tailles ou valeurs hors plage
* appel dans un état invalide
* double init / double del
* codes `esp_err_t` corrects

Exemple :

```c
TEST_CASE("drv_xxx_new NULL args", "[drv_xxx]")
{
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        drv_xxx_new(NULL, NULL)
    );
}
```

---

### 4.2 Tests de robustesse mémoire

Objectif : valider les exigences `STD-MEM-*`.

Cas usuels :

* boucle `new()` / `del()` × N
* simulation d’échec d’allocation
* appel `del(NULL)`
* pas d’état partiellement modifié en cas d’erreur

---

### 4.3 Typologie de tests par catégorie de composant

Cette section décrit **les types de tests attendus selon la couche**, afin de respecter la taxonomie `drv_* / lib_* / mw_* / app_*`.

---

#### 4.3.1 `drv_*` — Drivers matériels

**Objectif**
Valider la logique bas niveau **indépendamment du hardware réel**.

**Tests unitaires attendus**

✅ **Parsing & protocoles**

* trames valides vs invalides
* checksums / CRC
* headers, longueurs, formats
* endianness et alignement

✅ **Transactions & registres**

* ordre des accès (write → read)
* masques de bits (set / clear)
* valeurs par défaut

✅ **État interne**

* initialisation correcte
* transitions d’état
* erreurs I/O simulées

✅ **Valeurs limites**

* bornes de registres
* valeurs out-of-range

**Mocks recommandés**

* I2C / SPI / UART
* GPIO

**À éviter**

* accès matériel réel
* délais réels (`vTaskDelay`, timers)

---

#### 4.3.2 `lib_*` — Bibliothèques utilitaires

**Objectif**
Valider la logique algorithmique **pure et réutilisable**.

**Tests unitaires attendus**

✅ **Transformations**

* filtrage (moyenne, médian…)
* normalisation, conversions
* enrichissement de structures

✅ **Flux de données**

* entrée → sortie déterministe
* buffers vides, pleins, tailles limites

✅ **Erreurs**

* propagation d’erreurs depuis `drv_*`
* stabilité des codes de retour

---

#### 4.3.3 `mw_*` — Middleware micro-ROS

**Objectif**
Valider l’intégration micro-ROS **sans dépendre du transport réel**.

**Tests unitaires attendus**

✅ **Mapping ROS**

* structures internes → messages ROS
* champs obligatoires présents
* `timestamp`, `frame_id` corrects

✅ **Publication simulée**

* publication avec données valides
* gestion d’échec de publication

✅ **Configuration**

* topics, QoS, paramètres
* valeurs par défaut appliquées

✅ **Résilience**

* transport indisponible
* executor non initialisé
* QoS incompatibles
* publish échoué sans crash

**Mocks recommandés**

* `rcl`, `rclc`, executor
* transport ROS

---

#### 4.3.4 `app_*` — Applications / logique métier

**Objectif**
Valider la logique métier et l’orchestration.

**Tests unitaires attendus**

✅ **Logique métier**

* séquences fonctionnelles
* conditions d’activation
* orchestration multi-modules

✅ **Machines à états**

* transitions valides
* états non souhaités inaccessibles
* invariants respectés

✅ **Configuration**

* lecture de paramètres
* réactions aux flags

✅ **Fail-safe & recovery**

* erreurs critiques simulées
* timeout logique
* retour à un état sûr
* redémarrage logique possible

---

## 5. Mocks : principes recommandés

* mocks **déterministes**
* pas de random non seedé
* injection via :

  * pointeurs de fonctions
  * wrappers ESP-IDF
* possibilité de **spies** pour vérifier :

  * ordre des appels
  * arguments transmis

---

## 6. Ce que la CI vérifie typiquement

Sans entrer dans le normatif, une CI standard :

* détecte l’absence de dossier `test/`
* compile les tests unitaires
* exécute les scénarios
* refuse toute régression détectée

👉 Les critères exacts sont définis **par la CI**, pas par cette annexe.

---

## 7. Erreurs courantes (retours terrain)

* tester des logs comme oracle → ❌
* mélanger tests unitaires et intégration → ❌
* mocks partiels non déterministes → ❌
* oublier les cas d’erreur → ❌
* dépendre du timing réel → ❌

---

## 8. Résumé opérationnel

Avant de considérer un composant comme prêt :

* [ ] contrats d’API testés
* [ ] cas d’erreur couverts
* [ ] robustesse mémoire validée
* [ ] aucun accès matériel réel
* [ ] exécution reproductible et déterministe

---

## 📌 Conclusion

Les tests unitaires sont :

* un **outil de conception**
* un **filet de sécurité**
* un **contrat vérifiable**

Ils ne servent pas à “faire joli”,
mais à garantir que le framework reste **modulaire, robuste et industriellement exploitable**.
