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
#include <alt_types.h>
#include <altera_avalon_pio_regs.h>
#include "altera_up_avalon_ps2.h"
#include "altera_up_ps2_keyboard.h"

/* Global Variables. */
static volatile int currentState;
static volatile int loadStatusSwitch;
static volatile int loadStatusController;
static volatile double thresholdROC;
static volatile double thresholdFreq;
static volatile int timeoutFinish;

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
void KeyboardISR(void* context, alt_u32 id);
void PushButtonISR(void* context, alt_u32 id);
void FrequencyRelayISR(void* context, alt_u32 id);

/* Timer Callbacks */
void vTimeoutCallback(xTimerHandle t_timer);

/* FSM state type */
enum state { stable, shedLoad, monitor, reconnectLoad };
typedef enum state state_t;

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
    int timeoutDirection = 0;
    int nextToDisconnect;
    double oldFreqValue;
    double newFreqValue;
    double newRateOfChange;

    TimerHandle_t timeoutTimer = xTimerCreate("Timeout Timer", pdMS_TO_TICKS(500), pdFALSE, NULL, vTimeoutCallback);

    state_t curState = stable;
    state_t nextState = stable;

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

        // next state logic
        switch(curState)
        {
            case stable:
                if ((newFreqValue < thresholdFreq) || (newRateOfChange > thresholdROC)) { nextState = shedLoad; }
                else { nextState = stable; }
                break;
            case shedLoad:
                nextState = monitor;
                break;
            case monitor:
                if (timeoutFinish == 1)
                {
                    if (timeoutDirection == 1) { nextState = shedLoad; }
                    if (timeoutDirection == 2) { nextState = reconnectLoad; }
                }
                else { nextState = monitor; }
                break;
            case reconnectLoad:
                if (nextToDisconnect == 0) { nextState = stable; }
                else { nextState = monitor; }
                break;
        }

        // output logic
        switch(nextState)
        {
            case stable:
                break;
            case shedLoad:
                // disconnect load from system
                loadStatusController &= ~(1UL << nextToDisconnect);
                // increment last removed load counter
                nextToDisconnect++;
                // reset monitor state variables
                timeoutDirection = 0;
                timeoutFinish = 0;
                break;
            case monitor:
                // just entered into monitor state
                if (timeoutDirection == 0)
                {
                    // set whether next load should be disconnected or reconnected
                    if ((newFreqValue < thresholdFreq) || (newRateOfChange > thresholdROC)) { timeoutDirection = 1; }
                    else { timeoutDirection = 2; }
                    // start the timer
                    xTimerStart(timeoutTimer, 0);
                }
                // currently unstable, waiting to disconnect lowest rank load
                else if (timeoutDirection == 1)
                {
                    // change in situation, restart the timer
                    if ((newFreqValue > thresholdFreq) || (newRateOfChange < thresholdROC))
                    {
                        timeoutDirection = 2;
                        xTimerStart(timeoutTimer, 0);
                    }
                }
                // currently stable, waiting to reconnect highest rank load
                else if (timeoutDirection == 2)
                {
                    // change in situation, restart the timer
                    if ((newFreqValue < thresholdFreq) || (newRateOfChange > thresholdROC))
                    {
                        timeoutDirection = 1;
                        xTimerStart(timeoutTimer, 0);
                    }
                }
                break;
            case reconnectLoad:
                // decrement last removed load counter
                nextToDisconnect--;
                // reconnect load to system
                loadStatusController |= (1UL << nextToDisconnect);
                // reset monitor state variables
                timeoutDirection = 0;
                timeoutFinish = 0;
                break;
        }
        
        curState = nextState;
    }
}

void KeyboardISR (void* context, alt_u32 id)
{
    char ascii;
    int status = 0;
    unsigned char key = 0;
    unsigned char code;
    KB_CODE_TYPE decode_mode;
    status = decode_scancode (context, &decode_mode , &key , &ascii) ;
    if ( status == 0 ) //success
    {
        if (decode_mode == KB_ASCII_MAKE_CODE) { code = ascii; }
        if ((key == 0x75) || (key == 0x6B) || (key == 0x72) || (key == 0x74) || (key == 0x5A)) { code = key; }
        if (key == 0x76 ) { code = 27; }
        if ((code == 204) || (code == 1)) { return; }
        xQueueSendToBackFromISR(keyboardData, &code, pdFALSE);
    }
}

/**
 * Toggles the maintenance mode on any of the buttons being pressed.
 */
void PushButtonISR(void* context, alt_u32 id)
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
void FrequencyRelayISR(void* context, alt_u32 id)
{
    unsigned int temp = IORD(FREQUENCY_ANALYSER_BASE, 0);
    xQueueSendToBackFromISR(rawFreqData, &temp, pdFALSE);
    xSemaphoreGiveFromISR(freqRelaySemaphore, pdTRUE);
}

/**
 * Sets the timeoutFinish flag variable and gives a semaphore
 * to trigger the MainController Task to run.
 */
void vTimeoutCallback(xTimerHandle t_timer)
{
    timeoutFinish = 1;
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
    loadStatusController = 0xFF;
    thresholdROC = 0.6;
    thresholdFreq = 43.7;
    timeoutFinish = 0;

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

    // open the PS/2 keyboard device for reading
    alt_up_ps2_dev * ps2_device = alt_up_ps2_open_dev(PS2_NAME);

    // clear the fifo buffer on the ps/2 device
    alt_up_ps2_clear_fifo (ps2_device) ;

    // register the keyboard ISR
    alt_irq_register(PS2_IRQ, ps2_device, KeyboardISR);

    // clears the edge capture register
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0);
    // enable interrupts for all buttons
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x7);

    // register the buttons ISR
    alt_irq_register(PUSH_BUTTON_IRQ, (void*)&buttonValue, PushButtonISR);

    // register the frequency relay ISR
    alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, FrequencyRelayISR);
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

