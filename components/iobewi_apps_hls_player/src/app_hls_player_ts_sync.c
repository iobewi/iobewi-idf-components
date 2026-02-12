/**
 * @file app_hls_player_ts_sync.c
 * @brief Implémentation du module de synchronisation MPEG-TS
 *
 * Module pur sans dépendances externes (hormis ESP_LOGD optionnel pour debug).
 * Les logs sont désactivés par défaut en production.
 */

#include "app_hls_player/app_hls_player_ts_sync.h"
#include "esp_log.h"
#include "freertos/ringbuf.h"
#include <string.h>

static const char *TAG = "hls_ts_sync";

// Log optionnel (ESP_LOGD désactivé par défaut en production)
#ifdef CONFIG_LOG_DEFAULT_LEVEL_DEBUG
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

int hls_drop_until_audio_pusi(void *ring_buffer, int max_drop,
                               uint16_t audio_pid, hls_held_ts_packet_t *out_held)
{
    if (ring_buffer == NULL || max_drop <= 0 || audio_pid == 0) {
        ESP_LOGE(TAG, "drop_until_pusi: params invalides (audio_pid doit être != 0)");
        return -1;
    }
    
    // Init out_held
    if (out_held != NULL) {
        out_held->has_packet = false;
        out_held->pid = 0;
    }
    
    RingbufHandle_t rb = (RingbufHandle_t)ring_buffer;
    int drop_count = 0;
    bool found_pusi = false;
    
    while (drop_count < max_drop) {
        size_t item_size = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceive(rb, &item_size, 0);
        
        if (item == NULL) {
            // Plus d'items disponibles dans le ringbuffer
            break;
        }
        
        // Vérifier taille TS (188 bytes attendu avec NOSPLIT)
        if (item_size != 188) {
            ESP_LOGW(TAG, "drop_until_pusi: item size=%zu != 188, skip", item_size);
            vRingbufferReturnItem(rb, item);
            drop_count++;
            continue;
        }
        
        // Vérifier sync byte 0x47
        if (item[0] != 0x47) {
            ESP_LOGD(TAG, "drop_until_pusi: sync byte != 0x47, skip");
            vRingbufferReturnItem(rb, item);
            drop_count++;
            continue;
        }
        
        // Extraire PUSI flag (bit 6 de byte[1])
        // PUSI = Payload Unit Start Indicator (début PES/PSI section)
        bool has_pusi = (item[1] & 0x40) != 0;
        
        // Extraire PID (13 bits sur bytes[1:2], bits 0-12)
        uint16_t pid = ((item[1] & 0x1F) << 8) | item[2];
        
        // Check si c'est le paquet PUSI du PID audio qu'on cherche
        if (has_pusi && pid == audio_pid) {
            // PUSI trouvé ! Conserver ce paquet (held pattern)
            // CRITIQUE : Ne PAS dropper ce paquet, il contient le début PES propre
            if (out_held != NULL) {
                memcpy(out_held->data, item, 188);
                out_held->pid = pid;
                out_held->has_packet = true;
            }
            
            found_pusi = true;
            vRingbufferReturnItem(rb, item);  // Libérer l'item du ringbuffer
            
            // NE PAS incrémenter drop_count : ce paquet est SAUVÉ, pas droppé
            ESP_LOGD(TAG, "drop_until_pusi: PUSI trouvé (PID=0x%04X) après %d drops", 
                     pid, drop_count);
            break;
        }
        
        // Pas trouvé, dropper et continuer
        vRingbufferReturnItem(rb, item);
        drop_count++;
    }
    
    if (!found_pusi) {
        ESP_LOGW(TAG, "drop_until_pusi: PUSI non trouvé après %d drops (max=%d)", 
                 drop_count, max_drop);
        return -1;
    }
    
    return drop_count;
}

void hls_ts_parser_reset(void)
{
    // [PLACEHOLDER] Implémentation actuelle : reset implicite via gather_len=0
    // 
    // Si tu ajoutes un parser TS/PES avec état (ex: continuity counters,
    // PES assembly buffer, PMT cache), reset ces états ici.
    //
    // Exemples futurs :
    // - ts_ctx.continuity_counter[pid] = -1;
    // - pes_assembly_reset(&pes_ctx);
    // - pmt_cache_clear(&pmt_ctx);
    
    ESP_LOGD(TAG, "ts_parser_reset: état TS/PES réinitialisé (noop actuellement)");
}
