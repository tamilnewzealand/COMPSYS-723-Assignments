/* Standard includes. */
#include <system.h>
#include <stdio.h>

/* Scheduler includes. */
#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/semphr.h"
#include "FreeRTOS/task.h"
#include "FreeRTOS/timers.h"
#include "FreeRTOS/queue.h"

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
static void HumanInteractions(void *pvParameters);
static void LEDController(void *pvParameters);
static void SwitchPoll(void *pvParameters);
static void MainController(void *pvParameters);

/* ISR Prototypes. */
void ButtonInterruptsFunction(void* context, alt_u32 id);
void FreqRelayInterrupt(void* context, alt_u32 id);

/**
 * This task will read the data coming from the other tasks/ISRs and display the
 * necessary information in an appropriate format (graphical/textual) on the VGA
 * Display.
 */
static void VGAController(void *pvParameters)
{
    return;
}

/**
 * Controls all interactions with the computer via keyboard/screen. Calculates
 * and stores new threshold values based on these interactions.
 */
static void HumanInteractions(void *pvParameters)
{
    return;
}

/**
 * Calculates the current status of the loads and sets the LEDs appropriately.
 * In normal mode will read from two variables coming from the controller and
 * switches while in maintenance mode will only read from the switches.
 */
static void LEDController(void *pvParameters)
{
    const TickType_t xDelay = 10 / portTICK_PERIOD_MS;
    int loadStatus;

    while(1)
    {
        // not in maintenance mode
        if (currentState == 0) { loadStatus = loadStatusController & loadStatusSwitch; }
        // in maintenance mode
        else { loadStatus = loadStatusSwitch; }

        // write the status of the loads to the RED and GREEN LEDs
        IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, loadStatus);
        IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, (((currentState << 17) | (0x00FF & (~loadStatus)))));

        vTaskDelay(xDelay);
    }
}

/**
 * Polls the switches and writes their current status to a global variable.
 */
static void SwitchPoll(void *pvParameters)
{
    const TickType_t xDelay = 10 / portTICK_PERIOD_MS;

    while(1)
    {
        // read the value of the switch and store
        loadStatusSwitch = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);
        vTaskDelay(xDelay);
    }
}

/**
 * This is the central controller task that makes the major logic decisions in
 * this program.
 */
static void MainController(void *pvParameters)
{
    int rawFreqValue;
    double oldFreqValue;
    double newFreqValue;
    double newRateOfChange;

    while(1)
    {
    	// wait on semaphore
        xSemaphoreTake(freqRelaySemaphore, portMAX_DELAY);
        
        // check for any updates of new data
        xQueueReceive(rawFreqData, &rawFreqValue, 0);

        // store old freq value
        oldFreqValue = newFreqValue;
        
        // calculate the new freq of the power
        newFreqValue = 16000 / (double)rawFreqValue;

        // calculate the new rate of change of the power signal
        newRateOfChange = (newFreqValue - oldFreqValue) * 16000 / (double)rawFreqValue;

        // send calculated freq and ROC values to queue for VGA controller task
        xQueueSendToBack(freqForDisplay, &newFreqValue, 0);
        xQueueSendToBack(changeInFreqForDisplay, &newRateOfChange, 0);
    }
}

/**
 * Toggles the maintenance mode on any of the buttons being pressed.
 */
void ButtonInterruptsFunction(void* context, alt_u32 id)
{
    // need to cast the context first before using it
    int* temp = (int*) context;
    (*temp) = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE);

    // clears the edge capture register
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);

    if (currentState == 0) currentState = 1;
    else currentState = 0;
}

/**
 * Reads frequency data from the hardware component and adds it
 * to end of a queue for later processing.
 */
void FreqRelayInterrupt(void* context, alt_u32 id)
{
    unsigned int temp = IORD(FREQUENCY_ANALYSER_BASE, 0);
    xQueueSendToBackFromISR(rawFreqData, &temp, pdFALSE);
    xSemaphoreGiveFromISR(freqRelaySemaphore, pdTRUE);
}

/**
 * Sets up all the global variables and initializes the 
 * semaphores and queues used in the program.
 */
void SetUpMisc(void)
{
    // set global variable default values
    currentState = 0; // Default to non-maintance mode
    loadStatusSwitch = 0;
    loadStatusController = 0;
    thresholdROC = 0;
    thresholdFreq = 0;
    overThreshold = 0;

    // create queues
    freqForDisplay = xQueueCreate(20, sizeof(double));
    changeInFreqForDisplay = xQueueCreate(20, sizeof(double));
    rawFreqData = xQueueCreate(5, sizeof(unsigned int));
    keyboardData = xQueueCreate(100, sizeof(char));

    // create binary semaphores
    keyboardSemaphore = xSemaphoreCreateBinary();
    freqRelaySemaphore = xSemaphoreCreateBinary();
}

/**
 * Sets up all the ISRs and registers them.
 */
void SetUpISRs(void)
{
    int buttonValue = 0;

    // clears the edge capture register
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0);
    // enable interrupts for all buttons
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x7);

    // register the buttons ISR
    alt_irq_register(PUSH_BUTTON_IRQ, (void*)&buttonValue, ButtonInterruptsFunction);

    // register the frequency relay ISR
    alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, FreqRelayInterrupt);
}

/**
 * Creates all the tasks used in this program and starts the
 * FreeRTOS scheduler.
 */
void SetUpTasks(void)
{
    xTaskCreate(VGAController, "VGA Controller Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(HumanInteractions, "Human Interactions Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(LEDController, "LED Controller Task", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(SwitchPoll, "Switch Polling Task", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(MainController, "Main Controller Task", configMINIMAL_STACK_SIZE, NULL, 3, NULL);

    vTaskStartScheduler();
}

int main(void)
{
    SetUpMisc();
    SetUpISRs();
    SetUpTasks();

    while(1);
}

