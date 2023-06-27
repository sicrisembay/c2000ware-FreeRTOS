//#############################################################################
//
// FILE:   freertos_ex3_c28x_dual_blinky_cpu1.c
//
// TITLE:  LED Blinky Example
//
//! <h1> LED Blinky Example</h1>
//!
//! This example demonstrates LED blinky demo using freeRTOS tasks on both
//! CPU1 and CPU2 cores. CPU1 passes the ownership of GPIOs connected to
//! LED2 to CPU2. CPU1 toggles LED1 while CPU2 toggles LED2 with varying
//! frequency through two tasks. The varying frequency is achieved through
//! adding different delays between LED toggles for different tasks. Mutual
//! exclusion between tasks is achieved through mutex semaphores.
//!
//! \b External \b Connections \n
//!  - None.
//!
//! \b Watch \b Variables \n
//!  - None.
//!
//
//#############################################################################
//
//
// $Copyright:
// Copyright (C) 2022 Texas Instruments Incorporated - http://www.ti.co/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//   Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
//
//   Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the
//   distribution.
//
//   Neither the name of Texas Instruments Incorporated nor the names of
//   its contributors may be used to endorse or promote products derived
//   from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// $
//#############################################################################

//
// Included Files
//
#include "driverlib.h"
#include "device.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

//
// Macro to define stack size of individual tasks
//
#define LED_TASK1_STACK_SIZE 256
#define LED_TASK2_STACK_SIZE 256

//
// Macro to define priorities of individual tasks
//
#define LED_TASK1_PRIORITY   4
#define LED_TASK2_PRIORITY   2

//
// Macro to define delay(in ticks) between LED toggle
// in tasks. This delay determines the LED toggle
// frequency within a task.
// Note: 1 tick is configured as 1ms in FreeRTOSConfig.h
//
#define LED_TASK1_BLINK_DELAY 500
#define LED_TASK2_BLINK_DELAY 1500
#define LED_TASK_BLOCK_TICKS  10

//
// Macro to define task duration(in ticks)
//
#define LED_TASK1_DURATION  10000
#define LED_TASK2_DURATION  10000

#define STACK_SIZE  256U

//
// Globals
//

//
// Task handles
//
TaskHandle_t ledTask1, ledTask2;

//
// Execution counters
//
static uint32_t ctrTask1, ctrTask2;

//
// The mutex that protects concurrent access of LED2 from multiple tasks.
//
xSemaphoreHandle ledSemaphore;

static StaticTask_t idleTaskBuffer;
static StackType_t  idleTaskStack[STACK_SIZE];
#pragma DATA_SECTION(idleTaskStack,   ".freertosStaticStack")
#pragma DATA_ALIGN ( idleTaskStack , portBYTE_ALIGNMENT )

#if(configAPPLICATION_ALLOCATED_HEAP == 1)
uint8_t ucHeap[ configTOTAL_HEAP_SIZE ];
#pragma DATA_SECTION(ucHeap,   ".freertosHeap")
#pragma DATA_ALIGN ( ucHeap , portBYTE_ALIGNMENT )
#endif

//
// Function prototypes
//
void ledTask1Func(void *pvParameters);
void ledTask2Func(void *pvParameters);


//
// vApplicationGetIdleTaskMemory
//
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize )
{
    *ppxIdleTaskTCBBuffer = &idleTaskBuffer;
    *ppxIdleTaskStackBuffer = idleTaskStack;
    *pulIdleTaskStackSize = STACK_SIZE;
}

//
// vApplicationStackOverflowHook
//
void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
    while(1);
}

//
// Main
//
void main(void)
{
    //
    // Initialize device clock and peripherals
    //
    Device_init();

    //
    // Boot CPU2 core
    //
#ifdef _FLASH
    Device_bootCPU2(BOOTMODE_BOOT_TO_FLASH_SECTOR0);
#else
    Device_bootCPU2(BOOTMODE_BOOT_TO_M0RAM);
#endif

    //
    // Initialize GPIO and configure the GPIO pin as a push-pull output
    //
    Device_initGPIO();
    GPIO_setPadConfig(DEVICE_GPIO_PIN_LED1, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_LED1, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_LED2, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_LED2, GPIO_DIR_MODE_OUT);

    //
    // Configure CPU2 to control the LED GPIO
    //
    GPIO_setControllerCore(DEVICE_GPIO_PIN_LED2, GPIO_CORE_CPU2);

    //
    // Initialize PIE and clear PIE registers. Disables CPU interrupts.
    //
    Interrupt_initModule();

    //
    // Initialize the PIE vector table with pointers to the shell Interrupt
    // Service Routines (ISR).
    //
    Interrupt_initVectorTable();

    //
    // Create a mutex to guard the LED.
    //
    ledSemaphore = xSemaphoreCreateMutex();

    if(ledSemaphore != NULL)
    {
        //
        // Create tasks to toggle LEDs at varying frequency
        //
        if(xTaskCreate(ledTask1Func, (const char *)"ledTask1", LED_TASK1_STACK_SIZE, NULL,
                       (tskIDLE_PRIORITY + LED_TASK1_PRIORITY), &ledTask1) != pdTRUE)
        {
            ESTOP0;
        }

        if(xTaskCreate(ledTask2Func, (const char *)"ledTask2", LED_TASK2_STACK_SIZE, NULL,
                       (tskIDLE_PRIORITY + LED_TASK2_PRIORITY), &ledTask2) != pdTRUE)
        {
            ESTOP0;
        }

        //
        // Clear any IPC flags if set already
        //
        IPC_clearFlagLtoR(IPC_CPU1_L_CPU2_R, IPC_FLAG_ALL);

        //
        // Synchronize both the cores.
        //
        IPC_sync(IPC_CPU1_L_CPU2_R, IPC_FLAG31);

        //
        // Start the scheduler.  This should not return.
        //
        vTaskStartScheduler();
    }

    //
    // In case the scheduler returns for some reason, loop forever.
    //
    for(;;)
    {
    }
}

//
// led Task1 Func - Task function for led task 1
//
void ledTask1Func(void *pvParameters)
{
    portTickType wakeTime;
    uint32_t taskDuration;

    while(1)
    {
        if(xSemaphoreTake(ledSemaphore, portMAX_DELAY) == pdPASS)
        {
            //
            // Get the current tick count.
            //
            wakeTime = xTaskGetTickCount();
            taskDuration = xTaskGetTickCount() + LED_TASK1_DURATION;
            while(taskDuration > xTaskGetTickCount())
            {
                //
                // Toggle LED1 for the entire task duration
                //
                GPIO_togglePin(DEVICE_GPIO_PIN_LED1);

                //
                // Delay until LED_TASK1_BLINK_DELAY
                //
                vTaskDelayUntil(&wakeTime, LED_TASK1_BLINK_DELAY);
            }

            ctrTask1++;

            //
            // Give semaphore
            //
            xSemaphoreGive(ledSemaphore);

            //
            // Put the task in blocked state
            //
            vTaskDelay(LED_TASK_BLOCK_TICKS);
        }
    }
}

//
// led Task2 Func - Task function for led task 2
//
void ledTask2Func(void *pvParameters)
{
    portTickType wakeTime;
    uint32_t taskDuration;

    while(1)
    {
        if(xSemaphoreTake(ledSemaphore, portMAX_DELAY) == pdPASS)
        {
            //
            // Get the current tick count
            //
            wakeTime = xTaskGetTickCount();
            taskDuration = xTaskGetTickCount() + LED_TASK2_DURATION;
            while(taskDuration > xTaskGetTickCount())
            {
                //
                // Toggle LED1 for the entire task duration
                //
                GPIO_togglePin(DEVICE_GPIO_PIN_LED1);

                //
                // Delay until LED_TASK2_BLINK_DELAY ticks
                //
                vTaskDelayUntil(&wakeTime, LED_TASK2_BLINK_DELAY);
            }

            //
            // Increment counter
            //
            ctrTask2++;

            //
            // Give semaphore
            //
            xSemaphoreGive(ledSemaphore);

            //
            // Put the task in blocked state
            //
            vTaskDelay(LED_TASK_BLOCK_TICKS);
        }
    }
}


//
// End of File
//
