> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.

# 📁 Template — Nouveau composant (structure + fichiers)

## 1) Arborescence

```
components/<component_id>/
├── CMakeLists.txt
├── idf_component.yml
├── README.md
├── include/<component_api>/
│   ├── <component_api>_types.h
│   └── <component_api>.h
└── src/
    └── <component_api>.c

examples/<component_id>/
└── basic_app/
    ├── CMakeLists.txt
    ├── sdkconfig.defaults
    └── main/
        ├── CMakeLists.txt
        └── main.c
```

## 2) `include/<component_api>/<component_api>_types.h`

```c
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct <component_api>_s <component_api>_t;

typedef struct {
    // TODO: config publique (si applicable)
} <component_api>_config_t;

#ifdef __cplusplus
}
#endif
```

## 3) `include/<component_api>/<component_api>.h`

```c
#pragma once

#include "<component_api>/<component_api>_types.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t <component_api>_config_init(<component_api>_config_t *config);

esp_err_t <component_api>_new(const <component_api>_config_t *config,
                             <component_api>_t **out);

esp_err_t <component_api>_del(<component_api>_t *handle);

#ifdef __cplusplus
}
#endif
```

## 4) `src/<component_api>.c`

```c
#include "<component_api>/<component_api>.h"
#include <stdlib.h>

struct <component_api>_s {
    // TODO: champs privés
};

esp_err_t <component_api>_config_init(<component_api>_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // TODO: defaults stables
    return ESP_OK;
}

esp_err_t <component_api>_new(const <component_api>_config_t *config,
                             <component_api>_t **out)
{
    if (config == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    <component_api>_t *h = calloc(1, sizeof(*h));
    if (h == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // TODO: init interne

    *out = h;
    return ESP_OK;
}

esp_err_t <component_api>_del(<component_api>_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // TODO: cleanup
    free(handle);
    return ESP_OK;
}
```

## 5) `components/<component_id>/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "src/<component_api>.c"
    INCLUDE_DIRS "include"
    REQUIRES
    PRIV_REQUIRES
)
```

## 6) `examples/<component_id>/basic_app/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(basic_app)
```

## 7) `examples/<component_id>/basic_app/main/main.c`

```c
#include "<component_api>/<component_api>.h"
#include "esp_log.h"

static const char *TAG = "basic_app";

void app_main(void)
{
    <component_api>_config_t cfg;
    if (<component_api>_config_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "config_init failed");
        return;
    }

    <component_api>_t *h = NULL;
    esp_err_t err = <component_api>_new(&cfg, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "new failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "component init OK");

    (void)<component_api>_del(h);
}