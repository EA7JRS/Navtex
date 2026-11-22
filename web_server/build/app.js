/*
 * Navtex · interfaz web (fork EA7JRS/Navtex)
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Manuel - EA7JRS
 *
 * Todo lo que llega por radio se escribe con textContent: nunca se interpreta
 * como HTML.
 */
(function () {
  'use strict';

  // Temas (segunda letra de la cabecera, B2)
  var TEMAS = {
    A: 'Aviso a los navegantes', B: 'Aviso meteorológico', C: 'Hielos', D: 'Búsqueda y salvamento',
    E: 'Previsión meteorológica', F: 'Practicaje', G: 'AIS', H: 'LORAN', I: 'Reservado', J: 'Navegación por satélite',
    K: 'Otras ayudas electrónicas', L: 'Aviso a los navegantes (adicional)', T: 'Prueba', V: 'Servicio especial',
    W: 'Servicio especial', X: 'Servicio especial', Y: 'Servicio especial', Z: 'Sin mensajes'
  };
  var GRAVEDAD = { D: 'rojo', B: 'naranja', A: 'amarillo', L: 'amarillo' };
  var OBLIGATORIOS = 'ABDL';
  var LETRAS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'.split('');
  var TEMAS_AJUSTE = 'ABCDEFGHJKLVWXYZ'.split('');
  var MESES = ['ene', 'feb', 'mar', 'abr', 'may', 'jun', 'jul', 'ago', 'sep', 'oct', 'nov', 'dic'];

  var mensajes = [];            // todos los recibidos, del más reciente al más antiguo
  var config = { stations518: '', messages518: '', stations490: '', messages490: '' };
  var configLeida = false;
  var filtro = { freq: 0, tema: '', texto: '', todo: false };
  var abiertos = {};            // id -> true
  var estado = null;

  function $(id) { return document.getElementById(id); }
  function crear(etiqueta, clase, texto) {
    var e = document.createElement(etiqueta);
    if (clase) e.className = clase;
    if (texto !== undefined) e.textContent = texto;
    return e;
  }
  function dos(n) { return (n < 10 ? '0' : '') + n; }
  function pedir(ruta) {
    return fetch(ruta, { cache: 'no-store' }).then(function (r) {
      if (!r.ok) throw new Error('el servidor respondió ' + r.status);
      return r.json();
    });
  }

  // "hace 5 min", "hace 3 h", o la fecha
  function haceCuanto(ms, ahora) {
    var s = Math.round((ahora - ms) / 1000);
    if (!isFinite(s)) return '—';
    if (s < 90) return 'ahora mismo';
    if (s < 3600) return 'hace ' + Math.round(s / 60) + ' min';
    if (s < 36 * 3600) return 'hace ' + Math.round(s / 3600) + ' h';
    return 'hace ' + Math.round(s / 86400) + ' días';
  }
  function fechaUtc(ms) {
    var d = new Date(ms);
    return d.getUTCDate() + ' ' + MESES[d.getUTCMonth()] + ' ' + dos(d.getUTCHours()) + ':' + dos(d.getUTCMinutes()) + ' UTC';
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

  function seSigue(m) {
    var est = m.freq === 490 ? config.stations490 : config.stations518;
    var tem = m.freq === 490 ? config.messages490 : config.messages518;
    return est.indexOf(m.station) >= 0 && (OBLIGATORIOS.indexOf(m.subject) >= 0 || tem.indexOf(m.subject) >= 0);
  }

  // ---------------------------------------------------------------- estado
  function pintarEstado() {
    var ahora = Date.now();
    var d = new Date(ahora);
    $('hora-utc').textContent = dos(d.getUTCHours()) + ':' + dos(d.getUTCMinutes()) + ':' + dos(d.getUTCSeconds());
    if (!estado) return;
    var vivo = Date.parse(estado.receiver_alive || '');
    var recibiendo = isFinite(vivo) && ahora - vivo < 3 * 60000;
    var origen = estado.source === 'rtlsdr' ? 'RTL-SDR' : estado.source === 'sdrplay' ? 'SDRplay' : estado.source === 'file' ? 'grabación' : (estado.source || '');
    $('punto-receptor').className = 'punto ' + (recibiendo ? 'ok' : 'mal');
    $('texto-receptor').textContent = recibiendo ? 'Recibiendo' : 'Parado';
    $('detalle-receptor').textContent = recibiendo
      ? (origen || 'receptor en marcha')
      : (isFinite(vivo) ? 'últimas muestras ' + haceCuanto(vivo, ahora) + (origen ? ' · ' + origen : '') : 'el receptor aún no ha arrancado');
    ['518', '490'].forEach(function (f) {
      var t = Date.parse(estado['phasing_' + f] || '');
      $('sinc-' + f).textContent = isFinite(t) ? haceCuanto(t, ahora) : 'todavía ninguno';
    });
    $('total-mensajes').textContent = (estado.messages || 0) + (estado.messages === 1 ? ' mensaje guardado' : ' mensajes guardados');
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
      var o = crear('option', '', l + ' · ' + (TEMAS[l] || 'Tema ' + l));
      o.value = l;
      sel.appendChild(o);
    });
    sel.value = letras.indexOf(actual) >= 0 ? actual : '';
    filtro.tema = sel.value;
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
      var titulo = crear('span', 'titulo', TEMAS[m.subject] || 'Tema ' + m.subject);
      if (m.unread) titulo.appendChild(crear('span', 'marca-nuevo', 'nuevo'));
      boton.appendChild(titulo);
      boton.appendChild(crear('span', 'flecha', '›'));
      var meta = crear('span', 'meta');
      var linea = primeraLinea(m.message);
      if (linea) meta.appendChild(crear('span', 'resumen-linea', linea));
      meta.appendChild(document.createTextNode('estación ' + m.station + ' · ' + m.freq + ' kHz · ' + haceCuanto(m.recibido, ahora) + ' (' + fechaUtc(m.recibido) + ')'));
      if (!m.complete) { meta.appendChild(document.createTextNode(' · ')); meta.appendChild(crear('span', 'cortado', 'cortado')); }
      boton.appendChild(meta);
      boton.addEventListener('click', function () { alternar(m); });
      li.appendChild(boton);
      if (abiertos[m.id]) {
        var cuerpo = crear('div', 'cuerpo');
        cuerpo.appendChild(crear('pre', '', m.message));
        cuerpo.appendChild(crear('p', 'nota', 'Recibido el ' + fechaUtc(m.recibido) + ' (' + horaLocal(m.recibido) + ' hora local) en ' + m.freq + ' kHz.'
          + (m.complete ? '' : ' La recepción se cortó antes del final del mensaje.')
          + (m.message.indexOf('*') >= 0 ? ' Cada asterisco es un carácter que no se pudo recibir bien.' : '')));
        li.appendChild(cuerpo);
      }
      lista.appendChild(li);
    });

    var ocultos = mensajes.length - (filtro.todo || !configLeida ? mensajes.length : mensajes.filter(seSigue).length);
    $('resumen').textContent = mensajes.length === 0 ? ''
      : ver.length + (ver.length === 1 ? ' mensaje' : ' mensajes')
        + (ocultos > 0 ? ' · ' + ocultos + (ocultos === 1 ? ' más de estaciones o temas que no sigue' : ' más de estaciones o temas que no sigue') : '');
    var vacio = $('vacio');
    vacio.hidden = ver.length > 0;
    vacio.textContent = mensajes.length === 0
      ? 'Todavía no se ha recibido ningún mensaje. Cada estación emite cada cuatro horas, en su turno de diez minutos.'
      : 'Ningún mensaje con estos filtros.';
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

  // --------------------------------------------------------------- ajustes
  function guardar(tag, valor) {
    var aviso = $('estado-ajustes');
    config[tag] = valor;
    pintarAjustes();
    pintarLista();
    fetch('/saveconfig/' + tag + '/' + valor, { cache: 'no-store' }).then(function (r) {
      if (!r.ok) throw new Error('el servidor respondió ' + r.status);
      aviso.hidden = false; aviso.className = 'aviso bien'; aviso.textContent = 'Guardado.';
    }).catch(function (e) {
      aviso.hidden = false; aviso.className = 'aviso'; aviso.textContent = 'No se pudo guardar: ' + e.message + '.';
      leerConfig();
    });
  }
  function conLetra(valor, letra, poner) {
    var s = valor.split('').filter(function (c) { return c !== letra; });
    if (poner) s.push(letra);
    return s.sort().join('');
  }

  function pintarAjustes() {
    var raiz = $('ajustes');
    raiz.textContent = '';
    [{ f: '518', nombre: '518 kHz · internacional, en inglés' }, { f: '490', nombre: '490 kHz · nacional, en el idioma del país' }].forEach(function (b) {
      var tagE = 'stations' + b.f, tagT = 'messages' + b.f;
      var t = crear('div', 'tarjeta');
      t.id = 'ajustes-' + b.f;
      t.appendChild(crear('h2', '', b.nombre));
      t.appendChild(crear('h3', '', 'Estaciones'));
      var letras = crear('div', 'letras');
      LETRAS.forEach(function (l) {
        var activa = config[tagE].indexOf(l) >= 0;
        var bt = crear('button', 'letra' + (activa ? ' activa' : ''), l);
        bt.type = 'button';
        bt.id = 'est-' + b.f + '-' + l;
        bt.setAttribute('aria-pressed', activa ? 'true' : 'false');
        bt.setAttribute('aria-label', 'Estación ' + l);
        bt.addEventListener('click', function () { guardar(tagE, conLetra(config[tagE], l, !activa)); });
        letras.appendChild(bt);
      });
      t.appendChild(letras);
      var acc = crear('div', 'acciones');
      var todas = crear('button', 'boton pequeno', 'Todas'); todas.type = 'button';
      todas.addEventListener('click', function () { guardar(tagE, LETRAS.join('')); });
      var ninguna = crear('button', 'boton pequeno', 'Ninguna'); ninguna.type = 'button';
      ninguna.addEventListener('click', function () { guardar(tagE, ''); });
      acc.appendChild(todas); acc.appendChild(ninguna);
      t.appendChild(acc);
      t.appendChild(crear('h3', '', 'Temas'));
      var temas = crear('div', 'temas');
      TEMAS_AJUSTE.forEach(function (l) {
        var fijo = OBLIGATORIOS.indexOf(l) >= 0;
        var lab = crear('label');
        var c = crear('input');
        c.type = 'checkbox';
        c.id = 'tema-' + b.f + '-' + l;
        c.checked = fijo || config[tagT].indexOf(l) >= 0;
        c.disabled = fijo;
        c.addEventListener('change', function () { guardar(tagT, conLetra(config[tagT], l, c.checked)); });
        lab.appendChild(c);
        lab.appendChild(crear('span', 'l', l));
        lab.appendChild(crear('span', '', (TEMAS[l] || '') + (fijo ? ' · siempre' : '')));
        temas.appendChild(lab);
      });
      t.appendChild(temas);
      raiz.appendChild(t);
    });
  }

  // ---------------------------------------------------------------- lectura
  function leerConfig() {
    return pedir('/config').then(function (filas) {
      (Array.isArray(filas) ? filas : []).forEach(function (f) {
        if (f && Object.prototype.hasOwnProperty.call(config, f.tag)) config[f.tag] = String(f.value || '').toUpperCase().replace(/[^A-Z]/g, '');
      });
      configLeida = true;
      pintarAjustes();
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
    }).catch(function (e) {
      error.hidden = false;
      error.textContent = 'No se puede consultar el receptor: ' + e.message + '.';
    });
  }

  // ----------------------------------------------------------------- inicio
  Array.prototype.forEach.call(document.querySelectorAll('.pestana'), function (p) {
    p.addEventListener('click', function () {
      Array.prototype.forEach.call(document.querySelectorAll('.pestana'), function (q) { q.classList.toggle('activa', q === p); });
      ['mensajes', 'ajustes', 'acerca'].forEach(function (v) { $('vista-' + v).hidden = v !== p.getAttribute('data-vista'); });
      $('estado-ajustes').hidden = true;
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

  pintarAjustes();
  leerConfig().catch(function () { /* sin ajustes se muestra todo */ }).then(leerTodo);
  setInterval(leerTodo, 60000);
  setInterval(pintarEstado, 1000);
})();
