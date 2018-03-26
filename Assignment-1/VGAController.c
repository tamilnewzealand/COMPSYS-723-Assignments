#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sys/alt_irq.h"
#include "system.h"
#include "io.h"
#include "altera_up_avalon_video_character_buffer_with_dma.h"
#include "altera_up_avalon_video_pixel_buffer_dma.h"

#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"
#include "FreeRTOS/queue.h"

//For frequency plot
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
TaskHandle_t PRVGADraw;


static QueueHandle_t Q_freq_data;

typedef struct{
	unsigned int x1;
	unsigned int y1;
	unsigned int x2;
	unsigned int y2;
}Line;

int check_stabiltiy(void);

/****** VGA display ******/
void PRVGADraw_Task(void *pvParameters ){
	
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
		while(uxQueueMessagesWaiting( Q_freq_data ) != 0){
			xQueueReceive( Q_freq_data, freq+i, 0 );

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
		int currentState = 0; //SHOULD BE FROM GLOBAL VAR!!!!
		if (currentState == 1){  //In maintenance mode
			strncpy(systemStatus, "Maintenance", sizeof(systemStatus));
		}else if(check_stabiltiy() == 1){
			strncpy(systemStatus, "Stable", sizeof(systemStatus));
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

void freq_relay(){
	#define SAMPLING_FREQ 16000.0
	double temp = SAMPLING_FREQ/(double)IORD(FREQUENCY_ANALYSER_BASE, 0);

	xQueueSendToBackFromISR( Q_freq_data, &temp, pdFALSE );

	return;
}

//if any green LEDs are turned on then the system is unstable
//Return 1 if the system is stable, 0 if unstable
int check_stabiltiy(void){
	int greenLedStatus = IORD(GREEN_LEDS_BASE, 0);
	if (greenLedStatus == 0){
		return 1;
	}else{
		return 0;
	}
}



int main()
{
	Q_freq_data = xQueueCreate( 100, sizeof(double) );


	alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay);

	xTaskCreate( PRVGADraw_Task, "DrawTsk", configMINIMAL_STACK_SIZE, NULL, PRVGADraw_Task_P, &PRVGADraw );

	vTaskStartScheduler();

	while(1)

  return 0;
}


