/************************************************************************/
/* includes                                                             */
/************************************************************************/

#include <asf.h>
#include <string.h>
#include "arm_math.h"
#include "ili9341.h"
#include "lvgl.h"
#include "touch/touch.h"
#include "aps2/aps2.h"
#include "aps2/ecg.h"
#include "logo.h"
#include "health.h"

/************************************************************************/
/* STATIC                                                               */
/************************************************************************/

/*A static or global variable to store the buffers*/
static lv_disp_buf_t disp_buf;

/*Static or global buffer(s). The second buffer is optional*/
static lv_color_t buf_1[LV_HOR_RES_MAX * LV_VER_RES_MAX];

static  lv_obj_t * labelMenu;
static  lv_obj_t * labelPower;

/************************************************************************/
/* RTOS                                                                 */
/************************************************************************/

#define TASK_LCD_STACK_SIZE          (1024*6/sizeof(portSTACK_TYPE))
#define TASK_LCD_PRIORITY            (tskIDLE_PRIORITY)

#define TASK_APS2_STACK_SIZE         (1024*6/sizeof(portSTACK_TYPE))
#define TASK_APS2_PRIORITY           (tskIDLE_PRIORITY)

#define TASK_MAIN_STACK_SIZE         (1024*6/sizeof(portSTACK_TYPE))
#define TASK_MAIN_PRIORITY           (tskIDLE_PRIORITY)

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) {	}
}

extern void vApplicationIdleHook(void) { }

extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {  configASSERT( ( volatile void * ) NULL ); }
	
typedef struct  {
	uint32_t year;
	uint32_t month;
	uint32_t day;
	uint32_t week;
	uint32_t hour;
	uint32_t minute;
	uint32_t second;
} calendar;
volatile char flag_rtc = 0;

SemaphoreHandle_t xSemaphore;

/************************************************************************/
/* STATIC                                                               */
/************************************************************************/
static  lv_obj_t * labelHour;
static  lv_obj_t * labelMin;




/************************************************************************/
/* ASP2 - NAO MEXER!                                                    */
/************************************************************************/

QueueHandle_t xQueueOx;
TimerHandle_t xTimer;
volatile int g_ecgCnt = 0;
volatile int g_ecgDelayCnt = 0;
volatile int g_ecgDelayValue = 0;
const int g_ecgSize =  sizeof(ecg)/sizeof(ecg[0]);


/************************************************************************/
/* handler                                                               */
/************************************************************************/

static void menu_handler(lv_obj_t * obj, lv_event_t event) {
	if(event == LV_EVENT_CLICKED) {
		printf("Clicked\n");
	}
	else if(event == LV_EVENT_VALUE_CHANGED) {
		printf("Toggled\n");
	}
}

static void power_handler(lv_obj_t * obj, lv_event_t event) {
	if(event == LV_EVENT_CLICKED) {
		printf("Clicked\n");
	}
	else if(event == LV_EVENT_VALUE_CHANGED) {
		printf("Toggled\n");
	}
}

/************************************************************************/
/* RTC                                                                */
/************************************************************************/

void RTC_Handler(void)
{
	uint32_t ul_status = rtc_get_status(RTC);

	/* seccond tick	*/
	if ((ul_status & RTC_SR_SEC) == RTC_SR_SEC) {
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xSemaphoreGiveFromISR(xSemaphore, &xHigherPriorityTaskWoken);
	}
	
	/* Time or date alarm */
	if ((ul_status & RTC_SR_ALARM) == RTC_SR_ALARM) {
		//flag_rtc = 1;
	}
	
	rtc_clear_status(RTC, RTC_SCCR_SECCLR);
	rtc_clear_status(RTC, RTC_SCCR_ALRCLR);
	rtc_clear_status(RTC, RTC_SCCR_ACKCLR);
	rtc_clear_status(RTC, RTC_SCCR_TIMCLR);
	rtc_clear_status(RTC, RTC_SCCR_CALCLR);
	rtc_clear_status(RTC, RTC_SCCR_TDERRCLR);
}

void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type){
	/* Configura o PMC */
	pmc_enable_periph_clk(ID_RTC);

	/* Default RTC configuration, 24-hour mode */
	rtc_set_hour_mode(rtc, 0);

	/* Configura data e hora manualmente */
	rtc_set_date(rtc, t.year, t.month, t.day, t.week);
	rtc_set_time(rtc, t.hour, t.minute, t.second);

	/* Configure RTC interrupts */
	NVIC_DisableIRQ(id_rtc);
	NVIC_ClearPendingIRQ(id_rtc);
	NVIC_SetPriority(id_rtc, 4);
	NVIC_EnableIRQ(id_rtc);

	/* Ativa interrupcao via alarme */
	rtc_enable_interrupt(rtc,  irq_type);
}

/************************************************************************/
/* lvgl                                                                 */
/************************************************************************/

lv_oxi(void) {
	lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
	
	lv_obj_t * img1 = lv_img_create(lv_scr_act(), NULL);
	lv_img_set_src(img1, &logo);
	lv_obj_align(img1, NULL, LV_ALIGN_IN_TOP_LEFT, 5, 30);
	
	lv_obj_t * health_txt = lv_img_create(lv_scr_act(), NULL);
	lv_img_set_src(health_txt, &health);
	lv_obj_align(health_txt, NULL, LV_ALIGN_IN_TOP_LEFT, 50, 40);
	
	
	// cria botao de tamanho 60x60 redondo do MENU
	lv_obj_t * btnMenu = lv_btn_create(lv_scr_act(), NULL);
	lv_obj_set_event_cb(btnMenu, menu_handler);
	lv_obj_set_width(btnMenu, 100);  lv_obj_set_height(btnMenu, 40);

	// alinha no canto esquerdo e desloca um pouco para cima e para direita
	lv_obj_align(btnMenu, NULL, LV_ALIGN_IN_BOTTOM_RIGHT, -10, -10);
	
	// altera a cor de fundo, borda do botão criado para PRETO
	lv_obj_set_style_local_bg_color(btnMenu, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
	lv_obj_set_style_local_border_color(btnMenu, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLUE );
	lv_obj_set_style_local_border_width(btnMenu, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);
	
	labelMenu = lv_label_create(btnMenu, NULL);
	lv_label_set_recolor(labelMenu, true);
	lv_label_set_text(labelMenu, "#00000   BEGIN  #");
	
	// cria botao de tamanho 60x60 redondo do POWER
	lv_obj_t * btnPower = lv_btn_create(lv_scr_act(), NULL);
	lv_obj_set_event_cb(btnPower, power_handler);
	lv_obj_set_width(btnPower, 30);  lv_obj_set_height(btnPower, 30);

	// alinha no canto esquerdo e desloca um pouco para cima e para direita
	lv_obj_align(btnPower, NULL, LV_ALIGN_IN_TOP_LEFT, 10, 10);

	// altera a cor de fundo, borda do botão criado para PRETO
	lv_obj_set_style_local_bg_color(btnPower, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
	lv_obj_set_style_local_border_color(btnPower, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
	lv_obj_set_style_local_border_width(btnPower, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
	 
	labelPower = lv_label_create(btnPower, NULL);
	lv_label_set_recolor(labelPower, true);
	lv_label_set_text(labelPower, "#00000 [  " LV_SYMBOL_POWER "  |#");
	
	
	labelHour = lv_label_create(lv_scr_act(), NULL);
	lv_obj_align(labelHour, NULL, LV_ALIGN_IN_TOP_MID, -13 , 10);
	//lv_obj_set_style_local_text_font(labelHour, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &dseg30);
	lv_obj_set_style_local_text_color(labelHour, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
	//lv_label_set_text_fmt(labelHour, "%02d", 17);
	
	labelMin = lv_label_create(lv_scr_act(), NULL);
	lv_obj_align(labelMin, NULL, LV_ALIGN_IN_TOP_MID, 6 , 10);
	//lv_obj_set_style_local_text_font(labelMin, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &dseg30);
	lv_obj_set_style_local_text_color(labelMin, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

	lv_obj_t * labelBat;
	labelBat = lv_label_create(lv_scr_act(), NULL);
	lv_obj_align(labelBat, NULL, LV_ALIGN_IN_TOP_RIGHT, -5 , 5);
	//lv_obj_set_style_local_text_font(labelHour, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &dseg30);
	lv_obj_set_style_local_text_color(labelBat, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
	lv_label_set_text_fmt(labelBat, "%d %", 17);// nao sei pq o % nao aparece
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

static void task_lcd(void *pvParameters) {

	lv_oxi();
	for (;;)  {
		lv_tick_inc(50);
		lv_task_handler();
		vTaskDelay(50);
	}
}

static void task_main(void *pvParameters) {

	char ox;
	for (;;)  {
		
		if ( xQueueReceive( xQueueOx, &ox, 0 )) {
			//printf("ox: %d \n", ox);
		}
		
		vTaskDelay(25);
	}
}

static void task_clock(void *pvParameters) {
	
	
	calendar rtc_initial = {2021, 5, 7, 18, 10, 59 ,1};
	RTC_init(RTC, ID_RTC, rtc_initial, RTC_IER_SECEN);
	
	int flag_pisca = 0;
	
	uint32_t hour;
	uint32_t minute;
	uint32_t second;
	
	UNUSED(pvParameters);

	while(1){
		
		if( xSemaphoreTake(xSemaphore, ( TickType_t ) 10 / portTICK_PERIOD_MS) == pdTRUE ){
			rtc_get_time(RTC, &hour, &minute, &second);
			if(flag_pisca == 0){
				lv_label_set_text_fmt(labelHour, "%02d:", hour);
				lv_label_set_text_fmt(labelMin, "%02d", minute);
				flag_pisca = 1;
			}
			else if(flag_pisca == 1){
				lv_label_set_text_fmt(labelHour, "%02d", hour);
				lv_label_set_text_fmt(labelMin, "%02d", minute);
				flag_pisca = 0;
			}
		}
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
	xSemaphore = xSemaphoreCreateBinary();

	if (xTaskCreate(task_lcd, "LCD", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create lcd task\r\n");
	}
	
	if (xTaskCreate(task_clock, "RTC", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create RTC task\r\n");
	}
	
	if (xTaskCreate(task_aps2, "APS2", TASK_APS2_STACK_SIZE, NULL, TASK_APS2_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create APS task\r\n");
	}
	
	if (xTaskCreate(task_main, "main", TASK_MAIN_STACK_SIZE, NULL, TASK_MAIN_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create Main task\r\n");
	}
	
	/* Start the scheduler. */
	vTaskStartScheduler();

	/* RTOS n?o deve chegar aqui !! */
	while(1){ }
}
