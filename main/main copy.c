    /*
    * LED blink with FreeRTOS
    */
    #include <FreeRTOS.h>
    #include <task.h>
    #include <semphr.h>
    #include <queue.h>

    #include "ssd1306.h"
    #include "gfx.h"

    #include "pico/stdlib.h"
    #include <stdio.h>
    #include "hardware/gpio.h"
    #include "hardware/timer.h"`
    

    // Definição dos pinos
    const int ECHO_PIN = 15;
    const int TRIG_PIN = 16;

    // Recursos do FreeRTOS
    QueueHandle_t xQueueTime;
    QueueHandle_t xQueueDistance;
    SemaphoreHandle_t xSemaphoreTrigger;

    // Variáveis globais para armazenar tempos
    volatile int time_init = 0;
    volatile int time_end = 0;
    

    // Callback para medir tempo do pulso do echo
    void pin_callback(uint gpio, uint32_t events) {
        if (events == GPIO_IRQ_EDGE_RISE) {
            time_init = to_us_since_boot(get_absolute_time()); //Se o pulso sobe, armazena quando começou
        } else if (events == GPIO_IRQ_EDGE_FALL) {
            time_end = to_us_since_boot(get_absolute_time());
            int pulse_duration = time_end - time_init; // Se o pulso desce, calcula o tempo de pulso
            xQueueSendFromISR(xQueueTime, &pulse_duration, NULL);
        }
    }

    // Task para disparar o trigger do sensor
    //Essa tarefa envia um pulso de 10µs no TRIG_PIN a cada 500ms para ativar o sensor.
    void trigger_task(void *p) {
        gpio_init(TRIG_PIN);
        gpio_set_dir(TRIG_PIN, GPIO_OUT);

        while (1) {
            gpio_put(TRIG_PIN, 1);
            sleep_us(10);
            gpio_put(TRIG_PIN, 0);

            vTaskDelay(pdMS_TO_TICKS(2000)); // Aguarda 2s entre medições
        }
    }

    // Task para calcular distância com base no tempo recebido
    void echo_task(void *p) {
        int pulse_duration;
        float distance;

        while (1) {
            if (xQueueReceive(xQueueTime, &pulse_duration, portMAX_DELAY)) {
                distance = (pulse_duration * 0.0343) / 2; // Cálculo da distância em cm

                xQueueSend(xQueueDistance, &distance, portMAX_DELAY);
                xSemaphoreGive(xSemaphoreTrigger); // Notifica o OLED que há nova leitura
            }
        }
    }

    // Task para exibir a distância no display OLED
    void oled_task(void *p) {
        ssd1306_t disp;
        ssd1306_init();
        gfx_init(&disp, 128, 32);

        float distance;
        char buffer[20];

        while (1) {
            //Aguarda o semáforo xSemaphoreTrigger ser liberado
            if (xSemaphoreTake(xSemaphoreTrigger, portMAX_DELAY)) { 
                //Lê a distância da fila xQueueDistance
                if (xQueueReceive(xQueueDistance, &distance, portMAX_DELAY)) {
                    gfx_clear_buffer(&disp);
                    //Mostra a distância no OLED
                    snprintf(buffer, sizeof(buffer), "Dist: %.2f cm", distance);
                    gfx_draw_string(&disp, 0, 0, 1, buffer);
                    gfx_show(&disp);
                }
            }
        }
    }

    int main() {
        stdio_init_all();
        
        // Inicializa GPIO do echo
        gpio_init(ECHO_PIN);
        gpio_set_dir(ECHO_PIN, GPIO_IN);
        gpio_pull_down(ECHO_PIN);
        gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &pin_callback);

        // Criação das filas e semáforo
        xQueueTime = xQueueCreate(10, sizeof(int));
        xQueueDistance = xQueueCreate(10, sizeof(float));
        xSemaphoreTrigger = xSemaphoreCreateBinary();

        // Criação das tasks
        xTaskCreate(trigger_task, "TriggerTask", 256, NULL, 1, NULL);

        xTaskCreate(echo_task, "EchoTask", 256, NULL, 1, NULL);

        xTaskCreate(oled_task, "OLEDTask", 256, NULL, 1, NULL);

        // Inicia o escalonador do FreeRTOS
        vTaskStartScheduler();

        while (true);
    }
