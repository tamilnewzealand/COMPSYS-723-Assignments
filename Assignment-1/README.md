# Assignment 1 - Group 15

## COMPSYS 723 - Embedded Systems Design

*April 14, 2018 - Joshua Gudsell & Sakayan Sitsabesan*

## IMPORTANT NOTE:

Make sure there are NO space characters for all files used. There are known software issues caused by these spaces, so please extract this file in place with no such thing.

## SETUP INSTRUCTIONS:
1. After extracting the contents of this file, open "Quartus Prime 16.1 Programmer"
2. Click on 'Add File'
3. Select 'freq_relay_controller.sof' and make sure the Altera DE2-115 is connected to the computer
4. Press the 'Start' button making sure 'Program/Configure' is ticked
5. Open "Nios II 16.1 Software Build Tools for Eclipse"
7. From the File -> New menu create a new Nios II application and BSP from Template using the provided nios2.sopcinfo and the Hello World template.
6. Add the provided folder 'FreeRTOS' and the 'main.c' file, while deleting the 'hello.c' file
7. Select the 'Build All' option (Ctrl + B)
8. After successfully building the project, right click 'DDPacemaker'
9. Under 'Run as' select '3 Nios II Hardware'

## USAGE INSTRUCTIONS:
- Any of the switches from 0-7 can be used to control a load
- Any of the push buttons excluding KEY0 can be used to enter maintenance mode
- To configure the thresholds simply use the number row on the keyboard
- Six values must be entered (three for frequency threshold and three for rate of change threshold)
- Pressing enter will transfer the temporary new thresholds to the actual threshold values
- The escape key can be pressed to restart the process and correct any errors

## QUESTION & QUERIES:

For any questions or queries please contact Joshua Gudsell and/or Sakayan Sitsabesan.