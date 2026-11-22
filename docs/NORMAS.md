# Navtex · Normas y reglamentos

© 2026 Manuel - EA7JRS · versión 1.5.0 · ediciones comprobadas el 22 de noviembre de 2026

Este documento dice qué normas contempla el programa, qué aplica de cada una y qué no. Sirve para saber hasta dónde se puede confiar en él y para revisar el código contra el texto de cada norma.

> **Navtex no es un equipo homologado.** No tiene aprobación de tipo ni marca de conformidad de equipos marinos, y no sustituye al receptor NAVTEX que el Convenio SOLAS exige a los buques obligados a llevarlo. Es una ayuda: un receptor por software que sigue las reglas de funcionamiento que esas normas piden a un receptor, en lo que un programa puede seguirlas.

## Resumen

| Norma | De qué trata | Qué se aplica |
| :--- | :--- | :--- |
| Recomendación UIT-R M.540-2 (1990), en vigor | Características de funcionamiento y técnicas del sistema NAVTEX | Formato del mensaje, identidad B1B2B3B4, número 00, temas que no se pueden rechazar |
| Recomendaciones UIT-R M.476 y M.625 | Telegrafía de impresión directa de banda estrecha; modo B colectivo (corrección de errores sin canal de retorno) | La señal que se descodifica: 100 baudios, desplazamiento de 170 Hz |
| Resolución MSC.508(105) de la OMI (2022) | Normas de funcionamiento para recibir información de seguridad marítima por MF (NAVTEX) y HF. **Es la vigente:** se aplica a los equipos instalados desde el 1 de enero de 2024 | Tasa de errores, memoria de identidades y de mensajes, mensajes marcados para conservarlos, asterisco, alerta de búsqueda y salvamento, interfaz de datos |
| Resolución MSC.148(77) de la OMI (2003), enmendada por MSC.430(98) | La norma anterior, para equipos instalados entre 2005 y 2023 | Coincide con la vigente en la tasa de errores, la memoria de identidades, el asterisco, la alarma y la interfaz de datos |
| Manual NAVTEX de la OMI, edición de 2023 (MSC.1/Circ.1403/Rev.2) | Organización del servicio: turnos, temas, prioridades | Turnos de emisión por letra, temas A, B, D y L, número 00 |
| IEC 61097-6, edición 2.2 (2005 con las enmiendas de 2011 y 2019) | Requisitos y ensayos del receptor NAVTEX del SMSSM | Sentencias `NRX`, `ALR` y `ACK`; alarma de búsqueda y salvamento |
| IEC 61162-1 (NMEA 0183) | Interfaz serie entre equipos marinos | Forma de las sentencias: suma de control, longitud, caracteres reservados |
| Reglamento de Radiocomunicaciones de la UIT, edición de 2024 | Atribución de frecuencias | Solo recepción, en 490 kHz, 518 kHz y 4209,5 kHz |
| Convenio SOLAS, capítulo IV | Obligación de llevar receptor NAVTEX | No se aplica: el programa no sirve para cumplirla |

## Señal y frecuencias

| Requisito | Norma | En el programa |
| :--- | :--- | :--- |
| Emisión en impresión directa de banda estrecha con corrección de errores, modo B colectivo | UIT-R M.540-2, anexo II, 2; M.476 y M.625 | El demodulador y el descodificador vienen del proyecto original de bartelvdh y se han comprobado con dos grabaciones reales. Los textos de M.476 y M.625 no se han consultado para este fork |
| 518 kHz para el servicio internacional, en inglés | UIT-R M.540-2, anexo I, 1; Manual NAVTEX, 3.1.1 | Se recibe, a la vez que 490 kHz |
| 490 kHz y 4209,5 kHz para servicios nacionales | Manual NAVTEX, 3.1.1 y 4.4 | Se reciben: 490 kHz junto con 518 kHz; 4209,5 kHz con el mismo receptor por horario o con un segundo receptor |
| Solo recepción | — | El programa no emite nada por radio |

## Formato del mensaje

| Requisito | Norma | En el programa |
| :--- | :--- | :--- |
| El mensaje empieza con `ZCZC`, un espacio y la identidad B1B2B3B4, y acaba con `NNNN` | UIT-R M.540-2, anexo II, 3; Manual NAVTEX, 7.4 | Así se reconoce el principio y el final. Sin `NNNN`, el mensaje se guarda como cortado |
| B1: letra de la estación (A–X). B2: letra del tema. B3B4: número 01–99 | UIT-R M.540-2, anexo II, 2.1 y 2.2; Manual NAVTEX, 5.2 a 5.4 | Se guardan y se muestran por separado; la API los da como `station`, `subject` y `number` |
| Un mensaje con B3B4 = 00 se muestra siempre | UIT-R M.540-2, anexo II, 6; Manual NAVTEX, 5.4.3 | Se guarda cada vez que llega y se enseña aunque no se siga esa estación o ese tema |
| Los temas A, B, D y L no se pueden rechazar | Manual NAVTEX, 5.3.4 (M.540-2 cita A, B y D) | En la interfaz no se pueden desmarcar |

## Lo que debe hacer un receptor

| Requisito | Norma | En el programa |
| :--- | :--- | :--- |
| Un mensaje está bien recibido si su tasa de errores de carácter es inferior al 4 % | MSC.508(105), 6.2.3 | Se cuentan los caracteres mal recibidos de cada mensaje. La API lo dice en `errors` y `satisfactory` |
| Solo se guarda la identidad de los mensajes bien recibidos | MSC.508(105), 6.2.3; Manual NAVTEX, 7.5 | Una recepción cortada o con un 4 % o más de errores no impide guardar la repetición: la copia mejor sustituye a la peor |
| La identidad se borra sola entre 60 y 72 horas después | MSC.508(105), 6.2.2; Manual NAVTEX, 6.2 | 72 horas. En ese tiempo la repetición no vuelve a presentarse como nueva; pasado, sí |
| Un mensaje ya recibido no se vuelve a presentar | UIT-R M.540-2, anexo II, 4; Manual NAVTEX, 7.5 | Se anota solo que se ha vuelto a oír (`heard`) |
| Memoria para al menos 200 identidades y 200 mensajes de 500 caracteres, que no se pierda al apagar | MSC.508(105), 6.1.1 y 6.2.1 | Base de datos en disco, sin límite de número. Los mensajes se conservan 72 horas desde la última vez que se oyen, ampliable hasta un año |
| El usuario puede marcar mensajes para conservarlos siempre, hasta un 25 % de la memoria; las marcas se pueden quitar | MSC.508(105), 6.1.2 | Botón «Conservar siempre» en cada mensaje. Hasta 50, la cuarta parte de los 200 de la norma. Un mensaje conservado no se borra al cumplir el tiempo de conservación ni lo sustituye otro posterior con el mismo número |
| El usuario no debe poder borrar mensajes de la memoria | MSC.508(105), 6.1.1 | **No se cumple, a propósito:** el programa permite borrar un mensaje o todos. El borrado solo lo oculta: la identidad se sigue recordando 72 horas |
| Con la memoria llena se sobrescribe el más antiguo | MSC.508(105), 6.1.1 | No hay memoria que se llene: los mensajes se quitan por tiempo, nunca antes de 72 horas |
| Un mensaje nuevo sigue a la vista hasta que se reconoce o pasan 24 horas | MSC.508(105), 5.2.1 | **Parcial:** queda marcado como «nuevo» hasta que se abre, sin límite de 24 horas |
| Estaciones y temas no se pierden con un corte de alimentación de menos de 6 horas | MSC.508(105), 6.3 | Se guardan en disco: no se pierden |
| Un receptor en 518 kHz y un segundo receptor en al menos otras dos frecuencias NAVTEX | MSC.508(105), 4.1.1 | 518 y 490 kHz a la vez con un receptor; 4209,5 kHz con el mismo por horario o con un segundo receptor |
| Un medio para comprobar que el receptor y la memoria funcionan | MSC.508(105), 8 | **Parcial:** estado de cada receptor y último sincronismo en la interfaz; `make check` para las reglas y la memoria; se puede descodificar una grabación (`-s file`) |
| El carácter mal recibido se presenta con un asterisco | MSC.508(105), 5.7 | Así se guarda, se muestra y se exporta |
| Se pueden ver las estaciones y los temas excluidos | MSC.508(105), 3 | Pestaña Estaciones |
| La información de búsqueda y salvamento (B2 = D) da una alerta en el puesto de gobierno, que solo se puede apagar a mano | MSC.508(105), 7; IEC 61097-6, 4.3.9.1 | Aviso en pantalla, con pitido, hasta que se pulsa «Reconocer la alarma». **Parcial:** el pitido depende del navegador y no garantiza los 75–85 dBA que pide la IEC |
| Los mensajes de búsqueda y salvamento se muestran hasta que se reconoce la alarma | IEC 61097-6, 4.6.1.1 | Se muestran aunque no se siga esa estación |

## Turnos de emisión

| Requisito | Norma | En el programa |
| :--- | :--- | :--- |
| Cada letra B1 tiene como máximo diez minutos cada cuatro horas: A a las 0000, B a las 0010… X a las 0350 UTC | Manual NAVTEX, 5.2.5 y tabla 2 | Cada mensaje dice si llegó en el turno de su letra (`in_slot`). Con la hora se averigua qué estación lo emitió cuando varias comparten letra |
| Emisiones programadas a intervalos de ocho horas como máximo | UIT-R M.540-2, anexo I, 5 | La tabla de estaciones da las horas de cada una y la interfaz, las próximas emisiones |

De las 257 estaciones de la tabla de la UIT, 254 siguen la regla de la letra; las otras tres se listan al generar la tabla (`tools/estaciones/generar.py`).

## Salida de datos

| Requisito | Norma | En el programa |
| :--- | :--- | :--- |
| Al menos una interfaz para pasar los datos recibidos a otros equipos, conforme a las normas internacionales | MSC.508(105), 9.1 y 9.2 (remite a IEC 61162) | Sentencias NMEA 0183 por UDP y por un servidor TCP. **Parcial:** va por red, no por el puerto serie que describe IEC 61162-1 |
| Sentencia `NRX` con el mensaje recibido | IEC 61097-6, anexo C | Dos formas, a elegir: la de la norma y la de los receptores Furuno, que es la que usan la app y el plotter APRS LoRa+ |
| Índice de frecuencia: 1 = 490 kHz, 2 = 518 kHz, 3 = 4209,5 kHz | IEC 61097-6, anexo C, nota 4 | En la forma de la norma |
| Estado `A` si la recepción es sintácticamente correcta, `V` si falta el `NNNN` | IEC 61097-6, anexo C, nota 6 | En la forma de la norma |
| La alarma de búsqueda y salvamento se comunica con la sentencia `ALR` (alarma 003), cada 30 s hasta que se reconoce | IEC 61097-6, 4.3.9.2, 4.3.9.3 y tabla 1 | `$CRALR,hhmmss.00,003,A,V,NAVTEX: Search and rescue information` |
| La alarma se puede apagar con la sentencia `ACK` | IEC 61097-6, 8.4.2 | Por el servidor TCP: `$--ACK,003` |
| Suma de control, longitud máxima de 82 caracteres, caracteres reservados como `^hh` | IEC 61162-1 | Comprobado en `make check`; la suma de control se contrasta con el ejemplo de IEC 61097-6 |
| Sentencia `NRM` para elegir estaciones y temas desde otro equipo | IEC 61097-6, anexo C | **No hecho** |
| Interfaz de gestión de alertas del puente | MSC.508(105), 9.4, que remite a MSC.302(87) | **No hecho.** La alerta sale con las sentencias `ALR` y `ACK`, que son las de la edición de 2005 de IEC 61097-6. La gestión de alertas del puente usa otras sentencias (IEC 62923), y la enmienda de 2019 de IEC 61097-6 no se ha podido consultar |
| Interfaz para impresora, si no la lleva integrada | MSC.508(105), 9.3 | No hay; los mensajes se guardan en un archivo de texto o CSV para imprimirlos |

**NMEA 2000.** No existe un mensaje normalizado de NMEA 2000 para el texto de NAVTEX, y este programa no inventa uno. Al bus llega a través de [S.A.T.-APRS+](https://github.com/EA7JRS/sat-aprs).

## Lo que no se aplica o no se cumple

- **Homologación.** Sin ensayos de IEC 61097-6 ni de IEC 60945 (condiciones ambientales y compatibilidad electromagnética de equipos marinos); sin aprobación de tipo.
- **Sensibilidad y selectividad del receptor** (MSC.508(105), 4.1.2; IEC 61097-6): dependen del equipo de radio y de la antena que se le pongan, y no se han medido.
- **Impresora o pantalla dedicada** (MSC.508(105), 5): la interfaz es una página web.
- **Volumen de la alarma** (IEC 61097-6, 4.3.9.1): no se garantiza.
- **Puerto serie de IEC 61162-1:** la salida es por red.
- **Borrado de mensajes** (MSC.508(105), 6.1.1): la norma no lo permite y el programa sí, porque es una ayuda y no el receptor obligatorio.
- **Gestión de alertas del puente** (MSC.508(105), 9.4; MSC.302(87); IEC 62923): no hecha.
- **Recepción de información de seguridad por HF** (MSC.508(105), 4.2): solo se recibe 4209,5 kHz; no las demás frecuencias de HF, ni la sintonía por horario de todas las estaciones, ni el aviso por llamada selectiva digital.
- **Pantalla** (MSC.508(105), 5): 32 caracteres por línea y 16 líneas se cumplen en cualquier pantalla normal, pero dependen del navegador y del equipo donde se abra.

## Ediciones vigentes

Comprobado el 22 de noviembre de 2026:

| Norma | Edición vigente | La que se ha leído |
| :--- | :--- | :--- |
| UIT-R M.540 | M.540-2 (06/1990), en vigor | La misma |
| Normas de funcionamiento de la OMI | MSC.508(105), de 28 de abril de 2022, para equipos instalados desde el 1 de enero de 2024. Antes: MSC.148(77) enmendada por MSC.430(98) (2019–2023), MSC.148(77) (2005–2019) y A.525(13) (antes de 2005) | MSC.508(105) y MSC.148(77). La enmienda MSC.430(98) no se ha leído |
| Manual NAVTEX de la OMI | Edición de 2023: MSC.1/Circ.1403/Rev.2, de 28 de noviembre de 2022, en vigor desde el 1 de enero de 2023 | La misma |
| IEC 61097-6 | Edición 2.2: 2005 con la enmienda 1 (2011) y la enmienda 2 (2019) | Solo la de 2005. Las dos enmiendas son de pago y no se han podido consultar |
| IEC 61162-1 | Edición 6 | No se ha leído; lo aplicado sale de lo que cita IEC 61097-6 |
| Reglamento de Radiocomunicaciones | Edición de 2024 | No se ha leído |

Cuando una norma cambie, este documento y el programa hay que revisarlos: la fecha de arriba dice cuándo se comprobó por última vez.

## Documentos consultados

Leídos para este trabajo, el 22 de noviembre de 2026:

- [Recomendación UIT-R M.540-2](https://www.itu.int/dms_pubrec/itu-r/rec/m/R-REC-M.540-2-199006-I!!PDF-E.pdf), «Operational and technical characteristics for an automated direct-printing telegraph system for promulgation of navigational and meteorological warnings and urgent information to ships».
- [Resolución MSC.508(105) de la OMI](https://wwwcdn.imo.org/localresources/en/KnowledgeCentre/IndexofIMOResolutions/MSCResolutions/MSC.508(105).pdf), «Performance standards for the reception of maritime safety information and search and rescue related information by MF (NAVTEX) and HF».
- [Resolución MSC.148(77) de la OMI](https://law.resource.org/pub/us/cfr/ibr/inc/imo/imo.msc.148.2003.pdf), la norma anterior.
- [Manual NAVTEX de la OMI, edición de 2023](https://wwwcdn.imo.org/localresources/en/OurWork/Safety/Documents/Documents%20relevant%20to%20GMDSS/MSC.1-Circ.1403-Rev.2.pdf) (MSC.1/Circ.1403/Rev.2).
- [IEC 61097-6:2005](https://law.resource.org/pub/us/cfr/ibr/inc/iec/iec.61097-6.2005.pdf), «Global maritime distress and safety system (GMDSS) – Part 6: Narrowband direct-printing telegraph equipment for the reception of navigational and meteorological warnings and urgent information to ships (NAVTEX)».
- Manual del receptor Furuno NX-700, para la otra forma de la sentencia `NRX`.
- UIT, «Coast stations participating in the NAVTEX services» (tabla 4, 2017), para la tabla de estaciones.

Citadas por las anteriores y **no consultadas**: UIT-R M.476 y M.625, IEC 61162-1, IEC 60945, IEC 62923, MSC.302(87), MSC.430(98), las enmiendas de 2011 y 2019 de IEC 61097-6 y el Reglamento de Radiocomunicaciones. Lo que aquí se dice de ellas sale de lo que citan los documentos leídos.

Los números de apartado son los de las ediciones enlazadas. Si encuentra una diferencia entre este documento, el programa y el texto de una norma, vale la norma: abra una incidencia en el repositorio.
