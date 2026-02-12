# Tests Unitaires TS_SYNC (Offline)

Tests host-side pour le module `app_hls_player_ts_sync` (pur, 0 dépendances).

## Tests Couverts

### `hls_ts_find_next_sync()`

1. **Cas nominal** : 3 paquets TS alignés → sync trouvé à offset 0
2. **Offset** : 10 bytes bruit + 3 paquets → sync trouvé à offset 10
3. **Faux positif** : 0x47 isolé + bruit + paquet TS → ignore faux positif
4. **Not found** : buffer sans sync → retourne false

### `hls_ts_resync_smart()`

1. **Nominal** : bruit + paquet PID + PUSI=1 + PES start → resync OK
2. **Mauvais PID** : PID différent → resync échoue
3. **Pas de PUSI** : PUSI=0 → resync échoue
4. **Pas de PES start** : PES start code corrompu → resync échoue

## Usage

### Build + Run

```bash
cd test/
make test
```

Output attendu :
```
=========================================
Tests Unitaires TS_SYNC (Offline)
=========================================

=== TEST: find_sync_nominal ===
PASS: Sync trouvé à offset 0 (aligné)

=== TEST: find_sync_offset ===
PASS: Sync trouvé à offset 10 (bruit initial)

...

=========================================
TOUS LES TESTS PASSÉS ✓
=========================================
```

### Clean

```bash
make clean
```

## Architecture

Les tests incluent directement le `.c` du module TS_SYNC pour éviter de linker toute l'ESP-IDF.

**Stubs fournis** :
- `ESP_LOGx()` → printf (pas de dépendance ESP-IDF)

**Helpers internes** :
- `generate_ts_packet()` : génère un paquet TS minimal (188 bytes)
- `generate_ts_packets()` : génère N paquets consécutifs

## Intégration CI/CD

Pour intégrer dans un pipeline CI :

```yaml
# .gitlab-ci.yml ou .github/workflows/ci.yml
test_ts_sync:
  script:
    - cd components/iobewi_apps_hls_player/test
    - make test
```

## Extension Future

Pour tester avec Unity framework (ESP-IDF) :

1. Créer `test_app/` avec Unity
2. Utiliser `TEST_CASE()` macro
3. Run avec `idf.py test`

Voir : https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html
