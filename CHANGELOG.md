# Historial de cambios

Fork EA7JRS/Navtex del receptor NAVTEX de bartelvdh. Las entradas dicen también qué se probó y qué no. Lo descrito en una fase puede haber cambiado en las posteriores (por ejemplo, los servicios y sus ajustes): el estado actual es el del [README](README.md).

## [1.6.0] - 2026-11-22

### Sentencia NRM, regla de las 24 horas y un fallo de la alarma

- **Corregido:** el aviso rojo de la alarma se veía siempre, vacío, aunque no hubiera ningún mensaje de búsqueda y salvamento sin reconocer (fallo de las versiones 1.3.0 a 1.5.0: una regla de la hoja de estilos anulaba el ocultado). Lo mismo pasaba con los botones de guardar y borrar cuando no había mensajes.
- **Sentencia `NRM`** (IEC 61097-6, anexo C.2) por el servidor TCP: consultar qué estaciones y temas se siguen (`CRQ`), cambiarlos, y pedir los mensajes guardados como sentencias `NRX`. Con ella están las cuatro sentencias que la norma pide como mínimo: `NRX`, `NRM`, `ALR` y `ACK`. Los temas A, B, D y L tampoco se pueden quitar por esta vía.
- **Mensajes nuevos:** la marca de «nuevo» se quita al abrir el mensaje o a las 24 horas (MSC.508(105), 5.2.1).
- Capturas del README rehechas con la interfaz actual. Los mensajes que enseñan son de muestra y lo dicen en su texto, salvo el EE39, descodificado de una grabación real.
- `make check`: 103 comprobaciones, con los dos ejemplos de `NRM` de la norma.
- Probado con el programa real: consulta, cambio y petición por TCP con `nc`. No probado con otro equipo que hable `NRM`.

---

## [1.5.0] - 2026-11-22

### Normas al día: MSC.508(105) y Manual NAVTEX de 2023

- Se han comprobado las ediciones vigentes de las normas. La norma de funcionamiento de la OMI vigente es la **MSC.508(105)**, de 2022, para equipos instalados desde el 1 de enero de 2024; sustituye a la MSC.148(77), que era la aplicada hasta ahora. El Manual NAVTEX vigente es la edición de 2023 (MSC.1/Circ.1403/Rev.2). La UIT-R M.540-2 sigue en vigor.
- **Conservar siempre.** Nuevo, por MSC.508(105), 6.1.2: cada mensaje se puede marcar para conservarlo. No se borra al cumplir el tiempo de conservación ni lo sustituye otro posterior con el mismo número; «Borrar todos» lo respeta. Hasta 50 mensajes.
- `docs/NORMAS.md` cita ya la norma vigente, tiene una tabla de ediciones (la vigente y la que se ha leído) y dice lo que no se cumple: el borrado de mensajes, que la norma no permite; la gestión de alertas del puente; la recepción por HF más allá de 4209,5 kHz.
- No se han podido consultar las enmiendas de 2011 y 2019 de IEC 61097-6, que son de pago: las sentencias `NRX`, `ALR` y `ACK` son las de la edición de 2005.
- `make check`: 94 comprobaciones.

---

## [1.4.0] - 2026-11-22

### Interfaz en español e inglés, y selector de aspecto como el de APRS LoRa+

- **Idioma.** La interfaz está en español y en inglés. De serie toma el idioma del navegador; el botón `EN` / `ES` de la cabecera lo cambia, y la elección se guarda en el navegador. Los mensajes NAVTEX no se traducen: se muestran tal como se reciben. En inglés, los decimales van con punto y el oeste es `W`.
- **Aspecto.** El desplegable se sustituye por el botón con menú de la WebUI del tracker APRS LoRa+, con sus cinco modos: Sistema, Automático (claro de día y oscuro de noche, por la salida y la puesta del sol en la posición del receptor; sin posición, de 8 a 20 h), Claro, Oscuro y Noche (rojo sobre negro, para conservar la visión nocturna).
- Quien tuviera elegido «automático» en una versión anterior pasa al automático por el sol; «Sistema» es el de antes.
- Probado en un navegador en los dos idiomas y en los cinco aspectos. El texto inglés no lo ha revisado un hablante nativo.

---

## [1.3.0] - 2026-11-22

### Alarma de búsqueda y salvamento y sentencias de la norma IEC 61097-6

- **Alarma del tema D.** Un mensaje de búsqueda y salvamento da una alarma que solo se apaga a mano (OMI MSC.148(77), 7.1; IEC 61097-6, 4.3.9): aviso rojo arriba de la interfaz, con pitido, y botón «Reconocer la alarma». El mensaje se muestra aunque no se siga esa estación; su repetición no vuelve a disparar la alarma.
- **`ALR` y `ACK`.** Por la salida NMEA 0183, la alarma sale en la sentencia `ALR` (número 003) cada 30 segundos hasta que se reconoce, y se puede reconocer desde otro equipo con `ACK` por el servidor TCP.
- **`NRX` en la forma de la norma** (IEC 61097-6, anexo C), además de la de Furuno, que sigue de serie. Se elige en Receptores.
- API: `alarm` en los mensajes, `sar_alarms` en `/api/status` y `POST /api/alarm/ack`.
- `make check`: 87 comprobaciones; la suma de control se contrasta con el ejemplo de la propia norma.
- Documentación: nuevo `docs/NORMAS.md`, con cada norma (UIT-R M.540-2, OMI MSC.148(77), Manual NAVTEX, IEC 61097-6, IEC 61162-1), el apartado, lo que se aplica y lo que no.
- Probado con el programa real: mensaje D escrito para la prueba (no recibido), `ALR` cada 30 s y `ACK` por TCP con `nc`, y la interfaz en un navegador. No probado con un plotter ni con otro equipo que mande `ACK`. El pitido no se ha podido oír en la prueba.

---

## [1.2.0] - 2026-11-22

### Reglas de un receptor NAVTEX, borrar, guardar y salida NMEA 0183

- **Reglas de la OMI y la UIT** (MSC.148(77), Manual NAVTEX, UIT-R M.540). Se cuentan los caracteres mal recibidos de cada mensaje; una recepción se da por buena con menos de un 4 %. La identidad de un mensaje bien recibido se recuerda 72 horas: su repetición ya no vuelve a aparecer como nueva (antes, cada repetición completa sustituía a la anterior y salía otra vez como «nuevo»). Una copia mejor sustituye a una peor. Los mensajes con número 00 se guardan siempre.
- **Turnos.** Cada mensaje dice si llegó en los diez minutos de su letra, y la interfaz usa la hora para decir qué estación lo emitió cuando varias comparten letra.
- **Borrar** un mensaje o todos. El borrado se recuerda 72 horas, para que la repetición de la estación no lo traiga de vuelta.
- **Guardar** todos los mensajes en un archivo de texto o CSV (`/api/export`).
- **Conservación ajustable**: de 72 horas a un año desde la última vez que se oyó el mensaje.
- **Salida NMEA 0183**: sentencias `NRX` en la forma de Furuno (la de la app y el plotter APRS LoRa+), por UDP y por un servidor TCP.
- API: campos `characters`, `errors`, `satisfactory`, `heard` e `in_slot`; `?heard=` para saber qué mensajes se siguen emitiendo.
- `make check`: 61 comprobaciones de las reglas, el borrado, la conservación, la exportación y las sentencias `NRX`.
- Probado con el programa real y las grabaciones de prueba (UDP y TCP recibidos con `nc`, sumas de control comprobadas). No probado con un plotter ni con mensajes recibidos por antena. La forma de `NRX` se ha tomado del manual del Furuno NX-700 y del código de la app; la de IEC 61162-1 no se ha podido consultar.

---

## [1.1.0] - 2026-11-22

### Licencia: GPL-3.0-or-later

- El fork pasa de MIT a **GNU GPL versión 3 o posterior**, como el resto del ecosistema APRS LoRa+. El texto está en `LICENSE`.
- Se concede un permiso adicional (sección 7 de la GPL versión 3) para enlazar el programa con la API de SDRplay, que es privativa.
- El código original de bartelvdh conserva su licencia MIT y su aviso de copyright, en `LICENSE-MIT-original`.
- `COPYING.md` explica qué licencia tiene cada parte, incluidos los datos de las estaciones.
- La versión 1.0.0 se publicó bajo MIT y lo sigue estando para quien la obtuvo así.

Sin cambios en el funcionamiento.

## [1.0.0] - 2026-11-22

Primera versión del fork. Parte del código de bartelvdh (commit `964d6bb`, 25-02-2025) y lo amplía en ocho fases, que se detallan a continuación de la más reciente a la más antigua.

**Probado en un equipo real el 22-11-2026** (Intel NUC, Debian 13): la instalación y los servicios; un SDRplay RSPdx-R2 y un Nooelec NESDR SMArt v5 abiertos y recibiendo muestras a la vez; la posición por gpsd con un Leo Bodnar LBE-1420; y la interfaz web desde otros equipos de la red.

**Sin probar todavía con una emisión real:** la recepción de un mensaje completo del aire. El descodificador sí está probado con dos grabaciones reales de NAVTEX.

### Fase 8 · posición por GPS

La interfaz web toma la posición propia de **gpsd** (de serie, el del propio equipo, `127.0.0.1:2947`; se cambia o se quita con `NAVTEX_GPSD` en `/etc/default/navtex`).

- Junto a la hora hay un recuadro con la posición del GPS como se lee en una carta, el localizador, el tipo de fijo, los satélites y la altitud.
- La pestaña «Estaciones» usa esa posición para ordenar por distancia y marcar las que quedan en alcance; las listas se rehacen solas cuando el barco se ha movido más de una milla.
- Si el GPS no da posición, o si se desmarca «Usar la posición del GPS», se usa la escrita a mano.
- El servidor solo lee de gpsd; no envía nada al GPS. `GET /api/gps` da los mismos datos.

Probado con un gpsd de pruebas que envía los mismos informes que el real (con fijo, sin gpsd y desactivado) y en un navegador. **Sin probar con el gpsd del equipo.**

### Fase 7 · dos receptores, horario, estaciones del mundo y aspecto

**Dos receptores, ajustados desde la web.** Los servicios pasan a ser `navtex-receiver@1` y `navtex-receiver@2`. En la pestaña «Receptores» se elige para cada uno si está encendido, qué equipo usa (SDRplay o RTL-SDR), su toma de antena o su número de serie, y la banda. El receptor aplica los cambios en unos segundos, sin reiniciar el servicio ni tocar archivos. Con dos equipos se reciben las tres frecuencias a la vez.

- El receptor 2 está siempre en marcha pero no toca ningún equipo hasta que se enciende en la web.
- Si el receptor 1 comparte el SDRplay con SDRconnect o SDR++, sigue alternando con ellos (`sudo systemctl start navtex-receiver@1`).
- Si un equipo está desconectado o en uso, el receptor lo dice y lo reintenta cada 15 segundos.
- A mano sigue valiendo la línea de órdenes; `navtexreceiver -n 1` usa los ajustes de la web.

**Banda por horario.** Con un solo equipo, la banda «Por horario» escucha 518 y 490 kHz y pasa a 4209,5 kHz en las horas UTC que se indiquen, desde un minuto antes hasta once después. Un botón toma las horas de las estaciones de onda corta que se siguen.

**Estaciones del mundo.** `web_server/build/estaciones.json` trae 263 estaciones con su letra, posición, horas, alcance, idioma y estado. Indicando la posición propia (latitud y longitud, o un localizador), la pestaña «Estaciones» las ordena por distancia, marca las que quedan en alcance, permite seguirlas con un toque y enseña las próximas emisiones de las que se siguen.

- Fuentes: la Tabla 4 de la UIT, «Coast stations participating in the NAVTEX services» (518 y 490 kHz; coordinada con la OMI, GMDSS.1/Circ.21, 31-05-2017), y la tabla de 4209,5 kHz de la lista de Wikipedia (CC BY-SA 4.0, consultada el 22-11-2026).
- Los datos de origen están en `tools/estaciones/` y `generar.py` los comprueba y genera el archivo. De las 257 estaciones de la UIT, 254 siguen la regla de turnos (la letra fija la hora); las otras tres se listan al generar.
- **Los datos son de 2017 y pueden haber cambiado.** Sirven para orientarse, no sustituyen a las publicaciones oficiales en vigor.

**Aspecto.** Claro, oscuro o automático (el del equipo), con un selector en la cabecera; se recuerda en cada navegador.

**Seguridad.** Los cambios (ajustes, estaciones y temas) solo se aceptan por POST y con una cabecera propia: otra página web abierta en el mismo navegador no puede cambiar nada.

Probado: los ajustes y su validación; el cambio de banda por horario; el receptor sin equipo (reintenta sin salir); y la interfaz en un navegador (posición, estaciones en alcance, próximas emisiones, receptores y aspecto). **Sin probar con equipos reales**, y en particular dos receptores a la vez.

### Fase 6 · NAVTEX de onda corta (4209,5 kHz)

Además de 518 y 490 kHz, el receptor puede escuchar el canal de onda corta, **4209,5 kHz**, con la opción `-B hf`:

```
navtexreceiver -B hf                 # SDRplay
navtexreceiver -s rtlsdr -B hf       # RTL-SDR (muestreo directo o conversor)
```

- **Un receptor escucha una banda o la otra.** 518 y 490 kHz van juntas porque están a 28 kHz; 4209,5 kHz queda a 3,7 MHz y no cabe en la misma sintonía. Para tener las tres a la vez hacen falta dos receptores, cada uno con su copia del programa.
- En `/etc/default/navtex` se elige con `RECEIVER_OPTS`, por ejemplo `RECEIVER_OPTS="-p C -B hf"`.
- Los mensajes se guardan con frecuencia `4209` y tienen sus propias estaciones y temas (`stations4209`, `messages4209`; de serie, todas las estaciones). La interfaz web los muestra como «4209,5 kHz», con su filtro y su ajuste.
- `/api/status` dice la banda en uso (`band`: `mf` o `hf`) y el último sincronismo en ella (`phasing_4209`).

Sirve con antenas que cubren las tres frecuencias, como la JRC NAW-333 (518, 490 y 4209,5 kHz).

Probado con una recepción real colocada en el canal de onda corta: se descodifica y se guarda como 4209,5 kHz, y lo que cae en el otro canal se ignora. **Sin probar con una emisión real en 4209,5 kHz.**

### Fase 5 · interfaz web nueva, en español

La interfaz original venía compilada y sin su código fuente, así que no se podía traducir ni modificar. Se ha sustituido por una nueva (`web_server/build/`: `index.html`, `estilo.css`, `app.js`), escrita a mano y sin paso de compilación:

- **Mensajes:** lista con el tema en claro, la primera línea del texto, la estación, la frecuencia y hace cuánto llegó; colores por importancia (salvamento, aviso meteorológico, aviso a los navegantes); marca de «nuevo» y de «cortado»; filtros por frecuencia y tema, búsqueda en el texto y opción de ver también lo que no se sigue.
- **Estado del receptor:** si está recibiendo, con qué equipo, y el último sincronismo en 518 y en 490 kHz.
- **Estaciones y temas:** por frecuencia. Los temas A, B, D y L no se pueden quitar, como en un receptor homologado.
- Se ve bien en el móvil, sigue el modo claro u oscuro del equipo y **no carga nada de Internet**: funciona a bordo sin conexión.
- Lo recibido por radio se muestra siempre como texto, nunca se interpreta, y el servidor prohíbe a la página cargar nada que no sea suyo.

### Fase 4 · salida para otros programas

La interfaz web ofrece además una API de solo lectura, pensada para que otro programa (S.A.T.-APRS+, un plotter, un guion propio) recoja los mensajes por la red:

| Dirección | Qué devuelve |
| :--- | :--- |
| `GET /api/messages?since=<id>&limit=<n>` | Los mensajes con identificador mayor que `since`, del más antiguo al más reciente, con el texto completo. Sin filtros de estación ni de tipo, y sin marcar nada como leído. `limit` de 1 a 500 (200 si no se indica). |
| `GET /api/status` | Qué está haciendo el receptor: origen (`sdrplay`, `rtlsdr`, `file`), cuándo arrancó, última vez que recibió muestras (`receiver_alive`, cada minuto), último sincronismo en cada frecuencia, número de mensajes y último identificador. |

Cada mensaje:

```json
{"id":5,"bbbb":"EE39","station":"E","subject":"E","number":39,"freq":490,
 "received":"2026-11-22T07:38:00Z","complete":false,"message":"ZCZC EE39\n062040 UTC NOV 21\n..."}
```

El uso previsto es preguntar cada minuto con el último `id` recibido. Las horas van en UTC.

**NMEA 2000.** No existe un mensaje normalizado de NMEA 2000 para el texto de NAVTEX (el formato normalizado es la sentencia `NRX` de NMEA 0183). Este programa no inventa uno: los mensajes llegan al bus a través de S.A.T.-APRS+, que los recoge por esta API y los difunde con el resto de sus avisos.

### Fase 3 · servicios y mensajes incompletos

Servicios de systemd (`systemd/`, instalador `tools/instalar-servicios.sh`):

```
./configure && make && sudo make install
sudo sh tools/instalar-servicios.sh
```

- `navtex-receiver` y `navtex-web` se ejecutan con su usuario, no como `root`. La interfaz web va además encerrada en su carpeta.
- Los ajustes están en `/etc/default/navtex`: opciones del receptor (`RECEIVER_OPTS`), puerto y dirección de la interfaz web.
- Si el receptor no está o se pierde, el servicio se reinicia solo cada 15 segundos hasta que vuelva.
- **Receptor compartido.** Si en el equipo hay otros servicios que usan el mismo SDRplay (`sdrconnect`, `sdrpp-server`), el instalador los declara incompatibles con `navtex-receiver`: arrancar uno para el otro. En ese caso el receptor NAVTEX no arranca solo; se pone con `sudo systemctl start navtex-receiver` y se vuelve con `sudo systemctl start sdrconnect`. Con un RTL-SDR dedicado (`RECEIVER_OPTS="-s rtlsdr"`) no hay conflicto y arranca siempre.
- `sudo sh tools/instalar-servicios.sh --quitar` los retira.

Salida del receptor: en una terminal sigue mostrando cada carácter recibido; como servicio solo escribe lo que importa (sincronismo, comienzo de mensaje, mensaje guardado). `-v` y `-q` lo fuerzan.

Mensajes:
- Cada mensaje se guarda con una marca de completo o incompleto (columna `complete`, que también sale en `/messagelist`). Las bases de datos anteriores se actualizan solas.
- Una recepción cortada ya no sustituye a una copia completa del mismo mensaje recibida en las últimas 24 horas, ni a otra incompleta más larga.
- Un mensaje de 490 kHz ya no borra al de 518 kHz con el mismo identificador (y viceversa).
- Si llega una cabecera nueva sin haber terminado el mensaje anterior, este se guarda como incompleto; antes los dos se mezclaban bajo el identificador del primero.

Robustez:
- Las líneas y los mensajes se acumulaban en búferes de 5000 caracteres sin comprobar el límite; ahora no se puede escribir fuera de ellos, llegue lo que llegue por radio.
- Las expresiones de comienzo y fin de mensaje se compilaban de nuevo en cada línea recibida y no se liberaban: el receptor perdía memoria continuamente.

Probado: la recepción cortada, con una grabación real seguida de ruido (se guarda como incompleta y, repetida, no duplica ni degrada); las reglas de guardado, sobre una base de datos del formato anterior; las unidades, con `systemd-analyze verify`. **Sin probar: el instalador en un equipo con systemd**, y un mensaje completo de principio a fin (las grabaciones reales disponibles no llegan al `NNNN`).

### Fase 2 · receptores RTL-SDR y grabaciones

El receptor ya no depende de un solo tipo de equipo. La entrada de muestras está separada del descodificador (`receiver/iq_source.h`) y hay tres orígenes:

| Origen | Opción | Notas |
| :--- | :--- | :--- |
| SDRplay | `-s sdrplay` | Como hasta ahora. Es el origen por defecto si la API de SDRplay está instalada al compilar. |
| RTL-SDR | `-s rtlsdr` | Necesita `librtlsdr-dev` al compilar. Ver abajo cómo llegar a 518 kHz. |
| Grabación | `-f archivo` | Descodifica una grabación en vez de un receptor: para pruebas y para repasar una recepción dudosa. |

`./configure` detecta qué bibliotecas hay y dice al final qué receptores quedan incluidos (`receivers: SDRplay yes, RTL-SDR yes`). Ninguna de las dos es obligatoria.

```
sudo apt-get install librtlsdr-dev
```

Un RTL-SDR no sintoniza por sí solo 490 y 518 kHz. Hay tres formas de llegar:

| Equipo | Orden |
| :--- | :--- |
| **Nooelec NESDR SMArt v5**: muestreo directo por la rama Q, de 0,1 a 25 MHz, sin cambiar controlador ni tocar el equipo | `navtexreceiver -s rtlsdr` |
| Otros con muestreo directo (RTL-SDR Blog V3 y similares, antena a la entrada de HF) | `navtexreceiver -s rtlsdr` (rama Q; rama I: `-D 1`) |
| Conversor integrado (RTL-SDR Blog V4) | `navtexreceiver -s rtlsdr` |
| Conversor externo (por ejemplo, de 125 MHz) | `navtexreceiver -s rtlsdr -u 125000000` |

El modo de muestreo se elige solo según el modelo que anuncia el equipo por USB (rama Q para el NESDR SMArt v5 y para los que no se reconocen; apagado para el Blog V4 y con `-u`), y el arranque lo dice. `-D 0`, `-D 1` o `-D 2` lo fuerzan.

El NESDR SMArt v5 no tiene alimentación por el coaxial: una antena activa necesita su propio inyector.

Otras opciones del RTL-SDR: `-i` índice del equipo, `-S` número de serie (con varios equipos conectados), `-g` ganancia del sintonizador en dB (automática si no se da; no se usa en muestreo directo), `-P` corrección en ppm y `-b` alimentación por el coaxial. `navtexreceiver -h` las lista todas.

Con 8 bits y sin filtros de entrada, un RTL-SDR recibe NAVTEX peor que un SDRplay: en onda media las emisoras de radiodifusión cercanas lo saturan con facilidad. Un filtro paso bajo o una antena activa sintonizada ayudan mucho.

Grabaciones: 252 000 muestras por segundo centradas en 504 kHz, en `.wav` de 16 bits con I y Q en los dos canales (lo que escribe la opción `-d`) o en bruto de 8 bits sin signo (lo que escribe `rtl_sdr -s 252000`). Con `-t` se reproduce a velocidad real.

Probado: la cadena completa, desde el archivo de muestras hasta el texto, con dos recepciones reales de NAVTEX colocadas a la vez en 518 y 490 kHz, en los dos formatos de archivo. El origen RTL-SDR compila y enlaza con `librtlsdr` y falla con un mensaje claro si no hay equipo; **falta probarlo con un RTL-SDR real**. El origen SDRplay se ha reorganizado sin cambiar sus ajustes; **falta probarlo en un equipo con la API de SDRplay**.

### Fase 1 · seguridad y fallos

Servidor web (`navtexwebserver`):
- Una dirección larga desbordaba un búfer de 80 bytes y tumbaba el servidor. Ahora las direcciones se comprueban y no se copian a búferes fijos.
- Se podía leer cualquier archivo del equipo (`/../archivo`, `//etc/...`). Ahora solo se sirven archivos de la carpeta de la interfaz.
- `/saveconfig` metía el texto recibido en la orden SQL. Ahora solo admite los cuatro ajustes que existen, con valores de letras A–Z, y usa sentencias preparadas.
- Las respuestas de la API salen como `application/json` y los archivos con su tipo.
- Dirección de escucha opcional: `navtexwebserver [puerto] [dirección]`, por ejemplo `navtexwebserver 8080 127.0.0.1`.

Receptor (`navtexreceiver`):
- La opción `-d` (grabación de depuración) no estaba declarada y no se podía usar.
- Con SIGINT o SIGTERM deja de recibir y libera el receptor, para que otro programa pueda usarlo.
- Si el receptor no se puede abrir, se desconecta o deja de entregar muestras durante 15 s, el programa termina con código 1 en vez de quedarse en marcha sin recibir. Así un gestor de servicios lo puede reiniciar.

Base de datos e instalación:
- La antigüedad de los mensajes se calculaba con una hora de error durante el horario de verano.
- El receptor y el servidor web esperan si el otro tiene la base de datos ocupada, en vez de fallar.
- `make install` ya no sustituye una base de datos existente por una vacía (se perdían los mensajes y la configuración) ni anida `build/build`.
- La carpeta `/opt/Navtex` y la base de datos dejan de ser escribibles por cualquiera (`777`/`666` → `775`/`664`) y pasan a ser del usuario que ejecuta `sudo make install`.
