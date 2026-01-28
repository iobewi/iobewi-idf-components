> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.

# 🔒 Patterns API — Guide d’implémentation
## Annexe au standard `iobewi-idf-components`

> **Statut : INFORMATIF**
> Ce document **n’introduit aucune règle normative**.
> Il décrit des patterns recommandés pour satisfaire `docs/standard.md` :
> - `STD-API-*` (contrats d’API)
> - `STD-MEM-*` (robustesse)
> - `STD-STR-*` (structure)

---

## 1. Handle opaque (pattern d’encapsulation)

### Objectif

Le handle opaque permet :
- encapsulation stricte (zéro champ interne accessible)
- évolution de l’implémentation sans casser l’ABI
- réduction des dépendances (types privés confinés dans le `.c`)

### Implémentation type

#### Dans `<component_api>_types.h`

```c
/**
 * @brief Handle opaque du composant.
 */
typedef struct <component_api>_s <component_api>_t;
````

#### Dans `<component_api>.h`

```c
esp_err_t <component_api>_new(
    const <component_api>_config_t *config,
    <component_api>_t **out
);

esp_err_t <component_api>_del(
    <component_api>_t *handle
);
```

#### Dans `<component_api>.c`

```c
struct <component_api>_s {
    // Champs privés
    int example_field;
    void *internal_data;
};

esp_err_t <component_api>_new(const <component_api>_config_t *config,
                             <component_api>_t **out)
{
    if (config == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    <component_api>_t *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // init...
    *out = handle;
    return ESP_OK;
}

esp_err_t <component_api>_del(<component_api>_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // cleanup...
    free(handle);
    return ESP_OK;
}
```

---

## 2. Pattern “2 headers publics” (types vs API)

### Objectif

Séparer :

* contrat de données (`*_types.h`)
* contrat opérationnel (`*.h`)

Bénéfices :

* includes plus légers
* propagation maîtrisée des dépendances
* meilleure lisibilité de l’API publique

### Contenu type de `<component_api>_types.h`

* typedef handle opaque
* enums publics
* structs de config publiques
* structs “data” publiques
* aucune structure privée

### Contenu type de `<component_api>.h`

* includes uniquement via `#include "<component_api>/<component_api>_types.h"`
* fonctions publiques `esp_err_t`
* pas de variable globale publique
* doc Doxygen minimale mais complète

---

## 3. Conventions de nommage des symboles publics

### Objectif

Éliminer collisions et ambiguïtés :

* tous les symboles publics préfixés `<component_api>_`
* types et enums clairement namespacés

### Exemples recommandés

```c
// Fonctions
esp_err_t drv_a02yyuw_read_mm(drv_a02yyuw_t *h, uint16_t *out_mm);

// Types
typedef struct {
    int uart_port;
} drv_a02yyuw_config_t;

// Enums / defines
typedef enum {
    DRV_A02YYUW_MODE_REALTIME = 0,
    DRV_A02YYUW_MODE_PROCESSED = 1,
} drv_a02yyuw_mode_t;

#define DRV_A02YYUW_MAX_SENSORS 8
```

Anti-patterns courants :

* `config_t`, `init()`, `read()` sans préfixe
* PascalCase (sauf types ROS)
* export de structs internes

---

## 4. Pattern `config_init()` (qualité d’intégration)

### Quand l’utiliser

Utile quand :

* config non triviale
* plusieurs champs et valeurs par défaut

### Signature recommandée

```c
esp_err_t <component_api>_config_init(<component_api>_config_t *config);
```

Comportement attendu :

* `ESP_ERR_INVALID_ARG` si `config == NULL`
* initialise des valeurs par défaut stables

---

## 5. Robustesse : règles de codage utiles (opérationnel)

* valider systématiquement arguments en entrée
* éviter les effets de bord avant validations/allocation OK
* sur erreur : rollback / pas d’état partiellement modifié
* il est recommandé que `del()` puisse être appelé après init partielle (si new échoue après allocation)

---

## 🔗 Références

* `docs/standard.md` : `STD-API-*`, `STD-MEM-*`, `STD-STR-*`
* `docs/annexes/testing.md` : tests des contrats API / robustesse mémoire
