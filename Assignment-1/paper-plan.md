# Paper Plan for Assignment 1

*March 21, 2018 - Joshua Gudsell & Sakayan Sitsabesan*

## Tasks

* VGAController (Priority 1)
    * Description: This task will read the data coming from the other tasks/ISRs and display the necessary information in an appropriate format (graphical/textual) on the VGA Display.
    * Condition: TaskDelay every 40ms
    * Queues read: freqForDisplay, changeInFreqForDisplay
    * Variables read: displayText
* MainController (Priority 3)
    * Description: This is the central controller task that makes the major logic decisions in this program, more details can be found in FSM diagram below.
    * Condition: Wait on freqRelaySemaphore
    * Queues read: rawFreqData
    * Queues written: freqForDisplay, changeInFreqForDisplay
    * Variables written: loadStatusController
* HumanInteractions (Priority 1)
    * Description: Controls all interactions with the computer via keyboard/screen. Calculates and stores new threshold values based on these interactions.
    * Condition: Wait on keyboardSemaphore
    * Queues read: keyboardData
    * Variables written: thresholdFreq, thresholdROC, displayText
* LEDController (Priority 2)
    * Description: Calculates the current status of the loads and sets the LEDs appropriately. In normal mode will read from two variables coming from the controller and switches while in maintenance mode will only read from the switches.
    * Condition: TaskDelay every 10ms
    * Variables read: currentState, loadStatusSwitch, loadStatusController
* SwitchPoll (Priority 2)
    * Description: Polls the switches and writes their current status to a global variable
    * Condition: TaskDelay every 10ms
    * Variables written: loadStatusSwitch

## ISR

* KeyboardISR
    * Description: Reads and records the key presses registered on the PS/2 keyboard.
    * Condition: Interrupt on keyboard event
    * Queues written: keyboardData
    * Sets keyboardSemaphore
* PushButtonISR
    * Description: Reads the events occurring on the push buttons and passes on appropriate messages to the appropriate task to initiate the associated task. 
    * Condition: Interrupt on push button event
    * Variables written: currentState
* FrequencyRelayISR
    * Description: Reads the frequency from the hardware component 
    * Condition: Interrupt on hardware trigger event
    * Queues written: rawFreqData
    * Sets freqRelaySemaphore

## Global Variables

* currentState - bool
    * True => in maintenance mode
    * False => not in maintenance mode
* displayText - string
    * Contains misc other text that should be printed on the screen
* loadStatusSwitch - uint8_t
    * bits 1 - 5 represent the current status of each load as input by the user via switches
    * 0 => Load disconnected
    * 1 => Load connected
* loadStatusController - uint8_t
    * bits 1 - 5 represent the current status of each load as desired by the controller
    * 0 => Load disconnected
    * 1 => Load connected
* thresholdROC - uint8_t
    * Stores the value of the currently set Rate of Change threshold
* thresholdFreq - uint8_t
    * Stores the value of the currently set frequency threshold
* overThreshold - bool
## Queues

* freqForDisplay
     * list of values for displaying by the VGA controller
* changeInFreqForDisplay
    * list of values for displaying by the VGA controller
* rawFreqData
    * list of values coming from hardware component
* keyboardData
    * list of ASCII codes for the key presses occurring

## Semaphores

* keyboardSemaphore
* freqRelaySemaphore

## Timer Callback Function

* timeoutCallback
    * Description: Sets appropriate timeout variable and also sets freqRelaySemaphore
    * Condition: Callback function after 500ms have timed out from when system became stable/unstable

## FSM Diagram

*insert FSM diagram here*

## Doubts??
* Is frequency stored in memory or as a function (ie. dynamic)?