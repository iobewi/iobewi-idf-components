/**
 * @file test_ts_sync.c
 * @brief Tests unitaires pour le module TS_SYNC (offline, host-side)
 *
 * Tests validés :
 * - Cas nominal : paquets TS alignés 188 bytes
 * - Cas bruit : données aléatoires + sync à offset connu
 * - Cas faux positifs : 0x47 dans payload mais pas espacé 188
 * - Cas smart resync : PID audio + PUSI + PES start code
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// Stub ESP_LOGx pour tests host-side
#define ESP_LOGD(tag, fmt, ...) printf("[D] %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("[I] %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W] %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E] %s: " fmt "\n", tag, ##__VA_ARGS__)

// Include module TS_SYNC (header + source directement pour test host)
// NOTE: En production, utiliser Unity framework ESP-IDF
#include "../src/app_hls_player_ts_sync.h"

// Pour éviter de linker toute l'implémentation, on inclut le .c directement
// (méthode simplifiée pour tests host-side sans CMake complexe)
#include "../src/app_hls_player_ts_sync.c"

// ============================================================================
// Helpers pour générer des paquets TS minimalistes
// ============================================================================

/**
 * @brief Générer un paquet TS minimal (188 bytes)
 *
 * @param buf Buffer de sortie (doit être ≥ 188 bytes)
 * @param pid PID du paquet (13 bits)
 * @param pusi Payload Unit Start Indicator (1 = début PES)
 * @param payload_pattern Pattern à remplir dans le payload (0xFF par défaut)
 */
static void generate_ts_packet(uint8_t *buf, uint16_t pid, bool pusi, uint8_t payload_pattern)
{
    memset(buf, payload_pattern, 188);

    // Header TS (4 bytes)
    buf[0] = 0x47;  // Sync byte
    buf[1] = (pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F);  // PUSI + PID[12:8]
    buf[2] = pid & 0xFF;  // PID[7:0]
    buf[3] = 0x10;  // AFC=01 (payload only), CC=0

    // Si PUSI=1, ajouter un PES header minimal après le header TS
    if (pusi) {
        // PES start code (00 00 01)
        buf[4] = 0x00;
        buf[5] = 0x00;
        buf[6] = 0x01;
        buf[7] = 0xC0;  // Stream ID (audio)
    }
}

/**
 * @brief Générer N paquets TS consécutifs
 */
static void generate_ts_packets(uint8_t *buf, size_t num_packets, uint16_t pid, bool first_pusi)
{
    for (size_t i = 0; i < num_packets; i++) {
        bool pusi = (i == 0 && first_pusi);
        generate_ts_packet(buf + i * 188, pid, pusi, 0xAA);
    }
}

// ============================================================================
// Tests hls_ts_find_next_sync()
// ============================================================================

static void test_find_sync_nominal(void)
{
    printf("\n=== TEST: find_sync_nominal ===\n");

    // 3 paquets TS alignés
    uint8_t buf[3 * 188];
    generate_ts_packets(buf, 3, 0x0101, false);

    size_t skip = 0;
    bool found = hls_ts_find_next_sync(buf, sizeof(buf), &skip);

    assert(found == true);
    assert(skip == 0);  // Déjà aligné
    printf("PASS: Sync trouvé à offset 0 (aligné)\n");
}

static void test_find_sync_offset(void)
{
    printf("\n=== TEST: find_sync_offset ===\n");

    // 10 bytes de bruit + 3 paquets TS
    uint8_t buf[10 + 3 * 188];
    memset(buf, 0xFF, 10);  // Bruit
    generate_ts_packets(buf + 10, 3, 0x0101, false);

    size_t skip = 0;
    bool found = hls_ts_find_next_sync(buf, sizeof(buf), &skip);

    assert(found == true);
    assert(skip == 10);  // Sync trouvé après 10 bytes de bruit
    printf("PASS: Sync trouvé à offset 10 (bruit initial)\n");
}

static void test_find_sync_false_positive(void)
{
    printf("\n=== TEST: find_sync_false_positive ===\n");

    // 1 byte 0x47 isolé (faux positif) + 5 bytes bruit + 3 vrais paquets TS
    // (triple check nécessite 3 paquets minimum pour validation)
    uint8_t buf[1 + 5 + 3 * 188];
    buf[0] = 0x47;  // Faux positif (pas espacé 188)
    memset(buf + 1, 0xFF, 5);
    generate_ts_packets(buf + 6, 3, 0x0101, false);

    size_t skip = 0;
    bool found = hls_ts_find_next_sync(buf, sizeof(buf), &skip);

    assert(found == true);
    assert(skip == 6);  // Ignore faux positif, trouve vrai sync à offset 6
    printf("PASS: Faux positif ignoré, sync trouvé à offset 6\n");
}

static void test_find_sync_not_found(void)
{
    printf("\n=== TEST: find_sync_not_found ===\n");

    // Buffer trop petit ou sans sync
    uint8_t buf[100];
    memset(buf, 0xFF, sizeof(buf));

    size_t skip = 0;
    bool found = hls_ts_find_next_sync(buf, sizeof(buf), &skip);

    assert(found == false);
    printf("PASS: Aucun sync trouvé (buffer trop petit)\n");
}

// ============================================================================
// Tests hls_ts_resync_smart()
// ============================================================================

static void test_resync_smart_nominal(void)
{
    printf("\n=== TEST: resync_smart_nominal ===\n");

    // 50 bytes bruit + 1 paquet TS avec PUSI=1 + PES start
    uint8_t buf[50 + 3 * 188];
    memset(buf, 0xFF, 50);
    generate_ts_packets(buf + 50, 3, 0x0101, true);  // PUSI=1 sur premier paquet

    size_t skip = 0;
    bool found = hls_ts_resync_smart(buf, sizeof(buf), 4096, 0x0101, &skip);

    assert(found == true);
    assert(skip == 50);  // Resync trouvé à offset 50
    printf("PASS: Smart resync trouvé PID=0x0101 + PUSI=1 à offset 50\n");
}

static void test_resync_smart_wrong_pid(void)
{
    printf("\n=== TEST: resync_smart_wrong_pid ===\n");

    // Paquet TS avec mauvais PID
    uint8_t buf[3 * 188];
    generate_ts_packets(buf, 3, 0x0100, true);  // PID=0x0100 (pas 0x0101)

    size_t skip = 0;
    bool found = hls_ts_resync_smart(buf, sizeof(buf), 4096, 0x0101, &skip);

    assert(found == false);  // Ne devrait pas trouver (mauvais PID)
    printf("PASS: Smart resync ne trouve pas de PID=0x0101 (PID différent)\n");
}

static void test_resync_smart_no_pusi(void)
{
    printf("\n=== TEST: resync_smart_no_pusi ===\n");

    // Paquet TS avec bon PID mais PUSI=0
    uint8_t buf[3 * 188];
    generate_ts_packets(buf, 3, 0x0101, false);  // PUSI=0

    size_t skip = 0;
    bool found = hls_ts_resync_smart(buf, sizeof(buf), 4096, 0x0101, &skip);

    assert(found == false);  // Ne devrait pas trouver (pas de PUSI)
    printf("PASS: Smart resync ne trouve pas de PUSI=1 (PUSI=0)\n");
}

static void test_resync_smart_no_pes_start(void)
{
    printf("\n=== TEST: resync_smart_no_pes_start ===\n");

    // Paquet TS avec PID + PUSI mais pas de PES start code
    uint8_t buf[3 * 188];
    generate_ts_packets(buf, 3, 0x0101, true);

    // Corrompre PES start code (00 00 01 → FF FF FF)
    buf[4] = 0xFF;
    buf[5] = 0xFF;
    buf[6] = 0xFF;

    size_t skip = 0;
    bool found = hls_ts_resync_smart(buf, sizeof(buf), 4096, 0x0101, &skip);

    assert(found == false);  // Ne devrait pas trouver (pas de PES start)
    printf("PASS: Smart resync ne trouve pas de PES start code (corrompu)\n");
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    printf("=========================================\n");
    printf("Tests Unitaires TS_SYNC (Offline)\n");
    printf("=========================================\n");

    // Tests hls_ts_find_next_sync()
    test_find_sync_nominal();
    test_find_sync_offset();
    test_find_sync_false_positive();
    test_find_sync_not_found();

    // Tests hls_ts_resync_smart()
    test_resync_smart_nominal();
    test_resync_smart_wrong_pid();
    test_resync_smart_no_pusi();
    test_resync_smart_no_pes_start();

    printf("\n=========================================\n");
    printf("TOUS LES TESTS PASSÉS ✓\n");
    printf("=========================================\n");

    return 0;
}
