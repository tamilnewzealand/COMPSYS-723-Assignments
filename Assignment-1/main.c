/* Standard includes. */
#include <system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "io.h"

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
#include "altera_up_avalon_video_character_buffer_with_dma.h"
#include "altera_up_avalon_video_pixel_buffer_dma.h"
#include "altera_up_avalon_ps2.h"
#include "altera_up_ps2_keyboard.h"

/* Definitions for frequency plot */
#define FREQPLT_ORI_X 101		//x axis pixel position at the plot origin
#define FREQPLT_GRID_SIZE_X 5	//pixel separation in the x axis between two data points
#define FREQPLT_ORI_Y 199.0		//y axis pixel position at the plot origin
#define FREQPLT_FREQ_RES 20.0	//number of pixels per Hz (y axis scale)

#define ROCPLT_ORI_X 101
#define ROCPLT_GRID_SIZE_X 5
#define ROCPLT_ORI_Y 259.0
#define ROCPLT_ROC_RES 0.5		//number of pixels per Hz/s (y axis scale)

#define MIN_FREQ 45.0 //minimum frequency to draw

#define PRVGADraw_Task_P      (tskIDLE_PRIORITY+1)

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

/* Line Type for Display */
typedef struct{
	unsigned int x1;
	unsigned int y1;
	unsigned int x2;
	unsigned int y2;
}Line;

/**
 * This task will read the data coming from the other tasks/ISRs and display the
 * necessary information in an appropriate format (graphical/textual) on the VGA
 * Display.
 */
static void VGAController(void *pvParameters)
{
    	alt_up_pixel_buffer_dma_dev *pixel_buf;
	pixel_buf = alt_up_pixel_buffer_dma_open_dev(VIDEO_PIXEL_BUFFER_DMA_NAME);
	if(pixel_buf == NULL){
		printf("can't find pixel buffer device\n");
	}
	alt_up_pixel_buffer_dma_clear_screen(pixel_buf, 0);

	alt_up_char_buffer_dev *char_buf;
	char_buf = alt_up_char_buffer_open_dev("/dev/video_character_buffer_with_dma");
	if(char_buf == NULL){
		printf("can't find char buffer device\n");
	}
	alt_up_char_buffer_clear(char_buf);



	//Set up plot axes
	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 590, 200, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 590, 300, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 50, 200, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 220, 300, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);

	alt_up_char_buffer_string(char_buf, "Frequency(Hz)", 4, 4);
	alt_up_char_buffer_string(char_buf, "52", 10, 7);
	alt_up_char_buffer_string(char_buf, "50", 10, 12);
	alt_up_char_buffer_string(char_buf, "48", 10, 17);
	alt_up_char_buffer_string(char_buf, "46", 10, 22);

	alt_up_char_buffer_string(char_buf, "df/dt(Hz/s)", 4, 26);
	alt_up_char_buffer_string(char_buf, "60", 10, 28);
	alt_up_char_buffer_string(char_buf, "30", 10, 30);
	alt_up_char_buffer_string(char_buf, "0", 10, 32);
	alt_up_char_buffer_string(char_buf, "-30", 9, 34);
	alt_up_char_buffer_string(char_buf, "-60", 9, 36);


	double freq[100], dfreq[100];
	int i = 99, j = 0;
	Line line_freq, line_roc;

	//static words below the graphs
	alt_up_char_buffer_string(char_buf, "Lower threshold(Hz):", 11, 40);
	alt_up_char_buffer_string(char_buf, "ROC threshold(Hz/sec):", 40, 40);
	alt_up_char_buffer_string(char_buf, "Reaction times(ms):    ,    ,    ,    ,", 5, 45);
	alt_up_char_buffer_string(char_buf, "System status:", 52, 45);
	alt_up_char_buffer_string(char_buf, "Min(ms):     Max(ms):     Avg(ms):     Uptime(s):", 8, 50);	

	while(1){

		//receive frequency data from queue
		while(uxQueueMessagesWaiting(freqForDisplay) != 0){
			xQueueReceive(freqForDisplay, freq+i, 0 );

			//calculate frequency RoC

			if(i==0){
				dfreq[0] = (freq[0]-freq[99]) * 2.0 * freq[0] * freq[99] / (freq[0]+freq[99]);
			}
			else{
				dfreq[i] = (freq[i]-freq[i-1]) * 2.0 * freq[i]* freq[i-1] / (freq[i]+freq[i-1]);
			}

			if (dfreq[i] > 100.0){
				dfreq[i] = 100.0;
			}


			i =	++i%100; //point to the next data (oldest) to be overwritten

		}

		//clear old graph to draw new graph
		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 0, 639, 199, 0, 0);
		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 201, 639, 299, 0, 0);

		for(j=0;j<99;++j){ //i here points to the oldest data, j loops through all the data to be drawn on VGA
			if (((int)(freq[(i+j)%100]) > MIN_FREQ) && ((int)(freq[(i+j+1)%100]) > MIN_FREQ)){
				//Calculate coordinates of the two data points to draw a line in between
				//Frequency plot
				line_freq.x1 = FREQPLT_ORI_X + FREQPLT_GRID_SIZE_X * j;
				line_freq.y1 = (int)(FREQPLT_ORI_Y - FREQPLT_FREQ_RES * (freq[(i+j)%100] - MIN_FREQ));

				line_freq.x2 = FREQPLT_ORI_X + FREQPLT_GRID_SIZE_X * (j + 1);
				line_freq.y2 = (int)(FREQPLT_ORI_Y - FREQPLT_FREQ_RES * (freq[(i+j+1)%100] - MIN_FREQ));

				//Frequency RoC plot
				line_roc.x1 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X * j;
				line_roc.y1 = (int)(ROCPLT_ORI_Y - ROCPLT_ROC_RES * dfreq[(i+j)%100]);

				line_roc.x2 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X * (j + 1);
				line_roc.y2 = (int)(ROCPLT_ORI_Y - ROCPLT_ROC_RES * dfreq[(i+j+1)%100]);

				//Draw
				alt_up_pixel_buffer_dma_draw_line(pixel_buf, line_freq.x1, line_freq.y1, line_freq.x2, line_freq.y2, 0x3ff << 0, 0);
				alt_up_pixel_buffer_dma_draw_line(pixel_buf, line_roc.x1, line_roc.y1, line_roc.x2, line_roc.y2, 0x3ff << 0, 0);
			}
		}
		//Check the system status
		char systemStatus[12];
		if (currentState == 1){  //In maintenance mode
			strncpy(systemStatus, "Maintenance", sizeof(systemStatus));
		}else{
			strncpy(systemStatus, "Unstable", sizeof(systemStatus));
		}

		//Get the System uptime based on freeRTOS TickCount which measures in ms,
		//Then format it into a string to display
		unsigned int uptime = xTaskGetTickCount()/1000;
		char uptimeBuffer [sizeof(unsigned int)*8+1];
		(void) sprintf(uptimeBuffer, "%u", uptime);

		//populate fields below the graph
		//TODO: Make these data driven
		alt_up_char_buffer_string(char_buf, "47.3", 32, 40); //Lower threshold
		alt_up_char_buffer_string(char_buf, "0.60", 63, 40); //ROC threshold
		alt_up_char_buffer_string(char_buf, "123", 25, 45); //latest reaction time
		alt_up_char_buffer_string(char_buf, "234", 30, 45);
		alt_up_char_buffer_string(char_buf, "360", 35, 45);
		alt_up_char_buffer_string(char_buf, "200", 40, 45);
		alt_up_char_buffer_string(char_buf, "447", 45, 45); //oldest reaction time
		alt_up_char_buffer_string(char_buf, systemStatus, 67, 45); //System status: {Stable, Unstable, Maintenance}
		alt_up_char_buffer_string(char_buf, "73", 17, 50); //Min reaction time
		alt_up_char_buffer_string(char_buf, "499", 30, 50); //Max reaction time
		alt_up_char_buffer_string(char_buf, "213", 43, 50); //Avg reaction time
		alt_up_char_buffer_string(char_buf, uptimeBuffer, 58, 50); //Uptime

		//delay the task then refresh the display
		vTaskDelay(10);

	}
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

/**
 * Reads and records the key presses registered on the PS/2 keyboard.
 * Filters out only ASCII keys, Arrow Keys, Enter and escape. All other
 * keys are ignored and not passed on. Sends key to back of queue which
 * will trigger the HumanInteractions to handle the user input.
 */
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

