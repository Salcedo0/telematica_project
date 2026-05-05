# PIBL-WS: Proxy Inverso, Balanceador de Carga y Web Server

Proyecto desarrollado para la asignatura Telemática/Internet: Arquitectura y Protocolos.

## 1. Introducción

Este proyecto implementa una arquitectura cliente/servidor compuesta por un Proxy Inverso + Balanceador de Carga, denominado PIBL, y tres servidores web backend denominados TWS.

El objetivo principal es recibir peticiones HTTP/1.1 desde clientes como navegador, Postman, curl o telnet, procesarlas mediante sockets, distribuirlas entre varios servidores web y retornar la respuesta correspondiente al cliente.

La solución fue desarrollada usando la API Sockets y contempla concurrencia, balanceo Round Robin, sistema de logs, caché persistente en disco y despliegue en instancias EC2 de AWS.

## 2. Arquitectura general

La arquitectura está compuesta por los siguientes elementos:

- Cliente HTTP: navegador, Postman, curl o telnet.
- PIBL: Proxy Inverso + Balanceador de Carga.
- Servidores TWS: tres servidores web que contienen la misma aplicación web.
- Caché local: almacenamiento en disco de recursos solicitados.
- Logs: registro de peticiones y respuestas en consola y archivo.

Flujo general:

1. El cliente envía una petición HTTP al PIBL.
2. El PIBL recibe la conexión mediante sockets.
3. El PIBL revisa si el recurso está disponible en caché y si su TTL sigue vigente.
4. Si el recurso está en caché, responde directamente al cliente.
5. Si no está en caché, selecciona un servidor backend usando Round Robin.
6. El PIBL abre un nuevo socket hacia el servidor web seleccionado.
7. El servidor TWS procesa la petición y retorna una respuesta HTTP.
8. El PIBL almacena la respuesta en caché si aplica.
9. El PIBL devuelve la respuesta final al cliente.

## 3. Protocolos y conceptos usados

### HTTP/1.1

La solución procesa peticiones bajo el protocolo HTTP/1.1. Se soportan los métodos:

- GET: solicita un recurso y retorna su contenido.
- HEAD: solicita los encabezados del recurso, sin retornar cuerpo.
- POST: permite enviar datos al servidor mediante el cuerpo de la petición.

### API Sockets

La comunicación entre cliente, PIBL y servidores backend se realiza mediante sockets TCP. El PIBL actúa como servidor frente al cliente y como cliente frente a los servidores web backend.

### Round Robin

El balanceo de carga se implementa mediante la estrategia Round Robin. Cada nueva petición que no pueda ser respondida desde caché es enviada al siguiente servidor disponible en la lista de backends.

Ejemplo:

```txt
Petición 1 -> Servidor 1
Petición 2 -> Servidor 2
Petición 3 -> Servidor 3
Petición 4 -> Servidor 1
