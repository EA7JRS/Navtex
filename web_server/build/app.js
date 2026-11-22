/*
 * Navtex · interfaz web (fork EA7JRS/Navtex)
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 *
 * Todo lo que llega por radio se escribe con textContent: nunca se interpreta
 * como HTML.
 */
(function () {
  'use strict';
  var I18N = window.NAVTEX_I18N, tr = I18N.tr;   // idioma de la interfaz (i18n.js)

  // Temas (segunda letra de la cabecera, B2)
  var TEMAS = {
    A: tr('Aviso a los navegantes'), B: tr('Aviso meteorológico'), C: tr('Hielos'), D: tr('Búsqueda y salvamento'),
    E: tr('Previsión meteorológica'), F: tr('Practicaje'), G: 'AIS', H: 'LORAN', I: tr('Reservado'), J: tr('Navegación por satélite'),
    K: tr('Otras ayudas electrónicas'), L: tr('Aviso a los navegantes (adicional)'), T: tr('Prueba'), V: tr('Servicio especial'),
    W: tr('Servicio especial'), X: tr('Servicio especial'), Y: tr('Servicio especial'), Z: tr('Sin mensajes')
  };
  var GRAVEDAD = { D: 'rojo', B: 'naranja', A: 'amarillo', L: 'amarillo' };
  var OBLIGATORIOS = 'ABDL';
  var LETRAS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'.split('');
  var TEMAS_AJUSTE = 'ABCDEFGHJKLVWXYZ'.split('');
  var MESES = ['ene', 'feb', 'mar', 'abr', 'may', 'jun', 'jul', 'ago', 'sep', 'oct', 'nov', 'dic'];
  var BANDAS = [
    { f: 518, nombre: tr('518 kHz · internacional, en inglés') },
    { f: 490, nombre: tr('490 kHz · nacional, en el idioma del país') },
    { f: 4209, nombre: tr('4209,5 kHz · onda corta') }
  ];
  var IDIOMAS = {
    English: 'inglés', Spanish: 'español', French: 'francés', Portuguese: 'portugués', Italian: 'italiano', German: 'alemán',
    Dutch: 'neerlandés', Greek: 'griego', Turkish: 'turco', Icelandic: 'islandés', Romanian: 'rumano', Arabic: 'árabe',
    Chinese: 'chino', Korean: 'coreano', Japanese: 'japonés', Vietnamese: 'vietnamita', Farsi: 'persa', Azerbaijani: 'azerí', Creole: 'criollo'
  };
  var MN = 1852;   // metros por milla náutica

  var mensajes = [];            // todos los recibidos, del más reciente al más antiguo
  var config = { stations518: '', messages518: '', stations490: '', messages490: '', stations4209: '', messages4209: '' };
  var configLeida = false;
  var ajustes = {};             // /api/settings: posición y receptores
  var estaciones = [];          // estaciones.json
  var fuentes = [];
  var filtro = { freq: 0, tema: '', texto: '', todo: false };
  var abiertos = {};            // id -> true
  var verTodas = {};            // frecuencia -> true: lista completa de estaciones
  var estado = null;
  var gps = null;               // /api/gps
  var repintarAspecto = function () {};   // la posición decide el aspecto automático
  var posPintada = null;        // posición con la que se pintaron las listas

  function $(id) { return document.getElementById(id); }
  function crear(etiqueta, clase, texto) {
    var e = document.createElement(etiqueta);
    if (clase) e.className = clase;
    if (texto !== undefined) e.textContent = texto;
    return e;
  }
  function dos(n) { return (n < 10 ? '0' : '') + n; }
  // 4209 es el canal de onda corta, 4209,5 kHz
  function nombreFreq(f) { return (f === 4209 ? tr('4209,5') : String(f)) + ' kHz'; }
  function pedir(ruta) {
    return fetch(ruta, { cache: 'no-store' }).then(function (r) {
      if (!r.ok) throw new Error(tr('el servidor respondió ') + r.status);
      return r.json();
    });
  }
  // Los cambios van con POST y una cabecera propia: otra página web no puede hacerlos
  function enviar(ruta) {
    return fetch(ruta, { method: 'POST', cache: 'no-store', headers: { 'X-Navtex': '1' } }).then(function (r) {
      if (!r.ok) throw new Error(tr('el servidor respondió ') + r.status);
    });
  }
  function avisar(id, bien, texto) {
    var a = $(id);
    a.hidden = false;
    a.className = bien ? 'aviso bien' : 'aviso';
    a.textContent = texto;
  }

  // "hace 5 min", "hace 3 h", o la fecha
  function haceCuanto(ms, ahora) {
    var s = Math.round((ahora - ms) / 1000);
    if (!isFinite(s)) return '—';
    if (s < 90) return tr('ahora mismo');
    if (s < 3600) return tr('hace {n} min').replace('{n}', Math.round(s / 60));
    if (s < 36 * 3600) return tr('hace {n} h').replace('{n}', Math.round(s / 3600));
    return tr('hace {n} días').replace('{n}', Math.round(s / 86400));
  }
  function fechaUtc(ms) {
    var d = new Date(ms);
    return d.getUTCDate() + ' ' + tr(MESES[d.getUTCMonth()]) + ' ' + dos(d.getUTCHours()) + ':' + dos(d.getUTCMinutes()) + ' UTC';
  }
  function horaLocal(ms) {
    var d = new Date(ms);
    return dos(d.getHours()) + ':' + dos(d.getMinutes());
  }

  // Primera línea con contenido que no sea la cabecera ni el grupo fecha-hora
  function primeraLinea(texto) {
    var lineas = String(texto).split('\n');
    for (var i = 0; i < lineas.length; i++) {
      var l = lineas[i].trim();
      if (!l || /ZCZC/.test(l) || /^\d{6} ?UTC/.test(l) || /^NNNN/.test(l)) continue;
      return l.length > 90 ? l.slice(0, 90) + '…' : l;
    }
    return '';
  }

  function claveFreq(f) { return f === 490 || f === 4209 ? String(f) : '518'; }
  function seSigue(m) {
    var f = claveFreq(m.freq);
    // Los mensajes con número 00 se muestran siempre, se siga o no esa estación o ese tema
    if (m.number === 0) return true;
    // ...y los de búsqueda y salvamento, hasta que se reconoce su alarma (IEC 61097-6, 4.6.1.1)
    if (m.alarm) return true;
    return config['stations' + f].indexOf(m.station) >= 0 &&
      (OBLIGATORIOS.indexOf(m.subject) >= 0 || config['messages' + f].indexOf(m.subject) >= 0);
  }

  // --------------------------------------------------------------- posición
  function posicionManual() {
    var p = String(ajustes.position || '').split(',');
    var lat = parseFloat(p[0]), lon = parseFloat(p[1]);
    return p.length === 2 && isFinite(lat) && isFinite(lon) ? { lat: lat, lon: lon } : null;
  }
  // La del GPS si se quiere y hay fijo reciente; si no, la escrita a mano
  function usaGps() { return ajustes.position_source !== 'manual'; }
  function posicionGps() {
    return gps && gps.connected && typeof gps.lat === 'number' && typeof gps.lon === 'number' && !(gps.age > 120)
      ? { lat: gps.lat, lon: gps.lon } : null;
  }
  function posicion() {
    return (usaGps() && posicionGps()) || posicionManual();
  }
  // Localizador Maidenhead de seis caracteres
  function localizador(p) {
    var lo = p.lon + 180, la = p.lat + 90;
    if (!(lo >= 0 && lo < 360 && la >= 0 && la < 180)) return '';
    return String.fromCharCode(65 + Math.floor(lo / 20)) + String.fromCharCode(65 + Math.floor(la / 10))
      + Math.floor((lo % 20) / 2) + Math.floor(la % 10)
      + String.fromCharCode(97 + Math.floor((lo % 2) * 12)) + String.fromCharCode(97 + Math.floor((la % 1) * 24));
  }
  // "37° 36,46' N  000° 59,09' O": como se lee en una carta
  function gradosMinutos(v, positivo, negativo, ancho) {
    var a = Math.abs(v), g = Math.floor(a), m = (a - g) * 60;
    if (m >= 59.995) { g += 1; m = 0; }
    var gs = String(g); while (gs.length < ancho) gs = '0' + gs;
    return gs + '° ' + (m < 10 ? '0' : '') + m.toFixed(2).replace('.', ',') + "' " + (v >= 0 ? positivo : negativo);
  }
  function enCarta(p) { return gradosMinutos(p.lat, 'N', 'S', 2) + '  ' + gradosMinutos(p.lon, 'E', I18N.idioma === 'en' ? 'W' : 'O', 3); }
  // "36.53, -6.29" o un localizador Maidenhead de 4, 6 u 8 caracteres
  function leerPosicion(texto) {
    var t = String(texto).trim();
    var m = /^(-?\d{1,2}(?:[.,]\d+)?)\s*[,; ]\s*(-?\d{1,3}(?:[.,]\d+)?)$/.exec(t);
    if (m) {
      var lat = parseFloat(m[1].replace(',', '.')), lon = parseFloat(m[2].replace(',', '.'));
      return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180 ? { lat: lat, lon: lon } : null;
    }
    var g = /^([A-Ra-r])([A-Ra-r])(\d)(\d)(?:([A-Xa-x])([A-Xa-x])(?:(\d)(\d))?)?$/.exec(t);
    if (!g) return null;
    var c = function (x) { return x.toUpperCase().charCodeAt(0) - 65; };
    var lo = c(g[1]) * 20 - 180 + Number(g[3]) * 2, la = c(g[2]) * 10 - 90 + Number(g[4]);
    var anchoLo = 2, anchoLa = 1;
    if (g[5]) { lo += c(g[5]) * (2 / 24); la += c(g[6]) * (1 / 24); anchoLo = 2 / 24; anchoLa = 1 / 24; }
    if (g[7]) { lo += Number(g[7]) * (2 / 240); la += Number(g[8]) * (1 / 240); anchoLo = 2 / 240; anchoLa = 1 / 240; }
    return { lat: la + anchoLa / 2, lon: lo + anchoLo / 2 };   // centro del cuadro
  }
  // Distancia por el círculo máximo, en millas náuticas
  function distanciaMn(a, b) {
    var r = Math.PI / 180;
    var dLat = (b.lat - a.lat) * r, dLon = (b.lon - a.lon) * r;
    var h = Math.sin(dLat / 2) * Math.sin(dLat / 2) + Math.cos(a.lat * r) * Math.cos(b.lat * r) * Math.sin(dLon / 2) * Math.sin(dLon / 2);
    return 6371000 * 2 * Math.atan2(Math.sqrt(h), Math.sqrt(1 - h)) / MN;
  }
  function conDistancia(f) {
    var p = posicion();
    return estaciones.filter(function (e) { return e.khz === f; }).map(function (e) {
      var d = p && e.lat !== null && e.lon !== null ? distanciaMn(p, e) : null;
      return { e: e, d: d, enAlcance: d !== null && e.alcance !== null && d <= e.alcance };
    }).sort(function (a, b) {
      if (a.d === null && b.d === null) return a.e.letra < b.e.letra ? -1 : a.e.letra > b.e.letra ? 1 : 0;
      if (a.d === null) return 1;
      if (b.d === null) return -1;
      return a.d - b.d;
    });
  }
  // Próxima emisión de una estación: milisegundos, o null si no tiene horas
  function proxima(e, ahora) {
    var d = new Date(ahora), mejor = null;
    e.horas.forEach(function (h) {
      var t = Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate(), Number(h.slice(0, 2)), Number(h.slice(2)));
      if (t <= ahora) t += 86400000;
      if (mejor === null || t < mejor) mejor = t;
    });
    return mejor;
  }
  // Onda corta (4209,5 kHz): predicción de VOACAP, que da Wefax si está en este mismo equipo (puerto 8081).
  // Si no está, o no tiene VOACAP, no se enseña nada. En 518 y 490 kHz no vale: VOACAP empieza en 2 MHz.
  var voacap = {}, voacapPedido = {};
  function pedirVoacap(e) {
    var clave = e.nombre + e.letra, p = posicion();
    if (e.khz !== 4209 || e.lat === null || !p || voacapPedido[clave] > Date.now() - 3600000) return;
    voacapPedido[clave] = Date.now();
    fetch('http://' + location.hostname + ':8081/api/voacap?lat=' + e.lat + '&lon=' + e.lon + '&khz=4209.5&kw=5', { cache: 'no-store' })
      .then(function (r) { return r.json(); })
      .then(function (j) { if (j && j.ok && j.rel && j.rel[0]) { voacap[clave] = j; pintarAjustes(); } })
      .catch(function () { /* sin Wefax: nada */ });
  }
  function textoVoacap(e, ms) {
    var j = voacap[e.nombre + e.letra], r;
    if (!j || ms === null) return '';
    r = j.rel[0][new Date(ms).getUTCHours()];
    return typeof r === 'number' ? tr(' · VOACAP: llega el ') + Math.round(r * 100) + tr(' % de los días a esa hora') : '';
  }
  // Boletín meteorológico: en qué turnos lo emite la estación (dato del IHM; vacío si no se sabe)
  function llevaMeteo(e, ms) {
    var d = new Date(ms);
    return !!e.meteo && e.meteo.indexOf(dos(d.getUTCHours()) + dos(d.getUTCMinutes())) >= 0;
  }
  function textoMeteo(e) {
    if (!e.meteo || !e.meteo.length) return '';
    if (e.meteo.length === e.horas.length) return tr(' · boletín meteorológico en todos los turnos');
    return tr(' · boletín meteorológico a las ') + e.meteo.map(function (h) { return h.slice(0, 2) + ':' + h.slice(2); }).join(', ') + ' UTC';
  }
  function horaUtc(ms) { var d = new Date(ms); return dos(d.getUTCHours()) + ':' + dos(d.getUTCMinutes()); }

  // ---------------------------------------------------------------- estado
  function estadoReceptor(n, ahora) {
    var p = 'rx' + n + '.';
    var st = estado[p + 'state'] || '';
    var vivo = Date.parse(estado[p + 'receiver_alive'] || '');
    var fresco = isFinite(vivo) && ahora - vivo < 3 * 60000;
    var origen = estado[p + 'source'] === 'rtlsdr' ? 'RTL-SDR' : estado[p + 'source'] === 'sdrplay' ? 'SDRplay' : '';
    var banda = estado[p + 'band'] === 'hf' ? tr('4209,5 kHz') : tr('518 y 490 kHz');
    if (st === 'off') return { ok: null, texto: tr('Apagado'), detalle: tr('se enciende en Receptores') };
    if (st === 'receiving' && fresco) return { ok: true, texto: tr('Recibiendo'), detalle: banda + (origen ? ' · ' + origen : '') };
    if (st === 'starting' || (st === 'receiving' && !fresco && isFinite(vivo) && ahora - vivo < 6 * 60000)) return { ok: null, texto: tr('Arrancando'), detalle: banda + (origen ? ' · ' + origen : '') };
    if (st === 'unavailable') return { ok: false, texto: tr('Sin equipo'), detalle: (origen || tr('equipo')) + tr(' desconectado o en uso') };
    return { ok: false, texto: tr('Parado'), detalle: st === '' ? tr('el servicio no ha arrancado') : tr('el servicio está parado') };
  }
  function dato(id, etiqueta) {
    var d = crear('div', 'dato');
    d.appendChild(crear('span', 'etiqueta', etiqueta));
    var v = crear('span', 'valor'); v.id = id; d.appendChild(v);
    var x = crear('span', 'detalle'); x.id = id + '-detalle'; d.appendChild(x);
    return d;
  }
  function montarEstado() {
    var raiz = $('estado');
    raiz.textContent = '';
    raiz.appendChild(dato('rx1', tr('Receptor 1')));
    raiz.appendChild(dato('rx2', tr('Receptor 2')));
    raiz.appendChild(dato('sinc-518', tr('518 kHz · internacional')));
    raiz.appendChild(dato('sinc-490', tr('490 kHz · nacional')));
    raiz.appendChild(dato('sinc-4209', tr('4209,5 kHz · onda corta')));
    raiz.appendChild(dato('hora-utc', tr('Hora UTC')));
    raiz.appendChild(dato('gps', tr('Posición GPS')));
    $('gps').className = 'valor mono gps';
    $('hora-utc').className = 'valor mono';
  }
  function pintarEstado() {
    var ahora = Date.now();
    var d = new Date(ahora);
    $('hora-utc').textContent = dos(d.getUTCHours()) + ':' + dos(d.getUTCMinutes()) + ':' + dos(d.getUTCSeconds());
    pintarGps();
    if (!estado) return;
    [1, 2].forEach(function (n) {
      var r = estadoReceptor(n, ahora);
      var v = $('rx' + n);
      v.textContent = '';
      v.appendChild(crear('span', 'punto' + (r.ok === true ? ' ok' : r.ok === false ? ' mal' : '')));
      v.appendChild(document.createTextNode(r.texto));
      $('rx' + n + '-detalle').textContent = r.detalle;
      var e = $('estado-rx' + n);
      if (e) e.textContent = r.texto + ' · ' + r.detalle;
    });
    ['518', '490', '4209'].forEach(function (f) {
      var t = Date.parse(estado['phasing_' + f] || '');
      $('sinc-' + f).textContent = isFinite(t) ? haceCuanto(t, ahora) : tr('todavía ninguno');
      $('sinc-' + f + '-detalle').textContent = tr('último sincronismo');
    });
    if (typeof estado.version === 'string') $('version').textContent = 'v' + estado.version;
    $('hora-utc-detalle').textContent = (estado.messages || 0) + (estado.messages === 1 ? tr(' mensaje guardado') : tr(' mensajes guardados'));
  }

  // -------------------------------------------------------------- mensajes
  function visibles() {
    var busca = filtro.texto.trim().toUpperCase();
    return mensajes.filter(function (m) {
      if (filtro.freq && m.freq !== filtro.freq) return false;
      if (filtro.tema && m.subject !== filtro.tema) return false;
      if (!filtro.todo && configLeida && !seSigue(m)) return false;
      if (busca && m.message.toUpperCase().indexOf(busca) < 0 && m.bbbb.indexOf(busca) < 0) return false;
      return true;
    });
  }

  function pintarTemas() {
    var sel = $('filtro-tema');
    var vistos = {};
    mensajes.forEach(function (m) { vistos[m.subject] = true; });
    var letras = Object.keys(vistos).sort();
    var actual = sel.value;
    while (sel.options.length > 1) sel.remove(1);
    letras.forEach(function (l) {
      var o = crear('option', '', l + ' · ' + (TEMAS[l] || tr('Tema ') + l));
      o.value = l;
      sel.appendChild(o);
    });
    sel.value = letras.indexOf(actual) >= 0 ? actual : '';
    filtro.tema = sel.value;
  }

  // Nombre de la estación de esa letra más probable: la más cercana que la usa
  // ¿Emite esa estación a la hora en que se recibió el mensaje? Cada una tiene su turno de diez
  // minutos; el mensaje se guarda al terminar, así que vale hasta trece minutos después del comienzo.
  function enTurno(e, ms) {
    var d = new Date(ms), min = d.getUTCHours() * 60 + d.getUTCMinutes();
    return e.horas.some(function (h) {
      var desde = (min - (Number(h.slice(0, 2)) * 60 + Number(h.slice(2))) + 1440) % 1440;
      return desde <= 13;
    });
  }
  // Qué estación mandó el mensaje: varias comparten letra, pero no turno. Primero las que emitían
  // a esa hora, y entre ellas la más cercana. { nombre, porTurno } o null.
  function estacionDe(m) {
    var lista = conDistancia(m.freq === 490 || m.freq === 4209 ? m.freq : 518).filter(function (x) { return x.e.letra === m.station; });
    var turno = lista.filter(function (x) { return enTurno(x.e, m.recibido); });
    if (turno.length === 1 || (turno.length > 1 && turno[0].d !== null)) return { nombre: turno[0].e.nombre, porTurno: true };
    if (!lista.length || lista[0].d === null) return null;
    return { nombre: lista[0].e.nombre, porTurno: false };
  }

  function pintarLista() {
    var lista = $('lista');
    var ahora = Date.now();
    var ver = visibles();
    var nuevos = mensajes.filter(function (m) { return m.unread && (!configLeida || seSigue(m)); }).length;
    var cuenta = $('cuenta-nuevos');
    cuenta.hidden = nuevos === 0;
    cuenta.textContent = String(nuevos);
    document.title = (nuevos ? '(' + nuevos + ') ' : '') + 'Navtex';

    lista.textContent = '';
    ver.forEach(function (m) {
      var li = crear('li', 'mensaje' + (m.unread ? ' nuevo' : '') + (abiertos[m.id] ? ' abierto' : ''));
      li.id = 'mensaje-' + m.id;
      var boton = crear('button');
      boton.type = 'button';
      boton.setAttribute('aria-expanded', abiertos[m.id] ? 'true' : 'false');
      boton.appendChild(crear('span', 'cab mono ' + (GRAVEDAD[m.subject] || ''), m.bbbb));
      var titulo = crear('span', 'titulo', TEMAS[m.subject] || tr('Tema ') + m.subject);
      if (m.unread) titulo.appendChild(crear('span', 'marca-nuevo', tr('nuevo')));
      if (m.keep) titulo.appendChild(crear('span', 'marca-nuevo', tr('conservado')));
      boton.appendChild(titulo);
      boton.appendChild(crear('span', 'flecha', '›'));
      var meta = crear('span', 'meta');
      var linea = primeraLinea(m.message);
      if (linea) meta.appendChild(crear('span', 'resumen-linea', linea));
      meta.appendChild(document.createTextNode(tr('estación ') + m.station + ' · ' + nombreFreq(m.freq) + ' · ' + haceCuanto(m.recibido, ahora) + ' (' + fechaUtc(m.recibido) + ')'));
      if (!m.complete) { meta.appendChild(document.createTextNode(' · ')); meta.appendChild(crear('span', 'cortado', tr('cortado'))); }
      else if (m.satisfactory === false) { meta.appendChild(document.createTextNode(' · ')); meta.appendChild(crear('span', 'cortado', tr('con errores'))); }
      boton.appendChild(meta);
      boton.addEventListener('click', function () { alternar(m); });
      li.appendChild(boton);
      if (abiertos[m.id]) {
        var cuerpo = crear('div', 'cuerpo');
        cuerpo.appendChild(crear('pre', '', m.message));
        var est = estacionDe(m);
        var oido = typeof m.heard === 'number' && m.heard * 1000 - m.recibido > 600000 ? m.heard * 1000 : 0;
        var malos = typeof m.errors === 'number' ? m.errors : 0;
        var tasa = malos && m.characters ? (100 * malos / m.characters).toFixed(1).replace('.', I18N.idioma === 'en' ? '.' : ',') : '';
        cuerpo.appendChild(crear('p', 'nota', tr('Recibido el ') + fechaUtc(m.recibido) + ' (' + horaLocal(m.recibido) + tr(' hora local) en ') + nombreFreq(m.freq) + '.'
          + (oido ? tr(' La estación lo sigue emitiendo: oído por última vez ') + haceCuanto(oido, ahora) + '.' : '')
          + (est ? (est.porTurno ? tr(' Por la letra y la hora, lo emitió ') + est.nombre + '.' : tr(' La estación ') + m.station + tr(' más cercana en esa frecuencia es ') + est.nombre + '.') : '')
          + (m.in_slot === false ? tr(' Llegó fuera del turno de la letra ') + m.station + tr(': puede ser una estación lejana con otro horario.') : '')
          + (m.keep ? tr(' Marcado para conservarlo: no se borra al cumplir el tiempo de conservación.') : '')
          + (m.complete ? '' : tr(' La recepción se cortó antes del final del mensaje.'))
          + (malos ? ' ' + malos + (malos === 1 ? tr(' carácter no se pudo recibir bien (el asterisco): ') : tr(' caracteres no se pudieron recibir bien (los asteriscos): ')) + tasa + ' %.' : '')
          + (m.satisfactory === false && m.complete ? tr(' Con un 4 % o más de errores la recepción no se da por buena: si la estación lo repite mejor, la copia nueva sustituye a esta.') : '')));
        var bk = crear('button', 'boton pequeno', m.keep ? tr('Dejar de conservar') : tr('Conservar siempre'));
        bk.type = 'button'; bk.id = 'conservar-' + m.id;
        bk.addEventListener('click', function () {
          enviar('/api/messages/keep/' + m.id + '/' + (m.keep ? '0' : '1')).then(leerTodo).catch(function (e) {
            var error = $('error-general');
            error.hidden = false;
            error.textContent = e.message.indexOf('409') >= 0 ? tr('Ya hay 50 mensajes conservados: deje de conservar alguno.') : tr('No se pudo guardar: ') + e.message + '.';
          });
        });
        cuerpo.appendChild(bk);
        li.appendChild(cuerpo);
      }
      lista.appendChild(li);
    });

    var ocultos = mensajes.length - (filtro.todo || !configLeida ? mensajes.length : mensajes.filter(seSigue).length);
    $('resumen').textContent = mensajes.length === 0 ? ''
      : ver.length + (ver.length === 1 ? tr(' mensaje') : tr(' mensajes'))
        + (ocultos > 0 ? ' · ' + ocultos + tr(' más de estaciones o temas que no sigue') : '');
    $('acciones-mensajes').hidden = mensajes.length === 0;
    var vacio = $('vacio');
    vacio.hidden = ver.length > 0;
    vacio.textContent = mensajes.length === 0
      ? tr('Todavía no se ha recibido ningún mensaje. Cada estación emite cada cuatro horas, en su turno de diez minutos.')
      : tr('Ningún mensaje con estos filtros.');
  }

  function alternar(m) {
    if (abiertos[m.id]) { delete abiertos[m.id]; pintarLista(); return; }
    abiertos[m.id] = true;
    if (m.unread) {
      m.unread = false;
      // Abrirlo lo marca como leído en el receptor
      fetch('/messagetext/' + m.id, { cache: 'no-store' }).catch(function () { /* se marcará la próxima vez */ });
    }
    pintarLista();
  }

  // ------------------------------------------------------------ estaciones
  function guardar(tag, valor) {
    config[tag] = valor;
    pintarAjustes();
    pintarProximas();
    pintarLista();
    enviar('/saveconfig/' + tag + '/' + valor).then(function () {
      avisar('estado-ajustes', true, tr('Guardado.'));
    }).catch(function (e) {
      avisar('estado-ajustes', false, tr('No se pudo guardar: ') + e.message + '.');
      leerConfig();
    });
  }
  function conLetra(valor, letra, poner) {
    var s = valor.split('').filter(function (c) { return c !== letra; });
    if (poner) s.push(letra);
    return s.sort().join('');
  }
  function botonLetra(f, tag, l, id) {
    var activa = config[tag].indexOf(l) >= 0;
    var bt = crear('button', 'letra' + (activa ? ' activa' : ''), l);
    bt.type = 'button';
    if (id) bt.id = id;
    bt.setAttribute('aria-pressed', activa ? 'true' : 'false');
    bt.setAttribute('aria-label', tr('Estación ') + l + (activa ? tr(', seguida') : tr(', sin seguir')));
    bt.addEventListener('click', function () { guardar(tag, conLetra(config[tag], l, !activa)); });
    return bt;
  }

  function pintarAjustes() {
    var raiz = $('ajustes');
    var ahora = Date.now();
    var hayPos = posicion() !== null;
    raiz.textContent = '';
    BANDAS.forEach(function (b) {
      var f = String(b.f);
      var tagE = 'stations' + f, tagT = 'messages' + f;
      var t = crear('div', 'tarjeta');
      t.id = 'ajustes-' + f;
      t.appendChild(crear('h2', '', b.nombre));

      // Estaciones de la base de datos, por distancia
      var todas = conDistancia(b.f);
      var cerca = todas.filter(function (x, i) { return verTodas[f] || (hayPos ? i < 8 : false); });
      t.appendChild(crear('h3', '', hayPos ? tr('Estaciones, de la más cercana a la más lejana') : tr('Estaciones')));
      if (!hayPos && !verTodas[f]) {
        t.appendChild(crear('p', 'nota', tr('Indique su posición arriba para ver las más cercanas, o despliegue la lista completa.')));
      }
      if (cerca.length) {
        var ul = crear('ul', 'estaciones');
        cerca.forEach(function (x) {
          var e = x.e;
          var li = crear('li');
          li.appendChild(botonLetra(b.f, tagE, e.letra));
          var nombre = crear('span', 'nombre', e.nombre);
          if (x.enAlcance) nombre.appendChild(crear('span', 'etiq ok', tr('en alcance')));
          if (e.estado !== 'activa') nombre.appendChild(crear('span', 'etiq no', e.estado));
          li.appendChild(nombre);
          li.appendChild(crear('span', 'dist', x.d === null ? '' : Math.round(x.d) + tr(' mn')));
          var px = proxima(e, ahora);
          pedirVoacap(e);
          li.appendChild(crear('span', 'mas', e.pais
            + (e.alcance !== null ? tr(' · alcance ') + e.alcance + tr(' mn') : '')
            + (e.idioma ? ' · ' + (IDIOMAS[e.idioma] ? tr(IDIOMAS[e.idioma]) : e.idioma) : '')
            + (px !== null ? tr(' · próxima ') + horaUtc(px) + ' UTC' + (llevaMeteo(e, px) ? tr(' (con boletín meteorológico)') : '') : '')
            + textoVoacap(e, px)
            + textoMeteo(e)
            + (e.mmsi ? ' · MMSI ' + e.mmsi : '')
            + (e.nota ? ' · ' + e.nota : '')
            + (e.comprobada ? tr(' · comprobada: IHM 2023') : '')));
          ul.appendChild(li);
        });
        t.appendChild(ul);
      }
      var acc = crear('div', 'acciones');
      var mas = crear('button', 'boton pequeno', verTodas[f] ? tr('Ver solo las cercanas') : tr('Ver las {n} estaciones').replace('{n}', todas.length));
      mas.type = 'button';
      mas.id = 'ver-todas-' + f;
      mas.addEventListener('click', function () { verTodas[f] = !verTodas[f]; pintarAjustes(); });
      acc.appendChild(mas);
      if (hayPos) {
        var enAlc = todas.filter(function (x) { return x.enAlcance && x.e.estado === 'activa'; });
        // En onda corta no hay alcance publicado: se ofrecen las tres más cercanas
        var propuesta = enAlc.length ? enAlc : (b.f === 4209 ? todas.filter(function (x) { return x.d !== null; }).slice(0, 3) : []);
        if (propuesta.length) {
          var auto = crear('button', 'boton pequeno', enAlc.length ? tr('Seguir solo las que están en alcance') : tr('Seguir solo las tres más cercanas'));
          auto.type = 'button';
          auto.id = 'seguir-alcance-' + f;
          auto.addEventListener('click', function () {
            var letras = {};
            propuesta.forEach(function (x) { letras[x.e.letra] = true; });
            guardar(tagE, Object.keys(letras).sort().join(''));
          });
          acc.appendChild(auto);
        }
      }
      t.appendChild(acc);

      // Todas las letras
      var det = crear('details');
      det.appendChild(crear('summary', '', tr('Elegir por letra (') + (config[tagE].split('').join(' ') || tr('ninguna')) + ')'));
      var letras = crear('div', 'letras');
      LETRAS.forEach(function (l) { letras.appendChild(botonLetra(b.f, tagE, l, 'est-' + f + '-' + l)); });
      det.appendChild(letras);
      var acc2 = crear('div', 'acciones');
      var tod = crear('button', 'boton pequeno', tr('Todas')); tod.type = 'button';
      tod.addEventListener('click', function () { guardar(tagE, LETRAS.join('')); });
      var nin = crear('button', 'boton pequeno', tr('Ninguna')); nin.type = 'button';
      nin.addEventListener('click', function () { guardar(tagE, ''); });
      acc2.appendChild(tod); acc2.appendChild(nin);
      det.appendChild(acc2);
      t.appendChild(det);

      var dt = crear('details');
      dt.appendChild(crear('summary', '', tr('Temas')));
      var temas = crear('div', 'temas');
      TEMAS_AJUSTE.forEach(function (l) {
        var fijo = OBLIGATORIOS.indexOf(l) >= 0;
        var lab = crear('label');
        var c = crear('input');
        c.type = 'checkbox';
        c.id = 'tema-' + f + '-' + l;
        c.checked = fijo || config[tagT].indexOf(l) >= 0;
        c.disabled = fijo;
        c.addEventListener('change', function () { guardar(tagT, conLetra(config[tagT], l, c.checked)); });
        lab.appendChild(c);
        lab.appendChild(crear('span', 'l', l));
        lab.appendChild(crear('span', '', (TEMAS[l] || '') + (fijo ? tr(' · siempre') : '')));
        temas.appendChild(lab);
      });
      dt.appendChild(temas);
      t.appendChild(dt);
      raiz.appendChild(t);
    });

    $('fuente-estaciones').textContent = fuentes.length
      ? tr('Datos de las estaciones: ') + fuentes.map(function (x) { return tr(x.titulo) + ' (' + tr(x.nombre) + ', ' + x.fecha + ')'; }).join('; ')
        + tr('. Pueden haber cambiado: compruébelos con las publicaciones oficiales antes de fiarse de ellos.')
      : '';
  }

  // Estaciones seguidas con su próxima emisión: de cada letra, la más cercana
  function seguidasConHora(ahora) {
    var salida = [];
    BANDAS.forEach(function (b) {
      var vistas = {};
      conDistancia(b.f).forEach(function (x) {
        var e = x.e;
        if (config['stations' + b.f].indexOf(e.letra) < 0 || vistas[e.letra] || e.estado !== 'activa') return;
        if (posicion() === null) return;        // sin posición no se sabe cuál de las que usan la letra
        vistas[e.letra] = true;
        var px = proxima(e, ahora);
        if (px !== null) salida.push({ e: e, t: px, d: x.d });
      });
    });
    return salida.sort(function (a, b) { return a.t - b.t; });
  }
  function pintarProximas() {
    var ol = $('proximas');
    var ahora = Date.now();
    ol.textContent = '';
    if (posicion() === null) {
      ol.appendChild(crear('li', 'nota', tr('Indique su posición para ver el horario de las estaciones que sigue.')));
      return;
    }
    var lista = seguidasConHora(ahora).slice(0, 8);
    if (!lista.length) { ol.appendChild(crear('li', 'nota', tr('No sigue ninguna estación con horario conocido.'))); return; }
    lista.forEach(function (x) {
      var li = crear('li');
      li.appendChild(crear('span', 'hora', horaUtc(x.t)));
      li.appendChild(document.createTextNode(x.e.nombre + ' (' + x.e.letra + ') · ' + nombreFreq(x.e.khz) + tr(' · en {n} min').replace('{n}', Math.max(1, Math.round((x.t - ahora) / 60000))) + (llevaMeteo(x.e, x.t) ? tr(' · boletín meteorológico') : '')));
      ol.appendChild(li);
    });
  }
  function pintarGps() {
    var v = $('gps'), x = $('gps-detalle');
    if (!v) return;
    var p = posicionGps();
    if (!gps) { v.textContent = '—'; x.textContent = ''; return; }
    if (!gps.enabled) { v.textContent = tr('sin GPS'); x.textContent = tr('no hay gpsd configurado'); return; }
    if (!gps.connected) { v.textContent = tr('sin GPS'); x.textContent = tr('gpsd no contesta'); return; }
    var sat = typeof gps.used === 'number' ? tr('{a} de {b} satélites').replace('{a}', gps.used).replace('{b}', gps.visible) : '';
    if (!p) { v.textContent = tr('sin posición'); x.textContent = tr('buscando satélites') + (sat ? ' · ' + sat : ''); return; }
    v.textContent = enCarta(p);
    x.textContent = localizador(p) + ' · ' + (gps.fix >= 3 ? '3D' : '2D') + (sat ? ' · ' + sat : '')
      + (typeof gps.alt === 'number' ? ' · ' + Math.round(gps.alt) + ' m' : '');
  }
  function pintarPosicion() {
    var p = posicion(), g = posicionGps(), m = posicionManual();
    $('usar-gps').checked = usaGps();
    if (p) { window.NAVTEX_TEMA.ponerPosicion(p.lat, p.lon); repintarAspecto(); }
    var texto;
    if (usaGps() && g) texto = tr('En uso: la del GPS, ') + enCarta(g) + ' (' + localizador(g) + ').';
    else if (usaGps() && m) texto = tr('El GPS no da posición ahora: se usa la escrita, ') + m.lat.toFixed(3) + ', ' + m.lon.toFixed(3) + ' (' + localizador(m) + ').';
    else if (usaGps()) texto = tr('El GPS no da posición ahora y no hay ninguna escrita.');
    else if (m) texto = tr('En uso: la escrita, ') + m.lat.toFixed(3) + ', ' + m.lon.toFixed(3) + ' (' + localizador(m) + ').';
    else texto = tr('Sin posición.');
    $('posicion-actual').textContent = texto;
    posPintada = p;
  }
  // El GPS se mueve: las listas solo se rehacen si la posición cambia de verdad
  function posicionCambio() {
    var p = posicion();
    if ((p === null) !== (posPintada === null)) return true;
    return p !== null && distanciaMn(p, posPintada) > 1;
  }
  function leerGps() {
    return pedir('/api/gps').then(function (j) {
      gps = j && typeof j === 'object' ? j : null;
    }).catch(function () { gps = null; }).then(function () {
      pintarGps();
      if (posicionCambio()) { pintarPosicion(); pintarAjustes(); pintarProximas(); }
    });
  }

  // ------------------------------------------------------------ receptores
  function ponerAjuste(clave, valor) {
    ajustes[clave] = valor;
    return enviar('/api/settings/' + clave + '/' + encodeURIComponent(valor)).then(function () {
      avisar('estado-receptores', true, tr('Guardado. El receptor lo aplica en unos segundos.'));
    }).catch(function (e) {
      avisar('estado-receptores', false, tr('No se pudo guardar: ') + e.message + '.');
      return leerAjustes();
    });
  }
  function campo(etiqueta, control) {
    var l = crear('label', 'ajuste');
    l.appendChild(crear('span', '', etiqueta));
    l.appendChild(control);
    return l;
  }
  function selector(id, opciones, valor, alCambiar) {
    var s = crear('select');
    s.id = id;
    opciones.forEach(function (o) { var op = crear('option', '', o[1]); op.value = o[0]; s.appendChild(op); });
    s.value = valor;
    s.addEventListener('change', function () { alCambiar(s.value); });
    return s;
  }
  function texto(id, valor, marcador, alCambiar) {
    var i = crear('input');
    i.type = 'text'; i.id = id; i.value = valor; i.placeholder = marcador; i.autocomplete = 'off'; i.spellcheck = false;
    i.addEventListener('change', function () { alCambiar(i.value.trim()); });
    return i;
  }
  function pintarReceptores() {
    var raiz = $('receptores');
    raiz.textContent = '';
    [1, 2].forEach(function (n) {
      var p = 'rx' + n + '.';
      var v = function (k, defecto) { return ajustes[p + k] !== undefined && ajustes[p + k] !== '' ? ajustes[p + k] : defecto; };
      var equipo = v('source', 'sdrplay');
      var banda = v('band', 'mf');
      var t = crear('div', 'tarjeta');
      t.id = 'receptor-' + n;
      t.appendChild(crear('h2', '', tr('Receptor ') + n));
      var est = crear('p', 'nota estado-rx'); est.id = 'estado-rx' + n; t.appendChild(est);

      var enc = crear('label', 'interruptor');
      var c = crear('input'); c.type = 'checkbox'; c.id = 'rx' + n + '-enabled'; c.checked = v('enabled', '0') === '1';
      c.addEventListener('change', function () { ponerAjuste(p + 'enabled', c.checked ? '1' : '0'); });
      enc.appendChild(c); enc.appendChild(document.createTextNode(tr('Encendido')));
      t.appendChild(enc);

      var r1 = crear('div', 'rejilla');
      r1.appendChild(campo(tr('Equipo'), selector('rx' + n + '-source', [['sdrplay', 'SDRplay'], ['rtlsdr', 'RTL-SDR']], equipo,
        function (x) { ponerAjuste(p + 'source', x).then(pintarReceptores); })));
      r1.appendChild(campo(tr('Banda'), selector('rx' + n + '-band', [['mf', tr('518 y 490 kHz')], ['hf', tr('4209,5 kHz (onda corta)')], ['auto', tr('Por horario: las dos')]], banda,
        function (x) { ponerAjuste(p + 'band', x).then(pintarReceptores); })));
      t.appendChild(r1);

      if (banda === 'auto') {
        var hs = texto('rx' + n + '-slots', v('hf_slots', ''), '0200,0600,1000', function (x) {
          var limpio = x.replace(/[^0-9,]/g, '').split(',').filter(function (h) { return /^([01]\d|2[0-3])[0-5]\d$/.test(h); }).slice(0, 36).join(',');
          ponerAjuste(p + 'hf_slots', limpio).then(pintarReceptores);
        });
        t.appendChild(campo(tr('Horas UTC en onda corta (HHMM, separadas por comas)'), hs));
        var tomar = crear('button', 'boton pequeno', tr('Tomar las horas de las estaciones de onda corta que sigo'));
        tomar.type = 'button'; tomar.id = 'rx' + n + '-tomar';
        tomar.addEventListener('click', function () {
          var horas = {};
          conDistancia(4209).forEach(function (x) {
            if (config.stations4209.indexOf(x.e.letra) >= 0 && x.e.estado === 'activa') x.e.horas.forEach(function (h) { horas[h] = true; });
          });
          ponerAjuste(p + 'hf_slots', Object.keys(horas).sort().slice(0, 36).join(',')).then(pintarReceptores);
        });
        t.appendChild(tomar);
        t.appendChild(crear('p', 'nota', tr('Escucha 518 y 490 kHz y pasa a 4209,5 kHz en cada hora indicada, desde un minuto antes hasta once después. Mientras está en onda corta no recibe las otras dos.')));
      }

      var r2 = crear('div', 'rejilla');
      if (equipo === 'sdrplay') {
        r2.appendChild(campo(tr('Toma de antena (RSPdx)'), selector('rx' + n + '-port', [['A', 'A'], ['B', 'B'], ['C', 'C']], v('port', 'A'),
          function (x) { ponerAjuste(p + 'port', x); })));
      } else {
        r2.appendChild(campo(tr('Número de serie (si hay varios)'), texto('rx' + n + '-serial', v('serial', ''), tr('cualquiera'),
          function (x) { ponerAjuste(p + 'serial', x.replace(/[^A-Za-z0-9_-]/g, '').slice(0, 32)).then(pintarReceptores); })));
        r2.appendChild(campo(tr('Muestreo directo'), selector('rx' + n + '-direct', [['auto', tr('Automático, según el modelo')], ['2', tr('Rama Q')], ['1', tr('Rama I')], ['0', tr('Apagado (conversor)')]], v('direct', 'auto'),
          function (x) { ponerAjuste(p + 'direct', x); })));
        r2.appendChild(campo(tr('Conversor externo (Hz; 0 = ninguno)'), texto('rx' + n + '-upconv', v('upconv', '0'), '0',
          function (x) { ponerAjuste(p + 'upconv', x.replace(/\D/g, '').slice(0, 10) || '0').then(pintarReceptores); })));
        r2.appendChild(campo(tr('Ganancia (dB, o auto)'), texto('rx' + n + '-gain', v('gain', 'auto'), 'auto',
          function (x) { ponerAjuste(p + 'gain', /^\d{1,2}(\.\d)?$/.test(x) ? x : 'auto').then(pintarReceptores); })));
        r2.appendChild(campo(tr('Corrección (ppm)'), texto('rx' + n + '-ppm', v('ppm', '0'), '0',
          function (x) { ponerAjuste(p + 'ppm', /^-?\d{1,3}$/.test(x) ? x : '0').then(pintarReceptores); })));
      }
      t.appendChild(r2);

      var bt = crear('label', 'casilla');
      var cb = crear('input'); cb.type = 'checkbox'; cb.id = 'rx' + n + '-bias'; cb.checked = v('bias', '0') === '1';
      cb.addEventListener('change', function () { ponerAjuste(p + 'bias', cb.checked ? '1' : '0'); });
      bt.appendChild(cb); bt.appendChild(document.createTextNode(tr('Alimentar la antena por el coaxial') + (equipo === 'sdrplay' ? tr(' (RSPdx: solo en la toma B)') : tr(' (si el equipo lo permite)'))));
      t.appendChild(bt);
      raiz.appendChild(t);
    });
    if (estado) pintarEstado();
    pintarSalida();
  }

  // ---------------------------------------------------------------- lectura
  // --------------------------------- alarma de búsqueda y salvamento (D)
  // Solo se apaga a mano. Suena si el navegador lo permite (hace falta haber tocado la página alguna vez).
  var sonido = null, pitido = null;
  function sonar(si) {
    if (!si) { if (pitido) { clearInterval(pitido); pitido = null; } return; }
    if (pitido) return;
    var un = function () {
      try {
        var C = window.AudioContext || window.webkitAudioContext;
        if (!C) return;
        sonido = sonido || new C();
        if (sonido.state === 'suspended') sonido.resume();
        var o = sonido.createOscillator(), g = sonido.createGain();
        o.frequency.value = 880; g.gain.value = 0.15;
        o.connect(g); g.connect(sonido.destination);
        o.start(); o.stop(sonido.currentTime + 0.35);
      } catch (e) { /* sin sonido: queda el aviso en pantalla */ }
    };
    un();
    pitido = setInterval(un, 1500);
  }
  function pintarAlarma() {
    var caja = $('alarma');
    var con = mensajes.filter(function (m) { return m.alarm; });
    caja.hidden = con.length === 0;
    sonar(con.length > 0);
    if (!con.length) return;
    $('alarma-texto').textContent = (con.length === 1 ? tr('Mensaje de búsqueda y salvamento sin reconocer: ') : con.length + tr(' mensajes de búsqueda y salvamento sin reconocer: '))
      + con.map(function (m) { return m.bbbb + ' (' + nombreFreq(m.freq) + ', ' + fechaUtc(m.recibido) + ')'; }).join(', ') + '.';
  }

  // ------------------------------------------ conservación y salida NMEA
  function pintarSalida() {
    var raiz = $('salida');
    if (!raiz) return;
    raiz.textContent = '';
    var v = function (k, defecto) { return ajustes[k] !== undefined && ajustes[k] !== '' ? ajustes[k] : defecto; };
    var r = crear('div', 'rejilla');
    r.appendChild(campo(tr('Horas que se conserva un mensaje desde la última vez que se oye (72 a 8760)'), texto('retencion', v('retention_hours', '72'), '72',
      function (x) { var n = Math.round(Number(x)); ponerAjuste('retention_hours', String(isFinite(n) ? Math.min(8760, Math.max(72, n)) : 72)).then(pintarSalida); })));
    r.appendChild(campo(tr('NMEA 0183 por UDP: destino (equipo:puerto; vacío, apagado)'), texto('nrx-udp', v('nrx.udp', ''), '192.168.1.255:10110',
      function (x) { ponerAjuste('nrx.udp', /^[A-Za-z0-9.-]{1,70}:\d{1,5}$/.test(x) ? x : '').then(pintarSalida); })));
    r.appendChild(campo(tr('NMEA 0183 por TCP: puerto del servidor (0, apagado)'), texto('nrx-tcp', v('nrx.tcp', '0'), '10110',
      function (x) { var n = Math.round(Number(x)); ponerAjuste('nrx.tcp', String(isFinite(n) && n >= 1024 && n <= 65535 ? n : 0)).then(pintarSalida); })));
    r.appendChild(campo(tr('Gestión de alertas de puente (sentencias ALF, ALC y HBT; acepta ACN)'), selector('nrx-bam', [['off', tr('Apagada (solo ALR y ACK)')], ['on', tr('Encendida')]], v('nrx.bam', 'off'),
      function (x) { ponerAjuste('nrx.bam', x); })));
    r.appendChild(campo(tr('Forma de la sentencia NRX'), selector('nrx-format', [['furuno', tr('Furuno (la de la app y el plotter APRS LoRa+)')], ['iec', tr('IEC 61097-6 (la de la norma)')]], v('nrx.format', 'furuno'),
      function (x) { ponerAjuste('nrx.format', x); })));
    raiz.appendChild(r);
    var est = crear('p', 'nota'); est.id = 'estado-nrx'; raiz.appendChild(est);
    pedir('/api/nrx').then(function (j) {
      var partes = [];
      if (j.udp) partes.push(tr('UDP a ') + j.udp);
      if (j.tcp) partes.push(tr('servidor TCP en el puerto ') + j.tcp + ' (' + j.clients + (j.clients === 1 ? tr(' programa conectado') : tr(' programas conectados')) + ')');
      est.textContent = (partes.length ? tr('Salida NMEA: ') + partes.join(tr(' y ')) + tr('. Mensajes enviados desde que arrancó: ') + j.sent + '.' : tr('Salida NMEA apagada.'))
        + (j.error ? tr(' Último fallo: ') + j.error + '.' : '');
    }).catch(function () { est.textContent = ''; });
  }

  function leerConfig() {
    return pedir('/config').then(function (filas) {
      (Array.isArray(filas) ? filas : []).forEach(function (f) {
        if (f && Object.prototype.hasOwnProperty.call(config, f.tag)) config[f.tag] = String(f.value || '').toUpperCase().replace(/[^A-Z]/g, '');
      });
      configLeida = true;
      pintarAjustes();
      pintarProximas();
    });
  }
  function leerAjustes() {
    return pedir('/api/settings').then(function (j) {
      ajustes = j && typeof j === 'object' ? j : {};
      pintarPosicion();
      pintarAjustes();
      pintarProximas();
      pintarReceptores();
    });
  }
  function leerEstaciones() {
    return pedir('estaciones.json').then(function (j) {
      estaciones = Array.isArray(j.estaciones) ? j.estaciones : [];
      fuentes = Array.isArray(j.fuentes) ? j.fuentes : [];
      pintarAjustes();
      pintarProximas();
    });
  }
  function leerTodo() {
    var error = $('error-general');
    return Promise.all([
      pedir('/api/status'),
      pedir('/api/messages?since=0&limit=500')
    ]).then(function (r) {
      estado = r[0] || {};
      mensajes = (Array.isArray(r[1]) ? r[1] : []).filter(function (m) {
        return m && typeof m.message === 'string' && typeof m.bbbb === 'string';
      }).map(function (m) {
        var t = Date.parse(m.received || '');
        m.recibido = isFinite(t) ? t : Date.now();
        return m;
      }).sort(function (a, b) { return b.recibido - a.recibido || b.id - a.id; });
      error.hidden = true;
      pintarEstado();
      pintarTemas();
      pintarLista();
      pintarAlarma();
    }).catch(function (e) {
      error.hidden = false;
      error.textContent = tr('No se puede consultar el receptor: ') + e.message + '.';
    });
  }

  // ----------------------------------------------------------------- inicio
  Array.prototype.forEach.call(document.querySelectorAll('.pestana'), function (p) {
    p.addEventListener('click', function () {
      Array.prototype.forEach.call(document.querySelectorAll('.pestana'), function (q) { q.classList.toggle('activa', q === p); });
      ['mensajes', 'estaciones', 'receptores', 'acerca'].forEach(function (v) { $('vista-' + v).hidden = v !== p.getAttribute('data-vista'); });
      $('estado-ajustes').hidden = true;
      $('estado-receptores').hidden = true;
    });
  });
  Array.prototype.forEach.call(document.querySelectorAll('.chip[data-freq]'), function (c) {
    c.addEventListener('click', function () {
      filtro.freq = Number(c.getAttribute('data-freq'));
      Array.prototype.forEach.call(document.querySelectorAll('.chip[data-freq]'), function (q) { q.classList.toggle('activa', q === c); });
      pintarLista();
    });
  });
  $('filtro-tema').addEventListener('change', function (e) { filtro.tema = e.target.value; pintarLista(); });
  $('filtro-texto').addEventListener('input', function (e) { filtro.texto = e.target.value; pintarLista(); });
  $('filtro-todo').addEventListener('change', function (e) { filtro.todo = e.target.checked; pintarLista(); });
  $('reconocer').addEventListener('click', function () {
    enviar('/api/alarm/ack').then(leerTodo).catch(function (e) {
      $('alarma-texto').textContent = tr('No se pudo reconocer la alarma: ') + e.message + '.';
    });
  });

  // Aspecto: botón con menú, como en la WebUI del tracker APRS LoRa+. Los iconos son de Bootstrap Icons (MIT).
  (function () {
    var SVG = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16" aria-hidden="true">';
    var LUNA = '<path d="M6 .278a.77.77 0 0 1 .08.858 7.2 7.2 0 0 0-.878 3.46c0 4.021 3.278 7.277 7.318 7.277q.792-.001 1.533-.16a.79.79 0 0 1 .81.316.73.73 0 0 1-.031.893A8.35 8.35 0 0 1 8.344 16C3.734 16 0 12.286 0 7.71 0 4.266 2.114 1.312 5.124.06A.75.75 0 0 1 6 .278"/>';
    var ICONOS = {
      sistema: SVG + '<path d="M0 4s0-2 2-2h12s2 0 2 2v6s0 2-2 2h-4q0 1 .25 1.5H11a.5.5 0 0 1 0 1H5a.5.5 0 0 1 0-1h.75Q6 13 6 12H2s-2 0-2-2zm1.398-.855a.76.76 0 0 0-.254.302A1.5 1.5 0 0 0 1 4.01V10c0 .325.078.502.145.602q.105.156.302.254a1.5 1.5 0 0 0 .538.143L2.01 11H14c.325 0 .502-.078.602-.145a.76.76 0 0 0 .254-.302 1.5 1.5 0 0 0 .143-.538L15 9.99V4c0-.325-.078-.502-.145-.602a.76.76 0 0 0-.302-.254A1.5 1.5 0 0 0 13.99 3H2c-.325 0-.502.078-.602.145"/></svg>',
      auto: SVG + '<path d="M8 15A7 7 0 1 0 8 1zm0 1A8 8 0 1 1 8 0a8 8 0 0 1 0 16"/></svg>',
      claro: SVG + '<path d="M8 11a3 3 0 1 1 0-6 3 3 0 0 1 0 6m0 1a4 4 0 1 0 0-8 4 4 0 0 0 0 8M8 0a.5.5 0 0 1 .5.5v2a.5.5 0 0 1-1 0v-2A.5.5 0 0 1 8 0m0 13a.5.5 0 0 1 .5.5v2a.5.5 0 0 1-1 0v-2A.5.5 0 0 1 8 13m8-5a.5.5 0 0 1-.5.5h-2a.5.5 0 0 1 0-1h2a.5.5 0 0 1 .5.5M3 8a.5.5 0 0 1-.5.5h-2a.5.5 0 0 1 0-1h2A.5.5 0 0 1 3 8m10.657-5.657a.5.5 0 0 1 0 .707l-1.414 1.415a.5.5 0 1 1-.707-.708l1.414-1.414a.5.5 0 0 1 .707 0m-9.193 9.193a.5.5 0 0 1 0 .707L3.05 13.657a.5.5 0 0 1-.707-.707l1.414-1.414a.5.5 0 0 1 .707 0m9.193 2.121a.5.5 0 0 1-.707 0l-1.414-1.414a.5.5 0 0 1 .707-.707l1.414 1.414a.5.5 0 0 1 0 .707M4.464 4.465a.5.5 0 0 1-.707 0L2.343 3.05a.5.5 0 1 1 .707-.707l1.414 1.414a.5.5 0 0 1 0 .708"/></svg>',
      oscuro: SVG + LUNA + '</svg>',
      noche: SVG + LUNA + '<path d="M10.794 3.148a.217.217 0 0 1 .412 0l.387 1.162c.173.518.579.924 1.097 1.097l1.162.387a.217.217 0 0 1 0 .412l-1.162.387a1.73 1.73 0 0 0-1.097 1.097l-.387 1.162a.217.217 0 0 1-.412 0l-.387-1.162A1.73 1.73 0 0 0 9.31 6.593l-1.162-.387a.217.217 0 0 1 0-.412l1.162-.387a1.73 1.73 0 0 0 1.097-1.097z"/></svg>'
    };
    var NOMBRES = { sistema: tr('Sistema'), auto: tr('Automático'), claro: tr('Claro'), oscuro: tr('Oscuro'), noche: tr('Noche') };
    var T = window.NAVTEX_TEMA, boton = $('tema-boton'), menu = $('tema-menu');
    function cerrar() { menu.classList.remove('abierto'); boton.setAttribute('aria-expanded', 'false'); }
    function pintar() {
      var m = T.modo(), t = T.aplicar();
      // los iconos son constantes de este archivo, no datos recibidos
      boton.innerHTML = ICONOS[m];
      boton.title = tr('Aspecto: ') + NOMBRES[m] + (m === 'sistema' || m === 'auto' ? tr(' (ahora ') + NOMBRES[t].toLowerCase() + ')' : '');
      boton.setAttribute('aria-label', boton.title);
      Array.prototype.forEach.call(menu.querySelectorAll('button[data-modo]'), function (b) {
        var si = b.getAttribute('data-modo') === m;
        b.classList.toggle('elegido', si);
        b.setAttribute('aria-checked', si ? 'true' : 'false');
        b.querySelector('.ti').innerHTML = ICONOS[b.getAttribute('data-modo')];
      });
      $('tema-nota-auto').textContent = T.posicion() ? tr('claro de día y oscuro de noche (el sol en la posición del receptor)') : tr('claro de 8 a 20 h y oscuro el resto (aún sin posición)');
    }
    pintar();
    boton.addEventListener('click', function (e) {
      e.stopPropagation();
      boton.setAttribute('aria-expanded', menu.classList.toggle('abierto') ? 'true' : 'false');
    });
    menu.addEventListener('click', function (e) {
      var b = e.target.closest('button[data-modo]');
      if (!b) return;
      T.poner(b.getAttribute('data-modo'));
      pintar();
      cerrar();
    });
    document.addEventListener('click', function (e) { if (!menu.contains(e.target)) cerrar(); });
    document.addEventListener('keydown', function (e) { if (e.key === 'Escape') cerrar(); });
    if (window.matchMedia) {
      var mq = window.matchMedia('(prefers-color-scheme: dark)');
      var alCambiar = function () { if (T.modo() === 'sistema') pintar(); };
      if (mq.addEventListener) mq.addEventListener('change', alCambiar); else if (mq.addListener) mq.addListener(alCambiar);
    }
    setInterval(function () { if (T.modo() === 'auto') pintar(); }, 60000);
    repintarAspecto = pintar;
  })();

  // Idioma: el botón enseña el otro idioma y cambia a él
  (function () {
    var b = $('idioma-boton'), otro = I18N.idioma === 'en' ? 'es' : 'en';
    b.textContent = otro.toUpperCase();
    b.title = otro === 'en' ? 'English' : 'Español';
    b.setAttribute('aria-label', b.title);
    b.setAttribute('lang', otro);
    b.addEventListener('click', function () { I18N.poner(otro); });
  })();

  $('form-posicion').addEventListener('submit', function (ev) {
    ev.preventDefault();
    var p = leerPosicion($('posicion').value);
    if (!p) { avisar('estado-ajustes', false, tr('No entiendo esa posición. Ejemplos: 36.53, -6.29 o IM66uq.')); return; }
    var valor = p.lat.toFixed(4) + ',' + p.lon.toFixed(4);
    ajustes.position = valor;
    enviar('/api/settings/position/' + valor).then(function () {
      $('posicion').value = '';
      avisar('estado-ajustes', true, tr('Posición guardada.'));
      pintarPosicion(); pintarAjustes(); pintarProximas(); pintarLista();
    }).catch(function (e) { avisar('estado-ajustes', false, tr('No se pudo guardar: ') + e.message + '.'); });
  });
  $('quitar-posicion').addEventListener('click', function () {
    ajustes.position = '';
    enviar('/api/settings/position/').then(function () { pintarPosicion(); pintarAjustes(); pintarProximas(); pintarLista(); })
      .catch(function (e) { avisar('estado-ajustes', false, tr('No se pudo guardar: ') + e.message + '.'); });
  });

  $('usar-gps').addEventListener('change', function () {
    ajustes.position_source = $('usar-gps').checked ? 'gps' : 'manual';
    enviar('/api/settings/position_source/' + ajustes.position_source).then(function () {
      pintarPosicion(); pintarAjustes(); pintarProximas(); pintarLista();
    }).catch(function (e) { avisar('estado-ajustes', false, tr('No se pudo guardar: ') + e.message + '.'); });
  });

  montarEstado();
  pintarAjustes();
  pintarProximas();
  pintarReceptores();
  leerEstaciones().catch(function () { /* sin base de estaciones: quedan las letras */ });
  leerAjustes().catch(function () { /* sin ajustes: valores de serie */ }).then(leerGps);
  setInterval(leerGps, 10000);
  leerConfig().catch(function () { /* sin filtros se muestra todo */ }).then(leerTodo);
  setInterval(leerTodo, 60000);
  setInterval(pintarEstado, 1000);
  setInterval(pintarProximas, 60000);
})();
