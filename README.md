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
Before installing this software, make sure to install the sdrplay API version 3.15 or higher (https://www.sdrplay.com/api/)



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

### Previsto

- Servicios de systemd, sin `root` y en alternancia con otros programas que usen el mismo receptor.
- Receptores RTL-SDR, además de los SDRplay.
- Salida de los mensajes hacia S.A.T.-APRS+ y hacia NMEA 2000.
- Interfaz web nueva, en español.

