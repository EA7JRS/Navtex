# Navtex
Navtex receiver based on SDRPLAY SDR devices, meant for practical use on recreational boats


The main dish of this project is an sdr (software defined radio) experiment.
As a recreational sailor and licenced Ham operator, I though it was a fun and useful project to go for.
The main trigger is the fact that no good and usable navtex decoding software for use in leisure crafts exists at this moment.
Projects like openplotter have brought together a great deal of excellent software to cover a lot of the navigation and comminication needs of a recreational boat. However a navtext receiver is still a missing component in that ecosystem.
Sure there's excellent software like YAND and FLDigi. The thing is that those are just meant for the occasional HAM that wants to capture some navtex messages for fun.
As a sailor you need to be able to receive those messages without being in front of a GUI at the exact moment the messages are transmitted. Reception should happen in background. Next to that Navtex software should allow you to select messages based on message types (subject indicator) and message sources (transmitter identity), so the B1 and B2 characters of the navtex message, like a standard commercial NAVTEX receiver, more or less.

So I decided to create this background process that allows navtex demodulation and decoding.
Next to that I created a web-based GUI application for displaying messages. The gui application is based on the react javascript framework. The communication with the backend is via a json based (REST) API. This API is exposed by a lightweight http server in the backend. Communication between the web server and the receiver process runs via an SQLite database.

Note that the sdr code is written exclusively for sdrplay devices. The reason is that these are high quality devices, with a fairly well documented API, and with good developer support.

# Installation
Before installing this software, make sure to install the sdrplay API version 3.15 or higher (https://www.sdrplay.com/api/) if you use an SDRplay receiver, and/or librtlsdr-dev for RTL-SDR dongles (see the fork notes below)



Also run following commands to install some prerequisites:

sudo apt-get install libcjson-dev

sudo apt-get install libsqlite3-dev

sudo apt-get install libmicrohttpd-dev

sudo apt-get install sqlite3

sudo apt-get install autotools-dev autoconf


then git clone this repository:

git clone https://github.com/bartelvdh/Navtex


and do the following:

cd Navtex

autoreconf

automake --add-missing

autoreconf

./configure

make

sudo make install


# Fork EA7JRS/Navtex

Este fork parte del trabajo de bartelvdh (licencia MIT, que se conserva) y lo adapta al ecosistema APRS LoRa+ / S.A.T.-APRS+. Cambios del fork: © 2026 Manuel - EA7JRS, bajo la misma licencia MIT.

## Cambios

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

### Fase 5 · interfaz web nueva, en español

La interfaz original venía compilada y sin su código fuente, así que no se podía traducir ni modificar. Se ha sustituido por una nueva (`web_server/build/`: `index.html`, `estilo.css`, `app.js`), escrita a mano y sin paso de compilación:

- **Mensajes:** lista con el tema en claro, la primera línea del texto, la estación, la frecuencia y hace cuánto llegó; colores por importancia (salvamento, aviso meteorológico, aviso a los navegantes); marca de «nuevo» y de «cortado»; filtros por frecuencia y tema, búsqueda en el texto y opción de ver también lo que no se sigue.
- **Estado del receptor:** si está recibiendo, con qué equipo, y el último sincronismo en 518 y en 490 kHz.
- **Estaciones y temas:** por frecuencia. Los temas A, B, D y L no se pueden quitar, como en un receptor homologado.
- Se ve bien en el móvil, sigue el modo claro u oscuro del equipo y **no carga nada de Internet**: funciona a bordo sin conexión.
- Lo recibido por radio se muestra siempre como texto, nunca se interpreta, y el servidor prohíbe a la página cargar nada que no sea suyo.

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

### Previsto

- Sentencias `NRX` de NMEA 0183 para equipos que las admitan.
