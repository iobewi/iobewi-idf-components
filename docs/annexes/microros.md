# 🤖 micro-ROS — Guide d’implémentation

## Annexe au standard `iobewi-idf-components`

> **Statut : INFORMATIF**
> Ce document **n’introduit aucune règle normative**.
> Il explique **comment appliquer** les exigences définies dans `docs/standard.md`,
> notamment les sections **STD-TAX-***, **STD-UROS-***, **STD-API-***.

---

## 1. Rôle des composants `mw_*`

Les composants `mw_*` ont pour objectif de :

* adapter des données internes vers des **messages ROS 2**
* encapsuler l’usage de **micro-ROS (rcl / rclc)**
* fournir des **helpers génériques**, réutilisables
* isoler complètement micro-ROS du reste du framework

👉 Ils ne portent **aucune logique métier**.

---

## 2. Ce qu’un composant `mw_*` fait (et ne fait pas)

### ✅ Autorisé (rappel pratique)

* inclure `rcl`, `rclc`, `rmw_*`
* construire / remplir des messages ROS
* gérer :

  * QoS
  * time sync
  * allocateurs
* dépendre de `drv_*` et `lib_*`

### ❌ Interdit (rappel)

* accès matériel direct
* logique applicative
* orchestration métier
* paramètres hardcodés

👉 Ces interdits sont définis normativement dans `STD-UROS-*`.

---

## 3. Structure type d’un composant `mw_*`

```
components/iobewi_mw_<name>/
├── CMakeLists.txt
├── idf_component.yml
├── README.md
├── include/mw_<name>/
│   ├── mw_<name>_types.h
│   └── mw_<name>.h
└── src/
    └── mw_<name>.c
```

Points clés :

* l’API reste **générique**
* aucun message spécifique à un capteur particulier
* aucun topic hardcodé

---

## 4. Pattern courant : Builder de message ROS

### Exemple conceptuel

Un composant `mw_*` agit souvent comme un **builder** :

```c
mw_scan_builder_t *builder;
mw_scan_builder_init(&builder, &config);

mw_scan_builder_set_frame(builder, "base_link");
mw_scan_builder_add_range(builder, angle, distance);
mw_scan_builder_publish(builder);
```

Caractéristiques :

* pas de connaissance de la source des données
* pas de logique de filtrage
* pas de conversion métier

---

## 5. Gestion des dépendances micro-ROS

### Bonnes pratiques opérationnelles

* regrouper les includes micro-ROS dans le `.c`
* exposer des structures **ne contenant pas de types ROS** dans `_types.h`
* isoler les types ROS (`sensor_msgs__msg__*`) dans l’implémentation

👉 Cela limite l’impact des changements ROS sur l’API publique.

---

## 6. QoS, time sync et robustesse

### QoS

* exposer la configuration QoS via une structure
* fournir des valeurs par défaut raisonnables
* ne jamais imposer un profil figé

### Time sync

* déclencher explicitement la synchronisation
* prévoir un mode dégradé si la sync échoue
* ne jamais bloquer l’exécution principale

---

## 7. Gestion des erreurs (retours terrain)

Un composant `mw_*` robuste :

* ne crashe jamais si :

  * l’agent n’est pas disponible
  * la publication échoue
* retourne un `esp_err_t` exploitable
* laisse l’orchestrateur (`app_*`) décider de la stratégie

---

## 8. Tests unitaires pour `mw_*`

Rappel pratique :

* **pas de micro-ROS réel**
* `rcl`, `rclc`, executor **mockés**
* publication simulée
* mapping vérifié champ par champ

Exemples de cas testés :

* publish sans init
* publish avec transport indisponible
* message incomplet
* QoS incompatible

---

## 9. Erreurs fréquentes observées

* mélange logique métier / ROS
* hardcode de topic ou `frame_id`
* exposition directe de types ROS dans l’API
* dépendance micro-ROS dans `lib_*` ou `drv_*`

---

## 10. Résumé opérationnel

Avant de considérer un composant `mw_*` comme prêt :

* [ ] API générique, sans logique métier
* [ ] aucune dépendance matérielle directe
* [ ] QoS configurable
* [ ] gestion d’erreurs non bloquante
* [ ] tests unitaires avec ROS mocké

---

## 🔗 Références

* `docs/standard.md`

  * `STD-TAX-*`
  * `STD-UROS-*`
  * `STD-API-*`
* `docs/annexes/testing.md`

---

## 📌 Conclusion

Un bon composant `mw_*` est :

* **fin**
* **générique**
* **robuste**
* **transparent**

Il ne décide rien.
Il **adapte**.
