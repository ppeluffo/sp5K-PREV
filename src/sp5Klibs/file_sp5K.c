/*
 * file_sp5K.c
 *
 *  Created on: 31/10/2015
 *      Author: pablo
 */


//------------------------------------------------------------------------------------

#include "file_sp5K.h"

//#define DEBUG_FF

static u08 pv_memChecksum( char *buff, u08 limit );

//------------------------------------------------------------------------------------
size_t FF_fopen(void)
{
	/*  Debe correrse luego de iniciado el FRTOS ya que utiliza funciones de este !!!
	    Abre el archivo de memoria extEE.
	    Lo recorre buscando el ppio. y el final e inicializa el FCB
	 	Inicializa el sistema de la memoria ( punteros )
		Recorro la memoria buscando transiciones VACIO->DATO y DATO->VACIO.
		La transicion VACIO->DATO determina el puntero DELptr
		La transicion DATO->VACIO determina el puntero WRptr.
		Si no hay transiciones y todos son datos, la memoria esta llena.
		Si no hay transicions y todos son blancos, la memoria esta vacia.
		Si todo anda bien, retorna en ERRNO un NONE.
		En otro caso retorna el recdNbr del error y setea la variable ERRNO
	 */

char mark_Z, mark;
u16 xPos;
s08 transicion = FALSE;
u16 val = 0;
size_t xReturn = 0U;

	// Lo primero es obtener el semaforo del I2C
	FreeRTOS_ioctl(&pdI2C,ioctlOBTAIN_BUS_SEMPH, NULL);
	FCB.ff_stat.errno = pdFF_ERRNO_NONE;
	// Indicamos el periferico i2c al cual quiero acceder
	val = EE_ADDR;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_DEVADDRESS, &val);
	// Luego indicamos la direccion desde donde leer del dispositivo: largo ( en la ee son 2 bytes )
	val = 2;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_BYTEADDRESSLENGTH, &val);

	// El primer registro que leo es el ultimo del archivo
	// direccion de lectura
	xPos = (FF_MAX_RCDS -1);
	val = FF_ADDR_START +  xPos * FF_RECD_SIZE;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_BYTEADDRESS,&val);
	// leo una pagina entera, (recd) 64 bytes.
	memset( FCB.ff_buffer,'\0', sizeof(FCB.ff_buffer) );
	xReturn = FreeRTOS_read(&pdI2C, &FCB.ff_buffer, FF_RECD_SIZE);

#ifdef DEBUG_FF
	snprintf_P( debug_printfBuff,sizeof(debug_printfBuff),PSTR("FO: [%d][%d]\r\n\0"),FF_RECD_SIZE, xReturn);
	FreeRTOS_write( &pdUART1, debug_printfBuff, sizeof(debug_printfBuff) );
#endif

	if ( xReturn != FF_RECD_SIZE ) {
		xReturn = xPos;
		FCB.ff_stat.errno = pdFF_ERRNO_INIT;
		goto quit;
	}
	mark_Z = FCB.ff_buffer[sizeof(FCB.ff_buffer) - 1];

	// Recorro toda la memoria EE buscando transiciones.
	for ( xPos=0; xPos < FF_MAX_RCDS; xPos++) {
		val = FF_ADDR_START + xPos * FF_RECD_SIZE;
		FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_BYTEADDRESS,&val);
		// leo una pagina entera, (recd) 64 bytes.
		memset( FCB.ff_buffer,'\0', sizeof(FCB.ff_buffer) );
		xReturn = FreeRTOS_read(&pdI2C, &FCB.ff_buffer, FF_RECD_SIZE);

#ifdef DEBUG_FF
//		snprintf_P( debug_printfBuff,sizeof(debug_printfBuff),PSTR("FO: [%d][%d][%d][%d]\r\n\0"),xPos, val,FF_RECD_SIZE, xReturn);
//		FreeRTOS_write( &pdUART1, debug_printfBuff, sizeof(debug_printfBuff) );
#endif
		if ( xReturn != FF_RECD_SIZE )  {
			FCB.ff_stat.errno = pdFF_ERRNO_INIT;
			goto quit;
		}

		mark = FCB.ff_buffer[sizeof(FCB.ff_buffer) - 1];

#ifdef DEBUG_FF
		if ( mark == 0xC5 ) {
			snprintf_P( debug_printfBuff,sizeof(debug_printfBuff),PSTR("FO: [%d][%d][%d][%d][0X%03x]\r\n\0"),xPos, val,FF_RECD_SIZE, xReturn, mark);
			FreeRTOS_write( &pdUART1, debug_printfBuff, sizeof(debug_printfBuff) );
		}
#endif

		// busco transiciones:
		if ( ( mark_Z == '\0') && ( mark == 0xC5) ) {
			// Tengo una transicion VACIO->DATO.
			FCB.ff_stat.DELptr = xPos;
			FCB.ff_stat.RDptr = xPos;
			transicion = TRUE;
		}

		if ( ( mark_Z == 0xC5) && ( mark == '\0') ) {
			// Tengo una transicion DATO->VACIO.
			FCB.ff_stat.WRptr = xPos;
			transicion = TRUE;
		}

		mark_Z = mark;
	}

	// Recorri toda la memoria. Analizo las transiciones...
	if ( ! transicion ) {
		// Si no hubieron transiciones es que la memoria esta llena o vacia.
		if ( mark == '\0') {
			// Memoria vacia.
			FCB.ff_stat.RDptr = 0;
			FCB.ff_stat.WRptr = 0;
			FCB.ff_stat.DELptr = 0;
			FCB.ff_stat.rcds4wr = FF_MAX_RCDS;
			FCB.ff_stat.rcds4rd = 0;
			FCB.ff_stat.rcds4del = 0;
		} else {
			// Memoria llena
			FCB.ff_stat.DELptr = 0;
			FCB.ff_stat.RDptr = 0;
			FCB.ff_stat.WRptr = 0;
			FCB.ff_stat.rcds4wr = 0;
			FCB.ff_stat.rcds4rd = FF_MAX_RCDS;
			FCB.ff_stat.rcds4del = FF_MAX_RCDS;
		}
	} else {
		// Memoria con datos. Calculo los registro ocupados.
		if ( FCB.ff_stat.WRptr > FCB.ff_stat.DELptr) {
			FCB.ff_stat.rcds4rd = ( FCB.ff_stat.WRptr - FCB.ff_stat.DELptr);
			FCB.ff_stat.rcds4del = FCB.ff_stat.rcds4rd;
			FCB.ff_stat.rcds4wr = FF_MAX_RCDS - FCB.ff_stat.rcds4rd;
		} else {
			FCB.ff_stat.rcds4rd = FF_MAX_RCDS -FCB.ff_stat.DELptr + FCB.ff_stat.WRptr;
			FCB.ff_stat.rcds4del = FCB.ff_stat.rcds4rd;
			FCB.ff_stat.rcds4wr = FF_MAX_RCDS - FCB.ff_stat.rcds4rd;
		}
	}

quit:

	FreeRTOS_ioctl(&pdI2C,ioctlRELEASE_BUS_SEMPH, NULL);

#ifdef DEBUG_FF
		snprintf_P( debug_printfBuff,sizeof(debug_printfBuff),PSTR("FO: [%d][%d]\r\n\0"),xPos, FCB.ff_stat.errno);
		FreeRTOS_write( &pdUART1, debug_printfBuff, sizeof(debug_printfBuff) );
#endif

	return(xPos);

}
//------------------------------------------------------------------------------------
size_t FF_fwrite( const void *pvBuffer, size_t xSize)
{
	// El archivo es del tipo circular FirstIN-LastOUT.
	// Escribe un registro en la posicion apuntada por el WRptr.
	// El registro que se pasa en pvBuffer es del tipo 'frameData_t' de 38 bytes
	// pero en la memoria voy a escribir de a paginas de 64 bytes.
	// Retorna el nro.de bytes escritos y setea la variable 'errno' del FCB
	// En la posicion 63 grabo un tag con el valor 0xC5 para poder luego
	// determinar si el registro esta ocupado o vacio.

u16 val = 0;
size_t xReturn = 0U;
u16 tryes;

	// Lo primero es obtener el semaforo del I2C
	FreeRTOS_ioctl(&pdI2C,ioctlOBTAIN_BUS_SEMPH, NULL);
	FCB.ff_stat.errno = pdFF_ERRNO_NONE;

	// Si la memoria esta llena no puedo escribir: salgo
	if ( FCB.ff_stat.rcds4del == FF_MAX_RCDS ) {
		FCB.ff_stat.errno = pdFF_ERRNO_MEMFULL;
		goto quit;
	}

	// inicializo la estructura lineal temporal en el FCB para copiar ahi los datos y
	// calcular el checksum antes de grabarlo en memoria.
	memset( FCB.ff_buffer,'\0', sizeof(FCB.ff_buffer) );
	// copio los datos recibidos del frame al buffer ( 0..(xSize-1))
	memcpy ( FCB.ff_buffer, pvBuffer, xSize );
	// Calculo y grabo el checksum a continuacion del frame (en la pos.xSize)
	// El checksum es solo del dataFrame por eso paso dicho size.
	FCB.ff_buffer[xSize] = pv_memChecksum(FCB.ff_buffer, xSize );
	// Grabo el tag para indicar que el registro esta ocupado
	FCB.ff_buffer[sizeof(FCB.ff_buffer) - 1] = FF_TAG;

	// EE WRITE:
	// Luego indicamos el periferico i2c en el cual queremos leer
	val = EE_ADDR;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_DEVADDRESS, &val);
	// Luego indicamos la direccion a escribir del dispositivo: largo ( en la ee son 2 bytes )
	val = 2;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_BYTEADDRESSLENGTH, &val);
	// y direccion interna en la EE.(comienzo del registro / frontera)
	val = FF_ADDR_START + FCB.ff_stat.WRptr * FF_RECD_SIZE;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_BYTEADDRESS,&val);

	// Por ultimo escribo la memoria. Escribo un pagina entera, 64 bytes.

	for ( tryes = 0; tryes < 3; tryes++ ) {
		// Write
		xReturn = FreeRTOS_write(&pdI2C, &FCB.ff_buffer, FF_RECD_SIZE);
		taskYIELD();
		// Verify
		FreeRTOS_read(&pdI2C, &FCB.check_buffer, FF_RECD_SIZE);
		if ( memcmp (&FCB.check_buffer, &FCB.ff_buffer, FF_RECD_SIZE) == 0 )
			break;
		if  ( tryes == 3 ) {
			snprintf_P( debug_printfBuff,sizeof(debug_printfBuff),PSTR("FFWR ERR: [%d]\r\n\0"), val);
			FreeRTOS_write( &pdUART1, debug_printfBuff, sizeof(debug_printfBuff) );
			FCB.ff_stat.errno = pdFF_ERRNO_MEMWR;
			xReturn = 0U;
			goto quit;
		}
	}

	if (xReturn != FF_RECD_SIZE ) {
		// Errores de escritura ?
		FCB.ff_stat.errno = pdFF_ERRNO_MEMWR;
		xReturn = 0U;
		goto quit;
	} else {
		// OK: Ajusto los punteros
		FCB.ff_stat.rcds4rd++;
		FCB.ff_stat.rcds4del++;
		FCB.ff_stat.rcds4wr--;
		xReturn = xSize;
		// Avanzo el puntero de WR en modo circular
		FCB.ff_stat.WRptr = (++FCB.ff_stat.WRptr == FF_MAX_RCDS) ?  0 : FCB.ff_stat.WRptr;
	}

quit:
	// libero los semaforos
	FreeRTOS_ioctl(&pdI2C,ioctlRELEASE_BUS_SEMPH, NULL);
	return(xReturn);

}
//------------------------------------------------------------------------------------
size_t FF_fread( void *pvBuffer, size_t xSize)
{
	// Lee un registro apuntado por RDptr.
	// Retorna TRUE o FALSE y en este caso setea la variable 'errno' del FCB
	// Lee un registro ( como string ) y lo copia al espacio de memoria apuntado
	// *pvBuffer. El puntero es void y entonces debemos pasar el tamaño de la estructura
	// a la que apunta.
	// En caso que tenga problemas para leer o de checksum, debo continuar e indicar
	// el error.

u16 val = 0;
size_t xReturn = 0U;
u08 rdCheckSum;

	// Lo primero es obtener el semaforo del I2C
	FreeRTOS_ioctl(&pdI2C,ioctlOBTAIN_BUS_SEMPH, NULL);
	FCB.ff_stat.errno = pdFF_ERRNO_NONE;

	// Si la memoria esta vacia salgo
	if ( FCB.ff_stat.rcds4rd == 0 ) {
		FCB.ff_stat.errno = pdFF_ERRNO_MEMEMPTY;
		goto quit;
	}

	// inicializo la estructura lineal temporal en el FCB.
	memset( FCB.ff_buffer,'\0', sizeof(FCB.ff_buffer) );
	// EE READ:
	// Indicamos el periferico i2c al cual quiero acceder
	val = EE_ADDR;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_DEVADDRESS, &val);
	// Luego indicamos la direccion desde donde leer del dispositivo: largo ( en la ee son 2 bytes )
	val = 2;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_BYTEADDRESSLENGTH, &val);
	// y direccion interna en la EE.(comienzo del registro / frontera)
	val = FF_ADDR_START + FCB.ff_stat.RDptr * FF_RECD_SIZE;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_BYTEADDRESS,&val);
	// Por ultimo leo la memoria: un pagina entera, (recd) 64 bytes.
	xReturn = FreeRTOS_read(&pdI2C, &FCB.ff_buffer, FF_RECD_SIZE);

	// Avanzo el puntero de RD en modo circular siempre !!
	FCB.ff_stat.RDptr = (++FCB.ff_stat.RDptr == FF_MAX_RCDS) ?  0 : FCB.ff_stat.RDptr;
	FCB.ff_stat.rcds4rd--;

	// Copio los datos a la estructura de salida.: aun no se si estan correctos
	memcpy( pvBuffer, &FCB.ff_buffer, xSize );

	// Errores de lectura ?
	// Solo indico los errores, pero igual devuelvo el recd. para no trancarme
	if (xReturn != FF_RECD_SIZE ) {
		FCB.ff_stat.errno = pdFF_ERRNO_MEMRD;
		xReturn = 0U;
		goto quit;
	}

	// Verifico los datos leidos ( checksum )
	// El checksum es solo del dataFrame por eso paso dicho size.
	rdCheckSum = pv_memChecksum(FCB.ff_buffer, xSize );
	if ( rdCheckSum != FCB.ff_buffer[xSize] ) {
		FCB.ff_stat.errno = pdFF_ERRNO_RDCKS;
		xReturn = 0U;
		goto quit;
	}

	// Vemos si la ultima posicion tiene el tag de ocupado.
	if ( FCB.ff_buffer[sizeof(FCB.ff_buffer) - 1] != FF_TAG ) {
		FCB.ff_stat.errno = pdFF_ERRNO_RDNOTAG;
		xReturn = 0U;
		goto quit;
	}

	// Datos leidos correctamente
	xReturn = xSize;

quit:
	// libero los semaforos
	FreeRTOS_ioctl(&pdI2C,ioctlRELEASE_BUS_SEMPH, NULL);
	return(xReturn);
}
//------------------------------------------------------------------------------------
void FF_stat( StatBuffer_t *pxStatBuffer )
{
	// Copia por el puntero *pxStatBuffer pasado, la estructura de control del FCB
	// En caso de error devuelve FALSE pero no tiene sentido que setee el errno.
	FreeRTOS_ioctl(&pdI2C,ioctlOBTAIN_BUS_SEMPH, NULL);
	memcpy( pxStatBuffer, &FCB.ff_stat, sizeof(StatBuffer_t));
	FreeRTOS_ioctl(&pdI2C,ioctlRELEASE_BUS_SEMPH, NULL);

}
//------------------------------------------------------------------------------------
s08 FF_seek(void)
{
	// Ajusta la posicion del puntero de lectura al primer registro a leer, es decir
	// al DELptr.
	FreeRTOS_ioctl(&pdI2C,ioctlOBTAIN_BUS_SEMPH, NULL);
	FCB.ff_stat.RDptr = FCB.ff_stat.DELptr;
	FreeRTOS_ioctl(&pdI2C,ioctlRELEASE_BUS_SEMPH, NULL);
	return(TRUE);

}
//------------------------------------------------------------------------------------
s08 FF_truncate(void)
{
	// Borra todos los registros que ya fueron leidos ( descarta )

u16 val = 0;
size_t xReturn = 0U;

	// Lo primero es obtener el semaforo del I2C
	FreeRTOS_ioctl(&pdI2C,ioctlOBTAIN_BUS_SEMPH, NULL);
	FCB.ff_stat.errno = pdFF_ERRNO_NONE;

	// Voy a escribir en c/registro un registro en blanco
	memset( FCB.ff_buffer,'\0', sizeof(FCB.ff_buffer) );
	// Luego indicamos el periferico i2c en el cual queremos leer
	val = EE_ADDR;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_DEVADDRESS, &val);
	// Luego indicamos la direccion a escribir del dispositivo: largo ( en la ee son 2 bytes )
	val = 2;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_BYTEADDRESSLENGTH, &val);

	// Mientras hallan registros ya leidos y no borrados, los borro.
	while ( FCB.ff_stat.rcds4del > FCB.ff_stat.rcds4rd ) {

		// y direccion interna en la EE.(comienzo del registro / frontera)
		val = FF_ADDR_START + FCB.ff_stat.DELptr * FF_RECD_SIZE;
		FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_BYTEADDRESS,&val);
		// Por ultimo escribo la memoria. Escribo un pagina entera, 64 bytes.
		xReturn = FreeRTOS_write(&pdI2C, &FCB.ff_buffer, FF_RECD_SIZE);

		// Por ahora no controlo los errores de borrado
		// OK: Ajusto los punteros
		FCB.ff_stat.rcds4wr++;
		FCB.ff_stat.rcds4del--;
		FCB.ff_stat.DELptr = (++FCB.ff_stat.DELptr == FF_MAX_RCDS) ?  0 : FCB.ff_stat.DELptr;
	}

	// libero los semaforos
	FreeRTOS_ioctl(&pdI2C,ioctlRELEASE_BUS_SEMPH, NULL);
	return(xReturn);
}
//------------------------------------------------------------------------------------
s08 FF_rewind(void)
{
	// Borra el archivo y lo lleva a su condicion inicial.
	// Inicializa la memoria. Como lleva bastante tiempo, tenemos problemas con el
	// watchdog. Por esto desde donde la invocamos debemos desactivarlo y esta
	// funcion SOLO debe usarse desde CMD.

u16 val = 0;
u16 tryes;
u16 xPos;

	wdt_reset();
	wdt_disable();

	// Lo primero es obtener el semaforo del I2C
	FreeRTOS_ioctl(&pdI2C,ioctlOBTAIN_BUS_SEMPH, NULL);

	// inicializo la estructura lineal temporal del FCB.
	memset( FCB.ff_buffer,'\0', sizeof(FCB.ff_buffer) );

	// EE WRITE:
	// Luego indicamos el periferico i2c en el cual queremos leer
	val = EE_ADDR;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_DEVADDRESS, &val);
	// Luego indicamos la direccion a escribir del dispositivo: largo ( en la ee son 2 bytes )
	val = 2;
	FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_BYTEADDRESSLENGTH, &val);
	// Ciclo de borrado
	for ( xPos = 0; xPos < FF_MAX_RCDS; xPos++) {
		// direccion interna en la EE.(comienzo del registro / frontera)
		val = FF_ADDR_START + xPos * FF_RECD_SIZE;
		FreeRTOS_ioctl(&pdI2C,ioctl_I2C_SET_BYTEADDRESS,&val);

		for ( tryes = 0; tryes < 3; tryes++ ) {
			// Borro: escribo un pagina entera, 64 bytes con los '\0'
			FreeRTOS_write(&pdI2C, &FCB.ff_buffer, FF_RECD_SIZE);
			taskYIELD();
			// Leo y verifico
			FreeRTOS_read(&pdI2C, &FCB.check_buffer, FF_RECD_SIZE);
			if ( memcmp (&FCB.check_buffer, &FCB.ff_buffer, FF_RECD_SIZE) == 0 )
				break;
			if  ( tryes == 3 ) {
				snprintf_P( debug_printfBuff,sizeof(debug_printfBuff),PSTR("FFrew ERR: %d,%d\r\n\0"),xPos, val);
				FreeRTOS_write( &pdUART1, debug_printfBuff, sizeof(debug_printfBuff) );
			}

		}
		// Imprimo realimentacion c/32 recs.
		if ( (xPos % 32) == 0 ) {
			FreeRTOS_write( &pdUART1, ".\0", sizeof(".\0") );
		}
	}
	FreeRTOS_write( &pdUART1, "\r\n\0", sizeof("\r\n\0") );
	FreeRTOS_ioctl(&pdI2C,ioctlRELEASE_BUS_SEMPH, NULL);
	// RESET
	wdt_enable(WDTO_30MS);
	while(1) {}

}
//------------------------------------------------------------------------------------
int FF_errno( void )
{
	// Retorna el codigo de error de errno.
	return( FCB.ff_stat.errno);
}
//------------------------------------------------------------------------------------
static u08 pv_memChecksum( char *buff, u08 limit )
{
u08 checksum = 0;
u08 i;

	for( i=0; i<limit; i++)
		checksum += buff[i];

	checksum = ~checksum;
	return (checksum);
}
//----------------------------------------------------------------------------------
