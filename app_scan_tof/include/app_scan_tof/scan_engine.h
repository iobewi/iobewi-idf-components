#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_err.h>
#include <sensor_msgs/msg/laser_scan.h>

#include "mw_scan_builder/mw_scan_builder.h"
#include "drv_vl53l0x/tof_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const scan_config_t *cfg;
    const tof_hw_config_t *hw_cfg;
    int64_t (*time_provider)(void);
} scan_engine_t;

esp_err_t scan_engine_init(scan_engine_t *e, const scan_config_t *cfg, const tof_hw_config_t *hw_cfg);
esp_err_t scan_engine_deinit(scan_engine_t *e);
esp_err_t scan_engine_step(scan_engine_t *e, sensor_msgs__msg__LaserScan *out_msg);
esp_err_t scan_engine_set_time_provider(scan_engine_t *e, int64_t (*now_ns)(void));

#ifdef __cplusplus
}
#endif
