// hx711.h
#ifndef HX711_H
#define HX711_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_system.h"

// Opciones de ganancia
typedef enum {
    HX711_GAIN_128 = 1,  // Canal A, ganancia 128
    HX711_GAIN_64 = 3,   // Canal A, ganancia 64
    HX711_GAIN_32 = 2    // Canal B, ganancia 32
} hx711_gain_t;

typedef struct {
    int dout_pin;            // Pin de datos (DOUT)
    int sck_pin;             // Pin de reloj (SCK)
    int32_t offset;          // Offset de tara
    float scale;             // Factor de escala para convertir a unidades de peso
    hx711_gain_t gain;       // Configuración de ganancia
} hx711_sensor_t;

/**
 * @brief Inicializa el sensor HX711
 * @param sensor Puntero a la estructura del sensor
 * @param dout_pin Pin de datos (DOUT)
 * @param sck_pin Pin de reloj (SCK)
 * @param gain Ganancia del sensor
 * @return true si la inicialización fue exitosa
 */
bool hx711_init(hx711_sensor_t *sensor, int dout_pin, int sck_pin, hx711_gain_t gain);

/**
 * @brief Lee el valor raw del sensor
 * @param sensor Puntero a la estructura del sensor
 * @return Valor raw del sensor o 0 si hay error
 */
int32_t hx711_read_raw(hx711_sensor_t *sensor);

/**
 * @brief Lee el valor en unidades de peso
 * @param sensor Puntero a la estructura del sensor
 * @return Valor en unidades de peso o 0 si hay error
 */
float hx711_read_units(hx711_sensor_t *sensor);

/**
 * @brief Calibra el sensor
 * @param sensor Puntero a la estructura del sensor
 * @param known_weight Peso conocido para calibración
 * @param readings Número de lecturas para calibrar
 */
void hx711_calibrate(hx711_sensor_t *sensor, float known_weight, int readings);

/**
 * @brief Realiza la tara del sensor
 * @param sensor Puntero a la estructura del sensor
 * @param readings Número de lecturas para tara
 */
void hx711_tare(hx711_sensor_t *sensor, int readings);

/**
 * @brief Configura la ganancia del sensor
 * @param sensor Puntero a la estructura del sensor
 * @param gain Nueva ganancia
 */
void hx711_set_gain(hx711_sensor_t *sensor, hx711_gain_t gain);

/**
 * @brief Pone el sensor en modo sleep
 * @param sensor Puntero a la estructura del sensor
 */
void hx711_power_down(hx711_sensor_t *sensor);

/**
 * @brief Despierta el sensor del modo sleep
 * @param sensor Puntero a la estructura del sensor
 */
void hx711_power_up(hx711_sensor_t *sensor);

#endif // HX711_H