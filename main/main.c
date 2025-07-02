/*#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "diagnostics.h"
#include "config.h"
#include "sensors.h"
#include "storage.h"
#include "communication.h"
#include "power_manager.h"
#include "utils.h"
#include "tasks.h"

void app_main(void) {
    diagnostics_init();          // 1) Arranca el sistema de logs y diagnóstico
    config_init();               // 2) Carga o inicializa la configuración global (nvs, etc)
    sensors_init();              // 3) Pone a punto los sensores
    storage_init();              // 4) Inicializa el almacenamiento local (nvs)
    communication_init();        // 5) Arranca wifi y mqtt y sincroniza hora
    power_manager_init();        // 6) Comprueba y registra si venimos de deep sleep
    timer_manager_init();        // 7) Prepara el gestor de temporización
    tasks_start_all();           // 8) Crea y lanza las tareas de freertos (lectura, publicación y deep sleep)
}*/
// tfg/main/main.c (Versión Fusionada)
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2c.h"

// Componentes de Antonio Mata
#include "communication.h"
#include "power_manager.h"
#include "config.h"
#include "diagnostics.h"
#include "storage.h"

// Componentes específicos del nivómetro (antoniopalafox)
#include "nivometro_sensors.h"

static const char *TAG = "NIVOMETRO_MAIN";

// Configuración I2C
#define I2C_MASTER_SCL_IO           22
#define I2C_MASTER_SDA_IO           21  
#define I2C_MASTER_NUM              0
#define I2C_MASTER_FREQ_HZ          400000

// Configuración sensores específicos nivómetro
#define HCSR04P_TRIGGER_PIN         12
#define HCSR04P_ECHO_PIN            13
#define HCSR04P_CAL_FACTOR          1.02f

#define HX711_DOUT_PIN              26
#define HX711_SCK_PIN               27
#define HX711_KNOWN_WEIGHT_G        500.0f

#define VL53L0X_ADDRESS             0x29
#define VL53L0X_CAL_FACTOR          1.05f

// Variables globales
static nivometro_t g_nivometro;
static QueueHandle_t sensor_data_queue;

// Tópicos MQTT específicos
#define MQTT_TOPIC_ULTRASONIC       "nivometro/antartica/ultrasonic"
#define MQTT_TOPIC_WEIGHT           "nivometro/antartica/weight"  
#define MQTT_TOPIC_LASER            "nivometro/antartica/laser"
#define MQTT_TOPIC_STATUS           "nivometro/antartica/status"
#define MQTT_TOPIC_DIAGNOSTICS      "nivometro/antartica/diagnostics"

// Inicializar I2C
static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    
    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) return ret;
    
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// Tarea principal de lectura de sensores
void sensor_task(void *pvParameters) {
    nivometro_data_t sensor_data;
    
    ESP_LOGI(TAG, "Iniciando tarea de lectura de sensores");
    
    while (1) {
        // Leer todos los sensores
        esp_err_t ret = nivometro_read_all_sensors(&g_nivometro, &sensor_data);
        
        if (ret == ESP_OK) {
            // Enviar datos a la cola para procesamiento
            if (xQueueSend(sensor_data_queue, &sensor_data, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Cola de datos llena, descartando lectura");
            }
            
            // Log para debug
            ESP_LOGI(TAG, "📊 Ultrasonido: %.2f cm | Peso: %.2f g | Láser: %.0f mm | Estado: %s",
                     sensor_data.ultrasonic_distance_cm,
                     sensor_data.weight_grams, 
                     sensor_data.laser_distance_mm,
                     nivometro_get_sensor_status_string(sensor_data.sensor_status));
        } else {
            ESP_LOGE(TAG, "Error leyendo sensores");
        }
        
        // Esperar antes de la siguiente lectura
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 segundo
    }
}

// Tarea de comunicación MQTT
void communication_task(void *pvParameters) {
    nivometro_data_t sensor_data;
    char json_buffer[512];
    
    ESP_LOGI(TAG, "Iniciando tarea de comunicación MQTT");
    
    while (1) {
        // Esperar datos de sensores
        if (xQueueReceive(sensor_data_queue, &sensor_data, portMAX_DELAY) == pdTRUE) {
            
            // Crear JSON con datos del ultrasonido
            snprintf(json_buffer, sizeof(json_buffer),
                    "{\"distance_cm\":%.2f,\"timestamp\":%llu,\"sensor\":\"hcsr04p\",\"status\":%s}",
                    sensor_data.ultrasonic_distance_cm,
                    sensor_data.timestamp_us,
                    nivometro_is_sensor_working(sensor_data.sensor_status, 0) ? "true" : "false");
            communication_publish(MQTT_TOPIC_ULTRASONIC, json_buffer);
            
            // Crear JSON con datos del peso
            snprintf(json_buffer, sizeof(json_buffer),
                    "{\"weight_g\":%.2f,\"timestamp\":%llu,\"sensor\":\"hx711\",\"status\":%s}",
                    sensor_data.weight_grams,
                    sensor_data.timestamp_us,
                    nivometro_is_sensor_working(sensor_data.sensor_status, 1) ? "true" : "false");
            communication_publish(MQTT_TOPIC_WEIGHT, json_buffer);
            
            // Crear JSON con datos del láser
            snprintf(json_buffer, sizeof(json_buffer),
                    "{\"distance_mm\":%.0f,\"timestamp\":%llu,\"sensor\":\"vl53l0x\",\"status\":%s}",
                    sensor_data.laser_distance_mm,
                    sensor_data.timestamp_us,
                    nivometro_is_sensor_working(sensor_data.sensor_status, 2) ? "true" : "false");
            communication_publish(MQTT_TOPIC_LASER, json_buffer);
            
            // Datos de estado general
            snprintf(json_buffer, sizeof(json_buffer),
                    "{\"battery_v\":%.2f,\"temp_c\":%d,\"sensor_mask\":%d,\"timestamp\":%llu}",
                    sensor_data.battery_voltage,
                    sensor_data.temperature_c,
                    sensor_data.sensor_status,
                    sensor_data.timestamp_us);
            communication_publish(MQTT_TOPIC_STATUS, json_buffer);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Small delay
    }
}

// Configuración del nivómetro
static esp_err_t setup_nivometro(void) {
    nivometro_config_t config = {
        // HC-SR04P
        .hcsr04p_trigger_pin = HCSR04P_TRIGGER_PIN,
        .hcsr04p_echo_pin = HCSR04P_ECHO_PIN,
        .hcsr04p_cal_factor = HCSR04P_CAL_FACTOR,
        
        // HX711
        .hx711_dout_pin = HX711_DOUT_PIN,
        .hx711_sck_pin = HX711_SCK_PIN,
        .hx711_gain = HX711_GAIN_128,
        .hx711_known_weight = HX711_KNOWN_WEIGHT_G,
        
        // VL53L0X
        .vl53l0x_i2c_port = I2C_MASTER_NUM,
        .vl53l0x_address = VL53L0X_ADDRESS,
        .vl53l0x_accuracy = VL53L0X_ACCURACY_BETTER,
        .vl53l0x_cal_factor = VL53L0X_CAL_FACTOR
    };
    
    return nivometro_init(&g_nivometro, &config);
}

void app_main(void) {
    esp_err_t ret;
    
    ESP_LOGI(TAG, "🏔️  Iniciando TFG Nivómetro Antártida");
    
    // Inicializar NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Inicializar componentes base (Antonio Mata)
    ESP_ERROR_CHECK(system_config_init());
    ESP_ERROR_CHECK(power_manager_init());
    ESP_ERROR_CHECK(storage_init());
    ESP_ERROR_CHECK(diagnostics_init());
    
    // Inicializar I2C para VL53L0X
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "✅ I2C inicializado");
    
    // Configurar nivómetro con sensores específicos
    ESP_ERROR_CHECK(setup_nivometro());
    
    // Inicializar comunicación
    ESP_ERROR_CHECK(communication_init());
    ESP_LOGI(TAG, "✅ Comunicación MQTT inicializada");
    
    // Crear cola para datos de sensores
    sensor_data_queue = xQueueCreate(5, sizeof(nivometro_data_t));
    if (!sensor_data_queue) {
        ESP_LOGE(TAG, "Error creando cola de datos");
        return;
    }
    
    // Calibración inicial de la balanza
    ESP_LOGI(TAG, "🔧 Realizando tara inicial...");
    nivometro_tare_scale(&g_nivometro);
    
    // Crear tareas
    xTaskCreate(sensor_task, "sensor_task", 8192, NULL, 5, NULL);
    xTaskCreate(communication_task, "comm_task", 8192, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "🚀 Sistema nivómetro iniciado correctamente");
    ESP_LOGI(TAG, "📡 Enviando datos a tópicos MQTT:");
    ESP_LOGI(TAG, "   - %s", MQTT_TOPIC_ULTRASONIC);
    ESP_LOGI(TAG, "   - %s", MQTT_TOPIC_WEIGHT);
    ESP_LOGI(TAG, "   - %s", MQTT_TOPIC_LASER);
    ESP_LOGI(TAG, "   - %s", MQTT_TOPIC_STATUS);
}


