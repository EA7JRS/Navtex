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
| Mensajes incompletos | Una recepción cortada se guarda marcada, y no sustituye a una copia mejor |

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

```json
{"id":5,"bbbb":"EE39","station":"E","subject":"E","number":39,"freq":490,
 "received":"2026-11-22T07:38:00Z","complete":false,"unread":true,
 "message":"ZCZC EE39\n062040 UTC NOV 21\n..."}
```

`freq` vale 518, 490 o 4209 (por 4209,5 kHz). El uso previsto es preguntar cada minuto con el último `id` recibido.

Los cambios (`/api/settings/<clave>/<valor>` y `/saveconfig/<ajuste>/<valor>`) solo se aceptan por `POST` y con la cabecera `X-Navtex`, de modo que otra página abierta en el mismo navegador no puede cambiar nada.

**NMEA 2000.** No existe un mensaje normalizado de NMEA 2000 para el texto de NAVTEX; el formato normalizado es la sentencia `NRX` de NMEA 0183. Este programa no inventa uno: los mensajes llegan al bus a través de S.A.T.-APRS+, que los recoge por esta API.

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

- Sentencias `NRX` de NMEA 0183 para equipos que las admitan.
