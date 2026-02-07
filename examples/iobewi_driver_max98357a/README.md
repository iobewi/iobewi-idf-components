# Exemples MAX98357A

Ce répertoire contient des exemples d'utilisation du driver `iobewi_driver_max98357a`.

## Exemples disponibles

### 1. basic_app

Exemple basique démontrant les fonctionnalités de base du driver:
- Initialisation du driver I2S
- Contrôle enable/disable via SD_MODE
- Écriture de données audio (silence)
- Nettoyage des ressources

**Utilisation:**
```bash
cd examples/iobewi_driver_max98357a/basic_app
idf.py set-target esp32s3  # ou votre cible
idf.py menuconfig          # Optionnel: configuration
idf.py build flash monitor
```

**Configuration matérielle requise:**
- MAX98357A connecté aux GPIO I2S
- Haut-parleur connecté à la sortie du MAX98357A
- Voir README du composant pour le schéma de connexion

### 2. audio_tone_app

Exemple avancé démontrant la génération et lecture audio:
- Génération de signaux sinusoïdaux (tones)
- Lecture de différentes fréquences (notes de musique)
- Contrôle de l'amplitude
- Séquences musicales simples
- Signal de test à 1 kHz

**Utilisation:**
```bash
cd examples/iobewi_driver_max98357a/audio_tone_app
idf.py set-target esp32s3
idf.py build flash monitor
```

**Ce que vous entendrez:**
1. Gamme musicale montante (Do à Do, 8 notes)
2. Signal de test à 1 kHz pendant 2 secondes
3. Démonstration de différentes amplitudes (440 Hz)
4. Mélodie simple (arpège Do majeur)

## Configuration des GPIO

Les exemples utilisent les GPIO suivants par défaut (ESP32-S3):

| Signal | GPIO | Description |
|--------|------|-------------|
| BCLK   | 14   | I2S Bit Clock |
| WS     | 15   | I2S Word Select (LRCLK) |
| DOUT   | 16   | I2S Data Out (vers DIN du MAX98357A) |
| SD_MODE| 17   | Shutdown control (optionnel) |

**Important:** Adaptez ces GPIO selon votre matériel en modifiant les fichiers `main.c`.

## Configuration audio par défaut

Les exemples utilisent:
- **Sample rate:** 16 kHz (approprié pour la voix)
- **Bits per sample:** 16 bits
- **Mode:** Mono
- **Gain:** 9dB (défaut matériel)
- **DMA buffers:** 6 buffers de 512 échantillons

Pour de la musique haute qualité, modifiez le `sample_rate` à 44100 ou 48000 Hz dans le code.

## Schéma de connexion

```
ESP32-S3              MAX98357A
---------            ----------
GPIO 14  ----------> BCLK
GPIO 15  ----------> LRCLK
GPIO 16  ----------> DIN
GPIO 17  ----------> SD_MODE (optionnel)
3.3V/5V  ----------> VIN
GND      ----------> GND
                     GAIN (laisser flottant pour 9dB)
                     OUT+ -----> Haut-parleur +
                     OUT- -----> Haut-parleur -
```

**Notes:**
- Utilisez un haut-parleur 4Ω ou 8Ω
- Alimentation recommandée: 5V pour puissance maximale
- Le GAIN est fixé au power-up (flottant = 9dB)

## Dépannage

### Pas de son
1. Vérifier les connexions (BCLK, LRCLK, DIN)
2. Vérifier que SD_MODE est à HIGH (si utilisé)
3. Vérifier l'alimentation du MAX98357A
4. Vérifier que le haut-parleur est bien connecté

### Son très faible
1. Augmenter l'amplitude dans le code (parameter `amplitude`)
2. Augmenter le voltage d'alimentation (utiliser 5V au lieu de 3.3V)
3. Vérifier le gain matériel (pin GAIN du MAX98357A)

### Son distordu
1. Réduire l'amplitude dans le code
2. Vérifier que le signal PCM n'est pas saturé
3. Utiliser un haut-parleur de plus forte impédance (8Ω au lieu de 4Ω)

### Coupures audio
1. Augmenter `dma_buf_count` (ex: 8)
2. Augmenter `dma_buf_len` (ex: 1024)
3. Réduire la charge CPU

## Prochaines étapes

Après avoir testé ces exemples, vous pouvez:
1. Intégrer le driver dans votre application
2. Implémenter la lecture de fichiers WAV
3. Ajouter des effets audio (égaliseur, filtres)
4. Connecter à un microphone pour applications bidirectionnelles

## Documentation

Pour plus d'informations:
- Voir `components/iobewi_driver_max98357a/README.md`
- Datasheet MAX98357A: https://datasheets.maximintegrated.com/en/ds/MAX98357A-MAX98357B.pdf
- Documentation ESP-IDF I2S: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html
