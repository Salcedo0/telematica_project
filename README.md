# PIBL-WS: Proxy Inverso + Balanceador de Carga + Web Server

> **Asignatura:** Internet: Arquitectura y Protocolos  
> **Proyecto:** N1 — PIBL-WS  
> **Lenguaje:** C (API Sockets POSIX, pthreads)  
> **Despliegue:** Amazon Web Services (EC2)  
> **Fecha de entrega:** Mayo 6 de 2026

---

## Tabla de Contenido

1. [Introducción](#1-introducción)
2. [Arquitectura del Sistema](#2-arquitectura-del-sistema)
3. [Desarrollo](#3-desarrollo)
   - [Estructura del Repositorio](#31-estructura-del-repositorio)
   - [TWS — Telematics Web Server](#32-tws--telematics-web-server)
   - [PIBL — Proxy Inverso + Balanceador de Carga](#33-pibl--proxy-inverso--balanceador-de-carga)
   - [Modelo de Concurrencia](#34-modelo-de-concurrencia)
   - [Balanceador Round Robin](#35-balanceador-round-robin)
   - [Sistema de Caché con TTL](#36-sistema-de-caché-con-ttl)
   - [Sistema de Logging](#37-sistema-de-logging)
   - [Protocolo HTTP/1.1 y MIME Types](#38-protocolo-http11-y-mime-types)
   - [Archivo de Configuración](#39-archivo-de-configuración)
4. [Compilación y Ejecución](#4-compilación-y-ejecución)
5. [Despliegue en AWS](#5-despliegue-en-aws)
6. [Casos de Prueba](#6-casos-de-prueba)
7. [Conclusiones](#7-conclusiones)
8. [Referencias](#8-referencias)

---

## 1. Introducción

Este proyecto implementa desde cero, en lenguaje C con la API de sockets POSIX, dos binarios independientes que cooperan para reproducir la arquitectura de una infraestructura web real:

**TWS (Telematics Web Server)** es un servidor HTTP/1.1 que sirve recursos estáticos desde un directorio configurable (*document root*). Implementa los métodos `GET`, `HEAD` y `POST`, detección de MIME types por extensión de archivo, bloqueo de path traversal, respuestas de error estándar (200/400/404), y concurrencia mediante un hilo POSIX por conexión con logging thread-safe.

**PIBL (Proxy Inverso + Balanceador de Carga)** recibe peticiones HTTP de los clientes y las distribuye entre tres instancias TWS usando la política **Round Robin**, protegida por mutex para garantizar la correcta rotación bajo carga concurrente. Adicionalmente implementa un sistema de **caché en disco con TTL configurable**: las respuestas `200 OK` a peticiones `GET` se almacenan como archivos en el directorio `./cache/`, junto a un archivo `.meta` que registra el timestamp de expiración. Ante un *cache hit*, el PIBL sirve la respuesta directamente desde disco sin consultar ningún backend.

La arquitectura completa se despliega sobre cuatro instancias EC2 de AWS: una instancia pública ejecutando el PIBL en el puerto 8080 y tres instancias privadas ejecutando el TWS en los puertos 8081, 8082 y 8083 respectivamente.

---

## 2. Arquitectura del Sistema

```
                    ┌──────────────────────────────────────────────────┐
                    │                  AWS Cloud (VPC)                  │
                    │                                                    │
  Cliente HTTP      │  ┌─────────────────────────────────────────────┐  │
  (browser /        │  │            PIBL  — IP Pública :8080          │  │
   curl /           │  │                                               │  │
   Postman)  ───────┼─►│  accept() loop ──► pthread_create()          │  │
                    │  │                         │                     │  │
                    │  │            proxy_thread()                     │  │
                    │  │   ┌─────────────────────┴──────────┐         │  │
                    │  │   │  Cache lookup (solo GET)        │         │  │
                    │  │   │  HIT  → send desde disco → FIN │         │  │
                    │  │   │  MISS → next_backend() [RR]    │         │  │
                    │  │   └─────────────┬──────────────────┘         │  │
                    │  └────────────────┼────────────────────────────┘  │
                    │                   │   Red interna (privada)        │
                    │          ┌────────┴──────────────────────┐        │
                    │          │  ┌──────┐  ┌──────┐  ┌──────┐ │        │
                    │          │  │:8081 │  │:8082 │  │:8083 │ │        │
                    │          │  │ TWS  │  │ TWS  │  │ TWS  │ │        │
                    │          │  └──────┘  └──────┘  └──────┘ │        │
                    │          └───────────────────────────────┘        │
                    └──────────────────────────────────────────────────┘
```

### Flujo completo de una petición

```
Cliente
  │  GET /index.html HTTP/1.1
  ▼
PIBL :8080 → proxy_thread()
  │
  ├─ uri_to_filename("/index.html") → "./cache/_index.html.cache"
  │
  ├─ cache_is_valid()?
  │     ├─ SÍ (time(NULL) < expiry en .meta) → send_all(client_fd, .cache) → FIN
  │     └─ NO → next_backend() [Round Robin + mutex]
  │                   │
  │             connect() al backend seleccionado
  │                   │
  │             send_all(back_fd, req_buf)  — forward del request completo
  │                   │
  │             read() loop → send_all(client_fd) en streaming
  │                   │
  │             Si respuesta comienza "HTTP/1.1 200" o "HTTP/1.0 200":
  │                   └─ cache_write() → escribe .cache + .meta (expiry = now + TTL)
  │
  └─ close(client_fd) → pthread exit
```

---

## 3. Desarrollo

### 3.1 Estructura del Repositorio

```
telematica_project/
├── README.md
└── tws-pibl/
    ├── Makefile                     # Build modular: gcc -Wall, -lpthread
    ├── ANALISIS.md                  # Análisis técnico detallado del código
    ├── pibl/
    │   ├── pibl.c                   # main(): accept loop, pthread_create por conexión
    │   ├── proxy.c / proxy.h        # proxy_thread(): lógica central del proxy
    │   ├── balancer.c / balancer.h  # next_backend(): Round Robin con mutex
    │   ├── cache.c / cache.h        # uri_to_filename, cache_is_valid, cache_write
    │   ├── config.c / config.h      # load_config(): parsea config.txt
    │   ├── logger.c / logger.h      # log_init, log_msg (mutex-protected)
    │   ├── net_utils.c / net_utils.h# send_all, send_error
    │   └── config.txt               # Configuración: port, ttl, backends
    ├── tws/
    │   ├── tws.c                    # main(): accept loop, pthread_create por conexión
    │   ├── handler.c / handler.h    # handle_request(): GET/HEAD/POST + doc_root
    │   ├── http_parser.c / http_parser.h  # parse_request(): struct HttpRequest
    │   ├── mime.c / mime.h          # get_mime(): detección por extensión
    │   ├── logger.c / logger.h      # idéntico al del PIBL (binarios separados)
    │   └── net_utils.c / net_utils.h# idéntico al del PIBL
    ├── cache/                       # Directorio de caché en disco (generado en runtime)
    └── web_content/
        ├── case1/   index.html + logo.png
        ├── case2/   index.html + img1.png + img2.png + img3.png + img4.png
        ├── case3/   index.html + bigfile.txt (1 048 576 bytes exactos)
        └── case4/   index.html + img1.png + img2.png + file1.txt + file2.txt (~1 MB total)
```

> `logger.c` y `net_utils.c` están duplicados entre `tws/` y `pibl/` porque cada binario compila de forma completamente independiente y no comparte objetos intermedios.

---

### 3.2 TWS — Telematics Web Server

#### Arranque (`tws/tws.c`)

```bash
./tws/tws <PORT> <LogFile> <DocumentRootFolder>
```

El programa valida que `argc == 4`. Inicializa el log en modo append, ignora `SIGPIPE` (para que clientes que cierran abruptamente no maten el proceso), crea el socket TCP con `SO_REUSEADDR`, hace `listen(srv, 128)` y entra al `accept()` loop. Por cada conexión crea un hilo desacoplado (`pthread_detach`) que ejecuta `client_thread`.

#### Parser HTTP (`tws/http_parser.c`)

`parse_request()` lee del socket en un bucle hasta encontrar `\r\n\r\n` o agotar el buffer (`BUFFER_SIZE * 2 = 16 384` bytes). Extrae método, URI y versión con `sscanf("%15s %1023s %15s")`.

```c
typedef struct {
    char raw[BUFFER_SIZE * 2];  // buffer crudo completo del request
    int  raw_len;
    char method[16];
    char uri[MAX_PATH];         // MAX_PATH = 1024
    char version[16];
} HttpRequest;
```

Retorna `1` si OK, `0` si el cliente cerró sin enviar datos, `-1` si el parseo falla (→ 400).

#### Lógica de request (`tws/handler.c`)

| Paso | Detalle |
|------|---------|
| Path traversal | `strstr(uri, "..")` → 400 Bad Request inmediato |
| Construcción del filepath | `uri == "/"` → `<doc_root>/index.html`; resto → `<doc_root><uri>` |
| **GET / HEAD** | `stat()` verifica existencia y tipo regular. `get_mime()` detecta MIME. Headers con `Content-Length: <st_size>`. HEAD retorna sin body. GET envía en chunks de 8 192 bytes con `send_all()` |
| **POST** | Lee `Content-Length` del raw buffer. Drain del body del socket (máx 8 191 bytes). Responde siempre `HTTP/1.1 200 OK` con body literal `OK` (2 bytes). El body recibido se descarta |
| Método desconocido | 400 Bad Request |

#### Códigos HTTP que emite TWS

| Código | Condición |
|--------|-----------|
| `200 OK` | GET/HEAD a archivo existente; cualquier POST válido |
| `400 Bad Request` | Request malformado, path traversal (`..`), método no reconocido |
| `404 Not Found` | `stat()` falla o el path no es un archivo regular (`S_ISREG`) |

#### MIME Types soportados (`tws/mime.c`)

Detección case-insensitive (`strcasecmp`) por extensión del path:

| Extensión | MIME Type |
|-----------|-----------|
| `.html`, `.htm` | `text/html` |
| `.css` | `text/css` |
| `.js` | `application/javascript` |
| `.jpg`, `.jpeg` | `image/jpeg` |
| `.png` | `image/png` |
| `.gif` | `image/gif` |
| `.ico` | `image/x-icon` |
| `.txt` | `text/plain` |
| `.pdf` | `application/pdf` |
| Otros | `application/octet-stream` |

---

### 3.3 PIBL — Proxy Inverso + Balanceador de Carga

#### Arranque (`pibl/pibl.c`)

```bash
./pibl/pibl [config_path] [log_path]
# Defaults: pibl/config.txt   pibl/pibl.log
```

Carga la configuración con `load_config()`, crea el directorio `./cache/` con `mkdir(cache_dir, 0755)`, inicializa el log, ignora `SIGPIPE`, registra en el log la configuración completa (puerto, número de backends, TTL, directorio de caché y lista de cada backend con índice y dirección), y entra al accept loop. Por cada conexión asigna un `ConnData { fd, sockaddr_in }` en el heap y lo pasa a `proxy_thread` vía `pthread_create + pthread_detach`.

#### Lógica de proxy (`pibl/proxy.c`)

Por cada hilo los pasos son:

1. Lee el request HTTP completo del cliente (hasta `\r\n\r\n` o 16 384 bytes).
2. Parsea método y URI con `sscanf`.
3. Convierte el URI a nombre de archivo de caché con `uri_to_filename()`.
4. **Cache lookup (solo GET):** `cache_is_valid()` abre el `.meta`, lee el timestamp de expiración. Si `time(NULL) > expiry`, borra `.cache` y `.meta` y retorna 0 (miss). Si vigente, vuelca el `.cache` al cliente con `send_all()` y termina — cache hit.
5. **Selección de backend:** `next_backend()` Round Robin (ver §3.5).
6. Abre un nuevo socket TCP (`socket()` + `connect()`). Si falla → 502 Bad Gateway.
7. **Forward del request:** envía el buffer crudo completo al backend. Si había body POST parcialmente leído, lee del cliente los bytes restantes según `Content-Length` y los reenvía.
8. **Lectura de respuesta:** buffer dinámico (inicial 32 768 bytes, `realloc` duplicando al superar capacidad) con **streaming simultáneo** al cliente en tiempo real mediante `send_all()`.
9. **Cache write (solo GET 200):** si la respuesta empieza con `HTTP/1.1 200` o `HTTP/1.0 200`, guarda la respuesta completa en disco.

#### Códigos HTTP que emite PIBL directamente

| Código | Condición |
|--------|-----------|
| `400 Bad Request` | Request line del cliente no parseable |
| `500 Internal Server Error` | `malloc()` del buffer de respuesta falla |
| `502 Bad Gateway` | `socket()` o `connect()` al backend falla |
| `503 Service Unavailable` | `backend_count == 0` (sin backends configurados) |

Para todos los demás casos el código viene del backend y se retransmite tal cual (passthrough).

---

### 3.4 Modelo de Concurrencia

Ambos binarios implementan el patrón **Thread-per-Connection** con hilos desacoplados (`pthread_detach`):

```
Hilo principal — accept() loop (bloqueante)
       │
       ├─ conexión 1 → malloc(ConnData) → pthread_create → pthread_detach → hilo_1
       ├─ conexión 2 → malloc(ConnData) → pthread_create → pthread_detach → hilo_2
       └─ conexión N → ...
```

Cada hilo trabajador maneja una conexión completa de forma autónoma: lee → procesa → escribe → `close(fd)` → termina. El estado compartido está protegido por mutexes POSIX:

| Mutex | Protege | Módulo |
|-------|---------|--------|
| `log_mutex` | Escritura al log (stdout + archivo) | `logger.c` (TWS y PIBL) |
| `rr_mutex` | Contador `rr_index` del Round Robin | `balancer.c` (PIBL) |

Ambos binarios configuran `SO_REUSEADDR`, `listen(srv, 128)` e ignoran `SIGPIPE`.

---

### 3.5 Balanceador Round Robin

Implementado en `pibl/balancer.c` con un array global de hasta 16 backends (`MAX_BACKENDS`) y un índice circular protegido por mutex:

```c
Backend *next_backend(void) {
    if (backend_count == 0) return NULL;
    pthread_mutex_lock(&rr_mutex);
    Backend *b = &backends[rr_index % backend_count];
    rr_index = (rr_index + 1) % backend_count;
    pthread_mutex_unlock(&rr_mutex);
    return b;
}
```

Con tres backends la distribución de peticiones no cacheadas es estrictamente circular:

```
Petición 1 → TWS :8081
Petición 2 → TWS :8082
Petición 3 → TWS :8083
Petición 4 → TWS :8081  ← ciclo reinicia
...
```

> Las peticiones servidas desde caché no consumen un turno de Round Robin. Si un backend está caído el PIBL devuelve 502 a ese cliente y avanza el índice de todas formas; no hay reintentos automáticos ni health checks.

---

### 3.6 Sistema de Caché con TTL

Implementado en `pibl/cache.c`. La caché opera exclusivamente sobre peticiones `GET` con respuesta `200 OK`.

#### Estructura en disco

Por cada recurso cacheado se crean dos archivos en `./cache/`:

```
./cache/_index.html.cache       ← respuesta HTTP completa (headers + body)
./cache/_index.html.cache.meta  ← timestamp Unix de expiración (long en texto)
```

#### Sanitización URI → nombre de archivo (`uri_to_filename`)

Los caracteres `/`, `?`, `&` y `=` se reemplazan por `_`; el resto se mantiene literal. Se añade el sufijo `.cache`.

| URI solicitado | Archivo de caché |
|----------------|-----------------|
| `/index.html` | `./cache/_index.html.cache` |
| `/api?x=1&y=2` | `./cache/_api_x_1_y_2.cache` |
| `/img/logo.png` | `./cache/_img_logo.png.cache` |

#### Validación del TTL

```c
int cache_is_valid(const char *cache_path) {
    // Abre el .meta y lee el timestamp de expiración almacenado como long
    // Si time(NULL) > expiry → remove(.cache), remove(.meta) → return 0
    // Si vigente → return 1
}
```

La invalidación es **lazy**: solo ocurre al momento de la siguiente petición al mismo recurso. No hay proceso de fondo que limpie entradas expiradas.

#### Escritura en caché

```c
void cache_write(const char *cache_path, const char *data, size_t len) {
    // Escribe data en el .cache (modo "wb") — incluye headers HTTP + body
    // Escribe (time(NULL) + cache_ttl) en el .meta como long en texto plano
}
```

La caché persiste en disco entre reinicios del proceso PIBL, cumpliendo el requisito de resistencia ante fallas.

---

### 3.7 Sistema de Logging

Implementado en `logger.c` (código idéntico en `tws/` y `pibl/`). Thread-safe mediante `pthread_mutex_t log_mutex`.

**Formato:** `[YYYY-MM-DD HH:MM:SS] [LEVEL] mensaje`

Cada `log_msg()` escribe simultáneamente a **stdout** (con `fflush`) y al **archivo de log** especificado como argumento (modo append, con `fflush`).

| Nivel | Quién | Eventos |
|-------|-------|---------|
| `INFO` | TWS y PIBL | Arranque del servidor, configuración, cada conexión aceptada (IP) |
| `REQ` | TWS | Cada request line recibida (`METHOD URI VERSION`) |
| `RES` | TWS | Cada archivo servido o respuesta POST enviada |
| `WARN` | TWS | Path traversal bloqueado, método no reconocido |
| `PROXY` | PIBL | Request proxiado, backend seleccionado para cada petición |
| `CACHE` | PIBL | `HIT — served from cache` / `MISS — response cached` |
| `ERROR` | PIBL | Fallos de conexión a backends |

**Ejemplo de salida del PIBL:**
```
[2026-05-06 14:30:00] [INFO]  PIBL starting | port=8080 | backends=3 | ttl=60s | cache=./cache
[2026-05-06 14:30:00] [INFO]  Backend[0] = 127.0.0.1:8081
[2026-05-06 14:30:00] [INFO]  Backend[1] = 127.0.0.1:8082
[2026-05-06 14:30:00] [INFO]  Backend[2] = 127.0.0.1:8083
[2026-05-06 14:30:01] [INFO]  Connection from 54.210.3.11
[2026-05-06 14:30:01] [PROXY] REQ GET /index.html from client
[2026-05-06 14:30:01] [PROXY] Forwarding to 127.0.0.1:8081
[2026-05-06 14:30:01] [CACHE] MISS — response cached
[2026-05-06 14:30:02] [INFO]  Connection from 54.210.3.11
[2026-05-06 14:30:02] [PROXY] REQ GET /index.html from client
[2026-05-06 14:30:02] [CACHE] HIT — served from cache
```

---

### 3.8 Protocolo HTTP/1.1 y MIME Types

El servidor implementa el subconjunto de HTTP/1.1 (RFC 2616) necesario para servir contenido estático:

- **Request line parseada:** `METHOD SP Request-URI SP HTTP-Version CRLF`
- **Header interpretado:** `Content-Length` (en POST para TWS; en forward de body para PIBL)
- **Response line generada:** siempre `HTTP/1.1 <code> <reason>`
- **Headers de respuesta:** `Content-Type`, `Content-Length`, `Connection: close`
- **Keep-alive:** no soportado; siempre `Connection: close`
- **Version negotiation:** acepta cualquier versión en el request, siempre responde `HTTP/1.1`
- **Versión del protocolo aceptada:** cualquier valor en el campo version (no se valida)

---

### 3.9 Archivo de Configuración

El PIBL lee su configuración de un archivo de texto plano (`clave=valor`). Líneas que comienzan con `#` y líneas vacías se ignoran.

```ini
# pibl/config.txt
port=8080
ttl=60
backend=127.0.0.1:8081
backend=127.0.0.1:8082
backend=127.0.0.1:8083
```

| Parámetro | Tipo | Descripción | Default en código |
|-----------|------|-------------|-------------------|
| `port` | int | Puerto de escucha del PIBL | `8080` |
| `ttl` | int | Vida del caché en segundos | `60` |
| `backend` | `host:port` | Dirección de un TWS backend (hasta 16) | — |

> El directorio de caché (`./cache`) está inicializado como valor por defecto de `cache_dir` en `config.c` y **no se configura desde el archivo**. Se crea automáticamente al arrancar el PIBL.

---

## 4. Compilación y Ejecución

### Requisitos

- Linux (Ubuntu 22.04+ recomendado)
- GCC ≥ 9.0
- GNU Make
- pthreads (incluida en glibc estándar)

### Compilación

Desde `tws-pibl/`:

```bash
make          # compila tws/tws y pibl/pibl
make tws      # solo el TWS
make pibl     # solo el PIBL
make clean    # elimina .o y binarios
```

El Makefile compila cada `.c` a `.o` con `gcc -Wall` y enlaza con `-lpthread`.

### Ejecución local

```bash
# Desde tws-pibl/

# Levantar 3 instancias TWS
./tws/tws 8081 /tmp/tws1.log ./web_content/case1 &
./tws/tws 8082 /tmp/tws2.log ./web_content/case1 &
./tws/tws 8083 /tmp/tws3.log ./web_content/case1 &

# Levantar el PIBL
./pibl/pibl pibl/config.txt /tmp/pibl.log &
```

### Verificación rápida

```bash
# GET
curl http://localhost:8080/index.html

# HEAD (solo headers, sin body)
curl -I http://localhost:8080/index.html

# POST
curl -X POST http://localhost:8080/ \
     -H "Content-Type: application/x-www-form-urlencoded" \
     -d "campo=valor"

# 404
curl -v http://localhost:8080/noexiste.html

# Telnet manual (según especificación del proyecto)
telnet localhost 8080
GET /index.html HTTP/1.1
Host: localhost
[Enter dos veces]
```

---

## 5. Despliegue en AWS

### Topología EC2

| Instancia | Tipo | Rol | Acceso |
|-----------|------|-----|--------|
| `pibl-node` | t2.micro (Ubuntu) | Proxy público | IP pública, SG puerto 8080 |
| `tws-node-1` | t2.micro (Ubuntu) | TWS backend 1 | Solo red interna, puerto 8081 |
| `tws-node-2` | t2.micro (Ubuntu) | TWS backend 2 | Solo red interna, puerto 8082 |
| `tws-node-3` | t2.micro (Ubuntu) | TWS backend 3 | Solo red interna, puerto 8083 |

### Security Groups

**PIBL Security Group (inbound):**
```
TCP 8080  →  0.0.0.0/0          (tráfico HTTP público)
TCP 22    →  <tu-IP>/32         (administración SSH)
```

**TWS Security Group (inbound):**
```
TCP 8081-8083  →  <IP-privada-PIBL>/32   (solo desde el PIBL)
TCP 22         →  <tu-IP>/32
```

### Pasos de despliegue

```bash
# En cada instancia TWS (repetir ajustando el puerto: 8081, 8082, 8083):
sudo apt update && sudo apt install -y gcc make git
git clone https://github.com/Salcedo0/telematica_project.git
cd telematica_project/tws-pibl
make tws
./tws/tws 8081 /var/log/tws1.log ./web_content/case1

# En la instancia PIBL:
sudo apt update && sudo apt install -y gcc make git
git clone https://github.com/Salcedo0/telematica_project.git
cd telematica_project/tws-pibl
make pibl

# Editar pibl/config.txt con las IPs privadas reales:
# backend=<IP_PRIVADA_TWS1>:8081
# backend=<IP_PRIVADA_TWS2>:8082
# backend=<IP_PRIVADA_TWS3>:8083

./pibl/pibl pibl/config.txt /var/log/pibl.log

# Verificar desde máquina local:
curl http://<IP_PUBLICA_PIBL>:8080/index.html
```

---

## 6. Casos de Prueba

Todos los casos se acceden a través del PIBL en el puerto 8080. El contenido de prueba está en `web_content/` y el TWS se puede levantar apuntando a cualquiera de las cuatro carpetas.

### Caso 1 — Página HTML con hipertextos e imagen

```bash
curl http://<IP_PIBL>:8080/index.html    # HTML con enlace interno
curl http://<IP_PIBL>:8080/logo.png      # Imagen PNG (image/png)
```

Verifica detección correcta de MIME type y entrega de binarios (imágenes PNG) además de HTML.

---

### Caso 2 — Página con múltiples imágenes

```bash
curl http://<IP_PIBL>:8080/index.html    # HTML con 4 imágenes
curl http://<IP_PIBL>:8080/img1.png
curl http://<IP_PIBL>:8080/img2.png
curl http://<IP_PIBL>:8080/img3.png
curl http://<IP_PIBL>:8080/img4.png
```

Con TTL activo, la segunda solicitud de cada imagen debe mostrar `CACHE: HIT` en el log del PIBL.

---

### Caso 3 — Archivo único de ~1 MB

```bash
curl http://<IP_PIBL>:8080/index.html
curl http://<IP_PIBL>:8080/bigfile.txt \
     -o /dev/null -w "Size: %{size_download} bytes | Time: %{time_total}s\n"
```

`bigfile.txt` tiene exactamente 1 048 576 bytes. Verifica la transferencia correcta de archivos grandes usando el buffer de 8 192 bytes por chunk en `send_file_response()`.

---

### Caso 4 — Múltiples archivos con total ~1 MB

```bash
curl http://<IP_PIBL>:8080/index.html
curl http://<IP_PIBL>:8080/img1.png
curl http://<IP_PIBL>:8080/img2.png
curl http://<IP_PIBL>:8080/file1.txt    # 262 144 bytes
curl http://<IP_PIBL>:8080/file2.txt    # 262 144 bytes
```

`file1.txt` y `file2.txt` suman 512 KB; con las dos imágenes el total supera 1 MB.

---

### Prueba del Round Robin

```bash
# URIs distintos por petición para evitar cache hits
for i in 1 2 3 4 5 6; do
  curl -s "http://localhost:8080/index.html?n=$i" -o /dev/null
done

grep "Forwarding" /tmp/pibl.log | tail -6
```

Salida esperada (rotación estricta):
```
[...] [PROXY] Forwarding to 127.0.0.1:8081
[...] [PROXY] Forwarding to 127.0.0.1:8082
[...] [PROXY] Forwarding to 127.0.0.1:8083
[...] [PROXY] Forwarding to 127.0.0.1:8081
[...] [PROXY] Forwarding to 127.0.0.1:8082
[...] [PROXY] Forwarding to 127.0.0.1:8083
```

---

### Prueba del Caché con TTL

```bash
rm -rf cache && mkdir cache

# Primera petición → MISS (va al backend, almacena en disco)
curl -s http://localhost:8080/index.html -o /dev/null

# Segunda petición → HIT (sirve desde disco)
curl -s http://localhost:8080/index.html -o /dev/null

grep CACHE /tmp/pibl.log

# Ver archivo de caché en disco
ls -la cache/
cat cache/_index.html.cache.meta   # muestra timestamp Unix de expiración

# Esperar expiración (TTL=60 → 65s)
sleep 65

# Petición post-TTL → MISS nuevamente
curl -s http://localhost:8080/index.html -o /dev/null

grep CACHE /tmp/pibl.log | tail -4
```

---

### Prueba de concurrencia

```bash
for i in $(seq 1 10); do
  curl -s http://localhost:8080/index.html -o /dev/null &
done
wait
echo "Todos los clientes completados"
grep ERROR /tmp/pibl.log | wc -l   # debe ser 0
```

---

### Prueba de métodos HTTP

```bash
# GET → 200 + body
curl -v http://localhost:8080/index.html 2>&1 | grep "< HTTP"

# HEAD → 200 + headers solamente (body ausente)
curl -I http://localhost:8080/index.html

# POST → 200 OK con body literal "OK"
curl -X POST http://localhost:8080/ -d "dato=valor" -v

# 404 → recurso inexistente
curl -v http://localhost:8080/noexiste.html 2>&1 | grep "< HTTP"

# Método inválido → 400 Bad Request
printf "PATCH /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n" | nc localhost 8080
```

---

## 7. Conclusiones

- **C y sockets POSIX como base real.** Implementar el sistema directamente sobre la API de sockets sin ninguna librería de red de alto nivel obliga a comprender cada detalle del protocolo: apertura del socket, parseo manual del request line con `sscanf`, construcción a mano de los headers de respuesta con `snprintf`, y manejo de la lectura en chunks con `read()` + `send_all()`. Esta exposición directa convierte HTTP/1.1 de una caja negra en un protocolo tangible.

- **Modularización con responsabilidad única.** El proyecto se organizó en módulos acotados: `balancer.c` solo hace Round Robin, `cache.c` solo gestiona el sistema de archivos de caché, `config.c` solo parsea el archivo de configuración, `mime.c` solo detecta tipos de contenido. Esta separación facilita la depuración al aislar fallos y hace el código comprensible módulo por módulo.

- **Concurrencia con pthreads y sus implicaciones.** El modelo Thread-per-Connection es simple de implementar y correcto para la escala del proyecto. Todos los accesos a estado compartido (`rr_index`, el archivo de log) están protegidos por mutexes, lo que garantiza la ausencia de condiciones de carrera. A mayor escala, un pool de hilos con cola de trabajo sería más eficiente al evitar el costo de crear y destruir un hilo por cada petición.

- **Caché en disco como mecanismo de optimización real.** El sistema de caché con TTL demostró ser el cambio más impactante en el rendimiento observable: una vez que un recurso está almacenado, el PIBL lo sirve sin involucrar ningún backend ni ningún socket adicional. La persistencia en disco garantiza que el caché sobrevive a reinicios del proceso PIBL, tal como especificaba el enunciado del proyecto.

- **Round Robin como política de distribución equitativa.** La rotación circular garantiza que ningún backend acumule sistemáticamente más carga que los demás. La ausencia de health checks es una limitación reconocida del diseño actual: un backend caído recibe un turno y falla con 502, pero el balanceador recupera el ritmo normal en el ciclo siguiente sin intervención manual.

---

## 8. Referencias

- [RFC 2616 — Hypertext Transfer Protocol HTTP/1.1](https://datatracker.ietf.org/doc/rfc2616/)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [POSIX Threads Programming — Lawrence Livermore National Laboratory](https://hpc-tutorials.llnl.gov/posix/)
- [The Linux Programming Interface — Michael Kerrisk](https://man7.org/tlpi/)
- [GNU C Library — Socket Programming](https://www.gnu.org/software/libc/manual/html_node/Sockets.html)
- [AWS EC2 User Guide](https://docs.aws.amazon.com/ec2/latest/userguide/)
- [AWS VPC Security Groups](https://docs.aws.amazon.com/vpc/latest/userguide/security-groups.html)

---

*Internet: Arquitectura y Protocolos — Proyecto N1, 2026.*
