/**
 * @file test_lib_m3u8_parser.c
 * @brief Tests unitaires pour le parser M3U8/HLS
 */

#include "unity.h"
#include "lib_m3u8_parser/lib_m3u8_parser.h"
#include <string.h>

// ============================================================================
// Tests de validation des arguments
// ============================================================================

TEST_CASE("lib_m3u8_parser_parse rejects NULL content", "[lib_m3u8_parser]")
{
    lib_m3u8_parser_playlist_t playlist;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, lib_m3u8_parser_parse(NULL, NULL, &playlist));
}

TEST_CASE("lib_m3u8_parser_parse rejects NULL playlist", "[lib_m3u8_parser]")
{
    const char *content = "#EXTM3U\n";
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, lib_m3u8_parser_parse(content, NULL, NULL));
}

TEST_CASE("lib_m3u8_parser_parse rejects invalid header", "[lib_m3u8_parser]")
{
    const char *content = "NOT_M3U8\n";
    lib_m3u8_parser_playlist_t playlist;
    TEST_ASSERT_EQUAL(ESP_FAIL, lib_m3u8_parser_parse(content, NULL, &playlist));
}

TEST_CASE("lib_m3u8_parser_parse rejects empty playlist", "[lib_m3u8_parser]")
{
    const char *content = "#EXTM3U\n#EXT-X-ENDLIST\n";
    lib_m3u8_parser_playlist_t playlist;
    TEST_ASSERT_EQUAL(ESP_FAIL, lib_m3u8_parser_parse(content, NULL, &playlist));
}

// ============================================================================
// Tests de parsing de playlists VOD
// ============================================================================

TEST_CASE("lib_m3u8_parser_parse VOD playlist simple", "[lib_m3u8_parser]")
{
    const char *content =
        "#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-TARGETDURATION:10\n"
        "#EXT-X-PLAYLIST-TYPE:VOD\n"
        "#EXTINF:9.5,\n"
        "segment0.ts\n"
        "#EXTINF:10.0,\n"
        "segment1.ts\n"
        "#EXTINF:8.2,\n"
        "segment2.ts\n"
        "#EXT-X-ENDLIST\n";

    lib_m3u8_parser_playlist_t playlist;
    TEST_ASSERT_EQUAL(ESP_OK, lib_m3u8_parser_parse(content, NULL, &playlist));

    TEST_ASSERT_EQUAL(3, playlist.segment_count);
    TEST_ASSERT_FALSE(playlist.is_live);
    TEST_ASSERT_FALSE(playlist.is_master_playlist);
    TEST_ASSERT_EQUAL(10, playlist.target_duration);

    TEST_ASSERT_EQUAL_FLOAT(9.5f, playlist.segments[0].duration);
    TEST_ASSERT_EQUAL_STRING("segment0.ts", playlist.segments[0].url);

    TEST_ASSERT_EQUAL_FLOAT(10.0f, playlist.segments[1].duration);
    TEST_ASSERT_EQUAL_STRING("segment1.ts", playlist.segments[1].url);

    TEST_ASSERT_EQUAL_FLOAT(8.2f, playlist.segments[2].duration);
    TEST_ASSERT_EQUAL_STRING("segment2.ts", playlist.segments[2].url);

    lib_m3u8_parser_free(&playlist);
}

// ============================================================================
// Tests de parsing de playlists LIVE
// ============================================================================

TEST_CASE("lib_m3u8_parser_parse LIVE playlist", "[lib_m3u8_parser]")
{
    const char *content =
        "#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-TARGETDURATION:10\n"
        "#EXTINF:10.0,\n"
        "segment0.ts\n"
        "#EXTINF:10.0,\n"
        "segment1.ts\n";

    lib_m3u8_parser_playlist_t playlist;
    TEST_ASSERT_EQUAL(ESP_OK, lib_m3u8_parser_parse(content, NULL, &playlist));

    TEST_ASSERT_EQUAL(2, playlist.segment_count);
    TEST_ASSERT_TRUE(playlist.is_live);  // Pas de #EXT-X-ENDLIST
    TEST_ASSERT_FALSE(playlist.is_master_playlist);

    lib_m3u8_parser_free(&playlist);
}

// ============================================================================
// Tests de résolution d'URLs relatives
// ============================================================================

TEST_CASE("lib_m3u8_parser_parse resolves relative URLs", "[lib_m3u8_parser]")
{
    const char *content =
        "#EXTM3U\n"
        "#EXT-X-TARGETDURATION:10\n"
        "#EXTINF:10.0,\n"
        "segment0.ts\n"
        "#EXTINF:10.0,\n"
        "segment1.ts\n"
        "#EXT-X-ENDLIST\n";

    const char *base_url = "https://example.com/streams/playlist.m3u8";

    lib_m3u8_parser_playlist_t playlist;
    TEST_ASSERT_EQUAL(ESP_OK, lib_m3u8_parser_parse(content, base_url, &playlist));

    TEST_ASSERT_EQUAL(2, playlist.segment_count);
    TEST_ASSERT_EQUAL_STRING("https://example.com/streams/segment0.ts", playlist.segments[0].url);
    TEST_ASSERT_EQUAL_STRING("https://example.com/streams/segment1.ts", playlist.segments[1].url);

    lib_m3u8_parser_free(&playlist);
}

TEST_CASE("lib_m3u8_parser_parse handles absolute URLs", "[lib_m3u8_parser]")
{
    const char *content =
        "#EXTM3U\n"
        "#EXT-X-TARGETDURATION:10\n"
        "#EXTINF:10.0,\n"
        "https://cdn.example.com/seg0.ts\n"
        "#EXTINF:10.0,\n"
        "http://cdn2.example.com/seg1.ts\n"
        "#EXT-X-ENDLIST\n";

    const char *base_url = "https://example.com/streams/playlist.m3u8";

    lib_m3u8_parser_playlist_t playlist;
    TEST_ASSERT_EQUAL(ESP_OK, lib_m3u8_parser_parse(content, base_url, &playlist));

    TEST_ASSERT_EQUAL(2, playlist.segment_count);
    TEST_ASSERT_EQUAL_STRING("https://cdn.example.com/seg0.ts", playlist.segments[0].url);
    TEST_ASSERT_EQUAL_STRING("http://cdn2.example.com/seg1.ts", playlist.segments[1].url);

    lib_m3u8_parser_free(&playlist);
}

TEST_CASE("lib_m3u8_parser_parse resolves domain-relative URLs", "[lib_m3u8_parser]")
{
    const char *content =
        "#EXTM3U\n"
        "#EXT-X-TARGETDURATION:10\n"
        "#EXTINF:10.0,\n"
        "/absolute/path/segment0.ts\n"
        "#EXT-X-ENDLIST\n";

    const char *base_url = "https://example.com/streams/playlist.m3u8";

    lib_m3u8_parser_playlist_t playlist;
    TEST_ASSERT_EQUAL(ESP_OK, lib_m3u8_parser_parse(content, base_url, &playlist));

    TEST_ASSERT_EQUAL(1, playlist.segment_count);
    TEST_ASSERT_EQUAL_STRING("https://example.com/absolute/path/segment0.ts", playlist.segments[0].url);

    lib_m3u8_parser_free(&playlist);
}

// ============================================================================
// Tests de parsing de master playlists
// ============================================================================

TEST_CASE("lib_m3u8_parser_parse master playlist", "[lib_m3u8_parser]")
{
    const char *content =
        "#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360\n"
        "low.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1400000,RESOLUTION=1280x720\n"
        "high.m3u8\n";

    const char *base_url = "https://example.com/master.m3u8";

    lib_m3u8_parser_playlist_t playlist;
    TEST_ASSERT_EQUAL(ESP_OK, lib_m3u8_parser_parse(content, base_url, &playlist));

    TEST_ASSERT_TRUE(playlist.is_master_playlist);
    TEST_ASSERT_EQUAL(1, playlist.segment_count);  // Prend seulement la première variante
    TEST_ASSERT_EQUAL_STRING("https://example.com/low.m3u8", playlist.segments[0].url);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, playlist.segments[0].duration);  // Pas de durée pour media playlist

    lib_m3u8_parser_free(&playlist);
}

// ============================================================================
// Tests des limites
// ============================================================================

TEST_CASE("lib_m3u8_parser_parse respects max segments limit", "[lib_m3u8_parser]")
{
    // Générer une playlist avec plus de 32 segments
    char content[4096];
    strcpy(content, "#EXTM3U\n#EXT-X-TARGETDURATION:10\n");

    for (int i = 0; i < 40; i++) {
        char line[64];
        snprintf(line, sizeof(line), "#EXTINF:10.0,\nseg%d.ts\n", i);
        strcat(content, line);
    }
    strcat(content, "#EXT-X-ENDLIST\n");

    lib_m3u8_parser_playlist_t playlist;
    TEST_ASSERT_EQUAL(ESP_OK, lib_m3u8_parser_parse(content, NULL, &playlist));

    // Ne doit parser que les 32 premiers segments
    TEST_ASSERT_EQUAL(LIB_M3U8_PARSER_MAX_SEGMENTS, playlist.segment_count);

    lib_m3u8_parser_free(&playlist);
}

// ============================================================================
// Tests de lib_m3u8_parser_free et lib_m3u8_parser_dump
// ============================================================================

TEST_CASE("lib_m3u8_parser_free handles NULL safely", "[lib_m3u8_parser]")
{
    // Ne doit pas crasher
    lib_m3u8_parser_free(NULL);
    TEST_PASS();
}

TEST_CASE("lib_m3u8_parser_dump handles NULL safely", "[lib_m3u8_parser]")
{
    // Ne doit pas crasher
    lib_m3u8_parser_dump(NULL);
    TEST_PASS();
}

TEST_CASE("lib_m3u8_parser_dump outputs valid playlist", "[lib_m3u8_parser]")
{
    const char *content =
        "#EXTM3U\n"
        "#EXT-X-TARGETDURATION:10\n"
        "#EXTINF:10.0,\n"
        "segment0.ts\n"
        "#EXT-X-ENDLIST\n";

    lib_m3u8_parser_playlist_t playlist;
    TEST_ASSERT_EQUAL(ESP_OK, lib_m3u8_parser_parse(content, NULL, &playlist));

    // Ne doit pas crasher
    lib_m3u8_parser_dump(&playlist);

    lib_m3u8_parser_free(&playlist);
    TEST_PASS();
}
