#include "includes.h"

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

        // begin FSM logic here
    }
    
    return;
}