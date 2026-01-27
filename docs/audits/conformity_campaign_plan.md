# 🚀 Plan d'Exécution - Campagne de Remise aux Normes

**Période** : 6 semaines (Janvier-Février 2026)
**Référence** : [conformity_audit_2026-01-26.md](conformity_audit_2026-01-26.md)

---

## 📊 Vue d'Ensemble

### Composants par Statut

| Statut | Nombre | Composants |
|--------|--------|------------|
| ✅ **CONFORME** | 3 | `drv_a02yyuw`, `lib_a02_provider`, `app_scan_ultra` |
| ⚠️ **PARTIEL** | 3 | `drv_led_rgb`, `lib_status_led`, `mw_uros_core` |
| ❌ **NON-CONFORME** | 4 | `drv_vl53l0x`, `mw_scan_builder`, `mw_uros_transport_usb`, `app_scan_tof` |

### Progression Cible

```
ACTUEL        30% ████████████████████████████████
SPRINT 2      70% ██████████████████████████████████████████████████████████████████████
OBJECTIF     100% ████████████████████████████████████████████████████████████████████████████████████████████████████
```

---

## 📅 Planning Détaillé

### 🔴 Sprint 1 : Composants Critiques (P0)
**Durée** : 2 semaines | **Effort** : 7 jours

#### Semaine 1

**Jour 1-3 : Extraction `lib_vl53l0x_provider`**

```bash
# 1. Créer la structure du nouveau composant
mkdir -p lib_vl53l0x_provider/{include/lib_vl53l0x_provider,src,examples/basic_app/main}

# 2. Déplacer les fichiers
# - drv_vl53l0x/include/drv_vl53l0x/tof_provider.h → lib_vl53l0x_provider/
# - drv_vl53l0x/include/drv_vl53l0x/tof_config.h → lib_vl53l0x_provider/
# - drv_vl53l0x/include/drv_vl53l0x/tof_snapshot.h → lib_vl53l0x_provider/
# - drv_vl53l0x/src/tof_provider.c → lib_vl53l0x_provider/src/
# - drv_vl53l0x/src/tof_config.c → lib_vl53l0x_provider/src/
# - drv_vl53l0x/src/tof_snapshot.c → lib_vl53l0x_provider/src/

# 3. Créer les nouveaux headers
touch lib_vl53l0x_provider/include/lib_vl53l0x_provider/lib_vl53l0x_provider_types.h
touch lib_vl53l0x_provider/include/lib_vl53l0x_provider/lib_vl53l0x_provider.h

# 4. Renommer les fonctions (sed/refactoring)
# tof_provider_* → lib_vl53l0x_provider_*
# tof_config_* → lib_vl53l0x_provider_config_*
# tof_snapshot_* → lib_vl53l0x_provider_snapshot_*

# 5. Créer CMakeLists.txt
cat > lib_vl53l0x_provider/CMakeLists.txt << 'EOF'
idf_component_register(
    SRCS "src/lib_vl53l0x_provider.c"
    INCLUDE_DIRS "include"
    REQUIRES drv_vl53l0x
    PRIV_REQUIRES esp_timer
)
EOF

# 6. Créer README.md
touch lib_vl53l0x_provider/README.md

# 7. Créer exemple basic_app
# (copier depuis drv_vl53l0x/examples/basic_app et adapter)

# 8. Build test
cd lib_vl53l0x_provider/examples/basic_app
idf.py set-target esp32s3
idf.py build
```

**Livrables Jour 3** :
- ✅ Nouveau composant `lib_vl53l0x_provider/` créé
- ✅ Headers conformes : `lib_vl53l0x_provider_types.h` + `lib_vl53l0x_provider.h`
- ✅ Fonctions renommées : `lib_vl53l0x_provider_*`
- ✅ Build test réussi

---

#### Semaine 2

**Jour 4-5 : Refactorisation `drv_vl53l0x`**

```bash
# 1. Nettoyer les headers
cd drv_vl53l0x/include/drv_vl53l0x
rm tof_provider.h tof_config.h tof_snapshot.h

# 2. Créer drv_vl53l0x_types.h
touch drv_vl53l0x_types.h

# 3. Renommer les fonctions dans drv_vl53l0x.h
# vl53l0x_* → drv_vl53l0x_*
# vl53l0x_i2c_master_init → drv_vl53l0x_i2c_init
# vl53l0x_init → drv_vl53l0x_new
# vl53l0x_read_mm → drv_vl53l0x_read

# 4. Adapter le handle opaque
# vl53l0x_dev_t → drv_vl53l0x_t

# 5. Mettre à jour l'implémentation dans src/
cd ../../src
# Renommer toutes les fonctions internes

# 6. Mettre à jour CMakeLists.txt (vérifier REQUIRES)

# 7. Build test
cd ../examples/basic_app
idf.py build
```

**Livrables Jour 5** :
- ✅ `drv_vl53l0x` refactorisé en driver pur
- ✅ Headers conformes : `drv_vl53l0x_types.h` + `drv_vl53l0x.h`
- ✅ Fonctions renommées : `drv_vl53l0x_*`
- ✅ Pas de logique métier (extraction réussie vers lib)
- ✅ Build test réussi

---

**Jour 6-7 : Mise à jour `app_scan_tof`**

```bash
# 1. Mettre à jour les includes
cd app_scan_tof/src
# Remplacer :
# #include "drv_vl53l0x/tof_provider.h"
# par :
# #include "lib_vl53l0x_provider/lib_vl53l0x_provider.h"

# 2. Adapter les appels de fonctions
# tof_provider_* → lib_vl53l0x_provider_*

# 3. Mettre à jour CMakeLists.txt
# Ajouter REQUIRES lib_vl53l0x_provider

# 4. Build test
cd examples/basic_app
idf.py build
```

**Livrables Jour 7** :
- ✅ `app_scan_tof` mis à jour avec nouvelles dépendances
- ✅ Build test réussi
- ✅ Sprint 1 terminé

**🎯 Jalons Sprint 1** :
- Taxonomie respectée : `drv_vl53l0x` (driver pur) + `lib_vl53l0x_provider` (logique)
- Violation majeure résolue
- 3 composants fonctionnels

---

### 🟡 Sprint 2 : Harmonisation Nommage (P1)
**Durée** : 2 semaines | **Effort** : 8 jours

#### Semaine 3

**Jour 8-9 : Harmonisation `mw_scan_builder`**

```bash
# 1. Créer mw_scan_builder_types.h
cd mw_scan_builder/include/mw_scan_builder
touch mw_scan_builder_types.h

# 2. Extraire les types de mw_scan_builder.h → mw_scan_builder_types.h
# scan_builder_storage_t → mw_scan_builder_storage_t

# 3. Renommer les fonctions
# scan_builder_init → mw_scan_builder_init
# scan_builder_fill → mw_scan_builder_fill
# scan_builder_deinit → mw_scan_builder_deinit

# 4. Mettre à jour l'implémentation
cd ../../src
# Renommer toutes les fonctions

# 5. Mettre à jour l'exemple
cd ../examples/basic_app/main
# Adapter les appels de fonctions

# 6. Build test
cd ..
idf.py build
```

**Livrables Jour 9** :
- ✅ `mw_scan_builder` conforme (types + nommage)
- ✅ Build test réussi

---

**Jour 10-12 : Harmonisation `mw_uros_transport_usb`**

```bash
# 1. Renommer les headers
cd mw_uros_transport_usb/include/mw_uros_transport_usb
mv esp_usbcdc_transport.h mw_uros_transport_usb.h
mv esp_usbcdc_common.h mw_uros_transport_usb_types.h
rm esp_usbcdc_logging.h  # Ou renommer si nécessaire

# 2. Créer mw_uros_transport_usb_types.h
# Déplacer les types depuis mw_uros_transport_usb.h

# 3. Renommer les fonctions
# esp_usbcdc_tinyusb_init_once → mw_uros_transport_usb_init
# esp_usbcdc_logging_init → mw_uros_transport_usb_logging_init
# esp_usbcdc_open → mw_uros_transport_usb_open
# esp_usbcdc_close → mw_uros_transport_usb_close

# 4. Mettre à jour l'implémentation dans src/

# 5. Mettre à jour mw_uros_core qui utilise ce transport
cd ../../mw_uros_core/src
# Remplacer les includes et appels de fonctions

# 6. Build test des 2 composants
cd ../examples/basic_app
idf.py build
cd ../../mw_uros_transport_usb/examples/basic_app
idf.py build
```

**Livrables Jour 12** :
- ✅ `mw_uros_transport_usb` conforme (headers + nommage)
- ✅ `mw_uros_core` mis à jour (dépendance)
- ✅ Build test réussi pour les 2 composants

---

#### Semaine 4

**Jour 13-14 : Harmonisation finale `app_scan_tof`**

```bash
# 1. Créer les nouveaux headers
cd app_scan_tof/include/app_scan_tof
touch app_scan_tof_types.h
touch app_scan_tof.h

# 2. Extraire les types de scan_engine.h → app_scan_tof_types.h
# scan_engine_t → app_scan_tof_t
# scan_config_t → app_scan_tof_config_t

# 3. Créer app_scan_tof.h avec les fonctions
# scan_engine_init → app_scan_tof_init
# scan_engine_deinit → app_scan_tof_fini
# scan_engine_step → app_scan_tof_step
# scan_engine_set_time_provider → app_scan_tof_set_time_provider

# 4. Supprimer scan_engine.h
rm scan_engine.h

# 5. Renommer le fichier source
cd ../../src
mv scan_engine.c app_scan_tof.c

# 6. Mettre à jour CMakeLists.txt
cd ..
# SRCS "src/app_scan_tof.c"

# 7. Mettre à jour l'exemple
cd examples/basic_app/main
# Remplacer includes et appels de fonctions

# 8. Build test
cd ..
idf.py build
```

**Livrables Jour 14** :
- ✅ `app_scan_tof` 100% conforme (headers + nommage)
- ✅ Build test réussi

---

**Jour 15 : Ajout types header `mw_uros_core`**

```bash
# 1. Créer mw_uros_core_types.h
cd mw_uros_core/include/mw_uros_core
touch mw_uros_core_types.h

# 2. Extraire les types de mw_uros_core.h → mw_uros_core_types.h
# uros_core_config_t
# uros_core_context_t
# uros_app_interface_t
# etc.

# 3. Mettre à jour mw_uros_core.h pour inclure mw_uros_core_types.h

# 4. Mettre à jour l'implémentation
cd ../../src
# Adapter les includes

# 5. Build test
cd ../examples/basic_app
idf.py build
```

**Livrables Jour 15** :
- ✅ `mw_uros_core` 100% conforme (types + api séparés)
- ✅ Build test réussi

**🎯 Jalons Sprint 2** :
- 4 composants harmonisés
- **Taux de conformité : 70% (7/10)** ✅

---

### 🟢 Sprint 3 : Audit Final (P2)
**Durée** : 1 semaine | **Effort** : 2 jours

**Jour 16 : Audit `drv_led_rgb`**

```bash
# 1. Vérifier CMakeLists.txt
cd drv_led_rgb
cat CMakeLists.txt
# Vérifier REQUIRES vs PRIV_REQUIRES

# 2. Vérifier taxonomie (pas de logique métier)
cd src
grep -n "filter\|median\|average\|compute" drv_led_rgb.c
# Doit être vide

# 3. Documenter conformité dans README.md
cd ..
# Ajouter section "Conformité CDC"

# 4. Build test
cd examples/basic_app
idf.py build
```

**Jour 17 : Audit `lib_status_led`**

```bash
# 1. Vérifier CMakeLists.txt
cd lib_status_led
cat CMakeLists.txt
# Vérifier REQUIRES drv_led_rgb

# 2. Vérifier taxonomie (pas d'accès matériel direct)
cd src
grep -n "gpio\|i2c\|spi\|uart" lib_status_led.c
# Doit utiliser seulement drv_led_rgb API

# 3. Documenter conformité dans README.md

# 4. Build test
cd examples/basic_app
idf.py build
```

**Jour 18 : Documentation globale**

```bash
# 1. Mettre à jour README.md principal
cd /workspaces/iobewi-idf-components
# Ajouter badge "100% Conformité CDC"

# 2. Créer CHANGELOG.md pour documenter les breaking changes
touch CHANGELOG.md

# 3. Mettre à jour docs/cdc.md si nécessaire

# 4. Créer docs/migration_guide.md pour aider les utilisateurs
touch docs/migration_guide.md
```

**🎯 Jalons Sprint 3** :
- Audit complet terminé
- **Taux de conformité : 100% (10/10)** ✅
- Documentation à jour

---

### 🔵 Sprint 4 : Tests et Validation
**Durée** : 1 semaine | **Effort** : 5 jours

**Jour 19 : Build test drivers**

```bash
for drv in drv_a02yyuw drv_led_rgb drv_vl53l0x; do
    echo "Building $drv..."
    cd /workspaces/iobewi-idf-components/$drv/examples/basic_app
    idf.py set-target esp32s3
    idf.py build || echo "FAILED: $drv"
done
```

**Jour 20 : Build test libraries**

```bash
for lib in lib_a02_provider lib_status_led lib_vl53l0x_provider; do
    echo "Building $lib..."
    cd /workspaces/iobewi-idf-components/$lib/examples/basic_app
    idf.py set-target esp32s3
    idf.py build || echo "FAILED: $lib"
done
```

**Jour 21 : Build test middleware**

```bash
for mw in mw_scan_builder mw_uros_core mw_uros_transport_usb; do
    echo "Building $mw..."
    cd /workspaces/iobewi-idf-components/$mw/examples/basic_app
    idf.py set-target esp32s3
    idf.py build || echo "FAILED: $mw"
done
```

**Jour 22 : Build test applications**

```bash
for app in app_scan_tof app_scan_ultra; do
    echo "Building $app..."
    cd /workspaces/iobewi-idf-components/$app/examples/basic_app
    idf.py set-target esp32s3
    idf.py build || echo "FAILED: $app"
done
```

**Jour 23 : Rapport final**

```bash
# 1. Générer rapport de conformité
# Script Python pour valider checklist sur tous les composants

# 2. Créer docs/conformity_report_final.md

# 3. Tag version v2.0.0
git tag -a v2.0.0 -m "Release v2.0.0: 100% CDC Conformity"
git push origin v2.0.0
```

**🎯 Jalons Sprint 4** :
- ✅ Tous les builds passent
- ✅ Rapport final de conformité
- ✅ Version v2.0.0 taggée

---

## 🛠️ Scripts d'Aide

### Script 1 : Vérification Structure Headers

```bash
#!/bin/bash
# check_headers.sh

for comp in drv_* lib_* mw_* app_*; do
    if [ ! -d "$comp" ]; then continue; fi

    echo "Checking $comp..."

    types_h="${comp}/include/${comp}/${comp}_types.h"
    api_h="${comp}/include/${comp}/${comp}.h"

    if [ -f "$types_h" ]; then
        echo "  ✅ ${comp}_types.h"
    else
        echo "  ❌ MISSING: ${comp}_types.h"
    fi

    if [ -f "$api_h" ]; then
        echo "  ✅ ${comp}.h"
    else
        echo "  ❌ MISSING: ${comp}.h"
    fi

    # Vérifier qu'il n'y a pas d'autres headers publics (sauf vendor)
    other_headers=$(find "${comp}/include/${comp}" -name "*.h" ! -name "${comp}_types.h" ! -name "${comp}.h" 2>/dev/null)
    if [ -n "$other_headers" ]; then
        echo "  ⚠️ EXTRA HEADERS:"
        echo "$other_headers" | sed 's/^/    /'
    fi

    echo ""
done
```

### Script 2 : Vérification Nommage Fonctions

```bash
#!/bin/bash
# check_naming.sh

for comp in drv_* lib_* mw_* app_*; do
    if [ ! -d "$comp" ]; then continue; fi

    echo "Checking $comp..."

    # Extraire les fonctions publiques
    funcs=$(grep -h "^esp_err_t\|^bool\|^void" "${comp}/include/${comp}/"*.h 2>/dev/null | \
            grep -o '[a-z_]*(' | sed 's/($//')

    if [ -z "$funcs" ]; then
        echo "  ⚠️ No public functions found"
        echo ""
        continue
    fi

    # Vérifier le préfixe
    incorrect=0
    while IFS= read -r func; do
        if [[ ! "$func" =~ ^${comp}_ ]] && [[ ! "$func" =~ ^uros_core_ ]]; then
            echo "  ❌ INCORRECT: $func (should start with ${comp}_)"
            incorrect=1
        fi
    done <<< "$funcs"

    if [ $incorrect -eq 0 ]; then
        echo "  ✅ All functions correctly prefixed"
    fi

    echo ""
done
```

### Script 3 : Build Test All

```bash
#!/bin/bash
# build_all.sh

TARGET="${1:-esp32s3}"
FAILED=""

for comp in drv_* lib_* mw_* app_*; do
    if [ ! -d "$comp/examples/basic_app" ]; then
        echo "Skipping $comp (no example)"
        continue
    fi

    echo "========================================="
    echo "Building $comp..."
    echo "========================================="

    cd "$comp/examples/basic_app"

    idf.py set-target $TARGET
    if ! idf.py build; then
        echo "❌ FAILED: $comp"
        FAILED="$FAILED\n  - $comp"
    else
        echo "✅ SUCCESS: $comp"
    fi

    cd ../../..
    echo ""
done

if [ -n "$FAILED" ]; then
    echo "========================================="
    echo "❌ BUILD FAILURES:"
    echo -e "$FAILED"
    echo "========================================="
    exit 1
else
    echo "========================================="
    echo "✅ ALL BUILDS SUCCESSFUL"
    echo "========================================="
fi
```

---

## 📝 Checklist par Sprint

### Sprint 1 Checklist

- [ ] `lib_vl53l0x_provider/` créé
- [ ] Headers conformes : `lib_vl53l0x_provider_types.h` + `.h`
- [ ] Fonctions renommées : `lib_vl53l0x_provider_*`
- [ ] `drv_vl53l0x` refactorisé (driver pur)
- [ ] Headers conformes : `drv_vl53l0x_types.h` + `.h`
- [ ] Fonctions renommées : `drv_vl53l0x_*`
- [ ] `app_scan_tof` mis à jour
- [ ] Build tests réussis (3 composants)

### Sprint 2 Checklist

- [ ] `mw_scan_builder` : types header + renommage
- [ ] `mw_uros_transport_usb` : headers + renommage
- [ ] `mw_uros_core` mis à jour (dépendance transport)
- [ ] `app_scan_tof` : headers + renommage final
- [ ] `mw_uros_core` : types header
- [ ] Build tests réussis (4 composants)
- [ ] **Taux conformité 70%** atteint

### Sprint 3 Checklist

- [ ] Audit `drv_led_rgb` (CMakeLists + taxonomie)
- [ ] Audit `lib_status_led` (CMakeLists + taxonomie)
- [ ] Documentation CDC mise à jour
- [ ] CHANGELOG.md créé
- [ ] Migration guide créé
- [ ] **Taux conformité 100%** atteint

### Sprint 4 Checklist

- [ ] Build test tous les drivers
- [ ] Build test toutes les libs
- [ ] Build test tous les middleware
- [ ] Build test toutes les apps
- [ ] Rapport final généré
- [ ] Version v2.0.0 taggée

---

## 🎯 KPIs de Suivi

| KPI | Actuel | Sprint 2 | Sprint 3 | Sprint 4 |
|-----|--------|----------|----------|----------|
| Conformité globale | 30% | 70% | 100% | 100% |
| Headers types séparés | 5/10 | 9/10 | 10/10 | 10/10 |
| Nommage correct | 5/10 | 9/10 | 10/10 | 10/10 |
| Taxonomie respectée | 7/10 | 10/10 | 10/10 | 10/10 |
| Build tests OK | 10/10 | 10/10 | 10/10 | 10/10 |

---

## 📞 Support

Pour questions sur ce plan :
- 📚 Lire [conformity_audit_2026-01-26.md](conformity_audit_2026-01-26.md)
- 📖 Consulter [component_best_practices.md](component_best_practices.md)
- 💬 Ouvrir une discussion sur le dépôt

---

**Créé le** : 2026-01-26
**Version** : 1.0
