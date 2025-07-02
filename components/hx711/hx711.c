// hx711.c
#include "hx711.h"
#include "freertos/portmacro.h" // Asegurarse de incluir esto para el mutex
 
// Añadir mutex estático para secciones críticas
static portMUX_TYPE hx711_spinlock = portMUX_INITIALIZER_UNLOCKED;

bool hx711_init(hx711_sensor_t *sensor, int dout_pin, int sck_pin, hx711_gain_t gain) {
    if (sensor == NULL) {
        return false;
    }
    
    sensor->dout_pin = dout_pin;
    sensor->sck_pin = sck_pin;
    sensor->offset = 0;
    sensor->scale = 1.0f;
    sensor->gain = gain;
    
    // Configurar pines
    gpio_config_t io_conf = {};
    
    // Configurar SCK como salida
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << sck_pin);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    
    // Configurar DOUT como entrada
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << dout_pin);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    
    // Inicializar SCK en nivel bajo
    gpio_set_level(sck_pin, 0);
    
    // Despertar el sensor
    //hx711_power_up(sensor);
    
    // Establecer la ganancia
    hx711_set_gain(sensor, gain);
    
    // Esperar a que el sensor esté listo
    int timeout = 0;
    while (gpio_get_level(dout_pin) == 1 && timeout < 100) {
        vTaskDelay(1 / portTICK_PERIOD_MS);
        timeout++;
    }
    
    return (timeout < 100); // Retorna true si el sensor está listo
}

int32_t hx711_read_raw(hx711_sensor_t *sensor) {

    if (sensor == NULL || gpio_get_level(sensor->dout_pin) == 1) {
        return 0; // No hay datos disponibles
    }
    
    int32_t value = 0;
    uint8_t data[3] = {0};
    // Eliminar la variable no utilizada
    // uint8_t filler = 0x00;
    
    // Deshabilitar interrupciones durante la lectura
    taskENTER_CRITICAL(&hx711_spinlock);  // Añadir mutex aquí
    
    // Leer 24 bits
    for (uint8_t i = 0; i < 24; i++) {
        gpio_set_level(sensor->sck_pin, 1);
        esp_rom_delay_us(1); // Pequeño delay
        
        // Actualizar datos
        if (i < 8) {
            data[2] = data[2] << 1;
            if (gpio_get_level(sensor->dout_pin)) {
                data[2] |= 1;
            }
        } else if (i < 16) {
            data[1] = data[1] << 1;
            if (gpio_get_level(sensor->dout_pin)) {
                data[1] |= 1;
            }
        } else {
            data[0] = data[0] << 1;
            if (gpio_get_level(sensor->dout_pin)) {
                data[0] |= 1;
            }
        }
        
        gpio_set_level(sensor->sck_pin, 0);
        esp_rom_delay_us(1); // Pequeño delay
    }
    
    // Pulsos adicionales para configurar la ganancia para la próxima lectura
    for (uint8_t i = 0; i < sensor->gain; i++) {
        gpio_set_level(sensor->sck_pin, 1);
        esp_rom_delay_us(1);
        gpio_set_level(sensor->sck_pin, 0);
        esp_rom_delay_us(1);
    }
    
    // Habilitar interrupciones nuevamente
    taskEXIT_CRITICAL(&hx711_spinlock);  // Añadir mutex aquí
    
    // Combinar bytes en un valor de 32 bits
    value = ((int32_t)data[0] << 16) | ((int32_t)data[1] << 8) | (int32_t)data[2];
    
    // Si es un número negativo (bit más significativo es 1)
    if (data[0] & 0x80) {
        value |= 0xFF000000; // Completar con 1s para mantener signo negativo
    }
    
    return value;
}

float hx711_read_units(hx711_sensor_t *sensor) {
    if (sensor == NULL) {
        return 0;
    }
    
    int32_t raw_value = hx711_read_raw(sensor);
    return (float)(raw_value - sensor->offset) / sensor->scale;
}

void hx711_calibrate(hx711_sensor_t *sensor, float known_weight, int readings) {
    if (sensor == NULL || readings <= 0) {
        return;
    }
    
    // Primero, hacer tara
    hx711_tare(sensor, readings);
    
    // Tomar múltiples lecturas con el peso conocido
    int64_t sum = 0;
    for (int i = 0; i < readings; i++) {
        sum += hx711_read_raw(sensor);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    // Calcular escala
    int32_t avg_reading = sum / readings;
    sensor->scale = (float)(avg_reading - sensor->offset) / known_weight;
}

void hx711_tare(hx711_sensor_t *sensor, int readings) {
    if (sensor == NULL || readings <= 0) {
        return;
    }
    
    // Tomar múltiples lecturas sin peso
    int64_t sum = 0;
    for (int i = 0; i < readings; i++) {
        sum += hx711_read_raw(sensor);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    // Establecer offset
    sensor->offset = sum / readings;
}

void hx711_set_gain(hx711_sensor_t *sensor, hx711_gain_t gain) {
    if (sensor != NULL && (gain == HX711_GAIN_128 || gain == HX711_GAIN_64 || gain == HX711_GAIN_32)) {
        sensor->gain = gain;
        
        // Descartar una lectura para aplicar la nueva ganancia
        hx711_read_raw(sensor);
    }
}

//Modos de ahorro de energia
void hx711_power_down(hx711_sensor_t *sensor) {
    if (sensor != NULL) {
        gpio_set_level(sensor->sck_pin, 0);
        gpio_set_level(sensor->sck_pin, 1);
        esp_rom_delay_us(60);
    }
}

void hx711_power_up(hx711_sensor_t *sensor) {
    if (sensor != NULL) {
        gpio_set_level(sensor->sck_pin, 0);
    }
}