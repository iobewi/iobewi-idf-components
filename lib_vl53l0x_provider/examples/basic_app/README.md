# Exemple basic_app - lib_vl53l0x_provider

Exemple minimal d'utilisation du provider VL53L0X multi-capteurs.

## Prérequis

### Matériel

- ESP32-S3 (ou ESP32 avec I2C)
- 1-8x capteurs VL53L0X (pololu/ST)
- GPIO pour XSHUT (power control)
- GPIO pour INT (data ready IRQ)

### Logiciels

- ESP-IDF 5.x ou 6.x

## Compilation

```bash
cd lib_vl53l0x_provider/examples/basic_app
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## Configuration

Adapter les GPIO dans `main/main.c` selon votre câblage.

## Résultat Attendu

```
I (xxxx) lib_vl53l0x_provider: Init I2C bus...
I (xxxx) lib_vl53l0x_provider: Assign addresses (multi XSHUT)...
I (xxxx) lib_vl53l0x_provider: Init 4 devices...
I (xxxx) lib_vl53l0x_provider: VL53 provider started (4 sensors)
I (xxxx) APP: Sensor[0]: 1.25 m
I (xxxx) APP: Sensor[1]: 2.30 m
I (xxxx) APP: Sensor[2]: 0.85 m
I (xxxx) APP: Sensor[3]: INVALID (status=255)
```
