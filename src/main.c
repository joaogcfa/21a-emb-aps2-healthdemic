/************************************************************************/
/* includes                                                             */
/************************************************************************/

#include <asf.h>
#include <string.h>
#include "arm_math.h"
#include "ili9341.h"
#include "lvgl.h"
#include "touch/touch.h"
#include "ecg.h"

/************************************************************************/
/* STATIC                                                               */
/************************************************************************/

/*A static or global variable to store the buffers*/
static lv_disp_buf_t disp_buf;

/*Static or global buffer(s). The second buffer is optional*/
static lv_color_t buf_1[LV_HOR_RES_MAX * LV_VER_RES_MAX];

/************************************************************************/
/* RTOS                                                                 */
/************************************************************************/

#define TASK_LCD_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_LCD_STACK_PRIORITY            (tskIDLE_PRIORITY)

#define TASK_APS2_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_APS2_STACK_PRIORITY            (tskIDLE_PRIORITY)

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
  printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
  for (;;) {	}
}

extern void vApplicationIdleHook(void) { }

extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
  configASSERT( ( volatile void * ) NULL );
}


/************************************************************************/
/* ASP2                                                                 */
/************************************************************************/



/************************************************************************/
/* lvgl                                                                 */
/************************************************************************/

static void event_handler(lv_obj_t * obj, lv_event_t event) {
  if(event == LV_EVENT_CLICKED) {
    printf("Clicked\n");
  }
  else if(event == LV_EVENT_VALUE_CHANGED) {
    printf("Toggled\n");
  }
}

int ser1_data[32];
lv_chart_series_t * ser1;
lv_obj_t * chart;

void lv_ex_btn_1(void) {
  
  chart = lv_chart_create(lv_scr_act(), NULL);
  lv_obj_set_size(chart, 300, 75);
  lv_obj_align(chart, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);   /*Show lines and points too*/
  lv_chart_set_range(chart, 0, 4095);
  lv_chart_set_point_count(chart, 128);
  lv_chart_set_div_line_count(chart, 0, 0);
  //lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  

  lv_chart_series_t * ser1 = lv_chart_add_series(chart, LV_COLOR_RED);
  lv_chart_set_ext_array(chart, ser1, ser1_data, 128);
  //lv_chart_init_points(chart, ser1, 0);
  
  lv_chart_refresh(chart); /*Required after direct set*/
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

static void task_lcd(void *pvParameters) {
  int px, py;

  lv_ex_btn_1();

  for (;;)  {
    lv_tick_inc(50);
    lv_task_handler();
    vTaskDelay(50);
  }
}

/************************************************************************/
/* NAO MEXER!                                                           */
/************************************************************************/

#define ECG_CHANGE_TIME 10000

QueueHandle_t xQueueOx;
TimerHandle_t xTimer;
volatile int g_ecgCnt = 0;
volatile int g_ecgDelayCnt = 0;
volatile int g_ecgDelayValue = 0;

//! DAC channel used for test
#define DACC_CHANNEL        0 // (PB13)
//! DAC register base for test
#define DACC_BASE           DACC
//! DAC ID for test
#define DACC_ID             ID_DACC

#define DACC_ANALOG_CONTROL (DACC_ACR_IBCTLCH0(0x02) | DACC_ACR_IBCTLCH1(0x02))

void TC0_Handler(void) {
  volatile uint32_t ul_dummy;

  ul_dummy = tc_get_status(TC0, 0);

  int ecgSize = sizeof(ecg)/sizeof(ecg[0]) - 20;
  if (g_ecgCnt >= ecgSize) {
    g_ecgCnt = 0;
    g_ecgDelayCnt = g_ecgDelayValue;
  }

  if (g_ecgDelayCnt > 0) {
    g_ecgDelayCnt--;
    } else {
    g_ecgCnt++;
  }
  dacc_write_conversion_data(DACC_BASE, ecg[g_ecgCnt], DACC_CHANNEL);
}

static void config_DAC(void) {
  /* Enable clock for DACC */
  sysclk_enable_peripheral_clock(DACC_ID);

  /* Reset DACC registers */
  dacc_reset(DACC_BASE);

  /* Half word transfer mode */
  dacc_set_transfer_mode(DACC_BASE, 0);
  dacc_set_osr(DACC_BASE, DACC_CHANNEL, 5 );

  /* Enable output channel DACC_CHANNEL */
  dacc_enable_channel(DACC_BASE, DACC_CHANNEL);

  /* Set up analog current */
  dacc_set_analog_control(DACC_BASE, DACC_ANALOG_CONTROL);
}

void TC_init_modificado(Tc * TC, int ID_TC, int TC_CHANNEL, int freq){
  uint32_t ul_div;
  uint32_t ul_tcclks;
  uint32_t ul_sysclk = sysclk_get_cpu_hz();

  pmc_enable_periph_clk(ID_TC);
  tc_find_mck_divisor(freq, ul_sysclk, &ul_div, &ul_tcclks, ul_sysclk);
  tc_init(TC, TC_CHANNEL, TC_CMR_TCCLKS_TIMER_CLOCK2 | TC_CMR_CPCTRG);
  tc_write_rc(TC, TC_CHANNEL, (ul_sysclk / 16) / freq);

  NVIC_SetPriority(ID_TC, 4);
  NVIC_EnableIRQ((IRQn_Type) ID_TC);
  tc_enable_interrupt(TC, TC_CHANNEL, TC_IER_CPCS);

  tc_start(TC, TC_CHANNEL);
}

void vTimerEcgCallback( TimerHandle_t xTimer ) {
  // delay entre ECGs de [200, 500]
  g_ecgDelayValue = rand() % (500 + 1 - 200) + 200;
  printf("%d \n", g_ecgDelayValue);
}

static void task_aps2(void *pvParameters) {
  
  g_ecgDelayValue = 200;
  config_DAC();
  TC_init_modificado(TC0, ID_TC0, 0, 1000);
  xTimer = xTimerCreate("ECG", pdMS_TO_TICKS(ECG_CHANGE_TIME), pdTRUE, (void *) 0, vTimerEcgCallback);
  xTimerStart( xTimer, 0 );
  
  // oxiometria
  char ox;
  float theta = 0;

  for (;;)  {
    
    // Oxiometria: sin entre 88 e 100
    if (theta > 2*PI) {
      theta = 0;
    }
    ox = (char) ((arm_sin_f32(theta) + 1.0)*12.0/2.0 + 88.0);
    theta = theta + 0.3;
    xQueueSend(xQueueOx, &ox, 100);

    vTaskDelay(2000);
  }
}


/************************************************************************/
/*                                                                      */
/************************************************************************/


static void task_main(void *pvParameters) {

  int n = 128;
  int i = 0;
  for(i = 0; i < 32; i++) {
    ser1_data[i] = 0;
  }

  for (;;)  {
    
    char ox;
    if ( xQueueReceive( xQueueOx, &ox, 0 )) {
      printf("%d \n", ox);
    }
    
    for(i = n-1; i >0 ; i--) {
      ser1_data[i] = ser1_data[i-1];
    }
    ser1_data[0] = ecg[g_ecgCnt];
    //lv_chart_set_next(chart, ser1,  ecg[ecgCnt]);
    
    lv_chart_refresh(chart);
    
    vTaskDelay(25);
    
  }
}

/************************************************************************/
/* configs                                                              */
/************************************************************************/

static void configure_lcd(void) {
  /**LCD pin configure on SPI*/
  pio_configure_pin(LCD_SPI_MISO_PIO, LCD_SPI_MISO_FLAGS);  //
  pio_configure_pin(LCD_SPI_MOSI_PIO, LCD_SPI_MOSI_FLAGS);
  pio_configure_pin(LCD_SPI_SPCK_PIO, LCD_SPI_SPCK_FLAGS);
  pio_configure_pin(LCD_SPI_NPCS_PIO, LCD_SPI_NPCS_FLAGS);
  pio_configure_pin(LCD_SPI_RESET_PIO, LCD_SPI_RESET_FLAGS);
  pio_configure_pin(LCD_SPI_CDS_PIO, LCD_SPI_CDS_FLAGS);
  
}

static void configure_console(void) {
  const usart_serial_options_t uart_serial_options = {
    .baudrate = USART_SERIAL_EXAMPLE_BAUDRATE,
    .charlength = USART_SERIAL_CHAR_LENGTH,
    .paritytype = USART_SERIAL_PARITY,
    .stopbits = USART_SERIAL_STOP_BIT,
  };

  /* Configure console UART. */
  stdio_serial_init(CONSOLE_UART, &uart_serial_options);

  /* Specify that stdout should not be buffered. */
  setbuf(stdout, NULL);
}

/************************************************************************/
/* port lvgl                                                            */
/************************************************************************/

void my_flush_cb(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p) {
  ili9341_set_top_left_limit(area->x1, area->y1);   ili9341_set_bottom_right_limit(area->x2, area->y2);
  ili9341_copy_pixels_to_screen(color_p,  (area->x2 - area->x1) * (area->y2 - area->y1));
  
  /* IMPORTANT!!!
  * Inform the graphics library that you are ready with the flushing*/
  lv_disp_flush_ready(disp_drv);
}

bool my_input_read(lv_indev_drv_t * drv, lv_indev_data_t*data) {
  int px, py, pressed;
  
  if (readPoint(&px, &py)) {
    data->state = LV_INDEV_STATE_PR;
  }
  else {
    data->state = LV_INDEV_STATE_REL;
  }
  
  data->point.x = px;
  data->point.y = py;
  return false; /*No buffering now so no more data read*/
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/
int main(void) {
  /* board and sys init */
  board_init();
  sysclk_init();
  configure_console();

  /* LCd int */
  configure_lcd();
  ili9341_init();
  configure_touch();
  ili9341_backlight_on();
  
  /*LittlevGL init*/
  lv_init();
  lv_disp_drv_t disp_drv;                 /*A variable to hold the drivers. Can be local variable*/
  lv_disp_drv_init(&disp_drv);            /*Basic initialization*/
  lv_disp_buf_init(&disp_buf, buf_1, NULL, LV_HOR_RES_MAX * LV_VER_RES_MAX);  /*Initialize `disp_buf` with the buffer(s) */
  disp_drv.buffer = &disp_buf;            /*Set an initialized buffer*/
  disp_drv.flush_cb = my_flush_cb;        /*Set a flush callback to draw to the display*/
  lv_disp_t * disp;
  disp = lv_disp_drv_register(&disp_drv); /*Register the driver and save the created display objects*/
  
  /* Init input on LVGL */
  lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);      /*Basic initialization*/
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_input_read;
  /*Register the driver in LVGL and save the created input device object*/
  lv_indev_t * my_indev = lv_indev_drv_register(&indev_drv);
  
  xQueueOx = xQueueCreate(32, sizeof(char));

  if (xTaskCreate(task_lcd, "LCD", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
    printf("Failed to create lcd task\r\n");
  }
  
  if (xTaskCreate(task_aps2, "APS2", TASK_APS2_STACK_SIZE, NULL, TASK_APS2_STACK_PRIORITY, NULL) != pdPASS) {
    printf("Failed to create APS task\r\n");
  }
  
  if (xTaskCreate(task_main, "main", TASK_APS2_STACK_SIZE, NULL, TASK_APS2_STACK_PRIORITY, NULL) != pdPASS) {
    printf("Failed to create Main task\r\n");
  }
  
  /* Start the scheduler. */
  vTaskStartScheduler();

  /* RTOS n?o deve chegar aqui !! */
  while(1){ }
}
