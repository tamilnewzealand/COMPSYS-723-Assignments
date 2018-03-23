#include "includes.h"

int main(void)
{
    xTaskCreate(VGAController, "VGA Controller Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(MainController, "Main Controller Task", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(HumanInteractions, "Human Interactions Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(LEDController, "LED Controller Task", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(SwitchPoll, "Switch Polling Task", configMINIMAL_STACK_SIZE, NULL, 2, NULL);

    vTaskStartScheduler();
    
    for (;;);
}

