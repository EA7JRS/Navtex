# Navtex

Receptor NAVTEX por software para Linux, con SDRplay o RTL-SDR. Escucha en segundo plano 518 y 490 kHz a la vez, o 4209,5 kHz en onda corta, guarda los mensajes y los enseña en una interfaz web en español que funciona sin Internet.

Navtex · © 2026 Manuel - EA7JRS · fork de [bartelvdh/Navtex](https://github.com/bartelvdh/Navtex) · GPL-3.0-or-later · parte del ecosistema APRS LoRa+

[![Donar con PayPal](https://img.shields.io/badge/Donar-PayPal-0070ba?logo=paypal&logoColor=white)](https://paypal.me/ea7jrs)

![Mensajes recibidos, en aspecto oscuro](docs/img/mensajes.png)

*Las capturas muestran mensajes de ejemplo, cargados para ilustrar la pantalla.*

> **Aviso.** Este programa es una ayuda. No sustituye a un receptor NAVTEX homologado donde este sea obligatorio, ni a las publicaciones oficiales.

## Qué hace

| Función | Detalle |
| :--- | :--- |
| Recepción en segundo plano | Los mensajes se guardan aunque nadie esté mirando; cada estación emite cada cuatro horas, en su turno de diez minutos |
| Tres frecuencias | 518 kHz (internacional) y 490 kHz (nacional) a la vez con un receptor; 4209,5 kHz (onda corta) con el mismo receptor por horario, o con un segundo receptor a la vez |
| Dos tipos de receptor | SDRplay (RSP1A, RSP1B, RSP2, RSPdx, RSPdx-R2) y RTL-SDR (muestreo directo, conversor integrado o externo) |
| Dos receptores a la vez | Cada uno con su equipo y su banda, ajustados desde la web y sin reiniciar nada |
| Interfaz web | En español, adaptada al móvil, con aspecto claro, oscuro o automático, y sin cargar nada de Internet |
| Estaciones del mundo | 263 estaciones con letra, posición, horas, alcance e idioma; ordenadas por distancia desde la posición propia |
| Posición por GPS | Tomada de gpsd; recuadro con la posición junto a la hora |
| Salida para otros programas | API de solo lectura en JSON; [S.A.T.-APRS+](https://github.com/EA7JRS/sat-aprs) la usa para convertir los mensajes en avisos |
| Mensajes incompletos o con errores | Se guardan marcados, con su número de caracteres mal recibidos; una copia mejor sustituye a una peor, nunca al revés |
| Reglas de un receptor NAVTEX | Recepción buena con menos de un 4 % de errores; cada mensaje se recuerda 72 horas para no repetirlo; los de número 00 se muestran siempre; se comprueba el turno de la letra |
| Borrar y guardar | Borrar un mensaje o todos; guardar todos en un archivo de texto o CSV; tiempo de conservación ajustable |
| Alarma de búsqueda y salvamento | Un mensaje del tema D da una alarma, en pantalla y sonora, que solo se apaga a mano |
| Salida NMEA 0183 | Cada mensaje nuevo en sentencias `NRX` (forma de Furuno o de la norma IEC 61097-6) y la alarma en `ALR`, por UDP y por un servidor TCP |

## Estado de las pruebas

Probado el 22-11-2026 en un Intel NUC con Debian 13:

- la instalación y los servicios;
- un SDRplay RSPdx-R2 y un Nooelec NESDR SMArt v5 abiertos y recibiendo muestras a la vez;
- la posición por gpsd, con un Leo Bodnar LBE-1420;
- la interfaz web desde otros equipos de la red;
- el descodificador, con dos grabaciones reales de NAVTEX.

**Sin probar todavía:** la recepción de un mensaje completo del aire, y una emisión real en 4209,5 kHz. El detalle de cada parte está en el [historial de cambios](CHANGELOG.md).

## Qué hace falta

- Un equipo con Linux. Está hecho y probado en Debian 13; un procesador modesto basta.
- Un receptor:
  - **SDRplay**, con su API 3.15 o posterior instalada ([sdrplay.com/api](https://www.sdrplay.com/api/)); o
  - **RTL-SDR** que llegue a onda media: por muestreo directo (Nooelec NESDR SMArt v5, RTL-SDR Blog V3), con conversor integrado (RTL-SDR Blog V4) o con conversor externo.
- Una antena para 490–518 kHz. Las antenas activas de NAVTEX necesitan alimentación; véase [Antenas](#antenas).
- Opcional: un GPS servido por gpsd, para la posición propia.

## Instalación

Dependencias (falta la API de SDRplay, que se instala aparte, si va a usar un SDRplay):

```bash
sudo apt install build-essential autoconf automake libcjson-dev libsqlite3-dev libmicrohttpd-dev sqlite3 librtlsdr-dev
```

Compilar, instalar y montar los servicios:

```bash
git clone https://github.com/EA7JRS/Navtex.git
cd Navtex
autoreconf -i && automake --add-missing; autoreconf
./configure
make
sudo make install
sudo sh tools/instalar-servicios.sh
```

`./configure` dice al final qué receptores quedan incluidos (`receivers: SDRplay yes, RTL-SDR yes`); ninguno de los dos es obligatorio. El instalador de servicios toma como usuario el que ejecuta `sudo`, o el que se indique con `--usuario`.

Después abra `http://<equipo>:8080/`. Si hay cortafuegos, abra ese puerto a la red local:

```bash
sudo ufw allow from 192.168.0.0/16 to any port 8080 proto tcp
```

**Actualizar:** `git pull` y repetir desde `autoreconf`. La base de datos con los mensajes y los ajustes se conserva. **Retirar los servicios:** `sudo sh tools/instalar-servicios.sh --quitar`.

## Servicios

| Servicio | Qué hace |
| :--- | :--- |
| `navtex-web` | La interfaz web y la API, en el puerto 8080 |
| `navtex-receiver@1` | Receptor 1 |
| `navtex-receiver@2` | Receptor 2: siempre en marcha, pero no toca ningún equipo hasta que se enciende en la web |

Ninguno se ejecuta como `root`. El puerto, la dirección de escucha y el gpsd de la interfaz web están en `/etc/default/navtex`.

**Receptor compartido.** Un equipo solo lo puede usar un programa a la vez. Si en el sistema hay servicios `sdrconnect` o `sdrpp-server`, el instalador declara el receptor 1 incompatible con ellos: arrancar uno para el otro, y el receptor 1 no arranca solo.

```bash
sudo systemctl start navtex-receiver@1   # recibir NAVTEX con el SDRplay
sudo systemctl start sdrconnect          # devolvérselo a SDRconnect
```

Registro en vivo: `journalctl -u 'navtex-receiver@*' -f`

## La interfaz web

### Mensajes

Lista con el tema en claro, la primera línea del texto, la estación, la frecuencia y hace cuánto llegó. El color indica la importancia: salvamento, aviso meteorológico, aviso a los navegantes. Hay marcas de «nuevo» y de «cortado», filtros por frecuencia y por tema, y búsqueda en el texto. Un asterisco en el texto es un carácter que no se pudo recibir bien.

### Estaciones

![Estaciones ordenadas por distancia, con la posición del GPS](docs/img/estaciones.png)

- **Mi posición:** la del GPS si lo hay; si no, la que se escriba (latitud y longitud en grados decimales, o un localizador).
- **Por frecuencia:** las estaciones de la más cercana a la más lejana, con su letra, alcance, idioma y próxima emisión, y cuáles quedan en alcance. Se siguen con un toque, o todas las que están en alcance de una vez.
- **Próximas emisiones** de las estaciones que se siguen.
- **Temas:** los temas A, B, D y L (avisos a los navegantes, avisos meteorológicos y salvamento) no se pueden quitar, como en un receptor homologado.

En la cabecera de cada mensaje, `ZCZC GA12`, la primera letra es la estación y la segunda el tema. Seguir una letra muestra los mensajes de todas las estaciones que la usan.

### Receptores

![Los dos receptores, con la banda por horario](docs/img/receptores.png)

Para cada uno de los dos receptores: encendido, equipo, banda y los ajustes del equipo. El receptor aplica los cambios en unos segundos.

| Banda | Qué escucha |
| :--- | :--- |
| 518 y 490 kHz | Las dos a la vez |
| 4209,5 kHz | Solo onda corta |
| Por horario | 518 y 490 kHz, y pasa a 4209,5 kHz en las horas UTC indicadas, desde un minuto antes hasta once después |

Un receptor escucha una banda o la otra: 518 y 490 kHz van juntas porque están a 28 kHz, pero 4209,5 kHz queda a 3,7 MHz. Para las tres a la vez hacen falta dos equipos.

### Aspecto

Claro, oscuro o automático (el del equipo), con el selector de la cabecera. Se recuerda en cada navegador.

## Receptores

### SDRplay

Vale con RSP1A, RSP1B, RSP2, RSPdx y RSPdx-R2. En el RSPdx y el RSPdx-R2 se elige la toma de antena (A, B o C); la alimentación por el coaxial solo existe en la toma B.

### RTL-SDR

Un RTL-SDR no sintoniza por sí solo estas frecuencias. Hay tres formas de llegar, y el programa elige según el modelo que anuncia el equipo:

| Equipo | Modo |
| :--- | :--- |
| Nooelec NESDR SMArt v5 | Muestreo directo, rama Q (de 0,1 a 25 MHz, sin cambiar nada) |
| RTL-SDR Blog V3 y otros con muestreo directo | Rama Q; la rama I se elige a mano |
| RTL-SDR Blog V4 | Apagado: usa su conversor integrado |
| Con conversor externo | Apagado; se indica la frecuencia del conversor |

Con 8 bits y sin filtros de entrada, un RTL-SDR recibe NAVTEX peor que un SDRplay: en onda media las emisoras de radiodifusión cercanas lo saturan con facilidad. Una antena de NAVTEX, que solo deja pasar su banda, lo resuelve en buena parte.

### Antenas

Las antenas activas de NAVTEX necesitan alimentación, y no siempre vale la que da el receptor:

| Antena | Dato publicado |
| :--- | :--- |
| JRC NAW-333 | 518, 490 y 4209,5 kHz; 6,5 V y 23 mA; 50 Ω (manual del JRC NCR-333) |
| NASA Marine H Vector | Activa, alimentada por el coaxial; el fabricante no publica la tensión |

La alimentación por el coaxial de un RSPdx ronda los 4,7 V, y el Nooelec NESDR SMArt v5 no tiene. Para una antena que pida otra tensión hace falta un inyector externo con la tensión correcta. **No alimente una antena a una tensión superior a la indicada por su fabricante.**

### A mano, sin servicios

```bash
navtexreceiver -h                              # todas las opciones
navtexreceiver -p C                            # SDRplay, toma C, 518 y 490 kHz
navtexreceiver -s rtlsdr                       # RTL-SDR, modo según el modelo
navtexreceiver -s rtlsdr -u 125000000          # RTL-SDR con conversor de 125 MHz
navtexreceiver -B hf                           # 4209,5 kHz
navtexreceiver -f grabacion.wav                # descodificar una grabación
navtexreceiver -n 1                            # receptor 1, con los ajustes de la web
```

En una terminal muestra cada carácter recibido; como servicio, solo lo importante (`-v` y `-q` lo fuerzan). Con `-d` graba además las muestras en un `.wav`, que luego se puede volver a descodificar con `-f`.

## Posición por GPS

La interfaz web lee la posición de gpsd: de serie, el del propio equipo (`127.0.0.1:2947`). Se cambia o se quita con `NAVTEX_GPSD` en `/etc/default/navtex`. El servidor solo lee; no envía nada al GPS.

**Leo Bodnar LBE-1420.** Su puerto serie es a la vez salida NMEA y consola de órdenes: en cuanto recibe un carácter deja de emitir, hasta que se le envía `nmea`. gpsd sondea los puertos al arrancar y lo deja callado. Se evita arrancando gpsd en solo lectura, con `GPSD_OPTIONS="-n -b"` en `/etc/default/gpsd`. Si ya se ha callado:

```bash
sudo systemctl stop gpsd gpsd.socket
printf 'nmea\r' | sudo tee /dev/ttyACM0 > /dev/null
sudo systemctl start gpsd.socket gpsd
```

## Estaciones del mundo

`web_server/build/estaciones.json` trae 263 estaciones: 170 en 518 kHz, 87 en 490 kHz y 6 en 4209,5 kHz.

| Fuente | Qué aporta |
| :--- | :--- |
| UIT, «Coast stations participating in the NAVTEX services» (Tabla 4; coordinada con la OMI, GMDSS.1/Circ.21, 31-05-2017) | 518 y 490 kHz |
| Wikipedia, «List of Navtex stations» (CC BY-SA 4.0), consultada el 22-11-2026 | 4209,5 kHz, que la tabla de la UIT no trae |

Los datos de origen están en `tools/estaciones/`, y `python3 tools/estaciones/generar.py` los comprueba y genera el archivo. En NAVTEX la letra fija la hora de emisión; de las 257 estaciones de la UIT, 254 cumplen esa regla, y las otras tres se listan al generar.

**Los datos de 518 y 490 kHz son de 2017 y pueden haber cambiado.** Sirven para orientarse; compruébelos con las publicaciones oficiales en vigor.

## API

Para que otro programa recoja los mensajes por la red. Todo en JSON y con las horas en UTC.

| Dirección | Qué devuelve |
| :--- | :--- |
| `GET /api/messages?since=<id>&limit=<n>` | Los mensajes con identificador mayor que `since`, del más antiguo al más reciente, con el texto completo. Sin filtros y sin marcar nada como leído. `limit` de 1 a 500 (200 si no se indica) |
| `GET /api/status` | Versión, estado de cada receptor (`rx1.state`, `rx1.source`, `rx1.band`…), última vez que llegaron muestras, último sincronismo en cada frecuencia, número de mensajes y último identificador |
| `GET /api/gps` | Posición, fijo, satélites y hora del GPS |
| `GET /api/settings` | Ajustes de los receptores y de la posición |

| `GET /api/messages?since=<id>&heard=<hora unix>` | Además, los mensajes que se han vuelto a oír después de `heard`: los que la estación sigue emitiendo |
| `GET /api/export?format=txt` o `csv`, y `&freq=518`, `490` o `4209` | Todos los mensajes en un archivo |
| `GET /api/nrx` | Estado de la salida NMEA 0183 |
| `POST /api/alarm/ack` | Reconoce la alarma de búsqueda y salvamento |
| `POST /api/messages/delete/<id>` o `/all` | Borra un mensaje o todos |

```json
{"id":5,"bbbb":"EE39","station":"E","subject":"E","number":39,"freq":490,
 "received":"2026-11-22T07:38:00Z","complete":false,"characters":765,"errors":3,
 "satisfactory":false,"heard":1795333080,"in_slot":false,"unread":true,
 "message":"ZCZC EE39\n062040 UTC NOV 21\n..."}
```

`freq` vale 518, 490 o 4209 (por 4209,5 kHz). `errors` son los caracteres que no se pudieron recibir (los asteriscos); `satisfactory`, que el mensaje llegó entero y con menos de un 4 % de errores; `heard`, la última vez que se oyó; `in_slot`, si llegó en el turno de su letra. El uso previsto es preguntar cada minuto con el último `id` recibido.

Un mensaje del tema D sin reconocer lleva además `"alarm":true`, y `/api/status` dice cuántos hay en `sar_alarms`.

Los cambios (`/api/settings/<clave>/<valor>`, `/saveconfig/<ajuste>/<valor>`, `/api/messages/delete/…` y `/api/alarm/ack`) solo se aceptan por `POST` y con la cabecera `X-Navtex`, de modo que otra página abierta en el mismo navegador no puede cambiar nada.

## Reglas de un receptor NAVTEX

El programa sigue lo que piden a un receptor la resolución MSC.148(77) de la OMI y el Manual NAVTEX, y las características de la Recomendación UIT-R M.540 (impresión directa de banda estrecha con corrección de errores, modo B colectivo: 100 baudios y 170 Hz de desplazamiento).

| Regla | Cómo se aplica |
| :--- | :--- |
| Un mensaje se da por bien recibido con menos de un 4 % de caracteres erróneos (MSC.148(77), 6.2.3) | Se cuentan los caracteres que no se pudieron recibir. Solo un mensaje completo y por debajo del 4 % impide que se guarde su repetición |
| La identidad de un mensaje (B1B2B3B4) se recuerda de 60 a 72 horas (MSC.148(77), 6.2.2) | 72 horas. En ese tiempo, la repetición no vuelve a aparecer como nueva; después, sí |
| Un mensaje mal recibido no se recuerda | Si la estación lo repite mejor, la copia nueva sustituye a la anterior |
| Los mensajes con B3B4 = 00 se muestran siempre (UIT-R M.540, Manual NAVTEX 5.4.3) | Se guardan cada vez, y la interfaz los enseña aunque no se siga esa estación o ese tema |
| Los temas A, B, D y L no se pueden rechazar (Manual NAVTEX 5.3.4) | En la interfaz no se pueden desmarcar |
| El carácter mal recibido se marca con un asterisco (MSC.148(77), 5.7) | Así se guarda y así se muestra |
| Cada letra B1 tiene diez minutos cada cuatro horas: A a las 0000, B a las 0010… X a las 0350 (Manual NAVTEX, tabla 2) | Cada mensaje dice si llegó en el turno de su letra, y con la hora se averigua cuál de las estaciones que comparten letra lo emitió |
| Memoria para al menos 200 mensajes (MSC.148(77), 6.1.1) | Sin límite de número; se borran al cumplir el tiempo de conservación sin volver a oírse |
| Una interfaz para pasar los datos a otros equipos, según IEC 61162 (MSC.148(77), 9) | Sentencias `NRX` de NMEA 0183 |

| La información de búsqueda y salvamento (B2 = D) da una alarma que solo se puede apagar a mano (MSC.148(77), 7.1; IEC 61097-6, 4.3.9) | Aviso rojo arriba de la interfaz, con pitido, hasta que se pulsa «Reconocer la alarma». El mensaje se muestra aunque no se siga esa estación. La repetición del mismo mensaje no la vuelve a disparar |
| La alarma se comunica con la sentencia `ALR`, cada 30 segundos hasta que se reconoce, y se puede reconocer con `ACK` (IEC 61097-6, 4.3.9.2, 4.3.9.3 y 8.4.2) | Por la salida NMEA 0183: alarma número 003 |

El pitido depende del navegador: la mayoría no deja sonar una página hasta que se ha tocado alguna vez, y no llega a los 75–85 dBA que pide la norma para un equipo de puente. Este programa no está homologado ni sustituye a un receptor que lo esté.

**Borrar.** Un mensaje borrado deja de verse, pero su identidad se sigue recordando hasta 72 horas: la repetición de la estación no lo trae de vuelta.

**Conservación.** De serie, un mensaje se borra a las 72 horas de la última vez que se oyó. Se puede alargar hasta un año (pestaña Receptores). Mientras la estación lo siga emitiendo, no se borra.

## Salida NMEA 0183

Cada mensaje nuevo sale en sentencias `NRX`, en una de dos formas:

**Furuno** (de serie; la que usan la app y el plotter APRS LoRa+):

```
$CRNRX,GA05,1,12,518,22,11,2026,090700,0.0,TEXTO DE LA PRIMERA LINEA*hh
```

Identidad, número de línea, total de líneas, frecuencia (518, 490 o 4209), día, mes, año, hora UTC de recepción, porcentaje de errores y texto. Una línea de texto por sentencia; las largas se parten, en un espacio si se puede.

**IEC 61097-6** (anexo C; la de la norma):

```
$CRNRX,003,001,00,GA05,2,090700,22,11,2026,85,0,A,PRINCIPIO DEL TEXTO*hh
$CRNRX,003,002,00,,,,,,,,,,SIGUE EL TEXTO^0D^0AOTRA LINEA*hh
```

Total de sentencias, número de sentencia, número de mensaje (00–99), identidad, frecuencia (1 = 490 kHz, 2 = 518 kHz, 3 = 4209,5 kHz), hora UTC, día, mes, año, total de caracteres, caracteres mal recibidos, estado (`A` si el mensaje llegó hasta su NNNN, `V` si no) y texto. El texto va seguido de una sentencia a la siguiente, con los finales de línea como `^0D^0A`; en las sentencias siguientes los demás campos van vacíos.

En las dos, ninguna sentencia pasa de 82 caracteres y los caracteres reservados van como `^hh`.

**Alarma.** Mientras haya un mensaje de búsqueda y salvamento sin reconocer, sale cada 30 segundos `$CRALR,hhmmss.00,003,A,V,NAVTEX: Search and rescue information*hh`; al reconocerla, una vez más con `V,A`. Por el servidor TCP se puede reconocer con `$--ACK,003*hh`.

Se ajusta en la pestaña Receptores:

- **UDP**: destino `equipo:puerto` (vale la dirección de difusión de la red, por ejemplo `192.168.1.255:10110`). Una sentencia por datagrama.
- **TCP**: puerto de un servidor al que se conectan los programas (hasta 8 a la vez).

Solo salen los mensajes que llegan desde que se enciende la salida, no los ya guardados. La salida no tiene contraseña; de quien se conecta por TCP solo se atiende la sentencia `ACK` de la alarma.

**NMEA 2000.** No existe un mensaje normalizado de NMEA 2000 para el texto de NAVTEX; la salida normalizada es la sentencia `NRX` de NMEA 0183. Este programa no inventa uno: al bus llegan a través de S.A.T.-APRS+, que los recoge por la API.

## Seguridad

La interfaz no tiene contraseña: está pensada para la red local o la de a bordo. No la exponga a Internet.

- El servidor web y los receptores no se ejecutan como `root`; el servidor web va además encerrado en su carpeta.
- Solo se sirven los archivos de la interfaz; no se puede pedir ninguno de fuera.
- Los ajustes solo admiten claves y valores conocidos, y las consultas a la base de datos no se construyen con texto recibido.
- Lo recibido por radio se muestra siempre como texto, y el navegador tiene prohibido cargar nada que no sea de la propia interfaz.
- Con `navtexwebserver 8080 127.0.0.1` (o `WEB_ADDRESS=127.0.0.1`) escucha solo en el propio equipo.

## Solución de problemas

| Síntoma | Causa probable | Qué hacer |
| :--- | :--- | :--- |
| Receptor «Sin equipo» | Desconectado, o en uso por otro programa | `lsusb`; pare el otro programa. El receptor lo reintenta cada 15 s |
| El RTL-SDR no aparece en `lsusb` | Puerto o cable; con `error -71` en `dmesg`, mal contacto o falta de alimentación | Otro puerto, sin alargador, o un concentrador con alimentación propia |
| «Recibiendo» pero sin mensajes | Aún no ha emitido ninguna estación en alcance, o la antena no tiene alimentación | Mire las próximas emisiones en «Estaciones»; revise la alimentación de la antena |
| Sin sincronismo en ninguna frecuencia tras varias horas | Antena, toma equivocada o ruido | Compruebe la toma de antena elegida y aleje la antena de fuentes de ruido |
| «gpsd no contesta» | gpsd parado, o en otro equipo | `systemctl status gpsd`; `NAVTEX_GPSD` en `/etc/default/navtex` |
| «sin posición · buscando satélites» que no se resuelve | El GPS no entrega datos | `cgps`; con un LBE-1420, véase [Posición por GPS](#posición-por-gps) |
| La página no carga desde otro equipo | Cortafuegos | Abra el puerto 8080 a la red local |

## Créditos y licencia

- El receptor original, con el demodulador y el descodificador SITOR-B, es de **bartelvdh**: [bartelvdh/Navtex](https://github.com/bartelvdh/Navtex).
- Los cambios de este fork son © 2026 Manuel - EA7JRS.
- Desde la versión 1.1.0 el fork se distribuye bajo **GPL-3.0-or-later** ([LICENSE](LICENSE)), con un permiso adicional para enlazarlo con la API de SDRplay.
- El código original conserva su licencia MIT y su aviso ([LICENSE-MIT-original](LICENSE-MIT-original)).
- El detalle de cada parte, incluidos los datos de las estaciones, está en [COPYING.md](COPYING.md).

## Apoya el proyecto

Si te resulta útil: [paypal.me/ea7jrs](https://paypal.me/ea7jrs)

## Previsto

- La sentencia `NRM` de IEC 61097-6 (elegir estaciones y temas desde otro equipo).
