/**
 * @file test_sizeof.c
 * @brief Test de mesure de l'empreinte mémoire de lib_m3u8_parser
 */

#include <stdio.h>
#include "lib_m3u8_parser/lib_m3u8_parser_types.h"

int main(void)
{
    printf("=== Empreinte mémoire lib_m3u8_parser ===\n\n");

    printf("Structures individuelles :\n");
    printf("  sizeof(lib_m3u8_parser_segment_t)   = %zu bytes\n", sizeof(lib_m3u8_parser_segment_t));
    printf("  sizeof(lib_m3u8_parser_variant_t)   = %zu bytes\n", sizeof(lib_m3u8_parser_variant_t));

    printf("\nTableaux (MAX_SEGMENTS = %d) :\n", LIB_M3U8_PARSER_MAX_SEGMENTS);
    printf("  segments[32]                        = %zu bytes (~%.1f KB)\n",
           sizeof(lib_m3u8_parser_segment_t) * LIB_M3U8_PARSER_MAX_SEGMENTS,
           (sizeof(lib_m3u8_parser_segment_t) * LIB_M3U8_PARSER_MAX_SEGMENTS) / 1024.0);
    printf("  variants[32]                        = %zu bytes (~%.1f KB)\n",
           sizeof(lib_m3u8_parser_variant_t) * LIB_M3U8_PARSER_MAX_SEGMENTS,
           (sizeof(lib_m3u8_parser_variant_t) * LIB_M3U8_PARSER_MAX_SEGMENTS) / 1024.0);

    printf("\nStructure playlist complète :\n");
    printf("  sizeof(lib_m3u8_parser_playlist_t)  = %zu bytes (~%.1f KB)\n",
           sizeof(lib_m3u8_parser_playlist_t),
           sizeof(lib_m3u8_parser_playlist_t) / 1024.0);

    printf("\nComparaison théorique (sans union) :\n");
    size_t theoretical_without_union =
        sizeof(lib_m3u8_parser_segment_t) * LIB_M3U8_PARSER_MAX_SEGMENTS +
        sizeof(lib_m3u8_parser_variant_t) * LIB_M3U8_PARSER_MAX_SEGMENTS +
        sizeof(int) * 2 +  // segment_count + variant_count
        sizeof(bool) * 2 + // is_live + is_master_playlist
        sizeof(int) +      // target_duration
        sizeof(int64_t) +  // media_sequence
        sizeof(int);       // version

    printf("  Taille sans union (théorique)      = %zu bytes (~%.1f KB)\n",
           theoretical_without_union,
           theoretical_without_union / 1024.0);

    printf("\n✅ Gain avec union : %.1f KB (%.1f%%)\n",
           (theoretical_without_union - sizeof(lib_m3u8_parser_playlist_t)) / 1024.0,
           100.0 * (theoretical_without_union - sizeof(lib_m3u8_parser_playlist_t)) / theoretical_without_union);

    return 0;
}
