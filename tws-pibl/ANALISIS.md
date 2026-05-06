# Análisis del proyecto PIBL-WS

Documento técnico que describe en detalle qué hace cada componente del sistema cuando se ejecuta. Pensado para compararlo punto por punto contra los requisitos del PDF del proyecto.

---

## 1. Visión general

El sistema implementa dos binarios independientes en C que cooperan vía sockets TCP:

- **TWS** (Tiny Web Server): servidor HTTP/1.1 que sirve archivos estáticos desde un *document root*.
- **PIBL** (Proxy Inverso + Balanceador): recibe peticiones HTTP de clientes, las distribuye entre varios TWS usando **Round Robin**, y mantiene un **caché en disco** con TTL para respuestas GET.

Topología típica:

```
   Cliente (curl/navegador)
            │
            ▼  HTTP/1.1
       PIBL :8080
       ┌────┴────┐
       ▼         ▼         ▼
   TWS :8081  TWS :8082  TWS :8083
   (case1)    (case1)    (case1)
```

Ambos binarios son **multihilo** (un `pthread` desacoplado por conexión), thread-safe en logging, y soportan terminación limpia de conexiones via `Connection: close` y `signal(SIGPIPE, SIG_IGN)`.

---

## 2. Estructura del repositorio (post-refactor)

```
tws-pibl/
├── Makefile                   # build modular: gcc -Wall, -lpthread
├── README.md
├── ANALISIS.md                # este archivo
├── pibl/
│   ├── config.txt             # puerto, ttl, lista de backends
│   ├── pibl.c                 # main + accept loop
│   ├── proxy.c / proxy.h      # proxy_thread, ConnData
│   ├── balancer.c / balancer.h # Round Robin (next_backend), struct Backend
│   ├── cache.c / cache.h      # uri_to_filename, cache_is_valid, cache_write
│   ├── config.c / config.h    # load_config, globals listen_port/cache_ttl/cache_dir
│   ├── logger.c / logger.h    # log_init, log_msg, log_close (mutex-protected)
│   └── net_utils.c / net_utils.h # send_all, send_error
├── tws/
│   ├── tws.c                  # main + accept loop
│   ├── handler.c / handler.h  # handle_request, GET/HEAD/POST, send_file_response, doc_root
│   ├── http_parser.c / http_parser.h # struct HttpRequest, parse_request
│   ├── mime.c / mime.h        # get_mime
│   ├── logger.c / logger.h    # idéntico a pibl/logger (binarios separados)
│   └── net_utils.c / net_utils.h # idéntico a pibl/net_utils
├── cache/                     # persistencia del caché PIBL (creada en runtime)
└── web_content/
    ├── case1/                 # caso de prueba 1 (index.html, logo.png)
    ├── case2/
    ├── case3/
    └── case4/
```

`logger` y `net_utils` están duplicados entre `tws/` y `pibl/` porque cada binario se compila por separado; no comparten objetos.

---

## 3. Modelo de concurrencia

| Aspecto | TWS | PIBL |
|---|---|---|
| Thread principal | `accept()` loop bloqueante | `accept()` loop bloqueante |
| Thread por conexión | `pthread_create` + `pthread_detach` | igual |
| Sincronización | Mutex en logger (`log_mutex`) | Mutex en logger + mutex en Round Robin (`rr_mutex`) |
| Backlog `listen()` | 128 | 128 |
| `SO_REUSEADDR` | sí | sí |
| `SIGPIPE` | ignorado (`signal(SIGPIPE, SIG_IGN)`) — evita que un cliente que cierra abruptamente mate el proceso | igual |
| Alocación por conexión | `malloc(int)` para el fd | `malloc(ConnData)` (fd + sockaddr_in) |

Cada hilo trabajador maneja una conexión completa: lee request → procesa → escribe respuesta → `close(fd)` → termina. No hay pool de hilos; se crea uno nuevo por petición.

---

## 4. Flujo de ejecución de TWS

### 4.1. Arranque (`tws/tws.c`)

```
./tws/tws <PORT> <LogFile> <DocumentRootFolder>
```

1. Valida que `argc == 4`. Si no, imprime usage y `exit(1)`.
2. `port = atoi(argv[1])`, copia `argv[3]` a `doc_root` (global de `handler.c`).
3. Strip de slash final en `doc_root` (`./web_content/case1/` → `./web_content/case1`).
4. `log_init(argv[2])`: hace `fopen(path, "a")`. Si falla, `perror` + `exit(1)`.
5. `signal(SIGPIPE, SIG_IGN)`.
6. Crea socket TCP IPv4, `setsockopt(SO_REUSEADDR)`, `bind(INADDR_ANY:port)`, `listen(srv, 128)`.
7. Loguea `INFO: TWS listening on port X | root=Y | log=Z`.
8. Entra al loop infinito de `accept`.

### 4.2. Loop de aceptación

Por cada conexión entrante:
1. `malloc(sizeof(int))` para pasar el fd al thread.
2. `accept(srv, ...)` rellena `cli` (sockaddr_in del cliente).
3. `inet_ntop` extrae la IP, se loguea `INFO: New connection from <ip>`.
4. `pthread_create(client_thread, fd_ptr)` y `pthread_detach`.

### 4.3. `client_thread` → `handle_request` (handler.c)

1. **Parseo** (`parse_request` en `http_parser.c`):
   - Lee del socket en bucle hasta encontrar `\r\n\r\n` o llenar el buffer (`BUFFER_SIZE * 2 = 16384` bytes).
   - Si `read` devuelve 0 antes de leer cualquier byte → return 0 (cierra silenciosamente).
   - `sscanf(raw, "%15s %1023s %15s", method, uri, version)`. Si no extrae 3 tokens → return -1.
   - Llena `HttpRequest { raw, raw_len, method, uri, version }`.

2. **Logging del request**: `REQ <METHOD> <URI> <VERSION>`.

3. **Path traversal**: si `strstr(uri, "..")` encuentra `..` en cualquier parte del URI, responde **400 Bad Request** y loguea `WARN: Path traversal attempt blocked`.

4. **Construcción del filepath**:
   - Si `uri == "/"` → `<doc_root>/index.html`.
   - Si no → `<doc_root><uri>` (concatenación literal — el URI ya empieza con `/`).
   - Buffer `MAX_PATH * 2 = 2048` bytes.

5. **Dispatch por método**:

   - **GET / HEAD** → `send_file_response(fd, filepath, method, 200)`:
     1. `stat(filepath)`: si falla o no es regular file → **404 Not Found**.
     2. `get_mime(filepath)` por extensión (case-insensitive). 10 tipos soportados (ver §6.3).
     3. Envía header `HTTP/1.1 200 OK\r\nContent-Type: ...\r\nContent-Length: <st_size>\r\nConnection: close\r\n\r\n`.
     4. Si método es HEAD, retorna sin enviar body.
     5. Para GET, abre el archivo, lee en chunks de `BUFFER_SIZE = 8192` y los envía con `send_all`.
     6. Loguea `RES: Served: <filepath>`.

   - **POST** → respuesta fija:
     1. Busca `Content-Length:` (case-insensitive) en el buffer crudo, `atoi(cl_ptr + 15)`.
     2. Calcula cuántos bytes de body ya están en el buffer (todo lo que viene después de `\r\n\r\n`).
     3. Lee del socket los bytes restantes hasta llegar a `body_len` (limitado a `BUFFER_SIZE - 1 = 8191`).
     4. Envía respuesta hardcodeada: `HTTP/1.1 200 OK\r\n...\r\n\r\nOK` (body literal `"OK"`, 2 bytes).
     5. Loguea `RES: POST 200 OK`. **El body POST se descarta**: no se persiste ni se procesa.

   - **Otros métodos** (PUT, DELETE, etc.) → **400 Bad Request** + `WARN: Unknown method: <X>`.

6. `close(fd)`, retorna del hilo.

### 4.4. Códigos HTTP que TWS puede emitir

| Código | Cuándo |
|---|---|
| 200 OK | GET/HEAD a archivo existente, o cualquier POST |
| 400 Bad Request | request line malformada, path traversal, método desconocido |
| 404 Not Found | `stat()` falla o no es regular file |

No emite 405 ni 500. No soporta keep-alive (siempre `Connection: close`).

---

## 5. Flujo de ejecución de PIBL

### 5.1. Arranque (`pibl/pibl.c`)

```
./pibl/pibl [config_path] [log_path]
```

Defaults: `pibl/config.txt`, `pibl/pibl.log`.

1. `load_config(config_path)`:
   - Abre archivo. Si falla → `perror` + `exit(1)`.
   - Lee línea por línea. Ignora comentarios (`#`) y líneas vacías.
   - Reconoce 3 claves:
     - `port=N` → `listen_port`.
     - `ttl=N` → `cache_ttl` (segundos).
     - `backend=host:port` → agrega a `backends[]` (máx 16). Usa `strrchr(':')` para soportar IPs IPv6 con dos puntos (aunque el resto del código asume IPv4). Si no hay `:`, ignora la línea.
   - Cierra archivo.

2. `mkdir(cache_dir, 0755)` — `cache_dir` es siempre `"./cache"` (no se lee de config).

3. `log_init(log_path)`. Falla → exit.

4. `signal(SIGPIPE, SIG_IGN)`.

5. Loguea `INFO: PIBL starting | port=X | backends=N | ttl=Ts | cache=./cache` y la lista de backends.

6. Crea socket, `SO_REUSEADDR`, `bind(INADDR_ANY:listen_port)`, `listen(srv, 128)`.

7. `INFO: PIBL ready — accepting connections`.

8. Loop de accept idéntico a TWS pero con `ConnData { fd, cli }`.

### 5.2. `proxy_thread` (proxy.c)

Por cada conexión:

1. **Cleanup inicial** (con bug preexistente, ver §10):
   - Extrae `client_fd = cd->fd`.
   - `free(cd)`.
   - Computa una IP en `ip[]` que **no se usa después**.

2. **Lee el request del cliente** en un buffer de `BUFFER_SIZE * 2 = 16384` bytes hasta encontrar `\r\n\r\n` o EOF.

3. **Parsea request line** con `sscanf("%15s %1023s %15s")`. Si falla → 400 + close.

4. **Construye path de caché** vía `uri_to_filename(uri, ...)`:
   - Resultado: `./cache/<URI con / ? & = reemplazados por _>.cache`.
   - Ejemplo: `GET /api?x=1&y=2` → `./cache/_api_x_1_y_2.cache`.
   - Cada caché tiene un `.meta` paralelo: `./cache/_api_x_1_y_2.cache.meta`.

5. **Lookup de caché (sólo GET)**:
   - `cache_is_valid()` lee el `.meta`, parsea timestamp de expiración.
   - Si `time(NULL) > expiry`: borra ambos archivos y retorna inválido.
   - Si válido: abre `.cache` y vuelca al cliente con `send_all`. Loguea `CACHE: HIT — served from cache`. Cierra y retorna.

6. **Selección de backend** (`next_backend` en `balancer.c`):
   - Si `backend_count == 0` → 503 + close.
   - `pthread_mutex_lock(&rr_mutex)`.
   - Toma `&backends[rr_index % backend_count]`, incrementa `rr_index = (rr_index + 1) % backend_count`.
   - Unlock. Devuelve puntero al backend.
   - **Round Robin estricto**: cada conexión avanza el índice exactamente una vez, sin importar éxito/fallo. Un fallo de conexión no salta al siguiente backend.

7. **Conexión al backend**:
   - `socket(AF_INET)` + `connect()`.
   - Si falla `socket` → 502 + close. Si falla `connect` → 502 + close + log `ERROR: Cannot connect to backend X:Y`.

8. **Forwarding del request al backend**:
   - Busca `Content-Length:` (case-insensitive) en `req_buf`.
   - Calcula `already_body` = bytes del body que ya están en buffer.
   - Envía **el buffer crudo completo** (`req_buf[0..req_total]`) al backend, no sólo headers — esto incluye los bytes de body que ya leyó.
   - Lee del cliente los `body_len - already_body` bytes restantes y los reenvía. **Sin timeout** — si el cliente cuelga, el hilo se queda bloqueado.

9. **Lectura de respuesta del backend**:
   - Buffer dinámico (`malloc`/`realloc`, capacidad inicial `BUFFER_SIZE * 4 = 32768`, duplicación al doblar).
   - Lee del backend en chunks de `BUFFER_SIZE` y simultáneamente:
     - Acumula en `resp_buf` (para potencial cacheo).
     - Hace `send_all(client_fd, ...)` al cliente (streaming en tiempo real).
   - Continúa hasta que `read` retorna ≤ 0 (backend cerró su lado).

10. **Cacheo de respuesta** (sólo GET y sólo si la respuesta empieza con `HTTP/1.1 200` o `HTTP/1.0 200`):
    - `cache_write` escribe el archivo `.cache` con el response completo (incluye headers + body) y un `.meta` con `expiry = time(NULL) + cache_ttl`.
    - Loguea `CACHE: MISS — response cached`.
    - Respuestas con códigos distintos a 200 (3xx, 4xx, 5xx) no se cachean.

11. `free(resp_buf)`, `close(client_fd)`, retorna.

### 5.3. Códigos que PIBL puede emitir directamente

| Código | Cuándo |
|---|---|
| 400 Bad Request | request line del cliente malformada |
| 502 Bad Gateway | `socket()` o `connect()` al backend falla |
| 503 Service Unavailable | no hay backends configurados |
| 500 Internal Server Error | `malloc` del buffer de respuesta falla |

Para todo lo demás, el código viene del backend tal cual (passthrough).

---

## 6. Protocolo HTTP

### 6.1. Métodos soportados (TWS)

| Método | Comportamiento |
|---|---|
| GET | Sirve archivo |
| HEAD | Sirve sólo headers |
| POST | Lee body y responde "OK" (no procesa el body) |
| Cualquier otro | 400 |

### 6.2. Headers que TWS interpreta

- **`Content-Length:`** sólo en POST, para saber cuántos bytes leer del body.

No interpreta `Connection`, `Host`, `User-Agent`, `Accept-Encoding`, etc. Siempre cierra la conexión (`Connection: close`).

### 6.3. MIME types (TWS, en `mime.c`)

Detección por extensión (case-insensitive vía `strcasecmp`):

| Extensión | MIME |
|---|---|
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

### 6.4. Versión

Acepta cualquier versión en la request line (la captura en `version[16]` pero no la valida). Siempre responde `HTTP/1.1`.

---

## 7. Balanceador (PIBL)

- **Algoritmo**: Round Robin puro, índice global protegido por mutex.
- **Estado**: `backends[16]`, `backend_count`, `rr_index` — todos en `balancer.c`.
- **No hay health checks**: si un backend está caído, PIBL devuelve 502 a ese cliente y avanza igual al siguiente. La siguiente petición que toque ese mismo backend volverá a fallar.
- **No hay sticky sessions**, ni weighted RR, ni least-connections.
- **No reintenta** automáticamente con otro backend ante fallo.

Configuración (`pibl/config.txt`):
```
port=8080
ttl=60
backend=127.0.0.1:8081
backend=127.0.0.1:8082
backend=127.0.0.1:8083
```

---

## 8. Sistema de caché (PIBL)

- **Ubicación**: `./cache/` relativo al cwd donde se lanza `pibl`. **Hardcoded** — `cache_dir` se inicializa así, no se lee de config.
- **Granularidad**: por URI (no por backend). Una respuesta cacheada se reusa para futuras peticiones al mismo URI sin importar a qué backend habría tocado.
- **Política**: caché por TTL absoluto. No usa `Cache-Control`, `ETag`, `If-Modified-Since`, ni nada del HTTP. El TTL viene del `config.txt`.
- **Qué cachea**: sólo respuestas GET con primera línea `HTTP/1.1 200` o `HTTP/1.0 200`. Cualquier otra respuesta (404, 500, redirects) no se cachea.
- **Qué guarda**: la respuesta HTTP completa (headers + body) en `.cache`. Al servir un HIT, vuelca ese archivo tal cual al cliente, así que conserva el `Content-Type` y demás headers originales.
- **Estructura en disco** por entrada:
  - `./cache/<uri_sanitizado>.cache` — bytes raw de la respuesta.
  - `./cache/<uri_sanitizado>.cache.meta` — un `long` con timestamp Unix de expiración.
- **Sanitización del URI**: `/`, `?`, `&`, `=` se reemplazan por `_`. Otros caracteres se mantienen literales — un URI con caracteres no-imprimibles podría producir nombres extraños pero no traversal (no hay `..` que llegue al filesystem porque el `/` ya se neutralizó).
- **Invalidación**: lazy. Al consultar un caché expirado, `cache_is_valid` borra `.cache` y `.meta` y retorna 0. No hay GC de fondo.

---

## 9. Logging

### 9.1. Comportamiento (`logger.c`, idéntico en tws/ y pibl/)

- `log_init(path)` hace `fopen(path, "a")`. Append-only, sin rotación.
- `log_msg(level, msg)` formatea: `[YYYY-MM-DD HH:MM:SS] [LEVEL] msg\n`.
  - Escribe a `stdout` con `fflush`.
  - Escribe al archivo de log con `fflush`.
  - Mutex `log_mutex` envuelve ambas escrituras → thread-safe.
- Niveles usados: `INFO`, `REQ`, `RES`, `WARN`, `PROXY`, `CACHE`, `ERROR`. No hay filtrado por nivel.

### 9.2. Eventos logueados

**TWS:**
- Arranque (puerto, root, log).
- Cada conexión aceptada (IP del cliente).
- Cada request line.
- Cada respuesta servida (path).
- Path traversal blocked.
- Método desconocido.

**PIBL:**
- Arranque (puerto, count, ttl, cache).
- Lista de backends al arranque.
- Cada conexión aceptada (IP).
- Cada request line proxieada.
- Backend escogido para cada request.
- Cache HIT / MISS.
- Errores de conexión a backends.

---

## 10. Bugs y limitaciones conocidas

### 10.1. Use-after-free en `proxy_thread` (preexistente, **persiste tras refactor**)

`pibl/proxy.c:20-25`:
```c
ConnData *cd = (ConnData *)arg;
int client_fd = cd->fd;
free(cd);                                              // ← libera cd

char ip[INET_ADDRSTRLEN];
inet_ntop(AF_INET, &((struct sockaddr_in *)&cd)->sin_addr, ip, sizeof(ip)); // ← UB
```

Tres problemas concurrentes:
1. `cd` ya fue liberado por `free`.
2. `&cd` es la dirección del **puntero local** `cd`, no del struct. Aunque `cd` no estuviera liberado, esto no apuntaría al `sockaddr_in`. Reinterpreta los bytes del puntero como un `sockaddr_in` (UB).
3. El resultado en `ip[]` no se usa nunca después de calcularse.

Por qué el programa no crashea: la línea es esencialmente un no-op visible — `inet_ntop` lee bytes basura pero no los usa el resto del flujo. El logueo de la IP del cliente sí funciona pero ocurre **antes**, en `pibl.c` dentro del accept loop, donde sí está bien escrito.

**Fix sugerido (no aplicado en este refactor)**: o se elimina toda la línea, o se mueve el `inet_ntop` antes del `free(cd)` y se usa el resultado.

### 10.2. Otras limitaciones por diseño

- **Sin keep-alive**: cada request abre y cierra TCP. Costoso para el cliente.
- **Sin chunked encoding**: PIBL asume `Content-Length`. Un request POST con `Transfer-Encoding: chunked` no se forwardearía completo.
- **Sin timeouts** de socket: un cliente que abre conexión y no envía nada bloquea un hilo indefinidamente. Misma situación con un backend lento.
- **POST en TWS no procesa nada**: lee el body para limpiar el socket, pero responde un literal `"OK"` con `Content-Length: 2`. No persiste el body, no responde con eco del body, no soporta `multipart/form-data`.
- **Path traversal**: la protección es `strstr(uri, "..")`. Cualquier URI que contenga `..` se rechaza, incluso un nombre de archivo legítimo como `..weird..name.txt`. Robusto pero algo agresivo.
- **Caché ignora variantes**: dos clientes con `Accept-Language` distintos comparten la misma entrada de caché.
- **Buffer fijo para POST**: `body_buf[BUFFER_SIZE]` en `handler.c` limita el body POST visible a TWS a 8191 bytes. Bodies más grandes se truncan al copiar al buffer; el resto del body se sigue leyendo del socket pero se descarta. Esto no afecta la respuesta `"OK"` que de todos modos es fija.
- **`MAX_PATH`** se define como `1024` en `tws/http_parser.h` y `pibl/config.h`. No es el `PATH_MAX` del sistema; es un límite arbitrario interno.
- **Warnings de truncación de `snprintf`**: gcc detecta que en algunos `snprintf` el peor caso teórico podría exceder el buffer. En la práctica los datos reales caben (URIs cortos, cache_dir corto). Se dejaron sin tocar para no cambiar comportamiento.

---

## 11. Cómo se compila y se ejecuta

### Build

```bash
make clean && make    # produce tws/tws y pibl/pibl
```

El Makefile compila cada `.c` a `.o` con `gcc -Wall`, después linkea con `-lpthread`.

### Ejecución manual

```bash
# Levantar 3 backends TWS
./tws/tws 8081 /tmp/tws1.log ./web_content/case1 &
./tws/tws 8082 /tmp/tws2.log ./web_content/case1 &
./tws/tws 8083 /tmp/tws3.log ./web_content/case1 &

# Levantar el proxy
./pibl/pibl pibl/config.txt /tmp/pibl.log &

# Probar
curl http://localhost:8080/index.html        # 200 + HTML
curl -I http://localhost:8080/index.html     # 200 + headers (HEAD)
curl -X POST http://localhost:8080/ -d "x=1" # 200 OK
curl http://localhost:8080/noexiste          # 404
```

### Observación del round robin

```bash
for i in 1 2 3 4 5 6; do
  curl -s -o /dev/null "http://localhost:8080/index.html?n=$i"
done
tail -20 /tmp/pibl.log    # se ven las líneas "Forwarding to 127.0.0.1:808X"
                          # alternando 8081 → 8082 → 8083 → 8081 ...
```

### Observación del caché

```bash
rm -rf cache && mkdir cache
curl -s http://localhost:8080/index.html > /dev/null   # MISS
curl -s http://localhost:8080/index.html > /dev/null   # HIT
ls cache/                                              # _index.html.cache + .meta
grep CACHE /tmp/pibl.log
```
