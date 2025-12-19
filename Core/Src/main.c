/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define NUM_SEATS 8

typedef enum {
    EMPTY = 0,
    OCCUPIED = 1,
	ONLY_BAG = 2,
	TEMP_LEAVE = 3,
    MISUSE      = 4
} SeatState;

typedef struct {
    SeatState state;
    uint32_t state_enter_time;
    uint8_t misuse;

    uint8_t temp_leave_req;
    uint32_t temp_leave_req_time;
} SeatInfo;

GPIO_TypeDef* LED_PORT[NUM_SEATS] = {
    GPIOC, GPIOC, GPIOC, GPIOC, GPIOC, GPIOC, GPIOC, GPIOC
};

uint16_t LED_PIN[NUM_SEATS] = {
    GPIO_PIN_4,
    GPIO_PIN_5,
    GPIO_PIN_6,
    GPIO_PIN_8,
    GPIO_PIN_9,
    GPIO_PIN_10,
    GPIO_PIN_11,
    GPIO_PIN_12
};


GPIO_TypeDef* SW_PORT[NUM_SEATS] = {
    GPIOB, GPIOB, GPIOB, GPIOB, GPIOB, GPIOB,
    GPIOB, GPIOB
};

uint16_t SW_PIN[NUM_SEATS] = {
    GPIO_PIN_0,
    GPIO_PIN_1,
    GPIO_PIN_2,
    GPIO_PIN_4,
    GPIO_PIN_5,
    GPIO_PIN_6,
    GPIO_PIN_7,
    GPIO_PIN_8
};


/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
volatile uint32_t spi_rx_count = 0;

SeatInfo seats[NUM_SEATS];

#define TEMP_LEAVE_LIMIT 8000
#define ONLY_BAG_LIMIT 5000
#define TEMP_LEAVE_REQ_TIMEOUT 3000



uint8_t received_states[NUM_SEATS] = {
    0, 0, 0, 0, 0, 0, 0, 0
};

char uart_rx_buf[32];
uint8_t uart_rx_index = 0;

uint8_t spi_rx_buf[NUM_SEATS];
SemaphoreHandle_t spiRxSem;


typedef struct {
    uint8_t seat_id;
    uint8_t led_on;
} LedMessage;

QueueHandle_t ledQueue;
SemaphoreHandle_t ledMutex;



/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */


/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_rx;

UART_HandleTypeDef huart2;


/* USER CODE BEGIN PV */

void initSeats(void)
{
    for (int i = 0; i < NUM_SEATS; i++) {
        seats[i].state = EMPTY;
        seats[i].state_enter_time = 0;
        seats[i].misuse = 0;

        seats[i].temp_leave_req = 0;
        seats[i].temp_leave_req_time = 0;
    }
}


static void SetState(int i, SeatState next, uint32_t now)
{
    if (seats[i].state == next) return;

    seats[i].state = next;
    seats[i].state_enter_time = now;

    seats[i].misuse = (next == MISUSE) ? 1 : 0;

    LedMessage msg = { .seat_id = (uint8_t)i, .led_on = (next == OCCUPIED) ? 1 : 0 };
    xQueueSend(ledQueue, &msg, 0);
}

void updateSeatFSM(void)
{
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < NUM_SEATS; i++)
    {
        SeatState ext = (SeatState)received_states[i];
        SeatState cur = seats[i].state;

        if (ext != EMPTY && ext != OCCUPIED && ext != ONLY_BAG) {
            ext = EMPTY;
        }

        if (seats[i].temp_leave_req &&
            (now - seats[i].temp_leave_req_time > TEMP_LEAVE_REQ_TIMEOUT))
        {
            seats[i].temp_leave_req = 0;
        }

        if (cur == MISUSE)
        {
            if (ext == EMPTY)     { SetState(i, EMPTY, now); }
            else if (ext == OCCUPIED) { SetState(i, OCCUPIED, now); }
            continue;
        }


        if (cur == TEMP_LEAVE)
        {
            if (ext == OCCUPIED) { SetState(i, OCCUPIED, now); continue; }
            if (ext == EMPTY)    { SetState(i, EMPTY, now);    continue; }

            if (now - seats[i].state_enter_time > TEMP_LEAVE_LIMIT) {
                SetState(i, ONLY_BAG, now);
            }
            continue;
        }

        if (cur == ONLY_BAG)
        {

            if (ext == OCCUPIED) { SetState(i, OCCUPIED, now); continue; }
            if (ext == EMPTY)    { SetState(i, EMPTY, now);    continue; }
            if (now - seats[i].state_enter_time > ONLY_BAG_LIMIT) {
                SetState(i, MISUSE, now);
            }
            continue;
        }

        if (ext == OCCUPIED)
        {
            SetState(i, OCCUPIED, now);
            continue;
        }

        if (ext == ONLY_BAG)
        {
            if (seats[i].temp_leave_req && cur == OCCUPIED)
            {
                seats[i].temp_leave_req = 0;
                SetState(i, TEMP_LEAVE, now);
            }
            else
            {
                SetState(i, ONLY_BAG, now);
            }
            continue;
        }

        if (ext == EMPTY)
        {
            SetState(i, EMPTY, now);
            seats[i].temp_leave_req = 0;
            continue;
        }
    }
}


void simulateReceivingData(void)
{
    static uint32_t last = 0;
    uint32_t now = HAL_GetTick();

    if (now - last > 5000)
    {
        last = now;

        for (int i = 0; i < NUM_SEATS; i++) {
            received_states[i] = rand() % 4;
        }
    }
}

void printSeatStatus(void)
{
    char msg[80];
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < NUM_SEATS; i++)
    {
        snprintf(msg, sizeof(msg),
                 "[%lu ms] Seat %d: state=%d, misuse=%d\r\n",
                 now, i, seats[i].state, seats[i].misuse);

        HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 10);
    }

    char nl[] = "\r\n";
    HAL_UART_Transmit(&huart2, (uint8_t*)nl, strlen(nl), 10);
}

void Task_Receive(void *argument)
{
    for (;;)
    {

        if (xSemaphoreTake(spiRxSem, portMAX_DELAY) == pdTRUE)
        {
            for (int i = 0; i < NUM_SEATS; i++)
            {
                received_states[i] = spi_rx_buf[i];
            }
            HAL_SPI_Receive_DMA(&hspi1, spi_rx_buf, NUM_SEATS);
        }
    }
}

void Task_LED(void *argument)
{
    LedMessage msg;

    while (1)
    {
        if (xQueueReceive(ledQueue, &msg, portMAX_DELAY) == pdTRUE)
        {
            xSemaphoreTake(ledMutex, portMAX_DELAY);

            if (msg.led_on)
                HAL_GPIO_WritePin(LED_PORT[msg.seat_id], LED_PIN[msg.seat_id], GPIO_PIN_SET);
            else
                HAL_GPIO_WritePin(LED_PORT[msg.seat_id], LED_PIN[msg.seat_id], GPIO_PIN_RESET);

            xSemaphoreGive(ledMutex);
        }
    }
}

void Task_Switch(void *argument)
{
    uint8_t prev[NUM_SEATS] = {0};

    while(1)
    {
        for (int i = 0; i < NUM_SEATS; i++)
        {
            uint8_t cur = (HAL_GPIO_ReadPin(SW_PORT[i], SW_PIN[i]) == GPIO_PIN_RESET);

            if (cur && !prev[i])
            {
                seats[i].temp_leave_req = 1;
                seats[i].temp_leave_req_time = HAL_GetTick();

                char buf[64];
                sprintf(buf, "Seat %d: leave intent set\r\n", i);
                HAL_UART_Transmit(&huart2, (uint8_t*)buf, strlen(buf), 10);
            }

            prev[i] = cur;
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}


void Task_FSM(void *argument)
{

    for (;;)
    {
        updateSeatFSM();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void Task_UART(void *argument)
{
    for(;;)
    {
        printSeatStatus();
        vTaskDelay(pdMS_TO_TICKS(3000));

    }
}




/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
void Task_Receive(void *argument);
void Task_FSM(void *argument);
void Task_UART(void *argument);
void parseCommand(char *cmd);
void Task_LED(void *argument);
void Task_Switch(void *argument);
static void SetState(int i, SeatState next, uint32_t now);


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */
  initSeats();

  ledQueue = xQueueCreate(20, sizeof(LedMessage));
  ledMutex = xSemaphoreCreateMutex();

  spiRxSem = xSemaphoreCreateBinary();

  HAL_SPI_Receive_DMA(&hspi1, spi_rx_buf, NUM_SEATS);

  //테스트용
  HAL_UART_Transmit(&huart2, (uint8_t*)"start", strlen("start"), 1);
  HAL_UART_Receive_IT(&huart2, uart_rx_buf, 1);



  /* USER CODE END 2 */

  /* Init scheduler */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */

   xTaskCreate(Task_Receive, "ReceiveTask", 256, NULL, 2, NULL);
   xTaskCreate(Task_FSM,     "FSMTask",     512, NULL, 3, NULL);
   xTaskCreate(Task_UART,    "UARTTask",    512, NULL, 1, NULL);
   xTaskCreate(Task_LED,    "LEDTask",    256, NULL, 2, NULL);
   xTaskCreate(Task_Switch, "SwitchTask", 256, NULL, 2, NULL);


   vTaskStartScheduler();
   HAL_UART_Transmit(&huart2, (uint8_t*)"SCHED FAIL\r\n", 12, 10);

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  /*
	  simulateReceivingData(); // 임시 데이터생성값
	  updateSeatFSM();
	  printSeatStatus();

	  HAL_Delay(100);
	  */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_SLAVE;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_HARD_INPUT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */
  HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PA4 */
  /*
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  */

  /*Configure GPIO pins : PC4 PC5 PC6 PC7
                           PC8 PC9 PC10 PC11
                           PC12 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 PB2 PB10
                           PB11 PB12 PB4 PB5
                           PB6 PB7 PB8 PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_10
                          |GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PB13 PB14 PB15 */
  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  // 초기 상태: LED OFF (Active LOW)
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        spi_rx_count++;   // ← ISR에서 카운트만 증가

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        xSemaphoreGiveFromISR(spiRxSem, &xHigherPriorityTaskWoken);

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

    }
}


/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
