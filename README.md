# Cliente FTP Concurrente (BravoL-clienteFTP)

**Materia:** Computación Distribuida (EPN)  
**Estándar:** RFC 959

## Descripción

Este proyecto implementa un cliente FTP concurrente capaz de interactuar con servidores FTP estándar (como `vsftpd`). Su arquitectura utiliza `fork()` para manejar transferencias de datos en procesos hijos independientes, permitiendo que la consola de comandos principal permanezca receptiva mientras se realizan subidas o descargas pesadas en segundo plano.

El cliente soporta los dos modos de transferencia del protocolo FTP:
1.  **Modo Pasivo (PASV):** Utilizado por defecto en `get`, `put` y `dir`. El cliente inicia la conexión de datos hacia el servidor.
2.  **Modo Activo (PORT):** Implementado en el comando `pput`. El cliente abre un puerto de escucha y solicita al servidor que se conecte a él.

## Archivos del Proyecto

Para una correcta compilación, asegúrate de tener los siguientes archivos fuente en el mismo directorio:

* `BravoL-clienteFTP.c`: Código principal del cliente.
* `connectsock.c`: Lógica base para conexión de sockets.
* `connectTCP.c`: Wrapper para conexiones TCP salientes.
* `passivesock.c`: Lógica base para creación de sockets servidores.
* `passiveTCP.c`: Wrapper para creación de sockets de escucha TCP.
* `errexit.c`: Manejo de errores y salida del programa.

## Configuración del Entorno (Servidor y Cliente)

### 1. Instalación del Servidor (vsftpd)

Este cliente fue probado contra `vsftpd` (Very Secure FTP Daemon). Para instalar y configurar el servidor en un sistema Debian/Ubuntu (o WSL):

```bash
# 1. Instalar el paquete
sudo apt update
sudo apt install vsftpd

# 2. Iniciar el servicio
sudo systemctl start vsftpd

# 3. (Opcional) Verificar que esté corriendo
sudo systemctl status vsftpd
```

## 2. Compilación del Cliente

El proyecto incluye un Makefile. Para compilar el cliente, primero asegúrate de tener las herramientas de compilación:

```bash
# Instalar gcc, make, etc.
sudo apt install build-essential
```

Luego, simplemente compila:

```bash
make
```

Esto generará el ejecutable `BravoL-clienteFTP`.

---

## A Considerar (Ejecución y Permisos)

Para que el cliente funcione correctamente, es crucial entender cómo interactúan los permisos del sistema local (tu máquina) y del servidor (el host FTP).

### Permisos Locales (Para Descargas con `get`)

**Problema:** Si usas `get` y recibes un error local como "Failed to open file" o "Permission denied", es porque tu cliente no tiene permiso para crear archivos en el directorio desde el cual lo ejecutaste.

**Solución:** Ejecuta siempre el cliente desde un directorio local donde sí tengas permisos de escritura.

```bash
# 1. Crea y entra a un directorio de prueba local (test1)
mkdir ~/test1
cd ~/test1

# 2. Ejecuta el cliente usando su ruta
# (Asumiendo que el código está en ~/Compudistr)
~/Compudistr/BravoL-clienteFTP localhost
```

De esta forma, los archivos descargados se guardarán en `~/test1` sin problemas.

---

### Permisos del Servidor (Para Subidas con `put`)

**Problema:** Si usas `put` y recibes el error `550 Permission denied`, esto es un rechazo del servidor y no un bug de tu cliente. Significa que el usuario FTP con el que te conectaste no tiene permiso de escritura en el directorio actual del servidor.

Esto suele tener dos causas principales:

1. **Configuración de vsftpd (El más común):** Por defecto, `vsftpd` es muy seguro y deshabilita las subidas.

**Solución:** Edita el archivo de configuración del servidor.

```bash
sudo nano /etc/vsftpd.conf
```

Busca la línea `write_enable=YES`. Asegúrate de que no esté comentada (sin `#`) y esté en `YES`.

Guarda el archivo y reinicia el servicio para que aplique los cambios:

```bash
sudo systemctl restart vsftpd
```

2. **Permisos del Directorio en el Servidor:** Si `write_enable` está en `YES` pero sigue fallando, es un problema de permisos del sistema de archivos.

**Diagnóstico:** Usa `dir` en tu cliente FTP y mira los permisos (ej. `drwxr-xr-x`). Si tu usuario no es el "dueño" y los permisos de "otros" son `r-x`, te falta el permiso de escritura (`w`).

**Solución (en el servidor):**

```bash
# Opción 1 (Recomendada): Haz a tu usuario el dueño
sudo chown tu_usuario /home/tu_usuario

# Opción 2 (Rápida): Da permisos de escritura a "otros"
sudo chmod o+w /home/tu_usuario
```
