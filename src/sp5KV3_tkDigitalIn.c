/*
 * sp5KV3_tkDigitalIn.c
 *
 *  Created on: 13/4/2015
 *      Author: pablo
 *
 *  La nueva modalidad es por poleo.
 *  Configuro el MCP para que no interrumpa
 *  C/100ms leo el registro GPIO del MCP.
 *  En reposo la salida de los latch es 1 por lo que debo detectar cuando se hizo 0.
 *  Para evitar poder quedar colgado, c/ciclo borro el latch.
 *  Esto implica que no importa la duracion del pulso ya que lo capturo con un flip-flop, pero
 *  no pueden venir mas rapido que 10/s.
 *
 *	Esta version solo puede usarse con placas SP5K_3CH que tengan latch para los pulsos, o sea
 *	version >= R003.
 *
 */


#include <sp5KV3.h>

static void pv_clearQ(void);
static void pv_pollQ(void);
static void pv_pollDcd(void);

char dIn_printfBuff[CHAR64];	// Buffer de impresion
dinData_t digIn;				// Estructura local donde cuento los pulsos.

/*------------------------------------------------------------------------------------*/
void tkDigitalIn(void * pvParameters)
{

( void ) pvParameters;

	// Los pines del micro que resetean los latches de caudal son salidas.
	sbi(Q_DDR, Q0_CTL_PIN);
	sbi(Q_DDR, Q1_CTL_PIN);

	vTaskDelay( ( TickType_t)( 500 / portTICK_RATE_MS ) );
	snprintf_P( dIn_printfBuff,sizeof(dIn_printfBuff),PSTR("starting tkDigitalIn..\r\n\0"));
	FreeRTOS_write( &pdUART1, dIn_printfBuff, sizeof(dIn_printfBuff) );

	// Inicializo los latches borrandolos
	pv_clearQ();
	digIn.level[0] = 0;
	digIn.level[1] = 0;
	digIn.pulses[0] = 0;
	digIn.pulses[1] = 0;

	// Inicializo el valor del dcd.
	MCP_queryDcd(&systemVars.dcd);

	for( ;; )
	{
		u_clearWdg(WDG_DIN);
		vTaskDelay( ( TickType_t)( 100 / portTICK_RATE_MS ) );
		pv_pollDcd();
		pv_pollQ();

	}

}
/*------------------------------------------------------------------------------------*/
void u_readDigitalCounters( dinData_t *dIn , s08 resetCounters )
{
	// copio los valores de los contadores en la estructura dIn.
	// Si se solicita, luego se ponen a 0.

	memcpy( dIn, &digIn, sizeof(dinData_t)) ;
	if ( resetCounters == TRUE ) {
		digIn.level[0] = 0;
		digIn.level[1] = 0;
		digIn.pulses[0] = 0;
		digIn.pulses[1] = 0;
	}
}
/*------------------------------------------------------------------------------------*/
static void pv_pollDcd(void)
{
s08 retS = FALSE;
u08 pin;
u32 tickCount;

	retS = MCP_queryDcd(&pin);
	// Solo indico los cambios.
	if ( systemVars.dcd != pin ) {
		systemVars.dcd = pin;
		tickCount = xTaskGetTickCount();
		if ( pin == 1 ) { snprintf_P( dIn_printfBuff,sizeof(dIn_printfBuff),PSTR(".[%06lu] tkDigitalIn: DCD off(1)\r\n\0"), tickCount );	}
		if ( pin == 0 ) { snprintf_P( dIn_printfBuff,sizeof(dIn_printfBuff),PSTR(".[%06lu] tkDigitalIn: DCD on(0)\r\n\0"), tickCount );	}

		if ( (systemVars.debugLevel & D_DIGITAL) != 0) {
			FreeRTOS_write( &pdUART1, dIn_printfBuff, sizeof(dIn_printfBuff) );
		}
	}
}
/*------------------------------------------------------------------------------------*/
static void pv_pollQ(void)
{

s08 retS = FALSE;
u08 din0 = 0;
u08 din1 = 0;
s08 debugQ = FALSE;
u32 tickCount;

	// Leo el GPIO.
	retS = MCP_query2Din( &din0, &din1 );
	if ( retS ) {
		// Levels
		digIn.level[0] = din0;
		digIn.level[1] = din1;

		// Counts
		debugQ = FALSE;
		if (din0 == 0 ) { digIn.pulses[0]++ ; debugQ = TRUE;}
		if (din1 == 0 ) { digIn.pulses[1]++ ; debugQ = TRUE;}
	} else {
		snprintf_P( dIn_printfBuff,sizeof(dIn_printfBuff),PSTR("tkDigitalIn: READ DIN ERROR !!\r\n\0"));
		FreeRTOS_write( &pdUART1, dIn_printfBuff, sizeof(dIn_printfBuff) );
	}

	// Siempre borro los latches para evitar la posibilidad de quedar colgado.
	pv_clearQ();

	if ( ((systemVars.debugLevel & D_DIGITAL) != 0) && debugQ ) {
		tickCount = xTaskGetTickCount();
		snprintf_P( dIn_printfBuff,sizeof(dIn_printfBuff),PSTR(".[%06lu] tkDigitalIn: din0=%.0f,din1=%.0f\r\n\0"), tickCount, digIn.pulses[0],digIn.pulses[1] );
		FreeRTOS_write( &pdUART1, dIn_printfBuff, sizeof(dIn_printfBuff) );
	}

}
/*------------------------------------------------------------------------------------*/
static void pv_clearQ(void)
{
	// Pongo un pulso 0->1 en Q0/Q1 pin para resetear el latch
	// En reposo debe quedar en H.
	cbi(Q_PORT, Q0_CTL_PIN);
	cbi(Q_PORT, Q1_CTL_PIN);
	taskYIELD();
	sbi(Q_PORT, Q0_CTL_PIN);
	sbi(Q_PORT, Q1_CTL_PIN);
}
/*------------------------------------------------------------------------------------*/