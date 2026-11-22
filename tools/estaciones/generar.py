#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Manuel - EA7JRS
"""Genera web_server/build/estaciones.json a partir de las tablas de estaciones.

Fuentes:
  itu_tabla4.csv      UIT, «Coast stations participating in the NAVTEX services»
                      (Tabla 4; coordinada con la OMI, GMDSS.1/Circ.21, 31-05-2017):
                      518 y 490 kHz. https://www.itu.int/en/ITU-R/terrestrial/mars/Documents/Navtex.pdf
  wikipedia_4209.csv  Wikipedia, «List of Navtex stations», tabla de 4209,5 kHz
                      (CC BY-SA 4.0), consultada el 22-11-2026. La tabla de la UIT
                      no trae el canal de onda corta.

  ihm_2023.csv        Comprobación de las estaciones de las NAVAREA II y III con «Radioseñales»,
                      edición de 2023, del Instituto Hidrográfico de la Marina (España): estado
                      (activa / inactiva / prevista), alcance en millas, a qué horas se emite el
                      boletín meteorológico («todas» = en todos los turnos) y MMSI del centro. Solo
                      esos datos sueltos, cotejados uno a uno; la publicación es © IHM y no se copia.
                      Donde difiere de la UIT (2017) manda esta, más reciente, y la nota lo dice.

No se incluyen las emisiones japonesas en 424 kHz (el programa no las recibe)
ni las estaciones sin letra asignada.

Uso:  python3 tools/estaciones/generar.py
Comprueba cada fila (coordenadas, letra, horas) y avisa de las que no siguen
la regla de turnos (la letra fija el turno: A a las 00:00, B a las 00:10...).
"""
import csv, json, os, re, sys

AQUI = os.path.dirname(os.path.abspath(__file__))
SALIDA = os.path.join(AQUI, '..', '..', 'web_server', 'build', 'estaciones.json')
ESTADOS = {'operational': 'activa', 'non operational': 'inactiva', 'planned': 'prevista', 'under trial': 'en pruebas'}


def coordenada(texto):
    """«36°02'.53N» (grados, minutos y centésimas) o decimal con signo. None si no hay dato."""
    t = texto.strip()
    if not t or t == 'N.I.':
        return None
    m = re.fullmatch(r"(\d{1,3})°(\d{1,2})'\.(\d+)([NSEW])", t)
    if m:
        grados, minutos, cent, h = int(m.group(1)), int(m.group(2)), m.group(3), m.group(4)
        v = grados + (minutos + float('0.' + cent)) / 60.0
        return round(-v if h in 'SW' else v, 4)
    return round(float(t), 4)


def leer(nombre, fuente):
    filas, avisos = [], []
    with open(os.path.join(AQUI, nombre), encoding='utf-8', newline='') as f:
        for n, r in enumerate(csv.DictReader(f), start=2):
            donde = f'{nombre}:{n} {r["estacion"]}'
            b1 = r['b1'].strip()
            if not re.fullmatch(r'[A-Z]', b1):
                sys.exit(f'{donde}: letra no válida «{b1}»')
            horas = [h.strip() for h in r['horas'].split(',') if h.strip()]
            for h in horas:
                if not re.fullmatch(r'([01]\d|2[0-3])[0-5]\d', h):
                    sys.exit(f'{donde}: hora no válida «{h}»')
            lat, lon = coordenada(r['lat']), coordenada(r['lon'])
            if lat is not None and not (-90 <= lat <= 90 and -180 <= lon <= 180):
                sys.exit(f'{donde}: coordenadas fuera de rango')
            khz = float(r['khz'])
            estado_txt = r['estado'].strip()
            clave = estado_txt.split(';')[0].split('(')[0].strip().lower()
            if clave not in ESTADOS:
                sys.exit(f'{donde}: estado desconocido «{estado_txt}»')
            # Regla de turnos: la letra fija el minuto dentro de cada ciclo de 4 horas
            turno = (ord(b1) - 65) * 10
            esperadas = ['%02d%02d' % ((turno + c * 240) // 60, (turno + c * 240) % 60) for c in range(6)]
            regular = horas == esperadas
            if not regular:
                avisos.append(f'{donde} ({khz:g} kHz, {b1}): {",".join(horas)}')
            alcance = r['alcance'].strip()
            filas.append({
                'area': r['area'].strip(), 'pais': r['pais'].strip(), 'nombre': r['estacion'].strip(),
                'lat': lat, 'lon': lon, 'khz': 4209 if khz > 4000 else int(khz), 'letra': b1,
                'horas': horas, 'alcance': int(alcance) if alcance.isdigit() else None,
                'idioma': '' if r['idioma'].strip() in ('', 'N.I.') else r['idioma'].strip(),
                'estado': ESTADOS[clave], 'nota': estado_txt.split(';', 1)[1].strip() if ';' in estado_txt else '',
                'regular': regular, 'fuente': fuente,
            })
    return filas, avisos


def main():
    itu, a1 = leer('itu_tabla4.csv', 'UIT')
    hf, a2 = leer('wikipedia_4209.csv', 'Wikipedia')
    # La misma estación figura en dos zonas (Tarifa, Cross Corsen): una sola vez
    vistas, estaciones = set(), []
    for e in itu + hf:
        clave = (e['nombre'], e['khz'], e['letra'])
        if clave in vistas:
            continue
        vistas.add(clave)
        estaciones.append(e)
    # Comprobación con Radioseñales 2023 (IHM): datos sueltos de las NAVAREA II y III
    indice = {(e['nombre'], e['khz'], e['letra']): e for e in estaciones}
    comprobadas = 0
    with open(os.path.join(AQUI, 'ihm_2023.csv'), encoding='utf-8', newline='') as f:
        for n, r in enumerate(csv.DictReader(f), start=2):
            e = indice.get((r['estacion'], int(r['khz']), r['b1']))
            if e is None:
                sys.exit(f'ihm_2023.csv:{n}: no está en la lista: {r["estacion"]} {r["khz"]} {r["b1"]}')
            notas = [e['nota']] if e['nota'] else []
            if r['estado'] and r['estado'] != e['estado']:
                if r['estado'] not in ESTADOS.values():
                    sys.exit(f'ihm_2023.csv:{n}: estado desconocido «{r["estado"]}»')
                notas.append(f'{r["estado"]} según el IHM (2023); la UIT (2017) la daba {e["estado"]}')
                e['estado'] = r['estado']
            if r['alcance'] and int(r['alcance']) != e['alcance']:
                notas.append(f'alcance según el IHM (2023); la UIT (2017) daba {e["alcance"]} mn')
                e['alcance'] = int(r['alcance'])
            meteo = e['horas'] if r['meteo'] == 'todas' else r['meteo'].split()
            for h in meteo:
                if h not in e['horas']:
                    sys.exit(f'ihm_2023.csv:{n}: {h} no es un turno de {r["estacion"]} {r["b1"]}')
            e['meteo'] = meteo
            e['mmsi'] = r['mmsi']
            e['nota'] = '; '.join(notas)
            e['comprobada'] = 'IHM 2023'
            comprobadas += 1
    for e in estaciones:
        e.setdefault('meteo', [])
        e.setdefault('mmsi', '')
        e.setdefault('comprobada', '')
    datos = {
        'fuentes': [
            {'nombre': 'UIT', 'titulo': 'Coast stations participating in the NAVTEX services (Tabla 4)',
             'fecha': '2017-05-31', 'url': 'https://www.itu.int/en/ITU-R/terrestrial/mars/Documents/Navtex.pdf'},
            {'nombre': 'Wikipedia', 'titulo': 'List of Navtex stations, tabla de 4209,5 kHz (CC BY-SA 4.0)',
             'fecha': '2026-11-22', 'url': 'https://en.wikipedia.org/wiki/List_of_Navtex_stations'},
            {'nombre': 'IHM', 'titulo': 'Radioseñales, edición de 2023 (comprobación de las estaciones de las NAVAREA II y III: estado, alcance y boletines meteorológicos)',
             'fecha': '2023-11', 'url': 'https://publicaciones.defensa.gob.es'},
        ],
        'estaciones': estaciones,
    }
    with open(SALIDA, 'w', encoding='utf-8') as f:
        json.dump(datos, f, ensure_ascii=False, separators=(',', ':'))
        f.write('\n')
    por = {}
    for e in estaciones:
        por[e['khz']] = por.get(e['khz'], 0) + 1
    print(f'{len(estaciones)} estaciones: ' + ', '.join(f'{n} en {k}' for k, n in sorted(por.items())))
    print(f'{comprobadas} comprobadas con Radioseñales 2023 del IHM')
    print(f'{sum(1 for e in estaciones if e["lat"] is None)} sin coordenadas; {len(a1) + len(a2)} con horas fuera de la regla de turnos:')
    for a in a1 + a2:
        print('  ' + a)


if __name__ == '__main__':
    main()
