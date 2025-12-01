#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#define BTN_OPEN 12
#define BTN_CLOSE 13
#define BTN_EMERGENCY 14
#define LED_OPEN 2
#define LED_CLOSE 4
#define BUZZER_PIN 18
#define SERVO_PIN 15

QueueHandle_t gateQueue;
SemaphoreHandle_t servoMutex;
SemaphoreHandle_t emergencySemaphore;

typedef enum {
    CMD_OPEN,
    CMD_CLOSE,
    CMD_EMERGENCY_STOP
} gate_cmd_t;

volatile bool emergency_activated = false;
volatile bool gate_is_open = false;

// ---------------- Servo Control Functions ----------------
void init_servo_pwm() {
    ledc_timer_config_t servo_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&servo_timer);

    ledc_channel_config_t servo_channel = {
        .gpio_num = SERVO_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&servo_channel);
    
    printf("Servo PWM initialized on GPIO %d\n", SERVO_PIN);
}

void set_servo_angle(int angle) {
    int min_duty = 409;
    int max_duty = 2048;
    
    int duty = min_duty + (angle * (max_duty - min_duty) / 180);
    
    printf("Setting servo to %d degrees -> duty: %d\n", angle, duty);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// ---------------- PWM Buzzer Functions ----------------
void init_buzzer_pwm() {
    ledc_timer_config_t buzzer_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&buzzer_timer);

    ledc_channel_config_t buzzer_channel = {
        .gpio_num = BUZZER_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&buzzer_channel);
}

void buzzer_beep(int duration_ms, int frequency_hz) {
    if (frequency_hz > 0) {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1, frequency_hz);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 128);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
        vTaskDelay(duration_ms / portTICK_PERIOD_MS);
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

// ---------------- ISR Handler ----------------
static void IRAM_ATTR button_isr_handler(void *arg) {
    int btn = (int)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    gate_cmd_t cmd;
    
    if (btn == BTN_OPEN) {
        cmd = CMD_OPEN;
        xQueueSendFromISR(gateQueue, &cmd, &xHigherPriorityTaskWoken);
    } 
    else if (btn == BTN_CLOSE) {
        cmd = CMD_CLOSE;
        xQueueSendFromISR(gateQueue, &cmd, &xHigherPriorityTaskWoken);
    }
    else if (btn == BTN_EMERGENCY) {
        // Emergency pakai semaphore, bukan queue
        xSemaphoreGiveFromISR(emergencySemaphore, &xHigherPriorityTaskWoken);
    }
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// 🎯 TASK DUAL CORE ASSIGNMENT:

// ================= CORE 0 TASKS =================

// 🎯 Servo Task - CORE 0 (Priority 3)
void servo_task(void *pvParameters) {
    printf("Servo Task running on core %d\n", xPortGetCoreID());
    
    // Set initial position to CLOSED
    set_servo_angle(0);
    gate_is_open = false;
    gpio_set_level(LED_OPEN, 0);
    gpio_set_level(LED_CLOSE, 1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    while (1) {
        gate_cmd_t action;
        if (xQueueReceive(gateQueue, &action, portMAX_DELAY)) {
            
            // Emergency cek
            if (emergency_activated) {
                printf("BLOCKED: Emergency active\n");
                buzzer_beep(50, 800);
                buzzer_beep(50, 800);
                continue;
            }
            
            // Process OPEN/CLOSE commands
            if (xSemaphoreTake(servoMutex, 1000 / portTICK_PERIOD_MS)) {
                
                if (action == CMD_OPEN && !gate_is_open) {
                    printf("SERVO: Opening gate\n");
                    buzzer_beep(30, 1200);
                    
                    // Fast opening
                    set_servo_angle(0);
                    vTaskDelay(200 / portTICK_PERIOD_MS);
                    set_servo_angle(45);
                    vTaskDelay(200 / portTICK_PERIOD_MS);
                    set_servo_angle(90);
                    
                    gate_is_open = true;
                    gpio_set_level(LED_OPEN, 1);
                    gpio_set_level(LED_CLOSE, 0);
                    
                    buzzer_beep(100, 1500);
                    
                } else if (action == CMD_CLOSE && gate_is_open) {
                    printf("SERVO: Closing gate\n");
                    buzzer_beep(30, 1200);
                    
                    // Fast closing
                    set_servo_angle(90);
                    vTaskDelay(200 / portTICK_PERIOD_MS);
                    set_servo_angle(45);
                    vTaskDelay(200 / portTICK_PERIOD_MS);
                    set_servo_angle(0);
                    
                    gate_is_open = false;
                    gpio_set_level(LED_OPEN, 0);
                    gpio_set_level(LED_CLOSE, 1);
                    
                    buzzer_beep(100, 1200);
                    
                } else {
                    printf("SERVO: Gate already in requested position\n");
                    buzzer_beep(30, 800);
                }
                
                xSemaphoreGive(servoMutex);
            }
        }
    }
}

// 🎯 Buzzer Task - CORE 0 (Priority 2)
void buzzer_task(void *pvParameters) {
    printf("Buzzer Task running on core %d\n", xPortGetCoreID());
    
    // Startup sound
    buzzer_beep(100, 1500);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    buzzer_beep(100, 1500);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    buzzer_beep(100, 1500);

    while (1) {
        if (emergency_activated) {
            // EMERGENCY MODE - fast beeping
            buzzer_beep(80, 2500);
            vTaskDelay(80 / portTICK_PERIOD_MS);
        } else {
            // NORMAL MODE - silent
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
}

// ================= CORE 1 TASKS =================

// 🎯 Emergency Monitor Task - CORE 1 (Priority 4 - Highest)
void emergency_monitor_task(void *pvParameters) {
    printf("Emergency Monitor Task running on core %d\n", xPortGetCoreID());
    
    while (1) {
        // Wait for emergency semaphore
        if (xSemaphoreTake(emergencySemaphore, portMAX_DELAY)) {
            emergency_activated = !emergency_activated;
            
            if (emergency_activated) {
                printf("!!! EMERGENCY ACTIVATED !!!\n");
                // Emergency action: stop servo immediately
                if (xSemaphoreTake(servoMutex, 1000 / portTICK_PERIOD_MS)) {
                    // Hold current position
                    xSemaphoreGive(servoMutex);
                }
                buzzer_beep(200, 3000);
            } else {
                printf("Emergency deactivated\n");
                buzzer_beep(100, 1500);
            }
        }
    }
}

// 🎯 LED Task - CORE 1 (Priority 1 - Lowest)
void led_task(void *pvParameters) {
    printf("LED Task running on core %d\n", xPortGetCoreID());
    
    gpio_set_direction(LED_OPEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_CLOSE, GPIO_MODE_OUTPUT);

    while (1) {
        if (emergency_activated) {
            // Emergency - fast blinking both LEDs
            gpio_set_level(LED_OPEN, 1);
            gpio_set_level(LED_CLOSE, 1);
            vTaskDelay(150 / portTICK_PERIOD_MS);
            gpio_set_level(LED_OPEN, 0);
            gpio_set_level(LED_CLOSE, 0);
            vTaskDelay(150 / portTICK_PERIOD_MS);
        } else {
            // Normal - show gate state
            if (gate_is_open) {
                gpio_set_level(LED_OPEN, 1);  // Green ON
                gpio_set_level(LED_CLOSE, 0); // Red OFF
            } else {
                gpio_set_level(LED_OPEN, 0);  // Green OFF
                gpio_set_level(LED_CLOSE, 1); // Red ON
            }
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
    }
}

// ---------------- Main Application ----------------
void app_main() {
    printf("\n=== AUTOMATIC GATE SYSTEM - DUAL CORE ===\n");
    printf("Initializing...\n");

    // Initialize buttons
    gpio_config_t btn_config = {
        .pin_bit_mask = (1ULL << BTN_OPEN) | (1ULL << BTN_CLOSE) | (1ULL << BTN_EMERGENCY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&btn_config);

    // Initialize PWM
    init_servo_pwm();
    init_buzzer_pwm();

    // Create RTOS objects
    gateQueue = xQueueCreate(10, sizeof(gate_cmd_t));
    servoMutex = xSemaphoreCreateMutex();
    emergencySemaphore = xSemaphoreCreateBinary();

    // Install ISR service
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BTN_OPEN, button_isr_handler, (void *)BTN_OPEN);
    gpio_isr_handler_add(BTN_CLOSE, button_isr_handler, (void *)BTN_CLOSE);
    gpio_isr_handler_add(BTN_EMERGENCY, button_isr_handler, (void *)BTN_EMERGENCY);

    // 🎯 CREATE TASKS WITH CORE ASSIGNMENT
    // CORE 0: Servo & Buzzer (Real-time control)
    xTaskCreatePinnedToCore(servo_task, "ServoTask", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(buzzer_task, "BuzzerTask", 2048, NULL, 2, NULL, 0);
    
    // CORE 1: Emergency & LED (Monitoring & Indicators)
    xTaskCreatePinnedToCore(emergency_monitor_task, "EmergencyMonitor", 2048, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(led_task, "LEDTask", 2048, NULL, 1, NULL, 1);

    printf("=== SYSTEM READY ===\n");
    printf("CORE DISTRIBUTION:\n");
    printf("- CORE 0: Servo Task, Buzzer Task\n");
    printf("- CORE 1: Emergency Monitor, LED Task\n");
    printf("\nINITIAL STATE: Gate CLOSED\n");
    printf("Controls:\n");
    printf("- OPEN Button (GPIO %d): Open gate\n", BTN_OPEN);
    printf("- CLOSE Button (GPIO %d): Close gate\n", BTN_CLOSE);
    printf("- EMERGENCY Button (GPIO %d): Instant emergency toggle\n", BTN_EMERGENCY);
}
