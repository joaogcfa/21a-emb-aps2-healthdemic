// 	   Valor salvo de oxigenação/batimento

// O usuário deve ser capaz de:

//     Salvar o valor em um instante de oxigenação e batimento

//     Arrumar o bpm de 3 digitos
//	   Toggle do botao de aviso




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
#include "aviso.h"
#include "white.h"

/************************************************************************/
/* STATIC                                                               */
/************************************************************************/

/*A static or global variable to store the buffers*/
static lv_disp_buf_t disp_buf;

/*Static or global buffer(s). The second buffer is optional*/
static lv_color_t buf_1[LV_HOR_RES_MAX * LV_VER_RES_MAX];

static  lv_obj_t * labelMenu;
static  lv_obj_t * labelPower;
lv_obj_t * img2;

/************************************************************************/
/* RTOS                                                                 */
/************************************************************************/

#define TASK_LCD_STACK_SIZE          (1024*6/sizeof(portSTACK_TYPE))
#define TASK_LCD_PRIORITY            (tskIDLE_PRIORITY)

#define TASK_APS2_STACK_SIZE         (1024*6/sizeof(portSTACK_TYPE))
#define TASK_APS2_PRIORITY           (tskIDLE_PRIORITY)

#define TASK_MAIN_STACK_SIZE         (1024*6/sizeof(portSTACK_TYPE))
#define TASK_MAIN_PRIORITY           (tskIDLE_PRIORITY)

#define AFEC AFEC1
#define AFEC_ID ID_AFEC1
#define AFEC_CHANNEL 6

#define CHAR_DATA_LEN 250

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

typedef struct {
	int value;
} adcData;

typedef struct {
	int ecg;
	int bpm;
} ecgInfo;

SemaphoreHandle_t xSemaphore;
QueueHandle_t xQueueECG;
QueueHandle_t xQueueEcgInfo;
volatile uint32_t g_ul_value = 0;
volatile bool g_is_conversion_done = false;
volatile int g_dT;

/************************************************************************/
/* STATIC                                                               */
/************************************************************************/
static  lv_obj_t * labelHour;
static  lv_obj_t * labelMin;
static lv_obj_t * labelOx;
static lv_obj_t * labelEcg;

LV_FONT_DECLARE(arial60);
LV_FONT_DECLARE(arial20);

volatile int flag_inicia = 0;
volatile int pagina = 0;


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
		flag_inicia = !flag_inicia;
	}
	else if(event == LV_EVENT_VALUE_CHANGED) {
		printf("Toggled\n");
	}
}

static void disable_handler(lv_obj_t * obj, lv_event_t event) {
	if(event == LV_EVENT_CLICKED) {
		printf("Clicked\n");
		lv_img_set_src(img2, &white);
	}
	// 	else if(event == LV_EVENT_VALUE_CHANGED) {
	// 		printf("Toggled\n");
	// 	}
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
/* TC                                                                   */
/************************************************************************/

void TC_init(Tc * TC, int ID_TC, int TC_CHANNEL, int freq){
	uint32_t ul_div;
	uint32_t ul_tcclks;
	uint32_t ul_sysclk = sysclk_get_cpu_hz();

	/* Configura o PMC */
	/* O TimerCounter � meio confuso
	o uC possui 3 TCs, cada TC possui 3 canais
	TC0 : ID_TC0, ID_TC1, ID_TC2
	TC1 : ID_TC3, ID_TC4, ID_TC5
	TC2 : ID_TC6, ID_TC7, ID_TC8
	*/
	pmc_enable_periph_clk(ID_TC);

	/** Configura o TC para operar em  4Mhz e interrup�c�o no RC compare */
	tc_find_mck_divisor(freq, ul_sysclk, &ul_div, &ul_tcclks, ul_sysclk);
	tc_init(TC, TC_CHANNEL, ul_tcclks | TC_CMR_CPCTRG);
	tc_write_rc(TC, TC_CHANNEL, (ul_sysclk / ul_div) / freq);

	/* Configura e ativa interrup�c�o no TC canal 0 */
	/* Interrup��o no C */
	NVIC_SetPriority(ID_TC, 4);
	NVIC_EnableIRQ((IRQn_Type) ID_TC);
	tc_enable_interrupt(TC, TC_CHANNEL, TC_IER_CPCS);

	/* Inicializa o canal 0 do TC */
	tc_start(TC, TC_CHANNEL);
}

void TC3_Handler(void){
	volatile uint32_t ul_dummy;
	
	/* Selecina canal e inicializa convers�o */
	afec_channel_enable(AFEC, AFEC_CHANNEL);
	afec_start_software_conversion(AFEC);
	
	/****************************************************************
	* Devemos indicar ao TC que a interrup��o foi satisfeita.
	******************************************************************/
	ul_dummy = tc_get_status(TC1, 0);
	
	/* Avoid compiler warning */
	UNUSED(ul_dummy);
	
	/*flag_led1 = 1;*/
}


/************************************************************************/
/* RTT                                                                  */
/************************************************************************/

void RTT_Handler(void)
{
	uint32_t ul_status;

	/* Get RTT status - ACK */
	ul_status = rtt_get_status(RTT);

	/* IRQ due to Time has changed */
	if ((ul_status & RTT_SR_RTTINC) == RTT_SR_RTTINC) {
		//printf("%d\n", g_dT);
		g_dT ++;
		
	}

	/* IRQ due to Alarm */
	if ((ul_status & RTT_SR_ALMS) == RTT_SR_ALMS) {
		// pin_toggle(LED_PIO, LED_IDX_MASK);    // BLINK Led
		//f_rtt_alarme = true;                  // flag RTT alarme
	}
}

static float get_time_rtt(){
	uint ul_previous_time = rtt_read_timer_value(RTT);
}

static void RTT_init(uint16_t pllPreScale, uint32_t IrqNPulses)
{
	uint32_t ul_previous_time;

	/* Configure RTT for a 1 second tick interrupt */
	rtt_sel_source(RTT, false);
	rtt_init(RTT, pllPreScale);
	
	ul_previous_time = rtt_read_timer_value(RTT);
	while (ul_previous_time == rtt_read_timer_value(RTT));
	
	rtt_write_alarm_time(RTT, IrqNPulses+ul_previous_time);

	/* Enable RTT interrupt */
	NVIC_DisableIRQ(RTT_IRQn);
	NVIC_ClearPendingIRQ(RTT_IRQn);
	NVIC_SetPriority(RTT_IRQn, 4);
	NVIC_EnableIRQ(RTT_IRQn);
	rtt_enable_interrupt(RTT, RTT_MR_ALMIEN | RTT_MR_RTTINCIEN);
}

/************************************************************************/
/* lvgl                                                                 */
/************************************************************************/

int ser1_data[CHAR_DATA_LEN];
lv_obj_t * chart;
lv_chart_series_t * ser1;
lv_obj_t * labelFloor;
lv_obj_t * head;
lv_obj_t * body;
lv_obj_t * page1;
lv_obj_t * page2;


static void event_handler(lv_obj_t * obj, lv_event_t event) {
	if(event == LV_EVENT_VALUE_CHANGED) {
		const char * txt = lv_btnmatrix_get_active_btn_text(obj);
		printf("%s was pressed\n", txt);
	}
}


void lv_page1(void) {
	
	page1 = lv_page_create(body, NULL);
	lv_obj_set_width(page1, 320);  lv_obj_set_height(page1, 190);
	
	 lv_obj_t * btnDis = lv_btn_create(page1, NULL);
	lv_obj_set_event_cb(btnDis, disable_handler);
	lv_obj_set_width(btnDis, 50);  lv_obj_set_height(btnDis, 50);

	lv_obj_align(btnDis, lv_scr_act(), LV_ALIGN_IN_LEFT_MID, 130, -10);

	// altera a cor de fundo, borda do bot?o criado para branco para esconder
	//lv_obj_set_style_local_bg_color(btnDis, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT,  LV_COLOR_WHITE);
	lv_obj_set_style_local_border_color(btnDis, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
	lv_obj_set_style_local_border_width(btnDis, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
	
	img2 = lv_img_create(page1, NULL);
	lv_obj_align(img2, lv_scr_act(), LV_ALIGN_IN_LEFT_MID, 130, -10);
	lv_img_set_src(img2, &white);

	labelOx = lv_label_create(page1, NULL);
	lv_obj_align(labelOx, lv_scr_act(), LV_ALIGN_IN_LEFT_MID, 10 , 0);
	lv_obj_set_style_local_text_font(labelOx, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &arial60);
	lv_obj_set_style_local_text_color(labelOx, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x40CFDF));
	lv_label_set_text_fmt(labelOx, "0%%");

	lv_obj_t * labelLegendaOx;
	labelLegendaOx = lv_label_create(page1, NULL);
	lv_obj_align(labelLegendaOx, lv_scr_act(), LV_ALIGN_IN_LEFT_MID, 130 , 32);
	lv_obj_set_style_local_text_font(labelLegendaOx, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &arial20);
	lv_obj_set_style_local_text_color(labelLegendaOx, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x40CFDF));
	lv_label_set_text_fmt(labelLegendaOx, "spO2");

	// lv_obj_t * labelEcg;
	labelEcg = lv_label_create(page1, NULL);
	lv_obj_align(labelEcg, lv_scr_act(), LV_ALIGN_IN_RIGHT_MID, -100 , 0);
	lv_obj_set_style_local_text_font(labelEcg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &arial60);
	lv_obj_set_style_local_text_color(labelEcg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xED0B00));
	lv_label_set_text_fmt(labelEcg, "0");

	lv_obj_t * labelLegendaEcg;
	labelLegendaEcg = lv_label_create(page1, NULL);
	lv_obj_align(labelLegendaEcg, lv_scr_act(), LV_ALIGN_IN_RIGHT_MID, -30 , 30);
	lv_obj_set_style_local_text_font(labelLegendaEcg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &arial20);
	lv_obj_set_style_local_text_color(labelLegendaEcg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xED0B00));
	lv_label_set_text_fmt(labelLegendaEcg, "bpm");
	
	// chart
	chart = lv_chart_create(page1, NULL);
	lv_obj_set_size(chart, 200, 70);
	lv_obj_align(chart, lv_scr_act(), LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
	lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
	lv_chart_set_range(chart, 0, 4095);
	lv_chart_set_point_count(chart, CHAR_DATA_LEN);
	lv_chart_set_div_line_count(chart, 0, 0);
	lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);

	ser1 = lv_chart_add_series(chart, LV_COLOR_RED);
	lv_chart_set_ext_array(chart, ser1, ser1_data, CHAR_DATA_LEN);
	lv_obj_set_style_local_line_width(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 1);
	
	lv_obj_set_style_local_bg_opa(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_OPA_50); /*Max. opa.*/
	lv_obj_set_style_local_bg_grad_dir(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
	lv_obj_set_style_local_bg_main_stop(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 255);    /*Max opa on the top*/
	lv_obj_set_style_local_bg_grad_stop(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 0);      /*Transparent on the bottom*/
	
	// cria botao de tamanho 60x60 redondo do MENU
	lv_obj_t * btnMenu = lv_btn_create(page1, NULL);
	lv_obj_set_event_cb(btnMenu, menu_handler);
	lv_obj_set_width(btnMenu, 100);  lv_obj_set_height(btnMenu, 40);

	// alinha no canto esquerdo e desloca um pouco para cima e para direita
	lv_obj_align(btnMenu, lv_scr_act(), LV_ALIGN_IN_BOTTOM_RIGHT, -10, -10);
	
	// altera a cor de fundo, borda do bot?o criado para PRETO
	lv_obj_set_style_local_bg_color(btnMenu, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x40CFDF) );
	lv_obj_set_style_local_border_color(btnMenu, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x40CFDF) );
	lv_obj_set_style_local_border_width(btnMenu, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);
	
	labelMenu = lv_label_create(btnMenu, NULL);
	lv_label_set_recolor(labelMenu, true);
	lv_label_set_text(labelMenu, "#FFFFFF   BEGIN  #");

	// cria botao de tamanho 60x60 redondo do MENU
	// lv_obj_t * btnSave = lv_btn_create(page1, NULL);
	// lv_obj_set_event_cb(btnSave, menu_handler);
	// lv_obj_set_width(btnSave, 100);  lv_obj_set_height(btnSave, 40);

	// // alinha no canto esquerdo e desloca um pouco para cima e para direita
	// lv_obj_align(btnSave, lv_scr_act(), LV_ALIGN_IN_BOTTOM_RIGHT, -10, -10);
	
	// // altera a cor de fundo, borda do bot?o criado para PRETO
	// lv_obj_set_style_local_bg_color(btnSave, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x40CFDF) );
	// lv_obj_set_style_local_border_color(btnSave, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x40CFDF) );
	// lv_obj_set_style_local_border_width(btnSave, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);
	
	// labelSave = lv_label_create(btnSave, NULL);
	// lv_label_set_recolor(labelSave, true);
	// lv_label_set_text(labelSave, "#FFFFFF   SAVE  #");

	// labelFloor = lv_label_create(lv_scr_act(), NULL);
	// lv_obj_align(labelFloor, NULL, LV_ALIGN_IN_TOP_RIGHT, -50 , 30);
	// lv_obj_set_style_local_text_color(labelFloor, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
}

void lv_page2(void) {
	
	page2 = lv_page_create(body, NULL);
	lv_obj_set_width(page2, 320);  lv_obj_set_height(page2, 190);

	lv_obj_t * label;

	label = lv_label_create(page2, NULL);
	lv_label_set_text(label, "PAGINA 2");
}

void lv_body(void) {
	body =  lv_cont_create(lv_scr_act(), NULL);
	lv_obj_align(body, head, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
	lv_obj_set_width(body, 320);  lv_obj_set_height(body, 190);
	lv_cont_set_layout(body, LV_LAYOUT_OFF);

}

/************************************************************************/
/* handlers setas que trocam página                                     */
/************************************************************************/

static void arrowLeft_handler(lv_obj_t * obj, lv_event_t event) {
	if(event == LV_EVENT_CLICKED) {
		printf("Clicked\n");
		if(pagina == 1) {
			lv_page1();
			pagina--;
		}
	}
	else if(event == LV_EVENT_VALUE_CHANGED) {
		printf("Toggled\n");
	}
}

static void arrowRight_handler(lv_obj_t * obj, lv_event_t event) {
	if(event == LV_EVENT_CLICKED) {
		printf("Clicked\n");
		if(pagina == 0) {
			lv_page2();
			pagina++;
		}
	}
	else if(event == LV_EVENT_VALUE_CHANGED) {
		printf("Toggled\n");
	}
}


void lv_head(void) {
	head = lv_cont_create(lv_scr_act(), NULL);
	lv_obj_align(head, NULL, LV_ALIGN_IN_TOP_LEFT, 0, 0);
	lv_obj_set_width(head, 320);  lv_obj_set_height(head, 75);
	lv_cont_set_layout(head, LV_LAYOUT_OFF);

	lv_obj_t * img1 = lv_img_create(head, NULL);
	lv_img_set_src(img1, &logo);
	lv_obj_align(img1, NULL, LV_ALIGN_IN_TOP_LEFT, 5, 30);
	lv_obj_t * health_txt = lv_img_create(head, NULL);
	lv_img_set_src(health_txt, &health);
	lv_obj_align(health_txt, NULL, LV_ALIGN_IN_TOP_LEFT, 50, 40);
// cria botao de tamanho 60x60 redondo do POWER
	lv_obj_t * btnPower = lv_btn_create(head, NULL);
	lv_obj_set_event_cb(btnPower, power_handler);
	lv_obj_set_width(btnPower, 30);  lv_obj_set_height(btnPower, 30);

	// alinha no canto esquerdo e desloca um pouco para cima e para direita
	lv_obj_align(btnPower, NULL, LV_ALIGN_IN_TOP_LEFT, 10, 10);

	// altera a cor de fundo, borda do bot?o criado para PRETO
	lv_obj_set_style_local_bg_color(btnPower, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
	lv_obj_set_style_local_border_color(btnPower, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
	lv_obj_set_style_local_border_width(btnPower, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
	
	labelPower = lv_label_create(btnPower, NULL);
	lv_label_set_recolor(labelPower, true);
	lv_label_set_text(labelPower, "#00000 [  " LV_SYMBOL_POWER "  |#");
	
	
	labelHour = lv_label_create(head, NULL);
	lv_obj_align(labelHour, NULL, LV_ALIGN_IN_TOP_MID, -13 , 10);
	//lv_obj_set_style_local_text_font(labelHour, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &dseg30);
	lv_obj_set_style_local_text_color(labelHour, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
	//lv_label_set_text_fmt(labelHour, "%02d", 17);
	
	labelMin = lv_label_create(head, NULL);
	lv_obj_align(labelMin, NULL, LV_ALIGN_IN_TOP_MID, 4 , 10);
	//lv_obj_set_style_local_text_font(labelMin, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &dseg30);
	lv_obj_set_style_local_text_color(labelMin, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

	lv_obj_t * labelBat;
	labelBat = lv_label_create(head, NULL);
	lv_obj_align(labelBat, NULL, LV_ALIGN_IN_TOP_RIGHT, -5 , 5);
	//lv_obj_set_style_local_text_font(labelHour, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, &dseg30);
	lv_obj_set_style_local_text_color(labelBat, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
	lv_label_set_text_fmt(labelBat, "%d %%", 17);



	lv_obj_t * arrowRight = lv_btn_create(head, NULL);
	lv_obj_set_event_cb(arrowRight, arrowRight_handler);
	lv_obj_set_width(arrowRight, 40);  lv_obj_set_height(arrowRight, 40);

	// alinha no canto esquerdo e desloca um pouco para cima e para direita
	lv_obj_align(arrowRight, NULL, LV_ALIGN_IN_BOTTOM_RIGHT, -10, 0);

	// altera a cor de fundo, borda do bot?o criado para PRETO
	lv_obj_set_style_local_bg_color(arrowRight, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
	lv_obj_set_style_local_border_color(arrowRight, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
	lv_obj_set_style_local_border_width(arrowRight, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
	
	lv_obj_t * labelRight = lv_label_create(arrowRight, NULL);
	lv_label_set_recolor(labelRight, true);
	lv_label_set_text(labelRight, "#00000 " LV_SYMBOL_RIGHT " #");

	//FLECHA PRA ESQUERDA

	lv_obj_t * arrowLeft = lv_btn_create(head, NULL);
	lv_obj_set_event_cb(arrowLeft, arrowLeft_handler);
	lv_obj_set_width(arrowLeft, 40);  lv_obj_set_height(arrowLeft, 40);

	// alinha no canto esquerdo e desloca um pouco para cima e para direita
	lv_obj_align(arrowLeft, NULL, LV_ALIGN_IN_BOTTOM_RIGHT, -55, 0);

	// altera a cor de fundo, borda do bot?o criado para PRETO
	lv_obj_set_style_local_bg_color(arrowLeft, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
	lv_obj_set_style_local_border_color(arrowLeft, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE );
	lv_obj_set_style_local_border_width(arrowLeft, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
	
	lv_obj_t * labelLeft = lv_label_create(arrowLeft, NULL);
	lv_label_set_recolor(labelLeft, true);
	lv_label_set_text(labelLeft, "#00000 " LV_SYMBOL_LEFT " #");


}

/************************************************************************/
/* CALLBACK                                                             */
/************************************************************************/

static void AFEC_Callback(void){
	g_ul_value = afec_channel_get_value(AFEC, AFEC_CHANNEL);
	g_is_conversion_done = true;
	adcData adc;
	adc.value = g_ul_value;
	xQueueSendFromISR(xQueueECG, &adc, 0);
}



static void config_AFEC(Afec *afec, uint32_t afec_id, uint32_t afec_channel, afec_callback_t callback){
	/*************************************
	* Ativa e configura AFEC
	*************************************/
	/* Ativa AFEC - 0 */
	afec_enable(afec);

	/* struct de configuracao do AFEC */
	struct afec_config afec_cfg;

	/* Carrega parametros padrao */
	afec_get_config_defaults(&afec_cfg);

	/* Configura AFEC */
	afec_init(afec, &afec_cfg);

	/* Configura trigger por software */
	afec_set_trigger(afec, AFEC_TRIG_SW);

	/*** Configuracao espec�fica do canal AFEC ***/
	struct afec_ch_config afec_ch_cfg;
	afec_ch_get_config_defaults(&afec_ch_cfg);
	afec_ch_cfg.gain = AFEC_GAINVALUE_0;
	afec_ch_set_config(afec, afec_channel, &afec_ch_cfg);

	/*
	* Calibracao:
	* Because the internal ADC offset is 0x200, it should cancel it and shift
	down to 0.
	*/
	afec_channel_set_analog_offset(afec, afec_channel, 0x200);

	/***  Configura sensor de temperatura ***/
	struct afec_temp_sensor_config afec_temp_sensor_cfg;

	afec_temp_sensor_get_config_defaults(&afec_temp_sensor_cfg);
	afec_temp_sensor_set_config(afec, &afec_temp_sensor_cfg);
	
	/* configura IRQ */
	afec_set_callback(afec, afec_channel,	callback, 1);
	NVIC_SetPriority(afec_id, 4);
	NVIC_EnableIRQ(afec_id);
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

static void task_lcd(void *pvParameters) {
	lv_head();
	lv_body();
	lv_page1();
	
	for (;;)  {
		lv_tick_inc(50);
		lv_task_handler();
		vTaskDelay(50);
	}
}

static void task_main(void *pvParameters) {
	ecgInfo ecg_;
	int flag = 0;
	int flag2 = 0;
	char ox;
	
	for (;;)  {
		if(flag_inicia){
			
			if ( xQueueReceive( xQueueOx, &ox, 0 )) {
				lv_label_set_text_fmt(labelOx, "%d%%", ox);
				if(ox < 90){
					lv_img_set_src(img2, &aviso);
				}
			}

			if (xQueueReceive( xQueueEcgInfo, &(ecg_), ( TickType_t )  500 / portTICK_PERIOD_MS)) {
				/*printf("%d\n", ecg_.ecg);*/
				printf("BPM: %d\n", ecg_.bpm);
				
				if (ecg_.bpm > 0){
					lv_label_set_text_fmt(labelEcg, "%d", ecg_.bpm);
				}
				else{
					lv_label_set_text_fmt(labelEcg, "0", ecg_.bpm);
				}
				
				lv_chart_set_next(chart, ser1, ecg_.ecg);
				lv_obj_set_style_local_size(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_DPI/150);
				lv_chart_refresh(chart);
			}
			flag = 1;
		}
		else if (flag){
			lv_label_set_text_fmt(labelOx, "%02d%%", 0);
			lv_label_set_text_fmt(labelEcg, "%02d", 0);
			lv_chart_set_next(chart, ser1, 0);
			lv_obj_set_style_local_size(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_DPI/150);
			lv_chart_refresh(chart);
			pmc_sleep(SAM_PM_SMODE_SLEEP_WFI);
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

static void task_process(void *pvParameters) {
	xQueueECG = xQueueCreate(250, sizeof(int));
	TC_init(TC1, ID_TC3, 0, 250);
	config_AFEC(AFEC, AFEC_ID, AFEC_CHANNEL, AFEC_Callback);
	
	adcData adc;
	ecgInfo ecg_;
	int flag = 0;
	int bpm;
	
	for (;;)  {
		uint16_t pllPreScale = (int) (((float) 32768) / 1000.0);
		uint32_t irqRTTvalue = 0.001;
		
		// reinicia RTT para gerar um novo IRQ
		RTT_init(pllPreScale, irqRTTvalue);
		//f_rtt_alarme = 5;
		if (xQueueReceive( xQueueECG, &(adc), ( TickType_t )  100 / portTICK_PERIOD_MS)) {
			/*printf("%d\n", adc.value);*/
			
			if (adc.value > 3280  && flag == 0){
				printf("%d: %d ms\n", adc.value, g_dT);
				// come�amos a contar novamente
				double valor = 60000/g_dT;
				bpm = (int) valor;
				ecg_.bpm = bpm;
				// 				printf("BPM: %d\n", ecg_.bpm);
				g_dT = 0;
				flag = 1;
			}
			if(adc.value <3280){
				flag = 0;
			}
			ecg_.ecg = adc.value;
			xQueueSend(xQueueEcgInfo, &ecg_, 0);
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
	xQueueEcgInfo = xQueueCreate(32, sizeof(ecgInfo));

	if (xTaskCreate(task_lcd, "LCD", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create lcd task\r\n");
	}

	if (xTaskCreate(task_process, "process", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create process task\r\n");
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
