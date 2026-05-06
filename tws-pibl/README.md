# PIBL-WS: Proxy Inverso + Balanceador de Carga + Web Server

## 1. Introducción

Este proyecto implementa un servidor web HTTP/1.1 (TWS) y un proxy inverso con balanceador de carga (PIBL), desarrollados en lenguaje C usando la API de sockets POSIX. La arquitectura permite distribuir peticiones HTTP entre múltiples servidores backend de forma concurrente, con soporte de caché en disco y registro de logs.

## 2. Desarrollo

### Arquitectura

- **PIBL** (puerto 8080): recibe peticiones del cliente, aplica Round Robin entre 3 backends, implementa caché en disco con TTL configurable.
- **TWS** (puertos 8081/8082/8083): servidor web que sirve archivos estáticos, soporta GET, HEAD y POST.

### Componentes

**TWS - Telematics Web Server**
- Lenguaje: C con API Sockets POSIX
- Concurrencia: un hilo POSIX (pthread) por conexión
- Métodos: GET, HEAD, POST
- Códigos HTTP: 200, 400, 404
- Logger thread-safe con mutex (stdout + archivo)
- Ejecución: `./tws <PORT> <LogFile> <DocumentRootFolder>`

**PIBL - Proxy Inverso + Balanceador de Carga**
- Lenguaje: C con API Sockets POSIX
- Balanceo: Round Robin entre backends configurables
- Caché: almacenamiento en disco con TTL parametrizable
- Concurrencia: un hilo por conexión entrante
- Archivo de configuración: puerto, TTL, lista de backends
- Logger thread-safe (stdout + archivo)
- Ejecución: `./pibl <ConfigFile> <LogFile>`

### Archivo de configuración (pibl/config.txt)
port=8080
ttl=60
backend=<IP>:8081
backend=<IP>:8082
backend=<IP>:8083

### Compilación

```bash
make
```

### Ejecución local

```bash
# Iniciar 3 instancias TWS
./tws/tws 8081 /tmp/tws1.log ./web_content/case1 &
./tws/tws 8082 /tmp/tws2.log ./web_content/case1 &
./tws/tws 8083 /tmp/tws3.log ./web_content/case1 &

# Iniciar PIBL
./pibl/pibl pibl/config.txt /tmp/pibl.log &
```

### Casos de prueba

| Caso | Descripción |
|------|-------------|
| case1 | Página HTML con hipertextos e imagen |
| case2 | Página HTML con múltiples imágenes |
| case3 | Archivo único de ~1MB |
| case4 | Múltiples archivos con tamaño total ~1MB |

### Despliegue en AWS

- 4 instancias EC2 Ubuntu (t2.micro)
- 1 instancia PIBL con IP pública
- 3 instancias TWS en red privada
- Puertos abiertos: 8080 (PIBL), 8081-8083 (TWS)

## 3. Conclusiones

- Se implementó exitosamente un proxy inverso y balanceador de carga en C puro usando sockets POSIX.
- El mecanismo de caché en disco con TTL reduce la carga en los backends para recursos frecuentemente solicitados.
- La concurrencia basada en pthreads permite atender múltiples clientes simultáneamente.
- El protocolo HTTP/1.1 fue implementado correctamente con soporte para los métodos GET, HEAD y POST.

## 4. Referencias

- RFC 2616 - HTTP/1.1: https://datatracker.ietf.org/doc/rfc2616/
- Beej's Guide to Network Programming: https://beej.us/guide/bgnet/
- POSIX Threads Programming: https://hpc-tutorials.llnl.gov/posix/
- AWS EC2 Documentation: https://docs.aws.amazon.com/ec2/