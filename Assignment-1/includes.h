/* Standard includes. */
#include <system.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Scheduler includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "FreeRTOS/timers.h"
#include "freertos/queue.h"

/* Altera includes. */
#include <sys/alt_alarm.h>
#include <sys/alt_irq.h>
#include <altera_avalon_pio_regs.h>
#include <alt_types.h>

/* Global Variables. */
static volatile int currentState;
static volatile int loadStatusSwitch;
static volatile int loadStatusController;
static volatile int thresholdROC;
static volatile int thresholdFreq;
static volatile int overThreshold;

/* Queues */
static QueueHandle_t freqForDisplay;
static QueueHandle_t changeInFreqForDisplay;
static QueueHandle_t rawFreqData;
static QueueHandle_t keyboardData;

/* Semaphores */
SemaphoreHandle_t keyboardSemaphore;
SemaphoreHandle_t freqRelaySemaphore;

/* Function Prototypes. */
static void VGAController(void *pvParameters);
static void MainController(void *pvParameters);
static void HumanInteractions(void *pvParameters);
static void LEDController(void *pvParameters);
static void SwitchPoll(void *pvParameters);

/* ISR Prototypes. */
void ButtonInterruptsFunction(void* context, alt_u32 id);
void FreqRelayInterrupt(void* context, alt_u32 id);