# Makefile para cliente FTP concurrente (BravoL-clienteFTP)

# Compilador a usar
CC = gcc

# Banderas de compilación:
# -g : Agrega símbolos de depuración (para gdb)
# -Wall : Muestra todos los warnings (¡muy recomendado!)
CFLAGS = -g -Wall

# Lista de archivos objeto
CLTOBJ = BravoL-clienteFTP.o connectsock.o connectTCP.o passivesock.o passiveTCP.o errexit.o

# El ejecutable final
TARGET = BravoL-clienteFTP

# La regla por defecto: compilar todo
all: $(TARGET)

# Regla para enlazar el ejecutable final
$(TARGET): $(CLTOBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(CLTOBJ)

# Regla para limpiar (elimina objetos Y el ejecutable)
clean:
	rm -f $(CLTOBJ) $(TARGET)
