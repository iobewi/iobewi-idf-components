# drv_vl53l0x - Driver VL53L0X ToF Sensor

Driver complet pour capteur de distance Time-of-Flight VL53L0X de STMicroelectronics.

## Rôle

Ce composant fournit un accès matériel complet au capteur VL53L0X, incluant l'API ST officielle et les abstractions ESP-IDF.

**Responsabilités:**
- API ST VL53L0X (API officielle du fabricant)
- Wrapper ESP-IDF pour I2C
- Configuration hardware multi-capteurs
- Providers d'abstraction (VL53, Mock)
- Snapshot de mesures ToF

**Ce que ce composant NE FAIT PAS:**
- Construction de messages ROS (délégué à `mw_scan_builder`)
- Orchestration du scan (délégué à `app_scan_tof`)
- Publication ROS (délégué à `mw_uros_core`)

## Dépendances

- `esp_driver_i2c` (I2C ESP-IDF)
- `esp_driver_gpio` (GPIO pour XSHUT)
- `freertos` (RTOS)
- `esp_timer` (timing)

## Conformité CDC

- ✅ Catégorie `drv_*` (driver matériel)
- ✅ Accès direct I2C/GPIO
- ✅ Pas de dépendance micro-ROS
- ✅ Pas de logique applicative
- ✅ API ST officielle incluse
- ✅ Structure CDC: `include/drv_vl53l0x/`, `src/`, `st_api/`, `examples/`

## License

- ST API: Propriétaire STMicroelectronics (voir st_api/)
- ESP-IDF wrapper: MIT
