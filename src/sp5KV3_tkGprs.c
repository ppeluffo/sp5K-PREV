/*
 * sp5K_tkGprs2.c
 *
 *  Created on: 21/05/2014
 *      Author: root
 *
 *  Creo una maquina de estados con 4 estados. c/u es una nueva maquina de estados.
 *  Los eventos los evaluo de acuerdo al estado.
 *
 *  V2.0.10 @ 2014-11-28
 *  Monitoreo del SQE.
 *  - Agrego un nuevo valor al wrkMode, MONITOR.
 *    Al pasar a este modo, tkData es lo mismo que service, pero tkGprs NO.
 *
 *
 */
#include <sp5KV3.h>

static char gprs_printfBuff[CHAR256];
TimerHandle_t dialTimer;

static void pv_GPRSprintExitMsg(char *code);
static void pv_GPRSloadParameters(void);
static void pv_dialTimerCallback( TimerHandle_t pxTimer );
char *cb_strstr(Peripheral_Control_t *UART, const char substring[], int *pos);
static s08 pv_GPRSrspIs(const char *rsp, const size_t *pos);
static void pv_GPRSprintRsp(void);
// Funciones de reconfiguracion
static void pv_GPRSprocessServerClock(void);
static u08 pv_GPRSprocessPwrMode(void);
static u08 pv_GPRSprocessTimerPoll(void);
static u08 pv_GPRSprocessTimerDial(void);
static u08 pv_GPRSprocessPwrSave(void);
static u08 pv_GPRSprocessAch(u08 channel);
static u08 pv_GPRSprocessDch(u08 channel);

// Estados
typedef enum { gST_INIT = 0,
				gST_OFF,
				gST_ONoffline,
				gST_ONopenSocket,
				gST_ONinitFrame,
				gST_ONdata

} t_tkGprs_state;

// Subestados.
typedef enum {
				// Estado OFF
				gSST_OFF_Entry = 0,
	            gSST_OFF_Standby,
				gSST_OFF_prenderModem_01,
				gSST_OFF_prenderModem_02,
				gSST_OFF_prenderModem_03,
				gSST_OFF_prenderModem_04,
				gSST_OFF_prenderModem_05,
				gSST_OFF_prenderModem_06,

	            // Estado ONoffline
	            gSST_ONoffline_Entry,
				gSST_ONoffline_Config_01,
				gSST_ONoffline_Config_02,
				gSST_ONoffline_Net_01,
				gSST_ONoffline_Net_02,
				gSST_ONoffline_Net_03,
				gSST_ONoffline_Net_04,
				gSST_ONoffline_Sqe_01,
				gSST_ONoffline_Sqe_02,
				gSST_ONoffline_Sqe_03,
				gSST_ONoffline_IP_01,
				gSST_ONoffline_IP_02,
				gSST_ONoffline_IP_03,
				gSST_ONoffline_IP_04,
				gSST_ONoffline_IP_05,

	            // Estado gST_ONopenSocket
	            gSST_ONopenSocket_Entry,
				gSST_ONopenSocket_01,
				gSST_ONopenSocket_02,
				gSST_ONopenSocket_03,
				gSST_ONopenSocket_04,
				gSST_ONopenSocket_05,
				gSST_ONopenSocket_06,
				gSST_ONopenSocket_07,

				// Estado gST_ONinitFrame,
				gSST_ONinitframe_Entry,
				gSST_ONinitframe_01,
				gSST_ONinitframe_02,
				gSST_ONinitframe_03,
				gSST_ONinitframe_04,
				gSST_ONinitframe_05,
				gSST_ONinitframe_06,

				// Estado gST_ONdata
				gSST_ONdata_Entry,
				gSST_ONdata_01,
				gSST_ONdata_02,
				gSST_ONdata_03,
				gSST_ONdata_04,
				gSST_ONdata_05,
				gSST_ONdata_06,
				gSST_ONdata_07,
				gSST_ONdata_08,
				gSST_ONdata_09,
				gSST_ONdata_10,
				gSST_ONdata_11

} t_tkGprs_subState;

// Eventos
typedef enum {

	evRELOADCONFIG = 0,
	evSTART2DIAL,
	evCTIMER_IS_0,
	evATRSP_OK,			// GPRS ATrsp == OK

	evPTRYES_IS_0,
	evQTRYES_IS_0,
	ev_CREGRSP_OK,		// CREGrsp == +CREG 0,1
	ev_WKMONITOR_SQE,
	ev_IPRSP_OK,		// GPRS E2IPArsp == OK
	ev_BAND_OK,			// GPRS BAND RIGHT

	ev_SOCKRSP_CONNECT,	// GPRS AT*E2IPO == CONNECT
	ev_GPRSRSP_ERROR,	// La respuesta del modem es ERROR
	evCINITS_IS_0,		// Deberia seguir reintentando INITS
	evCINITS_IS_1,		// Llegue al limite de reintentos de INITS
	ev_SOCKET_IS_OPEN,
	ev_INITRSP_OK,

	ev_MEM_IS_EMPTY,
	ev_PWR_CONT,
	ev_GPRRSP_RXOK,
	ev_TX_A_RCD

} t_tkGprs_eventos;

#define gEVENT_COUNT		20

static s08 gEventos[gEVENT_COUNT];

typedef enum { RSP_NONE = 0,
	             RSP_OK,
				 RSP_ERROR,
	             RSP_READY,
	             RSP_CREG,
	             RSP_APN,
				 RSP_IPOK,
	             RSP_CONNECT,
				 RSP_INIT,
				 RSP_RXOK,

} t_tkGprs_responses;

t_tkGprs_responses GPRSrsp;

// Acciones Generales
static void GPRS_getNextEvent(u08 state);
static void gTR_reloadConfig(void);

// Estado OFF
static void SM_off(void);

static int gTR_o00(void);	// Subestado OFF
static int gTR_o01(void);
static int gTR_o02(void);
static int gTR_o03(void);
static int gTR_o04(void);
static int gTR_o05(void);
static int gTR_o06(void);
static int gTR_o07(void);
static int gTR_o08(void);
static int gTR_o09(void);
static int gTR_o10(void);
static int gTR_o11(void);
static int gTR_o12(void);

// Estado ON-OFFLINE
static void SM_onOffline(void);

static int gTR_c01(void);	// Subestado CONFIGURE
static int gTR_c02(void);
static int gTR_c03(void);
static int gTR_n01(void);	// Subestado NET
static int gTR_n02(void);
static int gTR_n03(void);
static int gTR_n04(void);
static int gTR_n05(void);
static int gTR_n06(void);
static int gTR_n07(void);
static int gTR_s01(void);	// Subestado SQE
static int gTR_s02(void);
static int gTR_s03(void);
static int gTR_s04(void);
static int gTR_s05(void);
static int gTR_i01(void);	// Subestado IP
static int gTR_i02(void);
static int gTR_i03(void);
static int gTR_i04(void);
static int gTR_i05(void);
static int gTR_i06(void);
static int gTR_i07(void);
static int gTR_i08(void);
static int gTR_i09(void);
static int gTR_i10(void);

// Estado ON SOCKET
static void SM_onOpenSocket(void);

static int gTR_k01(void);	// Subestado SOCKET
static int gTR_k02(void);
static int gTR_k03(void);
static int gTR_k04(void);
static int gTR_k05(void);
static int gTR_k06(void);
static int gTR_k07(void);
static int gTR_k08(void);
static int gTR_k09(void);
static int gTR_k10(void);
static int gTR_k11(void);
static int gTR_k12(void);
static int gTR_k13(void);
static int gTR_k14(void);

// Estado ON INITFRAME
static void SM_onInitFrame(void);

static int gTR_f01(void);	// Subestado INIT FRAME
static int gTR_f02(void);
static int gTR_f03(void);
static int gTR_f04(void);
static int gTR_f05(void);
static int gTR_f06(void);
static int gTR_f07(void);
static int gTR_f08(void);
static int gTR_f09(void);
static int gTR_f10(void);
static int gTR_f11(void);
static int gTR_f12(void);

// Estado ON DATA
static void SM_onData(void);

static int gTR_d01(void);	// Subestado DATA
static int gTR_d02(void);
static int gTR_d03(void);
static int gTR_d04(void);
static int gTR_d05(void);
static int gTR_d06(void);
static int gTR_d07(void);
static int gTR_d08(void);
static int gTR_d09(void);
static int gTR_d10(void);
static int gTR_d11(void);
static int gTR_d12(void);
static int gTR_d13(void);
static int gTR_d14(void);
static int gTR_d15(void);
static int gTR_d16(void);
static int gTR_d17(void);
static int gTR_d18(void);
static int gTR_d19(void);
static int gTR_d20(void);

u08 tkGprs_state, tkGprs_subState;
static u32 tickCount;	// para usar en los mensajes del debug.

static struct {
	s08 arranque;
	s08 sendTail;	// Las ventanas de datos pueden no ser completas y con esta flag lo indico.
	s08 msgReload;
	s08 allowsSleep;
	s08 start2dial;
	s08 modemPrendido;
	s08 gsmBandOK;
	s08 memoryEmpty;
	s08 txNewRcd;

} GPRS_flags;

static struct {
	u08 HWtryes;
	u08 SWtryes;
	u08 cTimer;
	u08 pTryes;
	u08 qTryes;
	u32 secs2dial;
	u08 cInits;
	u08 txRcdsInFrame;

} GPRS_counters;
//-------------------------------------------------------------------------------------

#define MAX_HWTRYES		3
#define MAX_SWTRYES		3
#define MAX_CTIMERSW	5

//------------------------------------------------------------------------------------
void tkGprs(void * pvParameters)
{

( void ) pvParameters;
BaseType_t xResult;
uint32_t ulNotifiedValue;

	while ( !startTask )
		vTaskDelay( ( TickType_t)( 100 / portTICK_RATE_MS ) );

	snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("starting tkGprs..\r\n\0"));
	FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	tkGprs_state = gST_OFF;
	tkGprs_subState = gSST_OFF_Entry;
	GPRS_flags.arranque = TRUE;
	//
	// Arranco el timer de dial.
	if ( xTimerStart( dialTimer, 0 ) != pdPASS )
		u_panic(P_GPRS_TIMERSTART);
	//
	for( ;; )
	{
		u_clearWdg(WDG_GPRS);

		// Espero hasta 100ms por un mensaje.
		xResult = xTaskNotifyWait( 0x00, ULONG_MAX, &ulNotifiedValue, ((TickType_t) 100 / portTICK_RATE_MS ) );
		// Si llego un mensaje, prendo la flag correspondiente.
		if ( xResult == pdTRUE ) {
			if ( ( ulNotifiedValue & TKG_PARAM_RELOAD ) != 0 ) {
				GPRS_flags.msgReload = TRUE;
			}
		}

		// Analizo los eventos.
		GPRS_getNextEvent(tkGprs_state);

		// El manejar la FSM con un switch por estado y no por transicion me permite
		// priorizar las transiciones.
		// Luego de c/transicion debe venir un break así solo evaluo de a 1 transicion por loop.
		//
		switch ( tkGprs_state ) {
		case gST_OFF:
			SM_off();
			break;
		case gST_ONoffline:
			SM_onOffline();
			break;
		case gST_ONopenSocket:
			SM_onOpenSocket();
			break;
		case gST_ONinitFrame:
			SM_onInitFrame();
			break;
		case gST_ONdata:
			SM_onData();
			break;
		default:
			snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("tkGprs::ERROR state NOT DEFINED\r\n\0"));
			FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
			tkGprs_state = gST_OFF;
			tkGprs_subState = gSST_OFF_Entry;
			break;
		}

	}
}
//------------------------------------------------------------------------------------
static void GPRS_getNextEvent(u08 state)
{
// Evaluo todas las condiciones que generan los eventos que disparan las transiciones.
// Tenemos un array de eventos y todos se evaluan.
// Las condiciones que evaluo dependen del estado ya que no todas se deben dar siempre


u08 i;

	// Inicializo la lista de eventos.
	for ( i=0; i < gEVENT_COUNT; i++ ) {
		gEventos[i] = FALSE;
	}

	// MSG_RELOAD
	if ( GPRS_flags.msgReload == TRUE ) { gEventos[evRELOADCONFIG] = TRUE; }

	switch (state) {
	case gST_OFF:
		// Evaluo solo los eventos del estado OFF.
		// START2DIAL
		if ( GPRS_flags.start2dial == TRUE ) { gEventos[evSTART2DIAL] = TRUE; }
		// evCTIMER_IS_0
		if ( GPRS_counters.cTimer <= 0 ) { gEventos[evCTIMER_IS_0] = TRUE; }
		// evPTRYES_IS_0
		if ( GPRS_counters.pTryes <= 0 ) { gEventos[evPTRYES_IS_0] = TRUE; }
		// evQTRYES_IS_0
		if ( GPRS_counters.qTryes <= 0 ) { gEventos[evQTRYES_IS_0] = TRUE; }
		// evATRSP_OK
		if ( GPRSrsp == RSP_OK ) { gEventos[evATRSP_OK] = TRUE; }
		break;

	case gST_ONoffline:
		// Evaluo solo los eventos del estado ONoffline
		// evCTIMER_IS_0
		if ( GPRS_counters.cTimer <= 0 ) { gEventos[evCTIMER_IS_0] = TRUE; }
		// evPTRYES_IS_0
		if ( GPRS_counters.pTryes <= 0 ) { gEventos[evPTRYES_IS_0] = TRUE; }
		// evQTRYES_IS_0
		if ( GPRS_counters.qTryes <= 0 ) { gEventos[evQTRYES_IS_0] = TRUE; }
		// ev_CREGRSP_OK		CREGrsp == +CREG 0,1
		if ( GPRSrsp == RSP_CREG ) { gEventos[ev_CREGRSP_OK] = TRUE; }
		// ev_WKMONITOR_SQE
		if ( systemVars.wrkMode == WK_MONITOR_SQE ) { gEventos[ev_WKMONITOR_SQE] = TRUE; }
		// ev_IPRSP_OK			GPRS E2IPArsp == OK
		if ( GPRSrsp == RSP_IPOK ) { gEventos[ev_IPRSP_OK] = TRUE; }
		// ev_BAND_OK			NET gprsBand is correct.
		if ( GPRS_flags.gsmBandOK == TRUE  ) { gEventos[ev_BAND_OK] = TRUE; }
		break;

	case gST_ONopenSocket:
		// evCTIMER_IS_0
		if ( GPRS_counters.cTimer <= 0 ) { gEventos[evCTIMER_IS_0] = TRUE; }
		// evPTRYES_IS_0
		if ( GPRS_counters.pTryes <= 0 ) { gEventos[evPTRYES_IS_0] = TRUE; }
		// evQTRYES_IS_0
		if ( GPRS_counters.qTryes <= 0 ) { gEventos[evQTRYES_IS_0] = TRUE; }
		// ev_ev_SOCKRSP_CONNECT		// GPRS AT*E2IPO == CONNECT
		if ( GPRSrsp == RSP_CONNECT ) { gEventos[ev_SOCKRSP_CONNECT] = TRUE; }
		// ev_GPRSRSP_ERROR
		if ( GPRSrsp == RSP_ERROR ) { gEventos[ev_GPRSRSP_ERROR] = TRUE; }
		// evCINITS_IS_0
		if ( GPRS_counters.cInits <= 0 ) { gEventos[evCINITS_IS_0] = TRUE; }
		break;

	case gST_ONinitFrame:
		// evCINITS_IS_1
		if ( GPRS_counters.cInits == 1 ) { gEventos[evCINITS_IS_1] = TRUE; }
		// evCTIMER_IS_0
		if ( GPRS_counters.cTimer <= 0 ) { gEventos[evCTIMER_IS_0] = TRUE; }
		// ev_SOCKET_IS_OPEN
		if ( systemVars.dcd == 0 ) { gEventos[ev_SOCKET_IS_OPEN] = TRUE; }
		// ev_INITRSP_OK
		if ( GPRSrsp == RSP_INIT ) { gEventos[ev_INITRSP_OK] = TRUE; }
		// evPTRYES_IS_0
		if ( GPRS_counters.pTryes <= 0 ) { gEventos[evPTRYES_IS_0] = TRUE; }
		break;

	case gST_ONdata:
		// ev_MEM_IS_EMPTY
		if ( GPRS_flags.memoryEmpty == TRUE ) { gEventos[ev_MEM_IS_EMPTY] = TRUE; }
		// ev_PWR_CONT
		if ( systemVars.pwrMode == PWR_CONTINUO ) { gEventos[ev_PWR_CONT] = TRUE; }
		// evCTIMER_IS_0
		if ( GPRS_counters.cTimer <= 0 ) { gEventos[evCTIMER_IS_0] = TRUE; }
		// ev_SOCKET_IS_OPEN
		if ( systemVars.dcd == 0 ) { gEventos[ev_SOCKET_IS_OPEN] = TRUE; }
		// ev_GPRRSP_RXOK
		if ( GPRSrsp == RSP_RXOK ) { gEventos[ev_GPRRSP_RXOK] = TRUE; }
		// ev_TX_A_RCD
		if ( GPRS_flags.txNewRcd == TRUE ) { gEventos[ev_TX_A_RCD] = TRUE; }
		break;
	}
}
//------------------------------------------------------------------------------------
static void gTR_reloadConfig(void)
{
	// -> gST_Entry
	// Msg.reload

	tkGprs_state = gST_OFF;
	tkGprs_subState = gSST_OFF_Entry;

}
//------------------------------------------------------------------------------------
/*
 *  FUNCIONES DEL ESTADO OFF:
 *  El modem esta apagado y sale hasta quedar prendido
 *
 */
//------------------------------------------------------------------------------------
static void SM_off(void)
{
	// Maquina de estados del estado OFF.( MODEM APAGADO)

	if ( gEventos[evRELOADCONFIG] ) { gTR_reloadConfig(); return; }

	switch ( tkGprs_subState ) {
	case gSST_OFF_Entry:
		tkGprs_subState = gTR_o00();
		break;
	case gSST_OFF_Standby:
		//if ( gEventos[evRELOADCONFIG] ) { tkGprs_subState = gTR_o01(); break; }
		if ( gEventos[evSTART2DIAL] )  { tkGprs_subState = gTR_o02(); break; }
		break;
	case gSST_OFF_prenderModem_01:
		tkGprs_subState = gTR_o03();
		break;
	case gSST_OFF_prenderModem_02:
		tkGprs_subState = gTR_o04();
		break;
	case gSST_OFF_prenderModem_03:
		if ( gEventos[evCTIMER_IS_0] ) {
			tkGprs_subState = gTR_o05();
		} else {
			tkGprs_subState = gTR_o06();
		}
		break;
	case gSST_OFF_prenderModem_04:
		if ( gEventos[evATRSP_OK] ) {
			tkGprs_subState =gTR_o07();
		} else {
			tkGprs_subState = gTR_o08();
		}
		break;
	case gSST_OFF_prenderModem_05:
		if ( gEventos[evPTRYES_IS_0] ) {
			tkGprs_subState =gTR_o09();
		} else {
			tkGprs_subState = gTR_o10();
		}
		break;
	case gSST_OFF_prenderModem_06:
		if ( gEventos[evQTRYES_IS_0] ) {
			tkGprs_subState =gTR_o12();
		} else {
			tkGprs_subState = gTR_o11();
		}
		break;
	default:
		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\ntkGprs::ERROR sst_off: subState  (%d) NOT DEFINED\r\n\0"),tkGprs_subState);
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
		tkGprs_state = gST_OFF;
		tkGprs_subState = gSST_OFF_Entry;
		break;
	}
}
/*------------------------------------------------------------------------------------*/
static int gTR_o00(void)
{

	// Evento inicial. Solo salta al primer estado operativo.
	// Inicializo el sistema aqui
	// gST_INIT -> gSST_OFF_Standby

	pv_GPRSloadParameters();

	// Apago el modem y dejo
	// activo el pwr del modem para que no consuma
	MODEM_HWpwrOff();
	MODEM_SWswitchHIGH();
	vTaskDelay( (portTickType)( 500 / portTICK_RATE_MS ) );
	MODEM_HWpwrOn();

	vTaskDelay( (portTickType)( 500 / portTICK_RATE_MS ) );
	strncpy_P(systemVars.dlgIp, PSTR("000.000.000.000\0"),16);
	systemVars.csq = 0;
	systemVars.dbm = 0;
	//
	// Inicializamos las variables de trabajo.
	GPRS_flags.allowsSleep = TRUE;
	GPRS_flags.arranque = FALSE;
	GPRS_flags.msgReload = FALSE;
	GPRS_flags.start2dial = FALSE;
	GPRS_flags.modemPrendido = FALSE;

	pv_GPRSprintExitMsg("o00\0");
	return(gSST_OFF_Standby);
}
//------------------------------------------------------------------------------------
static int gTR_o01(void)
{
	// gSST_OFF_Standby ->  -> gST_Entry
	// Msg.reload

	tkGprs_state = gST_OFF;

	pv_GPRSprintExitMsg("o01\0");
	return(gSST_OFF_Entry);

}
//------------------------------------------------------------------------------------
static int gTR_o02(void)
{

	// gSST_OFF_Standby -> gSST_OFF_prenderModem_01

	GPRS_flags.start2dial = FALSE;
	GPRS_counters.qTryes = MAX_HWTRYES;

	pv_GPRSprintExitMsg("o02\0");
	return(gSST_OFF_prenderModem_01);
}
//------------------------------------------------------------------------------------
static int gTR_o03(void)
{
	// gSST_OFF_prenderModem_01 -> gSST_OFF_prenderModem_02
	// Prendo el modem HW y espero estabilizar la fuente

	MODEM_HWpwrOff();
	MODEM_SWswitchHIGH();
	vTaskDelay( (portTickType)( 500 / portTICK_RATE_MS ) );
	MODEM_HWpwrOn();
	vTaskDelay( (portTickType)( 1000 / portTICK_RATE_MS ) );

	// Voy a reintentar 3 veces de prender el modem HW
	GPRS_counters.pTryes = MAX_SWTRYES;

	pv_GPRSprintExitMsg("o03\0");
	return(gSST_OFF_prenderModem_02);
}
//------------------------------------------------------------------------------------
static int gTR_o04(void)
{

	// gSST_OFF_prenderModem_02 -> gSST_OFF_prenderModem_03
	// Hago un switch on/off y espero 5s que se prenda

	GPRS_counters.cTimer = 5;		// espera en c/intento de settle time

	// switch on/off: aplico un pulso HIGH->LOW->HIGH
	MODEM_SWswitchLOW();
	vTaskDelay( (portTickType)( 500 / portTICK_RATE_MS ) );
	MODEM_SWswitchHIGH();

	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		tickCount = xTaskGetTickCount();
		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR(".[%06lu] tkGprs: switch on/off [%d][%d]\r\n\0"), tickCount,GPRS_counters.qTryes,GPRS_counters.pTryes);
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}

	pv_GPRSprintExitMsg("o04\0");
	return(gSST_OFF_prenderModem_03);
}
//------------------------------------------------------------------------------------
static int gTR_o05(void)
{
	// gSST_OFF_prenderModem_03 -> gSST_OFF_prenderModem_04
	// Mando un comando AT para ver si el modem prendio.
	// Previo borro los buffers.

size_t pos;

	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_TX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT\r\0", sizeof("AT\r\0") );
	//FreeRTOS_write( &pdUART1, "->AT\r\n\0", sizeof("->AT\r\n\0") );
	vTaskDelay( (portTickType)( 1000 / portTICK_RATE_MS ) );
	pv_GPRSprintRsp();
	// Leo y Evaluo la respuesta al comando AT
	if ( pv_GPRSrspIs("OK\0", &pos ) == TRUE ) {
		GPRSrsp = RSP_OK;
	}

	pv_GPRSprintExitMsg("o05\0");
	return(gSST_OFF_prenderModem_04);
}
//------------------------------------------------------------------------------------
static int gTR_o06(void)
{
	// gSST_OFF_prenderModem_03 -> gSST_OFF_prenderModem_03
	// Espero 5s luego del sw on/off

	if ( GPRS_counters.cTimer > 0 ) {
		vTaskDelay( ( TickType_t)( 1000 / portTICK_RATE_MS ) );
		GPRS_counters.cTimer--;
	}

	//pv_GPRSprintExitMsg("o05\0");
	return(gSST_OFF_prenderModem_03);
}
//------------------------------------------------------------------------------------
static int gTR_o07(void)
{
	// gSST_OFF_prenderModem_04 -> EXIT STATE

	GPRS_flags.modemPrendido = TRUE;

	// CAMBIO DE ESTADO:
	tkGprs_state = gST_ONoffline;

	pv_GPRSprintExitMsg("o07\0");
	return(gSST_ONoffline_Entry);
}
//------------------------------------------------------------------------------------
static int gTR_o08(void)
{
	// gSST_OFF_prenderModem_04 -> gSST_OFF_prenderModem_05

	if ( GPRS_counters.pTryes > 0 ) {
		vTaskDelay( ( TickType_t)( 1000 / portTICK_RATE_MS ) );
		GPRS_counters.pTryes--;
	}

	pv_GPRSprintExitMsg("o08\0");
	return(gSST_OFF_prenderModem_05);
}
//------------------------------------------------------------------------------------
static int gTR_o09(void)
{
	// gSST_OFF_prenderModem_05 -> gSST_OFF_prenderModem_06

	if ( GPRS_counters.qTryes > 0 ) {
		GPRS_counters.qTryes--;
	}

	pv_GPRSprintExitMsg("o09\0");
	return(gSST_OFF_prenderModem_06);
}
//------------------------------------------------------------------------------------
static int gTR_o10(void)
{
	// gSST_OFF_prenderModem_05 -> gSST_OFF_prenderModem_02

	pv_GPRSprintExitMsg("o10\0");
	return(gSST_OFF_prenderModem_02);
}
//------------------------------------------------------------------------------------
static int gTR_o11(void)
{
	// gSST_OFF_prenderModem_06 -> gSST_OFF_prenderModem_01

	pv_GPRSprintExitMsg("o11\0");
	return(gSST_OFF_prenderModem_01);
}
//------------------------------------------------------------------------------------
static int gTR_o12(void)
{
	// gSST_OFF_prenderModem_06 ->  gSST_OFF_Standby

	pv_GPRSprintExitMsg("t12\0");
	return( gSST_OFF_Entry);
}
//------------------------------------------------------------------------------------
/*
 *  FUNCIONES DEL ESTADO ON-OFFLINE:
 *  El modem esta prendido y aqui se configura, se conecta a la red y se pide una IP.
 *
 */
//------------------------------------------------------------------------------------
static void SM_onOffline(void)
{
	// Maquina de estados del estado ON-OFFLINE.

	if ( gEventos[evRELOADCONFIG] ) { gTR_reloadConfig(); return; }

	switch ( tkGprs_subState ) {
	case gSST_ONoffline_Entry:
		tkGprs_subState = gTR_c01();
		break;
	case gSST_ONoffline_Config_01:
		tkGprs_subState = gTR_c02();
		break;
	case gSST_ONoffline_Config_02:
		if ( gEventos[ev_BAND_OK] ) {
			tkGprs_subState =gTR_n01();
		} else {
			tkGprs_subState = gTR_c03();
		}
		break;
	case gSST_ONoffline_Net_01:
		tkGprs_subState = gTR_n02();
		break;
	case gSST_ONoffline_Net_02:
		if ( gEventos[evCTIMER_IS_0] ) {
			tkGprs_subState =gTR_n03();
		} else {
			tkGprs_subState = gTR_n04();
		}
		break;
	case gSST_ONoffline_Net_03:
		if ( gEventos[ev_CREGRSP_OK] ) {
			tkGprs_subState =gTR_s01();
		} else {
			tkGprs_subState = gTR_n05();
		}
		break;
	case gSST_ONoffline_Net_04:
		if ( gEventos[evPTRYES_IS_0] ) {
			tkGprs_subState =gTR_n07();
		} else {
			tkGprs_subState = gTR_n06();
		}
		break;
	case gSST_ONoffline_Sqe_01:
		tkGprs_subState =gTR_s02();
		break;
	case gSST_ONoffline_Sqe_02:
		if ( gEventos[evCTIMER_IS_0] ) {
			tkGprs_subState =gTR_s04();
		} else {
			tkGprs_subState = gTR_s03();
		}
		break;
	case gSST_ONoffline_Sqe_03:
		if ( gEventos[ev_WKMONITOR_SQE] ) {
			tkGprs_subState =gTR_s05();
		} else {
			tkGprs_subState = gTR_i01();
		}
		break;
	case gSST_ONoffline_IP_01:
		tkGprs_subState = gTR_i02();
		break;
	case gSST_ONoffline_IP_02:
		if ( gEventos[evCTIMER_IS_0] ) {
			tkGprs_subState =gTR_i04();
		} else {
			tkGprs_subState = gTR_i03();
		}
		break;
	case gSST_ONoffline_IP_03:
		if ( gEventos[ev_IPRSP_OK] ) {
			tkGprs_subState =gTR_i05();
		} else {
			tkGprs_subState = gTR_i06();
		}
		break;
	case gSST_ONoffline_IP_04:
		if ( gEventos[evPTRYES_IS_0] ) {
			tkGprs_subState =gTR_i08();
		} else {
			tkGprs_subState = gTR_i07();
		}
		break;
	case gSST_ONoffline_IP_05:
		if ( gEventos[evQTRYES_IS_0] ) {
			tkGprs_subState =gTR_i10();
		} else {
			tkGprs_subState = gTR_i09();
		}
		break;
	default:
		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\ntkGprs::ERROR sst_ONoffline: subState  (%d) NOT DEFINED\r\n\0"),tkGprs_subState);
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
		tkGprs_state = gST_OFF;
		tkGprs_subState = gSST_OFF_Entry;
		break;
	}
}
/*------------------------------------------------------------------------------------*/
static int gTR_c01(void)
{
	// gSST_ONoffline_Entry -> gSST_ONoffline_Config_01
	// Configuro el modem.

	snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\n%s: GPRS configure:\r\n\0"), u_now() );
	FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT&D0&C1\r\0", sizeof("AT&D0&C1\r\0") );
	vTaskDelay( (portTickType)( 100 / portTICK_RATE_MS ) );
	pv_GPRSprintRsp();

	// Configuro la secuencia de escape +++AT
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT*E2IPS=2,8,2,1020,0,15\r\0", sizeof("AT*E2IPS=2,8,2,1020,0,15\r\0") );
	vTaskDelay( (portTickType)( 100 / portTICK_RATE_MS ) );
	pv_GPRSprintRsp();

	// SMS Envio: Los envio en modo texto
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT+CMGF=1\r\0", sizeof("AT+CMGF=1\r\0") );
	vTaskDelay( (portTickType)( 100 / portTICK_RATE_MS ) );
	pv_GPRSprintRsp();

	// SMS Recepcion: No indico al TE ni le paso el mensaje
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT+CNMI=1,0\r\0", sizeof("AT+CNMI=1,0\r\0") );
	vTaskDelay( (portTickType)( 100 / portTICK_RATE_MS ) );
	pv_GPRSprintRsp();

	// SMS indicacion: Bajando el RI por 100ms.
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT*E2SMSRI=100\r\0", sizeof("AT*E2SMSRI=100\r\0") );
	vTaskDelay( (portTickType)( 100 / portTICK_RATE_MS ) );
	pv_GPRSprintRsp();

	// Borro todos los SMS de la memoria
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT+CMGD=0,4\r\0", sizeof("AT+CMGD=0,4\r\0") );
	vTaskDelay( (portTickType)( 100 / portTICK_RATE_MS ) );
	pv_GPRSprintRsp();

	// Deshabilito los mensajes
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT*E2IPEV=0,0\r\0", sizeof("AT*E2IPEV=0,0\r\0") );
	vTaskDelay( (portTickType)( 100 / portTICK_RATE_MS ) );
	pv_GPRSprintRsp();

	pv_GPRSprintExitMsg("c01\0");
	return(gSST_ONoffline_Config_01);
}
//------------------------------------------------------------------------------------
static int gTR_c02(void)
{
	// gSST_ONoffline_Config_01 -> gSST_ONoffline_Config_02
	// Configuro la banda

char bandBuffer[32];
char *ts = NULL;
u08 modemBand;
size_t xBytes;

	// Vemos si la banda configurada es la correcta. Si no la reconfiguro.
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_TX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT*EBSE?\r\0", sizeof("AT*EBSE?\r\0") );
	vTaskDelay( (portTickType)( 500 / portTICK_RATE_MS ) );
	pv_GPRSprintRsp();

	// Extraigo de la respuesta la banda
	memcpy(bandBuffer, FreeRTOS_UART_getFifoPtr(&pdUART0), sizeof(bandBuffer) );
	ts = strchr(bandBuffer, ':');
	ts++;
	modemBand = atoi(ts);
	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		tickCount = xTaskGetTickCount();
		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("mBAND=%d,sBAND=%d\r\n\0"),modemBand, systemVars.gsmBand);
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}

	GPRS_flags.gsmBandOK = TRUE;
	if ( modemBand != systemVars.gsmBand ) {
		// Debo reiniciar el modem
		GPRS_flags.gsmBandOK = FALSE;

		// Reconfiguro.
		xBytes = snprintf_P( gprs_printfBuff,CHAR256,PSTR("AT*EBSE=%d\r\0"),systemVars.gsmBand );
		FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
		FreeRTOS_write( &pdUART0, gprs_printfBuff, xBytes);
		vTaskDelay( (portTickType)( 100 / portTICK_RATE_MS ) );

		// Guardo el profile
		FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
		FreeRTOS_write( &pdUART0, "AT&W\r\0", sizeof("AT&W\r\0") );
		vTaskDelay( (portTickType)( 100 / portTICK_RATE_MS ) );

		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\n%s: GPRS Reconfiguro GSM_BAND a modo %d:\r\n\0"), u_now(),systemVars.gsmBand);
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}

	pv_GPRSprintExitMsg("c02\0");
	return(gSST_ONoffline_Config_02);
}
//------------------------------------------------------------------------------------
static int gTR_c03(void)
{
	// Debo reiniciar el modem para que tome la nueva banda
	// gSST_ONoffline_Config_02 -> gST_OFF

	tkGprs_state = gST_OFF;

	pv_GPRSprintExitMsg("c03\0");
	return(gSST_OFF_Entry);
}
//------------------------------------------------------------------------------------
static int gTR_n01(void)
{
	// gSST_ONoffline_Config_02 -> gSST_ONoffline_Net_01
	// Inicializo contador de cuantas veces voy a preguntar por la NET

	snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\n%s: GPRS Net Attach:\r\n\0"), u_now());
	FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	GPRS_counters.pTryes = 10;	// Pregunto hasta 10 veces

	pv_GPRSprintExitMsg("n01\0");
	return(gSST_ONoffline_Net_01);
}
//------------------------------------------------------------------------------------
static int gTR_n02(void)
{
	// gSST_ONoffline_Net_01 -> gSST_ONoffline_Net_02

	// Pregunto por la NET y espero 6s la respuesta.
	GPRS_counters.cTimer = 6;	// a intervalos de 6s entre consultas

	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_TX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT+CREG?\r\0", sizeof("AT+CREG?\r\0") );
	vTaskDelay( (portTickType)( 100 / portTICK_RATE_MS ) );

	pv_GPRSprintExitMsg("n02\0");
	return(gSST_ONoffline_Net_02);
}
//------------------------------------------------------------------------------------
static int gTR_n03(void)
{
	// gSST_ONoffline_Net_02 -> gSST_ONoffline_Net_03
	// Chequeo la respuesta a CNET.

size_t pos;

	// Leo y Evaluo la respuesta al comando AT+CREG ( home network, NOT roaming !!! )
	if ( pv_GPRSrspIs("+CREG: 0,1\0", &pos ) == TRUE ) {
		GPRSrsp = RSP_CREG;
//		pv_GPRSprintRsp();
	}
	pv_GPRSprintRsp();
	//FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );

	pv_GPRSprintExitMsg("n03\0");
	return(gSST_ONoffline_Net_03);
}
//------------------------------------------------------------------------------------
static int gTR_n04(void)
{
	// gSST_ONoffline_Net_02 -> gSST_ONoffline_Net_02
	// Espero 6s que expire cTimer para evaluar la respuesta a CNET

	if ( GPRS_counters.cTimer > 0 ) {
		vTaskDelay( ( TickType_t)( 1000 / portTICK_RATE_MS ) );
		GPRS_counters.cTimer--;
	}

	FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	//pv_GPRSprintExitMsg("n04\0");
	return(gSST_ONoffline_Net_02);
}
//------------------------------------------------------------------------------------
static int gTR_n05(void)
{
	// gSST_ONoffline_Net_03 -> gSST_ONoffline_Net_04
	// No obtuve respuesta correcta de CNET: reintento

	if ( GPRS_counters.pTryes > 0 ) {
		GPRS_counters.pTryes--;
	}

	//FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	pv_GPRSprintExitMsg("n05\0");
	return(gSST_ONoffline_Net_04);
}
//------------------------------------------------------------------------------------
static int gTR_n06(void)
{
	// gSST_ONoffline_Net_04 -> gSST_ONoffline_Net_01
	// No obtuve respuesta correcta de CNET: reintento

	pv_GPRSprintExitMsg("n06\0");
	return(gSST_ONoffline_Net_01);
}
//------------------------------------------------------------------------------------
static int gTR_n07(void)
{
	// gSST_ONoffline_Net_04 -> gST_OFF
	// No pude conectarme a la red: salgo

	tkGprs_state = gST_OFF;

	pv_GPRSprintExitMsg("n07\0");
	return(gSST_OFF_Entry);
}
//------------------------------------------------------------------------------------
static int gTR_s01(void)
{
	// gSST_ONoffline_Net_03 -> gSST_ONoffline_Sqe_01
	// Obtuve respuesta correcta de CNET: paso a leer/monitorear el SQE

	snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\n%s: query SQE:\r\n\0"), u_now());
	FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	//pv_GPRSprintExitMsg("s01\0");
	return(gSST_ONoffline_Sqe_01);
}
//------------------------------------------------------------------------------------
static int gTR_s02(void)
{
	// gSST_ONoffline_Sqe_01 -> gSST_ONoffline_Sqe_02
	// Leo el SQE

	GPRS_counters.cTimer = 5;
	GPRSrsp = RSP_NONE;
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_TX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT+CSQ\r\0", sizeof("AT+CSQ\r\0") );
	vTaskDelay( (portTickType)( 100 / portTICK_RATE_MS ) );

	//pv_GPRSprintExitMsg("s02\0");
	return(gSST_ONoffline_Sqe_02);
}
//------------------------------------------------------------------------------------
static int gTR_s03(void)
{
	// gSST_ONoffline_Sqe_02 -> gSST_ONoffline_Sqe_02
	// Espero 6s que expire cTimer para evaluar la respuesta a SQE

	if ( GPRS_counters.cTimer > 0 ) {
		vTaskDelay( ( TickType_t)( 1000 / portTICK_RATE_MS ) );
		GPRS_counters.cTimer--;
	}

	//pv_GPRSprintExitMsg("s03\0");
	return(gSST_ONoffline_Sqe_02);
}
//------------------------------------------------------------------------------------
static int gTR_s04(void)
{
	// gSST_ONoffline_Sqe_02 -> gSST_ONoffline_Sqe_03
	// Leo y Evaluo la respuesta al comando AT+CSQ y extraigo el valor del SQ

size_t pos;
char csqBuffer[32];
char *ts = NULL;

	if ( pv_GPRSrspIs("CSQ:\0", &pos ) == TRUE ) {

		pv_GPRSprintRsp();

		memcpy(csqBuffer, FreeRTOS_UART_getFifoPtr(&pdUART0), sizeof(csqBuffer) );
		ts = strchr(csqBuffer, ':');
		ts++;
		systemVars.csq = atoi(ts);
		systemVars.dbm = 113 - 2 * systemVars.csq;

		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\nCSQ=%d,DBM=%d\r\n\0"),systemVars.csq,systemVars.dbm);
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	}

	//pv_GPRSprintExitMsg("s04\0");
	return(gSST_ONoffline_Sqe_03);
}
//------------------------------------------------------------------------------------
static int gTR_s05(void)
{
	// gSST_ONoffline_Sqe_03 -> gSST_ONoffline_Sqe_01
	// Estoy en modo monitor sqe por lo tanto vuelvo a leer el SQE

	//pv_GPRSprintExitMsg("s05\0");
	return(gSST_ONoffline_Sqe_01);
}
//------------------------------------------------------------------------------------
static int gTR_i01(void)
{
	// gSST_ONoffline_Sqe_03 -> gSST_ONoffline_IP_01
	// Configuro el APN.

size_t xBytes;

	// APN
	snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\n%s: GPRS set APN:\r\n\0"), u_now());
	FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	xBytes = snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("AT+CGDCONT=1,\"IP\",\"%s\"\r\0"),systemVars.apn);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_TX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, gprs_printfBuff, xBytes );
	vTaskDelay( (portTickType)( 1000 / portTICK_RATE_MS ) );
	pv_GPRSprintRsp();

	GPRS_counters.qTryes = 3;	// Pregunto hasta 3 veces
	GPRS_counters.pTryes = 6;	// Con 6 reintentos c/u

	pv_GPRSprintExitMsg("i01\0");
	return(gSST_ONoffline_IP_01);
}
//------------------------------------------------------------------------------------
static int gTR_i02(void)
{
	// gSST_ONoffline_IP_01 -> gSST_ONoffline_IP_02
	// Pido la IP.

	snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\n[%s] GPRS ask IP:\r\n\0"), u_now());
	FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_TX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT*E2IPA=1,1\r\0", sizeof("AT*E2IPA=1,1\r\0") );

	GPRSrsp = RSP_NONE;
	GPRS_counters.cTimer = 10;	// espero 10s antes de consultar

	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		tickCount = xTaskGetTickCount();
		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR(".[%06lu] tkGprs: ASK IP (%d)\r\n\0"),tickCount, GPRS_counters.qTryes );
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}
	pv_GPRSprintExitMsg("i02\0");
	return(gSST_ONoffline_IP_02);
}
//------------------------------------------------------------------------------------
static int gTR_i03(void)
{
	// gSST_ONoffline_IP_02 -> gSST_ONoffline_IP_02

	if ( GPRS_counters.cTimer > 0 ) {
		vTaskDelay( ( TickType_t)( 1000 / portTICK_RATE_MS ) );
		GPRS_counters.cTimer--;
	}

	FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	//pv_GPRSprintExitMsg("i03\0");
	return(gSST_ONoffline_IP_02);
}

//------------------------------------------------------------------------------------
static int gTR_i04(void)
{
	// gSST_ONoffline_IP_02 -> gSST_ONoffline_IP_03

size_t pos;

	// Leo y Evaluo la respuesta al comando AT*E2IPA ( activacion de IP )
	// La respuesta correcta es *E2IPA: 000 OK
	if ( pv_GPRSrspIs("OK\0", &pos ) == TRUE ) {
		GPRSrsp = RSP_IPOK;
	}
	pv_GPRSprintRsp();

	//FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	pv_GPRSprintExitMsg("i04\0");
	return(gSST_ONoffline_IP_03);
}

//------------------------------------------------------------------------------------
static int gTR_i05(void)
{
	// gSST_ONoffline_IP_03 -> gSST_ONonline_Entry
	// Obtengo la IP asignada y CAMBIO DE ESTADO:

char *ts = NULL;
int i=0;
char c;

	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_TX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "AT*E2IPI=0\r\0", sizeof("AT*E2IPI=0\r\0") );
	vTaskDelay( (portTickType)( 500 / portTICK_RATE_MS ) );
	pv_GPRSprintRsp();

	// Extraigo la IP del token. Voy a usar el buffer  de print ya que la respuesta
	// puede ser grande.
	memcpy(gprs_printfBuff, FreeRTOS_UART_getFifoPtr(&pdUART0), sizeof(gprs_printfBuff) );

	ts = strchr( gprs_printfBuff, '\"');
	ts++;
	while ( (c= *ts) != '\"') {
		systemVars.dlgIp[i++] = c;
		ts++;
	}
	systemVars.dlgIp[i++] = '\0';
	snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\nIPADDRESS=[%s]\r\n\0"),systemVars.dlgIp);
	FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	// Cambio de estado
	tkGprs_state = gST_ONopenSocket;

	pv_GPRSprintExitMsg("i05\0");
	return( gSST_ONopenSocket_Entry );
}

//------------------------------------------------------------------------------------
static int gTR_i06(void)
{
	// gSST_ONoffline_IP_03 -> gSST_ONoffline_IP_04

	if ( GPRS_counters.pTryes > 0 ) {
		GPRS_counters.pTryes--;
	}

	//FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	pv_GPRSprintExitMsg("i06\0");
	return(gSST_ONoffline_IP_04);
}

//------------------------------------------------------------------------------------
static int gTR_i07(void)
{
	// gSST_ONoffline_IP_04 -> gSST_ONoffline_IP_02

	GPRS_counters.cTimer = 10;

	//FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	pv_GPRSprintExitMsg("i07\0");
	return(gSST_ONoffline_IP_02);
}

//------------------------------------------------------------------------------------
static int gTR_i08(void)
{
	// gSST_ONoffline_IP_04 -> gSST_ONoffline_IP_05

	if ( GPRS_counters.qTryes > 0 ) {
		GPRS_counters.qTryes--;
	}

	pv_GPRSprintExitMsg("i08\0");
	return(gSST_ONoffline_IP_05);
}

//------------------------------------------------------------------------------------
static int gTR_i09(void)
{
	// gSST_ONoffline_IP_05 -> gSST_ONoffline_IP_01

	GPRS_counters.pTryes = 3;

	pv_GPRSprintExitMsg("i09\0");
	return(gSST_ONoffline_IP_01);
}

//------------------------------------------------------------------------------------
static int gTR_i10(void)
{
	// gSST_ONoffline_IP_04 -> gSST_OFF
	tkGprs_state = gST_OFF;

	pv_GPRSprintExitMsg("i10\0");
	return(gSST_OFF_Entry);
}
//------------------------------------------------------------------------------------
/*
 *  FUNCIONES DEL ESTADO ON-OPENSOCKET:
 *  El modem esta prendido y con una IP. Debe abrir un socket
 *
 */
//------------------------------------------------------------------------------------
static void SM_onOpenSocket(void)
{
	// Maquina de estados del estado ON-OPENSOCKET

	if ( gEventos[evRELOADCONFIG] ) { gTR_reloadConfig(); return; }

	switch ( tkGprs_subState ) {
	case gSST_ONopenSocket_Entry:
		tkGprs_subState = gTR_k01();
		break;
	case gSST_ONopenSocket_01:
		tkGprs_subState = gTR_k02();
		break;
	case gSST_ONopenSocket_02:
		if ( gEventos[evCTIMER_IS_0] ) {
			tkGprs_subState = gTR_k04();
		} else {
			tkGprs_subState = gTR_k03();
		}
		break;
	case gSST_ONopenSocket_03:
		if ( gEventos[ev_SOCKRSP_CONNECT] ) {
			tkGprs_subState = gTR_k05();
		} else {
			tkGprs_subState = gTR_k06();
		}
		break;
	case gSST_ONopenSocket_04:
		if ( gEventos[evPTRYES_IS_0] ) {
			tkGprs_subState = gTR_k08();
		} else {
			tkGprs_subState = gTR_k07();
		}
		break;
	case gSST_ONopenSocket_05:
		if ( gEventos[evQTRYES_IS_0] ) {
			tkGprs_subState = gTR_k10();
		} else {
			tkGprs_subState = gTR_k09();
		}
		break;
	case gSST_ONopenSocket_06:
		if ( gEventos[evCINITS_IS_0] ) {
			tkGprs_subState = gTR_k11();
		} else {
			tkGprs_subState = gTR_k12();
		}
		break;
	case gSST_ONopenSocket_07:
		if ( gEventos[ev_GPRSRSP_ERROR] ) {
			tkGprs_subState = gTR_k14();
		} else {
			tkGprs_subState = gTR_k13();
		}
		break;
	default:
		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\ntkGprs::ERROR ONopenSocket: subState  (%d) NOT DEFINED\r\n\0"),tkGprs_subState);
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
		tkGprs_state = gST_OFF;
		tkGprs_subState = gSST_OFF_Entry;
		break;
	}
}

//------------------------------------------------------------------------------------
static int gTR_k01(void)
{
	// gSST_ONopenSocket_Entry -> gSST_ONopenSocket_01

	GPRS_counters.qTryes = 3;	// Envio el comando OPENSOCKET hasta 3 veces
	GPRS_counters.pTryes = 6;	// C/vez pregunto 6 veces

	snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\n%s: GPRS open SOCKET:\r\n\0"), u_now());
	FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	pv_GPRSprintExitMsg("k01\0");
	return(gSST_ONopenSocket_01);
}

//------------------------------------------------------------------------------------
static int gTR_k02(void)
{
	// gSST_ONopenSocket_01 -> gSST_ONopenSocket_02
	// Abro el socket

size_t xBytes;

	xBytes = snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("AT*E2IPO=1,\"%s\",%s\r\n\0"),systemVars.serverAddress,systemVars.serverPort);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_TX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, gprs_printfBuff, xBytes );

	GPRS_counters.cTimer = 5;	// espero 5s antes de consultar

	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		tickCount = xTaskGetTickCount();
		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR(".[%06lu] tkGprs: OPEN SOCKET (%d)\r\n\0"),tickCount, GPRS_counters.qTryes );
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}
	pv_GPRSprintExitMsg("k02\0");
	return(gSST_ONopenSocket_02);
}

//------------------------------------------------------------------------------------
static int gTR_k03(void)
{
	// gSST_ONopenSocket_02 -> gSST_ONopenSocket_02
	// Espera

	if ( GPRS_counters.cTimer > 0 ) {
		vTaskDelay( ( TickType_t)( 1000 / portTICK_RATE_MS ) );
		GPRS_counters.cTimer--;
	}

	FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	//pv_GPRSprintExitMsg("k03\0");
	return(gSST_ONopenSocket_02);
}

//------------------------------------------------------------------------------------
static int gTR_k04(void)
{
	// gSST_ONopenSocket_02 -> gSST_ONopenSocket_07

size_t pos;

	// Leo y Evaluo la respuesta al comando AT*E2IPO ( open socket )
	// La respuesta correcta debe ser CONNECT
	GPRSrsp = RSP_NONE;
	if ( pv_GPRSrspIs("CONNECT\0", &pos ) == TRUE ) {
		GPRSrsp = RSP_CONNECT;
	}
	if ( pv_GPRSrspIs("ERROR\0", &pos ) == TRUE ) {
		GPRSrsp = RSP_ERROR;
	}
	pv_GPRSprintRsp();

	//FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	pv_GPRSprintExitMsg("k04\0");
	return(gSST_ONopenSocket_07);
}

//------------------------------------------------------------------------------------
static int gTR_k05(void)
{
	// gSST_ONopenSocket_03 -> gSST_ONopenSocket_06
	// El socket esta abierto. Paso a enviar INIT o DATA

	pv_GPRSprintExitMsg("k05\0");
	return(gSST_ONopenSocket_06);

}

//------------------------------------------------------------------------------------
static int gTR_k06(void)
{
	// gSST_ONopenSocket_03 -> gSST_ONopenSocket_04

	if ( GPRS_counters.pTryes > 0 ) {
		GPRS_counters.pTryes--;
	}

	//FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	pv_GPRSprintExitMsg("k06\0");
	return(gSST_ONopenSocket_04);
}

//------------------------------------------------------------------------------------
static int gTR_k07(void)
{
	// gSST_ONopenSocket_04 -> gSST_ONopenSocket_02

	GPRS_counters.cTimer = 5;

	//FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	pv_GPRSprintExitMsg("k07\0");
	return(gSST_ONopenSocket_02);
}

//------------------------------------------------------------------------------------
static int gTR_k08(void)
{
	// gSST_ONopenSocket_04 -> gSST_ONopenSocket_05

	if ( GPRS_counters.qTryes > 0 ) {
		GPRS_counters.qTryes--;
	}

	pv_GPRSprintExitMsg("k08\0");
	return(gSST_ONopenSocket_05);
}

//------------------------------------------------------------------------------------
static int gTR_k09(void)
{
	// gSST_ONopenSocket_05 -> gSST_ONopenSocket_01

	GPRS_counters.pTryes = 3;

	pv_GPRSprintExitMsg("k09\0");
	return(gSST_ONopenSocket_01);
}

//------------------------------------------------------------------------------------
static int gTR_k10(void)
{
	// gSST_ONopenSocket_05 -> gSST_OFF
	tkGprs_state = gST_OFF;

	pv_GPRSprintExitMsg("k10\0");
	return(gSST_OFF_Entry);
}

//------------------------------------------------------------------------------------
static int gTR_k11(void)
{
	// gSST_ONopenSocket_06 -> gST_ONdata

	tkGprs_state = gST_ONdata;

	pv_GPRSprintExitMsg("k11\0");
	return(gSST_ONdata_Entry);
}

//------------------------------------------------------------------------------------
static int gTR_k12(void)
{
	// gSST_ONopenSocket_06 -> gST_ONinitFrame
	tkGprs_state = gST_ONinitFrame;

	pv_GPRSprintExitMsg("k12\0");
	return(gSST_ONinitframe_Entry);

}

//------------------------------------------------------------------------------------
static int gTR_k13(void)
{
	// gSST_ONopenSocket_07 -> gSST_ONopenSocket_03

	//FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	pv_GPRSprintExitMsg("k13\0");
	return(gSST_ONopenSocket_03);
}

//------------------------------------------------------------------------------------
static int gTR_k14(void)
{
	// gSST_ONopenSocket_07 -> gSST_ONopenSocket_05

	if ( GPRS_counters.qTryes > 0 ) {
		GPRS_counters.qTryes--;
	}

	pv_GPRSprintExitMsg("k14\0");
	return(gSST_ONopenSocket_05);
}
//------------------------------------------------------------------------------------
/*
 *  FUNCIONES DEL ESTADO ON-INITFRAME:
 *  El modem esta prendido y con una IP y el socket abierto: Debo mandar un frame de INIT
 *
 */
//------------------------------------------------------------------------------------
static void SM_onInitFrame(void)
{
	// Maquina de estados del estado ON-INITFRAME

	if ( gEventos[evRELOADCONFIG] ) { gTR_reloadConfig(); return; }

	switch ( tkGprs_subState ) {
	case gSST_ONinitframe_Entry:
		tkGprs_subState = gTR_f01();
		break;
	case gSST_ONinitframe_01:
		tkGprs_subState = gTR_f02();
		break;
	case gSST_ONinitframe_02:
		if ( gEventos[evCINITS_IS_1] ) {
			tkGprs_subState = gTR_f04();
		} else {
			tkGprs_subState = gTR_f03();
		}
		break;
	case gSST_ONinitframe_03:
		if ( gEventos[evCTIMER_IS_0] ) {
			tkGprs_subState = gTR_f06();
		} else {
			tkGprs_subState = gTR_f05();
		}
		break;
	case gSST_ONinitframe_04:
		if ( gEventos[ev_SOCKET_IS_OPEN] ) {
			tkGprs_subState = gTR_f08();
		} else {
			tkGprs_subState = gTR_f07();
		}
		break;
	case gSST_ONinitframe_05:
		if ( gEventos[ev_INITRSP_OK] ) {
			tkGprs_subState = gTR_f09();
		} else {
			tkGprs_subState = gTR_f10();
		}
		break;
	case gSST_ONinitframe_06:
		if ( gEventos[evPTRYES_IS_0] ) {
			tkGprs_subState = gTR_f11();
		} else {
			tkGprs_subState = gTR_f12();
		}
		break;
	default:
		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\ntkGprs::ERROR ONinitFrame: subState  (%d) NOT DEFINED\r\n\0"),tkGprs_subState);
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
		tkGprs_state = gST_OFF;
		tkGprs_subState = gSST_OFF_Entry;
		break;
	}
}

//------------------------------------------------------------------------------------
static int gTR_f01(void)
{
	// gSST_ONinitframe_01 -> gSST_ONinitframe_01

	snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\n%s: GPRS init FRAME:\r\n\0"), u_now());
	FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	pv_GPRSprintExitMsg("f01\0");
	return(gSST_ONinitframe_01);
}

//------------------------------------------------------------------------------------
static int gTR_f02(void)
{
	// gSST_ONinitframe_01 -> gSST_ONinitframe_02

	if ( GPRS_counters.cInits > 0 ) {
		GPRS_counters.cInits--;
	}

	pv_GPRSprintExitMsg("f02\0");
	return(gSST_ONinitframe_02);
}

//------------------------------------------------------------------------------------
static int gTR_f03(void)
{
	// gSST_ONinitframe_02 -> gSST_ONinitframe_03
	// Send Init Frame
	// GET /cgi-bin/sp5K/sp5K.pl?DLGID=SPY001&PASSWD=spymovil123&&INIT&PWRM=CONT&TPOLL=23&TDIAL=234&PWRS=1,1230,2045&A0=pZ,1,20,3,10&D0=qE,3.24&OUT=1,1,0,0,1,1234,927,1,3 HTTP/1.1
	// Host: www.spymovil.com
	// Connection: close\r\r ( no mando el close )

u16 pos = 0;
u08 i;

	GPRS_counters.pTryes = 6;	// Pregunto hasta 6 veces
	GPRS_counters.cTimer = 5;	// Pregunto c/5secs

	// Trasmision: 1r.Parte.
	// HEADER:
	// Envio parcial ( no CR )
	memset( gprs_printfBuff, '\0', sizeof(gprs_printfBuff));
	pos = snprintf_P( gprs_printfBuff,CHAR256,PSTR("GET " ));
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("%s"), systemVars.serverScript );
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("?DLGID=%s"), systemVars.dlgId );
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&PASSWD=%s"), systemVars.passwd );
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&VER=%s"), SP5K_REV );
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_TX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, gprs_printfBuff, pos );
	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("\r\n\0" ));
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}

	// BODY ( 1a parte) :
	memset(gprs_printfBuff, '\0', sizeof(gprs_printfBuff));
	pos = snprintf_P( gprs_printfBuff ,CHAR256,PSTR("&INIT"));
	// timerpoll
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&TPOLL=%d"), systemVars.timerPoll);
	// timerdial
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&TDIAL=%d"), systemVars.timerDial);
	// pwrMode
	if ( systemVars.pwrMode == PWR_CONTINUO) { pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&PWRM=CONT")); }
	if ( systemVars.pwrMode == PWR_DISCRETO) { pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&PWRM=DISC")); }
	// pwrSave
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&PWRS=%d,%d,%d"),systemVars.pwrSave, u_convertMINS2hhmm( systemVars.pwrSaveStartTime ),u_convertMINS2hhmm( systemVars.pwrSaveEndTime) );
	// csq
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&CSQ=%d"), systemVars.csq);
	FreeRTOS_write( &pdUART0, gprs_printfBuff, pos );
	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("\r\n\0" ));
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}

	// BODY ( 2a parte) :
	memset(gprs_printfBuff, '\0', sizeof(gprs_printfBuff));
	pos = 0;
	// Configuracion de canales analogicos
	for ( i = 0; i < NRO_CHANNELS; i++) {
		pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&A%d=%s,%d,%d,%d,%.1f"), i,systemVars.aChName[i],systemVars.Imin[i], systemVars.Imax[i], systemVars.Mmin[i], systemVars.Mmax[i]);
	}
	// Configuracion de canales digitales
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&D0=%s,%.2f"),systemVars.dChName[0],systemVars.magPP[0]);
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&D1=%s,%.2f"),systemVars.dChName[1],systemVars.magPP[1]);
	// Configuracion de salidas
	pos += snprintf_P( &gprs_printfBuff[pos],( CHAR256 - pos ),PSTR("&CONS=%d,%d,%d"),systemVars.consigna.status,u_convertMINS2hhmm(systemVars.consigna.horaConsDia),u_convertMINS2hhmm(systemVars.consigna.horaConsNoc));
	FreeRTOS_write( &pdUART0, gprs_printfBuff, pos );
	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("\r\n\0" ));
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}

	// TAIL ( No mando el close):
	memset(gprs_printfBuff, '\0', sizeof(gprs_printfBuff));
	pos = snprintf_P( gprs_printfBuff, ( sizeof(gprs_printfBuff) - pos ),PSTR(" HTTP/1.1\n") );
	pos += snprintf_P( &gprs_printfBuff[pos], ( sizeof(gprs_printfBuff) - pos ),PSTR("Host: www.spymovil.com\n" ));
	pos += snprintf_P( &gprs_printfBuff[pos], sizeof(gprs_printfBuff),PSTR("\n\n" ));
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_TX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, gprs_printfBuff, pos );
	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("\r\n\0" ));
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}

	pv_GPRSprintExitMsg("f03\0");
	return(gSST_ONinitframe_03);
}

//------------------------------------------------------------------------------------
static int gTR_f04(void)
{
	// gSST_ONinitframe_02 -> gSST_OFF
	tkGprs_state = gST_OFF;

	pv_GPRSprintExitMsg("f04\0");
	return(gSST_OFF_Entry);
}

//------------------------------------------------------------------------------------
static int gTR_f05(void)
{
	// gSST_ONinitframe_03 -> gSST_ONinitframe_03
	// Espera

	if ( GPRS_counters.cTimer > 0 ) {
		vTaskDelay( ( TickType_t)( 1000 / portTICK_RATE_MS ) );
		GPRS_counters.cTimer--;
	}

	FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	//pv_GPRSprintExitMsg("f05\0");
	return(gSST_ONinitframe_03);
}

//------------------------------------------------------------------------------------
static int gTR_f06(void)
{
	// gSST_ONinitframe_03 -> gSST_ONinitframe_04

	pv_GPRSprintExitMsg("f06\0");
	return(gSST_ONinitframe_04);
}

//------------------------------------------------------------------------------------
static int gTR_f07(void)
{
	// gSST_ONinitframe_04 ->  gSST_ONopenSocket_Entry

	tkGprs_state = gST_ONopenSocket;

	pv_GPRSprintExitMsg("f07\0");
	return(gSST_ONopenSocket_Entry);
}

//------------------------------------------------------------------------------------
static int gTR_f08(void)
{
	// gSST_ONinitframe_04 ->  gSST_ONinitframe_05

size_t pos;

	// Leo y Evaluo la respuesta al comando AT*E2IPO ( open socket )
	// La respuesta correcta debe ser CONNECT
	GPRSrsp = RSP_NONE;
	if ( pv_GPRSrspIs("INIT_OK\0", &pos ) == TRUE ) {
		GPRSrsp = RSP_INIT;
	}

	pv_GPRSprintRsp();

	pv_GPRSprintExitMsg("f08\0");
	return( gSST_ONinitframe_05);
}

//------------------------------------------------------------------------------------
static int gTR_f09(void)
{
	// gSST_ONinitframe_05 ->  gST_ONdata
	// Recibi la respuesta del INIT.
	// La parseo y me reconfiguro

u08 saveFlag = 0;

	// Proceso la respuesta del INIT para reconfigurar los parametros
	pv_GPRSprocessServerClock();
	saveFlag = pv_GPRSprocessPwrMode();
	saveFlag += pv_GPRSprocessTimerPoll();
	saveFlag += pv_GPRSprocessTimerDial();
	saveFlag += pv_GPRSprocessPwrSave();
	// Canales analogicos.
	saveFlag += pv_GPRSprocessAch(0);
	saveFlag += pv_GPRSprocessAch(1);
	saveFlag += pv_GPRSprocessAch(2);
	// Canales digitales
	saveFlag += pv_GPRSprocessDch(0);
	saveFlag += pv_GPRSprocessDch(1);
	// Consignas
//	saveFlag += pv_GPRSprocessConsignas();

	if ( saveFlag > 0 ) {
		if ( u_saveSystemParams() ) {

			snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("tkGprs: save parameters OK\r\n\0"));
			FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

			// Le aviso a la tkAnalog que se reconfigure.
			while ( xTaskNotify(xHandle_tkAIn, TKA_PARAM_RELOAD , eSetBits ) != pdPASS ) {
				vTaskDelay( ( TickType_t)( 100 / portTICK_RATE_MS ) );
			}
		}
	}

	// Cierro el socket
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "+++AT\r\0", sizeof("+++AT\r\0") );
	// Reseteo la variable cInits para no enviar mas un INIT.
	GPRS_counters.cInits = 0;
	// Cambio de estado
	tkGprs_state = gST_ONdata;

	pv_GPRSprintExitMsg("f09\0");
	return(gSST_ONdata_Entry);
}

//------------------------------------------------------------------------------------
static int gTR_f10(void)
{
	// gSST_ONinitframe_05 ->  gSST_ONinitframe_06

	if ( GPRS_counters.pTryes > 0 ) {
		GPRS_counters.pTryes--;
	}

	pv_GPRSprintExitMsg("f10\0");
	return( gSST_ONinitframe_06);
}

//------------------------------------------------------------------------------------
static int gTR_f11(void)
{
	// gSST_ONinitframe_06 ->  gSST_ONopenSocket_Entry
	// Por las dudas cierro el socket

	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_TX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, "+++AT\r\0", sizeof("+++AT\r\0") );

	tkGprs_state = gST_ONopenSocket;

	pv_GPRSprintExitMsg("f11\0");
	return(gSST_ONopenSocket_Entry);
}

//------------------------------------------------------------------------------------
static int gTR_f12(void)
{
	// gSST_ONinitframe_06 -> gSST_ONinitframe_03

	pv_GPRSprintExitMsg("f12\0");
	return(gSST_ONinitframe_03);
}
//------------------------------------------------------------------------------------
/*
 *  FUNCIONES DEL ESTADO ON-DATA:
 *
 */
//------------------------------------------------------------------------------------
static void SM_onData(void)
{
	// Maquina de estados del estado ON-DATA

	if ( gEventos[evRELOADCONFIG] ) { gTR_reloadConfig(); return; }

	switch ( tkGprs_subState ) {
	case gSST_ONdata_Entry:
		tkGprs_subState = gTR_d01();
		break;
	case gSST_ONdata_01:
		tkGprs_subState = gTR_d02();
		break;
	case gSST_ONdata_02:
		if ( gEventos[ev_MEM_IS_EMPTY] ) {
			tkGprs_subState = gTR_d03();
		} else {
			tkGprs_subState = gTR_d08();
		}
		break;
	case gSST_ONdata_03:
		if ( gEventos[ev_PWR_CONT] ) {
			tkGprs_subState = gTR_d04();
		} else {
			tkGprs_subState = gTR_d05();
		}
		break;
	case gSST_ONdata_04:
		if ( gEventos[evCTIMER_IS_0] ) {
			tkGprs_subState = gTR_d07();
		} else {
			tkGprs_subState = gTR_d06();
		}
		break;
	case gSST_ONdata_05:
		if ( gEventos[ev_SOCKET_IS_OPEN] ) {
			tkGprs_subState = gTR_d10();
		} else {
			tkGprs_subState = gTR_d09();
		}
		break;
	case gSST_ONdata_06:
		if ( gEventos[ev_TX_A_RCD] ) {
			tkGprs_subState = gTR_d11();
		} else {
			tkGprs_subState = gTR_d12();
		}
		break;
	case gSST_ONdata_07:
		tkGprs_subState = gTR_d13();
		break;
	case gSST_ONdata_08:
		if ( gEventos[ev_GPRRSP_RXOK] ) {
			tkGprs_subState = gTR_d14();
		} else {
			tkGprs_subState = gTR_d15();
		}
		break;
	case gSST_ONdata_09:
		if ( gEventos[ev_SOCKET_IS_OPEN] ) {
			tkGprs_subState = gTR_d16();
		} else {
			tkGprs_subState = gTR_d17();
		}
		break;
	case gSST_ONdata_10:
		if ( gEventos[evCTIMER_IS_0] ) {
			tkGprs_subState = gTR_d18();
		} else {
			tkGprs_subState = gTR_d19();
		}
		break;
	case gSST_ONdata_11:
		tkGprs_subState = gTR_d20();
		break;
	default:
		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\ntkGprs::ERROR ONdata: subState  (%d) NOT DEFINED\r\n\0"),tkGprs_subState);
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
		tkGprs_state = gST_OFF;
		tkGprs_subState = gSST_OFF_Entry;
		break;
	}
}

//------------------------------------------------------------------------------------
static int gTR_d01(void)
{
	// gSST_ONdata_Entry -> gSST_ONdata_01

	snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("\r\n%s: GPRS Data:\r\n\0"), u_now());
	FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	pv_GPRSprintExitMsg("d01\0");
	return(gSST_ONdata_01);
}

//------------------------------------------------------------------------------------
static int gTR_d02(void)
{
	// gSST_ONdata_01 -> gSST_ONdata_02
	// Veo si hay datos en la memoria para trasmitir o no

StatBuffer_t pxFFStatBuffer;

	GPRS_flags.memoryEmpty = TRUE;
	FF_stat(&pxFFStatBuffer);
	if ( pxFFStatBuffer.rcds4del > 0 ) {
		GPRS_flags.memoryEmpty = FALSE;
	}

	pv_GPRSprintExitMsg("d02\0");
	return(gSST_ONdata_02);
}

//------------------------------------------------------------------------------------
static int gTR_d03(void)
{
	// gSST_ONdata_02 -> gSST_ONdata_03

	pv_GPRSprintExitMsg("d03\0");
	return(gSST_ONdata_03);
}

//------------------------------------------------------------------------------------
static int gTR_d04(void)
{
	// gSST_ONdata_07 -> gSST_ONdata_08

	// Espero 30s para volver a chequear.
	GPRS_counters.cTimer = 30;

	pv_GPRSprintExitMsg("d04\0");
	return(gSST_ONdata_04);
}

//------------------------------------------------------------------------------------
static int gTR_d05(void)
{
	// gSST_ONdatae_07 -> gSST_OFF
	tkGprs_state = gST_OFF;

	pv_GPRSprintExitMsg("d05\0");
	return(gSST_OFF_Entry);
}

//------------------------------------------------------------------------------------
static int gTR_d06(void)
{
	// gSST_ONdata_08 -> gSST_ONdata_08
	// Genero una espera de 1s.

	if ( GPRS_counters.cTimer > 0 ) {
		vTaskDelay( ( TickType_t)( 1000 / portTICK_RATE_MS ) );
		GPRS_counters.cTimer--;
	}

	FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	//pv_GPRSprintExitMsg("d06\0");
	return(gSST_ONdata_06);
}

//------------------------------------------------------------------------------------
static int gTR_d07(void)
{
	// gSST_ONdata_04 -> gSST_ONdata_01

	pv_GPRSprintExitMsg("d04\0");
	return(gSST_ONdata_01);
}

static int gTR_d08(void)
{
	// gSST_ONdata_02 -> gSST_ONdata_05

	snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("DEBUG DCD (%d)\r\n\0"), systemVars.dcd );
	FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	pv_GPRSprintExitMsg("d08\0");
	return(gSST_ONdata_05);
}

static int gTR_d09(void)
{
	// gSST_ONdatae_07 -> gSST_OFF
	tkGprs_state = gST_ONopenSocket;

	pv_GPRSprintExitMsg("d09\0");
	return(gSST_ONopenSocket_Entry);
}

static int gTR_d10(void)
{
	// gSST_ONdata_05 -> gSST_ONdata_06
	// Trasmito el HEADER e inicializo el contador de registros enviados

u08 pos;
StatBuffer_t pxFFStatBuffer;

	FF_seek();
	vTaskDelay( ( TickType_t)( 1000 / portTICK_RATE_MS ) );

	// HEADER:
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_TX_BUFFER, NULL);
	memset( gprs_printfBuff, '\0', sizeof(gprs_printfBuff));
	pos = snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("GET " ));
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("%s"), systemVars.serverScript );
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("?DLGID=%s"), systemVars.dlgId );
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&PASSWD=%s"), systemVars.passwd );
	FreeRTOS_write( &pdUART0, gprs_printfBuff, pos );
	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("\r\n\0" ));
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}

	GPRS_counters.txRcdsInFrame = 10;	// Cantidad de registros maximo a trasmitir en una ventana

	// Chequeo la condicion de trasmision.
	// La memoria esta vacia
	FF_stat(&pxFFStatBuffer);
	GPRS_flags.txNewRcd = TRUE;
	if (pxFFStatBuffer.rcds4rd == 0 )
		GPRS_flags.txNewRcd = FALSE;

	pv_GPRSprintExitMsg("d10\0");
	return(gSST_ONdata_06);
}
//------------------------------------------------------------------------------------

static int gTR_d11(void)
{
	// gSST_ONdata_06 -> gSST_ONdata_06
	// Trasmito un nuevo registro

u08 pos, channel;
frameData_t Aframe;
StatBuffer_t pxFFStatBuffer;

	// Leo memoria
	FF_fread( &Aframe, sizeof(Aframe));
	// Armo el frame
	memset( gprs_printfBuff, '\0', sizeof(gprs_printfBuff));
	pos = snprintf_P( gprs_printfBuff,( sizeof(gprs_printfBuff) - pos ),PSTR("&CTL=%d"), pxFFStatBuffer.RDptr );
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("&LINE=") );
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR( "%04d%02d%02d,"),Aframe.rtc.year,Aframe.rtc.month,Aframe.rtc.day );
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ), PSTR("%02d%02d%02d,"),Aframe.rtc.hour,Aframe.rtc.min, Aframe.rtc.sec );
	// Valores analogicos
	for ( channel = 0; channel < NRO_CHANNELS; channel++) {
		pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("%s>%.3f,"),systemVars.aChName[channel],Aframe.analogIn[channel] );
	}
	// Valores digitales
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ), PSTR("%sP>%.2f,%sL>%d,"), systemVars.dChName[0],Aframe.dIn.pulses[0],systemVars.dChName[0],Aframe.dIn.level[0]);
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ), PSTR("%sP>%.2f,%sL>%d,"), systemVars.dChName[1],Aframe.dIn.pulses[1],systemVars.dChName[1],Aframe.dIn.level[1]);
	// Salidas
	// Bateria
	pos += snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ), PSTR("bt>%.2f"),Aframe.batt );

	// Trasmito:
	FreeRTOS_write( &pdUART0, gprs_printfBuff, pos );
	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("\r\n\0" ));
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}
	//taskYIELD();
	//vTaskDelay( ( TickType_t)( 1000 / portTICK_RATE_MS ) );

	// Chequeo la condicion de trasmision.
	// La memoria esta vacia
	FF_stat(&pxFFStatBuffer);
	GPRS_flags.txNewRcd = TRUE;
	if (pxFFStatBuffer.rcds4rd == 0 )
		GPRS_flags.txNewRcd = FALSE;

	// Complete una window
	 GPRS_counters.txRcdsInFrame--;
	if ( GPRS_counters.txRcdsInFrame == 0 )
		GPRS_flags.txNewRcd = FALSE;

	pv_GPRSprintExitMsg("d11\0");
	return(gSST_ONdata_06);
}
//------------------------------------------------------------------------------------

static int gTR_d12(void)
{
	// gSST_ONdata_06 -> gSST_ONdata_07
	// Trasmito la cola

u08 pos = 0;

	// TAIL ( No mando el close) :
	memset( gprs_printfBuff, '\0', sizeof(gprs_printfBuff));
	pos = snprintf_P( gprs_printfBuff, ( sizeof(gprs_printfBuff) - pos ),PSTR(" HTTP/1.1\n") );
	pos += snprintf_P( &gprs_printfBuff[pos], ( sizeof(gprs_printfBuff) - pos ),PSTR("Host: www.spymovil.com\n" ));
	pos += snprintf_P( &gprs_printfBuff[pos], ( sizeof(gprs_printfBuff) - pos ),PSTR("\n\n" ));
	FreeRTOS_ioctl( &pdUART0,ioctl_UART_CLEAR_RX_BUFFER, NULL);
	FreeRTOS_write( &pdUART0, gprs_printfBuff, pos );

	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		snprintf_P( &gprs_printfBuff[pos],( sizeof(gprs_printfBuff) - pos ),PSTR("\r\n\0" ));
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}

	// Solo espero hasta 10s la respuesta.
	GPRS_counters.cTimer = 10;

	pv_GPRSprintExitMsg("d12\0");
	return(gSST_ONdata_07);
}
//------------------------------------------------------------------------------------

static int gTR_d13(void)
{
	// gSST_ONdata_07 -> gSST_ONdata_08
	// Chequeo si llego una respuesta. Una vez por segundo

u08 pos = 0;

	GPRSrsp = RSP_NONE;
	if ( pv_GPRSrspIs("RX_OK\0", &pos ) == TRUE ) {
		GPRSrsp =  RSP_RXOK;
	}

	//pv_GPRSprintExitMsg("d13\0");
	return(gSST_ONdata_08);

}
//------------------------------------------------------------------------------------

static int gTR_d14(void)
{
	// gSST_ONdata_08 -> gSST_ONdata_11
	// Llego la respuesta correcta. Ajusto la memoria

StatBuffer_t pxFFStatBuffer;

	pv_GPRSprintRsp();

	FF_truncate();
	FF_stat(&pxFFStatBuffer);

	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		tickCount = xTaskGetTickCount();
		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("FSstat:\r\nwrPtr=%d,rdPtr=%d,delPtr=%d,4wr=%d,4rd=%d,4del=%d\r\n\0"),pxFFStatBuffer.WRptr,pxFFStatBuffer.RDptr, pxFFStatBuffer.DELptr,pxFFStatBuffer.rcds4wr,pxFFStatBuffer.rcds4rd,pxFFStatBuffer.rcds4del);
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}

	pv_GPRSprintExitMsg("d14\0");
	return(gSST_ONdata_11);
}
//------------------------------------------------------------------------------------

static int gTR_d15(void)
{
	// gSST_ONdata_08 -> gSST_ONdata_09

	snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR("DEBUG DCD (%d)\r\n\0"), systemVars.dcd );
	FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );

	pv_GPRSprintExitMsg("d15\0");
	return(gSST_ONdata_09);
}
//------------------------------------------------------------------------------------

static int gTR_d16(void)
{
	// gSST_ONdata_09 -> gSST_ONdata_10

	if ( GPRS_counters.cTimer > 0 ) {
		vTaskDelay( ( TickType_t)( 1000 / portTICK_RATE_MS ) );
		GPRS_counters.cTimer--;
	}

	FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
	//pv_GPRSprintExitMsg("d16\0");
	return(gSST_ONdata_10);
}
//------------------------------------------------------------------------------------

static int gTR_d17(void)
{
	// gSST_ONdata_09 -> gSST_OFF
	tkGprs_state = gST_ONopenSocket;

	pv_GPRSprintExitMsg("d17\0");
	return(gSST_ONopenSocket_Entry);
}
//------------------------------------------------------------------------------------

static int gTR_d18(void)
{
	// gSST_ONdata_10 -> gSST_OFF
	tkGprs_state = gST_ONopenSocket;

	pv_GPRSprintExitMsg("d18\0");
	return(gSST_ONopenSocket_Entry);
}
//------------------------------------------------------------------------------------

static int gTR_d19(void)
{
	// gSST_ONdata_10 -> gSST_ONdata_07

	pv_GPRSprintExitMsg("d19\0");
	return(gSST_ONdata_07);
}
//------------------------------------------------------------------------------------

static int gTR_d20(void)
{
	// gSST_ONdata_11 -> gSST_ONdata_01

	pv_GPRSprintExitMsg("d20\0");
	return(gSST_ONdata_01);
}
//------------------------------------------------------------------------------------
// FUNCIONES PRIVADAS
//------------------------------------------------------------------------------------
static void pv_GPRSprintExitMsg(char *code)
{
	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		tickCount = xTaskGetTickCount();
		snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR(".[%06lu] tkGprs: exit %s\r\n\0"), tickCount,code);
		FreeRTOS_write( &pdUART1, gprs_printfBuff, sizeof(gprs_printfBuff) );
	}
}
//------------------------------------------------------------------------------------
static void pv_GPRSloadParameters(void)
{

RtcTimeType_t rtcDateTime;
u16 now;

	// Hora actual en minutos.
	RTC_read(&rtcDateTime);
	now = rtcDateTime.hour * 60 + rtcDateTime.min;

	if ( systemVars.pwrMode == PWR_DISCRETO ) {
		// PWRSAVE ON
		if ( systemVars.pwrSave == modoPWRSAVE_ON ) {
			// Caso 2.1: pwrStart < pwrEnd
			if ( systemVars.pwrSaveStartTime < systemVars.pwrSaveEndTime ) {
				if ( ( now > systemVars.pwrSaveStartTime) && ( now < systemVars.pwrSaveEndTime) ) {
					// Estoy dentro del intervalo de pwrSave.
					GPRS_counters.secs2dial = systemVars.pwrSaveEndTime - now;
				}
			}

			// Caso 2.2: pwrStart > pwrEnd ( deberia ser lo mas comun )
			if ( systemVars.pwrSaveStartTime >= systemVars.pwrSaveEndTime ) {
				if ( now < systemVars.pwrSaveEndTime) {
					GPRS_counters.secs2dial = systemVars.pwrSaveEndTime - now;
				}

				if ( now > systemVars.pwrSaveStartTime ) {
					GPRS_counters.secs2dial = 1440 - now + systemVars.pwrSaveEndTime;
				}
			}
		} else {
			// PWRSAVE OFF
			GPRS_counters.secs2dial = systemVars.timerDial;
		}
	}

	// En modo continuo siempre espero 60s
	if ( systemVars.pwrMode == PWR_CONTINUO )
		GPRS_counters.secs2dial = 60;

	// Excepcion para arrancar rapido al inicio en cualquier modo ( aun pwrSave )
	if ( GPRS_flags.arranque ) {
		GPRS_counters.secs2dial = 30;
	}

	// Si reconfigure la banda gsm, reinicio rapido en cualquier modo
	if ( GPRS_flags.gsmBandOK == FALSE ) {
		GPRS_counters.secs2dial = 30;
	}

	// Si recibi un mensaje de monitor poll, habilito a
	// prender el modem.
	if ( systemVars.wrkMode == WK_MONITOR_SQE ) {
		GPRS_flags.start2dial = TRUE;
	}

	// Al arrancar solo hago 3 reintentos de INIT. Agrego 1 mas para controlar
	// si estoy en INIT o DATa
	GPRS_counters.cInits = 4;
}
//------------------------------------------------------------------------------------
static void pv_dialTimerCallback( TimerHandle_t pxTimer )
{
	// El timer expira c/1s de modo que el tiempo de discado
	// lo llevo en un contador.

	GPRS_counters.secs2dial--;

	if ( GPRS_counters.secs2dial <= 0 ) {	 	// programacion defensiva: uso <=
		// Reajusto el timer
		GPRS_flags.start2dial = TRUE;		// Habilito a discar
		GPRS_counters.secs2dial = systemVars.timerDial;
		return;
	}
}
//------------------------------------------------------------------------------------
static s08 pv_GPRSrspIs(const char *rsp, const size_t *pos)
{
s08 retS = FALSE;
char *p;

	p = cb_strstr(&pdUART0, rsp, pos );
	if ( p == NULL) {
		// NOT FOUND
	} else {
		// FOUND
		retS = TRUE;
	}
	return(retS);
}
//------------------------------------------------------------------------------------
char *cb_strstr(Peripheral_Control_t *UART, const char *s, int *pos )
{
int i;
char *p,*q, *res = NULL;
int stringSize;
s08 inString = FALSE;
s08 rollover = FALSE;

UART_device_control_t *uartDevice;
fifo_handle_s *uartFifo;

	// Busca un substring en un string circular

	uartDevice = (UART_device_control_t *) UART->phDevice;
	uartFifo = (fifo_handle_s *) uartDevice->rxStruct;

	p = uartFifo->buff;
	stringSize = uartFifo->length;
	q = s;

	// Chequeo que el substrig no sea vacio.
    if ( *s == 0) {
    	*pos = 0;
		return p;
    }

	// Recorro el string base. Lo recorro todo ya que como es circular, puede
	// tener un \0 en el medio.
    i = 0;
	inString = FALSE;
    while(1) {
		if ( *p == *q ) {
			if ( inString == FALSE ) {
				res = p;	// Guardo la primer posicion de coincidencia.
				*pos = i;
			}
			inString = TRUE;
			q++;
			if ( *q == '\0')
				// Esta es la unica condicion de salida valida.
				break;
		} else {
			// Reinicio las condiciones de busqueda
			inString = FALSE;
			q = s;
			*pos = 0;
			if ( rollover )	// Ya di vuelta y no coincide.
				break;
		}
		// Avanzo
		p++;
		i++;
		if ( i == stringSize ) {
			// Llegue al final. Rollover.
			i = 0;
			p = uartFifo->buff;
			rollover = TRUE;
		}
    }

    if ( ! inString) {
 		 // FAIL
		res = NULL;
  	}

    return(res);

}
//------------------------------------------------------------------------------------
static void pv_GPRSprintRsp(void)
{

	// Imprime la respuesta a un comando.

char *p;
size_t xBytes;

	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		tickCount = xTaskGetTickCount();
		xBytes = snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR(".[%06lu] tkGprs: Rsp=\r\n\0"),tickCount  );
		FreeRTOS_write( &pdUART1, gprs_printfBuff, xBytes );

		// Imprimo todo el buffer de RX del modem ( 640b). Sale por \0.
		p = FreeRTOS_UART_getFifoPtr(&pdUART0);
		FreeRTOS_write( &pdUART1, p, UART0_RXBUFFER_LEN );
	}
}
//------------------------------------------------------------------------------------
static void pv_GPRSprocessServerClock(void)
{
/* Extraigo el srv clock del string mandado por el server y si el drift con la hora loca
 * es mayor a 5 minutos, ajusto la hora local
 * La linea recibida es del tipo: <h1>INIT_OK:CLOCK=1402251122:PWRM=DISC:</h1>
 *
 */

char *p, *s;
unsigned long serverDate, localDate;
char rtcStr[12];
s08 retS = FALSE;
RtcTimeType_t ltime;
size_t xBytes;

	tickCount = xTaskGetTickCount();
	s = FreeRTOS_UART_getFifoPtr(&pdUART0);
	p = strstr(s, "CLOCK");
	if ( p == NULL )
		return;

	// Incremento para que apunte al str.con la hora.
	p += 6;
	serverDate = atol(p);
	if ( ! RTC_read(&ltime) )
		return;

	// Calculo la hora recibida desde el server:
	ltime.year -= 2000;
	snprintf_P( rtcStr,CHAR256,PSTR("%02d%02d%02d%02d%02d\0"), ltime.year,ltime.month, ltime.day,ltime.hour,ltime.min);
	localDate = atol(rtcStr);

	// Comparo con la hora local.
	if ( abs( serverDate - localDate ) > 5 ) {
		// La diferencia es de mas de 5s: debo reajustar.
		memset(rtcStr, '\0', sizeof(rtcStr));
		memcpy(rtcStr,p, sizeof(rtcStr));
		retS = u_wrRtc(rtcStr);
		xBytes = snprintf_P( gprs_printfBuff,CHAR256,PSTR(".[%06lu] tkGprs::UPDATE SrvTime: %lu, LocalTime: %lu\r\n\0"),tickCount, serverDate, localDate);
	} else {
		xBytes = snprintf_P( gprs_printfBuff,CHAR256,PSTR(".[%06lu] tkGprs::RTC OK; SrvTime: %lu, LocalTime: %lu\r\n\0"),tickCount, serverDate, localDate);
	}

	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		FreeRTOS_write( &pdUART1, gprs_printfBuff, xBytes );
	}

}
//------------------------------------------------------------------------------------
static u08 pv_GPRSprocessPwrMode(void)
{
char *s;
u08 ret = 0;
size_t xBytes;

	tickCount = xTaskGetTickCount();
	s = FreeRTOS_UART_getFifoPtr(&pdUART0);

	if (strstr( s, "PWRM=DISC") != NULL ) {
		u_configPwrMode(PWR_DISCRETO);
		ret = 1;
		xBytes = snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR(".[%06lu] tkGprs:: Reconfig PWRM to DISC\r\n\0"),tickCount);
	}

	if (strstr( s, "PWRM=CONT") != NULL ) {
		u_configPwrMode(PWR_CONTINUO);
		ret = 1;
		xBytes = snprintf_P( gprs_printfBuff,sizeof(gprs_printfBuff),PSTR(".[%06lu] tkGprs:: Reconfig PWRM to CONT\r\n\0"),tickCount);
	}

	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		FreeRTOS_write( &pdUART1, gprs_printfBuff, xBytes );
	}

	return(ret);
}
//------------------------------------------------------------------------------------
static u08 pv_GPRSprocessTimerPoll(void)
{
//	La linea recibida es del tipo: <h1>INIT_OK:CLOCK=1402251122:TPOLL=600:PWRM=DISC:</h1>

char *p, *s;
u08 ret = 0;
char localStr[32];
char *stringp;
char *token;
char *delim = ",=:><";
size_t xBytes;

	tickCount = xTaskGetTickCount();
	s = FreeRTOS_UART_getFifoPtr(&pdUART0);
	p = strstr(s, "TPOLL");
	if ( p == NULL )
		return(0);

	// Copio el mensaje enviado a un buffer local porque la funcion strsep lo modifica.
	memset(localStr,'\0',32);
	memcpy(localStr,p,sizeof(localStr));

	stringp = localStr;
	token = strsep(&stringp,delim);	// TPOLL

	token = strsep(&stringp,delim);	// timerPoll
	u_configTimerPoll(token);
	ret = 1;
	xBytes = snprintf_P( gprs_printfBuff,CHAR256,PSTR(".[%06lu] tkGprs:: Reconfig TPOLL\r\n\0"),tickCount);

	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		FreeRTOS_write( &pdUART1, gprs_printfBuff, xBytes );
	}

	return(ret);
}
//------------------------------------------------------------------------------------
static u08 pv_GPRSprocessTimerDial(void)
{
	//	La linea recibida es del tipo: <h1>INIT_OK:CLOCK=1402251122:TPOLL=600:TDIAL=10300:PWRM=DISC:CD=1230:CN=0530</h1>

char *p, *s;
u08 ret = 0;
char localStr[32];
char *stringp;
char *token;
char *delim = ",=:><";
size_t xBytes;

	tickCount = xTaskGetTickCount();
	s = FreeRTOS_UART_getFifoPtr(&pdUART0);
	p = strstr(s, "TDIAL");
	if ( p == NULL )
		return(0);

	// Copio el mensaje enviado a un buffer local porque la funcion strsep lo modifica.
	memset(localStr,'\0',32);
	memcpy(localStr,p,sizeof(localStr));

	stringp = localStr;
	token = strsep(&stringp,delim);	// TDIAL

	token = strsep(&stringp,delim);	// timerDial
	u_configTimerDial(token);
	ret = 1;
	xBytes = snprintf_P( gprs_printfBuff,CHAR256,PSTR(".[%06lu] tkGprs:: Reconfig TDIAL\r\n\0"),tickCount);

	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		FreeRTOS_write( &pdUART1, gprs_printfBuff, xBytes );
	}

	return(ret);
}
//------------------------------------------------------------------------------------
static u08 pv_GPRSprocessPwrSave(void)
{
//	La linea recibida es del tipo:
//	<h1>INIT_OK:CLOCK=1402251122:TPOLL=600:TDIAL=10300:PWRS=1,2230,0600:D0=q0,1.00:D1=q1,1.00</h1>
//  Las horas estan en formato HHMM.

u08 ret = 0;
char localStr[32];
char *stringp;
char *token;
char *delim = ",=:><";
char *p1,*p2;
u08 modo;
char *p, *s;
size_t xBytes;

	tickCount = xTaskGetTickCount();
	s = FreeRTOS_UART_getFifoPtr(&pdUART0);
	if ( p == NULL )
		return(0);

	// Copio el mensaje enviado a un buffer local porque la funcion strsep lo modifica.
	memset(localStr,'\0',32);
	memcpy(localStr,p,sizeof(localStr));

	stringp = localStr;
	token = strsep(&stringp,delim);	//PWRS

	token = strsep(&stringp,delim);	// modo
	modo = atoi(token);
	p1 = strsep(&stringp,delim);	// startTime
	p2 = strsep(&stringp,delim); 	// endTime

	u_configPwrSave(modo, p1, p2);
	xBytes = snprintf_P( gprs_printfBuff,CHAR256,PSTR(".[%06lu] tkGprs:: Reconfig PWRS\r\n\0"),tickCount );
	ret = 1;

	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		FreeRTOS_write( &pdUART1, gprs_printfBuff, xBytes );
	}

	return(ret);

}
/*------------------------------------------------------------------------------------*/
static u08 pv_GPRSprocessAch(u08 channel)
{
//	La linea recibida es del tipo:
//	<h1>INIT_OK:CLOCK=1402251122:TPOLL=600:TDIAL=10300:PWRM=DISC:A0=pA,0,20,0,6:A1=pB,0,20,0,10:A2=pC,0,20,0,10:D0=q0,1.00:D1=q1,1.00</h1>

u08 ret = 0;
char localStr[32];
char *stringp;
char *token;
char *delim = ",=:><";
char *chName,*s_iMin,*s_iMax,*s_mMin,*s_mMax;
char *s;
size_t xBytes;

	tickCount = xTaskGetTickCount();
	s = FreeRTOS_UART_getFifoPtr(&pdUART0);
	switch (channel) {
	case 0:
		stringp = strstr(s, "A0=");
		break;
	case 1:
		stringp = strstr(s, "A1=");
		break;
	case 2:
		stringp = strstr(s, "A2=");
		break;
	default:
		ret = 0;
		goto quit;
		break;
	}

	if ( stringp == NULL ) {
		ret = 0;
		goto quit;
	}

	// Copio el mensaje enviado a un buffer local porque la funcion strsep lo modifica.
	memset(localStr,'\0',32);
	memcpy(localStr,stringp,31);

	stringp = localStr;
	token = strsep(&stringp,delim);	//A0

	chName = strsep(&stringp,delim);	//name
	s_iMin = strsep(&stringp,delim);	//iMin
	s_iMax = strsep(&stringp,delim);	//iMax
	s_mMin = strsep(&stringp,delim);	//mMin
	s_mMax = strsep(&stringp,delim);	//mMax

	u_configAnalogCh( channel, chName,s_iMin,s_iMax,s_mMin,s_mMax );
	ret = 1;
	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		xBytes = snprintf_P( gprs_printfBuff,CHAR256,PSTR(".[%06lu] tkGprs::Reconfig A%d\r\n\0"),tickCount, channel);
		FreeRTOS_write( &pdUART1, gprs_printfBuff, xBytes );
	}

quit:
	return(ret);
}
/*------------------------------------------------------------------------------------*/
static u08 pv_GPRSprocessDch(u08 channel)
{

//	La linea recibida es del tipo:
//	<h1>INIT_OK:CLOCK=1402251122:TPOLL=600:TDIAL=10300:PWRM=DISC:A0=pA,0,20,0,6:A1=pB,0,20,0,10:A2=pC,0,20,0,10:D0=q0,1.00:D1=q1,1.00</h1>

u08 ret = 0;
char localStr[32];
char *stringp;
char *token;
char *delim = ",=:><";
char *chName, *s_magPP;
char *s;
size_t xBytes;

	tickCount = xTaskGetTickCount();
	s = FreeRTOS_UART_getFifoPtr(&pdUART0);
	switch (channel) {
	case 0:
		stringp = strstr(s, "D0=");
		break;
	case 1:
		stringp = strstr(s, "D1=");
		break;
	default:
		ret = 0;
		goto quit;
		break;
	}

	if ( stringp == NULL ) {
		ret = 0;
		goto quit;
	}

	// Copio el mensaje enviado a un buffer local porque la funcion strsep lo modifica.
	memset(localStr,'\0',32);
	memcpy(localStr,stringp,31);

	stringp = localStr;
	token = strsep(&stringp,delim);	//D0

	chName = strsep(&stringp,delim);	//name
	s_magPP = strsep(&stringp,delim);	//magPp
	u_configDigitalCh( channel, chName, s_magPP );
	ret = 1;
	if ( (systemVars.debugLevel & D_GPRS) != 0) {
		tickCount = xTaskGetTickCount();
		xBytes = snprintf_P( gprs_printfBuff,CHAR256,PSTR(".[%06lu] tkGprs::Reconfig D%d channel\r\n\0"),tickCount, channel );
		FreeRTOS_write( &pdUART1, gprs_printfBuff, xBytes );
	}

quit:

	return(ret);

}
/*------------------------------------------------------------------------------------*/
// FUNCIONES PUBLICAS
//------------------------------------------------------------------------------------
void tkGprsInit(void)
{
	// Esta funcion se utiliza  antes de arrancar el FRTOS de modo que cree
	// el timer que necesitamos en este modulo
	// Expira c/1sec

	dialTimer = xTimerCreate (  "DIAL_T",
	                     /* The timer period in ticks, must be greater than 0. */
	                     ( 1000 / portTICK_PERIOD_MS) ,
	                     /* The timers will auto-reload themselves when they expire. */
	                     pdTRUE,
	                     /* Assign each timer a unique id equal to its array index. */
	                     ( void * ) NULL,
	                     /* Each timer calls the same callback when it expires. */
						 pv_dialTimerCallback
	                   );

	if ( dialTimer == NULL )
		u_panic(P_OUT_TIMERCREATE);
}
//--------------------------------------------------------------------------------------
s32 u_readTimeToNextDial(void)
{
s16 retVal = -1;

	// Lo determina en base al time elapsed y el timerPoll.
	// El -1 indica un modo en que no esta poleando.
	if ( systemVars.wrkMode == WK_NORMAL )  {
		retVal = GPRS_counters.secs2dial;
	}

	return (retVal);
}
//--------------------------------------------------------------------------------------
s08 u_modemPrendido(void)
{
	return(GPRS_flags.modemPrendido);
}
//--------------------------------------------------------------------------------------
