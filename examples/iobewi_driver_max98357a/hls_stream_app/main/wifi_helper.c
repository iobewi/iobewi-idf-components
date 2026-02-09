#include "wifi_helper.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_helper";

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static int s_max_retry = 5;
static bool s_is_connected = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_connected = false;
        if (s_retry_num < s_max_retry) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Tentative de reconnexion WiFi (%d/%d)", s_retry_num, s_max_retry);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Échec de connexion WiFi après %d tentatives", s_max_retry);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Adresse IP obtenue: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_is_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_helper_init(void)
{
    esp_err_t ret;

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Échec de création de l'event group WiFi");
        return ESP_FAIL;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec d'initialisation netif: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Échec de création de la boucle d'événements: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec d'initialisation WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                             ESP_EVENT_ANY_ID,
                                             &wifi_event_handler,
                                             NULL,
                                             NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec d'enregistrement du handler WIFI_EVENT: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT,
                                             IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler,
                                             NULL,
                                             NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec d'enregistrement du handler IP_EVENT: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Initialisation WiFi réussie");
    return ESP_OK;
}

esp_err_t wifi_helper_connect(const char *ssid, const char *password, int max_retry)
{
    if (ssid == NULL || password == NULL) {
        ESP_LOGE(TAG, "SSID ou mot de passe NULL");
        return ESP_ERR_INVALID_ARG;
    }

    s_max_retry = max_retry;
    s_retry_num = 0;

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec de configuration du mode WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec de configuration WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec de démarrage WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Connexion au WiFi SSID:%s", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connecté au WiFi SSID:%s", ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Échec de connexion au WiFi SSID:%s", ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Événement WiFi inattendu");
        return ESP_FAIL;
    }
}

EventGroupHandle_t wifi_helper_get_event_group(void)
{
    return s_wifi_event_group;
}

bool wifi_helper_is_connected(void)
{
    return s_is_connected;
}
