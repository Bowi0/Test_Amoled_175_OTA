#ifndef TEST_ADC_ONESHOT_H
#define TEST_ADC_ONESHOT_H

typedef int esp_err_t;
typedef int adc_unit_t;
typedef int adc_channel_t;
typedef int adc_atten_t;
typedef int adc_bitwidth_t;
typedef void *adc_oneshot_unit_handle_t;

#define ESP_ERR_INVALID_ARG 0x102
#define ADC_UNIT_1 0
#define ADC_ULP_MODE_DISABLE 0
#define ADC_ATTEN_DB_12 3
#define ADC_BITWIDTH_12 12
#define ADC_CHANNEL_4 4

typedef struct {
    adc_unit_t unit_id;
    int clk_src;
    int ulp_mode;
} adc_oneshot_unit_init_cfg_t;

typedef struct {
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
} adc_oneshot_chan_cfg_t;

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *, adc_oneshot_unit_handle_t *);
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t);
esp_err_t adc_oneshot_io_to_channel(int, adc_unit_t *, adc_channel_t *);
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t, adc_channel_t, const adc_oneshot_chan_cfg_t *);
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t, adc_channel_t, int *);

#endif
