#include "appliance_api_feature_lists.h"

// ============================================================================
// Common features (ERD 0x0092)
// ============================================================================

const tiny_erd_t common_feature_primary_erds[] = {
  0x0001,
  0x0002,
  0x0008,
  0x0035,
};

const tiny_erd_t common_feature_service_mode_erds[] = {
  0x0036,
  0x0037,
};

const tiny_erd_t common_feature_boot_loader_erds[] = {
  0x0030,
};

const tiny_erd_t common_feature_clock_format_erds[] = {
  0x0006,
};

const tiny_erd_t common_feature_sound_level_erds[] = {
  0x000a,
};

const tiny_erd_t common_feature_control_lock_erds[] = {
  0x0004,
};

const tiny_erd_t common_feature_temperature_display_units_erds[] = {
  0x0007,
};

const tiny_erd_t common_feature_sabbath_erds[] = {
  0x0009,
};

const tiny_erd_t common_feature_enhanced_sabbath_erds[] = {
  0x004f,
};

const tiny_erd_t common_feature_time_of_day_erds[] = {
  0x0005,
};

const tiny_erd_t common_feature_reset_information_erds[] = {
  0x0700,
  0x0701,
  0x0702,
  0x0703,
};

const tiny_erd_t common_feature_configuration_hash_erds[] = {
  0x0060,
};

const tiny_erd_t common_feature_brand_information_erds[] = {
  0x0099,
};

const tiny_erd_t common_feature_sound_themes_erds[] = {
  0x0800,
  0x0801,
  0x0802,
};

const tiny_erd_t common_feature_inference_results_erds[] = {
  0x0505,
};

const tiny_erd_t common_feature_ui_sound_level_erds[] = {
  0x0902,
  0x0903,
  0x0904,
};

const tiny_erd_t common_feature_nonvolatile_usage_monitoring_erds[] = {
  0x070e,
  0x070f,
  0x0710,
};

const common_feature_descriptor_t common_feature_descriptors[] = {
  {0x00000001, "Primary", common_feature_primary_erds, 4},
  {0x00000002, "Service Mode", common_feature_service_mode_erds, 2},
  {0x00000004, "Boot Loader", common_feature_boot_loader_erds, 1},
  {0x00000008, "Clock Format", common_feature_clock_format_erds, 1},
  {0x00000010, "Sound Level", common_feature_sound_level_erds, 1},
  {0x00000020, "Control Lock", common_feature_control_lock_erds, 1},
  {0x00000040, "Temperature Display Units", common_feature_temperature_display_units_erds, 1},
  {0x00000080, "Sabbath", common_feature_sabbath_erds, 1},
  {0x00000100, "Enhanced Sabbath", common_feature_enhanced_sabbath_erds, 1},
  {0x00000200, "Time of Day", common_feature_time_of_day_erds, 1},
  {0x00000400, "Reset Information", common_feature_reset_information_erds, 4},
  {0x00000800, "Configuration Hash", common_feature_configuration_hash_erds, 1},
  {0x00001000, "Brand Information", common_feature_brand_information_erds, 1},
  {0x00002000, "Sound Themes", common_feature_sound_themes_erds, 3},
  {0x00004000, "Inference Results", common_feature_inference_results_erds, 1},
  {0x00008000, "UI Sound Level", common_feature_ui_sound_level_erds, 3},
  {0x00010000, "Nonvolatile Usage Monitoring", common_feature_nonvolatile_usage_monitoring_erds, 3},
};
const uint16_t common_feature_descriptor_count =
  sizeof(common_feature_descriptors) / sizeof(common_feature_descriptors[0]);

// ============================================================================
// Appliance Feature APIs (ERDs 0x0093-0x0097 and 0x0109-0x010D)
// ============================================================================

const tiny_erd_t appliance_api_water_heater_v1_foundation_erds[] = {
  0x4020,
  0x4023,
  0x4024,
  0x4025,
  0x4026,
  0x4028,
  0x4030,
  0x4040,
  0x4041,
  0x4047,
  0x4048,
  0x4049,
  0x404a,
  0x405f,
  0x4060,
  0x4065,
  0x4066,
  0x4068,
  0x4069,
  0x40c0,
  0x4102,
  0x4103,
  0x4107,
};

const tiny_erd_t appliance_api_water_heater_v1_anode_depletion_erds[] = {
  0x404c,
  0x404d,
  0x4081,
};

const tiny_erd_t appliance_api_water_heater_v1_mixing_valve_erds[] = {
  0x4008,
  0x4009,
  0x400a,
  0x400b,
  0x400c,
  0x402a,
  0x402b,
  0x406e,
  0x40b9,
  0x40c3,
  0x4101,
  0x4105,
};

const tiny_erd_t* appliance_api_water_heater_v1_tall_tank_erds __attribute__((unused)) = nullptr;

const tiny_erd_t* appliance_api_water_heater_v1_wifi_erds __attribute__((unused)) = nullptr;

const tiny_erd_t appliance_api_water_heater_v1_heat_pump_erds[] = {
  0x4061,
  0x4062,
  0x4063,
  0x4064,
};

const tiny_erd_t* appliance_api_water_heater_v1_concurrent_loads_erds __attribute__((unused)) = nullptr;

const tiny_erd_t* appliance_api_water_heater_v1_variable_speed_fan_erds __attribute__((unused)) = nullptr;

const tiny_erd_t* appliance_api_water_heater_v1_variable_speed_compressor_erds __attribute__((unused)) = nullptr;

const tiny_erd_t* appliance_api_water_heater_v1_120v_operation_erds __attribute__((unused)) = nullptr;

const tiny_erd_t appliance_api_water_heater_v1_estimation_erds[] = {
  0x40cb,
  0x40cc,
  0x40ce,
  0x40d0,
};

const tiny_erd_t appliance_api_water_heater_v1_cost_of_power_erds[] = {
  0x4125,
};

const tiny_erd_t* appliance_api_water_heater_v1_gas_erds __attribute__((unused)) = nullptr;

const tiny_erd_t* appliance_api_water_heater_v1_blower_erds __attribute__((unused)) = nullptr;

const tiny_erd_t appliance_api_water_heater_v1_dad_mode_erds[] = {
  0x411d,
};

const tiny_erd_t appliance_api_water_heater_v1_service_request_erds[] = {
  0x40c6,
};

const tiny_erd_t appliance_api_water_heater_v1_water_heater_boost_mode_erds[] = {
  0x4220,
  0x4221,
};

const tiny_erd_t appliance_api_water_heater_v1_water_heater_active_erds[] = {
  0x4225,
  0x4226,
};

const tiny_erd_t* appliance_api_water_heater_v1_canadian_model_erds __attribute__((unused)) = nullptr;

const tiny_erd_t appliance_api_water_heater_v1_anode_percentage_remaining_erds[] = {
  0x40c2,
};

const tiny_erd_t appliance_api_water_heater_v1_shutoff_valve_accessory_erds[] = {
  0x4082,
  0x4083,
  0x4222,
  0x4223,
};

const tiny_erd_t appliance_api_water_heater_v1_heating_diagnostics_erds[] = {
  0x4056,
  0x4058,
  0x40b8,
  0x40ba,
  0x40bb,
  0x40bc,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_foundation_erds[] = {
  0x2000,
  0x2001,
  0x2002,
  0x2007,
  0x200a,
  0x2010,
  0x2012,
  0x201c,
  0x201d,
  0x204d,
  0x2050,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_remote_start_and_stop_erds[] = {
  0x2038,
  0x2039,
  0x2040,
  0x2041,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_dryer_sheet_inventory_erds[] = {
  0x2022,
  0x2023,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_extended_tumble_erds[] = {
  0x2051,
  0x2052,
  0x2053,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_reduce_static_option_erds[] = {
  0x205d,
  0x205e,
  0x205f,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_washer_link_erds[] = {
  0x2069,
  0x206a,
  0x206b,
  0x206c,
  0x206d,
  0x206e,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_blocked_vent_erds[] = {
  0x206f,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_ecodry_option_erds[] = {
  0x2044,
  0x2045,
  0x2046,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_damp_alert_option_erds[] = {
  0x2047,
  0x2048,
  0x2049,
  0x204a,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_dryness_option_erds[] = {
  0x204b,
  0x204c,
  0x204d,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_temperature_option_erds[] = {
  0x204e,
  0x204f,
  0x2050,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_small_load_option_erds[] = {
  0x2066,
  0x2067,
  0x2068,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_detangle_option_erds[] = {
  0x2074,
  0x2075,
  0x2076,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_time_level_option_erds[] = {
  0x2077,
  0x2078,
  0x2079,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_sanitize_option_erds[] = {
  0x207a,
  0x207b,
  0x207c,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_key_volume_option_erds[] = {
  0x207d,
  0x207e,
  0x207f,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_alert_volume_option_erds[] = {
  0x2080,
  0x2081,
  0x2082,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_control_lock_option_erds[] = {
  0x2083,
  0x2084,
  0x2085,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_steam_option_erds[] = {
  0x2086,
  0x2087,
  0x2088,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_air_fluff_cycle_option_erds[] = {
  0x2089,
  0x208a,
  0x208b,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_steam_cycle_option_erds[] = {
  0x208c,
  0x208d,
  0x208e,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_warm_up_cycle_option_erds[] = {
  0x208f,
  0x2090,
  0x2091,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_smart_vent_cycle_option_erds[] = {
  0x2092,
  0x2093,
  0x2094,
  0x2095,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_washer_link_2_erds[] = {
  0x206a,
  0x206b,
  0x206c,
  0x206d,
  0x206e,
  0x2096,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_connect_cycle_erds[] = {
  0x2020,
  0x2021,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_scent_erds[] = {
  0x2099,
  0x209b,
  0x209c,
  0x209d,
  0x20ef,
  0x20f0,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_commercial_laundry_status_erds[] = {
  0x209e,
  0x209f,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_remote_cycle_selection_erds[] = {
  0x20a7,
  0x20a8,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_rj45_write_access_erds[] = {
  0x20cd,
  0x20ce,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_downloaded_cycle_erds[] = {
  0x20d1,
  0x20d2,
  0x20d3,
  0x20d7,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_remote_my_cycle_erds[] = {
  0x20df,
  0x20e0,
  0x20e1,
  0x20e2,
  0x20e3,
  0x20e4,
  0x20e5,
  0x20e6,
  0x20e7,
  0x20e8,
  0x20e9,
  0x20ea,
  0x20eb,
  0x20ec,
  0x20ed,
  0x20ee,
};

const tiny_erd_t appliance_api_clothes_dryer_v1_adaptive_my_cycle_erds[] = {
  0x20f1,
  0x20f2,
  0x20f3,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_notifications_and_warnings_erds[] = {
  0x20da,
  0x20db,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_remote_start_and_stop_v2_erds[] = {
  0x2039,
  0x2040,
  0x209d,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_smart_vent_cycle_option_v2_erds[] = {
  0x2092,
  0x2093,
  0x2094,
  0x2095,
  0x20f5,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_laundry_operating_mode_erds[] = {
  0x200e,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_legacy_extended_tumble_erds[] = {
  0x201b,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_optimise_cycle_option_erds[] = {
  0x210f,
  0x2110,
  0x2111,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_cycle_function_erds[] = {
  0x20dc,
  0x20dd,
  0x20de,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_dry_time_option_erds[] = {
  0x20c8,
  0x20c9,
  0x20ca,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_signal_option_erds[] = {
  0x211b,
  0x211c,
  0x211d,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_wrinkle_free_option_erds[] = {
  0x210c,
  0x210d,
  0x210e,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_commercial_laundry_payment_v1_attributes_erds[] = {
  0x211e,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_timed_cycle_total_drying_duration_option_erds[] = {
  0x211f,
  0x2120,
  0x2121,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_pre_cycle_time_increment_erds[] = {
  0x2007,
  0x209e,
  0x209f,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_mid_cycle_time_increment_erds[] = {
  0x212f,
  0x2130,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_dryer_ble_control_erds[] = {
  0x214f,
  0x2150,
  0x2151,
  0x2152,
  0x2153,
  0x2157,
  0x2158,
  0x2159,
  0x215a,
  0x215d,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_dryer_heater_type_erds[] = {
  0x2163,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_commercial_laundry_coin_integration_erds[] = {
  0x2164,
  0x2165,
  0x2166,
  0x2167,
  0x2168,
  0x2169,
  0x216a,
  0x216b,
  0x216c,
  0x216d,
  0x2171,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_item_count_option_erds[] = {
  0x215b,
  0x215c,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_dryer_notifications_erds[] = {
  0x217a,
  0x217b,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_drum_light_option_erds[] = {
  0x217c,
  0x217d,
  0x217e,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_delay_start_duration_option_erds[] = {
  0x217f,
  0x2180,
  0x2181,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_dryer_faults_erds[] = {
  0x222b,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_commercial_laundry_common_remote_parameters_erds[] = {
  0x2236,
  0x224c,
  0x224d,
  0x224e,
  0x2258,
  0x2259,
  0x2316,
  0x2317,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_commercial_laundry_front_load_only_remote_parameters_erds[] = {
  0x2237,
  0x2238,
  0x2239,
  0x223a,
  0x223b,
  0x224f,
  0x2250,
  0x2251,
  0x2252,
  0x2253,
  0x2254,
  0x2255,
  0x2256,
  0x2257,
  0x2258,
  0x2259,
};

const tiny_erd_t appliance_api_clothes_dryer_v2_commercial_free_mode_erds[] = {
  0x2338,
  0x2339,
};

const tiny_erd_t appliance_api_clothes_washer_v1_foundation_erds[] = {
  0x2000,
  0x2001,
  0x2002,
  0x2007,
  0x200a,
  0x2010,
  0x2012,
  0x2013,
  0x2015,
  0x2016,
  0x2017,
  0x2018,
  0x201d,
};

const tiny_erd_t appliance_api_clothes_washer_v1_add_garment_indicator_erds[] = {
  0x2014,
};

const tiny_erd_t appliance_api_clothes_washer_v1_connect_cycle_erds[] = {
  0x2020,
  0x2021,
};

const tiny_erd_t appliance_api_clothes_washer_v1_remote_start_and_stop_erds[] = {
  0x2038,
  0x2039,
  0x2040,
};

const tiny_erd_t appliance_api_clothes_washer_v1_bulk_dispense_and_tank_status_erds[] = {
  0x2008,
  0x2009,
};

const tiny_erd_t appliance_api_clothes_washer_v1_washer_link_erds[] = {
  0x2069,
};

const tiny_erd_t appliance_api_clothes_washer_v1_detergent_dispense_flow_rate_erds[] = {
  0x2071,
};

const tiny_erd_t appliance_api_clothes_washer_v1_deprecated_m00000080_erds[] = {
  0x2060,
  0x2061,
};

const tiny_erd_t appliance_api_clothes_washer_v1_one_step_wash_and_dry_erds[] = {
  0x2063,
  0x2064,
};

const tiny_erd_t appliance_api_clothes_washer_v1_smart_dispense_erds[] = {
  0x2033,
  0x2034,
  0x2035,
  0x2036,
  0x203a,
  0x203b,
  0x203c,
  0x203d,
  0x203e,
  0x2070,
  0x2072,
  0x2073,
};

const tiny_erd_t appliance_api_clothes_washer_v1_time_saver_erds[] = {
  0x2054,
  0x2055,
};

const tiny_erd_t* appliance_api_clothes_washer_v1_deprecated_m00000800_erds __attribute__((unused)) = nullptr;

const tiny_erd_t appliance_api_clothes_washer_v1_prewash_erds[] = {
  0x205a,
  0x205b,
};

const tiny_erd_t appliance_api_clothes_washer_v1_power_care_erds[] = {
  0x2097,
  0x2098,
};

const tiny_erd_t appliance_api_clothes_washer_v1_washer_link_v2_erds[] = {
  0x2096,
};

const tiny_erd_t appliance_api_clothes_washer_v1_color_keeper_erds[] = {
  0x20a0,
  0x20a1,
};

const tiny_erd_t appliance_api_clothes_washer_v1_no_tangle_wash_erds[] = {
  0x20a3,
  0x20a4,
};

const tiny_erd_t appliance_api_clothes_washer_v1_out_of_balance_notification_erds[] = {
  0x20a6,
};

const tiny_erd_t appliance_api_clothes_washer_v1_commercial_laundry_status_erds[] = {
  0x209e,
  0x209f,
};

const tiny_erd_t appliance_api_clothes_washer_v1_remote_cycle_selection_erds[] = {
  0x20a7,
  0x20a8,
};

const tiny_erd_t appliance_api_clothes_washer_v1_rj45_write_access_erds[] = {
  0x20cd,
  0x20ce,
};

const tiny_erd_t appliance_api_clothes_washer_v1_true_rinse_erds[] = {
  0x20a9,
  0x20aa,
};

const tiny_erd_t appliance_api_clothes_washer_v1_stain_removal_guide_erds[] = {
  0x20cf,
  0x20d0,
  0x20d4,
  0x20d8,
};

const tiny_erd_t appliance_api_clothes_washer_v1_flex_dispense_erds[] = {
  0x20d5,
  0x20d6,
  0x20d9,
};

const tiny_erd_t appliance_api_clothes_washer_v1_downloadable_cycle_erds[] = {
  0x20d1,
  0x20d2,
  0x20d3,
  0x20d7,
};

const tiny_erd_t appliance_api_clothes_washer_v1_smart_dispense_adjustability_erds[] = {
  0x20c0,
  0x20c1,
  0x20c2,
};

const tiny_erd_t appliance_api_clothes_washer_v1_deep_fill_incremental_option_erds[] = {
  0x20ad,
  0x20ae,
};

const tiny_erd_t appliance_api_clothes_washer_v1_deep_fill_fixed_level_option_erds[] = {
  0x20af,
  0x20b0,
};

const tiny_erd_t appliance_api_clothes_washer_v1_delay_wash_option_erds[] = {
  0x20b1,
  0x20b2,
};

const tiny_erd_t appliance_api_clothes_washer_v1_extra_rinse_option_erds[] = {
  0x20b3,
  0x20b4,
};

const tiny_erd_t appliance_api_clothes_washer_v1_fabric_softener_option_erds[] = {
  0x20b5,
  0x20b6,
};

const tiny_erd_t appliance_api_clothes_washer_v1_spin_level_option_erds[] = {
  0x20b7,
  0x20b8,
};

const tiny_erd_t appliance_api_clothes_washer_v2_water_on_demand_soap_dispense_option_erds[] = {
  0x20b9,
  0x20ba,
};

const tiny_erd_t appliance_api_clothes_washer_v2_water_temperature_option_erds[] = {
  0x20bb,
  0x20bc,
};

const tiny_erd_t appliance_api_clothes_washer_v2_warm_rinse_option_erds[] = {
  0x2025,
  0x20bd,
};

const tiny_erd_t appliance_api_clothes_washer_v2_soil_level_option_erds[] = {
  0x20be,
  0x20bf,
};

const tiny_erd_t appliance_api_clothes_washer_v2_auto_soak_option_erds[] = {
  0x20ab,
  0x20ac,
};

const tiny_erd_t appliance_api_clothes_washer_v2_control_lock_option_erds[] = {
  0x2083,
  0x2084,
};

const tiny_erd_t appliance_api_clothes_washer_v2_adaptive_my_cycle_erds[] = {
  0x20f1,
  0x20f2,
  0x20f3,
};

const tiny_erd_t appliance_api_clothes_washer_v2_self_clean_reminder_erds[] = {
  0x20f4,
};

const tiny_erd_t appliance_api_clothes_washer_v2_remote_my_cycle_erds[] = {
  0x20f6,
  0x20f7,
  0x20f8,
  0x20f9,
  0x20fa,
  0x20fb,
  0x20fc,
  0x20fd,
  0x20fe,
  0x20ff,
  0x2100,
  0x2101,
  0x2102,
  0x2103,
  0x2104,
  0x2105,
};

const tiny_erd_t appliance_api_clothes_washer_v2_auto_dose_option_erds[] = {
  0x2112,
  0x2113,
  0x2114,
};

const tiny_erd_t appliance_api_clothes_washer_v2_ai_option_erds[] = {
  0x2115,
  0x2116,
  0x2117,
};

const tiny_erd_t appliance_api_clothes_washer_v2_wrinkle_free_option_erds[] = {
  0x210c,
  0x210d,
  0x210e,
};

const tiny_erd_t appliance_api_clothes_washer_v2_water_level_option_erds[] = {
  0x2106,
  0x2107,
  0x2108,
};

const tiny_erd_t appliance_api_clothes_washer_v2_wash_progress_option_erds[] = {
  0x2109,
  0x210a,
  0x210b,
};

const tiny_erd_t appliance_api_clothes_washer_v2_optimise_cycle_option_erds[] = {
  0x210f,
  0x2110,
  0x2111,
};

const tiny_erd_t appliance_api_clothes_washer_v2_time_saver_allowable_erds[] = {
  0x211a,
};

const tiny_erd_t appliance_api_clothes_washer_v2_prewash_allowable_erds[] = {
  0x2119,
};

const tiny_erd_t appliance_api_clothes_washer_v2_extra_rinse_option_allowable_erds[] = {
  0x2118,
};

const tiny_erd_t appliance_api_clothes_washer_v2_spin_level_option_allowable_erds[] = {
  0x20c7,
};

const tiny_erd_t appliance_api_clothes_washer_v2_water_temperature_option_allowable_erds[] = {
  0x20c6,
};

const tiny_erd_t appliance_api_clothes_washer_v2_soil_level_option_allowable_erds[] = {
  0x20c4,
};

const tiny_erd_t appliance_api_clothes_washer_v2_auto_soak_option_allowable_erds[] = {
  0x20c5,
};

const tiny_erd_t appliance_api_clothes_washer_v2_smart_dispense_adjustability_v2_erds[] = {
  0x20c0,
  0x20c1,
  0x20c2,
};

const tiny_erd_t appliance_api_clothes_washer_v2_commercial_laundry_payment_v1_attributes_erds[] = {
  0x211e,
};

const tiny_erd_t appliance_api_clothes_washer_v2_eco_option_erds[] = {
  0x2124,
  0x2125,
  0x214e,
};

const tiny_erd_t appliance_api_clothes_washer_v2_water_level_option_gea_erds[] = {
  0x212c,
  0x212d,
  0x215e,
};

const tiny_erd_t appliance_api_clothes_washer_v2_soaking_rinse_option_erds[] = {
  0x214b,
  0x214c,
  0x214d,
};

const tiny_erd_t appliance_api_clothes_washer_v2_bluetooth_dryer_control_erds[] = {
  0x2000,
  0x214f,
  0x2150,
  0x2151,
  0x2153,
  0x2157,
  0x2159,
  0x215a,
  0x215d,
};

const tiny_erd_t appliance_api_clothes_washer_v2_steam_erds[] = {
  0x2057,
  0x2058,
  0x2160,
};

const tiny_erd_t appliance_api_clothes_washer_v2_tumble_care_erds[] = {
  0x2060,
  0x2061,
  0x2161,
};

const tiny_erd_t appliance_api_clothes_washer_v2_commercial_laundry_coin_integration_erds[] = {
  0x2164,
  0x2165,
  0x2166,
  0x2167,
  0x2168,
  0x2169,
  0x216a,
  0x216b,
  0x216c,
  0x216d,
  0x2171,
};

const tiny_erd_t appliance_api_clothes_washer_v2_smart_wash_and_rinse_erds[] = {
  0x2172,
  0x2173,
};

const tiny_erd_t appliance_api_clothes_washer_v3_ultra_fresh_air_option_erds[] = {
  0x2089,
  0x208a,
};

const tiny_erd_t appliance_api_clothes_washer_v3_sanitize_option_erds[] = {
  0x207a,
  0x207b,
};

const tiny_erd_t appliance_api_clothes_washer_v3_washer_push_notifications_and_faults_erds[] = {
  0x222a,
  0x222b,
};

const tiny_erd_t appliance_api_clothes_washer_v3_flexible_dispensing_erds[] = {
  0x222c,
  0x222d,
  0x222e,
  0x222f,
  0x2233,
  0x2234,
};

const tiny_erd_t appliance_api_clothes_washer_v3_commercial_laundry_top_load_remote_parameters_erds[] = {
  0x2235,
  0x223c,
  0x223d,
  0x223e,
  0x2258,
  0x2259,
};

const tiny_erd_t appliance_api_clothes_washer_v3_commercial_laundry_front_load_remote_parameters_erds[] = {
  0x2235,
  0x2237,
  0x2238,
  0x2239,
  0x223a,
  0x223b,
  0x223f,
  0x2240,
  0x2241,
  0x2242,
  0x2243,
  0x2244,
  0x2245,
  0x2246,
  0x2247,
  0x2248,
  0x2249,
  0x224a,
  0x224b,
  0x2258,
  0x2259,
};

const tiny_erd_t appliance_api_clothes_washer_v4_mabe_washer_foundation_erds[] = {
  0x0004,
  0x000e,
  0x0034,
  0x2026,
  0x202b,
  0x202c,
  0x202d,
  0x2031,
  0x2043,
  0x2f00,
  0x2f01,
  0x2f02,
  0x2f03,
  0x2f04,
  0x2f05,
  0x2f06,
  0x2f07,
  0x2f08,
  0x2f09,
  0x2f0a,
  0x2f0b,
  0x2f0c,
  0x2f0d,
  0x2f0e,
  0x2f0f,
  0x2f10,
  0x2f11,
  0x2f12,
  0x2f13,
  0x2f14,
  0x2f15,
};

const tiny_erd_t appliance_api_clothes_washer_v4_mabe_washer_diagnostics_erds[] = {
  0x2f16,
  0x2f17,
  0x2f18,
  0x2f19,
  0x2f1a,
  0x2f1b,
};

const tiny_erd_t appliance_api_clothes_washer_v4_commercial_free_mode_erds[] = {
  0x2338,
  0x2339,
};

const tiny_erd_t appliance_api_clothes_washer_v4_remote_pet_hair_removal_option_erds[] = {
  0x2230,
  0x2231,
  0x2232,
};

const tiny_erd_t appliance_api_clothes_washer_v4_remote_wash2x_option_erds[] = {
  0x205a,
  0x205b,
};

const tiny_erd_t appliance_api_refrigeration_v1_foundation_erds[] = {
  0x1004,
  0x1005,
  0x1006,
  0x1007,
  0x1009,
  0x100a,
  0x100b,
  0x100d,
  0x100e,
  0x100f,
  0x1016,
  0x1019,
  0x101d,
  0x1100,
  0x1101,
  0x1102,
  0x1103,
  0x1104,
  0x1105,
  0x1122,
  0x1123,
  0x1151,
  0x1152,
  0x1153,
  0x1154,
  0x1155,
  0x1157,
  0x1158,
  0x1159,
  0x115a,
  0x115b,
};

const tiny_erd_t appliance_api_refrigeration_v1_hot_water_erds[] = {
  0x1010,
  0x1011,
  0x1017,
  0x1018,
};

const tiny_erd_t appliance_api_refrigeration_v1_single_serve_erds[] = {
  0x1106,
};

const tiny_erd_t appliance_api_refrigeration_v1_deli_pan_erds[] = {
  0x1012,
  0x1013,
  0x1156,
};

const tiny_erd_t appliance_api_refrigeration_v1_convertible_drawer_erds[] = {
  0x1020,
  0x1021,
  0x1022,
  0x1023,
  0x1026,
  0x111c,
  0x115d,
};

const tiny_erd_t appliance_api_refrigeration_v1_odor_filter_erds[] = {
  0x101c,
};

const tiny_erd_t appliance_api_refrigeration_v1_humidity_control_erds[] = {
  0x100c,
};

const tiny_erd_t appliance_api_refrigeration_v1_feature_pan_erds[] = {
  0x101a,
  0x101b,
};

const tiny_erd_t appliance_api_refrigeration_v1_door_in_door_as_independent_input_erds[] = {
  0x1025,
};

const tiny_erd_t appliance_api_refrigeration_v1_ff_light_control_erds[] = {
  0x1024,
};

const tiny_erd_t appliance_api_refrigeration_v1_presence_sensing_erds[] = {
  0x1028,
  0x1029,
};

const tiny_erd_t appliance_api_refrigeration_v1_nighttime_snack_mode_erds[] = {
  0x101e,
  0x101f,
};

const tiny_erd_t appliance_api_refrigeration_v1_notifications_erds[] = {
  0x105a,
  0x105b,
};

const tiny_erd_t appliance_api_refrigeration_v1_autofill_pitcher_erds[] = {
  0x1053,
  0x1054,
  0x1055,
  0x1056,
  0x115c,
};

const tiny_erd_t appliance_api_refrigeration_v2_foundation_erds[] = {
  0x101d,
};

const tiny_erd_t appliance_api_refrigeration_v2_fresh_food_actual_temperature_erds[] = {
  0x105c,
};

const tiny_erd_t appliance_api_refrigeration_v2_freezer_actual_temperature_erds[] = {
  0x105d,
};

const tiny_erd_t appliance_api_refrigeration_v2_hydration_station_erds[] = {
  0x1060,
};

const tiny_erd_t appliance_api_refrigeration_v2_left_side_freezer_door_status_erds[] = {
  0x120f,
  0x127c,
  0x1286,
};

const tiny_erd_t appliance_api_refrigeration_v2_right_side_fresh_food_door_status_erds[] = {
  0x120e,
  0x127a,
  0x127b,
  0x1285,
};

const tiny_erd_t appliance_api_refrigeration_v2_barcode_scanner_feature_power_erds[] = {
  0x125f,
  0x1260,
};

const tiny_erd_t appliance_api_refrigeration_v2_turbo_cool_erds[] = {
  0x120c,
  0x120d,
  0x12d0,
};

const tiny_erd_t appliance_api_refrigeration_v2_turbo_freeze_erds[] = {
  0x121a,
  0x121b,
  0x12d1,
};

const tiny_erd_t appliance_api_refrigeration_v2_flowmeter_ounces_dispensed_erds[] = {
  0x121c,
  0x121d,
};

const tiny_erd_t appliance_api_refrigeration_v2_presence_sensing_enabled_erds[] = {
  0x1222,
  0x1223,
  0x1226,
};

const tiny_erd_t appliance_api_refrigeration_v2_presence_sensed_activates_recess_light_erds[] = {
  0x1224,
  0x1225,
};

const tiny_erd_t appliance_api_refrigeration_v2_dimmable_light_0_percent_level_erds[] = {
  0x121e,
  0x121f,
  0x1220,
};

const tiny_erd_t appliance_api_refrigeration_v2_door_alarm_enable_erds[] = {
  0x1227,
  0x1228,
};

const tiny_erd_t appliance_api_refrigeration_v2_night_snack_mode_lighting_erds[] = {
  0x1229,
  0x122a,
};

const tiny_erd_t appliance_api_refrigeration_v2_v2_refrigeration_notifications_erds[] = {
  0x122e,
};

const tiny_erd_t appliance_api_refrigeration_v3_autofill_pitcher_enable_erds[] = {
  0x1053,
  0x1054,
  0x1055,
  0x1056,
};

const tiny_erd_t appliance_api_refrigeration_v3_kitchen_illumination_feature_enable_erds[] = {
  0x1255,
  0x1256,
  0x129e,
  0x129f,
};

const tiny_erd_t appliance_api_refrigeration_v3_door_alarm_timer_timeout_erds[] = {
  0x1258,
  0x1259,
  0x125a,
};

const tiny_erd_t appliance_api_refrigeration_v3_top_right_side_fresh_food_door_in_door_status_erds[] = {
  0x125b,
  0x127b,
  0x1287,
  0x1288,
};

const tiny_erd_t appliance_api_refrigeration_v3_left_side_fresh_food_door_status_erds[] = {
  0x125c,
  0x1279,
  0x127b,
  0x1289,
};

const tiny_erd_t appliance_api_refrigeration_v3_convertible_compartment_door_status_erds[] = {
  0x125d,
  0x128a,
  0x128b,
};

const tiny_erd_t appliance_api_refrigeration_v3_bottom_freezer_single_door_status_erds[] = {
  0x120f,
  0x127c,
  0x1286,
};

const tiny_erd_t appliance_api_refrigeration_v3_barcode_scanner_device_power_erds[] = {
  0x1263,
  0x1264,
  0x142c,
};

const tiny_erd_t appliance_api_refrigeration_v3_single_door_status_erds[] = {
  0x1267,
  0x128e,
  0x128f,
};

const tiny_erd_t appliance_api_refrigeration_v3_top_right_side_fresh_food_door_status_erds[] = {
  0x120e,
  0x127a,
  0x127b,
  0x1285,
};

const tiny_erd_t appliance_api_refrigeration_v3_top_left_side_fresh_food_door_status_erds[] = {
  0x125c,
  0x1279,
  0x127b,
  0x1289,
};

const tiny_erd_t appliance_api_refrigeration_v3_middle_convertible_compartment_single_door_status_erds[] = {
  0x125d,
  0x128a,
  0x128b,
};

const tiny_erd_t appliance_api_refrigeration_v3_bottom_left_side_freezer_door_status_erds[] = {
  0x120f,
  0x127c,
  0x1286,
};

const tiny_erd_t appliance_api_refrigeration_v3_bottom_right_convertible_compartment_door_status_erds[] = {
  0x125d,
  0x128a,
  0x128b,
};

const tiny_erd_t appliance_api_refrigeration_v4_top_freezer_single_door_status_erds[] = {
  0x120f,
  0x127c,
  0x1286,
};

const tiny_erd_t appliance_api_refrigeration_v4_bottom_fresh_food_single_door_status_erds[] = {
  0x120e,
  0x127a,
  0x127b,
  0x1285,
};

const tiny_erd_t appliance_api_refrigeration_v4_freezer_ice_rate_is_active_erds[] = {
  0x12a6,
};

const tiny_erd_t appliance_api_refrigeration_v4_variable_cube_size_data_erds[] = {
  0x12b9,
  0x12ba,
  0x12bb,
};

const tiny_erd_t appliance_api_refrigeration_v4_fridge_focus_camera_enable_erds[] = {
  0x12ec,
  0x12ed,
};

const tiny_erd_t appliance_api_refrigeration_v4_recess_light_brightness_control_erds[] = {
  0x1268,
  0x1269,
  0x126a,
};

const tiny_erd_t appliance_api_refrigeration_v4_power_outage_data_erds[] = {
  0x140d,
  0x140e,
};

const tiny_erd_t appliance_api_refrigeration_v5_variable_cube_size_data_0_erds[] = {
  0x14bd,
  0x14be,
  0x14bf,
};

const tiny_erd_t appliance_api_refrigeration_v5_variable_cube_size_data_2_erds[] = {
  0x14c0,
  0x14c1,
  0x14c2,
};

const tiny_erd_t appliance_api_refrigeration_v5_scanned_barcode_result_erds[] = {
  0x141f,
  0x14c8,
};

const tiny_erd_t appliance_api_refrigeration_v5_dimmable_light_0_warmth_percentage_erds[] = {
  0x14c3,
  0x14c4,
};

const tiny_erd_t appliance_api_refrigeration_v5_secondary_feature_pan_filtered_temperature_erds[] = {
  0x14c5,
};

const tiny_erd_t appliance_api_refrigeration_v5_system_power_setting_erds[] = {
  0x14c6,
  0x14c7,
};

const tiny_erd_t appliance_api_microwave_v1_foundation_erds[] = {
  0x0005,
  0x0050,
  0x0051,
  0x500f,
  0x5020,
  0x5300,
  0x5c00,
  0x5c01,
  0x5c02,
  0x5c03,
  0x5c04,
  0x5c05,
  0x5c06,
  0x5c07,
  0x5c08,
  0x5c09,
  0x5c0a,
  0x5c0b,
  0x5c0c,
  0x5c0d,
  0x5c0e,
  0x5c0f,
  0x5c10,
  0x5c11,
  0x5c12,
  0x5c13,
  0x5c14,
  0x5c15,
  0x5c16,
  0x5c17,
  0x5c18,
  0x5c19,
  0x5c1a,
  0x5c1b,
  0x5c1c,
  0x5c1d,
  0x5c1e,
  0x5c20,
  0x5c21,
  0x5c22,
  0x5c23,
  0x5c24,
  0x5c2c,
  0x5c2e,
  0x5c31,
};

const tiny_erd_t appliance_api_microwave_v1_vent_and_light_erds[] = {
  0x5b00,
  0x5b01,
  0x5b02,
  0x5b03,
};

const tiny_erd_t appliance_api_microwave_v1_convection_erds[] = {
  0x5101,
  0x5109,
  0x510a,
  0x510d,
  0x5c25,
  0x5c26,
  0x5c27,
  0x5c28,
  0x5c29,
  0x5c2a,
  0x5c2d,
  0x5c2f,
  0x5c30,
};

const tiny_erd_t appliance_api_microwave_v1_multi_stage_cook_erds[] = {
  0x5c3c,
  0x5c3d,
  0x5c3e,
};

const tiny_erd_t appliance_api_microwave_v1_convection_time_erds[] = {
  0x5c2b,
};

const tiny_erd_t appliance_api_microwave_v1_melt_and_steam_modes_erds[] = {
  0x5c32,
  0x5c33,
  0x5c34,
  0x5c35,
};

const tiny_erd_t appliance_api_advantium_v1_foundation_erds[] = {
  0x0007,
  0x0050,
  0x5008,
  0x5109,
  0x5400,
  0x5401,
  0x5402,
  0x5403,
  0x5404,
  0x5407,
  0x5408,
  0x540a,
};

const tiny_erd_t appliance_api_advantium_v1_precision_cooking_erds[] = {
  0x5405,
  0x5406,
  0x5409,
};

const tiny_erd_t appliance_api_advantium_v1_12_hour_shutoff_erds[] = {
  0x5000,
};

const tiny_erd_t appliance_api_advantium_v1_timer_end_tone_erds[] = {
  0x5001,
};

const tiny_erd_t appliance_api_advantium_v1_auto_recipe_conversion_erds[] = {
  0x5003,
};

const tiny_erd_t appliance_api_advantium_v1_remote_enabled_erds[] = {
  0x510a,
  0x5c14,
};

const tiny_erd_t appliance_api_advantium_v1_microwave_details_erds[] = {
  0x5c12,
};

const tiny_erd_t appliance_api_advantium_v1_recipes_erds[] = {
  0x5300,
};

const tiny_erd_t appliance_api_advantium_v1_notifications_erds[] = {
  0x5c18,
};

const tiny_erd_t appliance_api_advantium_v1_camera_erds[] = {
  0x0501,
  0x0504,
  0x0507,
};

const tiny_erd_t appliance_api_advantium_v1_remote_menu_cooking_erds[] = {
  0x5405,
  0x5412,
  0x5416,
  0x5417,
};

const tiny_erd_t appliance_api_dishwasher_v1_foundation_erds[] = {
  0x3001,
  0x3003,
  0x3007,
  0x3009,
  0x300e,
  0x301c,
  0x301d,
  0x301f,
  0x304e,
  0xd003,
  0xd004,
};

const tiny_erd_t appliance_api_dishwasher_v1_service_erds[] = {
  0x3000,
  0x3006,
  0x301a,
  0x301b,
  0x301e,
  0x3020,
  0x3035,
  0x3036,
  0x3037,
  0x3038,
  0x3039,
  0x303a,
  0x303d,
  0x303f,
  0x3042,
  0x3043,
  0x3045,
  0x3046,
  0x3049,
  0x3083,
  0x3084,
  0x3085,
  0x3100,
};

const tiny_erd_t appliance_api_dishwasher_v1_current_diagnostics_erds[] = {
  0x3103,
  0x3104,
  0x3105,
  0x3106,
  0x3107,
};

const tiny_erd_t appliance_api_dishwasher_v1_low_rinse_aid_erds[] = {
  0x3038,
  0x3101,
};

const tiny_erd_t appliance_api_dishwasher_v1_door_pocket_light_erds[] = {
  0x3108,
};

const tiny_erd_t appliance_api_dishwasher_v1_feature_configuration_erds[] = {
  0x3200,
};

const tiny_erd_t appliance_api_dishwasher_v1_remote_cycle_control_erds[] = {
  0x3087,
  0x3204,
};

const tiny_erd_t appliance_api_dishwasher_v1_cycle_settings_delay_start_erds[] = {
  0x321a,
};

const tiny_erd_t appliance_api_dishwasher_v1_cycle_settings_selected_cycle_erds[] = {
  0x321b,
};

const tiny_erd_t appliance_api_dishwasher_v1_cycle_settings_wash_temp_selection_erds[] = {
  0x321c,
};

const tiny_erd_t appliance_api_dishwasher_v1_cycle_settings_dry_temp_selection_erds[] = {
  0x321d,
};

const tiny_erd_t appliance_api_dishwasher_v1_cycle_settings_wash_zone_selection_erds[] = {
  0x321e,
};

const tiny_erd_t appliance_api_dishwasher_v1_cycle_settings_steam_option_erds[] = {
  0x321f,
};

const tiny_erd_t appliance_api_dishwasher_v1_cycle_settings_bottle_blast_option_erds[] = {
  0x3220,
};

const tiny_erd_t appliance_api_dishwasher_v1_cycle_settings_ultrafresh_option_erds[] = {
  0x3221,
};

const tiny_erd_t appliance_api_dishwasher_v1_cycle_definitions_erds[] = {
  0x3210,
  0x3211,
  0x3212,
  0x3213,
  0x3214,
  0x3215,
  0x3216,
  0x3217,
  0x3218,
  0x3219,
};

const tiny_erd_t appliance_api_dishwasher_v1_demo_mode_erds[] = {
  0x3109,
};

const tiny_erd_t appliance_api_dishwasher_v1_dry_heater_and_fan_system_erds[] = {
  0x304f,
  0x3074,
  0x3075,
};

const tiny_erd_t appliance_api_dishwasher_v1_demand_response_extension_erds[] = {
  0xd005,
  0xd007,
};

const tiny_erd_t appliance_api_dishwasher_v1_error_erds[] = {
  0x3008,
};

const tiny_erd_t appliance_api_dishwasher_v1_cycle_modifiers_erds[] = {
  0x3250,
  0x3251,
};

const tiny_erd_t appliance_api_dishwasher_v1_actual_and_target_pump_speeds_erds[] = {
  0x3223,
  0x3224,
  0x3225,
  0x3226,
};

const tiny_erd_t appliance_api_dishwasher_v1_current_diagnostics_v2_erds[] = {
  0x3107,
  0x3227,
  0x3228,
  0x3229,
};

const tiny_erd_t appliance_api_dishwasher_v1_door_fan_2_actual_speed_percentage_erds[] = {
  0x304b,
};

const tiny_erd_t appliance_api_dishwasher_v1_preference_options_erds[] = {
  0x3201,
  0x3202,
};

const tiny_erd_t appliance_api_dishwasher_v1_remaining_delay_start_time_erds[] = {
  0x300d,
};

const tiny_erd_t appliance_api_dishwasher_v1_inlet_flow_rate_erds[] = {
  0x3015,
};

const tiny_erd_t appliance_api_dishwasher_v1_motor_identification_data_erds[] = {
  0x322a,
};

const tiny_erd_t appliance_api_dishwasher_v1_cycle_settings_silverware_wash_option_erds[] = {
  0x322b,
};

const tiny_erd_t appliance_api_dishwasher_v1_rinse_and_hold_mode_option_erds[] = {
  0x3260,
  0x3261,
  0x3262,
};

const tiny_erd_t appliance_api_dishwasher_v1_custom_cycle_erds[] = {
  0x322c,
  0x322d,
};

const tiny_erd_t appliance_api_dishwasher_v1_my_favorites_erds[] = {
  0x322e,
  0x322f,
};

const tiny_erd_t appliance_api_dishwasher_v2_smart_assist_erds[] = {
  0x310a,
  0x310b,
  0x310c,
  0x310d,
};

const tiny_erd_t appliance_api_dishwasher_v2_filter_fresh_erds[] = {
  0x3221,
};

const tiny_erd_t appliance_api_dishwasher_v2_connect_plus_erds[] = {
  0x322e,
  0x322f,
};

const tiny_erd_t appliance_api_dishwasher_v2_latched_key_status_erds[] = {
  0x3230,
};

const tiny_erd_t appliance_api_dishwasher_v2_water_softener_setting_erds[] = {
  0x320a,
  0x320b,
};

const tiny_erd_t appliance_api_dishwasher_v2_auto_door_open_erds[] = {
  0x3263,
  0x3264,
};

const tiny_erd_t appliance_api_dishwasher_v2_foundation_2_erds[] = {
  0x3001,
  0x3003,
  0x3009,
  0xd003,
  0xd004,
};

const tiny_erd_t appliance_api_dishwasher_v2_cycle_indication_erds[] = {
  0x300e,
};

const tiny_erd_t appliance_api_dishwasher_v2_cycle_names_erds[] = {
  0x301c,
};

const tiny_erd_t appliance_api_dishwasher_v2_analog_door_latch_ad_counts_erds[] = {
  0x3088,
};

const tiny_erd_t appliance_api_cooking_v1_foundation_erds[] = {
  0x5007,
};

const tiny_erd_t appliance_api_cooking_v1_twelve_hour_shutoff_erds[] = {
  0x5000,
};

const tiny_erd_t appliance_api_cooking_v1_end_tone_setting_erds[] = {
  0x5001,
};

const tiny_erd_t appliance_api_cooking_v1_convection_conversion_erds[] = {
  0x5003,
};

const tiny_erd_t appliance_api_cooking_v1_upper_oven_foundation_erds[] = {
  0x5100,
  0x5101,
  0x5102,
  0x5104,
  0x5106,
  0x5108,
  0x5109,
  0x510a,
  0x510b,
  0x510d,
  0x5110,
  0x5111,
  0x5112,
  0x5113,
};

const tiny_erd_t appliance_api_cooking_v1_upper_oven_kitchen_timer_erds[] = {
  0x5105,
};

const tiny_erd_t appliance_api_cooking_v1_upper_oven_probe_erds[] = {
  0x5103,
  0x5107,
};

const tiny_erd_t appliance_api_cooking_v1_upper_oven_precision_cook_selection_erds[] = {
  0x540b,
  0x540d,
  0x5411,
};

const tiny_erd_t appliance_api_cooking_v1_lower_oven_foundation_erds[] = {
  0x5200,
  0x5201,
  0x5202,
  0x5204,
  0x5206,
  0x5208,
  0x5209,
  0x520a,
  0x520b,
  0x520d,
  0x5210,
  0x5211,
  0x5212,
  0x5213,
};

const tiny_erd_t appliance_api_cooking_v1_lower_oven_kitchen_timer_erds[] = {
  0x5205,
};

const tiny_erd_t appliance_api_cooking_v1_lower_oven_probe_erds[] = {
  0x5203,
  0x5207,
};

const tiny_erd_t appliance_api_cooking_v1_lower_oven_precision_cook_selection_erds[] = {
  0x540c,
  0x540e,
  0x5421,
};

const tiny_erd_t appliance_api_cooking_v1_warming_drawer_erds[] = {
  0x5009,
};

const tiny_erd_t appliance_api_cooking_v1_cooktop_foundation_erds[] = {
  0x551c,
  0x5520,
};

const tiny_erd_t appliance_api_cooking_v1_extended_cooktop_foundation_erds[] = {
  0x551c,
  0x5522,
};

const tiny_erd_t appliance_api_cooking_v1_cooktop_sous_vide_cooking_erds[] = {
  0x5670,
  0x5671,
  0x5672,
  0x5673,
  0x5674,
  0x5675,
  0x5676,
  0x5677,
  0x5678,
  0x58b0,
};

const tiny_erd_t appliance_api_cooking_v1_cooktop_closed_loop_cooking_erds[] = {
  0x5770,
  0x5771,
  0x5772,
  0x5773,
  0x5774,
  0x5775,
  0x5776,
  0x5777,
  0x5778,
  0x5779,
  0x577a,
  0x577b,
  0x577c,
  0x577d,
  0x577e,
  0x623c,
  0x623d,
  0x623e,
  0x623f,
  0x6240,
};

const tiny_erd_t appliance_api_cooking_v1_extended_cooktop_closed_loop_cooking_erds[] = {
  0x5786,
  0x5787,
  0x5788,
  0x5789,
};

const tiny_erd_t appliance_api_cooking_v1_built_in_pop_up_sensor_erds[] = {
  0x5521,
  0x577f,
};

const tiny_erd_t appliance_api_cooking_v1_gas_cooktop_lockout_erds[] = {
  0x5900,
  0x5901,
};

const tiny_erd_t appliance_api_cooking_v1_upper_automatic_door_open_erds[] = {
  0x510f,
  0x5118,
};

const tiny_erd_t appliance_api_cooking_v1_lower_automatic_door_open_erds[] = {
  0x520f,
  0x5218,
};

const tiny_erd_t appliance_api_cooking_v1_single_knob_ui_erds[] = {
  0x5012,
  0x5013,
  0x5014,
  0x5015,
  0x5016,
  0x5017,
  0x5020,
  0x5022,
};

const tiny_erd_t appliance_api_cooking_v1_default_set_temperature_range_erds[] = {
  0x5008,
};

const tiny_erd_t appliance_api_cooking_v1_oven_camera_erds[] = {
  0x0501,
  0x0502,
  0x0503,
  0x0504,
};

const tiny_erd_t appliance_api_cooking_v1_supported_cookware_erds[] = {
  0x576f,
};

const tiny_erd_t appliance_api_cooking_v1_key_press_data_erds[] = {
  0x5006,
};

const tiny_erd_t appliance_api_cooking_v1_menu_tree_erds[] = {
  0x5405,
  0x5412,
  0x5422,
};

const tiny_erd_t appliance_api_cooking_v1_precision_cook_mode_name_erds[] = {
  0x5410,
  0x5411,
  0x5420,
  0x5421,
};

const tiny_erd_t appliance_api_cooking_v1_oven_cavity_layout_erds[] = {
  0x5018,
};

const tiny_erd_t appliance_api_cooking_v1_accent_lighting_erds[] = {
  0x5023,
  0x5024,
  0x5025,
};

const tiny_erd_t appliance_api_cooking_v1_air_fry_set_temperature_range_erds[] = {
  0x5027,
};

const tiny_erd_t appliance_api_cooking_v2_upper_oven_baking_surface_erds[] = {
  0x510e,
  0x5114,
};

const tiny_erd_t appliance_api_cooking_v2_smarthq_bake_erds[] = {
  0x5060,
  0x5061,
};

const tiny_erd_t appliance_api_cooking_v2_cooktop_right_front_burner_termination_reason_erds[] = {
  0x562d,
};

const tiny_erd_t appliance_api_cooking_v2_clock_display_erds[] = {
  0x5019,
  0x501a,
};

const tiny_erd_t appliance_api_cooking_v2_recipe_api_erds[] = {
  0x5300,
};

const tiny_erd_t appliance_api_cooking_v2_water_reservoir_erds[] = {
  0x5523,
};

const tiny_erd_t appliance_api_cooking_v2_remote_ui_erds[] = {
  0x5070,
  0x5071,
};

const tiny_erd_t appliance_api_cooking_v2_upper_cavity_recipe_api_v2_erds[] = {
  0x5120,
  0x5121,
  0x5122,
  0x5124,
  0x5125,
  0x5126,
  0x5127,
  0x5128,
};

const tiny_erd_t appliance_api_cooking_v2_lower_cavity_recipe_api_v2_erds[] = {
  0x5220,
  0x5221,
  0x5222,
  0x5224,
  0x5225,
  0x5226,
  0x5227,
  0x5228,
};

const tiny_erd_t appliance_api_cooking_v2_upper_cavity_remote_precision_cook_erds[] = {
  0x5410,
  0x5411,
  0x5413,
  0x5414,
};

const tiny_erd_t appliance_api_cooking_v2_lower_cavity_remote_precision_cook_erds[] = {
  0x5420,
  0x5421,
  0x5423,
  0x5424,
};

const tiny_erd_t appliance_api_cooking_v2_upper_cavity_remote_precision_cook_v3_erds[] = {
  0x5405,
  0x540b,
  0x540d,
  0x5410,
  0x5411,
  0x5413,
  0x5415,
};

const tiny_erd_t appliance_api_cooking_v2_lower_cavity_remote_precision_cook_v3_erds[] = {
  0x5405,
  0x540c,
  0x540e,
  0x5420,
  0x5421,
  0x5422,
  0x5425,
};

const tiny_erd_t appliance_api_cooking_v2_cooktop_multi_burner_closed_loop_cooking_erds[] = {
  0x5770,
  0x5771,
  0x5772,
  0x5773,
  0x5774,
  0x5775,
  0x5776,
  0x5777,
  0x5778,
  0x5779,
  0x577a,
  0x577b,
  0x577c,
  0x577d,
  0x577e,
  0x5780,
  0x5781,
  0x5782,
  0x5783,
  0x578a,
  0x578b,
  0x578c,
  0x58b1,
  0x58b2,
  0x58b3,
  0x58b4,
  0x58b5,
  0x58b6,
  0x6200,
  0x6201,
  0x6202,
  0x6203,
  0x6204,
  0x6228,
  0x6229,
  0x622a,
  0x622b,
  0x622c,
  0x623c,
  0x623d,
  0x623e,
  0x623f,
  0x6240,
};

const tiny_erd_t appliance_api_cooking_v2_customer_feedback_erds[] = {
  0x5c40,
  0x5c41,
};

const tiny_erd_t appliance_api_cooking_v2_remote_bluetooth_device_management_erds[] = {
  0x6107,
  0x6200,
  0x6201,
  0x6202,
  0x6203,
  0x6204,
};

const tiny_erd_t* appliance_api_cooking_v2_stui_presence_erds __attribute__((unused)) = nullptr;

const tiny_erd_t* appliance_api_cooking_v2_sentry_erds __attribute__((unused)) = nullptr;

const tiny_erd_t appliance_api_cooking_v2_estimated_remaining_cook_minutes_erds[] = {
  0x5790,
  0x5791,
};

const tiny_erd_t appliance_api_cooking_v2_ai_cook_mode_session_id_erds[] = {
  0x5119,
  0x5219,
};

const tiny_erd_t appliance_api_cooking_v2_articulating_display_erds[] = {
  0x5028,
  0x5029,
  0x502a,
  0x502b,
};

const tiny_erd_t appliance_api_cooking_v2_ui_menu_tree_key_erds[] = {
  0x5412,
  0x5422,
};

const tiny_erd_t appliance_api_cooking_v2_upper_oven_user_interactive_precision_cooking_erds[] = {
  0x5150,
  0x5151,
};

const tiny_erd_t appliance_api_cooking_v2_lower_oven_user_interactive_precision_cooking_erds[] = {
  0x5250,
  0x5251,
};

const tiny_erd_t appliance_api_cooking_v2_automatic_door_opener_local_public_enable_erds[] = {
  0x502c,
};

const tiny_erd_t appliance_api_cooking_v2_oven_light_automatic_on_off_setting_erds[] = {
  0x5014,
  0x5015,
  0x502d,
};

const tiny_erd_t appliance_api_cooking_v2_enhanced_sabbath_erds[] = {
  0x502e,
  0x502f,
  0x5030,
  0x5031,
  0x5032,
  0x5033,
};

const tiny_erd_t appliance_api_cooking_v2_cook_cam_ai_assistant_erds[] = {
  0x504e,
  0x504f,
  0x5052,
  0x5053,
  0x5054,
};

const tiny_erd_t appliance_api_cooking_v2_cooktop_v2_notifications_erds[] = {
  0x5080,
  0x5081,
};

const tiny_erd_t appliance_api_cooking_v2_flex_cook_timer_erds[] = {
  0x511a,
  0x511b,
  0x511c,
  0x511d,
  0x511e,
  0x521a,
  0x521b,
  0x521c,
  0x521d,
  0x521e,
};

const tiny_erd_t* appliance_api_cooking_v2_microphone_supported_erds __attribute__((unused)) = nullptr;

const tiny_erd_t* appliance_api_cooking_v2_enhanced_speaker_supported_erds __attribute__((unused)) = nullptr;

const tiny_erd_t* appliance_api_cooking_v3_ui_misconfiguration_detection_erds __attribute__((unused)) = nullptr;

const tiny_erd_t appliance_api_cooking_v3_enhanced_sabbath_manual_mode_erds[] = {
  0x502e,
  0x5031,
  0x5032,
  0x5302,
  0x5303,
};

const tiny_erd_t appliance_api_cooking_v3_upper_local_available_cook_modes_erds[] = {
  0x512c,
};

const tiny_erd_t appliance_api_cooking_v3_lower_local_available_cook_modes_erds[] = {
  0x522c,
};

const tiny_erd_t appliance_api_cooking_v3_enhanced_sabbath_override_settings_erds[] = {
  0x5034,
  0x5035,
  0x5036,
  0x5037,
};

const tiny_erd_t appliance_api_window_ac_v1_foundation_erds[] = {
  0x7003,
  0x7261,
  0x7a00,
  0x7a01,
  0x7a02,
  0x7a04,
  0x7a0f,
  0x7a10,
  0x7b00,
  0x7b0b,
};

const tiny_erd_t appliance_api_window_ac_v1_demand_response_erds[] = {
  0xd005,
  0xd006,
};

const tiny_erd_t appliance_api_window_ac_v1_energy_reporting_erds[] = {
  0x7020,
  0x7a12,
  0x7a13,
  0x7a14,
};

const tiny_erd_t appliance_api_window_ac_v1_selectable_temperature_range_erds[] = {
  0x7b06,
};

const tiny_erd_t appliance_api_window_ac_v1_cycle_count_erds[] = {
  0x7050,
};

const tiny_erd_t appliance_api_window_ac_v1_temporary_offset_demand_response_erds[] = {
  0xd00b,
  0xd00c,
};

const tiny_erd_t appliance_api_window_ac_v1_heat_mode_erds[] = {
  0x7002,
};

const tiny_erd_t appliance_api_window_ac_v1_eco_mode_erds[] = {
  0x7b0c,
};

const tiny_erd_t appliance_api_window_ac_v1_turbo_and_quiet_erds[] = {
  0x795e,
  0x795f,
  0x7963,
};

const tiny_erd_t appliance_api_split_ac_v1_foundation_erds[] = {
  0x7003,
  0x7261,
  0x7a00,
  0x7a01,
  0x7a02,
  0x7a0f,
  0x7b00,
  0x7b05,
  0x7b06,
  0x7b07,
  0x7b08,
  0x7b09,
};

const tiny_erd_t appliance_api_split_ac_v1_remote_monitoring_erds[] = {
  0x7049,
  0x7100,
  0x7101,
  0x7130,
  0x7132,
  0x7963,
  0x7968,
};

const tiny_erd_t appliance_api_split_ac_v1_self_clean_erds[] = {
  0x796a,
  0x796d,
  0x796e,
  0x7978,
  0x7979,
  0x797a,
};

const tiny_erd_t appliance_api_split_ac_v1_external_resistance_heating_erds[] = {
  0x7966,
  0x7967,
  0x7977,
};

const tiny_erd_t appliance_api_split_ac_v1_air_damper_erds[] = {
  0x7972,
  0x7973,
  0x7974,
};

const tiny_erd_t appliance_api_split_ac_v1_remote_diagnostic_information_erds[] = {
  0x7960,
  0x796c,
  0x796f,
  0x7970,
  0x7971,
};

const tiny_erd_t appliance_api_split_ac_v1_vacation_mode_supported_erds[] = {
  0x7975,
  0x7976,
};

const tiny_erd_t appliance_api_split_ac_v1_updated_vacation_mode_erds[] = {
  0x7975,
  0x7976,
  0x797c,
};

const tiny_erd_t appliance_api_split_ac_v1_alerts_erds[] = {
  0x797b,
};

const tiny_erd_t appliance_api_split_ac_v1_temperature_display_mode_support_erds[] = {
  0x7980,
  0x7981,
};

const tiny_erd_t appliance_api_split_ac_v1_motion_sensing_mode_support_erds[] = {
  0x7982,
  0x7983,
};

const tiny_erd_t appliance_api_split_ac_v1_static_pressure_and_condensate_float_switch_support_erds[] = {
  0x797d,
  0x797e,
};

const tiny_erd_t appliance_api_split_ac_v1_rs485_communication_erds[] = {
  0x7990,
};

const tiny_erd_t appliance_api_split_ac_v1_outdoor_model_and_serial_number_erds[] = {
  0x7991,
  0x7992,
};

const tiny_erd_t appliance_api_split_ac_v1_refrigerant_leak_sensor_support_erds[] = {
  0x7852,
  0x7853,
  0x7854,
  0x7855,
  0x7856,
  0x7857,
  0x7858,
};

const tiny_erd_t appliance_api_split_ac_v1_notification_support_erds[] = {
  0x7850,
  0x7851,
};

const tiny_erd_t appliance_api_split_ac_v1_multimode_conflict_reporting_erds[] = {
  0x7859,
};

const tiny_erd_t* appliance_api_home_water_softener_v1_foundation_erds __attribute__((unused)) = nullptr;

const tiny_erd_t appliance_api_home_water_softener_v1_water_valve_erds[] = {
  0x1167,
};

const tiny_erd_t appliance_api_home_water_softener_v1_manual_override_erds[] = {
  0x1168,
};

const tiny_erd_t appliance_api_home_water_softener_v1_continuous_flow_time_limits_erds[] = {
  0x116a,
};

const tiny_erd_t appliance_api_home_water_softener_v1_flow_meter_erds[] = {
  0x1160,
};

const tiny_erd_t appliance_api_home_water_softener_v1_water_usage_erds[] = {
  0x1161,
  0x116d,
  0x800d,
  0x800e,
};

const tiny_erd_t appliance_api_home_water_softener_v1_water_auto_shutoff_erds[] = {
  0x1173,
  0x1174,
  0x8032,
  0x8033,
  0x8034,
};

const tiny_erd_t appliance_api_home_water_softener_v1_salt_information_erds[] = {
  0x8005,
};

const tiny_erd_t appliance_api_home_water_softener_v1_water_softener_alerts_legacy_erds[] = {
  0x1162,
  0x1169,
  0x8004,
  0x8007,
};

const tiny_erd_t* appliance_api_home_water_filter_v1_foundation_erds __attribute__((unused)) = nullptr;

const tiny_erd_t appliance_api_home_water_filter_v1_water_valve_erds[] = {
  0x115e,
  0x115f,
  0x1167,
};

const tiny_erd_t appliance_api_home_water_filter_v1_manual_override_erds[] = {
  0x1168,
};

const tiny_erd_t appliance_api_home_water_filter_v1_filter_expiration_limits_erds[] = {
  0x1165,
};

const tiny_erd_t appliance_api_home_water_filter_v1_continuous_flow_time_limits_erds[] = {
  0x116a,
};

const tiny_erd_t appliance_api_home_water_filter_v1_leak_detection_erds[] = {
  0x116e,
};

const tiny_erd_t appliance_api_home_water_filter_v1_flow_meter_erds[] = {
  0x1160,
};

const tiny_erd_t appliance_api_home_water_filter_v1_water_usage_erds[] = {
  0x1161,
  0x1162,
  0x116d,
};

const tiny_erd_t appliance_api_home_water_filter_v1_filter_status_erds[] = {
  0x1164,
  0x1179,
};

const tiny_erd_t appliance_api_home_water_filter_v1_reset_water_filter_erds[] = {
  0x1166,
};

const tiny_erd_t appliance_api_home_water_filter_v1_water_auto_shutoff_erds[] = {
  0x1173,
  0x1174,
};

const tiny_erd_t appliance_api_home_water_filter_v1_water_filter_alerts_legacy_erds[] = {
  0x1163,
  0x1169,
  0x116b,
  0x116c,
};

const tiny_erd_t appliance_api_home_water_filter_v1_notifications_erds[] = {
  0x1175,
};

const tiny_erd_t appliance_api_home_water_filter_v1_filter_1_status_erds[] = {
  0x1176,
  0x117a,
};

const tiny_erd_t appliance_api_home_water_filter_v1_filter_2_status_erds[] = {
  0x1177,
  0x117b,
};

const tiny_erd_t appliance_api_home_water_filter_v1_home_water_filter_platform_erds[] = {
  0x1178,
};

const tiny_erd_t appliance_api_delivery_box_v1_go_box_foundation_erds[] = {
  0x1016,
  0x116f,
  0x1170,
  0x1171,
  0x2013,
};

const tiny_erd_t appliance_api_delivery_box_v1_go_box_mini_foundation_erds[] = {
  0x1172,
  0x2012,
};

const tiny_erd_t appliance_api_zoneline_v1_foundation_erds[] = {
  0x7000,
  0x7001,
  0x7002,
  0x7003,
  0x7009,
  0x700a,
  0x700b,
  0x7100,
  0x7101,
  0x7102,
  0x7103,
  0x7104,
  0x7107,
  0x7114,
  0x7115,
  0x7120,
  0x7121,
  0x7130,
  0x7131,
  0x7132,
  0x7133,
  0x721f,
  0x723f,
  0x724f,
  0x725f,
  0x7401,
  0x7402,
  0x7404,
  0x7405,
  0x7406,
  0x7407,
  0x7408,
  0x7409,
  0x740a,
  0x740b,
  0x740c,
  0x740d,
  0x740e,
  0x740f,
  0x7410,
  0x7507,
};

const tiny_erd_t appliance_api_zoneline_v1_uvc_kit_erds[] = {
  0x73ff,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_base_erds[] = {
  0x0007,
  0x7010,
  0x7100,
  0x7101,
  0x7104,
  0x7120,
  0x7130,
  0x7131,
  0x7132,
  0x7133,
  0x7134,
  0x7135,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_heat_pump_erds[] = {
  0x745e,
  0x745f,
  0x7460,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_inverter_erds[] = {
  0x7601,
  0x7602,
  0x7612,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_eev_erds[] = {
  0x7511,
  0x7512,
  0x7513,
  0x7514,
  0x7515,
  0x7516,
  0x7517,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_mua_erds[] = {
  0x7464,
  0x7465,
  0x7466,
  0x7467,
  0x7468,
  0x7469,
  0x750b,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_uvc_erds[] = {
  0x7462,
  0x7463,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_dehum_erds[] = {
  0x746a,
  0x746b,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_od_sensors_erds[] = {
  0x7102,
  0x7103,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_id_outlet_sensor_erds[] = {
  0x7106,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_eev_sensors_erds[] = {
  0x7108,
  0x710c,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_od_coil_liquid_sensor_erds[] = {
  0x710d,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_humidity_sensor_erds[] = {
  0x710b,
  0x7118,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_base_aux_modes_erds[] = {
  0x7450,
  0x7451,
  0x7452,
  0x7453,
  0x7454,
  0x7455,
  0x7456,
  0x7457,
  0x7458,
  0x7459,
  0x745a,
  0x745b,
  0x745c,
  0x745d,
  0x746c,
  0x746d,
};

const tiny_erd_t appliance_api_zoneline_v1_stratos_heat_source_optimization_erds[] = {
  0x7472,
  0x7473,
};

const tiny_erd_t appliance_api_hood_v1_foundation_erds[] = {
  0x500f,
  0x5020,
  0x5105,
  0x5b00,
  0x5b01,
  0x5b02,
  0x5b03,
};

const tiny_erd_t appliance_api_hood_v1_hood_delays_erds[] = {
  0x5b04,
  0x5b05,
};

const tiny_erd_t appliance_api_hood_v1_hub_support_erds[] = {
  0x5b06,
  0x5b07,
  0x5b08,
  0x5b09,
  0x5b0a,
  0x5b0b,
};

const tiny_erd_t appliance_api_hood_v1_hood_notifications_erds[] = {
  0x5b0c,
  0x5b0d,
};

const tiny_erd_t appliance_api_hood_v1_hood_boost_parameters_erds[] = {
  0x5b0e,
  0x5b10,
};

const tiny_erd_t appliance_api_hood_v1_hood_delay_off_parameters_erds[] = {
  0x5b0f,
  0x5b11,
};

const tiny_erd_t appliance_api_hood_v1_hood_notifications_v2_erds[] = {
  0x5b3e,
  0x5b48,
};

const tiny_erd_t appliance_api_hood_v1_hood_fan_run_time_erds[] = {
  0x5b0d,
  0x5b49,
  0x5b4a,
};

const tiny_erd_t appliance_api_portable_ac_v1_foundation_erds[] = {
  0x7003,
  0x7a00,
  0x7a01,
  0x7a02,
  0x7a0f,
  0x7b00,
  0x7b0b,
};

const tiny_erd_t appliance_api_portable_ac_v1_demand_response_erds[] = {
  0xd005,
  0xd006,
};

const tiny_erd_t appliance_api_portable_ac_v1_energy_reporting_erds[] = {
  0x7020,
  0x7a13,
  0x7a14,
  0xd007,
};

const tiny_erd_t appliance_api_portable_ac_v1_notification_support_erds[] = {
  0x7835,
  0x7836,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v1_act_like_a_washer_erds[] = {
  0x2000,
  0x2001,
  0x2002,
  0x2007,
  0x2008,
  0x2009,
  0x200a,
  0x2010,
  0x2012,
  0x2013,
  0x2014,
  0x2015,
  0x2016,
  0x2017,
  0x2018,
  0x201d,
  0x2020,
  0x2021,
  0x2038,
  0x2039,
  0x2040,
  0x2069,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_foundation_erds[] = {
  0x2000,
  0x2001,
  0x2007,
  0x200a,
  0x2026,
  0x202c,
  0x2042,
  0x2043,
  0x2122,
  0x2123,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_wash_mode_option_erds[] = {
  0x2126,
  0x2127,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_soil_option_erds[] = {
  0x20be,
  0x20bf,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_water_temperature_option_erds[] = {
  0x20bb,
  0x20bc,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_extra_rinse_option_erds[] = {
  0x20b3,
  0x20b4,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_prewash_option_erds[] = {
  0x205a,
  0x205b,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_more_water_toggle_option_erds[] = {
  0x20ad,
  0x20ae,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_spin_level_option_erds[] = {
  0x212a,
  0x2139,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_dry_mode_option_erds[] = {
  0x2128,
  0x2129,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_damp_alert_option_and_status_erds[] = {
  0x2047,
  0x2048,
  0x204a,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_wrinkle_care_option_erds[] = {
  0x2051,
  0x2053,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_timed_dry_option_erds[] = {
  0x2134,
  0x2137,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_air_fluff_option_erds[] = {
  0x2089,
  0x208a,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_ecocool_drying_option_erds[] = {
  0x2131,
  0x2132,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_more_dry_toggle_option_erds[] = {
  0x213a,
  0x213b,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_downloaded_cycle_erds[] = {
  0x20d1,
  0x20d2,
  0x20d3,
  0x20d7,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_delay_start_erds[] = {
  0x2010,
  0x2038,
  0x2039,
  0x2040,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_remote_start_erds[] = {
  0x200a,
  0x2040,
  0x212b,
  0x2149,
  0x214a,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_add_dry_erds[] = {
  0x2007,
  0x2123,
  0x2129,
  0x212e,
  0x2133,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_adaptive_smart_dispense_and_adjustability_detergent_tank_erds[] = {
  0x2036,
  0x203a,
  0x203b,
  0x203c,
  0x203d,
  0x203e,
  0x2070,
  0x2071,
  0x2072,
  0x2073,
  0x20c0,
  0x20c1,
  0x20c2,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_adaptive_smart_dispense_and_adjustability_softener_tank_erds[] = {
  0x213c,
  0x213d,
  0x213e,
  0x213f,
  0x2140,
  0x2141,
  0x2142,
  0x2143,
  0x2144,
  0x2145,
  0x2146,
  0x2147,
  0x2148,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_local_wash_complete_indication_erds[] = {
  0x2155,
  0x2156,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_push_notifications_erds[] = {
  0x2154,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_remote_cycle_selection_erds[] = {
  0x20a7,
  0x20a8,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_sanitize_option_erds[] = {
  0x207a,
  0x207b,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_dry_time_selection_erds[] = {
  0x20c8,
  0x20ca,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_filter_microswitch_status_erds[] = {
  0x215f,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_control_lock_option_erds[] = {
  0x2083,
  0x2084,
  0x2085,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_cycle_function_erds[] = {
  0x20dc,
  0x20de,
};

const tiny_erd_t appliance_api_combination_washer_dryer_v2_remote_pet_hair_removal_option_erds[] = {
  0x2230,
  0x2231,
  0x2232,
};

const tiny_erd_t appliance_api_dual_zone_wine_chiller_v1_foundation_erds[] = {
  0x1004,
  0x1005,
  0x1006,
  0x100b,
  0x1016,
  0x101d,
  0x102c,
  0x102d,
  0x102e,
  0x1100,
  0x1101,
  0x1105,
  0x1126,
  0x1127,
  0x1151,
  0x1152,
  0x1154,
};

const tiny_erd_t appliance_api_dual_zone_wine_chiller_v1_light_control_erds[] = {
  0x1024,
};

const tiny_erd_t appliance_api_dual_zone_wine_chiller_v1_presence_sensing_erds[] = {
  0x1029,
};

const tiny_erd_t appliance_api_beverage_center_v1_foundation_erds[] = {
  0x1004,
  0x1005,
  0x1006,
  0x100b,
  0x100f,
  0x1016,
  0x101d,
  0x102c,
  0x102d,
  0x102e,
  0x1100,
  0x1101,
  0x1105,
  0x1126,
  0x1127,
  0x1151,
  0x1152,
  0x1154,
};

const tiny_erd_t appliance_api_beverage_center_v1_light_control_erds[] = {
  0x1024,
};

const tiny_erd_t appliance_api_beverage_center_v1_presence_sensing_erds[] = {
  0x1029,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_foundation_erds[] = {
  0x9000,
  0x9001,
  0x9002,
  0x9003,
  0x9004,
  0x9005,
  0x9006,
  0x9007,
  0x9008,
  0x9009,
  0x900a,
  0x900b,
  0x900c,
  0x9016,
  0x9017,
  0x9018,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_custom_brew_erds[] = {
  0x900d,
  0x900e,
  0x900f,
  0x9010,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_filter_erds[] = {
  0x9011,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_pot_presence_erds[] = {
  0x9012,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_out_of_water_detection_erds[] = {
  0x9013,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_outlet_temperature_erds[] = {
  0x9014,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_descale_erds[] = {
  0x9015,
  0x9019,
  0x901a,
  0x901b,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_delay_start_erds[] = {
  0x905d,
  0x905e,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_model_configuration_erds[] = {
  0x905c,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_three_favorite_brews_erds[] = {
  0x9054,
  0x9055,
  0x9056,
  0x9057,
  0x9058,
  0x9059,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_supported_features_erds[] = {
  0x905f,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_filter_status_erds[] = {
  0x9104,
  0x911d,
};

const tiny_erd_t appliance_api_coffee_brewer_v1_filter_usage_erds[] = {
  0x9136,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_foundation_erds[] = {
  0x9051,
  0x9052,
  0x905a,
  0x905b,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_three_favorite_brews_erds[] = {
  0x9054,
  0x9055,
  0x9056,
  0x9057,
  0x9058,
  0x9059,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_brew_cancel_erds[] = {
  0x900c,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_brew_status_erds[] = {
  0x9000,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_descale_status_erds[] = {
  0x9030,
  0x9044,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_filter_status_erds[] = {
  0x9104,
  0x911d,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_pot_presence_erds[] = {
  0x9012,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_out_of_water_detection_erds[] = {
  0x9013,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_out_of_beans_erds[] = {
  0x902d,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_grinder_missing_or_not_locked_erds[] = {
  0x902b,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_tank_missing_or_not_locked_erds[] = {
  0x904e,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_brew_basket_missing_or_not_locked_erds[] = {
  0x904f,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_clean_brew_basket_erds[] = {
  0x902c,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_disable_grinder_erds[] = {
  0x9050,
  0x9053,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_grind_setting_erds[] = {
  0x904d,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_standby_erds[] = {
  0x9024,
  0x9025,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_model_configuration_erds[] = {
  0x905c,
};

const tiny_erd_t appliance_api_coffee_brewer_v2_delay_start_erds[] = {
  0x905d,
  0x905e,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v1_foundation_erds[] = {
  0x9100,
  0x9101,
  0x9102,
  0x9107,
  0x9108,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v1_filter_status_erds[] = {
  0x9104,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v1_cleaning_required_erds[] = {
  0x9106,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v1_filter_reset_erds[] = {
  0x911d,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v1_ice_maker_push_notifications_erds[] = {
  0x9135,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v1_filter_usage_details_erds[] = {
  0x9136,
  0x9137,
  0x9138,
  0x9139,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v1_descale_time_analytics_erds[] = {
  0x913a,
  0x913d,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v1_sanitize_elapsed_time_erds[] = {
  0x913b,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v1_total_compressor_runtime_erds[] = {
  0x913c,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v1_model_configuration_erds[] = {
  0x9134,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v1_multicolored_light_erds[] = {
  0x913e,
  0x913f,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v1_water_level_tone_setting_supported_erds[] = {
  0x9140,
  0x9141,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_foundation_erds[] = {
  0x9100,
  0x9107,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_filter_status_erds[] = {
  0x9104,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_cleaning_required_erds[] = {
  0x9106,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_filter_reset_erds[] = {
  0x911d,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_light_control_erds[] = {
  0x9101,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_report_ice_production_erds[] = {
  0x9108,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_model_configuration_erds[] = {
  0x9134,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_ice_maker_push_notifications_erds[] = {
  0x9135,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_filter_usage_details_erds[] = {
  0x9136,
  0x9137,
  0x9138,
  0x9139,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_descale_time_analytics_erds[] = {
  0x913a,
  0x913d,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_sanitize_elapsed_time_erds[] = {
  0x913b,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_total_compressor_runtime_erds[] = {
  0x913c,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_multicolored_light_erds[] = {
  0x913e,
  0x913f,
};

const tiny_erd_t appliance_api_opal_nugget_ice_maker_v2_water_level_tone_setting_supported_erds[] = {
  0x9140,
  0x9141,
};

const tiny_erd_t appliance_api_in_home_grower_v1_foundation_erds[] = {
  0x1300,
  0x1305,
  0x1306,
  0x1307,
  0x1308,
  0x1309,
  0x130a,
  0x130b,
  0x130c,
  0x130d,
  0x130e,
  0x130f,
  0x1310,
  0x1315,
  0x1316,
  0x1326,
  0x1327,
  0x1328,
};

const tiny_erd_t appliance_api_in_home_grower_v1_water_filter_erds[] = {
  0x1019,
  0x1304,
  0x131f,
};

const tiny_erd_t appliance_api_in_home_grower_v1_nutrient_tank_1_erds[] = {
  0x1301,
  0x1321,
};

const tiny_erd_t appliance_api_in_home_grower_v1_nutrient_tank_2_erds[] = {
  0x1302,
  0x1321,
};

const tiny_erd_t appliance_api_in_home_grower_v1_nutrient_tank_3_erds[] = {
  0x1303,
  0x1321,
};

const tiny_erd_t appliance_api_in_home_grower_v1_nutrient_tank_4_erds[] = {
  0x131b,
  0x1321,
};

const tiny_erd_t appliance_api_in_home_grower_v1_nutrient_tank_5_erds[] = {
  0x131c,
  0x1321,
};

const tiny_erd_t appliance_api_in_home_grower_v1_nutrient_tank_6_erds[] = {
  0x131d,
  0x1321,
};

const tiny_erd_t appliance_api_in_home_grower_v1_tri_chamber_system_erds[] = {
  0x1311,
  0x1312,
  0x1313,
  0x1318,
  0x1319,
  0x131a,
};

const tiny_erd_t appliance_api_in_home_grower_v1_display_always_on_feature_erds[] = {
  0x102d,
  0x1317,
};

const tiny_erd_t appliance_api_in_home_grower_v1_air_filter_erds[] = {
  0x131e,
  0x1320,
};

const tiny_erd_t appliance_api_in_home_grower_v1_clean_cycle_erds[] = {
  0x1322,
  0x1323,
  0x1324,
  0x1325,
};

const tiny_erd_t appliance_api_in_home_grower_v1_water_intake_erds[] = {
  0x1314,
};

const tiny_erd_t appliance_api_in_home_grower_v1_debug_packed_erds_1_erds[] = {
  0x1329,
};

const tiny_erd_t appliance_api_in_home_grower_v1_debug_packed_erds_2_erds[] = {
  0x132a,
};

const tiny_erd_t appliance_api_in_home_grower_v1_debug_packed_erds_3_erds[] = {
  0x132b,
};

const tiny_erd_t appliance_api_in_home_grower_v1_debug_packed_erds_4_erds[] = {
  0x132c,
};

const tiny_erd_t appliance_api_dehumidifier_v1_foundation_erds[] = {
  0x7261,
  0x7832,
  0x7a00,
  0x7a0f,
  0x7b01,
  0x7b02,
  0x7b03,
  0x7b0b,
};

const tiny_erd_t appliance_api_dehumidifier_v1_energy_reporting_erds[] = {
  0x7020,
  0x7a12,
  0x7a13,
  0x7a14,
  0xd005,
};

const tiny_erd_t appliance_api_dehumidifier_v1_pump_erds[] = {
  0x7830,
  0x7831,
};

const tiny_erd_t appliance_api_dehumidifier_v1_nonstop_mode_erds[] = {
  0x7833,
  0x7834,
};

const tiny_erd_t appliance_api_under_counter_ice_maker_v1_foundation_erds[] = {
  0x9100,
  0x9101,
  0x9102,
  0x9105,
  0x9107,
  0x9108,
  0x910a,
  0x910b,
  0x910c,
  0x910f,
};

const tiny_erd_t appliance_api_under_counter_ice_maker_v1_cube_model_adjustments_erds[] = {
  0x9109,
  0x9132,
  0x9133,
};

const tiny_erd_t appliance_api_through_wall_ac_v1_foundation_erds[] = {
  0x7003,
  0x7a00,
  0x7a01,
  0x7a02,
  0x7a04,
  0x7a0f,
  0x7b00,
  0x7b0b,
  0x7b0e,
  0x7b0f,
};

const tiny_erd_t appliance_api_through_wall_ac_v1_demand_response_erds[] = {
  0xd005,
  0xd006,
};

const tiny_erd_t appliance_api_through_wall_ac_v1_heat_erds[] = {
  0x7966,
  0x7967,
};

const tiny_erd_t appliance_api_through_wall_ac_v1_demand_response_available_erds[] = {
  0xd008,
};

const tiny_erd_t appliance_api_espresso_maker_v1_foundation_erds[] = {
  0x901c,
  0x901d,
  0x901e,
  0x9023,
  0x9043,
  0x9045,
  0x9047,
};

const tiny_erd_t appliance_api_espresso_maker_v1_rinse_erds[] = {
  0x9021,
  0x9022,
};

const tiny_erd_t appliance_api_espresso_maker_v1_standby_erds[] = {
  0x9024,
  0x9025,
};

const tiny_erd_t appliance_api_espresso_maker_v1_out_of_water_detection_erds[] = {
  0x9013,
};

const tiny_erd_t appliance_api_espresso_maker_v1_emptying_erds[] = {
  0x9027,
};

const tiny_erd_t appliance_api_espresso_maker_v1_grind_hopper_presence_detection_erds[] = {
  0x902b,
};

const tiny_erd_t appliance_api_espresso_maker_v1_bean_presence_detection_erds[] = {
  0x902d,
};

const tiny_erd_t appliance_api_espresso_maker_v1_water_hardness_erds[] = {
  0x902e,
  0x9042,
};

const tiny_erd_t appliance_api_espresso_maker_v1_door_open_or_module_missing_erds[] = {
  0x902f,
};

const tiny_erd_t appliance_api_espresso_maker_v1_descale_erds[] = {
  0x9019,
  0x9030,
  0x9044,
};

const tiny_erd_t appliance_api_espresso_maker_v1_steam_knob_position_erds[] = {
  0x9031,
};

const tiny_erd_t appliance_api_espresso_maker_v1_empty_waste_indicator_erds[] = {
  0x902c,
};

const tiny_erd_t appliance_api_espresso_maker_v1_overpressure_detected_erds[] = {
  0x9046,
};

const tiny_erd_t appliance_api_espresso_maker_v1_espresso_supported_erds[] = {
  0x9034,
  0x9035,
};

const tiny_erd_t appliance_api_espresso_maker_v1_americano_supported_erds[] = {
  0x9029,
  0x902a,
};

const tiny_erd_t appliance_api_espresso_maker_v1_lungo_supported_erds[] = {
  0x9032,
  0x9033,
};

const tiny_erd_t appliance_api_espresso_maker_v1_hot_water_supported_erds[] = {
  0x9036,
  0x9037,
};

const tiny_erd_t appliance_api_espresso_maker_v1_steam_supported_erds[] = {
  0x901f,
  0x9020,
  0x9038,
  0x9039,
};

const tiny_erd_t appliance_api_espresso_maker_v1_ristretto_supported_erds[] = {
  0x903a,
  0x903b,
};

const tiny_erd_t appliance_api_espresso_maker_v1_doppio_supported_erds[] = {
  0x903c,
  0x903d,
};

const tiny_erd_t appliance_api_espresso_maker_v1_triple_shot_supported_erds[] = {
  0x903e,
  0x903f,
};

const tiny_erd_t appliance_api_espresso_maker_v1_red_eye_supported_erds[] = {
  0x9040,
  0x9041,
};

const tiny_erd_t appliance_api_espresso_maker_v1_grinder_status_supported_erds[] = {
  0x9048,
  0x9049,
};

const tiny_erd_t appliance_api_espresso_maker_v1_mybrew_mycup_supported_erds[] = {
  0x904a,
  0x904b,
};

const tiny_erd_t appliance_api_espresso_maker_v1_steam_without_time_supported_erds[] = {
  0x9038,
  0x9039,
};

const tiny_erd_t appliance_api_toaster_oven_v1_foundation_erds[] = {
  0x510a,
  0x9200,
  0x9207,
  0x9208,
  0x9209,
  0x9225,
  0x9228,
  0x9229,
  0x922a,
  0x922b,
  0x922c,
  0x922d,
  0x922e,
  0x922f,
};

const tiny_erd_t appliance_api_toaster_oven_v1_cavity_light_supported_erds[] = {
  0x9201,
  0x9202,
};

const tiny_erd_t appliance_api_toaster_oven_v1_lcd_brightness_supported_erds[] = {
  0x9203,
  0x9204,
};

const tiny_erd_t appliance_api_toaster_oven_v1_door_alarm_supported_erds[] = {
  0x9205,
  0x9206,
  0x9224,
};

const tiny_erd_t appliance_api_toaster_oven_v1_air_fry_supported_erds[] = {
  0x920a,
  0x920b,
};

const tiny_erd_t appliance_api_toaster_oven_v1_bake_supported_erds[] = {
  0x920c,
  0x920d,
};

const tiny_erd_t appliance_api_toaster_oven_v1_broil_supported_erds[] = {
  0x920e,
  0x920f,
};

const tiny_erd_t appliance_api_toaster_oven_v1_roast_supported_erds[] = {
  0x9210,
  0x9211,
};

const tiny_erd_t appliance_api_toaster_oven_v1_reheat_supported_erds[] = {
  0x9212,
  0x9213,
};

const tiny_erd_t appliance_api_toaster_oven_v1_warm_supported_erds[] = {
  0x9214,
  0x9215,
};

const tiny_erd_t appliance_api_toaster_oven_v1_slow_cook_supported_erds[] = {
  0x9216,
  0x9217,
};

const tiny_erd_t appliance_api_toaster_oven_v1_dehydrate_supported_erds[] = {
  0x9218,
  0x9219,
};

const tiny_erd_t appliance_api_toaster_oven_v1_proof_supported_erds[] = {
  0x921a,
  0x921b,
};

const tiny_erd_t appliance_api_toaster_oven_v1_cookie_supported_erds[] = {
  0x921c,
  0x921d,
};

const tiny_erd_t appliance_api_toaster_oven_v1_pizza_supported_erds[] = {
  0x921e,
  0x921f,
};

const tiny_erd_t appliance_api_toaster_oven_v1_bagel_supported_erds[] = {
  0x9220,
  0x9221,
};

const tiny_erd_t appliance_api_toaster_oven_v1_toast_supported_erds[] = {
  0x9222,
  0x9223,
};

const tiny_erd_t appliance_api_toaster_oven_v1_crisp_finish_supported_erds[] = {
  0x9226,
  0x9227,
};

const tiny_erd_t appliance_api_toaster_oven_v1_cake_supported_erds[] = {
  0x9230,
  0x9231,
};

const tiny_erd_t appliance_api_toaster_oven_v1_cookie_with_preferences_supported_erds[] = {
  0x9232,
  0x9233,
};

const tiny_erd_t appliance_api_toaster_oven_v1_pizza_with_preferences_supported_erds[] = {
  0x9234,
  0x9235,
};

const tiny_erd_t appliance_api_toaster_oven_v1_cooking_push_notifications_erds[] = {
  0x9236,
};

const tiny_erd_t appliance_api_toaster_oven_v1_cooktime_modification_erds[] = {
  0x5c1a,
  0x5c1b,
};

const tiny_erd_t appliance_api_toaster_oven_v1_model_configuration_erds[] = {
  0x9237,
};

const tiny_erd_t appliance_api_toaster_oven_v2_foundation_erds[] = {
  0x9200,
  0x9207,
  0x9208,
  0x9209,
  0x9228,
  0x922f,
};

const tiny_erd_t appliance_api_toaster_oven_v2_cavity_light_supported_erds[] = {
  0x9201,
  0x9202,
};

const tiny_erd_t appliance_api_toaster_oven_v2_lcd_brightness_supported_erds[] = {
  0x9203,
  0x9204,
};

const tiny_erd_t appliance_api_toaster_oven_v2_door_alarm_supported_erds[] = {
  0x9205,
  0x9206,
  0x9224,
};

const tiny_erd_t appliance_api_toaster_oven_v2_air_fry_supported_erds[] = {
  0x920a,
  0x920b,
};

const tiny_erd_t appliance_api_toaster_oven_v2_bake_supported_erds[] = {
  0x920c,
  0x920d,
};

const tiny_erd_t appliance_api_toaster_oven_v2_broil_supported_erds[] = {
  0x920e,
  0x920f,
};

const tiny_erd_t appliance_api_toaster_oven_v2_roast_supported_erds[] = {
  0x9210,
  0x9211,
};

const tiny_erd_t appliance_api_toaster_oven_v2_reheat_supported_erds[] = {
  0x9212,
  0x9213,
};

const tiny_erd_t appliance_api_toaster_oven_v2_warm_supported_erds[] = {
  0x9214,
  0x9215,
};

const tiny_erd_t appliance_api_toaster_oven_v2_slow_cook_supported_erds[] = {
  0x9216,
  0x9217,
};

const tiny_erd_t appliance_api_toaster_oven_v2_dehydrate_supported_erds[] = {
  0x9218,
  0x9219,
};

const tiny_erd_t appliance_api_toaster_oven_v2_proof_supported_erds[] = {
  0x921a,
  0x921b,
};

const tiny_erd_t appliance_api_toaster_oven_v2_cookie_supported_erds[] = {
  0x921c,
  0x921d,
};

const tiny_erd_t appliance_api_toaster_oven_v2_pizza_supported_erds[] = {
  0x921e,
  0x921f,
};

const tiny_erd_t appliance_api_toaster_oven_v2_bagel_supported_erds[] = {
  0x9220,
  0x9221,
};

const tiny_erd_t appliance_api_toaster_oven_v2_toast_supported_erds[] = {
  0x9222,
  0x9223,
};

const tiny_erd_t appliance_api_toaster_oven_v2_crisp_finish_supported_erds[] = {
  0x9226,
  0x9227,
};

const tiny_erd_t appliance_api_toaster_oven_v2_cake_supported_erds[] = {
  0x9230,
  0x9231,
};

const tiny_erd_t appliance_api_toaster_oven_v2_cookie_with_preferences_supported_erds[] = {
  0x9232,
  0x9233,
};

const tiny_erd_t appliance_api_toaster_oven_v2_pizza_with_preferences_supported_erds[] = {
  0x9234,
  0x9235,
};

const tiny_erd_t appliance_api_toaster_oven_v2_cooking_push_notifications_erds[] = {
  0x9236,
};

const tiny_erd_t appliance_api_toaster_oven_v2_cooktime_modification_erds[] = {
  0x5c1a,
  0x5c1b,
};

const tiny_erd_t appliance_api_toaster_oven_v2_model_configuration_erds[] = {
  0x9237,
};

const tiny_erd_t appliance_api_toaster_oven_v2_preheat_control_erds[] = {
  0x9229,
  0x922a,
};

const tiny_erd_t appliance_api_toaster_oven_v2_convection_fan_control_erds[] = {
  0x922b,
  0x922c,
};

const tiny_erd_t appliance_api_toaster_oven_v2_reset_user_cooking_parameters_to_default_erds[] = {
  0x9225,
};

const tiny_erd_t appliance_api_toaster_oven_v2_remote_start_erds[] = {
  0x510a,
  0x922d,
};

const tiny_erd_t appliance_api_toaster_oven_v2_convection_bake_supported_erds[] = {
  0x9238,
};

const tiny_erd_t appliance_api_zoneline_vertical_v1_foundation_erds[] = {
  0x7000,
  0x7001,
  0x7002,
  0x7003,
  0x7009,
  0x700a,
  0x700b,
  0x7010,
  0x7020,
  0x7021,
  0x7022,
  0x7023,
  0x7100,
  0x7101,
  0x7102,
  0x7103,
  0x7104,
  0x7106,
  0x7107,
  0x710b,
  0x7114,
  0x7115,
  0x7118,
  0x7120,
  0x7123,
  0x7130,
  0x7131,
  0x7132,
  0x7133,
  0x7134,
  0x7135,
  0x721f,
  0x723f,
  0x724f,
  0x725f,
  0x7261,
  0x7263,
  0x7270,
  0x7401,
  0x7402,
  0x7404,
  0x7405,
  0x7406,
  0x7407,
  0x7408,
  0x7409,
  0x740a,
  0x7507,
};

const tiny_erd_t appliance_api_zoneline_vertical_v2_heat_pump_erds[] = {
  0x7284,
  0x740b,
  0x740c,
  0x7410,
};

const tiny_erd_t appliance_api_zoneline_vertical_v2_make_up_air_module_erds[] = {
  0x7122,
  0x740d,
  0x740e,
  0x750b,
};

const tiny_erd_t appliance_api_zoneline_vertical_v2_inverter_module_erds[] = {
  0x7108,
  0x710a,
  0x710d,
  0x7116,
  0x7117,
  0x7400,
  0x7511,
  0x7512,
  0x7513,
  0x7514,
  0x7515,
  0x7516,
  0x7517,
  0x7518,
  0x7519,
  0x7600,
  0x7601,
  0x7602,
  0x7610,
  0x7612,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_foundation_erds[] = {
  0x79a4,
  0x79a6,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_indoor_temperature_sensing_erds[] = {
  0x79c2,
  0x7a02,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_basic_temperature_control_erds[] = {
  0x7b06,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_indoor_humidity_sensing_erds[] = {
  0x7b01,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_dip_sw_equipped_erds[] = {
  0x79a5,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_duct_free_split_control_erds[] = {
  0x79a8,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_unitary_heating_erds[] = {
  0x7002,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_unitary_cooling_erds[] = {
  0x7003,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_unitary_auto_mode_erds[] = {
  0x79a9,
  0x79aa,
  0x79b9,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_unitary_air_filtration_erds[] = {
  0x79a0,
  0x79a1,
  0x79a7,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_unitary_fan_control_erds[] = {
  0x79bd,
  0x79be,
  0x79bf,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_unitary_mode_control_erds[] = {
  0x7a01,
  0x7a0f,
  0x7b00,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_local_scheduling_erds[] = {
  0x79a2,
  0x79a3,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_unitary_system_service_config_erds[] = {
  0x79ba,
  0x79bb,
  0x79bc,
  0x79c9,
  0x79ca,
  0x79cb,
  0x79cc,
  0x79cd,
  0x79ce,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_indoor_temperature_service_config_erds[] = {
  0x79c0,
  0x79c1,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_unitary_compressor_service_config_erds[] = {
  0x79ab,
  0x79ac,
  0x79ad,
  0x79ae,
  0x79af,
  0x79b0,
  0x79b3,
  0x79b4,
  0x79b5,
  0x79b6,
  0x79c3,
  0x79c4,
};

const tiny_erd_t appliance_api_central_dfs_controller_v1_unitary_fan_service_config_erds[] = {
  0x79b7,
  0x79b8,
  0x79c5,
  0x79c6,
  0x79c7,
  0x79c8,
};

const tiny_erd_t appliance_api_stand_mixer_v1_foundation_erds[] = {
  0x922f,
  0x9300,
  0x9301,
  0x9302,
  0x9303,
  0x9305,
  0x9309,
};

const tiny_erd_t appliance_api_stand_mixer_v1_remote_cancel_mixing_cycle_erds[] = {
  0x9228,
};

const tiny_erd_t appliance_api_stand_mixer_v1_remote_pause_mixing_cycle_erds[] = {
  0x9304,
};

const tiny_erd_t appliance_api_stand_mixer_v1_mixing_cycle_absolute_time_modifier_erds[] = {
  0x930a,
};

const tiny_erd_t appliance_api_stand_mixer_v1_diagnostics_and_prognostics_erds[] = {
  0x9306,
  0x9307,
  0x9308,
};

const tiny_erd_t appliance_api_stand_mixer_v1_builtin_mixer_scale_erds[] = {
  0x930b,
  0x930c,
  0x930d,
  0x930e,
  0x930f,
  0x9310,
  0x9311,
};

const tiny_erd_t appliance_api_stand_mixer_v1_recipes_erds[] = {
  0x5300,
};

const tiny_erd_t appliance_api_stand_mixer_v1_support_equipment_erds[] = {
  0x9312,
};

const tiny_erd_t appliance_api_stand_mixer_v1_model_configuration_erds[] = {
  0x9313,
};

const tiny_erd_t appliance_api_stand_mixer_v1_display_brightness_erds[] = {
  0x9203,
  0x9204,
};

const tiny_erd_t appliance_api_stand_mixer_v1_sound_settings_erds[] = {
  0x0051,
};

const tiny_erd_t appliance_api_stand_mixer_v2_foundation_erds[] = {
  0x922f,
  0x9300,
  0x9305,
  0x9314,
  0x9315,
  0x9316,
  0x9317,
};

const tiny_erd_t* appliance_api_stand_mixer_v2_precision_speed_erds __attribute__((unused)) = nullptr;

const tiny_erd_t* appliance_api_stand_mixer_v2_active_stir_erds __attribute__((unused)) = nullptr;

const tiny_erd_t* appliance_api_stand_mixer_v2_auto_sense_additional_parameters_erds __attribute__((unused)) = nullptr;

const tiny_erd_t appliance_api_smoker_v1_foundation_erds[] = {
  0x9400,
  0x9401,
  0x9402,
  0x9409,
  0x940c,
};

const tiny_erd_t appliance_api_smoker_v1_smoke_erds[] = {
  0x9403,
  0x9404,
  0x940f,
};

const tiny_erd_t appliance_api_smoker_v1_pause_smoke_erds[] = {
  0x9405,
  0x9406,
};

const tiny_erd_t appliance_api_smoker_v1_cook_time_erds[] = {
  0x5c1b,
  0x9407,
  0x940e,
};

const tiny_erd_t appliance_api_smoker_v1_elapsed_cooking_time_erds[] = {
  0x9408,
};

const tiny_erd_t appliance_api_smoker_v1_probe_erds[] = {
  0x940a,
  0x940b,
  0x940d,
};

const tiny_erd_t appliance_api_smoker_v1_door_alarm_erds[] = {
  0x9205,
  0x9206,
  0x9224,
};

const tiny_erd_t appliance_api_smoker_v1_light_erds[] = {
  0x9201,
  0x9202,
};

const tiny_erd_t appliance_api_smoker_v1_warm_limits_erds[] = {
  0x9427,
  0x9428,
};

const tiny_erd_t appliance_api_smoker_v1_auto_warm_setting_erds[] = {
  0x9421,
  0x9422,
  0x9423,
  0x9424,
  0x9425,
  0x9426,
  0x9429,
  0x942a,
};

const tiny_erd_t appliance_api_smoker_v1_smoker_type_erds[] = {
  0x9436,
};

const tiny_erd_t appliance_api_smoker_v1_ui_brightness_erds[] = {
  0x9203,
  0x9204,
};

const tiny_erd_t appliance_api_smoker_v1_probe_temperature_based_early_completion_notification_erds[] = {
  0x942b,
  0x942c,
  0x942d,
};

const tiny_erd_t appliance_api_smoker_v1_cook_time_based_early_completion_notification_erds[] = {
  0x942e,
  0x942f,
  0x9430,
};

const tiny_erd_t appliance_api_smoker_v1_igniter_temperature_control_erds[] = {
  0x9431,
  0x9432,
  0x9433,
};

const tiny_erd_t appliance_api_smoker_v1_cook_mode_parameter_reset_erds[] = {
  0x9434,
};

const tiny_erd_t appliance_api_smoker_v1_restore_factory_defaults_erds[] = {
  0x9435,
};

const tiny_erd_t appliance_api_smoker_v1_custom_mode_erds[] = {
  0x9411,
  0x9412,
};

const tiny_erd_t appliance_api_smoker_v1_keep_warm_mode_erds[] = {
  0x9413,
  0x9414,
};

const tiny_erd_t appliance_api_smoker_v1_brisket_mode_erds[] = {
  0x9415,
  0x9416,
};

const tiny_erd_t appliance_api_smoker_v1_pork_rib_mode_erds[] = {
  0x9417,
  0x9418,
};

const tiny_erd_t appliance_api_smoker_v1_pork_butt_mode_erds[] = {
  0x9419,
  0x941a,
};

const tiny_erd_t appliance_api_smoker_v1_wings_mode_erds[] = {
  0x941b,
  0x941c,
};

const tiny_erd_t appliance_api_smoker_v1_chicken_mode_erds[] = {
  0x941d,
  0x941e,
};

const tiny_erd_t appliance_api_smoker_v1_salmon_mode_erds[] = {
  0x941f,
  0x9420,
};

const tiny_erd_t appliance_api_smoker_v1_smoker_push_notifications_erds[] = {
  0x9437,
};

const tiny_erd_t appliance_api_smoker_v1_remote_configure_erds[] = {
  0x9410,
};

const tiny_erd_t appliance_api_smoker_v1_sound_settings_erds[] = {
  0x0051,
};

const tiny_erd_t appliance_api_smoker_v1_elapsed_warming_time_erds[] = {
  0x9438,
};

const tiny_erd_t appliance_api_smoker_v1_preheat_progress_erds[] = {
  0x9439,
};

const tiny_erd_t* appliance_api_air_handler_vrf_v1_foundation_erds __attribute__((unused)) = nullptr;

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v1_foundation_erds[] = {
  0x2000,
  0x2001,
  0x2002,
  0x2007,
  0x200a,
  0x2010,
  0x2012,
  0x201c,
  0x201d,
};

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v1_remote_start_and_stop_erds[] = {
  0x2038,
  0x2039,
  0x2040,
};

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v1_remote_cycle_selection_erds[] = {
  0x20a7,
  0x20a8,
};

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v1_downloaded_cycle_erds[] = {
  0x20d1,
  0x20d2,
  0x20d3,
  0x20d7,
};

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v1_cycle_function_erds[] = {
  0x20dc,
  0x20de,
};

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v1_dry_time_option_erds[] = {
  0x20c8,
  0x20ca,
};

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v1_water_tank_notifications_and_warnings_erds[] = {
  0x20db,
};

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v1_item_count_option_erds[] = {
  0x215b,
  0x215c,
};

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v1_cabinet_or_closet_fault_notifications_erds[] = {
  0x222b,
};

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v1_control_lock_option_erds[] = {
  0x2083,
  0x2084,
  0x2085,
};

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v2_foundation_erds[] = {
  0x2000,
  0x2001,
  0x2002,
  0x2007,
  0x200a,
  0x2010,
  0x2012,
};

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v2_cycle_function_erds[] = {
  0x20dc,
};

const tiny_erd_t appliance_api_fabric_care_cabinet_or_closet_v2_push_notifications_erds[] = {
  0x2162,
};

const tiny_erd_t appliance_api_laundry_center_v1_washer_foundation_erds[] = {
  0x2000,
  0x2001,
  0x2007,
  0x200a,
  0x2010,
  0x2012,
  0x2013,
};

const tiny_erd_t appliance_api_laundry_center_v1_dryer_foundation_erds[] = {
  0x201c,
  0x2200,
  0x2201,
  0x2202,
  0x2203,
  0x2204,
  0x2205,
};

const tiny_erd_t appliance_api_laundry_center_v1_washer_remote_start_and_stop_erds[] = {
  0x2038,
  0x2039,
  0x2040,
};

const tiny_erd_t appliance_api_laundry_center_v1_dryer_remote_start_and_stop_erds[] = {
  0x2206,
  0x2207,
  0x2208,
};

const tiny_erd_t appliance_api_laundry_center_v1_washer_remote_cycle_selection_erds[] = {
  0x20a7,
};

const tiny_erd_t appliance_api_laundry_center_v1_dryer_remote_cycle_selection_erds[] = {
  0x2209,
};

const tiny_erd_t appliance_api_laundry_center_v1_laundry_center_push_notifications_erds[] = {
  0x2170,
};

const tiny_erd_t appliance_api_laundry_center_v1_smart_combi_option_erds[] = {
  0x216e,
  0x216f,
};

const tiny_erd_t appliance_api_laundry_center_v1_laundry_center_function_erds[] = {
  0x20dc,
  0x20de,
};

const tiny_erd_t appliance_api_laundry_center_v1_dry_time_option_erds[] = {
  0x20c8,
  0x20ca,
};

const tiny_erd_t appliance_api_laundry_center_v1_dry_level_option_erds[] = {
  0x204b,
  0x204d,
};

const tiny_erd_t appliance_api_laundry_center_v1_washer_eco_option_erds[] = {
  0x2124,
  0x2125,
};

const tiny_erd_t appliance_api_laundry_center_v1_washer_uv_sanitize_option_erds[] = {
  0x207a,
  0x207b,
};

const tiny_erd_t appliance_api_laundry_center_v1_water_temperature_option_erds[] = {
  0x20bb,
  0x20bc,
};

const tiny_erd_t appliance_api_sourdough_starter_v1_foundation_erds[] = {
  0x9500,
};

const tiny_erd_t appliance_api_sourdough_starter_v1_ambient_temperature_supported_erds[] = {
  0x9501,
};

const tiny_erd_t appliance_api_sourdough_starter_v1_ambient_temperature_offset_supported_erds[] = {
  0x9502,
  0x9503,
  0x9504,
};

const tiny_erd_t appliance_api_sourdough_starter_v1_ui_brightness_supported_erds[] = {
  0x9203,
  0x9204,
};

const tiny_erd_t appliance_api_sourdough_starter_v1_sourdough_starter_notifications_supported_erds[] = {
  0x9505,
};

const tiny_erd_t appliance_api_sourdough_starter_v1_time_since_complete_supported_erds[] = {
  0x9506,
};

const tiny_erd_t appliance_api_sourdough_starter_v1_time_to_crock_full_supported_erds[] = {
  0x9507,
};

const tiny_erd_t appliance_api_sourdough_starter_v1_time_until_next_feed_supported_erds[] = {
  0x9508,
};

const tiny_erd_t appliance_api_sourdough_starter_v1_crock_detection_supported_erds[] = {
  0x9509,
};

const tiny_erd_t appliance_api_thermostat_v1_ambient_temperature_supported_erds[] = {
  0x7104,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_foundation_erds[] = {
  0x0007,
  0x0009,
  0x1005,
  0x1009,
  0x100a,
  0x100d,
  0x101d,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_foundation_2_erds[] = {
  0x0007,
  0x0009,
  0x1009,
  0x100a,
  0x100d,
  0x101d,
  0x102f,
  0x1030,
  0x1032,
  0x1033,
  0x1034,
  0x1035,
  0x1036,
  0x1037,
  0x103e,
  0x103f,
  0x1040,
  0x1041,
  0x1045,
  0x1046,
  0x1047,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_food_modes_control_upper_compartment_erds[] = {
  0x1038,
  0x1042,
  0x1050,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_key_status_erds[] = {
  0x1031,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_max_cool_control_erds[] = {
  0x103b,
  0x103c,
  0x103d,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_light_control_erds[] = {
  0x1048,
  0x1049,
  0x104a,
  0x104b,
  0x104c,
  0x104d,
  0x104e,
  0x104f,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_food_modes_control_lower_compartment_erds[] = {
  0x1039,
  0x1043,
  0x1051,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_food_modes_control_middle_compartment_erds[] = {
  0x103a,
  0x1044,
  0x1052,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_turbo_freeze_erds[] = {
  0x100e,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_food_mode_preset_control_erds[] = {
  0x1057,
  0x1058,
  0x1059,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_air_filter_control_erds[] = {
  0x131e,
  0x1320,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_door_information_erds[] = {
  0x1006,
  0x1016,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_light_mode_control_upper_compartment_erds[] = {
  0x1061,
  0x1062,
  0x1063,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_light_mode_control_lower_compartment_erds[] = {
  0x1064,
  0x1065,
  0x1066,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_light_mode_control_middle_compartment_erds[] = {
  0x1067,
  0x1068,
  0x1069,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_eco_mode_control_erds[] = {
  0x1076,
  0x1077,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_v2_refrigeration_notifications_erds[] = {
  0x122e,
};

const tiny_erd_t appliance_api_fpa_refrigerator_v1_ambient_temperature_and_humidity_erds[] = {
  0x1078,
  0x1273,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_foundation_erds[] = {
  0x300c,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_0_erds[] = {
  0x3003,
  0x3007,
  0x3008,
  0x3009,
  0x300e,
  0x300f,
  0x3035,
  0x3037,
  0x303e,
  0x3045,
  0x304d,
  0x3086,
  0xd004,
  0xd005,
  0xd007,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_1_erds[] = {
  0x3203,
  0x3207,
  0x3208,
  0x3209,
  0x320e,
  0x320f,
  0x3222,
  0x3235,
  0x3237,
  0x323e,
  0x3245,
  0x324d,
  0xd204,
  0xd205,
  0xd207,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_0_turbidity_erds[] = {
  0x3036,
  0x3100,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_1_turbidity_erds[] = {
  0x3236,
  0x3300,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_0_end_of_cycle_erds[] = {
  0xd003,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_1_end_of_cycle_erds[] = {
  0xd203,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_0_foundation_erds[] = {
  0x3001,
  0x3003,
  0x300e,
  0x301f,
  0xd003,
  0xd004,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_1_foundation_erds[] = {
  0x3203,
  0x320e,
  0x3401,
  0xd203,
  0xd204,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_0_configuration_erds[] = {
  0x3200,
  0x3210,
  0x3211,
  0x3212,
  0x3213,
  0x3214,
  0x3215,
  0x3216,
  0x3217,
  0x3218,
  0x3219,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_1_configuration_erds[] = {
  0x3600,
  0x3610,
  0x3611,
  0x3612,
  0x3613,
  0x3614,
  0x3615,
  0x3616,
  0x3617,
  0x3618,
  0x3619,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_0_service_erds[] = {
  0x3006,
  0x3009,
  0x301e,
  0x3035,
  0x3037,
  0x303e,
  0x303f,
  0x3045,
  0x3046,
  0x3049,
  0x3083,
  0x3085,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_1_service_erds[] = {
  0x3209,
  0x3235,
  0x3237,
  0x323e,
  0x3245,
  0x3406,
  0x341e,
  0x343f,
  0x3446,
  0x3449,
  0x3483,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_0_cycle_selection_erds[] = {
  0x321b,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_1_cycle_selection_erds[] = {
  0x361b,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_0_wash_temp_selection_erds[] = {
  0x321c,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_1_wash_temp_selection_erds[] = {
  0x361c,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_0_dry_temp_selection_erds[] = {
  0x321d,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_1_dry_temp_selection_erds[] = {
  0x361d,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_0_start_delay_selection_erds[] = {
  0x300d,
  0x321a,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_1_start_delay_selection_erds[] = {
  0x340d,
  0x361a,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_0_remote_cycle_control_erds[] = {
  0x3087,
  0x3204,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_1_remote_cycle_control_erds[] = {
  0x3487,
  0x3604,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_smart_assist_erds[] = {
  0x310a,
  0x310b,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_preference_options_erds[] = {
  0x3201,
  0x3202,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_water_softener_setting_erds[] = {
  0x320a,
  0x320b,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_auto_lid_down_setting_erds[] = {
  0x320c,
  0x320d,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_default_to_eco_cycle_setting_erds[] = {
  0x3231,
  0x3232,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_custom_cycle_erds[] = {
  0x322c,
  0x322d,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_0_cycle_modifiers_erds[] = {
  0x3250,
  0x3251,
};

const tiny_erd_t appliance_api_fpa_dishdrawer_v1_tub_1_cycle_modifiers_erds[] = {
  0x3252,
  0x3253,
};

const tiny_erd_t appliance_api_fpa_range_v1_foundation_erds[] = {
  0x0005,
  0x0006,
  0x0007,
  0x000a,
  0x5007,
  0x5008,
  0x5009,
  0x5100,
  0x5101,
  0x5102,
  0x5103,
  0x5104,
  0x5105,
  0x5106,
  0x5107,
  0x5108,
  0x5109,
  0x510a,
  0x510b,
  0x510d,
  0x5200,
  0x5201,
  0x5202,
  0x5203,
  0x5204,
  0x5205,
  0x5206,
  0x5207,
  0x5208,
  0x5209,
  0x520a,
  0x520b,
  0x520d,
};

const tiny_erd_t appliance_api_fpa_range_v1_upper_automatic_door_open_erds[] = {
  0x510f,
  0x5110,
  0x5118,
};

const tiny_erd_t appliance_api_fpa_range_v1_lower_automatic_door_open_erds[] = {
  0x520f,
  0x5210,
  0x5218,
};

const tiny_erd_t appliance_api_fpa_range_v1_automatic_door_opener_local_public_enable_erds[] = {
  0x502c,
};

const tiny_erd_t appliance_api_fpa_range_v1_upper_extended_available_cook_modes_erds[] = {
  0x5113,
};

const tiny_erd_t appliance_api_fpa_range_v1_lower_extended_available_cook_modes_erds[] = {
  0x5213,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_foundation_erds[] = {
  0x5903,
  0x5904,
  0x598f,
  0x5999,
  0x599a,
  0x599b,
  0x599c,
  0x599d,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x7d_cooktop_erds[] = {
  0x5905,
  0x5906,
  0x5907,
  0x5908,
  0x5909,
  0x590a,
  0x592f,
  0x5930,
  0x5931,
  0x5932,
  0x5933,
  0x5934,
  0x5965,
  0x5966,
  0x5967,
  0x5968,
  0x5969,
  0x596a,
  0x5991,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x7e_cooktop_erds[] = {
  0x590b,
  0x590c,
  0x590d,
  0x590e,
  0x590f,
  0x5910,
  0x5935,
  0x5936,
  0x5937,
  0x5938,
  0x5939,
  0x593a,
  0x596b,
  0x596c,
  0x596d,
  0x596e,
  0x596f,
  0x5970,
  0x5992,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x7f_cooktop_erds[] = {
  0x5911,
  0x5912,
  0x5913,
  0x5914,
  0x5915,
  0x5916,
  0x593b,
  0x593c,
  0x593d,
  0x593e,
  0x593f,
  0x5940,
  0x5970,
  0x5971,
  0x5972,
  0x5973,
  0x5974,
  0x5975,
  0x5976,
  0x5993,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x80_cooktop_erds[] = {
  0x5917,
  0x5918,
  0x5919,
  0x591a,
  0x591b,
  0x591c,
  0x5941,
  0x5942,
  0x5943,
  0x5944,
  0x5945,
  0x5946,
  0x5977,
  0x5978,
  0x5979,
  0x597a,
  0x597b,
  0x597c,
  0x5994,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x81_cooktop_erds[] = {
  0x591d,
  0x591e,
  0x591f,
  0x5920,
  0x5921,
  0x5922,
  0x5947,
  0x5948,
  0x5949,
  0x594a,
  0x594b,
  0x594c,
  0x597d,
  0x597e,
  0x597f,
  0x5980,
  0x5981,
  0x5982,
  0x5995,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x82_cooktop_erds[] = {
  0x5923,
  0x5924,
  0x5925,
  0x5926,
  0x5927,
  0x5928,
  0x594d,
  0x594e,
  0x594f,
  0x5950,
  0x5951,
  0x5952,
  0x5983,
  0x5984,
  0x5985,
  0x5986,
  0x5987,
  0x5988,
  0x5996,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x83_cooktop_erds[] = {
  0x5929,
  0x592a,
  0x592b,
  0x592c,
  0x592d,
  0x592e,
  0x5953,
  0x5954,
  0x5955,
  0x5956,
  0x5957,
  0x5958,
  0x5989,
  0x598a,
  0x598b,
  0x598c,
  0x598d,
  0x598e,
  0x5997,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x7d_downdraft_erds[] = {
  0x5959,
  0x595f,
  0x5991,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x7e_downdraft_erds[] = {
  0x595a,
  0x5960,
  0x5992,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x7f_downdraft_erds[] = {
  0x595b,
  0x5961,
  0x5993,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x81_downdraft_erds[] = {
  0x595c,
  0x5962,
  0x5995,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x82_downdraft_erds[] = {
  0x595d,
  0x5963,
  0x5996,
};

const tiny_erd_t appliance_api_fpa_cooktop_v1_device_position_0x83_downdraft_erds[] = {
  0x595e,
  0x5964,
  0x5997,
};

const tiny_erd_t appliance_api_fpa_hood_v1_foundation_erds[] = {
  0x5b13,
  0x5b14,
  0x5b15,
  0x5b16,
  0x5b17,
  0x5b18,
  0x5b19,
  0x5b1f,
  0x5b20,
};

const tiny_erd_t appliance_api_fpa_hood_v1_hood_notifications_erds[] = {
  0x5b0c,
  0x5b0d,
};

const tiny_erd_t appliance_api_fpa_hood_v1_hood_delays_erds[] = {
  0x5b11,
  0x5b1a,
  0x5b1b,
  0x5b1c,
  0x5b1d,
};

const tiny_erd_t appliance_api_fpa_hood_v1_hood_light_color_erds[] = {
  0x5b21,
  0x5b22,
  0x5b23,
};

const tiny_erd_t appliance_api_fpa_hood_v1_hood_on_off_feature_plus_light_and_fan_presets_erds[] = {
  0x5b12,
};

const tiny_erd_t appliance_api_fpa_hood_v1_ble_hood_to_hob_erds[] = {
  0x5b1e,
  0x5b24,
  0x5b25,
  0x5b26,
};

const tiny_erd_t appliance_api_enhanced_features_v1_block_1_erds[] = {
  0x0600,
  0x0601,
  0x0602,
};

const tiny_erd_t appliance_api_device_gateway_v1_matter_erds[] = {
  0x009c,
  0x0415,
  0x0416,
  0x0417,
  0x0418,
};

const tiny_erd_t appliance_api_device_gateway_v1_ble_advertise_only_sensor_erds[] = {
  0x6110,
  0x6111,
  0x6112,
  0x6113,
  0x6114,
  0x6115,
  0x6116,
  0x6117,
  0x6118,
  0x6119,
  0x6130,
  0x6131,
  0x6132,
  0x6133,
  0x6134,
  0x6135,
  0x6136,
  0x6137,
  0x6138,
  0x6139,
  0x6280,
  0x6281,
  0x6282,
  0x6283,
  0x6284,
  0x628a,
  0x628b,
  0x628c,
  0x628d,
  0x628e,
  0x628f,
};

const tiny_erd_t appliance_api_device_gateway_v1_test_mode_erds[] = {
  0x603c,
};

const tiny_erd_t appliance_api_device_gateway_v1_delayed_appliance_ota_update_erds[] = {
  0x0105,
};

const tiny_erd_t appliance_api_device_gateway_v1_voice_diagnostics_erds[] = {
  0x630b,
  0x630c,
  0x630d,
};

const tiny_erd_t appliance_api_device_gateway_v1_remote_voice_enablement_erds[] = {
  0x630e,
  0x630f,
};

const tiny_erd_t appliance_api_device_gateway_v1_network_state_indication_erds[] = {
  0x6003,
};

const tiny_erd_t appliance_api_hvac_v1_base_functionality_erds[] = {
  0x7700,
  0x7701,
  0x7702,
  0x7703,
  0x7704,
  0x7705,
  0x770c,
};

const tiny_erd_t appliance_api_hvac_v1_heat_mode_supported_erds[] = {
  0x7706,
  0x7707,
  0x7708,
  0x7718,
  0x7719,
  0x771a,
};

const tiny_erd_t appliance_api_hvac_v1_cool_mode_supported_erds[] = {
  0x7709,
  0x770a,
  0x770b,
  0x771e,
  0x771f,
  0x7720,
};

const tiny_erd_t appliance_api_hvac_v1_fan_mode_supported_erds[] = {
  0x771b,
  0x771c,
  0x771d,
};

const tiny_erd_t appliance_api_hvac_v1_heating_setpoint_minimum_limit_configurable_erds[] = {
  0x770d,
  0x770e,
};

const tiny_erd_t appliance_api_hvac_v1_heating_setpoint_maximum_limit_configurable_erds[] = {
  0x770f,
  0x7710,
};

const tiny_erd_t appliance_api_hvac_v1_cooling_setpoint_minimum_limit_configurable_erds[] = {
  0x7711,
  0x7712,
};

const tiny_erd_t appliance_api_hvac_v1_cooling_setpoint_maximum_limit_configurable_erds[] = {
  0x7713,
  0x7714,
};

const tiny_erd_t appliance_api_hvac_v1_air_purifier_erds[] = {
  0x7051,
  0x7053,
};

const tiny_erd_t appliance_api_hvac_v1_energy_conservation_erds[] = {
  0x7052,
  0x7054,
};

const tiny_erd_t appliance_api_hvac_v1_heat_pump_temperature_lockout_erds[] = {
  0x746e,
  0x746f,
};

const tiny_erd_t appliance_api_hvac_v1_input_current_limiting_erds[] = {
  0x7470,
  0x7471,
};

const tiny_erd_t appliance_api_voice_control_v1_alexa_voice_service_erds[] = {
  0x0300,
  0x0301,
};

const tiny_erd_t appliance_api_android_ui_v1_sign_of_life_erds[] = {
  0x0900,
};

const tiny_erd_t appliance_api_android_ui_v1_camera_configuration_personality_erds[] = {
  0x0117,
};

const tiny_erd_t appliance_api_resource_management_v1_electrical_energy_usage_erds[] = {
  0xd007,
  0xd014,
  0xd015,
  0xd02c,
};

const tiny_erd_t appliance_api_resource_management_v1_hot_water_usage_in_liters_erds[] = {
  0xd016,
  0xd017,
  0xd018,
  0xd02d,
};

const tiny_erd_t appliance_api_resource_management_v1_hot_water_usage_in_milliliters_erds[] = {
  0xd017,
  0xd018,
  0xd020,
  0xd02d,
};

const tiny_erd_t appliance_api_resource_management_v1_cold_water_usage_in_liters_erds[] = {
  0xd019,
  0xd01a,
  0xd01b,
  0xd02e,
};

const tiny_erd_t appliance_api_resource_management_v1_cold_water_usage_in_milliliters_erds[] = {
  0xd01a,
  0xd01b,
  0xd021,
  0xd02e,
};

const tiny_erd_t appliance_api_resource_management_v1_gas_usage_erds[] = {
  0xd01c,
  0xd01d,
  0xd01e,
  0xd01f,
  0xd02f,
};

const tiny_erd_t appliance_api_resource_management_v1_electrical_time_of_use_pricing_schedules_season_1_erds[] = {
  0xd022,
  0xd023,
  0xd024,
  0xd025,
};

const tiny_erd_t appliance_api_resource_management_v1_electrical_time_of_use_pricing_schedules_season_2_erds[] = {
  0xd026,
  0xd027,
};

const tiny_erd_t appliance_api_resource_management_v1_electrical_time_of_use_pricing_schedules_season_3_erds[] = {
  0xd028,
  0xd029,
};

const tiny_erd_t appliance_api_resource_management_v1_electrical_time_of_use_pricing_schedules_season_4_erds[] = {
  0xd02a,
  0xd02b,
};

const tiny_erd_t appliance_api_resource_management_v1_cost_comfort_slider_erds[] = {
  0xd00e,
  0xd00f,
};

const tiny_erd_t appliance_api_resource_management_v1_dsm_override_erds[] = {
  0xd001,
  0xd002,
};

const tiny_erd_t appliance_api_resource_management_v1_electrical_energy_usage_v2_erds[] = {
  0xd014,
  0xd015,
  0xd02c,
  0xd030,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_device_air_quality_erds[] = {
  0x5b4b,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_relative_humidity_sensor_erds[] = {
  0x5b27,
  0x5b28,
  0x5b29,
  0x5b2a,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_voc_index_sensor_erds[] = {
  0x5b2b,
  0x5b2c,
  0x5b2d,
  0x5b2e,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_fine_air_particles_sensor_erds[] = {
  0x5b2f,
  0x5b30,
  0x5b31,
  0x5b32,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_nox_index_sensor_erds[] = {
  0x5b33,
  0x5b34,
  0x5b35,
  0x5b36,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_1_um_particle_sensor_erds[] = {
  0x5b37,
  0x5b38,
  0x5b39,
  0x5b3a,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_4_um_particle_sensor_erds[] = {
  0x5b3b,
  0x5b3c,
  0x5b3d,
  0x5b3f,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_10_um_particle_sensor_erds[] = {
  0x5b40,
  0x5b41,
  0x5b42,
  0x5b43,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_temperature_sensor_erds[] = {
  0x5b44,
  0x5b45,
  0x5b46,
  0x5b47,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_displayed_color_scheme_erds[] = {
  0x5b4c,
  0x5b4d,
  0x5b4e,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_fine_air_particles_sensor_index_erds[] = {
  0x5b4f,
  0x5b50,
  0x5b51,
  0x5b52,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_1_um_particle_sensor_index_erds[] = {
  0x5b53,
  0x5b54,
  0x5b55,
  0x5b56,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_4_um_particle_sensor_index_erds[] = {
  0x5b57,
  0x5b58,
  0x5b59,
  0x5b5a,
};

const tiny_erd_t appliance_api_environmental_sensing_v1_10_um_particle_sensor_index_erds[] = {
  0x5b5b,
  0x5b5c,
  0x5b5d,
  0x5b5e,
};

const tiny_erd_t appliance_api_battery_v1_foundation_erds[] = {
  0x0910,
  0x0911,
  0x0912,
  0x0913,
  0x0914,
};

const tiny_erd_t appliance_api_software_diagnostics_v1_reset_information_erds[] = {
  0x0700,
  0x0701,
  0x0702,
  0x0703,
};

const tiny_erd_t appliance_api_software_diagnostics_v1_linux_diagnostics_erds[] = {
  0x0704,
  0x0705,
  0x0706,
  0x0707,
};

const tiny_erd_t appliance_api_software_diagnostics_v1_gea2_gea3_interface_diagnostics_for_interfaces_1_to_3_erds[] = {
  0x0708,
  0x0709,
  0x070a,
};

const tiny_erd_t appliance_api_software_diagnostics_v1_gea2_gea3_interface_diagnostics_for_interfaces_4_to_6_erds[] = {
  0x070b,
  0x070c,
  0x070d,
};

const tiny_erd_t appliance_api_matter_room_ac_v1_foundation_erds[] = {
  0x009c,
  0x0400,
  0x0401,
  0x0402,
  0x0410,
  0x0411,
  0x0412,
  0x0413,
  0x0414,
  0x0415,
  0x7a20,
  0x7a21,
  0x7a22,
  0x7a23,
  0x7a24,
  0x7a25,
  0x7a26,
  0x7a27,
  0x7a28,
  0x7a40,
  0x7a41,
  0x7a42,
  0x7a43,
  0x7a44,
  0x7a45,
};

const appliance_feature_api_descriptor_t appliance_feature_api_descriptors[] = {
  {0x0000, 1, 0x00000001, "Water Heater / Foundation", appliance_api_water_heater_v1_foundation_erds, 23},
  {0x0000, 1, 0x00000002, "Water Heater / Anode Depletion", appliance_api_water_heater_v1_anode_depletion_erds, 3},
  {0x0000, 1, 0x00000004, "Water Heater / Mixing Valve", appliance_api_water_heater_v1_mixing_valve_erds, 12},
  {0x0000, 1, 0x00000008, "Water Heater / Tall Tank", nullptr, 0},
  {0x0000, 1, 0x00000010, "Water Heater / WiFi", nullptr, 0},
  {0x0000, 1, 0x00000020, "Water Heater / Heat Pump", appliance_api_water_heater_v1_heat_pump_erds, 4},
  {0x0000, 1, 0x00000040, "Water Heater / Concurrent loads", nullptr, 0},
  {0x0000, 1, 0x00000080, "Water Heater / Variable speed fan", nullptr, 0},
  {0x0000, 1, 0x00000100, "Water Heater / Variable speed compressor", nullptr, 0},
  {0x0000, 1, 0x00000200, "Water Heater / 120v operation", nullptr, 0},
  {0x0000, 1, 0x00000400, "Water Heater / Estimation", appliance_api_water_heater_v1_estimation_erds, 4},
  {0x0000, 1, 0x00000800, "Water Heater / Cost of power", appliance_api_water_heater_v1_cost_of_power_erds, 1},
  {0x0000, 1, 0x00001000, "Water Heater / Gas", nullptr, 0},
  {0x0000, 1, 0x00002000, "Water Heater / Blower", nullptr, 0},
  {0x0000, 1, 0x00004000, "Water Heater / Dad Mode", appliance_api_water_heater_v1_dad_mode_erds, 1},
  {0x0000, 1, 0x00008000, "Water Heater / Service request", appliance_api_water_heater_v1_service_request_erds, 1},
  {0x0000, 1, 0x00010000, "Water Heater / Water Heater Boost Mode", appliance_api_water_heater_v1_water_heater_boost_mode_erds, 2},
  {0x0000, 1, 0x00020000, "Water Heater / Water Heater Active", appliance_api_water_heater_v1_water_heater_active_erds, 2},
  {0x0000, 1, 0x00040000, "Water Heater / Canadian model", nullptr, 0},
  {0x0000, 1, 0x00080000, "Water Heater / Anode Percentage remaining", appliance_api_water_heater_v1_anode_percentage_remaining_erds, 1},
  {0x0000, 1, 0x00100000, "Water Heater / Shutoff valve accessory", appliance_api_water_heater_v1_shutoff_valve_accessory_erds, 4},
  {0x0000, 1, 0x00200000, "Water Heater / Heating Diagnostics", appliance_api_water_heater_v1_heating_diagnostics_erds, 6},
  {0x0001, 1, 0x00000001, "Clothes Dryer / Foundation", appliance_api_clothes_dryer_v1_foundation_erds, 11},
  {0x0001, 1, 0x00000002, "Clothes Dryer / Remote Start and Stop", appliance_api_clothes_dryer_v1_remote_start_and_stop_erds, 4},
  {0x0001, 1, 0x00000004, "Clothes Dryer / Dryer Sheet Inventory", appliance_api_clothes_dryer_v1_dryer_sheet_inventory_erds, 2},
  {0x0001, 1, 0x00000008, "Clothes Dryer / Extended Tumble", appliance_api_clothes_dryer_v1_extended_tumble_erds, 3},
  {0x0001, 1, 0x00000010, "Clothes Dryer / Reduce Static Option", appliance_api_clothes_dryer_v1_reduce_static_option_erds, 3},
  {0x0001, 1, 0x00000020, "Clothes Dryer / Washer Link", appliance_api_clothes_dryer_v1_washer_link_erds, 6},
  {0x0001, 1, 0x00000040, "Clothes Dryer / Blocked Vent", appliance_api_clothes_dryer_v1_blocked_vent_erds, 1},
  {0x0001, 1, 0x00000080, "Clothes Dryer / EcoDry Option", appliance_api_clothes_dryer_v1_ecodry_option_erds, 3},
  {0x0001, 1, 0x00000100, "Clothes Dryer / Damp Alert Option", appliance_api_clothes_dryer_v1_damp_alert_option_erds, 4},
  {0x0001, 1, 0x00000200, "Clothes Dryer / Dryness Option", appliance_api_clothes_dryer_v1_dryness_option_erds, 3},
  {0x0001, 1, 0x00000400, "Clothes Dryer / Temperature Option", appliance_api_clothes_dryer_v1_temperature_option_erds, 3},
  {0x0001, 1, 0x00000800, "Clothes Dryer / Small Load Option", appliance_api_clothes_dryer_v1_small_load_option_erds, 3},
  {0x0001, 1, 0x00001000, "Clothes Dryer / Detangle Option", appliance_api_clothes_dryer_v1_detangle_option_erds, 3},
  {0x0001, 1, 0x00002000, "Clothes Dryer / Time Level Option", appliance_api_clothes_dryer_v1_time_level_option_erds, 3},
  {0x0001, 1, 0x00004000, "Clothes Dryer / Sanitize Option", appliance_api_clothes_dryer_v1_sanitize_option_erds, 3},
  {0x0001, 1, 0x00008000, "Clothes Dryer / Key Volume Option", appliance_api_clothes_dryer_v1_key_volume_option_erds, 3},
  {0x0001, 1, 0x00010000, "Clothes Dryer / Alert Volume Option", appliance_api_clothes_dryer_v1_alert_volume_option_erds, 3},
  {0x0001, 1, 0x00020000, "Clothes Dryer / Control Lock Option", appliance_api_clothes_dryer_v1_control_lock_option_erds, 3},
  {0x0001, 1, 0x00040000, "Clothes Dryer / Steam Option", appliance_api_clothes_dryer_v1_steam_option_erds, 3},
  {0x0001, 1, 0x00080000, "Clothes Dryer / Air Fluff Cycle Option", appliance_api_clothes_dryer_v1_air_fluff_cycle_option_erds, 3},
  {0x0001, 1, 0x00100000, "Clothes Dryer / Steam Cycle Option", appliance_api_clothes_dryer_v1_steam_cycle_option_erds, 3},
  {0x0001, 1, 0x00200000, "Clothes Dryer / Warm Up Cycle Option", appliance_api_clothes_dryer_v1_warm_up_cycle_option_erds, 3},
  {0x0001, 1, 0x00400000, "Clothes Dryer / Smart Vent Cycle Option", appliance_api_clothes_dryer_v1_smart_vent_cycle_option_erds, 4},
  {0x0001, 1, 0x00800000, "Clothes Dryer / Washer Link 2", appliance_api_clothes_dryer_v1_washer_link_2_erds, 6},
  {0x0001, 1, 0x01000000, "Clothes Dryer / Connect Cycle", appliance_api_clothes_dryer_v1_connect_cycle_erds, 2},
  {0x0001, 1, 0x02000000, "Clothes Dryer / Scent", appliance_api_clothes_dryer_v1_scent_erds, 6},
  {0x0001, 1, 0x04000000, "Clothes Dryer / Commercial Laundry Status", appliance_api_clothes_dryer_v1_commercial_laundry_status_erds, 2},
  {0x0001, 1, 0x08000000, "Clothes Dryer / Remote Cycle Selection", appliance_api_clothes_dryer_v1_remote_cycle_selection_erds, 2},
  {0x0001, 1, 0x10000000, "Clothes Dryer / RJ45 Write Access", appliance_api_clothes_dryer_v1_rj45_write_access_erds, 2},
  {0x0001, 1, 0x20000000, "Clothes Dryer / Downloaded Cycle", appliance_api_clothes_dryer_v1_downloaded_cycle_erds, 4},
  {0x0001, 1, 0x40000000, "Clothes Dryer / Remote My Cycle", appliance_api_clothes_dryer_v1_remote_my_cycle_erds, 16},
  {0x0001, 1, 0x80000000, "Clothes Dryer / Adaptive My Cycle", appliance_api_clothes_dryer_v1_adaptive_my_cycle_erds, 3},
  {0x0001, 2, 0x00000001, "Clothes Dryer / Notifications and Warnings", appliance_api_clothes_dryer_v2_notifications_and_warnings_erds, 2},
  {0x0001, 2, 0x00000002, "Clothes Dryer / Remote Start and Stop v2", appliance_api_clothes_dryer_v2_remote_start_and_stop_v2_erds, 3},
  {0x0001, 2, 0x00000004, "Clothes Dryer / Smart Vent Cycle Option v2", appliance_api_clothes_dryer_v2_smart_vent_cycle_option_v2_erds, 5},
  {0x0001, 2, 0x00000008, "Clothes Dryer / Laundry Operating Mode", appliance_api_clothes_dryer_v2_laundry_operating_mode_erds, 1},
  {0x0001, 2, 0x00000010, "Clothes Dryer / Legacy Extended Tumble", appliance_api_clothes_dryer_v2_legacy_extended_tumble_erds, 1},
  {0x0001, 2, 0x00000020, "Clothes Dryer / Optimise Cycle Option", appliance_api_clothes_dryer_v2_optimise_cycle_option_erds, 3},
  {0x0001, 2, 0x00000040, "Clothes Dryer / Cycle Function", appliance_api_clothes_dryer_v2_cycle_function_erds, 3},
  {0x0001, 2, 0x00000080, "Clothes Dryer / Dry Time Option", appliance_api_clothes_dryer_v2_dry_time_option_erds, 3},
  {0x0001, 2, 0x00000100, "Clothes Dryer / Signal Option", appliance_api_clothes_dryer_v2_signal_option_erds, 3},
  {0x0001, 2, 0x00000200, "Clothes Dryer / Wrinkle Free Option", appliance_api_clothes_dryer_v2_wrinkle_free_option_erds, 3},
  {0x0001, 2, 0x00000400, "Clothes Dryer / Commercial Laundry Payment V1 Attributes", appliance_api_clothes_dryer_v2_commercial_laundry_payment_v1_attributes_erds, 1},
  {0x0001, 2, 0x00000800, "Clothes Dryer / Timed Cycle Total Drying Duration Option", appliance_api_clothes_dryer_v2_timed_cycle_total_drying_duration_option_erds, 3},
  {0x0001, 2, 0x00001000, "Clothes Dryer / Pre Cycle Time Increment", appliance_api_clothes_dryer_v2_pre_cycle_time_increment_erds, 3},
  {0x0001, 2, 0x00002000, "Clothes Dryer / Mid Cycle Time Increment", appliance_api_clothes_dryer_v2_mid_cycle_time_increment_erds, 2},
  {0x0001, 2, 0x00004000, "Clothes Dryer / Dryer BLE Control", appliance_api_clothes_dryer_v2_dryer_ble_control_erds, 10},
  {0x0001, 2, 0x00008000, "Clothes Dryer / Dryer Heater Type", appliance_api_clothes_dryer_v2_dryer_heater_type_erds, 1},
  {0x0001, 2, 0x00010000, "Clothes Dryer / Commercial Laundry Coin Integration", appliance_api_clothes_dryer_v2_commercial_laundry_coin_integration_erds, 11},
  {0x0001, 2, 0x00020000, "Clothes Dryer / Item Count Option", appliance_api_clothes_dryer_v2_item_count_option_erds, 2},
  {0x0001, 2, 0x00040000, "Clothes Dryer / Dryer Notifications", appliance_api_clothes_dryer_v2_dryer_notifications_erds, 2},
  {0x0001, 2, 0x00080000, "Clothes Dryer / Drum Light Option", appliance_api_clothes_dryer_v2_drum_light_option_erds, 3},
  {0x0001, 2, 0x00100000, "Clothes Dryer / Delay Start Duration Option", appliance_api_clothes_dryer_v2_delay_start_duration_option_erds, 3},
  {0x0001, 2, 0x00400000, "Clothes Dryer / Dryer Faults", appliance_api_clothes_dryer_v2_dryer_faults_erds, 1},
  {0x0001, 2, 0x00800000, "Clothes Dryer / Commercial Laundry Common Remote Parameters", appliance_api_clothes_dryer_v2_commercial_laundry_common_remote_parameters_erds, 8},
  {0x0001, 2, 0x01000000, "Clothes Dryer / Commercial Laundry Front Load Only Remote Parameters", appliance_api_clothes_dryer_v2_commercial_laundry_front_load_only_remote_parameters_erds, 16},
  {0x0001, 2, 0x02000000, "Clothes Dryer / Commercial Free Mode", appliance_api_clothes_dryer_v2_commercial_free_mode_erds, 2},
  {0x0002, 1, 0x00000001, "Clothes Washer / Foundation", appliance_api_clothes_washer_v1_foundation_erds, 13},
  {0x0002, 1, 0x00000002, "Clothes Washer / Add Garment Indicator", appliance_api_clothes_washer_v1_add_garment_indicator_erds, 1},
  {0x0002, 1, 0x00000004, "Clothes Washer / Connect Cycle", appliance_api_clothes_washer_v1_connect_cycle_erds, 2},
  {0x0002, 1, 0x00000008, "Clothes Washer / Remote Start and Stop", appliance_api_clothes_washer_v1_remote_start_and_stop_erds, 3},
  {0x0002, 1, 0x00000010, "Clothes Washer / Bulk Dispense and Tank Status", appliance_api_clothes_washer_v1_bulk_dispense_and_tank_status_erds, 2},
  {0x0002, 1, 0x00000020, "Clothes Washer / Washer Link", appliance_api_clothes_washer_v1_washer_link_erds, 1},
  {0x0002, 1, 0x00000040, "Clothes Washer / Detergent Dispense Flow Rate", appliance_api_clothes_washer_v1_detergent_dispense_flow_rate_erds, 1},
  {0x0002, 1, 0x00000080, "Clothes Washer / Deprecated", appliance_api_clothes_washer_v1_deprecated_m00000080_erds, 2},
  {0x0002, 1, 0x00000100, "Clothes Washer / One Step Wash and Dry", appliance_api_clothes_washer_v1_one_step_wash_and_dry_erds, 2},
  {0x0002, 1, 0x00000200, "Clothes Washer / Smart Dispense", appliance_api_clothes_washer_v1_smart_dispense_erds, 12},
  {0x0002, 1, 0x00000400, "Clothes Washer / Time Saver", appliance_api_clothes_washer_v1_time_saver_erds, 2},
  {0x0002, 1, 0x00000800, "Clothes Washer / Deprecated", nullptr, 0},
  {0x0002, 1, 0x00001000, "Clothes Washer / Prewash", appliance_api_clothes_washer_v1_prewash_erds, 2},
  {0x0002, 1, 0x00002000, "Clothes Washer / Power Care", appliance_api_clothes_washer_v1_power_care_erds, 2},
  {0x0002, 1, 0x00004000, "Clothes Washer / Washer Link v2", appliance_api_clothes_washer_v1_washer_link_v2_erds, 1},
  {0x0002, 1, 0x00008000, "Clothes Washer / Color Keeper", appliance_api_clothes_washer_v1_color_keeper_erds, 2},
  {0x0002, 1, 0x00010000, "Clothes Washer / No Tangle Wash", appliance_api_clothes_washer_v1_no_tangle_wash_erds, 2},
  {0x0002, 1, 0x00020000, "Clothes Washer / Out of Balance Notification", appliance_api_clothes_washer_v1_out_of_balance_notification_erds, 1},
  {0x0002, 1, 0x00040000, "Clothes Washer / Commercial Laundry Status", appliance_api_clothes_washer_v1_commercial_laundry_status_erds, 2},
  {0x0002, 1, 0x00080000, "Clothes Washer / Remote Cycle Selection", appliance_api_clothes_washer_v1_remote_cycle_selection_erds, 2},
  {0x0002, 1, 0x00100000, "Clothes Washer / RJ45 Write Access", appliance_api_clothes_washer_v1_rj45_write_access_erds, 2},
  {0x0002, 1, 0x00200000, "Clothes Washer / True Rinse", appliance_api_clothes_washer_v1_true_rinse_erds, 2},
  {0x0002, 1, 0x00400000, "Clothes Washer / Stain Removal Guide", appliance_api_clothes_washer_v1_stain_removal_guide_erds, 4},
  {0x0002, 1, 0x00800000, "Clothes Washer / Flex Dispense", appliance_api_clothes_washer_v1_flex_dispense_erds, 3},
  {0x0002, 1, 0x01000000, "Clothes Washer / Downloadable Cycle", appliance_api_clothes_washer_v1_downloadable_cycle_erds, 4},
  {0x0002, 1, 0x02000000, "Clothes Washer / Smart Dispense Adjustability", appliance_api_clothes_washer_v1_smart_dispense_adjustability_erds, 3},
  {0x0002, 1, 0x04000000, "Clothes Washer / Deep Fill Incremental Option", appliance_api_clothes_washer_v1_deep_fill_incremental_option_erds, 2},
  {0x0002, 1, 0x08000000, "Clothes Washer / Deep Fill Fixed Level Option", appliance_api_clothes_washer_v1_deep_fill_fixed_level_option_erds, 2},
  {0x0002, 1, 0x10000000, "Clothes Washer / Delay Wash Option", appliance_api_clothes_washer_v1_delay_wash_option_erds, 2},
  {0x0002, 1, 0x20000000, "Clothes Washer / Extra Rinse Option", appliance_api_clothes_washer_v1_extra_rinse_option_erds, 2},
  {0x0002, 1, 0x40000000, "Clothes Washer / Fabric Softener Option", appliance_api_clothes_washer_v1_fabric_softener_option_erds, 2},
  {0x0002, 1, 0x80000000, "Clothes Washer / Spin Level Option", appliance_api_clothes_washer_v1_spin_level_option_erds, 2},
  {0x0002, 2, 0x00000001, "Clothes Washer / Water On Demand Soap Dispense Option", appliance_api_clothes_washer_v2_water_on_demand_soap_dispense_option_erds, 2},
  {0x0002, 2, 0x00000002, "Clothes Washer / Water Temperature Option", appliance_api_clothes_washer_v2_water_temperature_option_erds, 2},
  {0x0002, 2, 0x00000004, "Clothes Washer / Warm Rinse Option", appliance_api_clothes_washer_v2_warm_rinse_option_erds, 2},
  {0x0002, 2, 0x00000008, "Clothes Washer / Soil Level Option", appliance_api_clothes_washer_v2_soil_level_option_erds, 2},
  {0x0002, 2, 0x00000010, "Clothes Washer / Auto Soak Option", appliance_api_clothes_washer_v2_auto_soak_option_erds, 2},
  {0x0002, 2, 0x00000020, "Clothes Washer / Control Lock Option", appliance_api_clothes_washer_v2_control_lock_option_erds, 2},
  {0x0002, 2, 0x00000040, "Clothes Washer / Adaptive My Cycle", appliance_api_clothes_washer_v2_adaptive_my_cycle_erds, 3},
  {0x0002, 2, 0x00000080, "Clothes Washer / Self Clean Reminder", appliance_api_clothes_washer_v2_self_clean_reminder_erds, 1},
  {0x0002, 2, 0x00000100, "Clothes Washer / Remote My Cycle", appliance_api_clothes_washer_v2_remote_my_cycle_erds, 16},
  {0x0002, 2, 0x00000200, "Clothes Washer / Auto Dose Option", appliance_api_clothes_washer_v2_auto_dose_option_erds, 3},
  {0x0002, 2, 0x00000400, "Clothes Washer / AI Option", appliance_api_clothes_washer_v2_ai_option_erds, 3},
  {0x0002, 2, 0x00000800, "Clothes Washer / Wrinkle Free Option", appliance_api_clothes_washer_v2_wrinkle_free_option_erds, 3},
  {0x0002, 2, 0x00001000, "Clothes Washer / Water Level Option", appliance_api_clothes_washer_v2_water_level_option_erds, 3},
  {0x0002, 2, 0x00002000, "Clothes Washer / Wash Progress Option", appliance_api_clothes_washer_v2_wash_progress_option_erds, 3},
  {0x0002, 2, 0x00004000, "Clothes Washer / Optimise Cycle Option", appliance_api_clothes_washer_v2_optimise_cycle_option_erds, 3},
  {0x0002, 2, 0x00008000, "Clothes Washer / Time Saver Allowable", appliance_api_clothes_washer_v2_time_saver_allowable_erds, 1},
  {0x0002, 2, 0x00010000, "Clothes Washer / Prewash Allowable", appliance_api_clothes_washer_v2_prewash_allowable_erds, 1},
  {0x0002, 2, 0x00020000, "Clothes Washer / Extra Rinse Option Allowable", appliance_api_clothes_washer_v2_extra_rinse_option_allowable_erds, 1},
  {0x0002, 2, 0x00040000, "Clothes Washer / Spin Level Option Allowable", appliance_api_clothes_washer_v2_spin_level_option_allowable_erds, 1},
  {0x0002, 2, 0x00080000, "Clothes Washer / Water Temperature Option Allowable", appliance_api_clothes_washer_v2_water_temperature_option_allowable_erds, 1},
  {0x0002, 2, 0x00100000, "Clothes Washer / Soil Level Option Allowable", appliance_api_clothes_washer_v2_soil_level_option_allowable_erds, 1},
  {0x0002, 2, 0x00200000, "Clothes Washer / Auto Soak Option Allowable", appliance_api_clothes_washer_v2_auto_soak_option_allowable_erds, 1},
  {0x0002, 2, 0x00400000, "Clothes Washer / Smart Dispense Adjustability v2", appliance_api_clothes_washer_v2_smart_dispense_adjustability_v2_erds, 3},
  {0x0002, 2, 0x00800000, "Clothes Washer / Commercial Laundry Payment V1 Attributes", appliance_api_clothes_washer_v2_commercial_laundry_payment_v1_attributes_erds, 1},
  {0x0002, 2, 0x01000000, "Clothes Washer / Eco Option", appliance_api_clothes_washer_v2_eco_option_erds, 3},
  {0x0002, 2, 0x02000000, "Clothes Washer / Water Level Option GEA", appliance_api_clothes_washer_v2_water_level_option_gea_erds, 3},
  {0x0002, 2, 0x04000000, "Clothes Washer / Soaking Rinse Option", appliance_api_clothes_washer_v2_soaking_rinse_option_erds, 3},
  {0x0002, 2, 0x08000000, "Clothes Washer / Bluetooth Dryer Control", appliance_api_clothes_washer_v2_bluetooth_dryer_control_erds, 9},
  {0x0002, 2, 0x10000000, "Clothes Washer / Steam", appliance_api_clothes_washer_v2_steam_erds, 3},
  {0x0002, 2, 0x20000000, "Clothes Washer / Tumble Care", appliance_api_clothes_washer_v2_tumble_care_erds, 3},
  {0x0002, 2, 0x40000000, "Clothes Washer / Commercial Laundry Coin Integration", appliance_api_clothes_washer_v2_commercial_laundry_coin_integration_erds, 11},
  {0x0002, 2, 0x80000000, "Clothes Washer / Smart Wash And Rinse", appliance_api_clothes_washer_v2_smart_wash_and_rinse_erds, 2},
  {0x0002, 3, 0x00000001, "Clothes Washer / Ultra Fresh Air Option", appliance_api_clothes_washer_v3_ultra_fresh_air_option_erds, 2},
  {0x0002, 3, 0x00000002, "Clothes Washer / Sanitize Option", appliance_api_clothes_washer_v3_sanitize_option_erds, 2},
  {0x0002, 3, 0x00000008, "Clothes Washer / Washer Push Notifications and Faults", appliance_api_clothes_washer_v3_washer_push_notifications_and_faults_erds, 2},
  {0x0002, 3, 0x00000010, "Clothes Washer / Flexible Dispensing", appliance_api_clothes_washer_v3_flexible_dispensing_erds, 6},
  {0x0002, 3, 0x00000020, "Clothes Washer / Commercial Laundry Top Load Remote Parameters", appliance_api_clothes_washer_v3_commercial_laundry_top_load_remote_parameters_erds, 6},
  {0x0002, 3, 0x00000040, "Clothes Washer / Commercial Laundry Front Load Remote Parameters", appliance_api_clothes_washer_v3_commercial_laundry_front_load_remote_parameters_erds, 21},
  {0x0002, 4, 0x00000001, "Clothes Washer / Mabe Washer Foundation", appliance_api_clothes_washer_v4_mabe_washer_foundation_erds, 31},
  {0x0002, 4, 0x00000010, "Clothes Washer / Mabe Washer Diagnostics", appliance_api_clothes_washer_v4_mabe_washer_diagnostics_erds, 6},
  {0x0002, 4, 0x00000020, "Clothes Washer / Commercial Free Mode", appliance_api_clothes_washer_v4_commercial_free_mode_erds, 2},
  {0x0002, 4, 0x00000200, "Clothes Washer / Remote Pet Hair Removal Option", appliance_api_clothes_washer_v4_remote_pet_hair_removal_option_erds, 3},
  {0x0002, 4, 0x00000800, "Clothes Washer / Remote Wash2x Option", appliance_api_clothes_washer_v4_remote_wash2x_option_erds, 2},
  {0x0003, 1, 0x00000001, "Refrigeration / Foundation", appliance_api_refrigeration_v1_foundation_erds, 31},
  {0x0003, 1, 0x00000002, "Refrigeration / Hot Water", appliance_api_refrigeration_v1_hot_water_erds, 4},
  {0x0003, 1, 0x00000004, "Refrigeration / Single Serve", appliance_api_refrigeration_v1_single_serve_erds, 1},
  {0x0003, 1, 0x00000008, "Refrigeration / Deli Pan", appliance_api_refrigeration_v1_deli_pan_erds, 3},
  {0x0003, 1, 0x00000010, "Refrigeration / Convertible Drawer", appliance_api_refrigeration_v1_convertible_drawer_erds, 7},
  {0x0003, 1, 0x00000020, "Refrigeration / Odor Filter", appliance_api_refrigeration_v1_odor_filter_erds, 1},
  {0x0003, 1, 0x00000040, "Refrigeration / Humidity Control", appliance_api_refrigeration_v1_humidity_control_erds, 1},
  {0x0003, 1, 0x00000080, "Refrigeration / Feature Pan", appliance_api_refrigeration_v1_feature_pan_erds, 2},
  {0x0003, 1, 0x00000100, "Refrigeration / Door in Door as Independent Input", appliance_api_refrigeration_v1_door_in_door_as_independent_input_erds, 1},
  {0x0003, 1, 0x00000200, "Refrigeration / FF Light Control", appliance_api_refrigeration_v1_ff_light_control_erds, 1},
  {0x0003, 1, 0x00000400, "Refrigeration / Presence Sensing", appliance_api_refrigeration_v1_presence_sensing_erds, 2},
  {0x0003, 1, 0x00000800, "Refrigeration / Nighttime Snack Mode", appliance_api_refrigeration_v1_nighttime_snack_mode_erds, 2},
  {0x0003, 1, 0x00100000, "Refrigeration / Notifications", appliance_api_refrigeration_v1_notifications_erds, 2},
  {0x0003, 1, 0x00200000, "Refrigeration / Autofill pitcher", appliance_api_refrigeration_v1_autofill_pitcher_erds, 5},
  {0x0003, 2, 0x00000001, "Refrigeration / Foundation", appliance_api_refrigeration_v2_foundation_erds, 1},
  {0x0003, 2, 0x00000002, "Refrigeration / Fresh Food Actual Temperature", appliance_api_refrigeration_v2_fresh_food_actual_temperature_erds, 1},
  {0x0003, 2, 0x00000004, "Refrigeration / Freezer Actual Temperature", appliance_api_refrigeration_v2_freezer_actual_temperature_erds, 1},
  {0x0003, 2, 0x00000010, "Refrigeration / Hydration Station", appliance_api_refrigeration_v2_hydration_station_erds, 1},
  {0x0003, 2, 0x00008000, "Refrigeration / Left Side Freezer Door Status", appliance_api_refrigeration_v2_left_side_freezer_door_status_erds, 3},
  {0x0003, 2, 0x00010000, "Refrigeration / Right Side Fresh Food Door Status", appliance_api_refrigeration_v2_right_side_fresh_food_door_status_erds, 4},
  {0x0003, 2, 0x00040000, "Refrigeration / Barcode Scanner Feature Power", appliance_api_refrigeration_v2_barcode_scanner_feature_power_erds, 2},
  {0x0003, 2, 0x00200000, "Refrigeration / Turbo Cool", appliance_api_refrigeration_v2_turbo_cool_erds, 3},
  {0x0003, 2, 0x00400000, "Refrigeration / Turbo Freeze", appliance_api_refrigeration_v2_turbo_freeze_erds, 3},
  {0x0003, 2, 0x00800000, "Refrigeration / Flowmeter Ounces Dispensed", appliance_api_refrigeration_v2_flowmeter_ounces_dispensed_erds, 2},
  {0x0003, 2, 0x01000000, "Refrigeration / Presence Sensing Enabled", appliance_api_refrigeration_v2_presence_sensing_enabled_erds, 3},
  {0x0003, 2, 0x02000000, "Refrigeration / Presence Sensed Activates Recess Light", appliance_api_refrigeration_v2_presence_sensed_activates_recess_light_erds, 2},
  {0x0003, 2, 0x04000000, "Refrigeration / Dimmable Light 0 Percent Level", appliance_api_refrigeration_v2_dimmable_light_0_percent_level_erds, 3},
  {0x0003, 2, 0x08000000, "Refrigeration / Door Alarm Enable", appliance_api_refrigeration_v2_door_alarm_enable_erds, 2},
  {0x0003, 2, 0x10000000, "Refrigeration / Night Snack Mode Lighting", appliance_api_refrigeration_v2_night_snack_mode_lighting_erds, 2},
  {0x0003, 2, 0x80000000, "Refrigeration / V2 Refrigeration Notifications", appliance_api_refrigeration_v2_v2_refrigeration_notifications_erds, 1},
  {0x0003, 3, 0x00002000, "Refrigeration / Autofill Pitcher Enable", appliance_api_refrigeration_v3_autofill_pitcher_enable_erds, 4},
  {0x0003, 3, 0x00008000, "Refrigeration / Kitchen Illumination Feature Enable", appliance_api_refrigeration_v3_kitchen_illumination_feature_enable_erds, 4},
  {0x0003, 3, 0x00020000, "Refrigeration / Door Alarm Timer Timeout", appliance_api_refrigeration_v3_door_alarm_timer_timeout_erds, 3},
  {0x0003, 3, 0x00040000, "Refrigeration / Top Right Side Fresh Food Door In Door Status", appliance_api_refrigeration_v3_top_right_side_fresh_food_door_in_door_status_erds, 4},
  {0x0003, 3, 0x00080000, "Refrigeration / Left Side Fresh Food Door Status", appliance_api_refrigeration_v3_left_side_fresh_food_door_status_erds, 4},
  {0x0003, 3, 0x00100000, "Refrigeration / Convertible Compartment Door Status", appliance_api_refrigeration_v3_convertible_compartment_door_status_erds, 3},
  {0x0003, 3, 0x00200000, "Refrigeration / Bottom Freezer Single Door Status", appliance_api_refrigeration_v3_bottom_freezer_single_door_status_erds, 3},
  {0x0003, 3, 0x01000000, "Refrigeration / Barcode Scanner Device Power", appliance_api_refrigeration_v3_barcode_scanner_device_power_erds, 3},
  {0x0003, 3, 0x04000000, "Refrigeration / Single Door Status", appliance_api_refrigeration_v3_single_door_status_erds, 3},
  {0x0003, 3, 0x08000000, "Refrigeration / Top Right Side Fresh Food Door Status", appliance_api_refrigeration_v3_top_right_side_fresh_food_door_status_erds, 4},
  {0x0003, 3, 0x10000000, "Refrigeration / Top Left Side Fresh Food Door Status", appliance_api_refrigeration_v3_top_left_side_fresh_food_door_status_erds, 4},
  {0x0003, 3, 0x20000000, "Refrigeration / Middle Convertible Compartment Single Door Status", appliance_api_refrigeration_v3_middle_convertible_compartment_single_door_status_erds, 3},
  {0x0003, 3, 0x40000000, "Refrigeration / Bottom Left Side Freezer Door Status", appliance_api_refrigeration_v3_bottom_left_side_freezer_door_status_erds, 3},
  {0x0003, 3, 0x80000000, "Refrigeration / Bottom Right Convertible Compartment Door Status", appliance_api_refrigeration_v3_bottom_right_convertible_compartment_door_status_erds, 3},
  {0x0003, 4, 0x00000001, "Refrigeration / Top Freezer Single Door Status", appliance_api_refrigeration_v4_top_freezer_single_door_status_erds, 3},
  {0x0003, 4, 0x00000002, "Refrigeration / Bottom Fresh Food Single Door Status", appliance_api_refrigeration_v4_bottom_fresh_food_single_door_status_erds, 4},
  {0x0003, 4, 0x00000200, "Refrigeration / Freezer Ice Rate Is Active", appliance_api_refrigeration_v4_freezer_ice_rate_is_active_erds, 1},
  {0x0003, 4, 0x00020000, "Refrigeration / Variable Cube Size Data", appliance_api_refrigeration_v4_variable_cube_size_data_erds, 3},
  {0x0003, 4, 0x00040000, "Refrigeration / Fridge Focus Camera Enable", appliance_api_refrigeration_v4_fridge_focus_camera_enable_erds, 2},
  {0x0003, 4, 0x00100000, "Refrigeration / Recess Light Brightness Control", appliance_api_refrigeration_v4_recess_light_brightness_control_erds, 3},
  {0x0003, 4, 0x08000000, "Refrigeration / Power Outage data", appliance_api_refrigeration_v4_power_outage_data_erds, 2},
  {0x0003, 5, 0x00000400, "Refrigeration / Variable Cube Size Data 0", appliance_api_refrigeration_v5_variable_cube_size_data_0_erds, 3},
  {0x0003, 5, 0x00000800, "Refrigeration / Variable Cube Size Data 2", appliance_api_refrigeration_v5_variable_cube_size_data_2_erds, 3},
  {0x0003, 5, 0x00001000, "Refrigeration / Scanned Barcode Result", appliance_api_refrigeration_v5_scanned_barcode_result_erds, 2},
  {0x0003, 5, 0x00002000, "Refrigeration / Dimmable Light 0 Warmth Percentage", appliance_api_refrigeration_v5_dimmable_light_0_warmth_percentage_erds, 2},
  {0x0003, 5, 0x00004000, "Refrigeration / Secondary Feature Pan Filtered Temperature", appliance_api_refrigeration_v5_secondary_feature_pan_filtered_temperature_erds, 1},
  {0x0003, 5, 0x00008000, "Refrigeration / System Power Setting", appliance_api_refrigeration_v5_system_power_setting_erds, 2},
  {0x0004, 1, 0x00000001, "Microwave / Foundation", appliance_api_microwave_v1_foundation_erds, 45},
  {0x0004, 1, 0x00000002, "Microwave / Vent and Light", appliance_api_microwave_v1_vent_and_light_erds, 4},
  {0x0004, 1, 0x00000004, "Microwave / Convection", appliance_api_microwave_v1_convection_erds, 13},
  {0x0004, 1, 0x00000008, "Microwave / Multi Stage Cook", appliance_api_microwave_v1_multi_stage_cook_erds, 3},
  {0x0004, 1, 0x00000010, "Microwave / Convection Time", appliance_api_microwave_v1_convection_time_erds, 1},
  {0x0004, 1, 0x00000020, "Microwave / Melt and Steam Modes", appliance_api_microwave_v1_melt_and_steam_modes_erds, 4},
  {0x0005, 1, 0x00000001, "Advantium / Foundation", appliance_api_advantium_v1_foundation_erds, 12},
  {0x0005, 1, 0x00000002, "Advantium / Precision Cooking", appliance_api_advantium_v1_precision_cooking_erds, 3},
  {0x0005, 1, 0x00000004, "Advantium / 12 Hour Shutoff", appliance_api_advantium_v1_12_hour_shutoff_erds, 1},
  {0x0005, 1, 0x00000008, "Advantium / Timer End Tone", appliance_api_advantium_v1_timer_end_tone_erds, 1},
  {0x0005, 1, 0x00000010, "Advantium / Auto Recipe Conversion", appliance_api_advantium_v1_auto_recipe_conversion_erds, 1},
  {0x0005, 1, 0x00000020, "Advantium / Remote Enabled", appliance_api_advantium_v1_remote_enabled_erds, 2},
  {0x0005, 1, 0x00000040, "Advantium / Microwave Details", appliance_api_advantium_v1_microwave_details_erds, 1},
  {0x0005, 1, 0x00000080, "Advantium / Recipes", appliance_api_advantium_v1_recipes_erds, 1},
  {0x0005, 1, 0x00000100, "Advantium / Notifications", appliance_api_advantium_v1_notifications_erds, 1},
  {0x0005, 1, 0x00000200, "Advantium / Camera", appliance_api_advantium_v1_camera_erds, 3},
  {0x0005, 1, 0x00000400, "Advantium / Remote Menu Cooking", appliance_api_advantium_v1_remote_menu_cooking_erds, 4},
  {0x0006, 1, 0x00000001, "Dishwasher / Foundation", appliance_api_dishwasher_v1_foundation_erds, 11},
  {0x0006, 1, 0x00000002, "Dishwasher / Service", appliance_api_dishwasher_v1_service_erds, 23},
  {0x0006, 1, 0x00000004, "Dishwasher / Current Diagnostics", appliance_api_dishwasher_v1_current_diagnostics_erds, 5},
  {0x0006, 1, 0x00000008, "Dishwasher / Low Rinse Aid", appliance_api_dishwasher_v1_low_rinse_aid_erds, 2},
  {0x0006, 1, 0x00000010, "Dishwasher / Door Pocket Light", appliance_api_dishwasher_v1_door_pocket_light_erds, 1},
  {0x0006, 1, 0x00000020, "Dishwasher / Feature Configuration", appliance_api_dishwasher_v1_feature_configuration_erds, 1},
  {0x0006, 1, 0x00000040, "Dishwasher / Remote Cycle Control", appliance_api_dishwasher_v1_remote_cycle_control_erds, 2},
  {0x0006, 1, 0x00000080, "Dishwasher / Cycle Settings Delay Start", appliance_api_dishwasher_v1_cycle_settings_delay_start_erds, 1},
  {0x0006, 1, 0x00000100, "Dishwasher / Cycle Settings Selected Cycle", appliance_api_dishwasher_v1_cycle_settings_selected_cycle_erds, 1},
  {0x0006, 1, 0x00000200, "Dishwasher / Cycle Settings Wash Temp Selection", appliance_api_dishwasher_v1_cycle_settings_wash_temp_selection_erds, 1},
  {0x0006, 1, 0x00000400, "Dishwasher / Cycle Settings Dry Temp Selection", appliance_api_dishwasher_v1_cycle_settings_dry_temp_selection_erds, 1},
  {0x0006, 1, 0x00000800, "Dishwasher / Cycle Settings Wash Zone Selection", appliance_api_dishwasher_v1_cycle_settings_wash_zone_selection_erds, 1},
  {0x0006, 1, 0x00001000, "Dishwasher / Cycle Settings Steam Option", appliance_api_dishwasher_v1_cycle_settings_steam_option_erds, 1},
  {0x0006, 1, 0x00002000, "Dishwasher / Cycle Settings Bottle Blast Option", appliance_api_dishwasher_v1_cycle_settings_bottle_blast_option_erds, 1},
  {0x0006, 1, 0x00004000, "Dishwasher / Cycle Settings UltraFresh Option", appliance_api_dishwasher_v1_cycle_settings_ultrafresh_option_erds, 1},
  {0x0006, 1, 0x00008000, "Dishwasher / Cycle Definitions", appliance_api_dishwasher_v1_cycle_definitions_erds, 10},
  {0x0006, 1, 0x00010000, "Dishwasher / Demo Mode", appliance_api_dishwasher_v1_demo_mode_erds, 1},
  {0x0006, 1, 0x00020000, "Dishwasher / Dry Heater and Fan System", appliance_api_dishwasher_v1_dry_heater_and_fan_system_erds, 3},
  {0x0006, 1, 0x00040000, "Dishwasher / Demand Response Extension", appliance_api_dishwasher_v1_demand_response_extension_erds, 2},
  {0x0006, 1, 0x00080000, "Dishwasher / Error", appliance_api_dishwasher_v1_error_erds, 1},
  {0x0006, 1, 0x00100000, "Dishwasher / Cycle Modifiers", appliance_api_dishwasher_v1_cycle_modifiers_erds, 2},
  {0x0006, 1, 0x00200000, "Dishwasher / Actual and Target Pump Speeds", appliance_api_dishwasher_v1_actual_and_target_pump_speeds_erds, 4},
  {0x0006, 1, 0x00400000, "Dishwasher / Current Diagnostics V2", appliance_api_dishwasher_v1_current_diagnostics_v2_erds, 4},
  {0x0006, 1, 0x00800000, "Dishwasher / Door Fan 2 Actual Speed Percentage", appliance_api_dishwasher_v1_door_fan_2_actual_speed_percentage_erds, 1},
  {0x0006, 1, 0x01000000, "Dishwasher / Preference Options", appliance_api_dishwasher_v1_preference_options_erds, 2},
  {0x0006, 1, 0x02000000, "Dishwasher / Remaining delay start time", appliance_api_dishwasher_v1_remaining_delay_start_time_erds, 1},
  {0x0006, 1, 0x04000000, "Dishwasher / Inlet Flow Rate", appliance_api_dishwasher_v1_inlet_flow_rate_erds, 1},
  {0x0006, 1, 0x08000000, "Dishwasher / Motor Identification Data", appliance_api_dishwasher_v1_motor_identification_data_erds, 1},
  {0x0006, 1, 0x10000000, "Dishwasher / Cycle Settings Silverware Wash Option", appliance_api_dishwasher_v1_cycle_settings_silverware_wash_option_erds, 1},
  {0x0006, 1, 0x20000000, "Dishwasher / Rinse and Hold Mode Option", appliance_api_dishwasher_v1_rinse_and_hold_mode_option_erds, 3},
  {0x0006, 1, 0x40000000, "Dishwasher / Custom Cycle", appliance_api_dishwasher_v1_custom_cycle_erds, 2},
  {0x0006, 1, 0x80000000, "Dishwasher / My Favorites", appliance_api_dishwasher_v1_my_favorites_erds, 2},
  {0x0006, 2, 0x00000001, "Dishwasher / Smart Assist", appliance_api_dishwasher_v2_smart_assist_erds, 4},
  {0x0006, 2, 0x00000002, "Dishwasher / Filter Fresh", appliance_api_dishwasher_v2_filter_fresh_erds, 1},
  {0x0006, 2, 0x00000004, "Dishwasher / Connect Plus", appliance_api_dishwasher_v2_connect_plus_erds, 2},
  {0x0006, 2, 0x00000008, "Dishwasher / Latched Key Status", appliance_api_dishwasher_v2_latched_key_status_erds, 1},
  {0x0006, 2, 0x00000010, "Dishwasher / Water Softener Setting", appliance_api_dishwasher_v2_water_softener_setting_erds, 2},
  {0x0006, 2, 0x00000020, "Dishwasher / Auto Door Open", appliance_api_dishwasher_v2_auto_door_open_erds, 2},
  {0x0006, 2, 0x00000040, "Dishwasher / Foundation 2", appliance_api_dishwasher_v2_foundation_2_erds, 5},
  {0x0006, 2, 0x00000080, "Dishwasher / Cycle Indication", appliance_api_dishwasher_v2_cycle_indication_erds, 1},
  {0x0006, 2, 0x00000100, "Dishwasher / Cycle Names", appliance_api_dishwasher_v2_cycle_names_erds, 1},
  {0x0006, 2, 0x00000200, "Dishwasher / Analog Door Latch AD Counts", appliance_api_dishwasher_v2_analog_door_latch_ad_counts_erds, 1},
  {0x0007, 1, 0x00000001, "Cooking / Foundation", appliance_api_cooking_v1_foundation_erds, 1},
  {0x0007, 1, 0x00000002, "Cooking / Twelve Hour Shutoff", appliance_api_cooking_v1_twelve_hour_shutoff_erds, 1},
  {0x0007, 1, 0x00000004, "Cooking / End Tone Setting", appliance_api_cooking_v1_end_tone_setting_erds, 1},
  {0x0007, 1, 0x00000008, "Cooking / Convection Conversion", appliance_api_cooking_v1_convection_conversion_erds, 1},
  {0x0007, 1, 0x00000010, "Cooking / Upper Oven Foundation", appliance_api_cooking_v1_upper_oven_foundation_erds, 14},
  {0x0007, 1, 0x00000020, "Cooking / Upper Oven Kitchen Timer", appliance_api_cooking_v1_upper_oven_kitchen_timer_erds, 1},
  {0x0007, 1, 0x00000040, "Cooking / Upper Oven Probe", appliance_api_cooking_v1_upper_oven_probe_erds, 2},
  {0x0007, 1, 0x00000080, "Cooking / Upper Oven Precision Cook Selection", appliance_api_cooking_v1_upper_oven_precision_cook_selection_erds, 3},
  {0x0007, 1, 0x00000100, "Cooking / Lower Oven Foundation", appliance_api_cooking_v1_lower_oven_foundation_erds, 14},
  {0x0007, 1, 0x00000200, "Cooking / Lower Oven Kitchen Timer", appliance_api_cooking_v1_lower_oven_kitchen_timer_erds, 1},
  {0x0007, 1, 0x00000400, "Cooking / Lower Oven Probe", appliance_api_cooking_v1_lower_oven_probe_erds, 2},
  {0x0007, 1, 0x00000800, "Cooking / Lower Oven Precision Cook Selection", appliance_api_cooking_v1_lower_oven_precision_cook_selection_erds, 3},
  {0x0007, 1, 0x00001000, "Cooking / Warming Drawer", appliance_api_cooking_v1_warming_drawer_erds, 1},
  {0x0007, 1, 0x00002000, "Cooking / Cooktop Foundation", appliance_api_cooking_v1_cooktop_foundation_erds, 2},
  {0x0007, 1, 0x00004000, "Cooking / Extended Cooktop Foundation", appliance_api_cooking_v1_extended_cooktop_foundation_erds, 2},
  {0x0007, 1, 0x00008000, "Cooking / Cooktop Sous Vide Cooking", appliance_api_cooking_v1_cooktop_sous_vide_cooking_erds, 10},
  {0x0007, 1, 0x00010000, "Cooking / Cooktop Closed Loop Cooking", appliance_api_cooking_v1_cooktop_closed_loop_cooking_erds, 20},
  {0x0007, 1, 0x00020000, "Cooking / Extended Cooktop Closed Loop Cooking", appliance_api_cooking_v1_extended_cooktop_closed_loop_cooking_erds, 4},
  {0x0007, 1, 0x00040000, "Cooking / Built In Pop Up Sensor", appliance_api_cooking_v1_built_in_pop_up_sensor_erds, 2},
  {0x0007, 1, 0x00080000, "Cooking / Gas Cooktop Lockout", appliance_api_cooking_v1_gas_cooktop_lockout_erds, 2},
  {0x0007, 1, 0x00100000, "Cooking / Upper Automatic Door Open", appliance_api_cooking_v1_upper_automatic_door_open_erds, 2},
  {0x0007, 1, 0x00200000, "Cooking / Lower Automatic Door Open", appliance_api_cooking_v1_lower_automatic_door_open_erds, 2},
  {0x0007, 1, 0x00400000, "Cooking / Single Knob Ui", appliance_api_cooking_v1_single_knob_ui_erds, 8},
  {0x0007, 1, 0x00800000, "Cooking / Default Set Temperature Range", appliance_api_cooking_v1_default_set_temperature_range_erds, 1},
  {0x0007, 1, 0x01000000, "Cooking / Oven Camera", appliance_api_cooking_v1_oven_camera_erds, 4},
  {0x0007, 1, 0x02000000, "Cooking / Supported Cookware", appliance_api_cooking_v1_supported_cookware_erds, 1},
  {0x0007, 1, 0x04000000, "Cooking / Key Press Data", appliance_api_cooking_v1_key_press_data_erds, 1},
  {0x0007, 1, 0x08000000, "Cooking / Menu Tree", appliance_api_cooking_v1_menu_tree_erds, 3},
  {0x0007, 1, 0x10000000, "Cooking / Precision Cook Mode Name", appliance_api_cooking_v1_precision_cook_mode_name_erds, 4},
  {0x0007, 1, 0x20000000, "Cooking / Oven Cavity Layout", appliance_api_cooking_v1_oven_cavity_layout_erds, 1},
  {0x0007, 1, 0x40000000, "Cooking / Accent Lighting", appliance_api_cooking_v1_accent_lighting_erds, 3},
  {0x0007, 1, 0x80000000, "Cooking / Air Fry Set Temperature Range", appliance_api_cooking_v1_air_fry_set_temperature_range_erds, 1},
  {0x0007, 2, 0x00000001, "Cooking / Upper Oven Baking Surface", appliance_api_cooking_v2_upper_oven_baking_surface_erds, 2},
  {0x0007, 2, 0x00000002, "Cooking / SmartHQ Bake", appliance_api_cooking_v2_smarthq_bake_erds, 2},
  {0x0007, 2, 0x00000004, "Cooking / Cooktop Right Front Burner Termination Reason", appliance_api_cooking_v2_cooktop_right_front_burner_termination_reason_erds, 1},
  {0x0007, 2, 0x00000008, "Cooking / Clock Display", appliance_api_cooking_v2_clock_display_erds, 2},
  {0x0007, 2, 0x00000010, "Cooking / Recipe API", appliance_api_cooking_v2_recipe_api_erds, 1},
  {0x0007, 2, 0x00000020, "Cooking / Water Reservoir", appliance_api_cooking_v2_water_reservoir_erds, 1},
  {0x0007, 2, 0x00000040, "Cooking / Remote UI", appliance_api_cooking_v2_remote_ui_erds, 2},
  {0x0007, 2, 0x00000080, "Cooking / Upper Cavity Recipe API V2", appliance_api_cooking_v2_upper_cavity_recipe_api_v2_erds, 8},
  {0x0007, 2, 0x00000100, "Cooking / Lower Cavity Recipe API V2", appliance_api_cooking_v2_lower_cavity_recipe_api_v2_erds, 8},
  {0x0007, 2, 0x00000200, "Cooking / Upper Cavity Remote Precision Cook", appliance_api_cooking_v2_upper_cavity_remote_precision_cook_erds, 4},
  {0x0007, 2, 0x00000400, "Cooking / Lower Cavity Remote Precision Cook", appliance_api_cooking_v2_lower_cavity_remote_precision_cook_erds, 4},
  {0x0007, 2, 0x00000800, "Cooking / Upper Cavity Remote Precision Cook V3", appliance_api_cooking_v2_upper_cavity_remote_precision_cook_v3_erds, 7},
  {0x0007, 2, 0x00001000, "Cooking / Lower Cavity Remote Precision Cook V3", appliance_api_cooking_v2_lower_cavity_remote_precision_cook_v3_erds, 7},
  {0x0007, 2, 0x00002000, "Cooking / Cooktop Multi Burner Closed Loop Cooking", appliance_api_cooking_v2_cooktop_multi_burner_closed_loop_cooking_erds, 43},
  {0x0007, 2, 0x00004000, "Cooking / Customer Feedback", appliance_api_cooking_v2_customer_feedback_erds, 2},
  {0x0007, 2, 0x00008000, "Cooking / Remote Bluetooth Device Management", appliance_api_cooking_v2_remote_bluetooth_device_management_erds, 6},
  {0x0007, 2, 0x00010000, "Cooking / Stui Presence", nullptr, 0},
  {0x0007, 2, 0x00020000, "Cooking / Sentry", nullptr, 0},
  {0x0007, 2, 0x00040000, "Cooking / Estimated Remaining Cook Minutes", appliance_api_cooking_v2_estimated_remaining_cook_minutes_erds, 2},
  {0x0007, 2, 0x00080000, "Cooking / AI Cook Mode Session ID", appliance_api_cooking_v2_ai_cook_mode_session_id_erds, 2},
  {0x0007, 2, 0x00100000, "Cooking / Articulating Display", appliance_api_cooking_v2_articulating_display_erds, 4},
  {0x0007, 2, 0x00200000, "Cooking / UI Menu Tree Key", appliance_api_cooking_v2_ui_menu_tree_key_erds, 2},
  {0x0007, 2, 0x00400000, "Cooking / Upper Oven User Interactive Precision Cooking", appliance_api_cooking_v2_upper_oven_user_interactive_precision_cooking_erds, 2},
  {0x0007, 2, 0x00800000, "Cooking / Lower Oven User Interactive Precision Cooking", appliance_api_cooking_v2_lower_oven_user_interactive_precision_cooking_erds, 2},
  {0x0007, 2, 0x01000000, "Cooking / Automatic Door Opener Local Public Enable", appliance_api_cooking_v2_automatic_door_opener_local_public_enable_erds, 1},
  {0x0007, 2, 0x02000000, "Cooking / Oven Light Automatic On Off Setting", appliance_api_cooking_v2_oven_light_automatic_on_off_setting_erds, 3},
  {0x0007, 2, 0x04000000, "Cooking / Enhanced Sabbath", appliance_api_cooking_v2_enhanced_sabbath_erds, 6},
  {0x0007, 2, 0x08000000, "Cooking / Cook Cam AI Assistant", appliance_api_cooking_v2_cook_cam_ai_assistant_erds, 5},
  {0x0007, 2, 0x10000000, "Cooking / Cooktop v2 Notifications", appliance_api_cooking_v2_cooktop_v2_notifications_erds, 2},
  {0x0007, 2, 0x20000000, "Cooking / Flex Cook Timer", appliance_api_cooking_v2_flex_cook_timer_erds, 10},
  {0x0007, 2, 0x40000000, "Cooking / Microphone Supported", nullptr, 0},
  {0x0007, 2, 0x80000000, "Cooking / Enhanced Speaker Supported", nullptr, 0},
  {0x0007, 3, 0x00000001, "Cooking / UI misconfiguration detection", nullptr, 0},
  {0x0007, 3, 0x00000002, "Cooking / Enhanced Sabbath Manual Mode", appliance_api_cooking_v3_enhanced_sabbath_manual_mode_erds, 5},
  {0x0007, 3, 0x00000004, "Cooking / Upper Local Available Cook Modes", appliance_api_cooking_v3_upper_local_available_cook_modes_erds, 1},
  {0x0007, 3, 0x00000008, "Cooking / Lower Local Available Cook Modes", appliance_api_cooking_v3_lower_local_available_cook_modes_erds, 1},
  {0x0007, 3, 0x00000010, "Cooking / Enhanced Sabbath Override Settings", appliance_api_cooking_v3_enhanced_sabbath_override_settings_erds, 4},
  {0x000a, 1, 0x00000001, "Window AC / Foundation", appliance_api_window_ac_v1_foundation_erds, 10},
  {0x000a, 1, 0x00000002, "Window AC / Demand Response", appliance_api_window_ac_v1_demand_response_erds, 2},
  {0x000a, 1, 0x00000004, "Window AC / Energy Reporting", appliance_api_window_ac_v1_energy_reporting_erds, 4},
  {0x000a, 1, 0x00000008, "Window AC / Selectable Temperature Range", appliance_api_window_ac_v1_selectable_temperature_range_erds, 1},
  {0x000a, 1, 0x00000010, "Window AC / Cycle Count", appliance_api_window_ac_v1_cycle_count_erds, 1},
  {0x000a, 1, 0x00000020, "Window AC / Temporary Offset Demand Response", appliance_api_window_ac_v1_temporary_offset_demand_response_erds, 2},
  {0x000a, 1, 0x00000040, "Window AC / Heat Mode", appliance_api_window_ac_v1_heat_mode_erds, 1},
  {0x000a, 1, 0x00000080, "Window AC / Eco Mode", appliance_api_window_ac_v1_eco_mode_erds, 1},
  {0x000a, 1, 0x00000100, "Window AC / Turbo and Quiet", appliance_api_window_ac_v1_turbo_and_quiet_erds, 3},
  {0x000e, 1, 0x00000001, "Split AC / Foundation", appliance_api_split_ac_v1_foundation_erds, 12},
  {0x000e, 1, 0x00000002, "Split AC / Remote Monitoring", appliance_api_split_ac_v1_remote_monitoring_erds, 7},
  {0x000e, 1, 0x00000004, "Split AC / Self Clean", appliance_api_split_ac_v1_self_clean_erds, 6},
  {0x000e, 1, 0x00000008, "Split AC / External Resistance Heating", appliance_api_split_ac_v1_external_resistance_heating_erds, 3},
  {0x000e, 1, 0x00000010, "Split AC / Air Damper", appliance_api_split_ac_v1_air_damper_erds, 3},
  {0x000e, 1, 0x00000020, "Split AC / Remote Diagnostic Information", appliance_api_split_ac_v1_remote_diagnostic_information_erds, 5},
  {0x000e, 1, 0x00000040, "Split AC / Vacation Mode Supported", appliance_api_split_ac_v1_vacation_mode_supported_erds, 2},
  {0x000e, 1, 0x00000080, "Split AC / Updated Vacation Mode", appliance_api_split_ac_v1_updated_vacation_mode_erds, 3},
  {0x000e, 1, 0x00000100, "Split AC / Alerts", appliance_api_split_ac_v1_alerts_erds, 1},
  {0x000e, 1, 0x00000200, "Split AC / Temperature Display Mode Support", appliance_api_split_ac_v1_temperature_display_mode_support_erds, 2},
  {0x000e, 1, 0x00000400, "Split AC / Motion Sensing Mode Support", appliance_api_split_ac_v1_motion_sensing_mode_support_erds, 2},
  {0x000e, 1, 0x00000800, "Split AC / Static Pressure and Condensate Float Switch Support", appliance_api_split_ac_v1_static_pressure_and_condensate_float_switch_support_erds, 2},
  {0x000e, 1, 0x00001000, "Split AC / RS485 Communication", appliance_api_split_ac_v1_rs485_communication_erds, 1},
  {0x000e, 1, 0x00002000, "Split AC / Outdoor Model and Serial Number", appliance_api_split_ac_v1_outdoor_model_and_serial_number_erds, 2},
  {0x000e, 1, 0x00004000, "Split AC / Refrigerant Leak Sensor Support", appliance_api_split_ac_v1_refrigerant_leak_sensor_support_erds, 7},
  {0x000e, 1, 0x00008000, "Split AC / Notification Support", appliance_api_split_ac_v1_notification_support_erds, 2},
  {0x000e, 1, 0x00010000, "Split AC / MultiMode Conflict Reporting", appliance_api_split_ac_v1_multimode_conflict_reporting_erds, 1},
  {0x000f, 1, 0x00000001, "Home Water Softener / Foundation", nullptr, 0},
  {0x000f, 1, 0x00000002, "Home Water Softener / Water Valve", appliance_api_home_water_softener_v1_water_valve_erds, 1},
  {0x000f, 1, 0x00000004, "Home Water Softener / Manual Override", appliance_api_home_water_softener_v1_manual_override_erds, 1},
  {0x000f, 1, 0x00000008, "Home Water Softener / Continuous Flow Time Limits", appliance_api_home_water_softener_v1_continuous_flow_time_limits_erds, 1},
  {0x000f, 1, 0x00000010, "Home Water Softener / Flow Meter", appliance_api_home_water_softener_v1_flow_meter_erds, 1},
  {0x000f, 1, 0x00000020, "Home Water Softener / Water Usage", appliance_api_home_water_softener_v1_water_usage_erds, 4},
  {0x000f, 1, 0x00000040, "Home Water Softener / Water Auto shutoff", appliance_api_home_water_softener_v1_water_auto_shutoff_erds, 5},
  {0x000f, 1, 0x00000080, "Home Water Softener / Salt Information", appliance_api_home_water_softener_v1_salt_information_erds, 1},
  {0x000f, 1, 0x00000100, "Home Water Softener / Water Softener Alerts Legacy", appliance_api_home_water_softener_v1_water_softener_alerts_legacy_erds, 4},
  {0x0010, 1, 0x00000001, "Home Water Filter / Foundation", nullptr, 0},
  {0x0010, 1, 0x00000002, "Home Water Filter / Water Valve", appliance_api_home_water_filter_v1_water_valve_erds, 3},
  {0x0010, 1, 0x00000004, "Home Water Filter / Manual Override", appliance_api_home_water_filter_v1_manual_override_erds, 1},
  {0x0010, 1, 0x00000008, "Home Water Filter / Filter Expiration Limits", appliance_api_home_water_filter_v1_filter_expiration_limits_erds, 1},
  {0x0010, 1, 0x00000010, "Home Water Filter / Continuous Flow Time Limits", appliance_api_home_water_filter_v1_continuous_flow_time_limits_erds, 1},
  {0x0010, 1, 0x00000020, "Home Water Filter / Leak Detection", appliance_api_home_water_filter_v1_leak_detection_erds, 1},
  {0x0010, 1, 0x00000040, "Home Water Filter / Flow Meter", appliance_api_home_water_filter_v1_flow_meter_erds, 1},
  {0x0010, 1, 0x00000080, "Home Water Filter / Water Usage", appliance_api_home_water_filter_v1_water_usage_erds, 3},
  {0x0010, 1, 0x00000100, "Home Water Filter / Filter Status", appliance_api_home_water_filter_v1_filter_status_erds, 2},
  {0x0010, 1, 0x00000200, "Home Water Filter / Reset Water Filter", appliance_api_home_water_filter_v1_reset_water_filter_erds, 1},
  {0x0010, 1, 0x00000400, "Home Water Filter / Water Auto shutoff", appliance_api_home_water_filter_v1_water_auto_shutoff_erds, 2},
  {0x0010, 1, 0x00000800, "Home Water Filter / Water Filter Alerts Legacy", appliance_api_home_water_filter_v1_water_filter_alerts_legacy_erds, 4},
  {0x0010, 1, 0x00001000, "Home Water Filter / Notifications", appliance_api_home_water_filter_v1_notifications_erds, 1},
  {0x0010, 1, 0x00002000, "Home Water Filter / Filter 1 Status", appliance_api_home_water_filter_v1_filter_1_status_erds, 2},
  {0x0010, 1, 0x00004000, "Home Water Filter / Filter 2 Status", appliance_api_home_water_filter_v1_filter_2_status_erds, 2},
  {0x0010, 1, 0x00008000, "Home Water Filter / Home Water Filter Platform", appliance_api_home_water_filter_v1_home_water_filter_platform_erds, 1},
  {0x0012, 1, 0x00000001, "Delivery Box / Go Box Foundation", appliance_api_delivery_box_v1_go_box_foundation_erds, 5},
  {0x0012, 1, 0x00000004, "Delivery Box / Go Box Mini Foundation", appliance_api_delivery_box_v1_go_box_mini_foundation_erds, 2},
  {0x0014, 1, 0x00000001, "Zoneline / Foundation", appliance_api_zoneline_v1_foundation_erds, 41},
  {0x0014, 1, 0x00000002, "Zoneline / UVC Kit", appliance_api_zoneline_v1_uvc_kit_erds, 1},
  {0x0014, 1, 0x00000004, "Zoneline / Stratos Base", appliance_api_zoneline_v1_stratos_base_erds, 12},
  {0x0014, 1, 0x00000008, "Zoneline / Stratos Heat Pump", appliance_api_zoneline_v1_stratos_heat_pump_erds, 3},
  {0x0014, 1, 0x00000010, "Zoneline / Stratos Inverter", appliance_api_zoneline_v1_stratos_inverter_erds, 3},
  {0x0014, 1, 0x00000020, "Zoneline / Stratos EEV", appliance_api_zoneline_v1_stratos_eev_erds, 7},
  {0x0014, 1, 0x00000040, "Zoneline / Stratos MUA", appliance_api_zoneline_v1_stratos_mua_erds, 7},
  {0x0014, 1, 0x00000080, "Zoneline / Stratos UVC", appliance_api_zoneline_v1_stratos_uvc_erds, 2},
  {0x0014, 1, 0x00000100, "Zoneline / Stratos Dehum", appliance_api_zoneline_v1_stratos_dehum_erds, 2},
  {0x0014, 1, 0x00000200, "Zoneline / Stratos OD Sensors", appliance_api_zoneline_v1_stratos_od_sensors_erds, 2},
  {0x0014, 1, 0x00000400, "Zoneline / Stratos ID Outlet Sensor", appliance_api_zoneline_v1_stratos_id_outlet_sensor_erds, 1},
  {0x0014, 1, 0x00000800, "Zoneline / Stratos EEV Sensors", appliance_api_zoneline_v1_stratos_eev_sensors_erds, 2},
  {0x0014, 1, 0x00001000, "Zoneline / Stratos OD Coil Liquid Sensor", appliance_api_zoneline_v1_stratos_od_coil_liquid_sensor_erds, 1},
  {0x0014, 1, 0x00002000, "Zoneline / Stratos Humidity Sensor", appliance_api_zoneline_v1_stratos_humidity_sensor_erds, 2},
  {0x0014, 1, 0x00004000, "Zoneline / Stratos Base Aux Modes", appliance_api_zoneline_v1_stratos_base_aux_modes_erds, 16},
  {0x0014, 1, 0x00008000, "Zoneline / Stratos Heat Source Optimization", appliance_api_zoneline_v1_stratos_heat_source_optimization_erds, 2},
  {0x0015, 1, 0x00000001, "Hood / Foundation", appliance_api_hood_v1_foundation_erds, 7},
  {0x0015, 1, 0x00000002, "Hood / Hood Delays", appliance_api_hood_v1_hood_delays_erds, 2},
  {0x0015, 1, 0x00000004, "Hood / Hub Support", appliance_api_hood_v1_hub_support_erds, 6},
  {0x0015, 1, 0x00000008, "Hood / Hood Notifications", appliance_api_hood_v1_hood_notifications_erds, 2},
  {0x0015, 1, 0x00000010, "Hood / Hood Boost Parameters", appliance_api_hood_v1_hood_boost_parameters_erds, 2},
  {0x0015, 1, 0x00000020, "Hood / Hood Delay Off Parameters", appliance_api_hood_v1_hood_delay_off_parameters_erds, 2},
  {0x0015, 1, 0x00000040, "Hood / Hood Notifications v2", appliance_api_hood_v1_hood_notifications_v2_erds, 2},
  {0x0015, 1, 0x00000080, "Hood / Hood Fan Run Time", appliance_api_hood_v1_hood_fan_run_time_erds, 3},
  {0x0016, 1, 0x00000001, "Portable AC / Foundation", appliance_api_portable_ac_v1_foundation_erds, 7},
  {0x0016, 1, 0x00000002, "Portable AC / Demand Response", appliance_api_portable_ac_v1_demand_response_erds, 2},
  {0x0016, 1, 0x00000004, "Portable AC / Energy Reporting", appliance_api_portable_ac_v1_energy_reporting_erds, 4},
  {0x0016, 1, 0x00000008, "Portable AC / Notification Support", appliance_api_portable_ac_v1_notification_support_erds, 2},
  {0x0017, 1, 0x00000001, "Combination Washer Dryer / Act Like a Washer", appliance_api_combination_washer_dryer_v1_act_like_a_washer_erds, 22},
  {0x0017, 2, 0x00000001, "Combination Washer Dryer / Foundation", appliance_api_combination_washer_dryer_v2_foundation_erds, 10},
  {0x0017, 2, 0x00000002, "Combination Washer Dryer / Wash Mode option", appliance_api_combination_washer_dryer_v2_wash_mode_option_erds, 2},
  {0x0017, 2, 0x00000004, "Combination Washer Dryer / Soil option", appliance_api_combination_washer_dryer_v2_soil_option_erds, 2},
  {0x0017, 2, 0x00000008, "Combination Washer Dryer / Water temperature option", appliance_api_combination_washer_dryer_v2_water_temperature_option_erds, 2},
  {0x0017, 2, 0x00000010, "Combination Washer Dryer / Extra rinse option", appliance_api_combination_washer_dryer_v2_extra_rinse_option_erds, 2},
  {0x0017, 2, 0x00000020, "Combination Washer Dryer / PreWash option", appliance_api_combination_washer_dryer_v2_prewash_option_erds, 2},
  {0x0017, 2, 0x00000040, "Combination Washer Dryer / More water toggle option", appliance_api_combination_washer_dryer_v2_more_water_toggle_option_erds, 2},
  {0x0017, 2, 0x00000080, "Combination Washer Dryer / Spin Level option", appliance_api_combination_washer_dryer_v2_spin_level_option_erds, 2},
  {0x0017, 2, 0x00000100, "Combination Washer Dryer / Dry Mode option", appliance_api_combination_washer_dryer_v2_dry_mode_option_erds, 2},
  {0x0017, 2, 0x00000200, "Combination Washer Dryer / Damp alert option and status", appliance_api_combination_washer_dryer_v2_damp_alert_option_and_status_erds, 3},
  {0x0017, 2, 0x00000400, "Combination Washer Dryer / Wrinkle care option", appliance_api_combination_washer_dryer_v2_wrinkle_care_option_erds, 2},
  {0x0017, 2, 0x00000800, "Combination Washer Dryer / Timed dry option", appliance_api_combination_washer_dryer_v2_timed_dry_option_erds, 2},
  {0x0017, 2, 0x00001000, "Combination Washer Dryer / Air Fluff option", appliance_api_combination_washer_dryer_v2_air_fluff_option_erds, 2},
  {0x0017, 2, 0x00002000, "Combination Washer Dryer / EcoCool drying option", appliance_api_combination_washer_dryer_v2_ecocool_drying_option_erds, 2},
  {0x0017, 2, 0x00004000, "Combination Washer Dryer / More dry toggle option", appliance_api_combination_washer_dryer_v2_more_dry_toggle_option_erds, 2},
  {0x0017, 2, 0x00008000, "Combination Washer Dryer / Downloaded Cycle", appliance_api_combination_washer_dryer_v2_downloaded_cycle_erds, 4},
  {0x0017, 2, 0x00010000, "Combination Washer Dryer / Delay Start", appliance_api_combination_washer_dryer_v2_delay_start_erds, 4},
  {0x0017, 2, 0x00020000, "Combination Washer Dryer / Remote Start", appliance_api_combination_washer_dryer_v2_remote_start_erds, 5},
  {0x0017, 2, 0x00040000, "Combination Washer Dryer / Add Dry", appliance_api_combination_washer_dryer_v2_add_dry_erds, 5},
  {0x0017, 2, 0x00080000, "Combination Washer Dryer / Adaptive Smart Dispense and Adjustability Detergent Tank", appliance_api_combination_washer_dryer_v2_adaptive_smart_dispense_and_adjustability_detergent_tank_erds, 13},
  {0x0017, 2, 0x00100000, "Combination Washer Dryer / Adaptive Smart Dispense and Adjustability Softener Tank", appliance_api_combination_washer_dryer_v2_adaptive_smart_dispense_and_adjustability_softener_tank_erds, 13},
  {0x0017, 2, 0x00200000, "Combination Washer Dryer / Local Wash Complete Indication", appliance_api_combination_washer_dryer_v2_local_wash_complete_indication_erds, 2},
  {0x0017, 2, 0x00400000, "Combination Washer Dryer / Push Notifications", appliance_api_combination_washer_dryer_v2_push_notifications_erds, 1},
  {0x0017, 2, 0x00800000, "Combination Washer Dryer / Remote Cycle Selection", appliance_api_combination_washer_dryer_v2_remote_cycle_selection_erds, 2},
  {0x0017, 2, 0x01000000, "Combination Washer Dryer / Sanitize Option", appliance_api_combination_washer_dryer_v2_sanitize_option_erds, 2},
  {0x0017, 2, 0x02000000, "Combination Washer Dryer / Dry Time Selection", appliance_api_combination_washer_dryer_v2_dry_time_selection_erds, 2},
  {0x0017, 2, 0x04000000, "Combination Washer Dryer / Filter Microswitch Status", appliance_api_combination_washer_dryer_v2_filter_microswitch_status_erds, 1},
  {0x0017, 2, 0x08000000, "Combination Washer Dryer / Control Lock Option", appliance_api_combination_washer_dryer_v2_control_lock_option_erds, 3},
  {0x0017, 2, 0x10000000, "Combination Washer Dryer / Cycle Function", appliance_api_combination_washer_dryer_v2_cycle_function_erds, 2},
  {0x0017, 2, 0x20000000, "Combination Washer Dryer / Remote Pet Hair Removal Option", appliance_api_combination_washer_dryer_v2_remote_pet_hair_removal_option_erds, 3},
  {0x0018, 1, 0x00000001, "Dual Zone Wine Chiller / Foundation", appliance_api_dual_zone_wine_chiller_v1_foundation_erds, 17},
  {0x0018, 1, 0x00000002, "Dual Zone Wine Chiller / Light Control", appliance_api_dual_zone_wine_chiller_v1_light_control_erds, 1},
  {0x0018, 1, 0x00000004, "Dual Zone Wine Chiller / Presence Sensing", appliance_api_dual_zone_wine_chiller_v1_presence_sensing_erds, 1},
  {0x0019, 1, 0x00000001, "Beverage Center / Foundation", appliance_api_beverage_center_v1_foundation_erds, 18},
  {0x0019, 1, 0x00000002, "Beverage Center / Light Control", appliance_api_beverage_center_v1_light_control_erds, 1},
  {0x0019, 1, 0x00000004, "Beverage Center / Presence Sensing", appliance_api_beverage_center_v1_presence_sensing_erds, 1},
  {0x001a, 1, 0x00000001, "Coffee Brewer / Foundation", appliance_api_coffee_brewer_v1_foundation_erds, 16},
  {0x001a, 1, 0x00000002, "Coffee Brewer / Custom Brew", appliance_api_coffee_brewer_v1_custom_brew_erds, 4},
  {0x001a, 1, 0x00000004, "Coffee Brewer / Filter", appliance_api_coffee_brewer_v1_filter_erds, 1},
  {0x001a, 1, 0x00000008, "Coffee Brewer / Pot Presence", appliance_api_coffee_brewer_v1_pot_presence_erds, 1},
  {0x001a, 1, 0x00000010, "Coffee Brewer / Out of Water Detection", appliance_api_coffee_brewer_v1_out_of_water_detection_erds, 1},
  {0x001a, 1, 0x00000020, "Coffee Brewer / Outlet Temperature", appliance_api_coffee_brewer_v1_outlet_temperature_erds, 1},
  {0x001a, 1, 0x00000040, "Coffee Brewer / Descale", appliance_api_coffee_brewer_v1_descale_erds, 4},
  {0x001a, 1, 0x00000080, "Coffee Brewer / Delay Start", appliance_api_coffee_brewer_v1_delay_start_erds, 2},
  {0x001a, 1, 0x00000100, "Coffee Brewer / Model Configuration", appliance_api_coffee_brewer_v1_model_configuration_erds, 1},
  {0x001a, 1, 0x00000200, "Coffee Brewer / Three Favorite Brews", appliance_api_coffee_brewer_v1_three_favorite_brews_erds, 6},
  {0x001a, 1, 0x00000400, "Coffee Brewer / Supported Features", appliance_api_coffee_brewer_v1_supported_features_erds, 1},
  {0x001a, 1, 0x00000800, "Coffee Brewer / Filter Status", appliance_api_coffee_brewer_v1_filter_status_erds, 2},
  {0x001a, 1, 0x00001000, "Coffee Brewer / Filter Usage", appliance_api_coffee_brewer_v1_filter_usage_erds, 1},
  {0x001a, 2, 0x00000001, "Coffee Brewer / Foundation", appliance_api_coffee_brewer_v2_foundation_erds, 4},
  {0x001a, 2, 0x00000002, "Coffee Brewer / Three Favorite Brews", appliance_api_coffee_brewer_v2_three_favorite_brews_erds, 6},
  {0x001a, 2, 0x00000004, "Coffee Brewer / Brew Cancel", appliance_api_coffee_brewer_v2_brew_cancel_erds, 1},
  {0x001a, 2, 0x00000008, "Coffee Brewer / Brew Status", appliance_api_coffee_brewer_v2_brew_status_erds, 1},
  {0x001a, 2, 0x00000010, "Coffee Brewer / Descale Status", appliance_api_coffee_brewer_v2_descale_status_erds, 2},
  {0x001a, 2, 0x00000020, "Coffee Brewer / Filter Status", appliance_api_coffee_brewer_v2_filter_status_erds, 2},
  {0x001a, 2, 0x00000040, "Coffee Brewer / Pot Presence", appliance_api_coffee_brewer_v2_pot_presence_erds, 1},
  {0x001a, 2, 0x00000080, "Coffee Brewer / Out of Water Detection", appliance_api_coffee_brewer_v2_out_of_water_detection_erds, 1},
  {0x001a, 2, 0x00000100, "Coffee Brewer / Out Of Beans", appliance_api_coffee_brewer_v2_out_of_beans_erds, 1},
  {0x001a, 2, 0x00000200, "Coffee Brewer / Grinder missing or not locked", appliance_api_coffee_brewer_v2_grinder_missing_or_not_locked_erds, 1},
  {0x001a, 2, 0x00000400, "Coffee Brewer / Tank missing or not locked", appliance_api_coffee_brewer_v2_tank_missing_or_not_locked_erds, 1},
  {0x001a, 2, 0x00000800, "Coffee Brewer / Brew basket missing or not locked", appliance_api_coffee_brewer_v2_brew_basket_missing_or_not_locked_erds, 1},
  {0x001a, 2, 0x00001000, "Coffee Brewer / Clean Brew Basket", appliance_api_coffee_brewer_v2_clean_brew_basket_erds, 1},
  {0x001a, 2, 0x00002000, "Coffee Brewer / Disable Grinder", appliance_api_coffee_brewer_v2_disable_grinder_erds, 2},
  {0x001a, 2, 0x00004000, "Coffee Brewer / Grind Setting", appliance_api_coffee_brewer_v2_grind_setting_erds, 1},
  {0x001a, 2, 0x00008000, "Coffee Brewer / Standby", appliance_api_coffee_brewer_v2_standby_erds, 2},
  {0x001a, 2, 0x00010000, "Coffee Brewer / Model Configuration", appliance_api_coffee_brewer_v2_model_configuration_erds, 1},
  {0x001a, 2, 0x00020000, "Coffee Brewer / Delay Start", appliance_api_coffee_brewer_v2_delay_start_erds, 2},
  {0x001b, 1, 0x00000001, "Opal Nugget Ice Maker / Foundation", appliance_api_opal_nugget_ice_maker_v1_foundation_erds, 5},
  {0x001b, 1, 0x00000002, "Opal Nugget Ice Maker / Filter Status", appliance_api_opal_nugget_ice_maker_v1_filter_status_erds, 1},
  {0x001b, 1, 0x00000004, "Opal Nugget Ice Maker / Cleaning Required", appliance_api_opal_nugget_ice_maker_v1_cleaning_required_erds, 1},
  {0x001b, 1, 0x00000008, "Opal Nugget Ice Maker / Filter Reset", appliance_api_opal_nugget_ice_maker_v1_filter_reset_erds, 1},
  {0x001b, 1, 0x00000010, "Opal Nugget Ice Maker / Ice Maker Push Notifications", appliance_api_opal_nugget_ice_maker_v1_ice_maker_push_notifications_erds, 1},
  {0x001b, 1, 0x00000020, "Opal Nugget Ice Maker / Filter Usage Details", appliance_api_opal_nugget_ice_maker_v1_filter_usage_details_erds, 4},
  {0x001b, 1, 0x00000040, "Opal Nugget Ice Maker / Descale Time Analytics", appliance_api_opal_nugget_ice_maker_v1_descale_time_analytics_erds, 2},
  {0x001b, 1, 0x00000080, "Opal Nugget Ice Maker / Sanitize Elapsed Time", appliance_api_opal_nugget_ice_maker_v1_sanitize_elapsed_time_erds, 1},
  {0x001b, 1, 0x00000100, "Opal Nugget Ice Maker / Total Compressor Runtime", appliance_api_opal_nugget_ice_maker_v1_total_compressor_runtime_erds, 1},
  {0x001b, 1, 0x00000200, "Opal Nugget Ice Maker / Model Configuration", appliance_api_opal_nugget_ice_maker_v1_model_configuration_erds, 1},
  {0x001b, 1, 0x00000400, "Opal Nugget Ice Maker / Multicolored Light", appliance_api_opal_nugget_ice_maker_v1_multicolored_light_erds, 2},
  {0x001b, 1, 0x00000800, "Opal Nugget Ice Maker / Water Level Tone Setting Supported", appliance_api_opal_nugget_ice_maker_v1_water_level_tone_setting_supported_erds, 2},
  {0x001b, 2, 0x00000001, "Opal Nugget Ice Maker / Foundation", appliance_api_opal_nugget_ice_maker_v2_foundation_erds, 2},
  {0x001b, 2, 0x00000002, "Opal Nugget Ice Maker / Filter Status", appliance_api_opal_nugget_ice_maker_v2_filter_status_erds, 1},
  {0x001b, 2, 0x00000004, "Opal Nugget Ice Maker / Cleaning Required", appliance_api_opal_nugget_ice_maker_v2_cleaning_required_erds, 1},
  {0x001b, 2, 0x00000008, "Opal Nugget Ice Maker / Filter Reset", appliance_api_opal_nugget_ice_maker_v2_filter_reset_erds, 1},
  {0x001b, 2, 0x00000010, "Opal Nugget Ice Maker / Light Control", appliance_api_opal_nugget_ice_maker_v2_light_control_erds, 1},
  {0x001b, 2, 0x00000020, "Opal Nugget Ice Maker / Report Ice Production", appliance_api_opal_nugget_ice_maker_v2_report_ice_production_erds, 1},
  {0x001b, 2, 0x00000040, "Opal Nugget Ice Maker / Model Configuration", appliance_api_opal_nugget_ice_maker_v2_model_configuration_erds, 1},
  {0x001b, 2, 0x00000080, "Opal Nugget Ice Maker / Ice Maker Push Notifications", appliance_api_opal_nugget_ice_maker_v2_ice_maker_push_notifications_erds, 1},
  {0x001b, 2, 0x00000100, "Opal Nugget Ice Maker / Filter Usage Details", appliance_api_opal_nugget_ice_maker_v2_filter_usage_details_erds, 4},
  {0x001b, 2, 0x00000200, "Opal Nugget Ice Maker / Descale Time Analytics", appliance_api_opal_nugget_ice_maker_v2_descale_time_analytics_erds, 2},
  {0x001b, 2, 0x00000400, "Opal Nugget Ice Maker / Sanitize Elapsed Time", appliance_api_opal_nugget_ice_maker_v2_sanitize_elapsed_time_erds, 1},
  {0x001b, 2, 0x00000800, "Opal Nugget Ice Maker / Total Compressor Runtime", appliance_api_opal_nugget_ice_maker_v2_total_compressor_runtime_erds, 1},
  {0x001b, 2, 0x00001000, "Opal Nugget Ice Maker / Multicolored Light", appliance_api_opal_nugget_ice_maker_v2_multicolored_light_erds, 2},
  {0x001b, 2, 0x00002000, "Opal Nugget Ice Maker / Water Level Tone Setting Supported", appliance_api_opal_nugget_ice_maker_v2_water_level_tone_setting_supported_erds, 2},
  {0x001c, 1, 0x00000001, "In Home Grower / Foundation", appliance_api_in_home_grower_v1_foundation_erds, 18},
  {0x001c, 1, 0x00000002, "In Home Grower / Water Filter", appliance_api_in_home_grower_v1_water_filter_erds, 3},
  {0x001c, 1, 0x00000004, "In Home Grower / Nutrient Tank 1", appliance_api_in_home_grower_v1_nutrient_tank_1_erds, 2},
  {0x001c, 1, 0x00000008, "In Home Grower / Nutrient Tank 2", appliance_api_in_home_grower_v1_nutrient_tank_2_erds, 2},
  {0x001c, 1, 0x00000010, "In Home Grower / Nutrient Tank 3", appliance_api_in_home_grower_v1_nutrient_tank_3_erds, 2},
  {0x001c, 1, 0x00000020, "In Home Grower / Nutrient Tank 4", appliance_api_in_home_grower_v1_nutrient_tank_4_erds, 2},
  {0x001c, 1, 0x00000040, "In Home Grower / Nutrient Tank 5", appliance_api_in_home_grower_v1_nutrient_tank_5_erds, 2},
  {0x001c, 1, 0x00000080, "In Home Grower / Nutrient Tank 6", appliance_api_in_home_grower_v1_nutrient_tank_6_erds, 2},
  {0x001c, 1, 0x00000100, "In Home Grower / Tri chamber system", appliance_api_in_home_grower_v1_tri_chamber_system_erds, 6},
  {0x001c, 1, 0x00000200, "In Home Grower / Display Always On Feature", appliance_api_in_home_grower_v1_display_always_on_feature_erds, 2},
  {0x001c, 1, 0x00000400, "In Home Grower / Air filter", appliance_api_in_home_grower_v1_air_filter_erds, 2},
  {0x001c, 1, 0x00000800, "In Home Grower / Clean Cycle", appliance_api_in_home_grower_v1_clean_cycle_erds, 4},
  {0x001c, 1, 0x00001000, "In Home Grower / Water Intake", appliance_api_in_home_grower_v1_water_intake_erds, 1},
  {0x001c, 1, 0x00002000, "In Home Grower / Debug Packed ERDs 1", appliance_api_in_home_grower_v1_debug_packed_erds_1_erds, 1},
  {0x001c, 1, 0x00004000, "In Home Grower / Debug Packed ERDs 2", appliance_api_in_home_grower_v1_debug_packed_erds_2_erds, 1},
  {0x001c, 1, 0x00008000, "In Home Grower / Debug Packed ERDs 3", appliance_api_in_home_grower_v1_debug_packed_erds_3_erds, 1},
  {0x001c, 1, 0x00010000, "In Home Grower / Debug Packed ERDs 4", appliance_api_in_home_grower_v1_debug_packed_erds_4_erds, 1},
  {0x001d, 1, 0x00000001, "Dehumidifier / Foundation", appliance_api_dehumidifier_v1_foundation_erds, 8},
  {0x001d, 1, 0x00000002, "Dehumidifier / Energy Reporting", appliance_api_dehumidifier_v1_energy_reporting_erds, 5},
  {0x001d, 1, 0x00000004, "Dehumidifier / Pump", appliance_api_dehumidifier_v1_pump_erds, 2},
  {0x001d, 1, 0x00000008, "Dehumidifier / Nonstop Mode", appliance_api_dehumidifier_v1_nonstop_mode_erds, 2},
  {0x001e, 1, 0x00000001, "Under Counter Ice Maker / Foundation", appliance_api_under_counter_ice_maker_v1_foundation_erds, 10},
  {0x001e, 1, 0x00000002, "Under Counter Ice Maker / Cube Model Adjustments", appliance_api_under_counter_ice_maker_v1_cube_model_adjustments_erds, 3},
  {0x001f, 1, 0x00000001, "Through Wall AC / Foundation", appliance_api_through_wall_ac_v1_foundation_erds, 10},
  {0x001f, 1, 0x00000002, "Through Wall AC / Demand Response", appliance_api_through_wall_ac_v1_demand_response_erds, 2},
  {0x001f, 1, 0x00000004, "Through Wall AC / Heat", appliance_api_through_wall_ac_v1_heat_erds, 2},
  {0x001f, 1, 0x00000008, "Through Wall AC / Demand Response Available", appliance_api_through_wall_ac_v1_demand_response_available_erds, 1},
  {0x0021, 1, 0x00000001, "Espresso Maker / Foundation", appliance_api_espresso_maker_v1_foundation_erds, 7},
  {0x0021, 1, 0x00000002, "Espresso Maker / Rinse", appliance_api_espresso_maker_v1_rinse_erds, 2},
  {0x0021, 1, 0x00000004, "Espresso Maker / Standby", appliance_api_espresso_maker_v1_standby_erds, 2},
  {0x0021, 1, 0x00000008, "Espresso Maker / Out of Water Detection", appliance_api_espresso_maker_v1_out_of_water_detection_erds, 1},
  {0x0021, 1, 0x00000010, "Espresso Maker / Emptying", appliance_api_espresso_maker_v1_emptying_erds, 1},
  {0x0021, 1, 0x00000020, "Espresso Maker / Grind Hopper Presence Detection", appliance_api_espresso_maker_v1_grind_hopper_presence_detection_erds, 1},
  {0x0021, 1, 0x00000040, "Espresso Maker / Bean Presence Detection", appliance_api_espresso_maker_v1_bean_presence_detection_erds, 1},
  {0x0021, 1, 0x00000080, "Espresso Maker / Water Hardness", appliance_api_espresso_maker_v1_water_hardness_erds, 2},
  {0x0021, 1, 0x00000100, "Espresso Maker / Door Open or Module Missing", appliance_api_espresso_maker_v1_door_open_or_module_missing_erds, 1},
  {0x0021, 1, 0x00000200, "Espresso Maker / Descale", appliance_api_espresso_maker_v1_descale_erds, 3},
  {0x0021, 1, 0x00000400, "Espresso Maker / Steam Knob Position", appliance_api_espresso_maker_v1_steam_knob_position_erds, 1},
  {0x0021, 1, 0x00000800, "Espresso Maker / Empty Waste Indicator", appliance_api_espresso_maker_v1_empty_waste_indicator_erds, 1},
  {0x0021, 1, 0x00001000, "Espresso Maker / Overpressure Detected", appliance_api_espresso_maker_v1_overpressure_detected_erds, 1},
  {0x0021, 1, 0x00002000, "Espresso Maker / Espresso Supported", appliance_api_espresso_maker_v1_espresso_supported_erds, 2},
  {0x0021, 1, 0x00004000, "Espresso Maker / Americano Supported", appliance_api_espresso_maker_v1_americano_supported_erds, 2},
  {0x0021, 1, 0x00008000, "Espresso Maker / Lungo Supported", appliance_api_espresso_maker_v1_lungo_supported_erds, 2},
  {0x0021, 1, 0x00010000, "Espresso Maker / Hot Water Supported", appliance_api_espresso_maker_v1_hot_water_supported_erds, 2},
  {0x0021, 1, 0x00020000, "Espresso Maker / Steam Supported", appliance_api_espresso_maker_v1_steam_supported_erds, 4},
  {0x0021, 1, 0x00040000, "Espresso Maker / Ristretto Supported", appliance_api_espresso_maker_v1_ristretto_supported_erds, 2},
  {0x0021, 1, 0x00080000, "Espresso Maker / Doppio Supported", appliance_api_espresso_maker_v1_doppio_supported_erds, 2},
  {0x0021, 1, 0x00100000, "Espresso Maker / Triple Shot Supported", appliance_api_espresso_maker_v1_triple_shot_supported_erds, 2},
  {0x0021, 1, 0x00200000, "Espresso Maker / Red Eye Supported", appliance_api_espresso_maker_v1_red_eye_supported_erds, 2},
  {0x0021, 1, 0x00400000, "Espresso Maker / Grinder Status Supported", appliance_api_espresso_maker_v1_grinder_status_supported_erds, 2},
  {0x0021, 1, 0x00800000, "Espresso Maker / MyBrew MyCup Supported", appliance_api_espresso_maker_v1_mybrew_mycup_supported_erds, 2},
  {0x0021, 1, 0x01000000, "Espresso Maker / Steam Without Time Supported", appliance_api_espresso_maker_v1_steam_without_time_supported_erds, 2},
  {0x0022, 1, 0x00000001, "Toaster Oven / Foundation", appliance_api_toaster_oven_v1_foundation_erds, 14},
  {0x0022, 1, 0x00000002, "Toaster Oven / Cavity Light Supported", appliance_api_toaster_oven_v1_cavity_light_supported_erds, 2},
  {0x0022, 1, 0x00000004, "Toaster Oven / LCD Brightness Supported", appliance_api_toaster_oven_v1_lcd_brightness_supported_erds, 2},
  {0x0022, 1, 0x00000008, "Toaster Oven / Door Alarm Supported", appliance_api_toaster_oven_v1_door_alarm_supported_erds, 3},
  {0x0022, 1, 0x00000010, "Toaster Oven / Air Fry Supported", appliance_api_toaster_oven_v1_air_fry_supported_erds, 2},
  {0x0022, 1, 0x00000020, "Toaster Oven / Bake Supported", appliance_api_toaster_oven_v1_bake_supported_erds, 2},
  {0x0022, 1, 0x00000040, "Toaster Oven / Broil Supported", appliance_api_toaster_oven_v1_broil_supported_erds, 2},
  {0x0022, 1, 0x00000080, "Toaster Oven / Roast Supported", appliance_api_toaster_oven_v1_roast_supported_erds, 2},
  {0x0022, 1, 0x00000100, "Toaster Oven / Reheat Supported", appliance_api_toaster_oven_v1_reheat_supported_erds, 2},
  {0x0022, 1, 0x00000200, "Toaster Oven / Warm Supported", appliance_api_toaster_oven_v1_warm_supported_erds, 2},
  {0x0022, 1, 0x00000400, "Toaster Oven / Slow Cook Supported", appliance_api_toaster_oven_v1_slow_cook_supported_erds, 2},
  {0x0022, 1, 0x00000800, "Toaster Oven / Dehydrate Supported", appliance_api_toaster_oven_v1_dehydrate_supported_erds, 2},
  {0x0022, 1, 0x00001000, "Toaster Oven / Proof Supported", appliance_api_toaster_oven_v1_proof_supported_erds, 2},
  {0x0022, 1, 0x00002000, "Toaster Oven / Cookie Supported", appliance_api_toaster_oven_v1_cookie_supported_erds, 2},
  {0x0022, 1, 0x00004000, "Toaster Oven / Pizza Supported", appliance_api_toaster_oven_v1_pizza_supported_erds, 2},
  {0x0022, 1, 0x00008000, "Toaster Oven / Bagel Supported", appliance_api_toaster_oven_v1_bagel_supported_erds, 2},
  {0x0022, 1, 0x00010000, "Toaster Oven / Toast Supported", appliance_api_toaster_oven_v1_toast_supported_erds, 2},
  {0x0022, 1, 0x00020000, "Toaster Oven / Crisp Finish Supported", appliance_api_toaster_oven_v1_crisp_finish_supported_erds, 2},
  {0x0022, 1, 0x00040000, "Toaster Oven / Cake Supported", appliance_api_toaster_oven_v1_cake_supported_erds, 2},
  {0x0022, 1, 0x00080000, "Toaster Oven / Cookie with Preferences Supported", appliance_api_toaster_oven_v1_cookie_with_preferences_supported_erds, 2},
  {0x0022, 1, 0x00100000, "Toaster Oven / Pizza with Preferences Supported", appliance_api_toaster_oven_v1_pizza_with_preferences_supported_erds, 2},
  {0x0022, 1, 0x00200000, "Toaster Oven / Cooking Push Notifications", appliance_api_toaster_oven_v1_cooking_push_notifications_erds, 1},
  {0x0022, 1, 0x00400000, "Toaster Oven / Cooktime Modification", appliance_api_toaster_oven_v1_cooktime_modification_erds, 2},
  {0x0022, 1, 0x00800000, "Toaster Oven / Model Configuration", appliance_api_toaster_oven_v1_model_configuration_erds, 1},
  {0x0022, 2, 0x00000001, "Toaster Oven / Foundation", appliance_api_toaster_oven_v2_foundation_erds, 6},
  {0x0022, 2, 0x00000002, "Toaster Oven / Cavity Light Supported", appliance_api_toaster_oven_v2_cavity_light_supported_erds, 2},
  {0x0022, 2, 0x00000004, "Toaster Oven / LCD Brightness Supported", appliance_api_toaster_oven_v2_lcd_brightness_supported_erds, 2},
  {0x0022, 2, 0x00000008, "Toaster Oven / Door Alarm Supported", appliance_api_toaster_oven_v2_door_alarm_supported_erds, 3},
  {0x0022, 2, 0x00000010, "Toaster Oven / Air Fry Supported", appliance_api_toaster_oven_v2_air_fry_supported_erds, 2},
  {0x0022, 2, 0x00000020, "Toaster Oven / Bake Supported", appliance_api_toaster_oven_v2_bake_supported_erds, 2},
  {0x0022, 2, 0x00000040, "Toaster Oven / Broil Supported", appliance_api_toaster_oven_v2_broil_supported_erds, 2},
  {0x0022, 2, 0x00000080, "Toaster Oven / Roast Supported", appliance_api_toaster_oven_v2_roast_supported_erds, 2},
  {0x0022, 2, 0x00000100, "Toaster Oven / Reheat Supported", appliance_api_toaster_oven_v2_reheat_supported_erds, 2},
  {0x0022, 2, 0x00000200, "Toaster Oven / Warm Supported", appliance_api_toaster_oven_v2_warm_supported_erds, 2},
  {0x0022, 2, 0x00000400, "Toaster Oven / Slow Cook Supported", appliance_api_toaster_oven_v2_slow_cook_supported_erds, 2},
  {0x0022, 2, 0x00000800, "Toaster Oven / Dehydrate Supported", appliance_api_toaster_oven_v2_dehydrate_supported_erds, 2},
  {0x0022, 2, 0x00001000, "Toaster Oven / Proof Supported", appliance_api_toaster_oven_v2_proof_supported_erds, 2},
  {0x0022, 2, 0x00002000, "Toaster Oven / Cookie Supported", appliance_api_toaster_oven_v2_cookie_supported_erds, 2},
  {0x0022, 2, 0x00004000, "Toaster Oven / Pizza Supported", appliance_api_toaster_oven_v2_pizza_supported_erds, 2},
  {0x0022, 2, 0x00008000, "Toaster Oven / Bagel Supported", appliance_api_toaster_oven_v2_bagel_supported_erds, 2},
  {0x0022, 2, 0x00010000, "Toaster Oven / Toast Supported", appliance_api_toaster_oven_v2_toast_supported_erds, 2},
  {0x0022, 2, 0x00020000, "Toaster Oven / Crisp Finish Supported", appliance_api_toaster_oven_v2_crisp_finish_supported_erds, 2},
  {0x0022, 2, 0x00040000, "Toaster Oven / Cake Supported", appliance_api_toaster_oven_v2_cake_supported_erds, 2},
  {0x0022, 2, 0x00080000, "Toaster Oven / Cookie with Preferences Supported", appliance_api_toaster_oven_v2_cookie_with_preferences_supported_erds, 2},
  {0x0022, 2, 0x00100000, "Toaster Oven / Pizza with Preferences Supported", appliance_api_toaster_oven_v2_pizza_with_preferences_supported_erds, 2},
  {0x0022, 2, 0x00200000, "Toaster Oven / Cooking Push Notifications", appliance_api_toaster_oven_v2_cooking_push_notifications_erds, 1},
  {0x0022, 2, 0x00400000, "Toaster Oven / Cooktime Modification", appliance_api_toaster_oven_v2_cooktime_modification_erds, 2},
  {0x0022, 2, 0x00800000, "Toaster Oven / Model Configuration", appliance_api_toaster_oven_v2_model_configuration_erds, 1},
  {0x0022, 2, 0x01000000, "Toaster Oven / Preheat Control", appliance_api_toaster_oven_v2_preheat_control_erds, 2},
  {0x0022, 2, 0x02000000, "Toaster Oven / Convection Fan Control", appliance_api_toaster_oven_v2_convection_fan_control_erds, 2},
  {0x0022, 2, 0x04000000, "Toaster Oven / Reset User Cooking Parameters to Default", appliance_api_toaster_oven_v2_reset_user_cooking_parameters_to_default_erds, 1},
  {0x0022, 2, 0x08000000, "Toaster Oven / Remote Start", appliance_api_toaster_oven_v2_remote_start_erds, 2},
  {0x0022, 2, 0x10000000, "Toaster Oven / Convection Bake Supported", appliance_api_toaster_oven_v2_convection_bake_supported_erds, 1},
  {0x0023, 1, 0x00000001, "Zoneline Vertical / Foundation", appliance_api_zoneline_vertical_v1_foundation_erds, 48},
  {0x0023, 2, 0x00000001, "Zoneline Vertical / Heat Pump", appliance_api_zoneline_vertical_v2_heat_pump_erds, 4},
  {0x0023, 2, 0x00000002, "Zoneline Vertical / Make Up Air Module", appliance_api_zoneline_vertical_v2_make_up_air_module_erds, 4},
  {0x0023, 2, 0x00000004, "Zoneline Vertical / Inverter Module", appliance_api_zoneline_vertical_v2_inverter_module_erds, 20},
  {0x0024, 1, 0x00000001, "Central DFS Controller / Foundation", appliance_api_central_dfs_controller_v1_foundation_erds, 2},
  {0x0024, 1, 0x00000002, "Central DFS Controller / Indoor Temperature Sensing", appliance_api_central_dfs_controller_v1_indoor_temperature_sensing_erds, 2},
  {0x0024, 1, 0x00000004, "Central DFS Controller / Basic Temperature Control", appliance_api_central_dfs_controller_v1_basic_temperature_control_erds, 1},
  {0x0024, 1, 0x00000008, "Central DFS Controller / Indoor Humidity Sensing", appliance_api_central_dfs_controller_v1_indoor_humidity_sensing_erds, 1},
  {0x0024, 1, 0x00000010, "Central DFS Controller / DIP SW Equipped", appliance_api_central_dfs_controller_v1_dip_sw_equipped_erds, 1},
  {0x0024, 1, 0x00000020, "Central DFS Controller / Duct Free Split Control", appliance_api_central_dfs_controller_v1_duct_free_split_control_erds, 1},
  {0x0024, 1, 0x00000040, "Central DFS Controller / Unitary Heating", appliance_api_central_dfs_controller_v1_unitary_heating_erds, 1},
  {0x0024, 1, 0x00000080, "Central DFS Controller / Unitary Cooling", appliance_api_central_dfs_controller_v1_unitary_cooling_erds, 1},
  {0x0024, 1, 0x00000100, "Central DFS Controller / Unitary Auto Mode", appliance_api_central_dfs_controller_v1_unitary_auto_mode_erds, 3},
  {0x0024, 1, 0x00000200, "Central DFS Controller / Unitary Air Filtration", appliance_api_central_dfs_controller_v1_unitary_air_filtration_erds, 3},
  {0x0024, 1, 0x00000400, "Central DFS Controller / Unitary Fan Control", appliance_api_central_dfs_controller_v1_unitary_fan_control_erds, 3},
  {0x0024, 1, 0x00000800, "Central DFS Controller / Unitary Mode Control", appliance_api_central_dfs_controller_v1_unitary_mode_control_erds, 3},
  {0x0024, 1, 0x00001000, "Central DFS Controller / Local Scheduling", appliance_api_central_dfs_controller_v1_local_scheduling_erds, 2},
  {0x0024, 1, 0x00002000, "Central DFS Controller / Unitary System Service Config", appliance_api_central_dfs_controller_v1_unitary_system_service_config_erds, 9},
  {0x0024, 1, 0x00004000, "Central DFS Controller / Indoor Temperature Service Config", appliance_api_central_dfs_controller_v1_indoor_temperature_service_config_erds, 2},
  {0x0024, 1, 0x00008000, "Central DFS Controller / Unitary Compressor Service Config", appliance_api_central_dfs_controller_v1_unitary_compressor_service_config_erds, 12},
  {0x0024, 1, 0x00010000, "Central DFS Controller / Unitary Fan Service Config", appliance_api_central_dfs_controller_v1_unitary_fan_service_config_erds, 6},
  {0x0026, 1, 0x00000001, "Stand Mixer / Foundation", appliance_api_stand_mixer_v1_foundation_erds, 7},
  {0x0026, 1, 0x00000002, "Stand Mixer / Remote Cancel Mixing Cycle", appliance_api_stand_mixer_v1_remote_cancel_mixing_cycle_erds, 1},
  {0x0026, 1, 0x00000004, "Stand Mixer / Remote Pause Mixing Cycle", appliance_api_stand_mixer_v1_remote_pause_mixing_cycle_erds, 1},
  {0x0026, 1, 0x00000008, "Stand Mixer / Mixing Cycle Absolute Time Modifier", appliance_api_stand_mixer_v1_mixing_cycle_absolute_time_modifier_erds, 1},
  {0x0026, 1, 0x00000010, "Stand Mixer / Diagnostics and Prognostics", appliance_api_stand_mixer_v1_diagnostics_and_prognostics_erds, 3},
  {0x0026, 1, 0x00000020, "Stand Mixer / Builtin Mixer Scale", appliance_api_stand_mixer_v1_builtin_mixer_scale_erds, 7},
  {0x0026, 1, 0x00000040, "Stand Mixer / Recipes", appliance_api_stand_mixer_v1_recipes_erds, 1},
  {0x0026, 1, 0x00000080, "Stand Mixer / Support Equipment", appliance_api_stand_mixer_v1_support_equipment_erds, 1},
  {0x0026, 1, 0x00000100, "Stand Mixer / Model Configuration", appliance_api_stand_mixer_v1_model_configuration_erds, 1},
  {0x0026, 1, 0x00000200, "Stand Mixer / Display Brightness", appliance_api_stand_mixer_v1_display_brightness_erds, 2},
  {0x0026, 1, 0x00000400, "Stand Mixer / Sound Settings", appliance_api_stand_mixer_v1_sound_settings_erds, 1},
  {0x0026, 2, 0x00000001, "Stand Mixer / Foundation", appliance_api_stand_mixer_v2_foundation_erds, 7},
  {0x0026, 2, 0x00000002, "Stand Mixer / Precision Speed", nullptr, 0},
  {0x0026, 2, 0x00000004, "Stand Mixer / Active Stir", nullptr, 0},
  {0x0026, 2, 0x00000008, "Stand Mixer / Auto Sense Additional Parameters", nullptr, 0},
  {0x002b, 1, 0x00000001, "Smoker / Foundation", appliance_api_smoker_v1_foundation_erds, 5},
  {0x002b, 1, 0x00000002, "Smoker / Smoke", appliance_api_smoker_v1_smoke_erds, 3},
  {0x002b, 1, 0x00000004, "Smoker / Pause Smoke", appliance_api_smoker_v1_pause_smoke_erds, 2},
  {0x002b, 1, 0x00000008, "Smoker / Cook Time", appliance_api_smoker_v1_cook_time_erds, 3},
  {0x002b, 1, 0x00000010, "Smoker / Elapsed Cooking Time", appliance_api_smoker_v1_elapsed_cooking_time_erds, 1},
  {0x002b, 1, 0x00000020, "Smoker / Probe", appliance_api_smoker_v1_probe_erds, 3},
  {0x002b, 1, 0x00000040, "Smoker / Door Alarm", appliance_api_smoker_v1_door_alarm_erds, 3},
  {0x002b, 1, 0x00000080, "Smoker / Light", appliance_api_smoker_v1_light_erds, 2},
  {0x002b, 1, 0x00000100, "Smoker / Warm Limits", appliance_api_smoker_v1_warm_limits_erds, 2},
  {0x002b, 1, 0x00000200, "Smoker / Auto Warm Setting", appliance_api_smoker_v1_auto_warm_setting_erds, 8},
  {0x002b, 1, 0x00000400, "Smoker / Smoker Type", appliance_api_smoker_v1_smoker_type_erds, 1},
  {0x002b, 1, 0x00000800, "Smoker / UI Brightness", appliance_api_smoker_v1_ui_brightness_erds, 2},
  {0x002b, 1, 0x00001000, "Smoker / Probe Temperature Based Early Completion Notification", appliance_api_smoker_v1_probe_temperature_based_early_completion_notification_erds, 3},
  {0x002b, 1, 0x00002000, "Smoker / Cook Time Based Early Completion Notification", appliance_api_smoker_v1_cook_time_based_early_completion_notification_erds, 3},
  {0x002b, 1, 0x00004000, "Smoker / Igniter Temperature Control", appliance_api_smoker_v1_igniter_temperature_control_erds, 3},
  {0x002b, 1, 0x00008000, "Smoker / Cook Mode Parameter Reset", appliance_api_smoker_v1_cook_mode_parameter_reset_erds, 1},
  {0x002b, 1, 0x00010000, "Smoker / Restore Factory Defaults", appliance_api_smoker_v1_restore_factory_defaults_erds, 1},
  {0x002b, 1, 0x00020000, "Smoker / Custom Mode", appliance_api_smoker_v1_custom_mode_erds, 2},
  {0x002b, 1, 0x00040000, "Smoker / Keep Warm Mode", appliance_api_smoker_v1_keep_warm_mode_erds, 2},
  {0x002b, 1, 0x00080000, "Smoker / Brisket Mode", appliance_api_smoker_v1_brisket_mode_erds, 2},
  {0x002b, 1, 0x00100000, "Smoker / Pork Rib Mode", appliance_api_smoker_v1_pork_rib_mode_erds, 2},
  {0x002b, 1, 0x00200000, "Smoker / Pork Butt Mode", appliance_api_smoker_v1_pork_butt_mode_erds, 2},
  {0x002b, 1, 0x00400000, "Smoker / Wings Mode", appliance_api_smoker_v1_wings_mode_erds, 2},
  {0x002b, 1, 0x00800000, "Smoker / Chicken Mode", appliance_api_smoker_v1_chicken_mode_erds, 2},
  {0x002b, 1, 0x01000000, "Smoker / Salmon Mode", appliance_api_smoker_v1_salmon_mode_erds, 2},
  {0x002b, 1, 0x02000000, "Smoker / Smoker Push Notifications", appliance_api_smoker_v1_smoker_push_notifications_erds, 1},
  {0x002b, 1, 0x04000000, "Smoker / Remote Configure", appliance_api_smoker_v1_remote_configure_erds, 1},
  {0x002b, 1, 0x08000000, "Smoker / Sound Settings", appliance_api_smoker_v1_sound_settings_erds, 1},
  {0x002b, 1, 0x10000000, "Smoker / Elapsed Warming Time", appliance_api_smoker_v1_elapsed_warming_time_erds, 1},
  {0x002b, 1, 0x20000000, "Smoker / Preheat Progress", appliance_api_smoker_v1_preheat_progress_erds, 1},
  {0x002c, 1, 0x00000001, "Air Handler VRF / Foundation", nullptr, 0},
  {0x002d, 1, 0x00000001, "Fabric Care Cabinet or Closet / Foundation", appliance_api_fabric_care_cabinet_or_closet_v1_foundation_erds, 9},
  {0x002d, 1, 0x00000002, "Fabric Care Cabinet or Closet / Remote Start and Stop", appliance_api_fabric_care_cabinet_or_closet_v1_remote_start_and_stop_erds, 3},
  {0x002d, 1, 0x00000004, "Fabric Care Cabinet or Closet / Remote Cycle Selection", appliance_api_fabric_care_cabinet_or_closet_v1_remote_cycle_selection_erds, 2},
  {0x002d, 1, 0x00000008, "Fabric Care Cabinet or Closet / Downloaded Cycle", appliance_api_fabric_care_cabinet_or_closet_v1_downloaded_cycle_erds, 4},
  {0x002d, 1, 0x00000010, "Fabric Care Cabinet or Closet / Cycle Function", appliance_api_fabric_care_cabinet_or_closet_v1_cycle_function_erds, 2},
  {0x002d, 1, 0x00000020, "Fabric Care Cabinet or Closet / Dry Time Option", appliance_api_fabric_care_cabinet_or_closet_v1_dry_time_option_erds, 2},
  {0x002d, 1, 0x00000040, "Fabric Care Cabinet or Closet / Water Tank Notifications and Warnings", appliance_api_fabric_care_cabinet_or_closet_v1_water_tank_notifications_and_warnings_erds, 1},
  {0x002d, 1, 0x00000080, "Fabric Care Cabinet or Closet / Item Count Option", appliance_api_fabric_care_cabinet_or_closet_v1_item_count_option_erds, 2},
  {0x002d, 1, 0x00000200, "Fabric Care Cabinet or Closet / Cabinet or Closet Fault Notifications", appliance_api_fabric_care_cabinet_or_closet_v1_cabinet_or_closet_fault_notifications_erds, 1},
  {0x002d, 1, 0x00000400, "Fabric Care Cabinet or Closet / Control Lock Option", appliance_api_fabric_care_cabinet_or_closet_v1_control_lock_option_erds, 3},
  {0x002d, 2, 0x00000001, "Fabric Care Cabinet or Closet / Foundation", appliance_api_fabric_care_cabinet_or_closet_v2_foundation_erds, 7},
  {0x002d, 2, 0x00000002, "Fabric Care Cabinet or Closet / Cycle Function", appliance_api_fabric_care_cabinet_or_closet_v2_cycle_function_erds, 1},
  {0x002d, 2, 0x00000004, "Fabric Care Cabinet or Closet / Push Notifications", appliance_api_fabric_care_cabinet_or_closet_v2_push_notifications_erds, 1},
  {0x002e, 1, 0x00000001, "Laundry Center / Washer Foundation", appliance_api_laundry_center_v1_washer_foundation_erds, 7},
  {0x002e, 1, 0x00000002, "Laundry Center / Dryer Foundation", appliance_api_laundry_center_v1_dryer_foundation_erds, 7},
  {0x002e, 1, 0x00000004, "Laundry Center / Washer Remote Start and Stop", appliance_api_laundry_center_v1_washer_remote_start_and_stop_erds, 3},
  {0x002e, 1, 0x00000008, "Laundry Center / Dryer Remote Start and Stop", appliance_api_laundry_center_v1_dryer_remote_start_and_stop_erds, 3},
  {0x002e, 1, 0x00000010, "Laundry Center / Washer Remote Cycle Selection", appliance_api_laundry_center_v1_washer_remote_cycle_selection_erds, 1},
  {0x002e, 1, 0x00000020, "Laundry Center / Dryer Remote Cycle Selection", appliance_api_laundry_center_v1_dryer_remote_cycle_selection_erds, 1},
  {0x002e, 1, 0x00000040, "Laundry Center / Laundry Center Push Notifications", appliance_api_laundry_center_v1_laundry_center_push_notifications_erds, 1},
  {0x002e, 1, 0x00000080, "Laundry Center / Smart Combi Option", appliance_api_laundry_center_v1_smart_combi_option_erds, 2},
  {0x002e, 1, 0x00000100, "Laundry Center / Laundry Center Function", appliance_api_laundry_center_v1_laundry_center_function_erds, 2},
  {0x002e, 1, 0x00000200, "Laundry Center / Dry Time Option", appliance_api_laundry_center_v1_dry_time_option_erds, 2},
  {0x002e, 1, 0x00000400, "Laundry Center / Dry Level Option", appliance_api_laundry_center_v1_dry_level_option_erds, 2},
  {0x002e, 1, 0x00000800, "Laundry Center / Washer Eco Option", appliance_api_laundry_center_v1_washer_eco_option_erds, 2},
  {0x002e, 1, 0x00001000, "Laundry Center / Washer UV Sanitize Option", appliance_api_laundry_center_v1_washer_uv_sanitize_option_erds, 2},
  {0x002e, 1, 0x00002000, "Laundry Center / Water Temperature Option", appliance_api_laundry_center_v1_water_temperature_option_erds, 2},
  {0x0037, 1, 0x00000001, "Sourdough Starter / Foundation", appliance_api_sourdough_starter_v1_foundation_erds, 1},
  {0x0037, 1, 0x00000002, "Sourdough Starter / Ambient Temperature Supported", appliance_api_sourdough_starter_v1_ambient_temperature_supported_erds, 1},
  {0x0037, 1, 0x00000004, "Sourdough Starter / Ambient Temperature Offset Supported", appliance_api_sourdough_starter_v1_ambient_temperature_offset_supported_erds, 3},
  {0x0037, 1, 0x00000008, "Sourdough Starter / UI Brightness Supported", appliance_api_sourdough_starter_v1_ui_brightness_supported_erds, 2},
  {0x0037, 1, 0x00000010, "Sourdough Starter / Sourdough Starter Notifications Supported", appliance_api_sourdough_starter_v1_sourdough_starter_notifications_supported_erds, 1},
  {0x0037, 1, 0x00000020, "Sourdough Starter / Time Since Complete Supported", appliance_api_sourdough_starter_v1_time_since_complete_supported_erds, 1},
  {0x0037, 1, 0x00000040, "Sourdough Starter / Time To Crock Full Supported", appliance_api_sourdough_starter_v1_time_to_crock_full_supported_erds, 1},
  {0x0037, 1, 0x00000080, "Sourdough Starter / Time Until Next Feed Supported", appliance_api_sourdough_starter_v1_time_until_next_feed_supported_erds, 1},
  {0x0037, 1, 0x00000100, "Sourdough Starter / Crock Detection Supported", appliance_api_sourdough_starter_v1_crock_detection_supported_erds, 1},
  {0x0038, 1, 0x00000002, "Thermostat / Ambient Temperature Supported", appliance_api_thermostat_v1_ambient_temperature_supported_erds, 1},
  {0x0103, 1, 0x00000001, "FPA Refrigerator / Foundation", appliance_api_fpa_refrigerator_v1_foundation_erds, 7},
  {0x0103, 1, 0x00000002, "FPA Refrigerator / Foundation 2", appliance_api_fpa_refrigerator_v1_foundation_2_erds, 21},
  {0x0103, 1, 0x00000004, "FPA Refrigerator / Food Modes Control Upper Compartment", appliance_api_fpa_refrigerator_v1_food_modes_control_upper_compartment_erds, 3},
  {0x0103, 1, 0x00000008, "FPA Refrigerator / Key Status", appliance_api_fpa_refrigerator_v1_key_status_erds, 1},
  {0x0103, 1, 0x00000010, "FPA Refrigerator / Max Cool Control", appliance_api_fpa_refrigerator_v1_max_cool_control_erds, 3},
  {0x0103, 1, 0x00000020, "FPA Refrigerator / Light Control", appliance_api_fpa_refrigerator_v1_light_control_erds, 8},
  {0x0103, 1, 0x00000040, "FPA Refrigerator / Food Modes Control Lower Compartment", appliance_api_fpa_refrigerator_v1_food_modes_control_lower_compartment_erds, 3},
  {0x0103, 1, 0x00000080, "FPA Refrigerator / Food Modes Control Middle Compartment", appliance_api_fpa_refrigerator_v1_food_modes_control_middle_compartment_erds, 3},
  {0x0103, 1, 0x00000100, "FPA Refrigerator / Turbo Freeze", appliance_api_fpa_refrigerator_v1_turbo_freeze_erds, 1},
  {0x0103, 1, 0x00000200, "FPA Refrigerator / Food Mode Preset Control", appliance_api_fpa_refrigerator_v1_food_mode_preset_control_erds, 3},
  {0x0103, 1, 0x00000400, "FPA Refrigerator / Air Filter Control", appliance_api_fpa_refrigerator_v1_air_filter_control_erds, 2},
  {0x0103, 1, 0x00000800, "FPA Refrigerator / Door Information", appliance_api_fpa_refrigerator_v1_door_information_erds, 2},
  {0x0103, 1, 0x00001000, "FPA Refrigerator / Light Mode Control Upper Compartment", appliance_api_fpa_refrigerator_v1_light_mode_control_upper_compartment_erds, 3},
  {0x0103, 1, 0x00002000, "FPA Refrigerator / Light Mode Control Lower Compartment", appliance_api_fpa_refrigerator_v1_light_mode_control_lower_compartment_erds, 3},
  {0x0103, 1, 0x00004000, "FPA Refrigerator / Light Mode Control Middle Compartment", appliance_api_fpa_refrigerator_v1_light_mode_control_middle_compartment_erds, 3},
  {0x0103, 1, 0x00020000, "FPA Refrigerator / Eco Mode Control", appliance_api_fpa_refrigerator_v1_eco_mode_control_erds, 2},
  {0x0103, 1, 0x00040000, "FPA Refrigerator / V2 Refrigeration Notifications", appliance_api_fpa_refrigerator_v1_v2_refrigeration_notifications_erds, 1},
  {0x0103, 1, 0x00080000, "FPA Refrigerator / Ambient Temperature and Humidity", appliance_api_fpa_refrigerator_v1_ambient_temperature_and_humidity_erds, 2},
  {0x0106, 1, 0x00000001, "FPA DishDrawer / Foundation", appliance_api_fpa_dishdrawer_v1_foundation_erds, 1},
  {0x0106, 1, 0x00000002, "FPA DishDrawer / Tub 0", appliance_api_fpa_dishdrawer_v1_tub_0_erds, 15},
  {0x0106, 1, 0x00000004, "FPA DishDrawer / Tub 1", appliance_api_fpa_dishdrawer_v1_tub_1_erds, 15},
  {0x0106, 1, 0x00000008, "FPA DishDrawer / Tub 0 Turbidity", appliance_api_fpa_dishdrawer_v1_tub_0_turbidity_erds, 2},
  {0x0106, 1, 0x00000010, "FPA DishDrawer / Tub 1 Turbidity", appliance_api_fpa_dishdrawer_v1_tub_1_turbidity_erds, 2},
  {0x0106, 1, 0x00000020, "FPA DishDrawer / Tub 0 End of Cycle", appliance_api_fpa_dishdrawer_v1_tub_0_end_of_cycle_erds, 1},
  {0x0106, 1, 0x00000040, "FPA DishDrawer / Tub 1 End of Cycle", appliance_api_fpa_dishdrawer_v1_tub_1_end_of_cycle_erds, 1},
  {0x0106, 1, 0x00000080, "FPA DishDrawer / Tub 0 Foundation", appliance_api_fpa_dishdrawer_v1_tub_0_foundation_erds, 6},
  {0x0106, 1, 0x00000100, "FPA DishDrawer / Tub 1 Foundation", appliance_api_fpa_dishdrawer_v1_tub_1_foundation_erds, 5},
  {0x0106, 1, 0x00000200, "FPA DishDrawer / Tub 0 Configuration", appliance_api_fpa_dishdrawer_v1_tub_0_configuration_erds, 11},
  {0x0106, 1, 0x00000400, "FPA DishDrawer / Tub 1 Configuration", appliance_api_fpa_dishdrawer_v1_tub_1_configuration_erds, 11},
  {0x0106, 1, 0x00000800, "FPA DishDrawer / Tub 0 Service", appliance_api_fpa_dishdrawer_v1_tub_0_service_erds, 12},
  {0x0106, 1, 0x00001000, "FPA DishDrawer / Tub 1 Service", appliance_api_fpa_dishdrawer_v1_tub_1_service_erds, 11},
  {0x0106, 1, 0x00002000, "FPA DishDrawer / Tub 0 Cycle Selection", appliance_api_fpa_dishdrawer_v1_tub_0_cycle_selection_erds, 1},
  {0x0106, 1, 0x00004000, "FPA DishDrawer / Tub 1 Cycle Selection", appliance_api_fpa_dishdrawer_v1_tub_1_cycle_selection_erds, 1},
  {0x0106, 1, 0x00008000, "FPA DishDrawer / Tub 0 Wash Temp Selection", appliance_api_fpa_dishdrawer_v1_tub_0_wash_temp_selection_erds, 1},
  {0x0106, 1, 0x00010000, "FPA DishDrawer / Tub 1 Wash Temp Selection", appliance_api_fpa_dishdrawer_v1_tub_1_wash_temp_selection_erds, 1},
  {0x0106, 1, 0x00020000, "FPA DishDrawer / Tub 0 Dry Temp Selection", appliance_api_fpa_dishdrawer_v1_tub_0_dry_temp_selection_erds, 1},
  {0x0106, 1, 0x00040000, "FPA DishDrawer / Tub 1 Dry Temp Selection", appliance_api_fpa_dishdrawer_v1_tub_1_dry_temp_selection_erds, 1},
  {0x0106, 1, 0x00080000, "FPA DishDrawer / Tub 0 Start Delay Selection", appliance_api_fpa_dishdrawer_v1_tub_0_start_delay_selection_erds, 2},
  {0x0106, 1, 0x00100000, "FPA DishDrawer / Tub 1 Start Delay Selection", appliance_api_fpa_dishdrawer_v1_tub_1_start_delay_selection_erds, 2},
  {0x0106, 1, 0x00200000, "FPA DishDrawer / Tub 0 Remote Cycle Control", appliance_api_fpa_dishdrawer_v1_tub_0_remote_cycle_control_erds, 2},
  {0x0106, 1, 0x00400000, "FPA DishDrawer / Tub 1 Remote Cycle Control", appliance_api_fpa_dishdrawer_v1_tub_1_remote_cycle_control_erds, 2},
  {0x0106, 1, 0x00800000, "FPA DishDrawer / Smart Assist", appliance_api_fpa_dishdrawer_v1_smart_assist_erds, 2},
  {0x0106, 1, 0x01000000, "FPA DishDrawer / Preference Options", appliance_api_fpa_dishdrawer_v1_preference_options_erds, 2},
  {0x0106, 1, 0x02000000, "FPA DishDrawer / Water Softener Setting", appliance_api_fpa_dishdrawer_v1_water_softener_setting_erds, 2},
  {0x0106, 1, 0x04000000, "FPA DishDrawer / Auto Lid Down Setting", appliance_api_fpa_dishdrawer_v1_auto_lid_down_setting_erds, 2},
  {0x0106, 1, 0x08000000, "FPA DishDrawer / Default to Eco Cycle Setting", appliance_api_fpa_dishdrawer_v1_default_to_eco_cycle_setting_erds, 2},
  {0x0106, 1, 0x10000000, "FPA DishDrawer / Custom Cycle", appliance_api_fpa_dishdrawer_v1_custom_cycle_erds, 2},
  {0x0106, 1, 0x20000000, "FPA DishDrawer / Tub 0 Cycle Modifiers", appliance_api_fpa_dishdrawer_v1_tub_0_cycle_modifiers_erds, 2},
  {0x0106, 1, 0x40000000, "FPA DishDrawer / Tub 1 Cycle Modifiers", appliance_api_fpa_dishdrawer_v1_tub_1_cycle_modifiers_erds, 2},
  {0x0107, 1, 0x00000001, "FPA Range / Foundation", appliance_api_fpa_range_v1_foundation_erds, 33},
  {0x0107, 1, 0x00000002, "FPA Range / Upper Automatic Door Open", appliance_api_fpa_range_v1_upper_automatic_door_open_erds, 3},
  {0x0107, 1, 0x00000004, "FPA Range / Lower Automatic Door Open", appliance_api_fpa_range_v1_lower_automatic_door_open_erds, 3},
  {0x0107, 1, 0x00000008, "FPA Range / Automatic Door Opener Local Public Enable", appliance_api_fpa_range_v1_automatic_door_opener_local_public_enable_erds, 1},
  {0x0107, 1, 0x00000010, "FPA Range / Upper Extended Available Cook Modes", appliance_api_fpa_range_v1_upper_extended_available_cook_modes_erds, 1},
  {0x0107, 1, 0x00000020, "FPA Range / Lower Extended Available Cook Modes", appliance_api_fpa_range_v1_lower_extended_available_cook_modes_erds, 1},
  {0x0108, 1, 0x00000001, "FPA Cooktop / Foundation", appliance_api_fpa_cooktop_v1_foundation_erds, 8},
  {0x0108, 1, 0x00000002, "FPA Cooktop / Device Position 0x7D Cooktop", appliance_api_fpa_cooktop_v1_device_position_0x7d_cooktop_erds, 19},
  {0x0108, 1, 0x00000004, "FPA Cooktop / Device Position 0x7E Cooktop", appliance_api_fpa_cooktop_v1_device_position_0x7e_cooktop_erds, 19},
  {0x0108, 1, 0x00000008, "FPA Cooktop / Device Position 0x7F Cooktop", appliance_api_fpa_cooktop_v1_device_position_0x7f_cooktop_erds, 20},
  {0x0108, 1, 0x00000010, "FPA Cooktop / Device Position 0x80 Cooktop", appliance_api_fpa_cooktop_v1_device_position_0x80_cooktop_erds, 19},
  {0x0108, 1, 0x00000020, "FPA Cooktop / Device Position 0x81 Cooktop", appliance_api_fpa_cooktop_v1_device_position_0x81_cooktop_erds, 19},
  {0x0108, 1, 0x00000040, "FPA Cooktop / Device Position 0x82 Cooktop", appliance_api_fpa_cooktop_v1_device_position_0x82_cooktop_erds, 19},
  {0x0108, 1, 0x00000080, "FPA Cooktop / Device Position 0x83 Cooktop", appliance_api_fpa_cooktop_v1_device_position_0x83_cooktop_erds, 19},
  {0x0108, 1, 0x00000100, "FPA Cooktop / Device Position 0x7D Downdraft", appliance_api_fpa_cooktop_v1_device_position_0x7d_downdraft_erds, 3},
  {0x0108, 1, 0x00000200, "FPA Cooktop / Device Position 0x7E Downdraft", appliance_api_fpa_cooktop_v1_device_position_0x7e_downdraft_erds, 3},
  {0x0108, 1, 0x00000400, "FPA Cooktop / Device Position 0x7F Downdraft", appliance_api_fpa_cooktop_v1_device_position_0x7f_downdraft_erds, 3},
  {0x0108, 1, 0x00000800, "FPA Cooktop / Device Position 0x81 Downdraft", appliance_api_fpa_cooktop_v1_device_position_0x81_downdraft_erds, 3},
  {0x0108, 1, 0x00001000, "FPA Cooktop / Device Position 0x82 Downdraft", appliance_api_fpa_cooktop_v1_device_position_0x82_downdraft_erds, 3},
  {0x0108, 1, 0x00002000, "FPA Cooktop / Device Position 0x83 Downdraft", appliance_api_fpa_cooktop_v1_device_position_0x83_downdraft_erds, 3},
  {0x0109, 1, 0x00000001, "FPA Hood / Foundation", appliance_api_fpa_hood_v1_foundation_erds, 9},
  {0x0109, 1, 0x00000002, "FPA Hood / Hood Notifications", appliance_api_fpa_hood_v1_hood_notifications_erds, 2},
  {0x0109, 1, 0x00000004, "FPA Hood / Hood Delays", appliance_api_fpa_hood_v1_hood_delays_erds, 5},
  {0x0109, 1, 0x00000008, "FPA Hood / Hood Light Color", appliance_api_fpa_hood_v1_hood_light_color_erds, 3},
  {0x0109, 1, 0x00000010, "FPA Hood / Hood On Off Feature plus Light and Fan Presets", appliance_api_fpa_hood_v1_hood_on_off_feature_plus_light_and_fan_presets_erds, 1},
  {0x0109, 1, 0x00000020, "FPA Hood / BLE Hood to Hob", appliance_api_fpa_hood_v1_ble_hood_to_hob_erds, 4},
  {0x0200, 1, 0x00000001, "Enhanced Features / Block 1", appliance_api_enhanced_features_v1_block_1_erds, 3},
  {0x0201, 1, 0x00000001, "Device Gateway / Matter", appliance_api_device_gateway_v1_matter_erds, 5},
  {0x0201, 1, 0x00000002, "Device Gateway / BLE Advertise Only Sensor", appliance_api_device_gateway_v1_ble_advertise_only_sensor_erds, 31},
  {0x0201, 1, 0x00000004, "Device Gateway / Test Mode", appliance_api_device_gateway_v1_test_mode_erds, 1},
  {0x0201, 1, 0x00000008, "Device Gateway / Delayed Appliance OTA Update", appliance_api_device_gateway_v1_delayed_appliance_ota_update_erds, 1},
  {0x0201, 1, 0x00000010, "Device Gateway / Voice Diagnostics", appliance_api_device_gateway_v1_voice_diagnostics_erds, 3},
  {0x0201, 1, 0x00000020, "Device Gateway / Remote Voice Enablement", appliance_api_device_gateway_v1_remote_voice_enablement_erds, 2},
  {0x0201, 1, 0x00000040, "Device Gateway / Network State Indication", appliance_api_device_gateway_v1_network_state_indication_erds, 1},
  {0x0202, 1, 0x00000001, "HVAC / Base Functionality", appliance_api_hvac_v1_base_functionality_erds, 7},
  {0x0202, 1, 0x00000002, "HVAC / Heat Mode Supported", appliance_api_hvac_v1_heat_mode_supported_erds, 6},
  {0x0202, 1, 0x00000004, "HVAC / Cool Mode Supported", appliance_api_hvac_v1_cool_mode_supported_erds, 6},
  {0x0202, 1, 0x00000008, "HVAC / Fan Mode Supported", appliance_api_hvac_v1_fan_mode_supported_erds, 3},
  {0x0202, 1, 0x00000010, "HVAC / Heating Setpoint Minimum Limit Configurable", appliance_api_hvac_v1_heating_setpoint_minimum_limit_configurable_erds, 2},
  {0x0202, 1, 0x00000020, "HVAC / Heating Setpoint Maximum Limit Configurable", appliance_api_hvac_v1_heating_setpoint_maximum_limit_configurable_erds, 2},
  {0x0202, 1, 0x00000040, "HVAC / Cooling Setpoint Minimum Limit Configurable", appliance_api_hvac_v1_cooling_setpoint_minimum_limit_configurable_erds, 2},
  {0x0202, 1, 0x00000080, "HVAC / Cooling Setpoint Maximum Limit Configurable", appliance_api_hvac_v1_cooling_setpoint_maximum_limit_configurable_erds, 2},
  {0x0202, 1, 0x00000100, "HVAC / Air Purifier", appliance_api_hvac_v1_air_purifier_erds, 2},
  {0x0202, 1, 0x00000200, "HVAC / Energy Conservation", appliance_api_hvac_v1_energy_conservation_erds, 2},
  {0x0202, 1, 0x00000400, "HVAC / Heat Pump Temperature Lockout", appliance_api_hvac_v1_heat_pump_temperature_lockout_erds, 2},
  {0x0202, 1, 0x00000800, "HVAC / Input Current Limiting", appliance_api_hvac_v1_input_current_limiting_erds, 2},
  {0x0300, 1, 0x00000001, "Voice Control / Alexa Voice Service", appliance_api_voice_control_v1_alexa_voice_service_erds, 2},
  {0x0301, 1, 0x00000001, "Android UI / Sign of Life", appliance_api_android_ui_v1_sign_of_life_erds, 1},
  {0x0301, 1, 0x00000002, "Android UI / Camera Configuration Personality", appliance_api_android_ui_v1_camera_configuration_personality_erds, 1},
  {0x0302, 1, 0x00000001, "Resource Management / Electrical Energy Usage", appliance_api_resource_management_v1_electrical_energy_usage_erds, 4},
  {0x0302, 1, 0x00000002, "Resource Management / Hot Water Usage in Liters", appliance_api_resource_management_v1_hot_water_usage_in_liters_erds, 4},
  {0x0302, 1, 0x00000004, "Resource Management / Hot Water Usage in Milliliters", appliance_api_resource_management_v1_hot_water_usage_in_milliliters_erds, 4},
  {0x0302, 1, 0x00000008, "Resource Management / Cold Water Usage in Liters", appliance_api_resource_management_v1_cold_water_usage_in_liters_erds, 4},
  {0x0302, 1, 0x00000010, "Resource Management / Cold Water Usage in Milliliters", appliance_api_resource_management_v1_cold_water_usage_in_milliliters_erds, 4},
  {0x0302, 1, 0x00000020, "Resource Management / Gas Usage", appliance_api_resource_management_v1_gas_usage_erds, 5},
  {0x0302, 1, 0x00000040, "Resource Management / Electrical Time of Use Pricing Schedules Season 1", appliance_api_resource_management_v1_electrical_time_of_use_pricing_schedules_season_1_erds, 4},
  {0x0302, 1, 0x00000080, "Resource Management / Electrical Time of Use Pricing Schedules Season 2", appliance_api_resource_management_v1_electrical_time_of_use_pricing_schedules_season_2_erds, 2},
  {0x0302, 1, 0x00000100, "Resource Management / Electrical Time of Use Pricing Schedules Season 3", appliance_api_resource_management_v1_electrical_time_of_use_pricing_schedules_season_3_erds, 2},
  {0x0302, 1, 0x00000200, "Resource Management / Electrical Time of Use Pricing Schedules Season 4", appliance_api_resource_management_v1_electrical_time_of_use_pricing_schedules_season_4_erds, 2},
  {0x0302, 1, 0x00000400, "Resource Management / Cost Comfort Slider", appliance_api_resource_management_v1_cost_comfort_slider_erds, 2},
  {0x0302, 1, 0x00000800, "Resource Management / DSM Override", appliance_api_resource_management_v1_dsm_override_erds, 2},
  {0x0302, 1, 0x00001000, "Resource Management / Electrical Energy Usage v2", appliance_api_resource_management_v1_electrical_energy_usage_v2_erds, 4},
  {0x0303, 1, 0x00000001, "Environmental Sensing / Device Air Quality", appliance_api_environmental_sensing_v1_device_air_quality_erds, 1},
  {0x0303, 1, 0x00000002, "Environmental Sensing / Relative Humidity Sensor", appliance_api_environmental_sensing_v1_relative_humidity_sensor_erds, 4},
  {0x0303, 1, 0x00000004, "Environmental Sensing / VOC Index Sensor", appliance_api_environmental_sensing_v1_voc_index_sensor_erds, 4},
  {0x0303, 1, 0x00000008, "Environmental Sensing / Fine Air Particles Sensor", appliance_api_environmental_sensing_v1_fine_air_particles_sensor_erds, 4},
  {0x0303, 1, 0x00000010, "Environmental Sensing / NOx Index Sensor", appliance_api_environmental_sensing_v1_nox_index_sensor_erds, 4},
  {0x0303, 1, 0x00000020, "Environmental Sensing / 1 um Particle Sensor", appliance_api_environmental_sensing_v1_1_um_particle_sensor_erds, 4},
  {0x0303, 1, 0x00000040, "Environmental Sensing / 4 um Particle Sensor", appliance_api_environmental_sensing_v1_4_um_particle_sensor_erds, 4},
  {0x0303, 1, 0x00000080, "Environmental Sensing / 10 um Particle Sensor", appliance_api_environmental_sensing_v1_10_um_particle_sensor_erds, 4},
  {0x0303, 1, 0x00000100, "Environmental Sensing / Temperature Sensor", appliance_api_environmental_sensing_v1_temperature_sensor_erds, 4},
  {0x0303, 1, 0x00000200, "Environmental Sensing / Displayed Color Scheme", appliance_api_environmental_sensing_v1_displayed_color_scheme_erds, 3},
  {0x0303, 1, 0x00000400, "Environmental Sensing / Fine Air Particles Sensor Index", appliance_api_environmental_sensing_v1_fine_air_particles_sensor_index_erds, 4},
  {0x0303, 1, 0x00000800, "Environmental Sensing / 1 um Particle Sensor Index", appliance_api_environmental_sensing_v1_1_um_particle_sensor_index_erds, 4},
  {0x0303, 1, 0x00001000, "Environmental Sensing / 4 um Particle Sensor Index", appliance_api_environmental_sensing_v1_4_um_particle_sensor_index_erds, 4},
  {0x0303, 1, 0x00002000, "Environmental Sensing / 10 um Particle Sensor Index", appliance_api_environmental_sensing_v1_10_um_particle_sensor_index_erds, 4},
  {0x0304, 1, 0x00000001, "Battery / Foundation", appliance_api_battery_v1_foundation_erds, 5},
  {0x0400, 1, 0x00000001, "Software Diagnostics / Reset Information", appliance_api_software_diagnostics_v1_reset_information_erds, 4},
  {0x0400, 1, 0x00000002, "Software Diagnostics / Linux Diagnostics", appliance_api_software_diagnostics_v1_linux_diagnostics_erds, 4},
  {0x0400, 1, 0x00000004, "Software Diagnostics / GEA2 GEA3 Interface Diagnostics for Interfaces 1 to 3", appliance_api_software_diagnostics_v1_gea2_gea3_interface_diagnostics_for_interfaces_1_to_3_erds, 3},
  {0x0400, 1, 0x00000008, "Software Diagnostics / GEA2 GEA3 Interface Diagnostics for Interfaces 4 to 6", appliance_api_software_diagnostics_v1_gea2_gea3_interface_diagnostics_for_interfaces_4_to_6_erds, 3},
  {0x0500, 1, 0x00000001, "Matter Room AC / Foundation", appliance_api_matter_room_ac_v1_foundation_erds, 25},
};
const uint16_t appliance_feature_api_descriptor_count =
  sizeof(appliance_feature_api_descriptors) / sizeof(appliance_feature_api_descriptors[0]);
