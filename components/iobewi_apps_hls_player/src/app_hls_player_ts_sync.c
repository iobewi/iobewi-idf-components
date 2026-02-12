/**
 * @file app_hls_player_ts_sync.c
 * @brief Implémentation du module de synchronisation MPEG-TS
 *
 * Module pur sans dépendances externes (hormis ESP_LOGD optionnel pour debug).
 * Les logs sont désactivés par défaut en production.
 */

#include "app_hls_player/app_hls_player_ts_sync.h"

// Log optionnel (ESP_LOGD désactivé par défaut en production)
#ifdef CONFIG_LOG_DEFAULT_LEVEL_DEBUG
    #include "esp_log.h"
    static const char *TAG = "hls_ts_sync";
    #define TS_LOGD(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
#else
    #define TS_LOGD(fmt, ...) do {} while(0)
#endif

bool hls_ts_resync_smart(const uint8_t *buf,
                         size_t len,
                         size_t scan_max,
                         uint16_t audio_pid,
                         size_t *out_skip)
{
    size_t n = (len < scan_max) ? len : scan_max;
    if (n < 188 * 3) return false; // Besoin 3 paquets TS minimum

    for (size_t i = 0; i + 188 * 2 < n; i++) {
        if (buf[i] != 0x47) continue;

        // Parse header TS
        uint8_t b1 = buf[i + 1];
        uint8_t b2 = buf[i + 2];
        uint8_t b3 = buf[i + 3];

        uint16_t pid = ((b1 & 0x1F) << 8) | b2;
        uint8_t pusi = (b1 & 0x40) ? 1 : 0;
        uint8_t afc  = (b3 & 0x30) >> 4;   // 1=payload, 2=adapt, 3=adapt+payload
        uint8_t cc0  = (b3 & 0x0F);

        // Filtres PID + PUSI + payload
        if (pid != audio_pid) continue;
        if (!pusi) continue;
        if (afc != 1 && afc != 3) continue; // Payload requis

        // Compute payload start (skip adaptation field si présent)
        size_t p = i + 4;
        size_t pkt_end = i + 188;  // Fin du paquet TS courant
        if (afc == 3) {
            if (p >= pkt_end) continue;  // Vérif AVANT lecture afl
            uint8_t afl = buf[p];
            p += 1 + afl;
            if (p >= pkt_end) continue;  // Payload doit rester dans paquet TS
        }
        if (p + 2 >= pkt_end) continue;  // PES start code (3 bytes) doit être dans paquet
        if (p + 2 >= n) continue;        // Vérif buffer global

        // Vérif start code PES (00 00 01)
        if (!(buf[p] == 0x00 && buf[p+1] == 0x00 && buf[p+2] == 0x01)) {
            continue;
        }

        // Valider next 2 packets : sync + au moins 1 PID audio
        const uint8_t *p1 = buf + i + 188;
        const uint8_t *p2 = buf + i + 376;
        if (p1[0] != 0x47 || p2[0] != 0x47) continue;

        // Au moins 1 des 2 paquets suivants doit avoir PID audio
        // (tolère PAT/PMT/autres, mais réduit faux positifs)
        uint16_t pid1 = ((p1[1] & 0x1F) << 8) | p1[2];
        uint16_t pid2 = ((p2[1] & 0x1F) << 8) | p2[2];
        if (pid1 != audio_pid && pid2 != audio_pid) continue;

        // Validation complète : PID + PUSI + PES + 3 sync + au moins 1 PID audio
        *out_skip = i;

        // Log debug optionnel (désactivé par défaut)
        TS_LOGD("resync_smart HIT at skip=%zu pid=%u pusi=%u afc=%u cc=%u",
                i, pid, pusi, afc, cc0);

        return true;
    }

    return false;
}

bool hls_ts_find_next_sync(const uint8_t *buf, size_t len, size_t *out_skip)
{
    for (size_t i = 0; i + 188 < len; i++) {
        if (buf[i] == 0x47 && buf[i + 188] == 0x47) {
            // Bonus: triple check si disponible
            if (i + 376 < len && buf[i + 376] != 0x47) {
                continue;  // Faux positif, continuer scan
            }
            *out_skip = i;
            return true;
        }
    }
    return false;
}
