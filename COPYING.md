# Licencias de Navtex (fork EA7JRS)

Navtex · © 2026 Manuel - EA7JRS, a partir de Navtex · © 2024 bartelvdh.

## El programa

Desde la versión 1.1.0, este fork se distribuye bajo la **GNU General Public License, versión 3 o posterior** (`GPL-3.0-or-later`). El texto está en [LICENSE](LICENSE).

    Este programa es software libre: puede redistribuirlo y modificarlo bajo los
    términos de la GNU General Public License publicada por la Free Software
    Foundation, en su versión 3 o, a su elección, cualquier versión posterior.

    Este programa se distribuye con la esperanza de que sea útil, pero SIN
    NINGUNA GARANTÍA; ni siquiera la garantía implícita de COMERCIALIZACIÓN o
    de ADECUACIÓN A UN FIN DETERMINADO. Véase la licencia para más detalles.

La versión 1.0.0 se publicó bajo licencia MIT y lo sigue estando para quien la obtuvo así.

## Permiso adicional: API de SDRplay

Para recibir con equipos SDRplay, el programa se enlaza con la API de SDRplay, que es una biblioteca privativa. Conforme a la sección 7 de la GPL versión 3 se concede este permiso adicional:

    Additional permission under GNU GPL version 3 section 7

    If you modify this Program, or any covered work, by linking or combining
    it with the SDRplay API library (or a modified version of that library),
    containing parts covered by the terms of the SDRplay API licence, the
    licensors of this Program grant you additional permission to convey the
    resulting work. Corresponding Source for a non-source form of such a
    combination shall include the source code for the parts of this Program,
    but not for the SDRplay API library.

Este permiso lo concede el titular de los cambios del fork y cubre esos cambios. El código original no lo necesita, por ser MIT.

**Aviso para quien distribuya ejecutables.** Este proyecto se distribuye en código fuente, y cada cual lo compila en su equipo. El soporte de RTL-SDR se enlaza con librtlsdr, que es GPL y de otros autores, y este permiso no alcanza a esa biblioteca. Quien quiera distribuir un ejecutable compilado a la vez con librtlsdr y con la API de SDRplay debe valorar por su cuenta si las dos licencias lo permiten; compilarlo con uno solo de los dos soportes evita la duda.

## El código original

El receptor del que parte este fork, con el demodulador y el descodificador, es de bartelvdh y se publicó bajo licencia MIT. Su aviso de copyright y el texto íntegro de esa licencia se conservan en [LICENSE-MIT-original](LICENSE-MIT-original), como ella misma exige. Ese código puede seguir obteniéndose bajo MIT en [bartelvdh/Navtex](https://github.com/bartelvdh/Navtex).

Archivos que proceden del original, modificados o no en este fork: `receiver/capt_sched.c`, `decoder.C`, `decoder.h`, `fir1cpp.*`, `fir2cpp.*`, `fir3cpp.*`, `message_store.*`, `nav_b_sm.*`, `nav_sched.*`, `generate_db.*`, `receiver/COPYING` (copia de su licencia), `web_server/https_fileserver.c` y los archivos de autotools. Las modificaciones hechas en ellos son GPL-3.0-or-later.

## Otras partes

| Parte | Licencia |
| :--- | :--- |
| `web_server/https_fileserver.c` | Parte de un ejemplo de libmicrohttpd, LGPL 2.1 o posterior; conserva su aviso |
| `receiver/wav.c`, `receiver/wav.h` | Biblioteca de terceros para archivos WAV, de Zhaosheng Pan, incluida ya en el original sin texto de licencia. Parece ser libwav, que se publica bajo MPL 2.0, compatible con la GPL; no se ha modificado y conserva su aviso de autoría |
| `web_server/build/estaciones.json`, estaciones de 4209,5 kHz, y `tools/estaciones/wikipedia_4209.csv` | Datos de Wikipedia, «List of Navtex stations», CC BY-SA 4.0 |
| `tools/estaciones/itu_tabla4.csv` | Datos de la Tabla 4 de la UIT, «Coast stations participating in the NAVTEX services» (2017) |

## Bibliotecas con las que se enlaza

libmicrohttpd (LGPL), cJSON (MIT), SQLite (dominio público), librtlsdr (GPL-2.0-or-later, opcional) y la API de SDRplay (privativa, opcional).
