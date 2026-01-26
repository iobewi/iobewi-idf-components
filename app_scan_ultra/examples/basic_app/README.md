# Exemple basic_app - app_scan_ultra

Exemple complet d'intégration micro-ROS pour publier des messages `sensor_msgs/LaserScan` à partir de 4 capteurs A02YYUW.

## 📋 Prérequis

### Matériel

- ESP32-S3 (ou ESP32-S2 avec USB-CDC)
- 4x capteurs A02YYUW (SEN0311)
- 4x STMPS2141STR (power switches)
- Câblage selon schéma du driver drv_a02yyuw

### Logiciels

- **ESP-IDF 5.x ou 6.x** ([Installation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/))
- **micro-ROS pour ESP-IDF** ([micro_ros_espidf_component](https://github.com/micro-ROS/micro_ros_espidf_component))
- **ROS 2** (Humble, Iron ou Jazzy) pour l'agent et RViz

## 🚀 Installation micro-ROS

### 1. Installer le composant micro-ROS

```bash
cd /path/to/iobewi-idf-components
git clone -b humble https://github.com/micro-ROS/micro_ros_espidf_component.git
```

### 2. Configurer le transport

Le transport USB-CDC est recommandé pour ESP32-S3 :

```bash
cd app_scan_ultra/examples/basic_app
idf.py menuconfig
```

Naviguer vers :
```
micro-ROS → Transport Settings → USB CDC
```

## 🔧 Compilation

### 1. Configurer la cible

```bash
cd app_scan_ultra/examples/basic_app
idf.py set-target esp32s3
```

### 2. Configurer les GPIO

```bash
idf.py menuconfig
```

Naviguer vers `Ultrasonic Scan App Configuration` et configurer :
- UART RX GPIO (défaut : 18)
- Mode GPIO (défaut : 5)
- EN GPIOs pour les 4 capteurs (défaut : 14, 10, 7, 4)
- Status LED GPIO (défaut : 38)

### 3. Compiler

```bash
idf.py build
```

### 4. Flasher

```bash
idf.py -p /dev/ttyUSB0 flash
```

## 📡 Lancer l'agent micro-ROS

Sur votre PC hôte (avec ROS 2 installé) :

```bash
# Installation de l'agent (première fois seulement)
sudo apt install ros-humble-micro-ros-agent

# Lancer l'agent
ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyUSB0 -b 115200
```

Vous devriez voir :
```
[1234567890.123456] (info) micro-ROS Agent connected successfully
[1234567890.234567] (info) session created
```

## 👁️ Visualisation RViz

### 1. Vérifier le topic

```bash
ros2 topic list
# Devrait afficher : /scan

ros2 topic echo /scan
```

### 2. Lancer RViz

```bash
ros2 run rviz2 rviz2
```

### 3. Configuration RViz

1. **Fixed Frame** : Changer de `map` à `base_link`
2. **Add** (bouton en bas à gauche) → **By topic** → `/scan` → **LaserScan**
3. Ajuster les paramètres :
   - **Size (m)** : 0.05
   - **Color Transformer** : Flat Color ou Intensity
   - **Color** : Rouge (255, 0, 0)

Vous devriez voir 4 points à 0°, 90°, 180° et 270° correspondant aux capteurs.

## 📊 Monitoring

### Topics ROS

```bash
# Lister les topics
ros2 topic list

# Afficher les messages
ros2 topic echo /scan

# Fréquence de publication
ros2 topic hz /scan
# Devrait afficher ~5 Hz

# Informations du topic
ros2 topic info /scan
```

### Logs ESP32

```bash
idf.py monitor
```

Vous devriez voir :
```
I (xxxx) app_scan_ultra: LaserScan published: 4/4 valid samples
I (xxxx) app_scan_ultra: Sensor[0] → bin[0] = 1.25 m
I (xxxx) app_scan_ultra: Sensor[1] → bin[9] = 2.50 m
...
```

## 🐛 Dépannage

### Agent ne se connecte pas

**Symptôme** : `Waiting for agent...` en boucle

**Solutions** :
1. Vérifier que l'agent est lancé : `ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyUSB0`
2. Vérifier le port série : `ls /dev/ttyUSB*` ou `/dev/ttyACM*`
3. Vérifier les permissions : `sudo chmod 666 /dev/ttyUSB0`
4. Réinitialiser l'ESP32 : bouton RESET

### Pas de données dans RViz

**Symptôme** : RViz ouvert mais rien ne s'affiche

**Solutions** :
1. Vérifier Fixed Frame = `base_link`
2. Vérifier que le topic `/scan` est visible : `ros2 topic list`
3. Vérifier la fréquence : `ros2 topic hz /scan`
4. Zoomer/dézoomer dans RViz (molette de la souris)
5. Ajouter un Grid pour référence visuelle

### Tous les bins à NAN

**Symptôme** : `ros2 topic echo /scan` montre tous les ranges à `nan`

**Solutions** :
1. Vérifier les logs ESP32 : `idf.py monitor`
2. Vérifier le câblage des capteurs (GPIO EN, UART RX, Mode)
3. Vérifier l'alimentation des capteurs (5V)
4. Tester avec `lib_a02_provider/examples/basic_app` d'abord

### Fréquence faible

**Symptôme** : `ros2 topic hz /scan` montre <5 Hz

**Solutions** :
1. Augmenter `timer_period_ms` dans main.c (défaut 200ms)
2. Vérifier que l'UART fonctionne à 9600 bps
3. Réduire `median_filter_size` à 1 (non recommandé)

## 📖 Voir aussi

- [Architecture micro-ROS](../../../docs/architecture_a02yyuw_microros.md)
- [README drv_a02yyuw](../../../drv_a02yyuw/README.md)
- [README lib_a02_provider](../../../lib_a02_provider/README.md)
- [micro-ROS documentation](https://micro.ros.org/)
