#include "includes.h"


void setUpMisc(void)
{
    currentState = 0; // Default to non-maintance mode
}

void setUpISRs(void)
{
    int buttonValue = 0;
    
    // clears the edge capture register
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTONS_BASE, 0);
    // enable interrupts for all buttons
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x7);

    // register the ISR
    alt_irq_register(PUSH_BUTTON_IRQ, (void*)&buttonValue, ButtonInterruptsFunction);
}

void setUpTasks(void)
{
    xTaskCreate(VGAController, "VGA Controller Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(MainController, "Main Controller Task", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(HumanInteractions, "Human Interactions Task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(LEDController, "LED Controller Task", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(SwitchPoll, "Switch Polling Task", configMINIMAL_STACK_SIZE, NULL, 2, NULL);

    vTaskStartScheduler();
}

int main(void)
{
    setUpMisc();
    setUpISRs();
    setUpTasks();
    
    while(1);
}

