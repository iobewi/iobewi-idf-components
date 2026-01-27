# drv_fan_pwm basic_app

Exemple minimal d'utilisation de `drv_fan_pwm` (LEDC PWM) sur ESP32-S3.

## Câblage
- Branche le fil PWM du ventilateur sur le GPIO choisi (`gpio_num` dans `main.c`)
- Masse commune

## Build / Flash
```bash
idf.py set-target esp32s3
idf.py build flash monitor
