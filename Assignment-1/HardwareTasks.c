#include "includes.h"

static void LEDController(void *pvParameters)
{
    return;
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